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
#include "base/threading/task_loop.h"
#include "base/time_utils.h"
#include "td/order_sink.h"

#include <spdlog/spdlog.h>

namespace cts::oms {
namespace {

td::Direction Opposite(td::Direction d) {
  return d == td::Direction::kBuy ? td::Direction::kSell : td::Direction::kBuy;
}

}  // namespace

void OrderManager::OnTdDisconnected(int /*reason*/) {
  std::lock_guard lock(mutex_);
  ready_ = false;  // a reconnect reopens the recovery window
}

void OrderManager::OnTdReady(bool success, std::string_view /*msg*/) {
  std::lock_guard lock(mutex_);
  ready_ = success;
}

void OrderManager::OnTdOrder(td::OrderReport const& order) {
  std::optional<td::OrderReport> effect;
  std::shared_ptr<std::vector<td::PositionRow> const> applied_rows;
  {
    std::lock_guard lock(mutex_);
    // Any report (confirm or reject) finalizes the matching intent
    intents_.erase(order.order_ref);
    // Latest report wins among live states; terminal states absorb everything
    // that arrives after them (replay, unordered sources). CTP fires the
    // all-traded report BEFORE the trade reports -- the journal reflects it,
    // but only trades touch the ledger (no double counting)
    auto const it = journal_.find(order.order_ref);
    if (it != journal_.end() && td::IsTerminal(it->second.report.status)) {
      // Terminal-vs-terminal (or terminal-vs-live) conflict: inside the
      // recovery window the query answer is newer than any RESUME replay, so
      // it overrides; once ready, the first terminal stands (docs ch.6.1)
      bool const recovery_query =
          !ready_ && order.source == td::ReportSource::kQuery;
      if (!recovery_query) {
        return;  // absorbed: no transition, no effect
      }
      SPDLOG_WARN(
          "oms: recovery query overrides journal terminal {} -> {} for {}",
          static_cast<int>(it->second.report.status),
          static_cast<int>(order.status), order.order_ref);
    }
    auto& entry = journal_[order.order_ref];
    entry.report = order;
    entry.updated_epoch_ms = base::NowEpochMs();
    if (td::IsTerminal(order.status)) {
      cancel_requested_.erase(order.order_ref);  // the ref is settled
      // CTP announces terminal states BEFORE the trailing trade details:
      // delay the effect until the announced fills have been applied, so a
      // listener never sees "flight over" before the ledger shows the fills
      if (order.volume_traded > filled_[order.order_ref]) {
        pending_terminal_[order.order_ref] = order;
      } else {
        effect = order;
      }
    } else {
      effect = order;
    }
    applied_rows = ApplyPendingLocked();
    SaveLocked();
  }
  if (effect.has_value()) {
    for (auto* l : listeners_) {
      l->OnOrderUpdate(*effect);
    }
  }
  if (applied_rows != nullptr) {
    for (auto* l : listeners_) {
      l->OnPositionsReconciled(applied_rows);
    }
  }
}

void OrderManager::RegisterIntent(td::OrderRequest const& req,
                                  std::string_view order_ref) {
  std::lock_guard lock(mutex_);
  // Empty ref = the send failed locally; a journal hit = the first report
  // raced ahead of registration -- no intent either way
  if (order_ref.empty() || journal_.contains(std::string(order_ref))) {
    return;
  }
  Intent in;
  in.instrument_id = req.instrument_id;
  in.direction = req.direction;
  in.offset = req.offset;
  in.volume = req.volume;
  in.limit_price = req.limit_price;
  intents_.emplace(order_ref, std::move(in));
}

bool OrderManager::HasIntent(std::string_view order_ref) const {
  std::lock_guard lock(mutex_);
  return intents_.contains(std::string(order_ref));
}

OrderManager::OpenExposure OrderManager::InFlightOpenExposure(
    std::string_view instrument_id) const {
  std::lock_guard lock(mutex_);
  OpenExposure e;
  auto fold = [&](td::Direction d, std::int32_t lots, double price) {
    e.net_lots += d == td::Direction::kBuy ? lots : -lots;
    e.notional += price * lots;
  };
  for (auto const& [ref, in] : intents_) {
    if (in.offset == td::Offset::kOpen && in.instrument_id == instrument_id) {
      fold(in.direction, in.volume, in.limit_price);
    }
  }
  for (auto const& [ref, entry] : journal_) {
    auto const& r = entry.report;
    if (r.offset == td::Offset::kOpen && !td::IsTerminal(r.status) &&
        r.volume_total > 0 && instrument_id == r.instrument_id) {
      fold(r.direction, r.volume_total, r.limit_price);
    }
  }
  return e;
}

namespace {

// Self-reposting watchdog task (the HealthTick pattern in main.cc)
struct TimeoutSweep {
  OrderManager* om;
  base::TaskLoop* loop;
  td::OrderSink* sink;
  std::chrono::milliseconds timeout;
  std::chrono::milliseconds period;
  void operator()() const {
    om->SweepStaleOrders(base::NowEpochMs(), *sink, timeout);
    loop->PostDelayedTask(*this, period);
  }
};

}  // namespace

void OrderManager::EnableOrderTimeout(base::TaskLoop& loop,
                                      td::OrderSink& sink,
                                      std::chrono::milliseconds timeout) {
  auto const period =
      std::max<std::chrono::milliseconds>(timeout / 2,
                                          std::chrono::milliseconds{100});
  loop.PostDelayedTask(TimeoutSweep{this, &loop, &sink, timeout, period},
                       period);
}

void OrderManager::SweepStaleOrders(std::int64_t now_ms,
                                    td::OrderSink& sink,
                                    std::chrono::milliseconds timeout) {
  std::vector<std::string> stale;
  {
    std::lock_guard lock(mutex_);
    for (auto const& [ref, entry] : journal_) {
      if (td::IsTerminal(entry.report.status) ||
          cancel_requested_.contains(ref)) {
        continue;
      }
      if (now_ms - entry.updated_epoch_ms >= timeout.count()) {
        stale.push_back(ref);
      }
    }
    for (auto const& ref : stale) {
      cancel_requested_.insert(ref);
    }
  }
  // Cancels outside the lock: the sink may re-enter the manager
  for (auto const& ref : stale) {
    SPDLOG_WARN("oms: order {} stale over {}ms, canceling", ref,
                timeout.count());
    sink.CancelOrder(ref);
  }
}

void OrderManager::OnTdTrade(td::TradeReport const& trade) {
  std::optional<td::OrderReport> terminal_effect;
  std::shared_ptr<std::vector<td::PositionRow> const> applied_rows;
  {
    std::lock_guard lock(mutex_);
    // Dedup on the exchange-side idempotency key: RESUME replay and reconnect
    // re-deliver trades. An empty trade_id cannot be told apart, so it is
    // always applied (pre-dedup behavior)
    if (trade.trade_id[0] != '\0') {
      std::string key = trade.exchange_id;
      key += '|';
      key += trade.trading_day;
      key += '|';
      key += trade.trade_id;
      if (!seen_trades_.insert(std::move(key)).second) {
        return;  // replay: no ledger change, no effect
      }
    }
    RollTradingDay(trade.trading_day);
    ApplyTrade(trade);
    filled_[trade.order_ref] += trade.volume;
    // Release a delayed terminal effect once its announced fills have landed
    if (auto const it = pending_terminal_.find(trade.order_ref);
        it != pending_terminal_.end() &&
        filled_[trade.order_ref] >= it->second.volume_traded) {
      terminal_effect = it->second;
      pending_terminal_.erase(it);
    }
    applied_rows = ApplyPendingLocked();
    SaveLocked();
  }
  for (auto* l : listeners_) {
    l->OnTradeApplied(trade);
  }
  if (terminal_effect.has_value()) {
    for (auto* l : listeners_) {
      l->OnOrderUpdate(*terminal_effect);
    }
  }
  if (applied_rows != nullptr) {
    for (auto* l : listeners_) {
      l->OnPositionsReconciled(applied_rows);
    }
  }
}

bool OrderManager::QuiescentLocked() const {
  if (!intents_.empty() || !pending_terminal_.empty()) {
    return false;
  }
  for (auto const& [ref, entry] : journal_) {
    if (!td::IsTerminal(entry.report.status)) {
      return false;
    }
  }
  return true;
}

std::shared_ptr<std::vector<td::PositionRow> const>
OrderManager::ApplyPendingLocked() {
  if (!QuiescentLocked() ||
      (pending_positions_ == nullptr && !pending_account_.has_value())) {
    return nullptr;
  }
  auto rows = std::move(pending_positions_);
  pending_positions_ = nullptr;
  if (pending_account_.has_value()) {
    account_ = *pending_account_;
    pending_account_.reset();
  }
  if (rows != nullptr) {
    ReconcileLocked(rows);
  }
  SPDLOG_INFO("oms: quiesced, deferred broker snapshot applied");
  SaveLocked();
  return rows;
}

void OrderManager::OnTdPositions(
    std::shared_ptr<std::vector<td::PositionRow> const> rows) {
  {
    std::lock_guard lock(mutex_);
    if (!QuiescentLocked()) {
      // The snapshot's sample time is unknowable and may predate in-flight
      // fills: defer until quiesce, keep only the latest (docs ch.6.2)
      SPDLOG_INFO("oms: position snapshot deferred (orders in flight)");
      pending_positions_ = std::move(rows);
      return;
    }
    ReconcileLocked(rows);
    SaveLocked();
  }
  for (auto* l : listeners_) {
    l->OnPositionsReconciled(rows);
  }
}

void OrderManager::ReconcileLocked(
    std::shared_ptr<std::vector<td::PositionRow> const> rows) {
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

void OrderManager::OnTdAccount(td::AccountReport const& account) {
  std::lock_guard lock(mutex_);
  if (!QuiescentLocked()) {
    pending_account_ = account;  // same gate as positions (docs ch.6.2)
    return;
  }
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
                 r.session_id, static_cast<long long>(entry.updated_epoch_ms));
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
