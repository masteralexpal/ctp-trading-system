/**
 * @file checked_order_sink_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for oms::CheckedOrderSink.
 */
#include "oms/checked_order_sink.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "base/time_utils.h"
#include "oms/order_manager.h"
#include "session/session.h"

namespace cts::oms {
namespace {

struct FakeSink final : td::OrderSink {
  std::vector<td::OrderRequest> inserted;
  std::vector<std::string> canceled;
  int next_ref = 1;

  std::string InsertOrder(td::OrderRequest const& req) override {
    inserted.push_back(req);
    return std::to_string(next_ref++);
  }
  int CancelOrder(std::string_view ref) override {
    canceled.emplace_back(ref);
    return 0;
  }
};

td::OrderRequest MakeReq(int volume = 1,
                         double price = 3500.0,
                         td::Offset offset = td::Offset::kOpen,
                         td::Direction direction = td::Direction::kBuy) {
  td::OrderRequest req{};
  std::strncpy(req.instrument_id, "rb2701", sizeof(req.instrument_id) - 1);
  std::strncpy(req.exchange_id, "SHFE", sizeof(req.exchange_id) - 1);
  req.direction = direction;
  req.offset = offset;
  req.limit_price = price;
  req.volume = volume;
  return req;
}

class CheckedOrderSinkTest : public testing::Test {
 protected:
  OrderManager ledger_;
  FakeSink inner_;
  RiskLimits limits_{.max_order_volume = 5,
                     .max_position_per_instrument = 3};
  CheckedOrderSink sink_{inner_, ledger_, limits_};

  void OpenLong(int volume) {
    td::TradeReport t{};
    std::strncpy(t.instrument_id, "rb2701", sizeof(t.instrument_id) - 1);
    std::strncpy(t.trading_day, "20261017", sizeof(t.trading_day) - 1);
    t.direction = td::Direction::kBuy;
    t.offset = td::Offset::kOpen;
    t.price = 3500;
    t.volume = volume;
    ledger_.OnTdTrade(t);
  }
};

TEST_F(CheckedOrderSinkTest, PassesCleanOrdersThrough) {
  EXPECT_EQ(sink_.InsertOrder(MakeReq()), "1");
  ASSERT_EQ(inner_.inserted.size(), 1);
}

TEST_F(CheckedOrderSinkTest, HaltRejectsNewOrdersButNotCancels) {
  sink_.Halt();
  EXPECT_TRUE(sink_.InsertOrder(MakeReq()).empty());
  EXPECT_TRUE(inner_.inserted.empty());
  EXPECT_EQ(sink_.CancelOrder("42"), 0);  // cancels always pass
  ASSERT_EQ(inner_.canceled.size(), 1);

  sink_.Resume();
  EXPECT_FALSE(sink_.InsertOrder(MakeReq()).empty());
}

TEST_F(CheckedOrderSinkTest, VolumeCap) {
  // No position cap in this case: isolate the volume check
  CheckedOrderSink sink{inner_, ledger_, {.max_order_volume = 5}};
  EXPECT_TRUE(sink.InsertOrder(MakeReq(6)).empty());  // over cap 5
  EXPECT_FALSE(sink.InsertOrder(MakeReq(5)).empty());
}

TEST_F(CheckedOrderSinkTest, VolumeCapFallsBackToExchangeCap) {
  OrderManager ledger;
  FakeSink inner;
  CheckedOrderSink sink{inner, ledger,
                        {.max_order_volume = 0}};  // use the exchange cap
  sink.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.max_limit_order_volume = 3;
            return i;
          }()})));
  EXPECT_TRUE(sink.InsertOrder(MakeReq(4)).empty());
  EXPECT_FALSE(sink.InsertOrder(MakeReq(3)).empty());
}

TEST_F(CheckedOrderSinkTest, PositionCapCountsNetAfterOpen) {
  OpenLong(2);
  // cap 3, net +2: +1 ok, +2 would reach 4
  EXPECT_FALSE(sink_.InsertOrder(MakeReq(1)).empty());
  EXPECT_TRUE(sink_.InsertOrder(MakeReq(2)).empty());
  // Closes are exempt from the position cap (sell closes the long)
  EXPECT_FALSE(
      sink_
          .InsertOrder(MakeReq(2, 3500, td::Offset::kCloseToday,
                               td::Direction::kSell))
          .empty());
}

TEST_F(CheckedOrderSinkTest, MarginEstimateRejectsOverAvailable) {
  td::RateTable rates;
  td::MarginRate rate{};
  std::strncpy(rate.instrument_id, "rb2701", sizeof(rate.instrument_id) - 1);
  rate.long_ratio = 0.1;
  rate.short_ratio = 0.1;
  rates.Update(rate);
  sink_.SetRateTable(&rates);
  sink_.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.volume_multiple = 10;
            return i;
          }()})));

  td::AccountReport account{};
  account.balance = 4000;
  account.available = 4000;
  ledger_.OnTdAccount(account);

  // est = 3500 * 1 * 10 * 0.1 = 3500 > 4000 * 0.8 = 3200
  EXPECT_TRUE(sink_.InsertOrder(MakeReq(1)).empty());
  EXPECT_TRUE(inner_.inserted.empty());

  // Enough available: the same order passes
  account.available = 10000;
  ledger_.OnTdAccount(account);
  EXPECT_FALSE(sink_.InsertOrder(MakeReq(1)).empty());
}

