/**
 * @file md_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard MdDelegate: box-and-enqueue, fan out on a worker thread.
 */
#include "md/md_dispatcher.h"

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
    park_.WaitUnless([this] {
      return stop_requested_.load(std::memory_order_acquire) ||
             !queue_.Empty();
    });
  }
}

void MdDispatcher::RequestStop() {
  stop_requested_.store(true, std::memory_order_release);
  park_.NotifyUnconditionally();
}

void MdDispatcher::Push(detail::MdEvent event) {
  // Full queue drops the newest (snapshot semantics); see dropped_events()
  if (queue_.TryPush(std::move(event))) {
    park_.Notify();
  }
}

void MdDispatcher::Dispatch(detail::MdEvent const& event) {
  // Login state is tracked here, not in any particular client
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
