/**
 * @file tick_ctp_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief TickFromCtp boundary translation tests.
 */
#include "md/detail/tick_ctp.h"

#include <cfloat>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace cts::md {
namespace {

using detail::TickFromCtp;

// A night-session style frame: ActionDay is the calendar date, TradingDay is
// the next trading day
CThostFtdcDepthMarketDataField MakeField() {
  CThostFtdcDepthMarketDataField f{};
  std::strncpy(f.TradingDay, "20261014", sizeof(f.TradingDay) - 1);
  std::strncpy(f.ActionDay, "20261013", sizeof(f.ActionDay) - 1);
  std::strncpy(f.UpdateTime, "21:35:00", sizeof(f.UpdateTime) - 1);
  f.UpdateMillisec = 500;
  std::strncpy(f.InstrumentID, "rb2701", sizeof(f.InstrumentID) - 1);
  std::strncpy(f.ExchangeID, "SHFE", sizeof(f.ExchangeID) - 1);
  f.LastPrice = 3123.5;
  f.Volume = 1234567;
  f.Turnover = 3.85e9;
  f.OpenInterest = 987654.0;
  f.BidPrice1 = 3123.0;
  f.BidPrice2 = 3122.5;
  f.BidPrice3 = 3122.0;
  f.BidPrice4 = 3121.5;
  f.BidPrice5 = 3121.0;
  f.AskPrice1 = 3124.0;
  f.AskPrice2 = 3124.5;
  f.AskPrice3 = 3125.0;
  f.AskPrice4 = 3125.5;
  f.AskPrice5 = 3126.0;
  f.BidVolume1 = 11;
  f.BidVolume2 = 12;
  f.BidVolume3 = 13;
  f.BidVolume4 = 14;
  f.BidVolume5 = 15;
  f.AskVolume1 = 21;
  f.AskVolume2 = 22;
  f.AskVolume3 = 23;
  f.AskVolume4 = 24;
  f.AskVolume5 = 25;
  f.UpperLimitPrice = 3400.0;
  f.LowerLimitPrice = 2800.0;
  f.PreSettlementPrice = 3100.0;
  f.SettlementPrice = 0.0;  // unknown during the session
  return f;
}

TEST(TickCtpTest, FieldMapping) {
  Tick const tick = TickFromCtp(MakeField());
  EXPECT_STREQ(tick.instrument_id, "rb2701");
  EXPECT_STREQ(tick.exchange_id, "SHFE");
  // TradingDay (not ActionDay) is the exchange trading day
  EXPECT_STREQ(tick.trading_day, "20261014");
  EXPECT_DOUBLE_EQ(tick.last_price, 3123.5);
  EXPECT_EQ(tick.volume, 1234567);
  EXPECT_DOUBLE_EQ(tick.turnover, 3.85e9);
  EXPECT_DOUBLE_EQ(tick.open_interest, 987654.0);
  EXPECT_DOUBLE_EQ(tick.upper_limit_price, 3400.0);
  EXPECT_DOUBLE_EQ(tick.lower_limit_price, 2800.0);
  EXPECT_DOUBLE_EQ(tick.pre_settlement_price, 3100.0);
  EXPECT_DOUBLE_EQ(tick.settlement_price, 0.0);
}

TEST(TickCtpTest, DepthLevelsMappedInOrder) {
  Tick const tick = TickFromCtp(MakeField());
  for (std::size_t i = 0; i < Tick::kDepthLevels; ++i) {
    EXPECT_DOUBLE_EQ(tick.bid_price[i], 3123.0 - 0.5 * i);
    EXPECT_DOUBLE_EQ(tick.ask_price[i], 3124.0 + 0.5 * i);
    EXPECT_EQ(tick.bid_volume[i], static_cast<std::int32_t>(11 + i));
    EXPECT_EQ(tick.ask_volume[i], static_cast<std::int32_t>(21 + i));
  }
}

TEST(TickCtpTest, EpochMsFoldsUtcPlus8) {
  Tick const tick = TickFromCtp(MakeField());
  // 2026-10-13 is 20739 days after 1970-01-01; 21:35:00.500 +0800 equals
  // 13:35:00.500 UTC
  std::int64_t const expected =
      (20739LL * 86400 + 13 * 3600 + 35 * 60) * 1000 + 500;
  EXPECT_EQ(tick.epoch_ms, expected);
}

TEST(TickCtpTest, MalformedTimestampYieldsZeroEpoch) {
  CThostFtdcDepthMarketDataField f = MakeField();
  std::strncpy(f.ActionDay, "", sizeof(f.ActionDay) - 1);
  std::strncpy(f.UpdateTime, "ab:cd:ef", sizeof(f.UpdateTime) - 1);
  EXPECT_EQ(TickFromCtp(f).epoch_ms, 0);
}

TEST(TickCtpTest, OverlongInstrumentIdIsTruncatedSafely) {
  CThostFtdcDepthMarketDataField f = MakeField();
  std::memset(f.InstrumentID, 'x', sizeof(f.InstrumentID) - 1);
  f.InstrumentID[sizeof(f.InstrumentID) - 1] = '\0';
  Tick const tick = TickFromCtp(f);
  EXPECT_EQ(std::strlen(tick.instrument_id), sizeof(tick.instrument_id) - 1);
}

TEST(TickCtpTest, SessionStatsMapped) {
  CThostFtdcDepthMarketDataField f = MakeField();
  f.OpenPrice = 3101.0;
  f.HighestPrice = 3130.0;
  f.LowestPrice = 3095.0;
  f.PreClosePrice = 3102.0;
  Tick const tick = TickFromCtp(f);
  EXPECT_DOUBLE_EQ(tick.open_price, 3101.0);
  EXPECT_DOUBLE_EQ(tick.highest_price, 3130.0);
  EXPECT_DOUBLE_EQ(tick.lowest_price, 3095.0);
  EXPECT_DOUBLE_EQ(tick.pre_close_price, 3102.0);
}

TEST(TickCtpTest, DblMaxPlaceholdersFoldToZero) {
  CThostFtdcDepthMarketDataField f = MakeField();
  f.LastPrice = DBL_MAX;
  f.HighestPrice = DBL_MAX;
  f.AskPrice1 = DBL_MAX;  // empty ask side (limit-locked)
  f.SettlementPrice = DBL_MAX;
  Tick const tick = TickFromCtp(f);
  EXPECT_DOUBLE_EQ(tick.last_price, 0.0);
  EXPECT_DOUBLE_EQ(tick.highest_price, 0.0);
  EXPECT_DOUBLE_EQ(tick.ask_price[0], 0.0);
  EXPECT_DOUBLE_EQ(tick.settlement_price, 0.0);
  // Valid fields untouched
  EXPECT_DOUBLE_EQ(tick.bid_price[0], 3123.0);
}

}  // namespace
}  // namespace cts::md
