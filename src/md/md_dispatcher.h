/**
 * @file md_dispatcher.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief MdDispatcher: an SpscLoop<MdEvent> pump plus the client fan-out.
 */
#pragma once

#ifndef CTS_MD_MD_DISPATCHER_H_
#define CTS_MD_MD_DISPATCHER_H_

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

#include "base/threading/run_loop.h"
#include "base/threading/spsc_loop.h"
#include "md/detail/md_event.h"
#include "md/md_client.h"
#include "md/md_delegate.h"

namespace cts::md {

// Standard MdDelegate: on the CTP callback thread it only boxes and pushes;
// the pump is a base::SpscLoop (single producer = the callback thread),
// placed on a dedicated thread by the assembler (this class spawns nothing).
// The dispatcher itself is just the fan-out policy over the loop: Dispatcher
// = RunLoop + clients.
//
// Assembly order: AddClient* -> base::Thread.Start([&]{ d.Run(); }) ->
// MdGateway::Connect -> ... -> MdGateway::Disconnect ->
// RequestStop -> Thread.Stop
class MdDispatcher final : public MdDelegate {
 public:
  MdDispatcher()
      : loop_("md-dispatch",
              [this](detail::MdEvent const& e) { Dispatch(e); }) {}
  ~MdDispatcher() override = default;

  MdDispatcher(MdDispatcher const&) = delete;
  MdDispatcher& operator=(MdDispatcher const&) = delete;

  // Call only before Run
  void AddClient(MdClient& client) { clients_.push_back(&client); }

  // Consumer loop: runs on the caller's thread until RequestStop and drained
  void Run() { loop_.Run(); }
  // Idempotent, any thread; wakes the parked loop
  void RequestStop() { loop_.RequestStop(); }

  // The pump itself, for fleet operations (unified shutdown, health checks)
  base::RunLoop& loop() { return loop_; }

  // Total events dropped on queue overflow (backpressure metric)
  std::size_t dropped_events() const { return loop_.dropped(); }

  // Login readiness (dispatch-thread written, readable from any thread)
  bool logged_in() const { return logged_in_.load(std::memory_order_acquire); }

  // MdDelegate: all of the following run on the CTP callback thread
  void OnMdConnected() override {
    detail::MdEvent e;
    e.kind = detail::MdEvent::Kind::kConnected;
    Push(std::move(e));
  }
  void OnMdDisconnected(int reason) override {
    detail::MdEvent e;
    e.kind = detail::MdEvent::Kind::kDisconnected;
    e.code = reason;
    Push(std::move(e));
  }
  void OnMdLogin(bool success, std::string_view msg) override {
    detail::MdEvent e;
    e.kind = detail::MdEvent::Kind::kLogin;
    e.success = success;
    e.msg = msg;
    Push(std::move(e));
  }
  void OnMdTick(Tick const& tick) override {
    detail::MdEvent e;
    e.kind = detail::MdEvent::Kind::kTick;
    e.tick = tick;
    Push(std::move(e));
  }
  void OnMdError(int error_id, std::string_view msg) override {
    detail::MdEvent e;
    e.kind = detail::MdEvent::Kind::kError;
    e.code = error_id;
    e.msg = msg;
    Push(std::move(e));
  }

 private:
  // Full queue drops the newest (snapshot semantics), counted by the loop
  void Push(detail::MdEvent event) { loop_.TryPush(std::move(event)); }
  void Dispatch(detail::MdEvent const& event);

  base::SpscLoop<detail::MdEvent, 4096> loop_;
  std::vector<MdClient*> clients_;
  std::atomic<bool> logged_in_{false};  // dispatch-thread written
};

}  // namespace cts::md

#endif  // CTS_MD_MD_DISPATCHER_H_
