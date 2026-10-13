/**
 * @file md_spi_adapter.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Adapter from CTP MdSpi callbacks to MdDelegate events.
 */
#pragma once

#ifndef CTS_MD_DETAIL_MD_SPI_ADAPTER_H_
#define CTS_MD_DETAIL_MD_SPI_ADAPTER_H_

// Boundary class: only the md module's own .cc files (and its tests) may
// include this; it exposes CTP types by design.

#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ThostFtdcMdApi.h"
#include "md/md_delegate.h"
#include "md/md_gateway.h"

namespace cts::md::detail {

// Adapter from CTP Spi to MdDelegate. Login is issued in OnFrontConnected;
// all subscriptions are restored after login (a reconnected session starts
// empty).
//
// All overrides run on the CTP callback thread; Subscribe/UnSubscribe are
// callable from any thread (CTP request APIs are thread-safe).
// Note: CThostFtdcMdSpi has no virtual destructor -- delete with the exact
// type MdSpiAdapter (see MdGateway::Disconnect).
class MdSpiAdapter final : public CThostFtdcMdSpi {
 public:
  MdSpiAdapter(CThostFtdcMdApi* api,
               MdGatewayConfig config,
               MdDelegate& delegate);

  // Returns the CTP error code; 0 means accepted
  int Subscribe(std::vector<std::string> const& instruments);
  int UnSubscribe(std::vector<std::string> const& instruments);

  // CThostFtdcMdSpi
  void OnFrontConnected() override;
  void OnFrontDisconnected(int reason) override;
  void OnRspUserLogin(CThostFtdcRspUserLoginField* rsp,
                      CThostFtdcRspInfoField* info,
                      int request_id,
                      bool is_last) override;
  void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* instrument,
                          CThostFtdcRspInfoField* info,
                          int request_id,
                          bool is_last) override;
  void OnRspError(CThostFtdcRspInfoField* info,
                  int request_id,
                  bool is_last) override;
  void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* tick) override;

 private:
  // subscribe=true subscribes, false unsubscribes; empty list is a no-op.
  // returns the CTP error code; 0 means accepted
  int CallSubscribe(std::vector<std::string> const& instruments,
                    bool subscribe);

  CThostFtdcMdApi* api_;
  MdGatewayConfig config_;
  MdDelegate& delegate_;
  int request_id_{0};
  std::once_flag name_once_;
  // Guards the subscription set: touched by both the callback thread
  // (resubscribe) and callers
  std::mutex mutex_;
  std::set<std::string> subscribed_;
};

}  // namespace cts::md::detail

#endif  // CTS_MD_DETAIL_MD_SPI_ADAPTER_H_