TEST_F(CheckedOrderSinkTest, MarginCheckSkippedWhenInputsUnknown) {
  // No rate table, no account: opens must not be blocked
  EXPECT_FALSE(sink_.InsertOrder(MakeReq(1)).empty());
}

TEST_F(CheckedOrderSinkTest, OutsideTradingTimeIsRejected) {
  session::Calendar cal;
  session::SessionTable sessions;
  // A one-minute section twelve hours away from now: never "now"
  auto const dm = session::LocalDateAndMinute(base::NowEpochMs());
  int const start = (dm.second + 720) % 1440;
  sessions.Add(session::Template{.product = "rb",
                                 .sections = {{start, start + 1}}});
  sink_.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.product_id = "rb";
            return i;
          }()})));
  sink_.SetSessionGuard(&cal, &sessions);
  EXPECT_TRUE(sink_.InsertOrder(MakeReq(1)).empty());
  EXPECT_TRUE(inner_.inserted.empty());
}

TEST_F(CheckedOrderSinkTest, InsideTradingTimePasses) {
  session::Calendar cal;
  session::SessionTable sessions;
  sessions.Add(session::Template{.product = "rb", .sections = {{0, 1440}}});
  sink_.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.product_id = "rb";
            return i;
          }()})));
  sink_.SetSessionGuard(&cal, &sessions);
  EXPECT_FALSE(sink_.InsertOrder(MakeReq(1)).empty());
}

void OpenLongOn(OrderManager& ledger, char const* day, int volume) {
  td::TradeReport t{};
  std::strncpy(t.instrument_id, "rb2701", sizeof(t.instrument_id) - 1);
  std::strncpy(t.trading_day, day, sizeof(t.trading_day) - 1);
  t.direction = td::Direction::kBuy;
  t.offset = td::Offset::kOpen;
  t.price = 3500;
  t.volume = volume;
  ledger.OnTdTrade(t);
}

TEST_F(CheckedOrderSinkTest, CloseSplitsAcrossBucketsOnShfe) {
  OpenLongOn(ledger_, "20261016", 1);  // rolled to yd by the next day's trade
  OpenLongOn(ledger_, "20261017", 1);  // today
  // A blind close-today of 2 must come out as today 1 + yesterday 1
  EXPECT_FALSE(sink_
                   .InsertOrder(MakeReq(2, 3500, td::Offset::kCloseToday,
                                        td::Direction::kSell))
                   .empty());
  ASSERT_EQ(inner_.inserted.size(), 2u);
  EXPECT_EQ(inner_.inserted[0].offset, td::Offset::kCloseToday);
  EXPECT_EQ(inner_.inserted[0].volume, 1);
  EXPECT_EQ(inner_.inserted[1].offset, td::Offset::kCloseYesterday);
  EXPECT_EQ(inner_.inserted[1].volume, 1);
}

TEST_F(CheckedOrderSinkTest, CloseBeyondPositionIsRejected) {
  OpenLong(1);  // today=1 only
  EXPECT_TRUE(sink_
                  .InsertOrder(MakeReq(2, 3500, td::Offset::kCloseToday,
                                       td::Direction::kSell))
                  .empty());
  EXPECT_TRUE(inner_.inserted.empty());
}

TEST_F(CheckedOrderSinkTest, CloseOnPlainExchangeIsSingleLeg) {
  td::TradeReport t{};
  std::strncpy(t.instrument_id, "i2701", sizeof(t.instrument_id) - 1);
  std::strncpy(t.trading_day, "20261017", sizeof(t.trading_day) - 1);
  t.direction = td::Direction::kBuy;
  t.offset = td::Offset::kOpen;
  t.price = 800;
  t.volume = 2;
  ledger_.OnTdTrade(t);

  td::OrderRequest req{};
  std::strncpy(req.instrument_id, "i2701", sizeof(req.instrument_id) - 1);
  std::strncpy(req.exchange_id, "DCE", sizeof(req.exchange_id) - 1);
  req.direction = td::Direction::kSell;
  req.offset = td::Offset::kClose;
  req.limit_price = 800;
  req.volume = 2;
  EXPECT_FALSE(sink_.InsertOrder(req).empty());
  ASSERT_EQ(inner_.inserted.size(), 1u);
  EXPECT_EQ(inner_.inserted[0].offset, td::Offset::kClose);
  EXPECT_EQ(inner_.inserted[0].volume, 2);
}

}  // namespace
}  // namespace cts::oms
