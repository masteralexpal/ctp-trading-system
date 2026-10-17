/**
 * @file backtest_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief End-to-end test: synthesize a raw journal, replay it, check the
 *        ledger.
 */
#include "backtest/backtest.h"

#include <cstring>
#include <filesystem>

#include <gtest/gtest.h>

#include "recorder/raw_tick_file.h"

namespace cts::backtest {
namespace {

recorder::RawTickRecord MakeRecord(std::uint64_t seq, double price) {
  recorder::RawTickRecord record{};
  record.seq = seq;
  record.arrival_ms = static_cast<std::int64_t>(seq) * 500;
  std::strncpy(record.tick.InstrumentID, "rb2701",
               sizeof(record.tick.InstrumentID) - 1);
  std::strncpy(record.tick.ExchangeID, "SHFE",
               sizeof(record.tick.ExchangeID) - 1);
  std::strncpy(record.tick.TradingDay, "20261017",
               sizeof(record.tick.TradingDay) - 1);
  record.tick.LastPrice = price;
  record.tick.BidPrice1 = price;
  record.tick.BidVolume1 = 10;
  record.tick.AskPrice1 = price;
  record.tick.AskVolume1 = 10;
  return record;
}

TEST(BacktestTest, ReplaysJournalThroughStrategyAndLedger) {
  // fast=2/slow=4: 4 flats, a dip crosses down (short opens), a rally crosses
  // up (short closes at a loss of (90-110) * 1 lot * multiple 1 = -20)
  double const prices[] = {100, 100, 100, 100, 90, 90, 100, 110, 110};
  auto const path = std::filesystem::temp_directory_path() /
                    "cts_backtest_test_ticks.raw";
  std::filesystem::remove(path);
  {
    recorder::RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    for (std::size_t i = 0; i < std::size(prices); ++i) {
      ASSERT_TRUE(writer.Write(MakeRecord(i + 1, prices[i])));
    }
  }

  strategy::SmaCrossConfig config{};
  config.instrument_id = "rb2701";
  config.exchange_id = "SHFE";
  config.fast = 2;
  config.slow = 4;
  config.volume = 1;

  auto const result = RunBacktest(path, config);
  EXPECT_EQ(result.ticks, std::size(prices));
  EXPECT_EQ(result.orders_submitted, 2);  // open short + close
  EXPECT_DOUBLE_EQ(result.realized_pnl, -20.0);
  for (auto const& p : result.positions) {
    EXPECT_EQ(p.total(), 0);  // round trip: nothing left open
  }
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace cts::backtest
