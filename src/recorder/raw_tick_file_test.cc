/**
 * @file raw_tick_file_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for the raw tick journal format.
 */
#include "recorder/raw_tick_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace cts::recorder {
namespace {

class RawTickFileTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("raw_tick_file_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override { std::filesystem::remove_all(dir_); }

  std::filesystem::path Path(std::string const& name) const {
    return dir_ / name;
  }

  static RawTickRecord MakeRecord(std::uint64_t seq,
                                  char const* instrument,
                                  char const* trading_day) {
    RawTickRecord record{};
    record.seq = seq;
    record.arrival_ms = 1700000000000 + static_cast<std::int64_t>(seq);
    std::strncpy(record.tick.InstrumentID, instrument,
                 sizeof(record.tick.InstrumentID) - 1);
    std::strncpy(record.tick.TradingDay, trading_day,
                 sizeof(record.tick.TradingDay) - 1);
    record.tick.LastPrice = 3500.0 + static_cast<double>(seq);
    record.tick.Volume = static_cast<int>(seq);
    return record;
  }

  std::filesystem::path dir_;
};

TEST_F(RawTickFileTest, EmptyFileGetsHeaderOnOpen) {
  auto const path = Path("empty.raw");
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    EXPECT_TRUE(writer.is_open());
    EXPECT_EQ(writer.last_seq(), 0);
  }
  EXPECT_EQ(std::filesystem::file_size(path), sizeof(RawTickFileHeader));
}

TEST_F(RawTickFileTest, WriteThenReadRoundtrip) {
  auto const path = Path("roundtrip.raw");
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "simnow-724"));
    for (std::uint64_t i = 1; i <= 3; ++i) {
      ASSERT_TRUE(writer.Write(MakeRecord(i, "rb2601", "20261016")));
    }
    EXPECT_EQ(writer.written(), 3);
  }

  RawTickFileReader reader;
  ASSERT_TRUE(reader.Open(path));
  EXPECT_EQ(reader.source(), "simnow-724");

  for (std::uint64_t i = 1; i <= 3; ++i) {
    RawTickRecord out;
    ASSERT_TRUE(reader.Next(out)) << "seq=" << i;
    EXPECT_EQ(out.seq, i);
    EXPECT_EQ(out.arrival_ms, 1700000000000 + static_cast<std::int64_t>(i));
    EXPECT_STREQ(out.tick.InstrumentID, "rb2601");
    EXPECT_STREQ(out.tick.TradingDay, "20261016");
    EXPECT_DOUBLE_EQ(out.tick.LastPrice, 3500.0 + static_cast<double>(i));
  }
  RawTickRecord out;
  EXPECT_FALSE(reader.Next(out));  // clean EOF
  EXPECT_EQ(reader.read_count(), 3);
}

TEST_F(RawTickFileTest, RejectsBadMagic) {
  auto const path = Path("garbage.raw");
  {
    std::string const garbage(128, '\x42');
    int const fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::write(fd, garbage.data(), garbage.size()),
              static_cast<ssize_t>(garbage.size()));
    ::close(fd);
  }
  RawTickFileReader reader;
  EXPECT_FALSE(reader.Open(path));
  RawTickFileWriter writer;
  EXPECT_FALSE(writer.Open(path, "test"));
}

TEST_F(RawTickFileTest, ReaderTreatsTornTailAsEof) {
  auto const path = Path("torn_read.raw");
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    ASSERT_TRUE(writer.Write(MakeRecord(1, "rb2601", "20261016")));
    ASSERT_TRUE(writer.Write(MakeRecord(2, "rb2601", "20261016")));
  }
  // Simulate a crash mid-write: half a record appended
  {
    auto const half = MakeRecord(3, "rb2601", "20261016");
    int const fd = ::open(path.c_str(), O_WRONLY | O_APPEND);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::write(fd, &half, sizeof(half) / 2),
              static_cast<ssize_t>(sizeof(half) / 2));
    ::close(fd);
  }

  RawTickFileReader reader;
  ASSERT_TRUE(reader.Open(path));
  RawTickRecord out;
  ASSERT_TRUE(reader.Next(out));
  EXPECT_EQ(out.seq, 1);
  ASSERT_TRUE(reader.Next(out));
  EXPECT_EQ(out.seq, 2);
  EXPECT_FALSE(reader.Next(out));  // torn tail == clean EOF
}

TEST_F(RawTickFileTest, WriterRepairsTornTailOnOpen) {
  auto const path = Path("torn_repair.raw");
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    ASSERT_TRUE(writer.Write(MakeRecord(1, "rb2601", "20261016")));
    ASSERT_TRUE(writer.Write(MakeRecord(2, "rb2601", "20261016")));
  }
  {
    auto const half = MakeRecord(3, "rb2601", "20261016");
    int const fd = ::open(path.c_str(), O_WRONLY | O_APPEND);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::write(fd, &half, sizeof(half) / 2),
              static_cast<ssize_t>(sizeof(half) / 2));
    ::close(fd);
  }

  // Reopen: torn tail truncated, seq readable from the last whole record
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    EXPECT_EQ(writer.last_seq(), 2);
    EXPECT_EQ(std::filesystem::file_size(path),
              sizeof(RawTickFileHeader) + 2 * sizeof(RawTickRecord));
    ASSERT_TRUE(writer.Write(MakeRecord(3, "rb2601", "20261016")));
  }

  RawTickFileReader reader;
  ASSERT_TRUE(reader.Open(path));
  RawTickRecord out;
  for (std::uint64_t i = 1; i <= 3; ++i) {
    ASSERT_TRUE(reader.Next(out));
    EXPECT_EQ(out.seq, i);
  }
  EXPECT_FALSE(reader.Next(out));
}

TEST_F(RawTickFileTest, SameDayRestartAppends) {
  auto const path = Path("append.raw");
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    ASSERT_TRUE(writer.Write(MakeRecord(1, "rb2601", "20261016")));
  }
  // A second writer appends; the header is written only once
  {
    RawTickFileWriter writer;
    ASSERT_TRUE(writer.Open(path, "test"));
    EXPECT_EQ(writer.last_seq(), 1);
    EXPECT_EQ(writer.written(), 0);
    ASSERT_TRUE(writer.Write(MakeRecord(2, "rb2601", "20261016")));
  }
  EXPECT_EQ(std::filesystem::file_size(path),
            sizeof(RawTickFileHeader) + 2 * sizeof(RawTickRecord));

  RawTickFileReader reader;
  ASSERT_TRUE(reader.Open(path));
  RawTickRecord out;
  ASSERT_TRUE(reader.Next(out));
  EXPECT_EQ(out.seq, 1);
  ASSERT_TRUE(reader.Next(out));
  EXPECT_EQ(out.seq, 2);
  EXPECT_FALSE(reader.Next(out));
}

}  // namespace
}  // namespace cts::recorder
