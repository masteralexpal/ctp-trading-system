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

  // Margin estimate for opens: price * volume * multiple * ratio vs the
  // account's available. Skipped (not failed) when any input is unknown --
  // a missing rate table must not block order flow
  if (req.offset == td::Offset::kOpen) {
    auto const* rates = rates_.load(std::memory_order_acquire);
    auto const account = ledger_.account();
    auto const master = master_.load();
    if (rates != nullptr && account.has_value() && master != nullptr) {
      auto const rate = rates->FindMargin(req.instrument_id);
      auto const* inst = master->Find(req.instrument_id);
      if (rate.has_value() && inst != nullptr && inst->volume_multiple > 0) {
        double const ratio = req.direction == td::Direction::kBuy
                                 ? rate->long_ratio
                                 : rate->short_ratio;
        double const estimate = req.limit_price * req.volume *
                                inst->volume_multiple * ratio;
        if (estimate > account->available * limits_.max_margin_usage) {
          SPDLOG_WARN(
              "risk: order rejected, margin estimate {:.0f} exceeds {:.0f} "
              "(available {:.0f} * {:.2f})",
              estimate, account->available * limits_.max_margin_usage,
              account->available, limits_.max_margin_usage);
          return {};
        }
      }
    }
  }

  // Closes: the requester's today/yesterday guess is corrected against the
  // ledger buckets (SHFE/INE split); one request may become two legs
  if (req.offset != td::Offset::kOpen) {
    td::Direction const side = req.direction == td::Direction::kBuy
                                   ? td::Direction::kSell
                                   : td::Direction::kBuy;
    auto const legs =
        ledger_.PlanClose(req.instrument_id, side, req.volume, req.exchange_id);
    if (legs.empty()) {
      SPDLOG_WARN("risk: close rejected, position cannot cover {} lots of {}",
                  req.volume, req.instrument_id);
      return {};
    }
    std::string first_ref;
    for (auto const& leg : legs) {
      td::OrderRequest r = req;
      r.offset = leg.offset;
      r.volume = leg.volume;
      std::string const ref = inner_.InsertOrder(r);
      if (ref.empty()) {
        SPDLOG_WARN("risk: close leg rejected, {} lots of {} unsent",
                    leg.volume, req.instrument_id);
      } else if (first_ref.empty()) {
        first_ref = ref;
      }
    }
    return first_ref;
  }

  return inner_.InsertOrder(req);
}

int CheckedOrderSink::CancelOrder(std::string_view order_ref) {
  return inner_.CancelOrder(order_ref);
}

}  // namespace cts::oms
