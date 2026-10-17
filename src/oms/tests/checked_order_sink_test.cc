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

#include "oms/order_manager.h"

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
                         td::Offset offset = td::Offset::kOpen) {
  td::OrderRequest req{};
  std::strncpy(req.instrument_id, "rb2701", sizeof(req.instrument_id) - 1);
  std::strncpy(req.exchange_id, "SHFE", sizeof(req.exchange_id) - 1);
  req.direction = td::Direction::kBuy;
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
  // Closes are exempt from the position cap
  EXPECT_FALSE(
      sink_.InsertOrder(MakeReq(2, 3500, td::Offset::kCloseToday)).empty());
}

}  // namespace
}  // namespace cts::oms
