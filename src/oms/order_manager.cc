/**
 * @file order_manager.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OrderManager: order journal + position ledger + reconciliation.
 */
#include "oms/order_manager.h"

#include <algorithm>
#include <utility>

#include "base/time_utils.h"

#include <spdlog/spdlog.h>

namespace cts::oms {
namespace {

td::Direction Opposite(td::Direction d) {
  return d == td::Direction::kBuy ? td::Direction::kSell : td::Direction::kBuy;
}

}  // namespace

void OrderManager::OnTdOrder(td::OrderReport const& order) {
  std::lock_guard lock(mutex_);
  // Latest report wins; terminal states stay in the journal. CTP fires the
  // all-traded report BEFORE the trade reports -- the journal reflects it,
  // but only trades touch the ledger (no double counting)
  auto& entry = journal_[order.order_ref];
  entry.report = order;
  entry.updated_epoch_ms = base::NowEpochMs();
}

void OrderManager::OnTdTrade(td::TradeReport const& trade) {
  std::lock_guard lock(mutex_);
  RollTradingDay(trade.trading_day);
  ApplyTrade(trade);
}

void OrderManager::OnTdPositions(
    std::shared_ptr<std::vector<td::PositionRow> const> rows) {
  std::lock_guard lock(mutex_);
  // Broker snapshot is authoritative: adopt its buckets, warn on divergence.
  // avg_price is NOT in the snapshot (PositionRow carries volumes only) --
  // after a restart it stays approximate until new fills re-weight it
  for (auto const& row : *rows) {
    Key const key{row.instrument_id, row.direction};
    auto& pos = ledger_[key];
    if (pos.instrument_id.empty()) {
      pos.instrument_id = row.instrument_id;
      pos.side = row.direction;
    }
    if (pos.yd_volume != row.yd_volume ||
        pos.today_volume != row.today_volume) {
      SPDLOG_WARN(
          "oms: reconcile {} {} local(today={} yd={}) broker(today={} "
          "yd={}), adopting broker",
          row.instrument_id,
          row.direction == td::Direction::kBuy ? "long" : "short",
          pos.today_volume, pos.yd_volume, row.today_volume, row.yd_volume);
      pos.yd_volume = row.yd_volume;
      pos.today_volume = row.today_volume;
      if (pos.total() == 0) {
        pos.avg_price = 0;
      }
    }
  }
  // Ledger entries missing from the snapshot are flat at the broker
  for (auto& [key, pos] : ledger_) {
    bool const in_snapshot = std::any_of(
        rows->begin(), rows->end(), [&](td::PositionRow const& row) {
          return key.instrument_id == row.instrument_id &&
                 key.side == row.direction;
        });
    if (!in_snapshot && pos.total() != 0) {
      SPDLOG_WARN("oms: reconcile {} {} absent from broker snapshot, zeroing",
                  key.instrument_id,
                  key.side == td::Direction::kBuy ? "long" : "short");
      pos.yd_volume = 0;
      pos.today_volume = 0;
      pos.avg_price = 0;
    }
  }
}

std::vector<OrderEntry> OrderManager::orders() const {
  std::lock_guard lock(mutex_);
  std::vector<OrderEntry> out;
  out.reserve(journal_.size());
  for (auto const& [ref, entry] : journal_) {
    out.push_back(entry);
  }
  return out;
}

std::vector<Position> OrderManager::positions() const {
  std::lock_guard lock(mutex_);
  std::vector<Position> out;
  out.reserve(ledger_.size());
  for (auto const& [key, pos] : ledger_) {
    if (pos.total() > 0 || pos.realized_pnl != 0) {
      out.push_back(pos);
    }
  }
  return out;
}

std::string OrderManager::trading_day() const {
  std::lock_guard lock(mutex_);
  return current_day_;
}

void OrderManager::RollTradingDay(std::string_view day) {
  if (day.empty() || day == current_day_) {
    return;
  }
  if (!current_day_.empty()) {
    // TradingDay flip: every today bucket becomes yesterday. No clock math --
    // the exchange declares the boundary
    for (auto& [key, pos] : ledger_) {
      pos.yd_volume += pos.today_volume;
      pos.today_volume = 0;
    }
    SPDLOG_INFO("oms: trading day {} -> {}, buckets rolled", current_day_, day);
  }
  current_day_ = day;
}

void OrderManager::ApplyTrade(td::TradeReport const& trade) {
  bool const is_open = trade.offset == td::Offset::kOpen;
  td::Direction const side =
      is_open ? trade.direction : Opposite(trade.direction);
  Key const key{trade.instrument_id, side};
  auto& pos = ledger_[key];
  if (pos.instrument_id.empty()) {
    pos.instrument_id = trade.instrument_id;
    pos.side = side;
  }

  if (is_open) {
    double const total_cost =
        pos.avg_price * pos.total() + trade.price * trade.volume;
    pos.today_volume += trade.volume;
    pos.avg_price = total_cost / pos.total();
    return;
  }

  // Close: pick buckets per offset; plain kClose eats yesterday first
  std::int32_t left = trade.volume;
  std::int32_t from_today = 0;
  if (trade.offset == td::Offset::kCloseToday) {
    from_today = std::min(left, pos.today_volume);
  } else {
    std::int32_t const from_yd = std::min(left, pos.yd_volume);
    pos.yd_volume -= from_yd;
    left -= from_yd;
    if (trade.offset == td::Offset::kClose) {
      from_today = std::min(left, pos.today_volume);
    }
  }
  pos.today_volume -= from_today;
  left -= from_today;
  if (left > 0) {
    SPDLOG_WARN("oms: close of {} {} exceeds ledger by {} lots",
                trade.instrument_id,
                side == td::Direction::kBuy ? "long" : "short", left);
  }

  double const unit = side == td::Direction::kBuy ? trade.price - pos.avg_price
                                                  : pos.avg_price - trade.price;
  pos.realized_pnl += unit * trade.volume * VolumeMultiple(trade.instrument_id);
  if (pos.total() == 0) {
    pos.avg_price = 0;
  }
}

int OrderManager::VolumeMultiple(std::string_view instrument_id) const {
  auto const master = master_.load();
  if (master == nullptr) {
    return 1;
  }
  auto const* inst = master->Find(instrument_id);
  if (inst == nullptr || inst->volume_multiple <= 0) {
    return 1;
  }
  return inst->volume_multiple;
}

}  // namespace cts::oms
