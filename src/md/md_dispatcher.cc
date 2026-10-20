/**
 * @file md_dispatcher.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief MdDispatcher: an SpscLoop<MdEvent> pump plus the client fan-out.
 */
#include "md/md_dispatcher.h"

namespace cts::md {

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
