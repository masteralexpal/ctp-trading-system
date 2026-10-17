/**
 * @file md_spi_adapter.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Adapter from CTP MdSpi callbacks to MdDelegate events.
 */
#include "md/detail/md_spi_adapter.h"

#include <pthread.h>

#include <utility>

#include "base/charset.h"
#include "base/string_utils.h"
#include "md/detail/tick_ctp.h"

namespace cts::md::detail {

MdSpiAdapter::MdSpiAdapter(MdApiOps* api,
                           MdGatewayConfig config,
                           MdDelegate& delegate)
    : api_(api), config_(std::move(config)), delegate_(delegate) {}

int MdSpiAdapter::Subscribe(std::vector<std::string> const& instruments) {
  std::vector<std::string> fresh;
  {
    std::lock_guard lock(mutex_);
    for (auto const& id : instruments) {
      if (subscribed_.insert(id).second) {
        fresh.push_back(id);
      }
    }
  }
  return CallSubscribe(fresh, true);
}

int MdSpiAdapter::UnSubscribe(std::vector<std::string> const& instruments) {
  std::vector<std::string> gone;
  {
    std::lock_guard lock(mutex_);
    for (auto const& id : instruments) {
      if (subscribed_.erase(id) > 0) {
        gone.push_back(id);
      }
    }
  }
  return CallSubscribe(gone, false);
}

void MdSpiAdapter::OnFrontConnected() {
  // Name the CTP internal thread on first callback
  std::call_once(name_once_,
                 [] { pthread_setname_np(pthread_self(), "ctp-md-io"); });
  delegate_.OnMdConnected();

  CThostFtdcReqUserLoginField req{};
  base::CopyToFixedArray(req.BrokerID, config_.broker_id);
  base::CopyToFixedArray(req.UserID, config_.investor_id);
  base::CopyToFixedArray(req.Password, config_.password);
  api_->ReqUserLogin(&req, ++request_id_);
}

void MdSpiAdapter::OnFrontDisconnected(int reason) {
  delegate_.OnMdDisconnected(reason);
}

void MdSpiAdapter::OnRspUserLogin(CThostFtdcRspUserLoginField* /*rsp*/,
                                  CThostFtdcRspInfoField* info,
                                  int /*request_id*/,
                                  bool /*is_last*/) {
  bool const ok = info == nullptr || info->ErrorID == 0;
  delegate_.OnMdLogin(ok,
                      info != nullptr ? base::GbkToUtf8(info->ErrorMsg) : "");
  if (ok) {
    // Restore all subscriptions after re-login (empty on first login: no-op)
    std::vector<std::string> all;
    {
      std::lock_guard lock(mutex_);
      all.assign(subscribed_.begin(), subscribed_.end());
    }
    CallSubscribe(all, true);
  }
}

void MdSpiAdapter::OnRspSubMarketData(
    CThostFtdcSpecificInstrumentField* /*instrument*/,
    CThostFtdcRspInfoField* info,
    int /*request_id*/,
    bool /*is_last*/) {
  if (info != nullptr && info->ErrorID != 0) {
    delegate_.OnMdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

void MdSpiAdapter::OnRspError(CThostFtdcRspInfoField* info,
                              int /*request_id*/,
                              bool /*is_last*/) {
  if (info != nullptr) {
    delegate_.OnMdError(info->ErrorID, base::GbkToUtf8(info->ErrorMsg));
  }
}

void MdSpiAdapter::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* tick) {
  if (tick == nullptr) {
    return;
  }
  // Raw tap first: the verbatim wire struct, before translation -- a
  // translator bug must never poison the archive
  if (auto* sink = raw_tick_sink_.load(std::memory_order_acquire)) {
    sink->OnRawTick(*tick);
  }
  // Boundary translation: the delegate never sees CTP types
  delegate_.OnMdTick(TickFromCtp(*tick));
}

int MdSpiAdapter::CallSubscribe(std::vector<std::string> const& instruments,
                                bool subscribe) {
  if (instruments.empty()) {
    return 0;
  }
  // CTP wants char* arrays; copy to avoid const_cast-ing the caller's const
  std::vector<std::string> copies = instruments;
  std::vector<char*> ptrs;
  ptrs.reserve(copies.size());
  for (auto& s : copies) {
    ptrs.push_back(s.data());
  }
  if (subscribe) {
    return api_->SubscribeMarketData(ptrs);
  }
  return api_->UnSubscribeMarketData(ptrs);
}

}  // namespace cts::md::detail
