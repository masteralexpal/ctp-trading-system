/**
 * @file td_api_ops.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Narrow seam over CThostFtdcTraderApi (concrete, non-virtual,
 *        unfakeable).
 */
#pragma once

#ifndef CTS_TD_DETAIL_TD_API_OPS_H_
#define CTS_TD_DETAIL_TD_API_OPS_H_

// Boundary header: td internal + tests only; exposes CTP types by design.

#include <string>

#include "ThostFtdcTraderApi.h"

namespace cts::td::detail {

// Every operation TdGateway/TdSpiAdapter needs from the TraderApi. Production
// wraps the real API (CtpTdApiOps); tests inject a fake and script the Spi
// callbacks by hand.
class TdApiOps {
 public:
  virtual ~TdApiOps() = default;

  virtual void RegisterSpi(CThostFtdcTraderSpi* spi) = 0;
  virtual void RegisterFront(char* front) = 0;
  // Topic subscription must precede RegisterFront; THOST_TERT_QUICK = no
  // resume of private/public streams after reconnect
  virtual void SubscribePrivateTopic(int resume_type) = 0;
  virtual void SubscribePublicTopic(int resume_type) = 0;
  virtual void Init() = 0;
  virtual void Release() = 0;
  virtual int ReqAuthenticate(CThostFtdcReqAuthenticateField* req,
                              int request_id) = 0;
  virtual int ReqUserLogin(CThostFtdcReqUserLoginField* req,
                           int request_id) = 0;
  virtual int ReqSettlementInfoConfirm(
      CThostFtdcSettlementInfoConfirmField* req,
      int request_id) = 0;
  virtual int ReqOrderInsert(CThostFtdcInputOrderField* req,
                             int request_id) = 0;
  virtual int ReqOrderAction(CThostFtdcInputOrderActionField* req,
                             int request_id) = 0;
  // Whole-market instrument snapshot; answered in batches via
  // OnRspQryInstrument (throttled by CTP to ~1 query/sec)
  virtual int ReqQryInstrument(CThostFtdcQryInstrumentField* req,
                               int request_id) = 0;
};

// Production impl: owns the real CThostFtdcTraderApi.
class CtpTdApiOps final : public TdApiOps {
 public:
  // Creates the underlying API; flow_dir gets its mandatory trailing slash
  // and is created on disk here (the SDK concatenates filenames raw)
  explicit CtpTdApiOps(std::string flow_dir);

  void RegisterSpi(CThostFtdcTraderSpi* spi) override {
    api_->RegisterSpi(spi);
  }
  void RegisterFront(char* front) override { api_->RegisterFront(front); }
  void SubscribePrivateTopic(int resume_type) override {
    api_->SubscribePrivateTopic(static_cast<THOST_TE_RESUME_TYPE>(resume_type));
  }
  void SubscribePublicTopic(int resume_type) override {
    api_->SubscribePublicTopic(static_cast<THOST_TE_RESUME_TYPE>(resume_type));
  }
  void Init() override { api_->Init(); }
  // Blocks until internal threads exit
  void Release() override;
  int ReqAuthenticate(CThostFtdcReqAuthenticateField* req,
                      int request_id) override {
    return api_->ReqAuthenticate(req, request_id);
  }
  int ReqUserLogin(CThostFtdcReqUserLoginField* req, int request_id) override {
    return api_->ReqUserLogin(req, request_id);
  }
  int ReqSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* req,
                               int request_id) override {
    return api_->ReqSettlementInfoConfirm(req, request_id);
  }
  int ReqOrderInsert(CThostFtdcInputOrderField* req, int request_id) override {
    return api_->ReqOrderInsert(req, request_id);
  }
  int ReqOrderAction(CThostFtdcInputOrderActionField* req,
                     int request_id) override {
    return api_->ReqOrderAction(req, request_id);
  }
  int ReqQryInstrument(CThostFtdcQryInstrumentField* req,
                       int request_id) override {
    return api_->ReqQryInstrument(req, request_id);
  }

 private:
  CThostFtdcTraderApi* api_;
};

}  // namespace cts::td::detail

#endif  // CTS_TD_DETAIL_TD_API_OPS_H_
