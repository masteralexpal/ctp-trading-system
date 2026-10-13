/**
 * @file md_gateway.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Market data gateway: CTP MdApi lifecycle and subscription management.
 */
#pragma once

#ifndef CTS_MD_MD_GATEWAY_H_
#define CTS_MD_MD_GATEWAY_H_

#include <string>
#include <vector>

#include "md/md_delegate.h"

class CThostFtdcMdApi;
class CThostFtdcMdSpi;

namespace cts::md {

struct MdGatewayConfig {
  std::string front;
  std::string broker_id;
  std::string investor_id;
  std::string password;
  std::string flow_dir;  // must be exclusive per gateway instance (.con clash)
};

class MdGateway {
 public:
  MdGateway();
  ~MdGateway();

  MdGateway(MdGateway const&) = delete;
  MdGateway& operator=(MdGateway const&) = delete;

  // Call only from non-CTP threads (Disconnect joins the callback thread)
  void Connect(MdGatewayConfig config, MdDelegate& delegate);

  // Subscribe/unsubscribe; callable from any thread. Subscriptions are restored
  // after reconnect
  int Subscribe(std::vector<std::string> const& instruments);
  int UnSubscribe(std::vector<std::string> const& instruments);

  // Idempotent. NEVER call from the CTP callback thread (joining self =
  // deadlock)
  void Disconnect();

 private:
  CThostFtdcMdApi* api_ = nullptr;
  CThostFtdcMdSpi* spi_ = nullptr;
};

}  // namespace cts::md

#endif  // CTS_MD_MD_GATEWAY_H_
