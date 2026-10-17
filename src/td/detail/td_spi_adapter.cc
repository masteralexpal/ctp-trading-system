/**
 * @file td_spi_adapter.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Adapter from CTP TraderSpi callbacks to TdDelegate events.
 */
#include "td/detail/td_spi_adapter.h"

#include <pthread.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "base/charset.h"
#include "base/string_utils.h"
#include "instrument/instrument_ctp.h"
#include "td/detail/order_ctp.h"

namespace cts::td::detail {

TdSpiAdapter::TdSpiAdapter(TdApiOps* api,
                           TdGatewayConfig config,
                           TdDelegate& delegate,
                           RateTable& rates)
    : api_(api),
      config_(std::move(config)),
      delegate_(delegate),
      rates_(rates) {}

std::string TdSpiAdapter::NextOrderRef() {
  char buf[13];
  std::snprintf(buf, sizeof(buf), "%012d",
                next_ref_.fetch_add(1, std::memory_order_relaxed));
  return buf;
}

std::string TdSpiAdapter::InsertOrder(OrderRequest const& req) {
  if (!ready_.load(std::memory_order_acquire) || req.volume <= 0) {
    return {};
  }
  std::string const ref = NextOrderRef();
  auto field =
      InputOrderToCtp(req, config_.broker_id, config_.investor_id, ref);

  OrderContext ctx{};
  ctx.front_id = front_id_.load(std::memory_order_relaxed);
  ctx.session_id = session_id_.load(std::memory_order_relaxed);
  base::CopyToFixedArray(ctx.exchange_id, req.exchange_id);
  base::CopyToFixedArray(ctx.instrument_id, req.instrument_id);
  // Register the cancel key BEFORE the request: the first report can race
  // the return of ReqOrderInsert
  {
    std::lock_guard lock(mutex_);
    orders_.emplace(ref, ctx);
  }
  int const rc = api_->ReqOrderInsert(&field, request_id_.fetch_add(1) + 1);
  if (rc != 0) {
    std::lock_guard lock(mutex_);
    orders_.erase(ref);
    return {};
  }
  return ref;
}

int TdSpiAdapter::CancelOrder(std::string_view order_ref) {
  OrderContext ctx;
  {
    std::lock_guard lock(mutex_);
    auto const it = orders_.find(std::string(order_ref));
    if (it == orders_.end()) {
      return -1;
    }
    ctx = it->second;
  }
  CThostFtdcInputOrderActionField f{};
  base::CopyToFixedArray(f.BrokerID, config_.broker_id);
  base::CopyToFixedArray(f.InvestorID, config_.investor_id);
  base::CopyToFixedArray(f.UserID, config_.investor_id);  // CTP: same
  base::CopyToFixedArray(f.OrderRef, std::string(order_ref));
  base::CopyToFixedArray(f.ExchangeID, ctx.exchange_id);
  base::CopyToFixedArray(f.InstrumentID, ctx.instrument_id);
  if (ctx.order_sys_id[0] != '\0') {
    // Reconnect-proof key: identify by exchange-side sys id; front/session
    // of the OLD session would be rejected after a reconnect
    base::CopyToFixedArray(f.OrderSysID, ctx.order_sys_id);
  } else {
    f.FrontID = ctx.front_id;
    f.SessionID = ctx.session_id;
  }
  f.OrderActionRef = next_action_ref_.fetch_add(1, std::memory_order_relaxed);
  f.ActionFlag = THOST_FTDC_AF_Delete;
  return api_->ReqOrderAction(&f, request_id_.fetch_add(1) + 1);
}

void TdSpiAdapter::OnFrontConnected() {
  // Name the CTP internal thread on first callback
  std::call_once(name_once_,
                 [] { pthread_setname_np(pthread_self(), "ctp-td-io"); });
  delegate_.OnTdConnected();

  CThostFtdcReqAuthenticateField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.UserID, config_.investor_id);
  base::CopyToFixedArray(req.AppID, config_.app_id);
  base::CopyToFixedArray(req.AuthCode, config_.auth_code);
  api_->ReqAuthenticate(&req, request_id_.fetch_add(1) + 1);
}

void TdSpiAdapter::OnFrontDisconnected(int reason) {
  ready_.store(false, std::memory_order_release);
  delegate_.OnTdDisconnected(reason);
}

void TdSpiAdapter::OnRspAuthenticate(CThostFtdcRspAuthenticateField* /*rsp*/,
                                     CThostFtdcRspInfoField* info,
                                     int /*request_id*/,
                                     bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    FailChain(base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  CThostFtdcReqUserLoginField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.UserID, config_.investor_id);
  base::CopyToFixedArray(req.Password, config_.password);
  api_->ReqUserLogin(&req, request_id_.fetch_add(1) + 1);
}

