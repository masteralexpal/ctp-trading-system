/**
 * @file sim_order_sink_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for backtest::SimOrderSink.
 */
#include "backtest/sim_order_sink.h"

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace cts::backtest {
namespace {

struct RecordingClient final : td::TdClient {
  std::vector<std::string> events;

  void OnTdConnected() override {}
  void OnTdDisconnected(int) override {}
  void OnTdReady(bool, std::string_view) override {}
  void OnTdOrder(td::OrderReport const& o) override {
    events.emplace_back(std::string("order:") + o.order_ref + ":" +
                        std::to_string(static_cast<int>(o.status)));
  }
  void OnTdTrade(td::TradeReport const& t) override {
    events.emplace_back(std::string("trade:") + t.order_ref + ":" +
                        std::to_string(t.volume));
  }
  void OnTdPositions(
      std::shared_ptr<std::vector<td::PositionRow> const>) override {}
  void OnTdAccount(td::AccountReport const&) override {}
  void OnTdError(int, std::string_view) override {}
};

td::OrderRequest MakeReq(td::Direction dir, double price, int volume = 1) {
  td::OrderRequest req{};
  std::strncpy(req.instrument_id, "rb2701", sizeof(req.instrument_id) - 1);
  std::strncpy(req.exchange_id, "SHFE", sizeof(req.exchange_id) - 1);
  req.direction = dir;
  req.offset = td::Offset::kOpen;
  req.limit_price = price;
  req.volume = volume;
  return req;
}

CThostFtdcDepthMarketDataField MakeTick(double ask, double bid) {
  CThostFtdcDepthMarketDataField tick{};
  std::strncpy(tick.InstrumentID, "rb2701", sizeof(tick.InstrumentID) - 1);
  std::strncpy(tick.TradingDay, "20261017", sizeof(tick.TradingDay) - 1);
  tick.AskPrice1 = ask;
  tick.AskVolume1 = 5;
  tick.BidPrice1 = bid;
  tick.BidVolume1 = 5;
  return tick;
}

TEST(SimOrderSinkTest, InsertFiresSubmittedImmediately) {
  SimOrderSink sink;
  RecordingClient client;
  sink.AddClient(client);
  auto const ref = sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500));
  EXPECT_EQ(ref, "000000000001");
  ASSERT_EQ(client.events.size(), 1);
  EXPECT_EQ(client.events[0], "order:000000000001:" +
                                  std::to_string(static_cast<int>(
                                      td::OrderStatus::kSubmitted)));
  EXPECT_EQ(sink.working_count(), 1);
}

TEST(SimOrderSinkTest, BuyFillsWhenAskTouchesLimit) {
  SimOrderSink sink;
  RecordingClient client;
  sink.AddClient(client);
  sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500, 2));

  sink.OnTick(MakeTick(3501, 3500));  // ask above limit: resting
  EXPECT_EQ(sink.working_count(), 1);

  sink.OnTick(MakeTick(3499, 3498));  // touched: fills at the limit
  EXPECT_EQ(sink.working_count(), 0);
  // CTP quirk preserved: all-traded report precedes the trade report
  ASSERT_EQ(client.events.size(), 3);
  EXPECT_EQ(client.events[1], "order:000000000001:" +
                                  std::to_string(static_cast<int>(
                                      td::OrderStatus::kAllTraded)));
  EXPECT_EQ(client.events[2], "trade:000000000001:2");
}

TEST(SimOrderSinkTest, SellFillsWhenBidTouchesLimit) {
  SimOrderSink sink;
  RecordingClient client;
  sink.AddClient(client);
  sink.InsertOrder(MakeReq(td::Direction::kSell, 3500));
  sink.OnTick(MakeTick(3501, 3499));
  EXPECT_EQ(sink.working_count(), 1);
  sink.OnTick(MakeTick(3502, 3500));
  EXPECT_EQ(sink.working_count(), 0);
}

TEST(SimOrderSinkTest, FeesChargedPerOffsetClass) {
  SimOrderSink sink;
  td::CommissionRate fee{};
  fee.open_by_money = 0.0001;
  fee.close_today_by_money = 0.0003;  // the SHFE-style close-today surcharge
  sink.SetFee(fee, 10);               // rb: 10 tons per lot

  sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500));
  sink.OnTick(MakeTick(3499, 3498));  // fills @3500
  EXPECT_DOUBLE_EQ(sink.total_fee(), 3500 * 10 * 0.0001);

  auto close = MakeReq(td::Direction::kSell, 3600);
  close.offset = td::Offset::kCloseToday;
  sink.InsertOrder(close);
  sink.OnTick(MakeTick(3601, 3600));  // fills @3600
  EXPECT_DOUBLE_EQ(sink.total_fee(),
                   3500 * 10 * 0.0001 + 3600 * 10 * 0.0003);
}

TEST(SimOrderSinkTest, ZeroFeeByDefault) {
  SimOrderSink sink;
  sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500));
  sink.OnTick(MakeTick(3499, 3498));
  EXPECT_DOUBLE_EQ(sink.total_fee(), 0.0);
}

TEST(SimOrderSinkTest, PartialFillCapsAtOppositeVolume) {
  SimOrderSink sink;
  RecordingClient client;
  sink.AddClient(client);
  sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500, 5));

  auto thin = MakeTick(3499, 3498);
  thin.AskVolume1 = 2;  // only 2 lots shown at the touch price
  sink.OnTick(thin);
  EXPECT_EQ(sink.working_count(), 1);  // 3 lots still working
  ASSERT_EQ(client.events.size(), 3);
  EXPECT_EQ(client.events[1],
            "order:000000000001:" +
                std::to_string(static_cast<int>(td::OrderStatus::kPartTraded)));
  EXPECT_EQ(client.events[2], "trade:000000000001:2");

  sink.OnTick(MakeTick(3499, 3498));  // ask vol 5 covers the remaining 3
  EXPECT_EQ(sink.working_count(), 0);
  ASSERT_EQ(client.events.size(), 5);
  EXPECT_EQ(client.events[3],
            "order:000000000001:" +
                std::to_string(static_cast<int>(td::OrderStatus::kAllTraded)));
  EXPECT_EQ(client.events[4], "trade:000000000001:3");
}

TEST(SimOrderSinkTest, CancelFiresCanceledAndDropsTheOrder) {
  SimOrderSink sink;
  RecordingClient client;
  sink.AddClient(client);
  auto const ref = sink.InsertOrder(MakeReq(td::Direction::kBuy, 3500));
  EXPECT_EQ(sink.CancelOrder(ref), 0);
  EXPECT_EQ(sink.working_count(), 0);
  EXPECT_EQ(sink.CancelOrder(ref), -1);  // unknown ref
  ASSERT_EQ(client.events.size(), 2);
  EXPECT_EQ(client.events[1], "order:000000000001:" +
                                  std::to_string(static_cast<int>(
                                      td::OrderStatus::kCanceled)));
}

}  // namespace
}  // namespace cts::backtest
