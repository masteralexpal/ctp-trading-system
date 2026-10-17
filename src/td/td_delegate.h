/**
 * @file td_delegate.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief TdDelegate: trading events fired on the CTP callback thread.
 */
#pragma once

#ifndef CTS_TD_TD_DELEGATE_H_
#define CTS_TD_TD_DELEGATE_H_

#include <memory>
#include <string_view>
#include <vector>

#include "td/order.h"

namespace cts::td {

class TdDelegate {
 public:
  virtual ~TdDelegate() = default;

  virtual void OnTdConnected() {}
  virtual void OnTdDisconnected(int /*reason*/) {}
  // The full startup chain (auth -> login -> settlement confirm) finished;
  // success=false carries the failure message of whichever leg failed
  virtual void OnTdReady(bool /*success*/, std::string_view /*msg*/) {}
  virtual void OnTdOrder(OrderReport const& /*order*/) {}
  virtual void OnTdTrade(TradeReport const& /*trade*/) {}
  // Broker position snapshot answer (reconciliation input); fired once per
  // QueryPositions, never spontaneously
  virtual void OnTdPositions(
      std::shared_ptr<std::vector<PositionRow> const> /*rows*/) {}
  // Account snapshot answer; fired once per QueryAccount
  virtual void OnTdAccount(AccountReport const& /*account*/) {}
  virtual void OnTdError(int /*error_id*/, std::string_view /*msg*/) {}
};

}  // namespace cts::td

#endif  // CTS_TD_TD_DELEGATE_H_