void TdSpiAdapter::OnRspUserLogin(CThostFtdcRspUserLoginField* rsp,
                                  CThostFtdcRspInfoField* info,
                                  int /*request_id*/,
                                  bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    FailChain(base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  front_id_.store(rsp->FrontID, std::memory_order_relaxed);
  session_id_.store(rsp->SessionID, std::memory_order_relaxed);
  // OrderRef must stay unique across reconnects within the day: continue
  // after the broker's max
  int const max_ref = std::atoi(rsp->MaxOrderRef);
  int expected = next_ref_.load(std::memory_order_relaxed);
  while (max_ref >= expected &&
         !next_ref_.compare_exchange_weak(expected, max_ref + 1,
                                          std::memory_order_relaxed)) {
  }

  CThostFtdcSettlementInfoConfirmField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.InvestorID, config_.investor_id);
  api_->ReqSettlementInfoConfirm(&req, request_id_.fetch_add(1) + 1);
}

void TdSpiAdapter::OnRspSettlementInfoConfirm(
    CThostFtdcSettlementInfoConfirmField* /*rsp*/,
    CThostFtdcRspInfoField* info,
    int /*request_id*/,
    bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    FailChain(base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  ready_.store(true, std::memory_order_release);
  delegate_.OnTdReady(true, "");
}

void TdSpiAdapter::OnRspOrderInsert(CThostFtdcInputOrderField* req,
                                    CThostFtdcRspInfoField* info,
                                    int /*request_id*/,
                                    bool /*is_last*/) {
  if (info == nullptr || info->ErrorID == 0) {
    return;  // accepted; the state arrives via OnRtnOrder
  }
  if (req != nullptr) {
    // Broker-side rejection: terminal, drop the cancel key
    OrderReport const report = RejectedOrderFromCtp(*req);
    {
      std::lock_guard lock(mutex_);
      orders_.erase(req->OrderRef);
    }
    delegate_.OnTdOrder(report);
  }
  delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
}

void TdSpiAdapter::OnRspOrderAction(CThostFtdcInputOrderActionField* /*req*/,
                                    CThostFtdcRspInfoField* info,
                                    int /*request_id*/,
                                    bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

void TdSpiAdapter::OnRspError(CThostFtdcRspInfoField* info,
                              int /*request_id*/,
                              bool /*is_last*/) {
  if (info != nullptr) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

int TdSpiAdapter::QueryInstruments() {
  if (!ready_.load(std::memory_order_acquire)) {
    return -1;
  }
  // Empty InstrumentID = whole market
  CThostFtdcQryInstrumentField req{};
  return api_->ReqQryInstrument(&req, request_id_.fetch_add(1) + 1);
}

int TdSpiAdapter::QueryPositions() {
  if (!ready_.load(std::memory_order_acquire)) {
    return -1;
  }
  // Empty InstrumentID = all positions
  CThostFtdcQryInvestorPositionField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.InvestorID, config_.investor_id);
  return api_->ReqQryInvestorPosition(&req, request_id_.fetch_add(1) + 1);
}

int TdSpiAdapter::QueryAccount() {
  if (!ready_.load(std::memory_order_acquire)) {
    return -1;
  }
  CThostFtdcQryTradingAccountField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.InvestorID, config_.investor_id);
  return api_->ReqQryTradingAccount(&req, request_id_.fetch_add(1) + 1);
}

int TdSpiAdapter::QueryMarginRate(std::string_view instrument_id) {
  if (!ready_.load(std::memory_order_acquire)) {
    return -1;
  }
  CThostFtdcQryInstrumentMarginRateField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.InvestorID, config_.investor_id);
  base::CopyToFixedArray(req.reserve1, instrument_id);  // 6.7.x instrument id
  req.HedgeFlag = THOST_FTDC_HF_Speculation;
  return api_->ReqQryInstrumentMarginRate(&req, request_id_.fetch_add(1) + 1);
}

int TdSpiAdapter::QueryCommissionRate(std::string_view instrument_id) {
  if (!ready_.load(std::memory_order_acquire)) {
    return -1;
  }
  CThostFtdcQryInstrumentCommissionRateField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.InvestorID, config_.investor_id);
  base::CopyToFixedArray(req.reserve1, instrument_id);
  return api_->ReqQryInstrumentCommissionRate(&req,
                                              request_id_.fetch_add(1) + 1);
}

