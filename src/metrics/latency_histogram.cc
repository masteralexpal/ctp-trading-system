/**
 * @file latency_histogram.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Bucketed latency distribution recorder (p50/p99/max, not averages).
 */
#include "metrics/latency_histogram.h"

#include <bit>
#include <cmath>

namespace cts::metrics {

void LatencyHistogram::Record(std::chrono::nanoseconds latency) noexcept {
  std::uint64_t const v =
      latency.count() < 0 ? 0 : static_cast<std::uint64_t>(latency.count());
  ++buckets_[BucketIndex(v)];
  ++count_;
  sum_ += v;
  if (count_ == 1 || v < min_) {
    min_ = v;
  }
  if (v > max_) {
    max_ = v;
  }
}

void LatencyHistogram::Reset() noexcept {
  for (auto& bucket : buckets_) {
    bucket = 0;
  }
  count_ = 0;
  sum_ = 0;
  min_ = 0;
  max_ = 0;
}

double LatencyHistogram::MeanNanos() const noexcept {
  if (count_ == 0) {
    return 0.0;
  }
  return static_cast<double>(sum_) / static_cast<double>(count_);
}

std::chrono::nanoseconds LatencyHistogram::Percentile(double p) const noexcept {
  if (count_ == 0) {
    return std::chrono::nanoseconds{0};
  }
  if (p < 0.0) {
    p = 0.0;
  }
  if (p > 100.0) {
    p = 100.0;
  }
  std::uint64_t rank =
      static_cast<std::uint64_t>(std::ceil(p / 100.0 * count_));
  if (rank == 0) {
    rank = 1;
  }
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < kBucketCount; ++i) {
    cumulative += buckets_[i];
    if (cumulative >= rank) {
      return std::chrono::nanoseconds{
          static_cast<std::int64_t>(BucketLowerBound(i))};
    }
  }
  return max();  // unreachable
}

std::size_t LatencyHistogram::BucketIndex(std::uint64_t ns) noexcept {
  if (ns < kExactLimit) {
    return static_cast<std::size_t>(ns);
  }
  int const exponent = 63 - std::countl_zero(ns);
  std::size_t const mantissa =
      (ns >> (exponent - kMantissaBits)) & ((1 << kMantissaBits) - 1);
  return kExactLimit +
         static_cast<std::size_t>(exponent - kMantissaBits - 1) *
             (1 << kMantissaBits) +
         mantissa;
}

std::uint64_t LatencyHistogram::BucketLowerBound(std::size_t index) noexcept {
  if (index < kExactLimit) {
    return index;
  }
  std::size_t const j = index - kExactLimit;
  std::size_t const octave = j >> kMantissaBits;  // 0-based
  std::size_t const mantissa = j & ((1 << kMantissaBits) - 1);
  int const exponent = kMantissaBits + 1 + static_cast<int>(octave);
  return ((1 << kMantissaBits) + mantissa) << (exponent - kMantissaBits);
}

}  // namespace cts::metrics
