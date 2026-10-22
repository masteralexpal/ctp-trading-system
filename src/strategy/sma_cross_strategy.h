/**
 * @file sma_cross_strategy.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Dual-SMA crossover strategy (single instrument, ±1 position).
 */
#pragma once

#ifndef CTS_STRATEGY_SMA_CROSS_STRATEGY_H_
#define CTS_STRATEGY_SMA_CROSS_STRATEGY_H_

#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

#include "strategy/strategy.h"
#include "td/order_sink.h"

namespace cts::strategy {

struct SmaCrossConfig {
  std::string instrument_id;
  std::string exchange_id;
  int fast{5};     // fast SMA window (ticks)
  int slow{20};    // slow SMA window (ticks)
  int volume{1};   // lots per order
  int max_pos{1};  // absolute position cap (signed)
  // Stale working orders are canceled by the OMS watchdog
  // (OrderManager::EnableOrderTimeout), not by this strategy
};

// Dual-SMA crossover on tick last_price: fast crossing above slow goes long,
// crossing below goes short (both capped at max_pos, one working order at a
// time). Entries/exits use aggressive limits: ask for buys, bid for sells.
//
// Close offset: the strategy just guesses (close-today on SHFE/INE, plain
// close elsewhere); the risk sink re-splits every close against the ledger's
// today/yesterday buckets, so a wrong guess is corrected downstream.
class SmaCrossStrategy final : public Strategy {
 public:
  SmaCrossStrategy(SmaCrossConfig config, td::OrderSink& order_sink);

  void OnTick(md::Tick const& tick) override;  // md-dispatch thread
  // oms::OmsListener (td-dispatch thread; terminal updates arrive only after
  // the fills they announced, so position_ is current when a flight ends)
  void OnOrderUpdate(td::OrderReport const& order) override;
  void OnTradeApplied(td::TradeReport const& trade) override;
  // Resyncs the virtual position from the broker snapshot (restart/reconnect
  // recovery); skipped while an order is in flight -- the snapshot may lag
  // its fill
  void OnPositionsReconciled(
      std::shared_ptr<std::vector<td::PositionRow> const> rows) override;

  struct Status {
    int position;
    bool order_in_flight;
    double fast_sma;
    double slow_sma;
    std::string working_ref;
  };
  // Any thread
  Status status() const;

 private:
  // Guards all mutable state below (two dispatcher threads converge here)
  mutable std::mutex mutex_;
  SmaCrossConfig const config_;
  td::OrderSink& order_sink_;

  std::deque<double> prices_;
  // Sign of (fast - slow) at the last ACTED-UPON tick; deliberately not
  // updated while an order is in flight, so a stale prev re-fires the intent
  // (chase or reversal) the tick after the flight ends
  int prev_sign_{0};
  double fast_sma_{0.0};
  double slow_sma_{0.0};
  int position_{0};  // signed net position from fills
  bool order_in_flight_{false};
  std::string working_ref_;
  td::Direction working_dir_{td::Direction::kBuy};
  bool cancel_sent_{false};  // one cancel per flight, no spam
  // Every ref this strategy ever submitted: fills are matched against this
  // set, NOT working_ref_ -- CTP sends OnRtnOrder(all-traded) BEFORE
  // OnRtnTrade, so the working ref is already cleared when the fill arrives
  std::unordered_set<std::string> own_refs_;
};

}  // namespace cts::strategy

#endif  // CTS_STRATEGY_SMA_CROSS_STRATEGY_H_
