/**
 * @file md_gateway.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Market data gateway: CTP MdApi lifecycle and subscription management.
 */
#include "md/md_gateway.h"

#include <utility>

#include "md/detail/md_api_ops.h"
#include "md/detail/md_spi_adapter.h"

namespace cts::md {

MdGateway::MdGateway() = default;

MdGateway::~MdGateway() {
  Disconnect();
}

void MdGateway::Connect(MdGatewayConfig config, MdDelegate& delegate) {
  auto ops = std::make_unique<detail::CtpMdApiOps>(config.flow_dir);
  Connect(std::move(config), delegate, std::move(ops));
}

void MdGateway::Connect(MdGatewayConfig config,
                        MdDelegate& delegate,
                        std::unique_ptr<detail::MdApiOps> ops) {
  if (api_ != nullptr) {
    return;
  }
  std::string front = config.front;
  spi_ = new detail::MdSpiAdapter(ops.get(), std::move(config), delegate);
  api_ = ops.release();
  api_->RegisterSpi(spi_);
  api_->RegisterFront(front.data());  // CTP copies the string internally
  api_->Init();
}

int MdGateway::Subscribe(std::vector<std::string> const& instruments) {
  if (api_ == nullptr || instruments.empty()) {
    return -1;
  }
  return static_cast<detail::MdSpiAdapter*>(spi_)->Subscribe(instruments);
}

int MdGateway::UnSubscribe(std::vector<std::string> const& instruments) {
  if (api_ == nullptr || instruments.empty()) {
    return -1;
  }
  return static_cast<detail::MdSpiAdapter*>(spi_)->UnSubscribe(instruments);
}

void MdGateway::Disconnect() {
  if (api_ == nullptr) {
    return;
  }
  api_->RegisterSpi(nullptr);
  api_->Release();  // production impl blocks until internal threads exit
  delete api_;
  api_ = nullptr;
  // CThostFtdcMdSpi has no virtual dtor; delete with the exact type
  delete static_cast<detail::MdSpiAdapter*>(spi_);
  spi_ = nullptr;
}

}  // namespace cts::md
