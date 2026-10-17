/**
 * @file td_gateway.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Trading gateway: CTP TraderApi lifecycle, order insert/cancel.
 */
#pragma once

#ifndef CTS_TD_TD_GATEWAY_H_
#define CTS_TD_TD_GATEWAY_H_

#include <memory>
#include <string>
#include <string_view>

#include "instrument/instrument_master.h"
#include "td/order.h"
#include "td/order_sink.h"
#include "td/rate_table.h"
#include "td/td_delegate.h"

class CThostFtdcTraderSpi;

namespace cts::td {

namespace detail {
class TdApiOps;  // test seam; production callers never touch it
}

struct TdGatewayConfig {
  std::string front;
  std::string broker_id;
  std::string investor_id;
  std::string password;
  std::string app_id;
  std::string auth_code;
  std::string flow_dir;  // must be exclusive per gateway instance (.con clash)
};

class TdGateway : public OrderSink {
 public:
  TdGateway();
  ~TdGateway();

  TdGateway(TdGateway const&) = delete;
  TdGateway& operator=(TdGateway const&) = delete;

  // Call only from non-CTP threads (Disconnect joins the callback thread)
  void Connect(TdGatewayConfig config, TdDelegate& delegate);
  // Test seam: inject a fake TdApiOps. Defined out-of-line -- the type is
  // incomplete here, so no default argument
  void Connect(TdGatewayConfig config,
               TdDelegate& delegate,
               std::unique_ptr<detail::TdApiOps> ops);

  // Any thread. Returns the allocated order_ref (empty = locally rejected).
  // The terminal state arrives later via TdDelegate::OnTdOrder
  std::string InsertOrder(OrderRequest const& req) override;
  // Any thread. 0 = accepted; -1 = unknown ref or not connected
  int CancelOrder(std::string_view order_ref) override;

  // Any thread. Issues the whole-market instrument query; the snapshot is
  // published into instruments() when the answer completes
  int QueryInstruments();
  // Latest instrument snapshot; nullptr until the first query completes
  std::shared_ptr<instrument::InstrumentMaster const> instruments() const;

  // Any thread. The answer arrives once as TdDelegate::OnTdPositions
  // (reconciliation input for the OMS)
  int QueryPositions();
  // Any thread. The answer arrives once as TdDelegate::OnTdAccount
  int QueryAccount();

  // Any thread. Per-instrument margin/commission; answers land in rates()
  // (same throttle; one instrument per call)
  int QueryMarginRate(std::string_view instrument_id);
  int QueryCommissionRate(std::string_view instrument_id);
  // On-demand rate table; filled by the two queries above
  RateTable const& rates() const { return rates_; }

  // Startup chain finished (auth + login + settlement confirm)
  bool ready() const;

  // Idempotent. NEVER call from the CTP callback thread (joining self =
  // deadlock)
  void Disconnect();

 private:
  // Owned. Raw pointer because detail::TdApiOps is incomplete here; deleted in
  // Disconnect (same pattern as spi_)
  detail::TdApiOps* api_ = nullptr;
  CThostFtdcTraderSpi* spi_ = nullptr;
  RateTable rates_;  // adapter writes, risk sink reads
};

}  // namespace cts::td

#endif  // CTS_TD_TD_GATEWAY_H_
