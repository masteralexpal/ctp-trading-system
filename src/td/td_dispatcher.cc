/**
 * @file td_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Standard TdDelegate: box-and-post, fan out on a task loop.
 */
#include "td/td_dispatcher.h"

namespace cts::td {

void TdDispatcher::Push(detail::TdEvent event) {
  // Task is move_only_function: the event moves into the closure, no
  // allocation beyond the task node itself. FIFO order is the loop's
  // guarantee; wakeups are the loop's job (eventfd, not a futex here)
  loop_.PostTask([this, e = std::move(event)] { Dispatch(e); });
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
      case detail::TdEvent::Kind::kAccount:
        client->OnTdAccount(event.account);
        break;
      case detail::TdEvent::Kind::kError:
        client->OnTdError(event.code, event.msg);
        break;
    }
  }
}

}  // namespace cts::td
