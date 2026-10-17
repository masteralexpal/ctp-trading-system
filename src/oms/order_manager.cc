/**
 * @file order_manager.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OrderManager: order journal + position ledger + reconciliation.
 */
#include "oms/order_manager.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <utility>
#include <vector>

#include "base/string_utils.h"
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
  SaveLocked();
}

void OrderManager::OnTdTrade(td::TradeReport const& trade) {
  std::lock_guard lock(mutex_);
  RollTradingDay(trade.trading_day);
  ApplyTrade(trade);
  SaveLocked();
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
  SaveLocked();
}

void OrderManager::OnTdAccount(td::AccountReport const& account) {
  std::lock_guard lock(mutex_);
  account_ = account;
  SaveLocked();
}

std::optional<td::AccountReport> OrderManager::account() const {
  std::lock_guard lock(mutex_);
  return account_;
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

std::vector<OrderManager::CloseLeg> OrderManager::PlanClose(
    std::string_view instrument_id,
    td::Direction position_side,
    std::int32_t volume,
    std::string_view exchange_id) const {
  std::lock_guard lock(mutex_);
  std::int32_t today = 0;
  std::int32_t yd = 0;
  if (auto const it =
          ledger_.find(Key{std::string(instrument_id), position_side});
      it != ledger_.end()) {
    today = it->second.today_volume;
    yd = it->second.yd_volume;
  }
  bool const split = exchange_id == "SHFE" || exchange_id == "INE";
  if (!split) {
    if (volume <= today + yd) {
      return {{td::Offset::kClose, volume}};
    }
    return {};
  }
  // Today bucket first, yesterday covers the overflow
  std::vector<CloseLeg> legs;
  std::int32_t left = volume;
  if (std::int32_t const v = std::min(left, today); v > 0) {
    legs.push_back({td::Offset::kCloseToday, v});
    left -= v;
  }
  if (std::int32_t const v = std::min(left, yd); v > 0) {
    legs.push_back({td::Offset::kCloseYesterday, v});
    left -= v;
  }
  if (left > 0) {
    return {};  // position cannot cover the request
  }
  return legs;
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

// --- snapshot persistence ---

void OrderManager::SetSnapshotPath(std::filesystem::path path) {
  std::lock_guard lock(mutex_);
  snapshot_path_ = std::move(path);
}

void OrderManager::SaveSnapshot() {
  std::lock_guard lock(mutex_);
  SaveLocked();
}

void OrderManager::SaveLocked() {
  if (snapshot_path_.empty()) {
    return;
  }
  // tmp + rename: a crash mid-write leaves the last good file intact
  auto const tmp = snapshot_path_.string() + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) {
    SPDLOG_WARN("oms: snapshot open failed: {}", tmp);
    return;
  }
  std::fprintf(f, "CTSOMS 1\n");
  // Empty day (no trade seen yet) would write "day " which the strict
  // loader rejects; omit the line instead
  if (!current_day_.empty()) {
    std::fprintf(f, "day %s\n", current_day_.c_str());
  }
  if (account_) {
    std::fprintf(f, "account %.17g %.17g %.17g %.17g %.17g %.17g\n",
                 account_->balance, account_->available, account_->margin,
                 account_->frozen_margin, account_->close_profit,
                 account_->position_profit);
  }
  for (auto const& [key, pos] : ledger_) {
    std::fprintf(f, "pos %s %c %d %d %.17g %.17g\n", pos.instrument_id.c_str(),
                 pos.side == td::Direction::kBuy ? 'B' : 'S', pos.yd_volume,
                 pos.today_volume, pos.avg_price, pos.realized_pnl);
  }
  for (auto const& [ref, entry] : journal_) {
    auto const& r = entry.report;
    std::fprintf(f, "order %s %s %s %s %d %d %.17g %d %d %d %d %d %d %lld\n",
                 r.instrument_id, r.exchange_id, r.order_ref, r.order_sys_id,
                 static_cast<int>(r.direction), static_cast<int>(r.offset),
                 r.limit_price, r.volume_total_original, r.volume_traded,
                 r.volume_total, static_cast<int>(r.status), r.front_id,
                 r.session_id,
                 static_cast<long long>(entry.updated_epoch_ms));
  }
  std::fprintf(f, "end\n");
  std::fclose(f);
  if (std::rename(tmp.c_str(), snapshot_path_.c_str()) != 0) {
    SPDLOG_WARN("oms: snapshot rename failed: {}", snapshot_path_.string());
  }
}

namespace {

// from_chars on a token; false on any parse failure (no exceptions)
template <typename T>
bool ParseNum(std::string_view tok, T& out) {
  auto const r = std::from_chars(tok.data(), tok.data() + tok.size(), out);
  return r.ec == std::errc() && r.ptr == tok.data() + tok.size();
}

std::vector<std::string_view> SplitLine(std::string_view line) {
  std::vector<std::string_view> out;
  std::string_view rest = line;
  while (!rest.empty()) {
    auto const p = rest.find(' ');
    out.push_back(rest.substr(0, p));
    rest = p == std::string_view::npos ? "" : rest.substr(p + 1);
  }
  return out;
}

}  // namespace

bool OrderManager::LoadSnapshot() {
  std::lock_guard lock(mutex_);
  if (snapshot_path_.empty()) {
    return false;
  }
  FILE* f = std::fopen(snapshot_path_.c_str(), "r");
  if (f == nullptr) {
    return false;  // fresh start
  }
  std::string content;
  char buf[4096];
  while (std::size_t const n = std::fread(buf, 1, sizeof(buf), f)) {
    content.append(buf, n);
  }
  std::fclose(f);

  journal_.clear();
  ledger_.clear();
  current_day_.clear();
  account_.reset();

  bool ok = true;
  std::string_view rest = content;
  bool first_line = true;
  while (!rest.empty()) {
    auto const nl = rest.find('\n');
    std::string_view const line = rest.substr(0, nl);
    rest = nl == std::string_view::npos ? "" : rest.substr(nl + 1);
    if (line.empty()) {
      continue;
    }
    if (first_line) {
      first_line = false;
      if (line != "CTSOMS 1") {
        SPDLOG_WARN("oms: bad snapshot header: {}", line);
        return false;
      }
      continue;
    }
    if (line == "end") {
      break;
    }
    auto const tok = SplitLine(line);
    if (tok[0] == "day" && tok.size() == 2) {
      current_day_ = std::string(tok[1]);
    } else if (tok[0] == "account" && tok.size() == 7) {
      td::AccountReport acc{};
      ok &= ParseNum(tok[1], acc.balance);
      ok &= ParseNum(tok[2], acc.available);
      ok &= ParseNum(tok[3], acc.margin);
      ok &= ParseNum(tok[4], acc.frozen_margin);
      ok &= ParseNum(tok[5], acc.close_profit);
      ok &= ParseNum(tok[6], acc.position_profit);
      account_ = acc;
    } else if (tok[0] == "pos" && tok.size() == 7) {
      Position pos;
      pos.instrument_id = std::string(tok[1]);
      if (tok[2] == "B") {
        pos.side = td::Direction::kBuy;
      } else if (tok[2] == "S") {
        pos.side = td::Direction::kSell;
      } else {
        ok = false;
      }
      ok &= ParseNum(tok[3], pos.yd_volume);
      ok &= ParseNum(tok[4], pos.today_volume);
      ok &= ParseNum(tok[5], pos.avg_price);
      ok &= ParseNum(tok[6], pos.realized_pnl);
      ledger_[Key{pos.instrument_id, pos.side}] = std::move(pos);
    } else if (tok[0] == "order" && tok.size() == 15) {
      OrderEntry entry{};
      auto& r = entry.report;
      base::CopyToFixedArray(r.instrument_id, tok[1]);
      base::CopyToFixedArray(r.exchange_id, tok[2]);
      base::CopyToFixedArray(r.order_ref, tok[3]);
      base::CopyToFixedArray(r.order_sys_id, tok[4]);
      int dir = 0, off = 0, status = 0;
      ok &= ParseNum(tok[5], dir);
      ok &= ParseNum(tok[6], off);
      ok &= ParseNum(tok[7], r.limit_price);
      ok &= ParseNum(tok[8], r.volume_total_original);
      ok &= ParseNum(tok[9], r.volume_traded);
      ok &= ParseNum(tok[10], r.volume_total);
      ok &= ParseNum(tok[11], status);
      ok &= ParseNum(tok[12], r.front_id);
      ok &= ParseNum(tok[13], r.session_id);
      ok &= ParseNum(tok[14], entry.updated_epoch_ms);
      r.direction = static_cast<td::Direction>(dir);
      r.offset = static_cast<td::Offset>(off);
      r.status = static_cast<td::OrderStatus>(status);
      journal_[r.order_ref] = entry;
    } else {
      ok = false;  // unknown line: forward-compat would skip; v1 is strict
    }
    if (!ok) {
      SPDLOG_WARN("oms: snapshot parse error at line: {}", line);
      journal_.clear();
      ledger_.clear();
      current_day_.clear();
      account_.reset();
      return false;
    }
  }
  SPDLOG_INFO("oms: snapshot loaded: {} positions, {} orders, day {}",
              ledger_.size(), journal_.size(), current_day_);
  return true;
}

}  // namespace cts::oms
