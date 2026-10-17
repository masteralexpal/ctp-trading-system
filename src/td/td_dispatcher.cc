/**
 * @file td_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard TdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#include "td/td_dispatcher.h"

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
    park_.WaitUnless([this] {
      return stop_requested_.load(std::memory_order_acquire) ||
             !queue_.Empty();
    });
  }
}

void TdDispatcher::RequestStop() {
  stop_requested_.store(true, std::memory_order_release);
  park_.NotifyUnconditionally();
}

void TdDispatcher::Push(detail::TdEvent event) {
  // Full queue drops the newest (snapshot semantics); see dropped_events()
  if (queue_.TryPush(std::move(event))) {
    park_.Notify();
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
      case detail::TdEvent::Kind::kPositions:
        client->OnTdPositions(event.positions);
        break;
      case detail::TdEvent::Kind::kError:
        client->OnTdError(event.code, event.msg);
        break;
    }
  }
}

}  // namespace cts::td
