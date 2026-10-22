/**
 * @file order_manager.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OrderManager: order journal + position ledger + reconciliation.
 */
#pragma once

#ifndef CTS_OMS_ORDER_MANAGER_H_
#define CTS_OMS_ORDER_MANAGER_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "instrument/instrument_master.h"
#include "oms/oms_listener.h"
#include "td/td_client.h"

namespace cts::base {
class TaskLoop;
}
namespace cts::td {
class OrderSink;
}

namespace cts::oms {

// Journal entry: the latest known state of one order. The FSM is implicit --
// the broker's report stream is the source of truth; we keep the newest
// report per order_ref, and terminal states are absorbing: once terminal, no
// later report changes the entry (stale replays never resurrect an order)
struct OrderEntry {
  td::OrderReport report;
  std::int64_t updated_epoch_ms;
};

// Position of one (instrument, side). today/yd buckets follow TradingDay
// semantics: "today" means opened under the CURRENT TradingDay (night
// session belongs to the next business day). The bucket boundary is declared
// by the exchange field, never computed from the local clock
struct Position {
  std::string instrument_id;
  td::Direction side;  // kBuy = long, kSell = short
  std::int32_t yd_volume{0};
  std::int32_t today_volume{0};
  double avg_price{0};     // volume-weighted open price across buckets
  double realized_pnl{0};  // signed, volume_multiple folded in

  std::int32_t total() const { return yd_volume + today_volume; }
};

// Passive bookkeeper: observes the td event stream, owns the order journal
// and the position ledger. Sends nothing -- order flow stays with OrderSink.
//
// Threading: TdClient callbacks run on the td-dispatch thread; queries are
// callable from any thread (mutex-guarded snapshots).
class OrderManager final : public td::TdClient {
 public:
  OrderManager() = default;
  ~OrderManager() override = default;

  OrderManager(OrderManager const&) = delete;
  OrderManager& operator=(OrderManager const&) = delete;

  // Any thread; supplies volume_multiple for PnL accounting
  void SetInstrumentMaster(
      std::shared_ptr<instrument::InstrumentMaster const> master) {
    master_.store(std::move(master));
  }

  // The effects output channel. Register at assembly, before events flow
  void AddListener(OmsListener& listener) { listeners_.push_back(&listener); }

  // Snapshot persistence (restart recovery). Load before any events arrive;
  // once a path is set, every order/trade event rewrites the file (tmp +
  // rename; tiny file, event rate is low). The broker snapshot still wins at
  // reconcile -- local state restores what the broker lacks: avg_price,
  // realized_pnl, the order journal
  void SetSnapshotPath(std::filesystem::path path);
  bool LoadSnapshot();  // false on missing/corrupt file (fresh start)
  void SaveSnapshot();  // public for the explicit shutdown save

  // td::TdClient (td dispatch thread)
  void OnTdConnected() override {}
  void OnTdDisconnected(int /*reason*/) override {}
  void OnTdReady(bool /*success*/, std::string_view /*msg*/) override {}
  void OnTdOrder(td::OrderReport const& order) override;
  void OnTdTrade(td::TradeReport const& trade) override;
  void OnTdPositions(
      std::shared_ptr<std::vector<td::PositionRow> const> rows) override;
  void OnTdAccount(td::AccountReport const& account) override;
  void OnTdError(int /*error_id*/, std::string_view /*msg*/) override {}

  // Any thread: snapshot copies
  std::vector<OrderEntry> orders() const;
  std::vector<Position> positions() const;

  // Local intent: an order sent but not yet acknowledged by the broker. The
  // first report for the ref (confirm OR reject) finalizes and removes it.
  // In-memory only -- a restart reconciles via the broker query, not intents
  void RegisterIntent(td::OrderRequest const& req, std::string_view order_ref);
  // Test/introspection: is this ref an unfinalized intent
  bool HasIntent(std::string_view order_ref) const;

