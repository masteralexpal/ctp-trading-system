/**
 * @file md_client.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Business-side market data client.
 */
#pragma once

#ifndef CTS_MD_MD_CLIENT_H_
#define CTS_MD_MD_CLIENT_H_

#include <string_view>

#include "md/tick.h"

namespace cts::md {

// The client of the market data stream. Broadcast semantics: every registered
// client receives every event. Pure virtual by design -- the interface is a
// contract signed in full; ignoring an event must be a conscious act.
//
// Contract: all callbacks run on the dispatcher thread. A client must outlive
// the dispatcher's Run() (register before Run, destroy after it returns).
// The event vocabulary mirrors MdDelegate exactly, one hop downstream.
class MdClient {
 public:
  virtual ~MdClient() = default;

  virtual void OnMdConnected() = 0;
  virtual void OnMdDisconnected(int reason) = 0;
  virtual void OnMdLogin(bool success, std::string_view msg) = 0;
  virtual void OnTick(Tick const& tick) = 0;
  virtual void OnMdError(int error_id, std::string_view msg) = 0;
};

}  // namespace cts::md

#endif  // CTS_MD_MD_CLIENT_H_
