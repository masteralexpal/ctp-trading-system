/**
 * @file sim_order_sink.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Simulated OrderSink for backtests: touch-fill on replayed ticks.
 */
#pragma once

#ifndef CTS_BACKTEST_SIM_ORDER_SINK_H_
#define CTS_BACKTEST_SIM_ORDER_SINK_H_

#include <string>
#include <string_view>
#include <vector>

#include "recorder/raw_tick_file.h"
#include "td/order_sink.h"
#include "td/td_client.h"

namespace cts::backtest {

// Simulated execution: touch-fill (a buy fills when ask1 <= limit, a sell
// when bid1 >= limit) at the limit price, full volume. Optimistic on purpose
// for v1; queue-position realism is a later iteration.
//
// Fires TdClient events to registered clients (strategy + OMS) with the CTP
// ordering quirk preserved: the all-traded order report precedes the trade
// report. Single-threaded: the replay loop calls everything.
class SimOrderSink final : public td::OrderSink {
 public:
  void AddClient(td::TdClient& client) { clients_.push_back(&client); }

  // Fires OnTdOrder(kSubmitted) immediately; the order rests until a tick
  // touches the limit
  std::string InsertOrder(td::OrderRequest const& req) override;
  int CancelOrder(std::string_view order_ref) override;

  // Drive fills from the raw tick. Call BEFORE handing the tick to
  // strategies: an order resting in the book reacts to the tick, an order
  // born of the tick must not fill on it
  void OnTick(CThostFtdcDepthMarketDataField const& tick);

  std::size_t working_count() const { return working_.size(); }

 private:
  struct Working {
    td::OrderRequest req;
    std::string ref;
  };
  void FireOrder(td::OrderReport const& report);
  void FireTrade(td::TradeReport const& trade);

  std::vector<td::TdClient*> clients_;
  std::vector<Working> working_;
  int next_ref_{1};
};

}  // namespace cts::backtest

#endif  // CTS_BACKTEST_SIM_ORDER_SINK_H_
