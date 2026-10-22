/**
 * @file sma_cross_strategy.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Dual-SMA crossover strategy (single instrument, ±1 position).
 */
#include "strategy/sma_cross_strategy.h"

#include <string_view>

#include <spdlog/spdlog.h>

#include "base/string_utils.h"

namespace cts::strategy {
namespace {

bool NeedsCloseToday(std::string_view exchange_id) {
  return exchange_id == "SHFE" || exchange_id == "INE";
}

double Mean(std::deque<double> const& v, std::size_t last_n) {
  double sum = 0.0;
  for (std::size_t i = v.size() - last_n; i < v.size(); ++i) {
    sum += v[i];
  }
  return sum / static_cast<double>(last_n);
}

}  // namespace

SmaCrossStrategy::SmaCrossStrategy(SmaCrossConfig config,
                                   td::OrderSink& order_sink)
    : Strategy("sma-cross:" + config.instrument_id),
      config_(std::move(config)),
      order_sink_(order_sink) {}

void SmaCrossStrategy::OnTick(md::Tick const& tick) {
  if (!running() || config_.instrument_id != tick.instrument_id) {
    return;
  }

  // Decide under the lock; emit outside it (the sink may call back fast)
  bool submit = false;
  bool cancel = false;
  std::string cancel_ref;
  td::OrderRequest req{};
  {
    std::lock_guard lock(mutex_);
    prices_.push_back(tick.last_price);
    if (prices_.size() > static_cast<std::size_t>(config_.slow)) {
      prices_.pop_front();
    }
    if (prices_.size() < static_cast<std::size_t>(config_.slow)) {
      return;  // window not full yet
    }
    fast_sma_ = Mean(prices_, static_cast<std::size_t>(config_.fast));
    slow_sma_ = Mean(prices_, static_cast<std::size_t>(config_.slow));
    int const sign =
        fast_sma_ > slow_sma_ ? 1 : (fast_sma_ < slow_sma_ ? -1 : 0);

    if (order_in_flight_) {
      // Working order: cancel once on reversal; prev_sign_ stays stale so the
      // intent re-fires the tick after the flight ends. Stale-order cancels
      // are the OMS watchdog's job, not the strategy's
      if (!cancel_sent_ && sign != 0) {
        bool const reversed =
            (sign > 0 && working_dir_ == td::Direction::kSell) ||
            (sign < 0 && working_dir_ == td::Direction::kBuy);
        if (reversed) {
          cancel = true;
          cancel_ref = working_ref_;
          cancel_sent_ = true;
          // Re-arm the edge detector: the intent (chase or reversal) must
          // re-fire on the first tick after the flight ends, even when the
          // sign never changed
          prev_sign_ = 0;
          SPDLOG_INFO("strategy {}: cancel {} (signal reversed)", name(),
                      working_ref_);
        }
      }
    } else {
      if (sign != 0 && sign != prev_sign_) {
        bool const want_long = sign > 0;
        bool const flat_or_short = position_ <= 0;
        bool const flat_or_long = position_ >= 0;
        if ((want_long && flat_or_short) || (!want_long && flat_or_long)) {
          bool const closing = position_ != 0;
          req.direction =
              want_long ? td::Direction::kBuy : td::Direction::kSell;
          if (!closing) {
            req.offset = td::Offset::kOpen;
          } else {
            // The risk sink re-splits this guess against the ledger buckets
            req.offset = NeedsCloseToday(config_.exchange_id)
                             ? td::Offset::kCloseToday
                             : td::Offset::kClose;
          }
          // Aggressive limit: cross the book, stay a limit order
          req.limit_price = want_long ? tick.ask_price[0] : tick.bid_price[0];
          req.volume = config_.volume;
          base::CopyToFixedArray(req.instrument_id, config_.instrument_id);
          base::CopyToFixedArray(req.exchange_id, config_.exchange_id);
          submit = req.limit_price > 0.0;
        }
      }
      prev_sign_ = sign;
    }
  }

  if (cancel) {
    order_sink_.CancelOrder(cancel_ref);
    return;
  }
  if (submit) {
    std::string const ref = order_sink_.InsertOrder(req);
    std::lock_guard lock(mutex_);
    if (!ref.empty()) {
      order_in_flight_ = true;
      working_ref_ = ref;
      working_dir_ = req.direction;
      cancel_sent_ = false;
      own_refs_.insert(ref);
      SPDLOG_INFO("strategy {}: {} {} {} @{} ref={}", name(),
                  req.direction == td::Direction::kBuy ? "buy" : "sell",
                  config_.instrument_id, req.volume, req.limit_price, ref);
    }
  }
}

void SmaCrossStrategy::OnOrderUpdate(td::OrderReport const& order) {
  std::lock_guard lock(mutex_);
  if (order.order_ref != working_ref_) {
    return;  // not ours (manual orders share the stream)
  }
  if (order.status == td::OrderStatus::kAllTraded ||
      order.status == td::OrderStatus::kCanceled ||
      order.status == td::OrderStatus::kRejected) {
    order_in_flight_ = false;
    cancel_sent_ = false;
    if (order.status == td::OrderStatus::kRejected) {
      SPDLOG_WARN("strategy {}: order {} rejected", name(), working_ref_);
    }
    working_ref_.clear();
  }
}

void SmaCrossStrategy::OnTradeApplied(td::TradeReport const& trade) {
  std::lock_guard lock(mutex_);
  if (!own_refs_.contains(trade.order_ref)) {
    return;  // not ours
  }
  // Net position: buys add, sells remove (offset is irrelevant for netting)
  int const signed_volume =
      trade.direction == td::Direction::kBuy ? trade.volume : -trade.volume;
  position_ += signed_volume;
  SPDLOG_INFO("strategy {}: filled {} {} @{} position={}", name(),
              trade.direction == td::Direction::kBuy ? "buy" : "sell",
              trade.volume, trade.price, position_);
}

void SmaCrossStrategy::OnPositionsReconciled(
    std::shared_ptr<std::vector<td::PositionRow> const> rows) {
  std::lock_guard lock(mutex_);
  if (order_in_flight_) {
    return;  // the snapshot may predate the in-flight fill; wait it out
  }
  // Broker truth for our instrument only; absent rows mean flat
  int net = 0;
  for (auto const& row : *rows) {
    if (config_.instrument_id == row.instrument_id) {
      int const total = row.yd_volume + row.today_volume;
      net += row.direction == td::Direction::kBuy ? total : -total;
    }
  }
  if (net != position_) {
    SPDLOG_WARN("strategy {}: position resync {} -> {} (broker snapshot)",
                name(), position_, net);
    position_ = net;
  }
}

SmaCrossStrategy::Status SmaCrossStrategy::status() const {
  std::lock_guard lock(mutex_);
  return {
      .position = position_,
      .order_in_flight = order_in_flight_,
      .fast_sma = fast_sma_,
      .slow_sma = slow_sma_,
      .working_ref = working_ref_,
  };
}

}  // namespace cts::strategy
