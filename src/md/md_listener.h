/**
 * @file md_listener.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Business-side market data observer.
 */
#pragma once

#ifndef CTS_MD_MD_LISTENER_H_
#define CTS_MD_MD_LISTENER_H_

#include <string_view>

#include "md/tick.h"

namespace cts::md {

class MdListener {
 public:
  virtual ~MdListener() = default;

  virtual void OnTick(Tick const& /*tick*/) {}
  virtual void OnMdState(bool /*connected*/, bool /*logged_in*/) {}
  virtual void OnMdError(int /*error_id*/, std::string_view /*msg*/) {}
};

}  // namespace cts::md

#endif  // CTS_MD_MD_LISTENER_H_
