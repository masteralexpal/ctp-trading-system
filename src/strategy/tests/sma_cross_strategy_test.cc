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
  int next_ref = 1;

  std::string InsertOrder(td::OrderRequest const& req) override {
    inserted.push_back(req);
    return "fake-" + std::to_string(next_ref++);
  }
  int CancelOrder(std::string_view /*order_ref*/) override { return 0; }
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
  // Whipsaw without a fill: no further orders until the working one resolves
  FeedPrices(strategy, {90, 90, 110, 110});
  EXPECT_EQ(sink.inserted.size(), 1u);
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

}  // namespace
}  // namespace cts::strategy
