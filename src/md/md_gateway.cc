/**
 * @file md_gateway.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Market data gateway: CTP MdApi lifecycle and subscription management.
 */
#include "md/md_gateway.h"

#include <filesystem>
#include <utility>

#include "ThostFtdcMdApi.h"
#include "md/detail/md_spi_adapter.h"

namespace cts::md {

MdGateway::MdGateway() = default;

MdGateway::~MdGateway() {
  Disconnect();
}

void MdGateway::Connect(MdGatewayConfig config, MdDelegate& delegate) {
  if (api_ != nullptr) {
    return;
  }
  // The SDK concatenates filenames raw; the trailing slash is contractual
  if (config.flow_dir.empty() || config.flow_dir.back() != '/') {
    config.flow_dir += '/';
  }
  std::filesystem::create_directories(config.flow_dir);

  std::string front = config.front;
  api_ = CThostFtdcMdApi::CreateFtdcMdApi(config.flow_dir.c_str());
  spi_ = new detail::MdSpiAdapter(api_, std::move(config), delegate);
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
  api_->Release();  // blocks until internal threads exit
  api_ = nullptr;
  // CThostFtdcMdSpi has no virtual dtor; delete with the exact type
  delete static_cast<detail::MdSpiAdapter*>(spi_);
  spi_ = nullptr;
}

}  // namespace cts::md
