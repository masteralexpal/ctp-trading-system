/**
 * @file order_sink.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Narrow order-emission seam (test double friendly).
 */
#pragma once

#ifndef CTS_TD_ORDER_SINK_H_
#define CTS_TD_ORDER_SINK_H_

#include <string>
#include <string_view>

#include "td/order.h"

namespace cts::td {

// The order-emission half of TdGateway, as an interface: strategies and
// tests depend on this, not on the concrete gateway. Same seam philosophy as
// detail::TdApiOps, one level up.
class OrderSink {
 public:
  virtual ~OrderSink() = default;

  // Returns the allocated order_ref; empty = locally rejected
  virtual std::string InsertOrder(OrderRequest const& req) = 0;
  // 0 = accepted; -1 = unknown ref or not connected
  virtual int CancelOrder(std::string_view order_ref) = 0;
};

}  // namespace cts::td

#endif  // CTS_TD_ORDER_SINK_H_
