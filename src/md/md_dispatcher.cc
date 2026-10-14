/**
 * @file md_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard MdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#include "md/md_dispatcher.h"

#include <thread>

namespace cts::md {

void MdDispatcher::Run() {
  detail::MdEvent event;
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
    // Yield when empty; an eventfd/futex wakeup is the low-latency upgrade path
    std::this_thread::yield();
  }
}

void MdDispatcher::Push(detail::MdEvent event) {
  // On full queue the newest event is dropped; watch dropped_events() for
  // backpressure; the drop is intentional (snapshot semantics), so discard the
  // result explicitly
  static_cast<void>(queue_.TryPush(std::move(event)));
}

void MdDispatcher::Dispatch(detail::MdEvent const& event) {
  // State is tracked before fan-out: login readiness lives here, not in any
  // particular client
  if (event.kind == detail::MdEvent::Kind::kLogin) {
    logged_in_.store(event.success, std::memory_order_release);
  } else if (event.kind == detail::MdEvent::Kind::kDisconnected) {
    logged_in_.store(false, std::memory_order_release);
  }
  // Pure forwarding: the event vocabulary is identical on both sides
  for (auto* client : clients_) {
    switch (event.kind) {
      case detail::MdEvent::Kind::kConnected:
        client->OnMdConnected();
        break;
      case detail::MdEvent::Kind::kDisconnected:
        client->OnMdDisconnected(event.code);
        break;
      case detail::MdEvent::Kind::kLogin:
        client->OnMdLogin(event.success, event.msg);
        break;
      case detail::MdEvent::Kind::kTick:
        client->OnTick(event.tick);
        break;
      case detail::MdEvent::Kind::kError:
        client->OnMdError(event.code, event.msg);
        break;
    }
  }
}

}  // namespace cts::md
