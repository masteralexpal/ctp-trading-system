/**
 * @file sma_cross_strategy_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief SmaCrossStrategy tests via a fake OrderSink.
 */
#include "strategy/sma_cross_strategy.h"

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace cts::strategy {
namespace {

class FakeOrderSink final : public td::OrderSink {
 public:
  std::vector<td::OrderRequest> inserted;
  std::vector<std::string> canceled;
  int next_ref = 1;

  std::string InsertOrder(td::OrderRequest const& req) override {
    inserted.push_back(req);
    return "fake-" + std::to_string(next_ref++);
  }
  int CancelOrder(std::string_view order_ref) override {
    canceled.emplace_back(order_ref);
    return 0;
  }
};

md::Tick TickOf(char const* id, double price) {
  md::Tick t{};
  std::strncpy(t.instrument_id, id, sizeof(t.instrument_id) - 1);
  std::strncpy(t.exchange_id, "SHFE", sizeof(t.exchange_id) - 1);
  t.last_price = price;
  t.bid_price[0] = price - 1;
  t.ask_price[0] = price + 1;
  return t;
}

// Feeds a price series; each price becomes one tick
void FeedPrices(SmaCrossStrategy& s, std::vector<double> const& prices) {
  for (double p : prices) {
    s.OnTick(TickOf("rb2701", p));
  }
}

struct SmaCrossTest : testing::Test {
  FakeOrderSink sink;
  SmaCrossConfig config{
      .instrument_id = "rb2701",
      .exchange_id = "SHFE",
      .fast = 2,
      .slow = 3,
      .volume = 1,
      .max_pos = 1,
  };
  SmaCrossStrategy strategy{config, sink};
};

TEST_F(SmaCrossTest, InertUntilStarted) {
  FeedPrices(strategy, {100, 100, 100, 105, 105});  // cross up, but stopped
  EXPECT_TRUE(sink.inserted.empty());
}

TEST_F(SmaCrossTest, CrossUpBuysOpenAtAsk) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100});  // flat window, sign stays 0
  EXPECT_TRUE(sink.inserted.empty());
  FeedPrices(strategy, {110, 110});  // fast pulls above slow: cross
  ASSERT_EQ(sink.inserted.size(), 1u);
  auto const& req = sink.inserted[0];
  EXPECT_EQ(req.direction, td::Direction::kBuy);
  EXPECT_EQ(req.offset, td::Offset::kOpen);
  EXPECT_DOUBLE_EQ(req.limit_price, 111.0);  // ask = last + 1
  EXPECT_EQ(req.volume, 1);
  EXPECT_STREQ(req.instrument_id, "rb2701");
  EXPECT_TRUE(strategy.status().order_in_flight);
}

TEST_F(SmaCrossTest, FillUpdatesPositionAndClearsFlight) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});
  ASSERT_EQ(sink.inserted.size(), 1u);
  std::string const ref = "fake-1";

  // CTP ordering: OnRtnOrder(all-traded) arrives BEFORE OnRtnTrade. The
  // terminal report clears in-flight; the fill must still count
  td::OrderReport report{};
  std::strncpy(report.order_ref, ref.c_str(), sizeof(report.order_ref) - 1);
  report.status = td::OrderStatus::kAllTraded;
  strategy.OnTdOrder(report);
  EXPECT_FALSE(strategy.status().order_in_flight);

  td::TradeReport trade{};
  std::strncpy(trade.order_ref, ref.c_str(), sizeof(trade.order_ref) - 1);
  trade.direction = td::Direction::kBuy;
  trade.offset = td::Offset::kOpen;
  trade.volume = 1;
  trade.price = 111.0;
  strategy.OnTdTrade(trade);
  EXPECT_EQ(strategy.status().position, 1);
}

TEST_F(SmaCrossTest, CrossDownClosesLongWithCloseTodayOnShfe) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});  // long 1
  td::TradeReport trade{};
  std::strncpy(trade.order_ref, "fake-1", sizeof(trade.order_ref) - 1);
  trade.direction = td::Direction::kBuy;
  trade.volume = 1;
  strategy.OnTdTrade(trade);
  td::OrderReport report{};
  std::strncpy(report.order_ref, "fake-1", sizeof(report.order_ref) - 1);
  report.status = td::OrderStatus::kAllTraded;
  strategy.OnTdOrder(report);

  FeedPrices(strategy, {90, 90, 90});  // cross down
  ASSERT_EQ(sink.inserted.size(), 2u);
  auto const& req = sink.inserted[1];
  EXPECT_EQ(req.direction, td::Direction::kSell);
  EXPECT_EQ(req.offset, td::Offset::kCloseToday);  // SHFE: today positions
  EXPECT_DOUBLE_EQ(req.limit_price, 89.0);         // bid = last - 1
}

TEST_F(SmaCrossTest, NoSecondOrderWhileInFlight) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});
  ASSERT_EQ(sink.inserted.size(), 1u);
  // Whipsaw without a fill: the reversal cancels the working buy, but no
  // second order goes out until the working one resolves
  FeedPrices(strategy, {90, 90, 110, 110});
  EXPECT_EQ(sink.inserted.size(), 1u);
  EXPECT_EQ(sink.canceled.size(), 1u);
}

