/**
 * @file md_event.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Boxed market data event carried by MdDispatcher's queue.
 */
#pragma once

#ifndef CTS_MD_DETAIL_MD_EVENT_H_
#define CTS_MD_DETAIL_MD_EVENT_H_

#include <string>

#include "md/tick.h"

namespace cts::md::detail {

// Uniformly boxed event: every MdDelegate callback enters one queue,
// preserving global order
struct MdEvent {
  enum class Kind { kConnected, kDisconnected, kLogin, kTick, kError };
  Kind kind;
  Tick tick{};          // kTick only
  int code{0};          // kDisconnected=reason / kError=error_id
  bool success{false};  // kLogin only
  std::string msg;      // kLogin / kError (UTF-8)
};

}  // namespace cts::md::detail

#endif  // CTS_MD_DETAIL_MD_EVENT_H_
