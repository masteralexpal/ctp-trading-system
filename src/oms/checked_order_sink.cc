/**
 * @file checked_order_sink.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Pre-trade risk gate in front of the real OrderSink.
 */
#include "oms/checked_order_sink.h"

#include "oms/order_manager.h"

#include <spdlog/spdlog.h>

namespace cts::oms {

CheckedOrderSink::CheckedOrderSink(td::OrderSink& inner,
                                   OrderManager const& ledger,
                                   RiskLimits limits)
    : inner_(inner), ledger_(ledger), limits_(limits) {}

std::string CheckedOrderSink::InsertOrder(td::OrderRequest const& req) {
  if (halted_.load(std::memory_order_acquire)) {
    SPDLOG_WARN("risk: order rejected, kill switch engaged");
    return {};
  }
  // Volume cap: configured limit wins; 0 falls back to the exchange cap
  std::int32_t cap = limits_.max_order_volume;
  if (cap <= 0) {
    if (auto const master = master_.load()) {
      if (auto const* inst = master->Find(req.instrument_id)) {
        cap = inst->max_limit_order_volume;
      }
    }
  }
  if (cap > 0 && (req.volume <= 0 || req.volume > cap)) {
    SPDLOG_WARN("risk: order rejected, volume {} over cap {}", req.volume,
                cap);
    return {};
  }
  if (req.volume <= 0) {
    return {};
  }

  if (limits_.max_position_per_instrument > 0 &&
      req.offset == td::Offset::kOpen) {
    std::int32_t net = 0;
    for (auto const& p : ledger_.positions()) {
      if (p.instrument_id == req.instrument_id) {
        net += p.side == td::Direction::kBuy ? p.total() : -p.total();
      }
    }
    std::int32_t const after =
        net + (req.direction == td::Direction::kBuy ? req.volume
                                                    : -req.volume);
    if (after > limits_.max_position_per_instrument ||
        after < -limits_.max_position_per_instrument) {
      SPDLOG_WARN("risk: order rejected, position {} would reach {}",
                  req.instrument_id, after);
      return {};
    }
  }

  return inner_.InsertOrder(req);
}

int CheckedOrderSink::CancelOrder(std::string_view order_ref) {
  return inner_.CancelOrder(order_ref);
}

}  // namespace cts::oms
