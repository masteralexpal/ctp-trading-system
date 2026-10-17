/**
 * @file td_dispatcher.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard TdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#pragma once

#ifndef CTS_TD_TD_DISPATCHER_H_
#define CTS_TD_TD_DISPATCHER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "base/spsc_queue.h"
#include "td/detail/td_event.h"
#include "td/td_client.h"
#include "td/td_delegate.h"

namespace cts::td {

// Standard TdDelegate: on the CTP callback thread it only boxes and enqueues;
// the consumer loop Run() is placed on a dedicated thread by the assembler
// (this class spawns nothing).
//
// Assembly order: AddClient* -> base::Thread.Start([&]{ d.Run(); }) ->
// TdGateway::Connect -> ... -> TdGateway::Disconnect ->
// RequestStop -> Thread.Stop
class TdDispatcher final : public TdDelegate {
 public:
  TdDispatcher() = default;
  ~TdDispatcher() override = default;

  TdDispatcher(TdDispatcher const&) = delete;
  TdDispatcher& operator=(TdDispatcher const&) = delete;

  // Call only before Run
  void AddClient(TdClient& client) { clients_.push_back(&client); }

  // Consumer loop: parks on a futex when empty (zero CPU); runs on the
  // caller's thread until RequestStop and drained
  void Run();
  // Idempotent, any thread; wakes the parked loop; Run drains then returns
  void RequestStop();

  // Total events dropped on queue overflow (backpressure metric)
  std::size_t dropped_events() const { return queue_.dropped(); }

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

  base::SpscQueue<detail::TdEvent, 4096> queue_;
  std::vector<TdClient*> clients_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> ready_{false};      // dispatch-thread written
  std::atomic<std::uint32_t> wake_{0};  // futex parking word
  std::atomic<bool> parked_{false};     // gates producer-side wakes
};

}  // namespace cts::td

#endif  // CTS_TD_TD_DISPATCHER_H_
