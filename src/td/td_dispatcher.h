/**
 * @file td_dispatcher.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard TdDelegate: box-and-post, fan out on a task loop.
 */
#pragma once

#ifndef CTS_TD_TD_DISPATCHER_H_
#define CTS_TD_TD_DISPATCHER_H_

#include <atomic>
#include <utility>
#include <vector>

#include "base/task/task_loop.h"
#include "td/detail/td_event.h"
#include "td/td_client.h"
#include "td/td_delegate.h"

namespace cts::td {

// Standard TdDelegate: on the CTP callback thread it only boxes and posts a
// task; the fan-out runs on the dispatcher's own TaskLoop, placed on a
// dedicated thread by the assembler (this class spawns nothing).
//
// Task-centric: td events are LOW rate (reports + query answers), so the
// TaskLoop buys timers (PostDelayedTask) for free -- query throttling and
// reconnect backoff live here, not on the main loop. The md dispatcher keeps
// its SPSC+futex design: tick rate justifies zero-allocation POD queues.
//
// Assembly order: AddClient* -> base::Thread.Start([&]{ d.Run(); }) ->
// TdGateway::Connect -> ... -> TdGateway::Disconnect ->
// RequestStop -> Thread.Stop
class TdDispatcher final : public TdDelegate {
 public:
  TdDispatcher() : loop_("td-dispatch") {}
  ~TdDispatcher() override = default;

  TdDispatcher(TdDispatcher const&) = delete;
  TdDispatcher& operator=(TdDispatcher const&) = delete;

  // Call only before Run
  void AddClient(TdClient& client) { clients_.push_back(&client); }

  // Consumer loop: runs on the caller's thread until RequestStop
  void Run() { loop_.Run(); }
  // Idempotent, any thread; Run drains remaining tasks then returns
  void RequestStop() { loop_.RequestStop(); }

  // The dispatcher's task loop: query throttling and reconnect timers post
  // their delayed tasks here (any-thread safe)
  base::TaskLoop& loop() { return loop_; }

  // Readiness (dispatch-thread written, readable from any thread)
  bool ready() const { return ready_.load(std::memory_order_acquire); }

  // TdDelegate: all of the following run on the CTP callback thread
  void OnTdConnected() override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kConnected;
    Push(std::move(e));
  }
  void OnTdDisconnected(int reason) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kDisconnected;
    e.code = reason;
    Push(std::move(e));
  }
  void OnTdReady(bool success, std::string_view msg) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kReady;
    e.success = success;
    e.msg = msg;
    Push(std::move(e));
  }
  void OnTdOrder(OrderReport const& order) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kOrder;
    e.order = order;
    Push(std::move(e));
  }
  void OnTdTrade(TradeReport const& trade) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kTrade;
    e.trade = trade;
    Push(std::move(e));
  }
  void OnTdPositions(
      std::shared_ptr<std::vector<PositionRow> const> rows) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kPositions;
    e.positions = std::move(rows);
    Push(std::move(e));
  }
  void OnTdAccount(AccountReport const& account) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kAccount;
    e.account = account;
    Push(std::move(e));
  }
  void OnTdError(int error_id, std::string_view msg) override {
    detail::TdEvent e;
    e.kind = detail::TdEvent::Kind::kError;
    e.code = error_id;
    e.msg = msg;
    Push(std::move(e));
  }

 private:
  void Push(detail::TdEvent event);
  void Dispatch(detail::TdEvent const& event);

  base::TaskLoop loop_;
  std::vector<TdClient*> clients_;
  std::atomic<bool> ready_{false};  // dispatch-thread written
};

}  // namespace cts::td

#endif  // CTS_TD_TD_DISPATCHER_H_