  // In-flight open exposure of one instrument: unacked intents plus live
  // journal orders. net_lots is signed (buy opens +, sell opens -); notional
  // sums limit_price * remaining volume over both sides (pre-multiple)
  struct OpenExposure {
    std::int32_t net_lots{0};
    double notional{0};
  };
  OpenExposure InFlightOpenExposure(std::string_view instrument_id) const;

  // Watchdog: working orders with no report progress for `timeout` are
  // canceled via `sink` (pass the risk sink -- cancels always pass it).
  // Replaces tick-driven timeout logic: fires even when the market stalls.
  // The sweep re-posts itself on `loop` every timeout/2 (min 100ms). Enable
  // once at assembly; the loop must stop before this manager dies
  void EnableOrderTimeout(base::TaskLoop& loop, td::OrderSink& sink,
                          std::chrono::milliseconds timeout);
  // One watchdog step: cancel every live journal order whose last report is
  // older than timeout at now_ms. Public so tests drive it without a loop
  void SweepStaleOrders(std::int64_t now_ms, td::OrderSink& sink,
                        std::chrono::milliseconds timeout);
  // The TradingDay the ledger currently buckets against; empty before the
  // first trade
  std::string trading_day() const;
  // Latest account snapshot; nullopt before the first QueryAccount answer
  std::optional<td::AccountReport> account() const;

  struct CloseLeg {
    td::Offset offset;
    std::int32_t volume;
  };
  // Splits a close into today/yesterday legs per the ledger buckets
  // (SHFE/INE distinguish them; other exchanges close plain, single leg).
  // Empty = the position cannot cover the requested volume. Any thread
  std::vector<CloseLeg> PlanClose(std::string_view instrument_id,
                                  td::Direction position_side,
                                  std::int32_t volume,
                                  std::string_view exchange_id) const;

 private:
  struct Key {
    std::string instrument_id;
    td::Direction side;
    bool operator==(Key const&) const = default;
  };
  struct KeyHash {
    std::size_t operator()(Key const& k) const noexcept {
      return std::hash<std::string>{}(k.instrument_id) * 31 +
             static_cast<std::size_t>(k.side);
    }
  };

  // Both under mutex_
  void RollTradingDay(std::string_view day);
  void ApplyTrade(td::TradeReport const& trade);
  int VolumeMultiple(std::string_view instrument_id) const;
  void SaveLocked();  // write the snapshot; mutex_ held, path set

  struct Intent {
    std::string instrument_id;
    td::Direction direction;
    td::Offset offset;
    std::int32_t volume;
    double limit_price;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, OrderEntry> journal_;
  std::unordered_map<std::string, Intent> intents_;
  std::unordered_map<Key, Position, KeyHash> ledger_;
  // Idempotency keys of applied trades: exchange_id|trading_day|trade_id.
  // In-memory only -- the RESUME replay seam is intra-process; a process
  // restart reconciles from the broker position snapshot instead
  std::unordered_set<std::string> seen_trades_;
  // Refs the watchdog already canceled: one cancel request per order
  std::unordered_set<std::string> cancel_requested_;
  // Σ applied (deduped) fill volume per order_ref; gates the delayed
  // terminal-order effect until the fills the terminal announces have landed
  std::unordered_map<std::string, std::int32_t> filled_;
  // Terminal reports whose fills have not caught up yet; the effect fires
  // from OnTdTrade once filled_[ref] reaches the announced volume_traded
  std::unordered_map<std::string, td::OrderReport> pending_terminal_;
  std::vector<OmsListener*> listeners_;  // assembly-time only
  std::string current_day_;
  std::optional<td::AccountReport> account_;
  std::filesystem::path snapshot_path_;  // empty = persistence off
  std::atomic<std::shared_ptr<instrument::InstrumentMaster const>> master_;
};

}  // namespace cts::oms

#endif  // CTS_OMS_ORDER_MANAGER_H_
