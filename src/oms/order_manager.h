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
// report per order_ref, terminal states included (never erased)
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

  mutable std::mutex mutex_;
  std::unordered_map<std::string, OrderEntry> journal_;
  std::unordered_map<Key, Position, KeyHash> ledger_;
  std::string current_day_;
  std::optional<td::AccountReport> account_;
  std::atomic<std::shared_ptr<instrument::InstrumentMaster const>> master_;
};

}  // namespace cts::oms

#endif  // CTS_OMS_ORDER_MANAGER_H_
