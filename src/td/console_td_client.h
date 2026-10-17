/**
 * @file console_td_client.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug client: forwards trading events to the log.
 */
#pragma once

#ifndef CTS_TD_CONSOLE_TD_CLIENT_H_
#define CTS_TD_CONSOLE_TD_CLIENT_H_

#include "td/td_client.h"

namespace cts::td {

// Logs every trading event; the reference client for the TdClient contract
class ConsoleTdClient final : public TdClient {
 public:
  void OnTdConnected() override;
  void OnTdDisconnected(int reason) override;
  void OnTdReady(bool success, std::string_view msg) override;
  void OnTdOrder(OrderReport const& order) override;
  void OnTdTrade(TradeReport const& trade) override;
  void OnTdError(int error_id, std::string_view msg) override;
};

}  // namespace cts::td

#endif  // CTS_TD_CONSOLE_TD_CLIENT_H_
