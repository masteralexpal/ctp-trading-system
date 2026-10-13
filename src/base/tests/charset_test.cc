/**
 * @file charset_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief GBK/UTF-8 transcoding tests.
 */
#include "base/charset.h"

#include "base/iconv_converter.h"

#include <gtest/gtest.h>

namespace cts::base {
namespace {

TEST(CharsetTest, ConvertsGbkChinese) {
  // "正确" in GBK
  std::string const gbk = "\xD5\xFD\xC8\xB7";
  EXPECT_EQ(GbkToUtf8(gbk), "正确");
}

TEST(CharsetTest, AsciiPassthrough) {
  // ASCII is a GBK subset and passes through (most CTP messages are English)
  EXPECT_EQ(GbkToUtf8("CTP:No Error"), "CTP:No Error");
}

TEST(CharsetTest, EmptyInput) {
  EXPECT_EQ(GbkToUtf8(""), "");
}

TEST(CharsetTest, InvalidBytesPassthrough) {
  // Invalid GBK sequence: returned unchanged rather than crashing/throwing
  std::string const bad = "\xFF\xFE\x81";
  EXPECT_EQ(GbkToUtf8(bad), bad);
}

TEST(CharsetTest, ReusableAcrossCalls) {
  // Reusing one descriptor (with reset) must be stable across calls
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(GbkToUtf8("\xD5\xFD\xC8\xB7"), "正确");
  }
}

}  // namespace
}  // namespace cts::base

namespace cts::base {
namespace {

TEST(CharsetTest, Utf8ToGbk) {
  // Mirror of GbkToUtf8: UTF-8 "正确" -> GBK bytes
  EXPECT_EQ(Utf8ToGbk("正确"), "\xD5\xFD\xC8\xB7");
}

TEST(CharsetTest, RoundTrip) {
  std::string const original = "rebar rb2701 filled";
  EXPECT_EQ(GbkToUtf8(Utf8ToGbk(original)), original);
}

TEST(CharsetTest, Utf8ToGbkAsciiPassthrough) {
  EXPECT_EQ(Utf8ToGbk("CTP:No Error"), "CTP:No Error");
}

TEST(CharsetTest, UnmappableCharPassthrough) {
  // Characters GBK cannot represent (e.g. emoji): returned unchanged
  std::string const emoji = "ticker 📈";
  EXPECT_EQ(Utf8ToGbk(emoji), emoji);
}

}  // namespace
}  // namespace cts::base

namespace cts::base {
namespace {

TEST(CharsetTest, CreateFailsOnBadCharset) {
  // Factory discipline: failure returns nullopt, never throws
  EXPECT_FALSE(IconvConverter::Create("UTF-8", "NO-SUCH-CHARSET").has_value());
}

TEST(CharsetTest, MovedFromConvertsNothing) {
  auto a = IconvConverter::Create("UTF-8", "GBK");
  ASSERT_TRUE(a.has_value());
  auto b = std::move(*a);  // a is moved-from now
  EXPECT_EQ(b.Convert("\xD5\xFD\xC8\xB7"), "正确");
  // A moved-from instance is safe: returns input unchanged
  EXPECT_EQ(a->Convert("abc"), "abc");
}

}  // namespace
}  // namespace cts::base
