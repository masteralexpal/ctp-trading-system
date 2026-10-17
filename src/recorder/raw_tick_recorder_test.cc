/**
 * @file raw_tick_recorder_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for RawTickRecorder.
 */
#include "recorder/raw_tick_recorder.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

namespace cts::recorder {
namespace {

class RawTickRecorderTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("raw_tick_recorder_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override { std::filesystem::remove_all(dir_); }

  static CThostFtdcDepthMarketDataField MakeTick(char const* instrument,
                                                 char const* trading_day,
                                                 double price) {
    CThostFtdcDepthMarketDataField tick{};
    std::strncpy(tick.InstrumentID, instrument,
                 sizeof(tick.InstrumentID) - 1);
    std::strncpy(tick.TradingDay, trading_day, sizeof(tick.TradingDay) - 1);
    tick.LastPrice = price;
    return tick;
  }

  template <typename F>
  static bool WaitFor(F&& pred) {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
  }

  static std::vector<RawTickRecord> ReadAll(
      std::filesystem::path const& path) {
    RawTickFileReader reader;
    EXPECT_TRUE(reader.Open(path));
    std::vector<RawTickRecord> out;
    RawTickRecord record;
    while (reader.Next(record)) {
      out.push_back(record);
    }
    return out;
  }

  std::filesystem::path dir_;
};

TEST_F(RawTickRecorderTest, RecordsTicksToDayFile) {
  RawTickRecorder recorder{dir_, "test"};
  std::thread writer([&recorder] { recorder.Run(); });

  for (int i = 0; i < 5; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261016", 3500.0 + i));
  }
  ASSERT_TRUE(WaitFor([&] { return recorder.recorded() == 5; }));
  recorder.RequestStop();
  writer.join();

  auto const records = ReadAll(dir_ / "ticks_20261016.raw");
  ASSERT_EQ(records.size(), 5);
  for (std::size_t i = 0; i < records.size(); ++i) {
    EXPECT_EQ(records[i].seq, i + 1);  // producer-side seq starts at 1
    EXPECT_GT(records[i].arrival_ms, 0);
    EXPECT_STREQ(records[i].tick.InstrumentID, "rb2601");
    EXPECT_DOUBLE_EQ(records[i].tick.LastPrice, 3500.0 + i);
  }
  EXPECT_EQ(recorder.dropped(), 0);
  EXPECT_EQ(recorder.write_errors(), 0);
}

TEST_F(RawTickRecorderTest, RotatesOnTradingDayChange) {
  RawTickRecorder recorder{dir_, "test"};
  std::thread writer([&recorder] { recorder.Run(); });

  for (int i = 0; i < 3; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261016", 3500.0 + i));
  }
  for (int i = 0; i < 3; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261017", 3600.0 + i));
  }
  ASSERT_TRUE(WaitFor([&] { return recorder.recorded() == 6; }));
  recorder.RequestStop();
  writer.join();

  auto const day1 = ReadAll(dir_ / "ticks_20261016.raw");
  auto const day2 = ReadAll(dir_ / "ticks_20261017.raw");
  ASSERT_EQ(day1.size(), 3);
  ASSERT_EQ(day2.size(), 3);
  // seq is per process run: monotonic across the rotation
  for (std::uint64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(day1[i].seq, i + 1);
    EXPECT_EQ(day2[i].seq, i + 4);
  }
}

TEST_F(RawTickRecorderTest, FullQueueDropsNewestAndLeavesSeqGap) {
  constexpr std::size_t kCapacity = 65536;  // SpscQueue capacity of the member
  RawTickRecorder recorder{dir_, "test"};

  // No writer yet: fill the queue, then overflow it
  for (std::size_t i = 0; i < kCapacity; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261016", 3500.0));
  }
  for (int i = 0; i < 5; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261016", 9999.0));
  }
  EXPECT_EQ(recorder.dropped(), 5);

  std::thread writer([&recorder] { recorder.Run(); });
  ASSERT_TRUE(WaitFor([&] { return recorder.recorded() == kCapacity; }));

  // Room again: these land AFTER the dropped ones in seq space
  for (int i = 0; i < 5; ++i) {
    recorder.OnRawTick(MakeTick("rb2601", "20261016", 4000.0 + i));
  }
  ASSERT_TRUE(
      WaitFor([&] { return recorder.recorded() == kCapacity + 5; }));
  recorder.RequestStop();
  writer.join();

  auto const records = ReadAll(dir_ / "ticks_20261016.raw");
  ASSERT_EQ(records.size(), kCapacity + 5);
  for (std::size_t i = 0; i < kCapacity; ++i) {
    ASSERT_EQ(records[i].seq, i + 1);
  }
  // seq 65537..65541 are the dropped ticks; the gap is the loss marker
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(records[kCapacity + i].seq, kCapacity + 6 + i);
  }
}

TEST_F(RawTickRecorderTest, StopBeforeRunStillDrains) {
  RawTickRecorder recorder{dir_, "test"};
  for (int i = 0; i < 100; ++i) {
    recorder.OnRawTick(MakeTick("au2512", "20261016", 500.0 + i));
  }
  recorder.RequestStop();
  std::thread writer([&recorder] { recorder.Run(); });
  writer.join();
  EXPECT_EQ(recorder.recorded(), 100);
}

}  // namespace
}  // namespace cts::recorder