void TdSpiAdapter::OnRspQryInstrumentMarginRate(
    CThostFtdcInstrumentMarginRateField* rate,
    CThostFtdcRspInfoField* info,
    int /*request_id*/,
    bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  if (rate != nullptr) {
    rates_.Update(MarginRateFromCtp(*rate));
  }
}

void TdSpiAdapter::OnRspQryInstrumentCommissionRate(
    CThostFtdcInstrumentCommissionRateField* rate,
    CThostFtdcRspInfoField* info,
    int /*request_id*/,
    bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  if (rate != nullptr) {
    rates_.Update(CommissionRateFromCtp(*rate));
  }
}

void TdSpiAdapter::OnRspQryTradingAccount(CThostFtdcTradingAccountField* account,
                                          CThostFtdcRspInfoField* info,
                                          int /*request_id*/,
                                          bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  if (account != nullptr) {
    delegate_.OnTdAccount(AccountReportFromCtp(*account));
  }
}

void TdSpiAdapter::OnRspQryInvestorPosition(
    CThostFtdcInvestorPositionField* position,
    CThostFtdcRspInfoField* info,
    int /*request_id*/,
    bool is_last) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  if (position != nullptr) {
    PositionRow row;
    if (PositionRowFromCtp(*position, row) &&
        (row.yd_volume > 0 || row.today_volume > 0)) {
      // Merge into the pending row of the same (instrument, side)
      auto it = std::find_if(pending_positions_.begin(),
                             pending_positions_.end(),
                             [&](PositionRow const& p) {
                               return p.direction == row.direction &&
                                      std::string_view(p.instrument_id) ==
                                          row.instrument_id;
                             });
      if (it == pending_positions_.end()) {
        pending_positions_.push_back(row);
      } else {
        it->yd_volume += row.yd_volume;
        it->today_volume += row.today_volume;
      }
    }
  }
  if (is_last) {
    delegate_.OnTdPositions(
        std::make_shared<std::vector<PositionRow> const>(
            std::move(pending_positions_)));
    pending_positions_.clear();
  }
}

void TdSpiAdapter::OnRspQryInstrument(CThostFtdcInstrumentField* instrument,
                                      CThostFtdcRspInfoField* info,
                                      int /*request_id*/,
                                      bool is_last) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
    return;
  }
  if (instrument != nullptr) {
    pending_instruments_.push_back(instrument::InstrumentFromCtp(*instrument));
  }
  if (is_last) {
    auto master = std::make_shared<instrument::InstrumentMaster>(
        instrument::InstrumentMaster::Build(std::move(pending_instruments_)));
    pending_instruments_.clear();
    instruments_.store(std::move(master));
  }
}

void TdSpiAdapter::OnRtnOrder(CThostFtdcOrderField* order) {
  if (order == nullptr) {
    return;
  }
  OrderReport const report = OrderReportFromCtp(*order);
  {
    std::lock_guard lock(mutex_);
    if (report.status == OrderStatus::kAllTraded ||
        report.status == OrderStatus::kCanceled) {
      orders_.erase(report.order_ref);  // terminal: no longer cancellable
    } else if (auto const it = orders_.find(report.order_ref);
               it != orders_.end()) {
      // Refresh the cancel key: authoritative front/session, plus the
      // exchange-side sys id (the reconnect-proof half)
      it->second.front_id = report.front_id;
      it->second.session_id = report.session_id;
      if (report.order_sys_id[0] != '\0') {
        base::CopyToFixedArray(it->second.order_sys_id, report.order_sys_id);
      }
    }
  }
  delegate_.OnTdOrder(report);
}

void TdSpiAdapter::OnRtnTrade(CThostFtdcTradeField* trade) {
  if (trade != nullptr) {
    delegate_.OnTdTrade(TradeReportFromCtp(*trade));
  }
}

void TdSpiAdapter::OnErrRtnOrderInsert(CThostFtdcInputOrderField* req,
                                       CThostFtdcRspInfoField* info) {
  if (req != nullptr) {
    // Exchange-side async rejection: terminal, drop the cancel key
    OrderReport const report = RejectedOrderFromCtp(*req);
    {
      std::lock_guard lock(mutex_);
      orders_.erase(req->OrderRef);
    }
    delegate_.OnTdOrder(report);
  }
  if (info != nullptr) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

void TdSpiAdapter::OnErrRtnOrderAction(CThostFtdcOrderActionField* /*action*/,
                                       CThostFtdcRspInfoField* info) {
  if (info != nullptr) {
    delegate_.OnTdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

void TdSpiAdapter::FailChain(std::string_view msg) {
  ready_.store(false, std::memory_order_release);
  delegate_.OnTdReady(false, msg);
}

}  // namespace cts::td::detail
