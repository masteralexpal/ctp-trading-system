/**
 * @file td_gateway.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Trading gateway: CTP TraderApi lifecycle, order insert/cancel.
 */
#include "td/td_gateway.h"

#include <utility>

#include "td/detail/td_api_ops.h"
#include "td/detail/td_spi_adapter.h"

namespace cts::td {

TdGateway::TdGateway() = default;

TdGateway::~TdGateway() {
  Disconnect();
}

void TdGateway::Connect(TdGatewayConfig config, TdDelegate& delegate) {
  auto ops = std::make_unique<detail::CtpTdApiOps>(config.flow_dir);
  Connect(std::move(config), delegate, std::move(ops));
}

void TdGateway::Connect(TdGatewayConfig config,
                        TdDelegate& delegate,
                        std::unique_ptr<detail::TdApiOps> ops) {
  if (api_ != nullptr) {
    return;
  }
  std::string front = config.front;
  spi_ = new detail::TdSpiAdapter(ops.get(), std::move(config), delegate,
                                  rates_);
  api_ = ops.release();
  api_->RegisterSpi(spi_);
  // THOST_TERT_RESUME (=1): after a reconnect the broker resends the
  // private/public flow from the .con bookmark -- reports during the outage
  // are not lost. RESTART would replay the whole trading day; QUICK would
  // skip the outage window entirely
  api_->SubscribePrivateTopic(1);
  api_->SubscribePublicTopic(1);
  api_->RegisterFront(front.data());  // CTP copies the string internally
  api_->Init();
}

std::string TdGateway::InsertOrder(OrderRequest const& req) {
  if (api_ == nullptr) {
    return {};
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->InsertOrder(req);
}

int TdGateway::CancelOrder(std::string_view order_ref) {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->CancelOrder(order_ref);
}

int TdGateway::QueryInstruments() {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->QueryInstruments();
}

int TdGateway::QueryPositions() {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->QueryPositions();
}

int TdGateway::QueryAccount() {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->QueryAccount();
}

int TdGateway::QueryMarginRate(std::string_view instrument_id) {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->QueryMarginRate(
      instrument_id);
}

int TdGateway::QueryCommissionRate(std::string_view instrument_id) {
  if (api_ == nullptr) {
    return -1;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->QueryCommissionRate(
      instrument_id);
}

std::shared_ptr<instrument::InstrumentMaster const> TdGateway::instruments()
    const {
  if (api_ == nullptr) {
    return nullptr;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->instruments();
}

bool TdGateway::ready() const {
  if (api_ == nullptr) {
    return false;
  }
  return static_cast<detail::TdSpiAdapter*>(spi_)->ready();
}

void TdGateway::Disconnect() {
  if (api_ == nullptr) {
    return;
  }
  api_->RegisterSpi(nullptr);
  api_->Release();  // production impl blocks until internal threads exit
  delete api_;
  api_ = nullptr;
  // CThostFtdcTraderSpi has no virtual dtor; delete with the exact type
  delete static_cast<detail::TdSpiAdapter*>(spi_);
  spi_ = nullptr;
}

}  // namespace cts::td
