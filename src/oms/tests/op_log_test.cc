/**
 * @file op_log_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for oms::OpLog.
 */
#include "oms/op_log.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace cts::oms {
namespace {

struct TempLog {
  std::filesystem::path const path =
      std::filesystem::temp_directory_path() / "cts_oplog_test.log";
  TempLog() { std::filesystem::remove(path); }
  ~TempLog() { std::filesystem::remove(path); }
};

td::OrderReport OrderOf(char const* ref, td::OrderStatus status) {
  td::OrderReport r{};
  std::strncpy(r.instrument_id, "rb2701", sizeof(r.instrument_id) - 1);
  std::strncpy(r.order_ref, ref, sizeof(r.order_ref) - 1);
  r.status = status;
  r.volume_total_original = 2;
  return r;
}

td::TradeReport TradeOf(char const* trade_id) {
  td::TradeReport t{};
  std::strncpy(t.instrument_id, "rb2701", sizeof(t.instrument_id) - 1);
  std::strncpy(t.trade_id, trade_id, sizeof(t.trade_id) - 1);
  std::strncpy(t.trading_day, "20261017", sizeof(t.trading_day) - 1);
  t.price = 3500;
  t.volume = 1;
  return t;
}

struct Collected {
  std::vector<std::pair<std::uint64_t, td::OrderReport>> orders;
  std::vector<std::pair<std::uint64_t, td::TradeReport>> trades;
  std::vector<std::pair<std::uint64_t, std::string>> intents;
};

Collected ReadAll(OpLog const& log) {
  Collected c;
  EXPECT_TRUE(log.ForEach(
      [&](std::uint64_t off, td::OrderReport const& r) {
        c.orders.emplace_back(off, r);
      },
      [&](std::uint64_t off, td::TradeReport const& t) {
        c.trades.emplace_back(off, t);
      },
      [&](std::uint64_t off, std::string_view ref, td::OrderRequest const&) {
        c.intents.emplace_back(off, std::string(ref));
      }));
  return c;
}

TEST(OpLogTest, RoundTrip) {
  TempLog tmp;
  td::OrderRequest req{};
  std::strncpy(req.instrument_id, "rb2701", sizeof(req.instrument_id) - 1);
  req.volume = 3;
  {
    OpLog log;
    ASSERT_TRUE(log.Open(tmp.path));
    log.AppendOrder(OrderOf("1", td::OrderStatus::kSubmitted));
    log.AppendTrade(TradeOf("T1"));
    log.AppendIntent("000000000007", req);
  }
  OpLog reader;
  ASSERT_TRUE(reader.Open(tmp.path));
  auto const c = ReadAll(reader);
  ASSERT_EQ(c.orders.size(), 1u);
  EXPECT_STREQ(c.orders[0].second.order_ref, "1");
  ASSERT_EQ(c.trades.size(), 1u);
  EXPECT_STREQ(c.trades[0].second.trade_id, "T1");
  ASSERT_EQ(c.intents.size(), 1u);
  EXPECT_EQ(c.intents[0].second, "000000000007");
  // Offsets strictly increase and cover the file
  EXPECT_LT(c.orders[0].first, c.trades[0].first);
  EXPECT_LT(c.trades[0].first, c.intents[0].first);
}

TEST(OpLogTest, TornTailIsSkippedAndTruncatedOnOpen) {
  TempLog tmp;
  {
    OpLog log;
    ASSERT_TRUE(log.Open(tmp.path));
    log.AppendOrder(OrderOf("1", td::OrderStatus::kSubmitted));
    log.AppendTrade(TradeOf("T1"));
  }
  // kill -9 mid-append: half a record at the tail
  {
    FILE* f = std::fopen(tmp.path.c_str(), "ab");
    std::uint8_t const garbage[] = {2, 60, 0};  // kind + truncated length
    std::fwrite(garbage, 1, sizeof(garbage), f);
    std::fclose(f);
  }
  OpLog log;
  ASSERT_TRUE(log.Open(tmp.path));  // truncates the torn tail
  auto const c = ReadAll(log);
  EXPECT_EQ(c.orders.size(), 1u);
  EXPECT_EQ(c.trades.size(), 1u);
  EXPECT_EQ(std::filesystem::file_size(tmp.path), log.size());
}

TEST(OpLogTest, ResetDropsEverything) {
  TempLog tmp;
  OpLog log;
  ASSERT_TRUE(log.Open(tmp.path));
  log.AppendTrade(TradeOf("T1"));
  log.Reset();
  log.AppendOrder(OrderOf("2", td::OrderStatus::kCanceled));
  auto const c = ReadAll(log);
  EXPECT_TRUE(c.trades.empty());
  ASSERT_EQ(c.orders.size(), 1u);
  EXPECT_STREQ(c.orders[0].second.order_ref, "2");
}

}  // namespace
}  // namespace cts::oms
