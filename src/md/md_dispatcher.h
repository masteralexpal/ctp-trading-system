/**
 * @file md_dispatcher.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard MdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#pragma once

#ifndef CTS_MD_MD_DISPATCHER_H_
#define CTS_MD_MD_DISPATCHER_H_

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

#include "base/spsc_queue.h"
#include "md/detail/md_event.h"
#include "md/md_client.h"
#include "md/md_delegate.h"

namespace cts::md {

// Standard MdDelegate: on the CTP callback thread it only boxes and enqueues;
// the consumer loop Run() is placed on a dedicated thread by the assembler
// (this class spawns nothing).
//
// Assembly order: AddClient* -> base::Thread.Start([&]{ d.Run(); }) ->
// MdGateway::Connect -> ... -> MdGateway::Disconnect ->
// RequestStop -> Thread.Stop
class MdDispatcher final : public MdDelegate {
 public:
  MdDispatcher() = default;
  ~MdDispatcher() override = default;

  MdDispatcher(MdDispatcher const&) = delete;
  MdDispatcher& operator=(MdDispatcher const&) = delete;

  // Call only before Run
  void AddClient(MdClient& client) { clients_.push_back(&client); }

  // Consumer loop: runs on the caller's thread until RequestStop and drained
  void Run();
  // Idempotent, callable from any thread; Run returns after draining
  void RequestStop() { stop_requested_.store(true, std::memory_order_release); }

  // Total events dropped on queue overflow (backpressure metric)
  std::size_t dropped_events() const { return queue_.dropped(); }

  // Login readiness, owned here (first to know, not a debug client's side
  // job). Written on the dispatch thread; readable from any thread
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
  void Push(detail::MdEvent event);
  void Dispatch(detail::MdEvent const& event);

  base::SpscQueue<detail::MdEvent, 4096> queue_;
  std::vector<MdClient*> clients_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> logged_in_{false};  // dispatch-thread written
};

}  // namespace cts::md

#endif  // CTS_MD_MD_DISPATCHER_H_
