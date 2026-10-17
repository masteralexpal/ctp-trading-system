/**
 * @file td_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard TdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#include "td/td_dispatcher.h"

#include <optional>

#include "base/futex_utils.h"

namespace cts::td {

void TdDispatcher::Run() {
  detail::TdEvent event;
  for (;;) {
    // Drain all queued events
    while (queue_.TryPop(event)) {
      Dispatch(event);
    }
    if (stop_requested_.load(std::memory_order_acquire)) {
      // Drain once more after the stop request to close the producer gap
      while (queue_.TryPop(event)) {
        Dispatch(event);
      }
      return;
    }
    // Parking protocol. Invariant: producers write their flag BEFORE bumping
    // wake_; so sample the word first, then re-check the conditions -- an
    // absorbed bump implies the flag is visible here, a later bump returns
    // EAGAIN or wakes us
    parked_.store(true);
    std::uint32_t const sample = wake_.load(std::memory_order_acquire);
    if (stop_requested_.load(std::memory_order_acquire) || !queue_.Empty()) {
      parked_.store(false, std::memory_order_release);
      continue;
    }
    base::detail::FutexWait(wake_, sample, std::nullopt);
    parked_.store(false, std::memory_order_release);
  }
}

void TdDispatcher::RequestStop() {
  stop_requested_.store(true, std::memory_order_release);
  wake_.fetch_add(1, std::memory_order_release);
  // Unconditional: a stop must never be gated by a stale parked_ read
  base::detail::FutexWakeOne(wake_);
}

void TdDispatcher::Push(detail::TdEvent event) {
  // Full queue drops the newest (snapshot semantics); see dropped_events()
  if (queue_.TryPush(std::move(event))) {
    wake_.fetch_add(1, std::memory_order_release);
    // seq_cst pairs with the consumer's parked_ store
    if (parked_.load()) {
      base::detail::FutexWakeOne(wake_);
    }
  }
}

void TdDispatcher::Dispatch(detail::TdEvent const& event) {
  // Readiness is tracked here, not in any particular client
  if (event.kind == detail::TdEvent::Kind::kReady) {
    ready_.store(event.success, std::memory_order_release);
  } else if (event.kind == detail::TdEvent::Kind::kDisconnected) {
    ready_.store(false, std::memory_order_release);
  }
  // Pure forwarding: the event vocabulary is identical on both sides
  for (auto* client : clients_) {
    switch (event.kind) {
      case detail::TdEvent::Kind::kConnected:
        client->OnTdConnected();
        break;
      case detail::TdEvent::Kind::kDisconnected:
        client->OnTdDisconnected(event.code);
        break;
      case detail::TdEvent::Kind::kReady:
        client->OnTdReady(event.success, event.msg);
        break;
      case detail::TdEvent::Kind::kOrder:
        client->OnTdOrder(event.order);
        break;
      case detail::TdEvent::Kind::kTrade:
        client->OnTdTrade(event.trade);
        break;
      case detail::TdEvent::Kind::kError:
        client->OnTdError(event.code, event.msg);
        break;
    }
  }
}

}  // namespace cts::td
