/**
 * @file md_delegate.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief MdDelegate: market data events fired on the CTP callback thread.
 */
#pragma once

#ifndef CTS_MD_MD_DELEGATE_H_
#define CTS_MD_MD_DELEGATE_H_

#include <string_view>

#include "md/tick.h"

namespace cts::md {

class MdDelegate {
 public:
  virtual ~MdDelegate() = default;

  virtual void OnMdConnected() {}
  virtual void OnMdDisconnected(int /*reason*/) {}
  virtual void OnMdLogin(bool /*success*/, std::string_view /*msg*/) {}
  virtual void OnMdTick(Tick const& /*tick*/) {}
  virtual void OnMdError(int /*error_id*/, std::string_view /*msg*/) {}
};

}  // namespace cts::md

#endif  // CTS_MD_MD_DELEGATE_H_
