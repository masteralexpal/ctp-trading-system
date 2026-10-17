/**
 * @file td_client.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Business-side trading client.
 */
#pragma once

#ifndef CTS_TD_TD_CLIENT_H_
#define CTS_TD_TD_CLIENT_H_

#include <memory>
#include <string_view>
#include <vector>

#include "td/order.h"

namespace cts::td {

// The client of the trading event stream. Broadcast semantics: every
// registered client receives every event. Pure virtual by design -- the
// interface is a contract signed in full; ignoring an event must be a
// conscious act.
//
// Contract: all callbacks run on the dispatcher thread. A client must outlive
// the dispatcher's Run() (register before Run, destroy after it returns).
// The event vocabulary mirrors TdDelegate exactly, one hop downstream.
class TdClient {
 public:
  virtual ~TdClient() = default;

  virtual void OnTdConnected() = 0;
  virtual void OnTdDisconnected(int reason) = 0;
  virtual void OnTdReady(bool success, std::string_view msg) = 0;
  virtual void OnTdOrder(OrderReport const& order) = 0;
  virtual void OnTdTrade(TradeReport const& trade) = 0;
  virtual void OnTdPositions(
      std::shared_ptr<std::vector<PositionRow> const> rows) = 0;
  virtual void OnTdError(int error_id, std::string_view msg) = 0;
};

}  // namespace cts::td

#endif  // CTS_TD_TD_CLIENT_H_
