/**
 * @file order_manager.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OrderManager: order journal + position ledger + reconciliation.
 */
#pragma once

#ifndef CTS_OMS_ORDER_MANAGER_H_
#define CTS_OMS_ORDER_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "instrument/instrument_master.h"
#include "td/td_client.h"

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

  mutable std::mutex mutex_;
  std::unordered_map<std::string, OrderEntry> journal_;
  std::unordered_map<Key, Position, KeyHash> ledger_;
  std::string current_day_;
  std::optional<td::AccountReport> account_;
  std::filesystem::path snapshot_path_;  // empty = persistence off
  std::atomic<std::shared_ptr<instrument::InstrumentMaster const>> master_;
};

}  // namespace cts::oms

#endif  // CTS_OMS_ORDER_MANAGER_H_
