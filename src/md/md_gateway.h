/**
 * @file md_gateway.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Market data gateway: CTP MdApi lifecycle and subscription management.
 */
#pragma once

#ifndef CTS_MD_MD_GATEWAY_H_
#define CTS_MD_MD_GATEWAY_H_

#include <memory>
#include <string>
#include <vector>

#include "md/md_delegate.h"

class CThostFtdcMdSpi;

namespace cts::md {

class RawTickSink;

namespace detail {
class MdApiOps;  // test seam; production callers never touch it
}

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
  // Test seam: inject a fake MdApiOps. Defined out-of-line -- the type is
  // incomplete here, so no default argument
  void Connect(MdGatewayConfig config,
               MdDelegate& delegate,
               std::unique_ptr<detail::MdApiOps> ops);

  // Subscribe/unsubscribe; callable from any thread. Subscriptions are restored
  // after reconnect
  int Subscribe(std::vector<std::string> const& instruments);
  int UnSubscribe(std::vector<std::string> const& instruments);

  // Attach a raw tap (recorder) that receives the verbatim wire struct before
  // translation; callable before or after Connect (nullptr detaches). The sink
  // must outlive the gateway
  void SetRawTickSink(RawTickSink* sink);

  // Idempotent. NEVER call from the CTP callback thread (joining self =
  // deadlock)
  void Disconnect();

 private:
  // Owned. Raw pointer because detail::MdApiOps is incomplete here; deleted in
  // Disconnect (same pattern as spi_)
  detail::MdApiOps* api_ = nullptr;
  CThostFtdcMdSpi* spi_ = nullptr;
  RawTickSink* raw_tick_sink_ = nullptr;  // not owned
};

}  // namespace cts::md

#endif  // CTS_MD_MD_GATEWAY_H_
