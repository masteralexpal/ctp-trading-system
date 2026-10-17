/**
 * @file td_spi_adapter.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Adapter from CTP TraderSpi callbacks to TdDelegate events.
 */
#pragma once

#ifndef CTS_TD_DETAIL_TD_SPI_ADAPTER_H_
#define CTS_TD_DETAIL_TD_SPI_ADAPTER_H_

// Boundary class: only the td module's own .cc files (and its tests) may
// include this; it exposes CTP types by design.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ThostFtdcTraderApi.h"
#include "instrument/instrument_master.h"
#include "td/detail/td_api_ops.h"
#include "td/td_delegate.h"
#include "td/td_gateway.h"

namespace cts::td::detail {

// Adapter from CTP Spi to TdDelegate. The startup chain is driven from the
// callbacks:
//   OnFrontConnected -> ReqAuthenticate
//   -> OnRspAuthenticate -> ReqUserLogin
//   -> OnRspUserLogin -> ReqSettlementInfoConfirm
//   -> OnRspSettlementInfoConfirm -> OnTdReady
// OrderRef is seeded from MaxOrderRef at login and allocated atomically
// (zero-padded 12-digit), unique across reconnects within the trading day.
//
// All overrides run on the CTP callback thread; InsertOrder/CancelOrder are
// callable from any thread (CTP request APIs are thread-safe).
// Note: CThostFtdcTraderSpi has no virtual destructor -- delete with the
// exact type TdSpiAdapter (see TdGateway::Disconnect).
class TdSpiAdapter final : public CThostFtdcTraderSpi {
 public:
  TdSpiAdapter(TdApiOps* api, TdGatewayConfig config, TdDelegate& delegate);

  // Any thread. Returns the allocated order_ref, or empty when rejected
  // locally (not ready / CTP refused the request)
  std::string InsertOrder(OrderRequest const& req);
  // Any thread. Returns the CTP error code; -1 = unknown order_ref
  int CancelOrder(std::string_view order_ref);
  // Any thread; issues the whole-market instrument query (call once after
  // ready; CTP throttles queries to ~1/sec)
  int QueryInstruments();

  bool ready() const { return ready_.load(std::memory_order_acquire); }
  // Latest built snapshot; nullptr until the first query completes.
  // Read-only afterwards; wholesale-replaced by the next query
  std::shared_ptr<instrument::InstrumentMaster const> instruments() const {
    return instruments_.load();
  }

  // CThostFtdcTraderSpi
  void OnFrontConnected() override;
  void OnFrontDisconnected(int reason) override;
  void OnRspAuthenticate(CThostFtdcRspAuthenticateField* rsp,
                         CThostFtdcRspInfoField* info,
                         int request_id,
                         bool is_last) override;
  void OnRspUserLogin(CThostFtdcRspUserLoginField* rsp,
                      CThostFtdcRspInfoField* info,
                      int request_id,
                      bool is_last) override;
  void OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* rsp,
                                  CThostFtdcRspInfoField* info,
                                  int request_id,
                                  bool is_last) override;
  void OnRspOrderInsert(CThostFtdcInputOrderField* req,
                        CThostFtdcRspInfoField* info,
                        int request_id,
                        bool is_last) override;
  void OnRspOrderAction(CThostFtdcInputOrderActionField* req,
                        CThostFtdcRspInfoField* info,
                        int request_id,
                        bool is_last) override;
  void OnRspError(CThostFtdcRspInfoField* info,
                  int request_id,
                  bool is_last) override;
  void OnRspQryInstrument(CThostFtdcInstrumentField* instrument,
                          CThostFtdcRspInfoField* info,
                          int request_id,
                          bool is_last) override;
  void OnRtnOrder(CThostFtdcOrderField* order) override;
  void OnRtnTrade(CThostFtdcTradeField* trade) override;
  void OnErrRtnOrderInsert(CThostFtdcInputOrderField* req,
                           CThostFtdcRspInfoField* info) override;
  void OnErrRtnOrderAction(CThostFtdcOrderActionField* action,
                           CThostFtdcRspInfoField* info) override;

 private:
  // Cancel key of a live order, captured at insert and refreshed by reports
  struct OrderContext {
    int front_id;
    int session_id;
    char exchange_id[9];
    char instrument_id[31];
  };

  std::string NextOrderRef();  // zero-padded 12-digit, monotonic
  void FailChain(std::string_view msg);

  TdApiOps* api_;
  TdGatewayConfig config_;
  TdDelegate& delegate_;
  std::once_flag name_once_;

  std::atomic<bool> ready_{false};
  std::atomic<int> front_id_{0};
  std::atomic<int> session_id_{0};
  std::atomic<int> next_ref_{1};
  std::atomic<int> next_action_ref_{1};
  std::atomic<int> request_id_{0};

  // Live orders by order_ref; erased on terminal reports. Guards cancel
  std::mutex mutex_;
  std::unordered_map<std::string, OrderContext> orders_;

  // Instrument query accumulation (callback thread) + published snapshot
  std::vector<instrument::Instrument> pending_instruments_;
  std::atomic<std::shared_ptr<instrument::InstrumentMaster const>> instruments_;
};

}  // namespace cts::td::detail

#endif  // CTS_TD_DETAIL_TD_SPI_ADAPTER_H_
