/**
 * @file checked_order_sink.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Pre-trade risk gate in front of the real OrderSink.
 */
#pragma once

#ifndef CTS_OMS_CHECKED_ORDER_SINK_H_
#define CTS_OMS_CHECKED_ORDER_SINK_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "instrument/instrument_master.h"
#include "td/order_sink.h"
#include "td/rate_table.h"

namespace cts::oms {

class OrderManager;

struct RiskLimits {
  // Lots per order; 0 = fall back to the exchange cap (MaxLimitOrderVolume)
  std::int32_t max_order_volume{10};
  // Net position cap per instrument; 0 = unlimited
  std::int32_t max_position_per_instrument{0};
  // Estimated margin of an open must fit under available * this ratio;
  // skipped when the rate or the account snapshot is unknown
  double max_margin_usage{0.8};
};

// Synchronous pre-trade checks; a reject returns an empty ref, the same
// signal as a local reject from the gateway. Cheap and dumb on purpose:
// every check is O(1)-ish against the ledger snapshot
class CheckedOrderSink final : public td::OrderSink {
 public:
  CheckedOrderSink(td::OrderSink& inner,
                   OrderManager const& ledger,
                   RiskLimits limits);

  std::string InsertOrder(td::OrderRequest const& req) override;
  // Cancels ALWAYS pass through, halted or not
  int CancelOrder(std::string_view order_ref) override;

  void SetInstrumentMaster(
      std::shared_ptr<instrument::InstrumentMaster const> master) {
    master_.store(std::move(master));
  }
  // Any thread; the table is gateway-owned and outlives this sink
  void SetRateTable(td::RateTable const* rates) {
    rates_.store(rates, std::memory_order_release);
  }

  // Kill switch: reject new orders until Resume
  void Halt() { halted_.store(true, std::memory_order_release); }
  void Resume() { halted_.store(false, std::memory_order_release); }
  bool halted() const { return halted_.load(std::memory_order_acquire); }

 private:
  td::OrderSink& inner_;
  OrderManager const& ledger_;
  RiskLimits const limits_;
  std::atomic<bool> halted_{false};
  std::atomic<std::shared_ptr<instrument::InstrumentMaster const>> master_;
  std::atomic<td::RateTable const*> rates_{nullptr};
};

}  // namespace cts::oms

#endif  // CTS_OMS_CHECKED_ORDER_SINK_H_
