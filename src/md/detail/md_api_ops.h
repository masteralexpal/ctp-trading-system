/**
 * @file md_api_ops.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Narrow seam over CThostFtdcMdApi (concrete, non-virtual, unfakeable).
 */
#pragma once

#ifndef CTS_MD_DETAIL_MD_API_OPS_H_
#define CTS_MD_DETAIL_MD_API_OPS_H_

// Boundary header: md internal + tests only; exposes CTP types by design.

#include <span>
#include <string>

#include "ThostFtdcMdApi.h"

namespace cts::md::detail {

// Every operation MdGateway/MdSpiAdapter needs from the MdApi. Production
// wraps the real API (CtpMdApiOps); tests inject a fake and script the Spi
// callbacks by hand.
class MdApiOps {
 public:
  virtual ~MdApiOps() = default;

  virtual void RegisterSpi(CThostFtdcMdSpi* spi) = 0;
  virtual void RegisterFront(char* front) = 0;
  virtual void Init() = 0;
  virtual void Release() = 0;
  virtual int ReqUserLogin(CThostFtdcReqUserLoginField* req,
                           int request_id) = 0;
  virtual int SubscribeMarketData(std::span<char*> instrument_ids) = 0;
  virtual int UnSubscribeMarketData(std::span<char*> instrument_ids) = 0;
};

// Production impl: owns the real CThostFtdcMdApi.
class CtpMdApiOps final : public MdApiOps {
 public:
  // Creates the underlying API; flow_dir gets its mandatory trailing slash
  // and is created on disk here (the SDK concatenates filenames raw)
  explicit CtpMdApiOps(std::string flow_dir);

  void RegisterSpi(CThostFtdcMdSpi* spi) override { api_->RegisterSpi(spi); }
  void RegisterFront(char* front) override { api_->RegisterFront(front); }
  void Init() override { api_->Init(); }
  // Blocks until internal threads exit
  void Release() override;
  int ReqUserLogin(CThostFtdcReqUserLoginField* req, int request_id) override {
    return api_->ReqUserLogin(req, request_id);
  }
  int SubscribeMarketData(std::span<char*> instrument_ids) override {
    return api_->SubscribeMarketData(instrument_ids.data(),
                                     static_cast<int>(instrument_ids.size()));
  }
  int UnSubscribeMarketData(std::span<char*> instrument_ids) override {
    return api_->UnSubscribeMarketData(instrument_ids.data(),
                                       static_cast<int>(instrument_ids.size()));
  }

 private:
  CThostFtdcMdApi* api_;
};

}  // namespace cts::md::detail

#endif  // CTS_MD_DETAIL_MD_API_OPS_H_
