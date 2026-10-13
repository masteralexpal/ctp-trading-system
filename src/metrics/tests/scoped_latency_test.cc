/**
 * @file scoped_latency_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief ScopedLatency tests.
 */
#include "metrics/scoped_latency.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace cts::metrics {
namespace {

using namespace std::chrono_literals;

TEST(ScopedLatencyTest, RecordsExactlyOneSamplePerScope) {
  LatencyHistogram h;
  {
    ScopedLatency const guard{h};
    EXPECT_EQ(h.count(), 0u);  // nothing recorded until scope exit
  }
  EXPECT_EQ(h.count(), 1u);
  {
    ScopedLatency const guard{h};
  }
  EXPECT_EQ(h.count(), 2u);
}

TEST(ScopedLatencyTest, MeasuresRealElapsedTime) {
  LatencyHistogram h;
  {
    ScopedLatency const guard{h};
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(h.count(), 1u);
  // sleep_for guarantees at least the requested duration
  EXPECT_GE(h.min(), 1ms);
}

}  // namespace
}  // namespace cts::metrics
