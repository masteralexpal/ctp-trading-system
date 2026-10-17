/**
 * @file order_ctp_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Boundary translation tests (td types <-> CTP trader structs).
 */
#include "td/detail/order_ctp.h"

#include <cstring>

#include <gtest/gtest.h>

namespace cts::td::detail {
namespace {

TEST(OrderCtpTest, InputOrderMapping) {
  OrderRequest req{};
  std::strcpy(req.instrument_id, "rb2701");
  std::strcpy(req.exchange_id, "SHFE");
  req.direction = Direction::kSell;
  req.offset = Offset::kCloseToday;
  req.limit_price = 3500.5;
  req.volume = 3;

  auto const f = InputOrderToCtp(req, "9999", "276491", "000000000007");
  EXPECT_STREQ(f.BrokerID, "9999");
  EXPECT_STREQ(f.InvestorID, "276491");
  EXPECT_STREQ(f.UserID, "276491");
  EXPECT_STREQ(f.OrderRef, "000000000007");
  EXPECT_STREQ(f.InstrumentID, "rb2701");
  EXPECT_STREQ(f.ExchangeID, "SHFE");
  EXPECT_EQ(f.Direction, THOST_FTDC_D_Sell);
  EXPECT_EQ(f.CombOffsetFlag[0], THOST_FTDC_OF_CloseToday);
  EXPECT_EQ(f.CombHedgeFlag[0], THOST_FTDC_HF_Speculation);
  EXPECT_EQ(f.OrderPriceType, THOST_FTDC_OPT_LimitPrice);
  EXPECT_EQ(f.TimeCondition, THOST_FTDC_TC_GFD);
  EXPECT_EQ(f.VolumeCondition, THOST_FTDC_VC_AV);
  EXPECT_EQ(f.ContingentCondition, THOST_FTDC_CC_Immediately);
  EXPECT_EQ(f.MinVolume, 1);
  EXPECT_DOUBLE_EQ(f.LimitPrice, 3500.5);
  EXPECT_EQ(f.VolumeTotalOriginal, 3);
}

TEST(OrderCtpTest, OrderStatusMapping) {
  CThostFtdcOrderField f{};
  f.OrderStatus = THOST_FTDC_OST_AllTraded;
  EXPECT_EQ(OrderReportFromCtp(f).status, OrderStatus::kAllTraded);
  f.OrderStatus = THOST_FTDC_OST_Canceled;
  EXPECT_EQ(OrderReportFromCtp(f).status, OrderStatus::kCanceled);
  f.OrderStatus = THOST_FTDC_OST_PartTradedQueueing;
  EXPECT_EQ(OrderReportFromCtp(f).status, OrderStatus::kPartTraded);
  f.OrderStatus = THOST_FTDC_OST_NoTradeQueueing;
  EXPECT_EQ(OrderReportFromCtp(f).status, OrderStatus::kSubmitted);
  f.OrderStatus = THOST_FTDC_OST_Unknown;
  EXPECT_EQ(OrderReportFromCtp(f).status, OrderStatus::kUnknown);
}

TEST(OrderCtpTest, RejectedOrderCarriesTheRequestEcho) {
  CThostFtdcInputOrderField f{};
  std::strcpy(f.InstrumentID, "cu2611");
  std::strcpy(f.OrderRef, "000000000009");
  f.Direction = THOST_FTDC_D_Buy;
  f.CombOffsetFlag[0] = THOST_FTDC_OF_Open;
  f.LimitPrice = 78000.0;
  f.VolumeTotalOriginal = 2;

  auto const r = RejectedOrderFromCtp(f);
  EXPECT_EQ(r.status, OrderStatus::kRejected);
  EXPECT_STREQ(r.instrument_id, "cu2611");
  EXPECT_STREQ(r.order_ref, "000000000009");
  EXPECT_EQ(r.volume_traded, 0);
  EXPECT_EQ(r.volume_total, 2);
}

TEST(OrderCtpTest, TradeTimeFoldsToEpochMs) {
  CThostFtdcTradeField f{};
  std::strcpy(f.TradeDate, "20261013");
  std::strcpy(f.TradeTime, "21:35:00");
  f.OffsetFlag = THOST_FTDC_OF_CloseYesterday;
  auto const r = TradeReportFromCtp(f);
  EXPECT_EQ(r.offset, Offset::kCloseYesterday);
  EXPECT_GT(r.epoch_ms, 0);
  // 2026-10-13 21:35:00 +0800 == 1791898500000 ms
  EXPECT_EQ(r.epoch_ms, 1791898500000LL);

  std::strcpy(f.TradeDate, "");  // malformed -> 0
  EXPECT_EQ(TradeReportFromCtp(f).epoch_ms, 0);
}

}  // namespace
}  // namespace cts::td::detail