TEST_F(SmaCrossTest, ReversalCancelsThenFiresTheNewDirection) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});  // cross up: buy in flight
  ASSERT_EQ(sink.inserted.size(), 1u);

  FeedPrices(strategy, {90, 90});  // cross down: cancel, no resubmit yet
  ASSERT_EQ(sink.canceled.size(), 1u);
  EXPECT_EQ(sink.canceled[0], "fake-1");
  EXPECT_EQ(sink.inserted.size(), 1u);  // still waiting for the terminal

  td::OrderReport report{};  // the cancel lands
  std::strncpy(report.order_ref, "fake-1", sizeof(report.order_ref) - 1);
  report.status = td::OrderStatus::kCanceled;
  strategy.OnTdOrder(report);

  strategy.OnTick(TickOf("rb2701", 85));  // stale prev re-fires the intent
  ASSERT_EQ(sink.inserted.size(), 2u);
  EXPECT_EQ(sink.inserted[1].direction, td::Direction::kSell);
  EXPECT_EQ(sink.inserted[1].offset, td::Offset::kOpen);  // flat: fresh short
  EXPECT_DOUBLE_EQ(sink.inserted[1].limit_price, 84.0);   // bid
}

TEST_F(SmaCrossTest, CancelCompletionRefiresTheSameDirection) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});  // cross up: buy in flight
  ASSERT_EQ(sink.inserted.size(), 1u);

  FeedPrices(strategy, {90, 90});  // reversal: cancel, prev sign re-armed
  ASSERT_EQ(sink.canceled.size(), 1u);
  EXPECT_EQ(sink.canceled[0], "fake-1");

  td::OrderReport report{};  // the cancel lands
  std::strncpy(report.order_ref, "fake-1", sizeof(report.order_ref) - 1);
  report.status = td::OrderStatus::kCanceled;
  strategy.OnTdOrder(report);

  FeedPrices(strategy, {110, 110});  // sign back up: chase re-fires the buy
  ASSERT_EQ(sink.inserted.size(), 2u);
  EXPECT_EQ(sink.inserted[1].direction, td::Direction::kBuy);
  EXPECT_EQ(sink.inserted[1].offset, td::Offset::kOpen);
  EXPECT_DOUBLE_EQ(sink.inserted[1].limit_price, 111.0);  // ask
}

TEST_F(SmaCrossTest, RejectedOrderClearsFlight) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100, 110, 110});
  ASSERT_TRUE(strategy.status().order_in_flight);
  td::OrderReport report{};
  std::strncpy(report.order_ref, "fake-1", sizeof(report.order_ref) - 1);
  report.status = td::OrderStatus::kRejected;
  strategy.OnTdOrder(report);
  EXPECT_FALSE(strategy.status().order_in_flight);
}

TEST_F(SmaCrossTest, IgnoresOtherInstrumentsAndRefs) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 100});
  strategy.OnTick(TickOf("cu2611", 78000));  // wrong instrument
  EXPECT_TRUE(sink.inserted.empty());

  td::TradeReport trade{};  // someone else's fill
  std::strncpy(trade.order_ref, "manual-9", sizeof(trade.order_ref) - 1);
  trade.direction = td::Direction::kBuy;
  trade.volume = 5;
  strategy.OnTdTrade(trade);
  EXPECT_EQ(strategy.status().position, 0);
}

td::PositionRow RowOf(char const* id, td::Direction dir, int yd, int today) {
  td::PositionRow r{};
  std::strncpy(r.instrument_id, id, sizeof(r.instrument_id) - 1);
  r.direction = dir;
  r.yd_volume = yd;
  r.today_volume = today;
  return r;
}

TEST_F(SmaCrossTest, BrokerSnapshotResyncsPosition) {
  strategy.Start();
  // Restart recovery: the account holds a long the strategy knows nothing of
  auto rows = std::make_shared<std::vector<td::PositionRow>>();
  rows->push_back(RowOf("rb2701", td::Direction::kBuy, 1, 0));
  rows->push_back(RowOf("cu2611", td::Direction::kSell, 2, 0));  // not ours
  strategy.OnTdPositions(rows);
  EXPECT_EQ(strategy.status().position, 1);

  // Long + short legs net out; absent instruments are flat
  rows = std::make_shared<std::vector<td::PositionRow>>();
  rows->push_back(RowOf("rb2701", td::Direction::kBuy, 1, 0));
  rows->push_back(RowOf("rb2701", td::Direction::kSell, 0, 2));
  strategy.OnTdPositions(rows);
  EXPECT_EQ(strategy.status().position, -1);
}

TEST_F(SmaCrossTest, BrokerSnapshotSkippedWhileOrderInFlight) {
  strategy.Start();
  FeedPrices(strategy, {100, 100, 105});  // cross up: buy sent, in flight
  ASSERT_EQ(sink.inserted.size(), 1u);

  // A snapshot taken before the fill must not regress the local position
  strategy.OnTdPositions(
      std::make_shared<std::vector<td::PositionRow>>());  // broker still flat
  EXPECT_EQ(strategy.status().position, 0);
  EXPECT_TRUE(strategy.status().order_in_flight);
}

}  // namespace
}  // namespace cts::strategy
