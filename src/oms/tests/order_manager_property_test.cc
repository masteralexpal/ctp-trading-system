/**
 * @file order_manager_property_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Property tests for the OMS fold: random op sequences vs invariants.
 *
 * Method: generate random event sequences (with duplicates and out-of-order
 * arrivals, as RESUME replay produces), fold them through OrderManager, and
 * check the laws of docs/op-driven-transaction.md ch.5 against a naive
 * reference model computed in-test. Fixed seed: failures reproduce.
 */
#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "oms/order_manager.h"

namespace cts::oms {
namespace {

td::TradeReport MakeTrade(char const* trade_id,
                          std::int32_t volume,
                          double price) {
  td::TradeReport t{};
  std::strncpy(t.instrument_id, "rb2701", sizeof(t.instrument_id) - 1);
  std::strncpy(t.exchange_id, "SHFE", sizeof(t.exchange_id) - 1);
  std::strncpy(t.order_ref, "000000000001", sizeof(t.order_ref) - 1);
  std::strncpy(t.trade_id, trade_id, sizeof(t.trade_id) - 1);
  std::strncpy(t.trading_day, "20261016", sizeof(t.trading_day) - 1);
  t.direction = td::Direction::kBuy;
  t.offset = td::Offset::kOpen;
  t.price = price;
  t.volume = volume;
  return t;
}

// Law: re-applying an already-seen trade (RESUME replay) must not change the
// ledger. Positions must equal the fold of UNIQUE trade ids.
TEST(OrderManagerProperty, ReplayedTradesDoNotChangeTheLedger) {
  std::mt19937 rng(20261016);
  for (int round = 0; round < 200; ++round) {
    OrderManager om;
    // Reference model: unique trade ids, first arrival order
    std::set<std::string> seen;
    std::vector<td::TradeReport> unique;

    int const n = 1 + static_cast<int>(rng() % 20);
    int next_id = 0;
    std::vector<td::TradeReport> stream;
    for (int i = 0; i < n; ++i) {
      char id[21];
      std::snprintf(id, sizeof(id), "T%05d", next_id++);
      std::int32_t const vol = 1 + static_cast<int>(rng() % 5);
      double const price = 3000.0 + static_cast<double>(rng() % 1000);
      stream.push_back(MakeTrade(id, vol, price));
      // ~40% of events get replayed right away (reconnect seam)
      if (rng() % 5 < 2) {
        stream.push_back(stream.back());
      }
    }
    // Shuffle into a random arrival order (replay interleaves with live)
    std::shuffle(stream.begin(), stream.end(), rng);

    for (auto const& t : stream) {
      om.OnTdTrade(t);
      if (seen.insert(t.trade_id).second) {
        unique.push_back(t);
      }
    }

    std::int32_t ref_volume = 0;
    double ref_cost = 0;
    for (auto const& t : unique) {
      ref_volume += t.volume;
      ref_cost += t.price * t.volume;
    }
    auto const positions = om.positions();
    ASSERT_EQ(positions.size(), 1u) << "round " << round;
    EXPECT_EQ(positions[0].today_volume, ref_volume)
        << "round " << round << ": replayed trades double-counted";
    EXPECT_NEAR(positions[0].avg_price, ref_cost / ref_volume, 1e-9)
        << "round " << round;
  }
}

td::OrderReport MakeOrder(char const* ref,
                          td::OrderStatus status,
                          std::int32_t traded) {
  td::OrderReport r{};
  std::strncpy(r.instrument_id, "rb2701", sizeof(r.instrument_id) - 1);
  std::strncpy(r.exchange_id, "SHFE", sizeof(r.exchange_id) - 1);
  std::strncpy(r.order_ref, ref, sizeof(r.order_ref) - 1);
  r.direction = td::Direction::kBuy;
  r.offset = td::Offset::kOpen;
  r.limit_price = 3500;
  r.volume_total_original = 2;
  r.volume_traded = traded;
  r.volume_total = 2 - traded;
  r.status = status;
  return r;
}

// Law (terminal absorption): once the journal shows a terminal state, stale
// reports arriving late (replay interleaved after the terminal) must not
// resurrect the order.
TEST(OrderManagerProperty, TerminalOrderIsNotResurrectedByStaleReports) {
  std::mt19937 rng(7);
  td::OrderStatus const kTerminals[] = {td::OrderStatus::kAllTraded,
                                        td::OrderStatus::kCanceled,
                                        td::OrderStatus::kRejected};
  td::OrderStatus const kLive[] = {td::OrderStatus::kSubmitted,
                                   td::OrderStatus::kPartTraded};
  for (int round = 0; round < 200; ++round) {
    OrderManager om;
    char ref[13];
    std::snprintf(ref, sizeof(ref), "%012d", round);
    td::OrderStatus const terminal = kTerminals[rng() % 3];
    int const traded = terminal == td::OrderStatus::kAllTraded ? 2 : 0;

    // Live reports, then the terminal, then stale live reports (replay)
    int const pre = 1 + static_cast<int>(rng() % 3);
    for (int i = 0; i < pre; ++i) {
      om.OnTdOrder(MakeOrder(ref, kLive[rng() % 2], 0));
    }
    om.OnTdOrder(MakeOrder(ref, terminal, traded));
    int const post = 1 + static_cast<int>(rng() % 3);
    for (int i = 0; i < post; ++i) {
      om.OnTdOrder(MakeOrder(ref, kLive[rng() % 2], 0));
    }

    auto const orders = om.orders();
    ASSERT_EQ(orders.size(), 1u) << "round " << round;
    EXPECT_TRUE(td::IsTerminal(orders[0].report.status))
        << "round " << round << ": stale report resurrected a "
        << static_cast<int>(terminal) << " order as "
        << static_cast<int>(orders[0].report.status);
  }
}

// Law (intent finalization): an intent leaves the registry on the first
// report for its ref (confirm or reject); registration after the first
// report is a no-op. A leaked intent would lock the instrument's risk
// budget forever -- the registry must equal exactly the refs that were
// registered while unreported and never reported since.
TEST(OrderManagerProperty, IntentsAlwaysFinalize) {
  std::mt19937 rng(99);
  td::OrderStatus const kAny[] = {
      td::OrderStatus::kSubmitted, td::OrderStatus::kPartTraded,
      td::OrderStatus::kAllTraded, td::OrderStatus::kCanceled,
      td::OrderStatus::kRejected};
  for (int round = 0; round < 200; ++round) {
    OrderManager om;
    std::array<bool, 8> in_journal{};
    std::array<bool, 8> expected_intent{};
    int const n = 1 + static_cast<int>(rng() % 40);
    for (int i = 0; i < n; ++i) {
      int const ref = static_cast<int>(rng() % 8);
      char ref_str[13];
      std::snprintf(ref_str, sizeof(ref_str), "%012d", ref);
      if (rng() % 2 == 0) {
        td::OrderRequest req{};
        std::strncpy(req.instrument_id, "rb2701", sizeof(req.instrument_id) - 1);
        std::strncpy(req.exchange_id, "SHFE", sizeof(req.exchange_id) - 1);
        req.direction = td::Direction::kBuy;
        req.offset = td::Offset::kOpen;
        req.limit_price = 3500;
        req.volume = 1;
        om.RegisterIntent(req, ref_str);
        if (!in_journal[ref]) {
          expected_intent[ref] = true;
        }
      } else {
        om.OnTdOrder(MakeOrder(ref_str, kAny[rng() % 5], 0));
        in_journal[ref] = true;
        expected_intent[ref] = false;
      }
    }
    for (int ref = 0; ref < 8; ++ref) {
      char ref_str[13];
      std::snprintf(ref_str, sizeof(ref_str), "%012d", ref);
      EXPECT_EQ(om.HasIntent(ref_str), expected_intent[ref])
          << "round " << round << " ref " << ref_str
          << ": intent leaked or lost";
    }
  }
}

// Control (must pass today): day roll conserves total volume.
TEST(OrderManagerProperty, DayRollConservesTotalVolume) {
  std::mt19937 rng(42);
  for (int round = 0; round < 100; ++round) {
    OrderManager om;
    std::int32_t total = 0;
    int id = 0;
    for (int day = 0; day < 3; ++day) {
      char trading_day[9];
      std::snprintf(trading_day, sizeof(trading_day), "2026101%d", 6 + day);
      int const n = 1 + static_cast<int>(rng() % 5);
      for (int i = 0; i < n; ++i) {
        char tid[21];
        std::snprintf(tid, sizeof(tid), "T%05d", id++);
        auto t = MakeTrade(tid, 1 + static_cast<int>(rng() % 4), 3500.0);
        std::strncpy(t.trading_day, trading_day, sizeof(t.trading_day) - 1);
        om.OnTdTrade(t);
        total += t.volume;
      }
    }
    auto const positions = om.positions();
    ASSERT_EQ(positions.size(), 1u) << "round " << round;
    EXPECT_EQ(positions[0].total(), total) << "round " << round;
  }
}

}  // namespace
}  // namespace cts::oms
