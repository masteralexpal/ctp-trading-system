/**
 * @file strategy.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Strategy base: consumes ticks and order events, emits orders.
 */
#pragma once

#ifndef CTS_STRATEGY_STRATEGY_H_
#define CTS_STRATEGY_STRATEGY_H_

#include <atomic>
#include <string>

#include "md/md_client.h"
#include "oms/oms_listener.h"

namespace cts::strategy {

// A strategy is an MdClient (ticks in) plus an OmsListener (order/trade
// effects in); it emits orders through a td::OrderSink. Trading events arrive
// as OMS EFFECTS, not raw broadcast: replays and stale reports never reach
// the strategy, and a terminal "flight over" fires only after the fills it
// announced are already in the ledger.
//
// Threading: OnTick runs on the md-dispatch thread, On* effects on the
// td-dispatch thread -- two threads touch one strategy. Concrete strategies
// must synchronize their own state (a mutex is fine at this scale). The
// upgrade path is a per-strategy event funnel thread.
//
// The base signs both contracts with deliberate no-ops; concrete strategies
// override only what they consume.
class Strategy : public md::MdClient, public oms::OmsListener {
 public:
  explicit Strategy(std::string name) : name_(std::move(name)) {}
  ~Strategy() override = default;

  Strategy(Strategy const&) = delete;
  Strategy& operator=(Strategy const&) = delete;

  // Any thread. Stopped strategies ignore ticks
  void Start() { running_.store(true, std::memory_order_release); }
  void Stop() { running_.store(false, std::memory_order_release); }
  bool running() const { return running_.load(std::memory_order_acquire); }
  std::string const& name() const { return name_; }

  // md::MdClient
  void OnMdConnected() override {}
  void OnMdDisconnected(int /*reason*/) override {}
  void OnMdLogin(bool /*success*/, std::string_view /*msg*/) override {}
  void OnTick(md::Tick const& /*tick*/) override {}
  void OnMdError(int /*error_id*/, std::string_view /*msg*/) override {}

  // oms::OmsListener
  void OnOrderUpdate(td::OrderReport const& /*report*/) override {}
  void OnTradeApplied(td::TradeReport const& /*trade*/) override {}
  void OnPositionsReconciled(
      std::shared_ptr<std::vector<td::PositionRow> const> /*rows*/) override {}

 private:
  std::string const name_;
  std::atomic<bool> running_{false};
};

}  // namespace cts::strategy

#endif  // CTS_STRATEGY_STRATEGY_H_
