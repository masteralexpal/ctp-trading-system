/**
 * @file string_utils_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief String utilities tests.
 */
#include "base/string_utils.h"

#include <cstring>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

TEST(StringUtilsTest, CopyFits) {
  char field[16] = {};
  CopyToFixedArray(field, std::string{"rb2701"});
  EXPECT_STREQ(field, "rb2701");
}

TEST(StringUtilsTest, CopyTruncates) {
  char field[8] = {};
  CopyToFixedArray(field, std::string{"this-is-too-long"});
  EXPECT_EQ(std::strlen(field), 7u);  // truncated to N-1
  EXPECT_STREQ(field, "this-is");
}

TEST(StringUtilsTest, CopyEmpty) {
  char field[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  CopyToFixedArray(field, std::string{});
  EXPECT_STREQ(field, "");
}

TEST(StringUtilsTest, AlwaysNulTerminated) {
  // Must NUL-terminate even with no room left
  char field[4];
  std::memset(field, 'a', sizeof(field));
  CopyToFixedArray(field, std::string{"abcdef"});
  EXPECT_EQ(field[3], '\0');
}

}  // namespace
}  // namespace cts::base
