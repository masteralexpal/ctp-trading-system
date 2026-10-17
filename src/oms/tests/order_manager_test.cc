/**
 * @file order_manager_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for oms::OrderManager.
 */
#include "oms/order_manager.h"

#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace cts::oms {
namespace {

td::OrderReport MakeOrder(std::string_view ref,
                          std::string_view instrument,
                          td::OrderStatus status,
                          int total,
                          int traded) {
  td::OrderReport r{};
  std::memcpy(r.order_ref, ref.data(), ref.size());
  std::memcpy(r.instrument_id, instrument.data(), instrument.size());
  std::memcpy(r.exchange_id, "SHFE", 5);
  r.direction = td::Direction::kBuy;
  r.offset = td::Offset::kOpen;
  r.limit_price = 3500;
  r.volume_total_original = total;
  r.volume_traded = traded;
  r.volume_total = total - traded;
  r.status = status;
  return r;
}

td::TradeReport MakeTrade(std::string_view instrument,
                          td::Direction direction,
                          td::Offset offset,
                          double price,
                          int volume,
                          std::string_view trading_day,
                          std::string_view ref = "1") {
  td::TradeReport t{};
  std::memcpy(t.instrument_id, instrument.data(), instrument.size());
  std::memcpy(t.exchange_id, "SHFE", 5);
  std::memcpy(t.order_ref, ref.data(), ref.size());
  std::memcpy(t.trade_id, "t1", 3);
  std::memcpy(t.trading_day, trading_day.data(), trading_day.size());
  t.direction = direction;
  t.offset = offset;
  t.price = price;
  t.volume = volume;
  return t;
}

td::PositionRow MakeRow(std::string_view instrument,
                        td::Direction side,
                        int yd,
                        int today) {
  td::PositionRow r{};
  std::memcpy(r.instrument_id, instrument.data(), instrument.size());
  std::memcpy(r.exchange_id, "SHFE", 5);
  r.direction = side;
  r.yd_volume = yd;
  r.today_volume = today;
  return r;
}

Position const* FindPosition(std::vector<Position> const& positions,
                             std::string_view instrument,
                             td::Direction side) {
  for (auto const& p : positions) {
    if (p.instrument_id == instrument && p.side == side) {
      return &p;
    }
  }
  return nullptr;
}

TEST(OrderManagerTest, JournalKeepsLatestReportPerRef) {
  OrderManager oms;
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kPartTraded, 2, 1));
  oms.OnTdOrder(MakeOrder("2", "rb2701", td::OrderStatus::kSubmitted, 1, 0));

  auto const orders = oms.orders();
  ASSERT_EQ(orders.size(), 2);
  for (auto const& e : orders) {
    if (std::string_view(e.report.order_ref) == "1") {
      EXPECT_EQ(e.report.status, td::OrderStatus::kPartTraded);
      EXPECT_EQ(e.report.volume_traded, 1);
    }
  }
}

TEST(OrderManagerTest, OpenFillAddsTodayBucket) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3520, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->today_volume, 3);
  EXPECT_EQ(p->yd_volume, 0);
  EXPECT_DOUBLE_EQ(p->avg_price, (3500 * 2 + 3520) / 3.0);
  EXPECT_EQ(oms.trading_day(), "20261017");
}

TEST(OrderManagerTest, TradingDayRollFlipsBuckets) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261016"));
  // The night session of day T belongs to TradingDay T+1: the flip is
  // declared by the trade's TradingDay field
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3510, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 2);
  EXPECT_EQ(p->today_volume, 1);
}

TEST(OrderManagerTest, CloseTodayReducesTodayBucketOnly) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261016"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell,
                          td::Offset::kCloseToday, 3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 1);
  EXPECT_EQ(p->today_volume, 0);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0);  // multiplier defaults to 1
}

TEST(OrderManagerTest, PlainCloseEatsYesterdayFirst) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261016"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell, td::Offset::kClose,
                          3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 0);
  EXPECT_EQ(p->today_volume, 1);
}

TEST(OrderManagerTest, ShortSideRealizedPnl) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell, td::Offset::kOpen,
                          3600, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy,
                          td::Offset::kCloseToday, 3500, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kSell);
  // Flat positions with realized pnl stay visible
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->total(), 0);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0);
}

// CTP quirk: OnRtnOrder(all-traded) arrives BEFORE OnRtnTrade; the ledger
// must only follow trades
TEST(OrderManagerTest, TerminalOrderReportBeforeTrade) {
  OrderManager oms;
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 1, 1));
  EXPECT_TRUE(oms.positions().empty());  // no trade yet, no position
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->total(), 1);
}

TEST(OrderManagerTest, RealizedPnlUsesVolumeMultiple) {
  OrderManager oms;
  oms.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.exchange_id = "SHFE";
            i.volume_multiple = 10;
            return i;
          }()})));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell,
                          td::Offset::kCloseToday, 3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0 * 10);
}

TEST(OrderManagerTest, ReconcileAdoptsBrokerBuckets) {
  OrderManager oms;
  // Local tracking says: 2 long rb2701, 1 short au2512
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017"));
  oms.OnTdTrade(MakeTrade("au2512", td::Direction::kSell, td::Offset::kOpen,
                          500, 1, "20261017"));

  // Broker disagrees on rb2701 (restart missed a fill), au2512 is flat there
  auto rows = std::make_shared<std::vector<td::PositionRow> const>(
      std::vector<td::PositionRow>{
          MakeRow("rb2701", td::Direction::kBuy, 0, 3)});
  oms.OnTdPositions(rows);

  auto const positions = oms.positions();
  auto const* rb = FindPosition(positions, "rb2701", td::Direction::kBuy);
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->today_volume, 3);  // broker adopted
  EXPECT_EQ(FindPosition(positions, "au2512", td::Direction::kSell), nullptr)
      << "zeroed and flat-with-zero-pnl entries drop out of the snapshot";
}

}  // namespace
}  // namespace cts::oms
