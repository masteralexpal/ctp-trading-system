/**
 * @file time_utils_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Time utilities tests.
 */
#include "base/time_utils.h"

#include <gtest/gtest.h>

namespace cts::base {
namespace {

// 2026-10-13 is 20739 days after 1970-01-01
constexpr std::int64_t kAnchor =
    20739LL * 86400 * 1000;  // 2026-10-13 00:00 UTC

TEST(TimeUtilsTest, MidnightUtcIsEightAmLocal) {
  EXPECT_EQ(FormatLocalTimeOfDay(kAnchor), "08:00:00.000");
}

TEST(TimeUtilsTest, RendersMilliseconds) {
  // 13:35:00.500 UTC == 21:35:00.500 local
  EXPECT_EQ(FormatLocalTimeOfDay(kAnchor + (13 * 3600 + 35 * 60) * 1000 + 500),
            "21:35:00.500");
}

TEST(TimeUtilsTest, NegativeEpochWrapsCorrectly) {
  // 1ms before the epoch == 1969-12-31 23:59:59.999 UTC == 07:59:59.999 local
  EXPECT_EQ(FormatLocalTimeOfDay(-1), "07:59:59.999");
}

TEST(TimeUtilsTest, NextDayWrapsToZero) {
  EXPECT_EQ(FormatLocalTimeOfDay(kAnchor + 16 * 3600 * 1000), "00:00:00.000");
}

}  // namespace
}  // namespace cts::base
