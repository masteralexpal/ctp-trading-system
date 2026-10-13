/**
 * @file latency_histogram_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief LatencyHistogram tests.
 */
#include "metrics/latency_histogram.h"

#include <chrono>

#include <gtest/gtest.h>

namespace cts::metrics {
namespace {

using namespace std::chrono_literals;

TEST(LatencyHistogramTest, EmptyIsZero) {
  LatencyHistogram h;
  EXPECT_EQ(h.count(), 0u);
  EXPECT_EQ(h.min(), 0ns);
  EXPECT_EQ(h.max(), 0ns);
  EXPECT_DOUBLE_EQ(h.MeanNanos(), 0.0);
  EXPECT_EQ(h.Percentile(50), 0ns);
  EXPECT_EQ(h.Percentile(99), 0ns);
}

TEST(LatencyHistogramTest, TinyValuesAreExact) {
  LatencyHistogram h;
  h.Record(5ns);
  h.Record(5ns);
  h.Record(15ns);
  EXPECT_EQ(h.count(), 3u);
  EXPECT_EQ(h.min(), 5ns);
  EXPECT_EQ(h.max(), 15ns);
  EXPECT_EQ(h.Percentile(50), 5ns);
  EXPECT_EQ(h.Percentile(100), 15ns);
  EXPECT_DOUBLE_EQ(h.MeanNanos(), (5.0 + 5.0 + 15.0) / 3.0);
}

TEST(LatencyHistogramTest, SingleSampleBucketized) {
  LatencyHistogram h;
  h.Record(1000ns);
  // 1000ns lands in bucket [960, 1024); percentiles return the lower bound
  EXPECT_EQ(h.Percentile(50), 960ns);
  EXPECT_EQ(h.Percentile(99), 960ns);
  EXPECT_EQ(h.min(), 1000ns);
  EXPECT_EQ(h.max(), 1000ns);
  EXPECT_DOUBLE_EQ(h.MeanNanos(), 1000.0);
}

TEST(LatencyHistogramTest, PercentileDistribution) {
  LatencyHistogram h;
  for (int i = 1; i <= 100; ++i) {
    h.Record(std::chrono::microseconds{i * 10});
  }
  EXPECT_EQ(h.count(), 100u);
  // Bucketing quantizes by at most 12.5%
  auto const p50 = h.Percentile(50);
  EXPECT_GE(p50, std::chrono::microseconds{500} * 0.875);
  EXPECT_LE(p50, std::chrono::microseconds{500});
  auto const p99 = h.Percentile(99);
  EXPECT_GE(p99, std::chrono::microseconds{990} * 0.875);
  EXPECT_LE(p99, std::chrono::microseconds{990});
  EXPECT_EQ(h.max(), std::chrono::microseconds{1000});
}

TEST(LatencyHistogramTest, PercentileClampsOutOfRangeInput) {
  LatencyHistogram h;
  h.Record(1000ns);
  EXPECT_EQ(h.Percentile(-5), h.Percentile(0));
  EXPECT_EQ(h.Percentile(101), h.Percentile(100));
}

TEST(LatencyHistogramTest, NegativeInputClampsToZero) {
  LatencyHistogram h;
  h.Record(std::chrono::nanoseconds{-5});
  EXPECT_EQ(h.count(), 1u);
  EXPECT_EQ(h.min(), 0ns);
  EXPECT_EQ(h.Percentile(50), 0ns);
}

TEST(LatencyHistogramTest, LargeValuesDoNotOverflowIndex) {
  LatencyHistogram h;
  constexpr auto kOneHour =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::hours{1});
  h.Record(kOneHour);
  EXPECT_EQ(h.count(), 1u);
  auto const p100 = h.Percentile(100);
  EXPECT_LE(p100, kOneHour);
  EXPECT_GE(p100, kOneHour * 0.875);
}

TEST(LatencyHistogramTest, ResetClearsEverything) {
  LatencyHistogram h;
  h.Record(100ns);
  h.Record(200ns);
  h.Reset();
  EXPECT_EQ(h.count(), 0u);
  EXPECT_EQ(h.min(), 0ns);
  EXPECT_EQ(h.max(), 0ns);
  EXPECT_EQ(h.Percentile(99), 0ns);
}

}  // namespace
}  // namespace cts::metrics
