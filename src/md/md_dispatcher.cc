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
  for (auto* listener : listeners_) {
    switch (event.kind) {
      case detail::MdEvent::Kind::kConnected:
        listener->OnMdState(true, false);
        break;
      case detail::MdEvent::Kind::kDisconnected:
        listener->OnMdState(false, false);
        break;
      case detail::MdEvent::Kind::kLogin:
        listener->OnMdState(true, event.success);
        break;
      case detail::MdEvent::Kind::kTick:
        listener->OnTick(event.tick);
        break;
      case detail::MdEvent::Kind::kError:
        listener->OnMdError(event.code, event.msg);
        break;
    }
  }
}

}  // namespace cts::md
