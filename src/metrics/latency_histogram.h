/**
 * @file latency_histogram.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Bucketed latency distribution recorder (p50/p99/max, not averages).
 */
#pragma once

#ifndef CTS_METRICS_LATENCY_HISTOGRAM_H_
#define CTS_METRICS_LATENCY_HISTOGRAM_H_

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cts::metrics {

// Bucketed latency recorder: percentiles, because the tail matters and
// averages hide it. Values <16ns are exact; above, 8 sub-buckets per
// power-of-two octave (max 12.5% relative error). O(1), allocation-free.
// NOT thread-safe: one instance per measuring thread. No exceptions.
class LatencyHistogram {
 public:
  LatencyHistogram() = default;

  // Negative input clamps to zero
  void Record(std::chrono::nanoseconds latency) noexcept;

  void Reset() noexcept;

  std::uint64_t count() const noexcept { return count_; }

  // Exact (not bucketized); zero when empty
  std::chrono::nanoseconds min() const noexcept {
    return std::chrono::nanoseconds{min_};
  }
  std::chrono::nanoseconds max() const noexcept {
    return std::chrono::nanoseconds{max_};
  }

  // 0 when empty; the sum wraps in very long runs -- Reset periodically
  double MeanNanos() const noexcept;

  // p in [0,100]; returns the containing bucket's lower bound; 0 when empty
  std::chrono::nanoseconds Percentile(double p) const noexcept;

 private:
  static constexpr int kMantissaBits = 3;
  static constexpr std::size_t kExactLimit = 1 << (kMantissaBits + 1);
  static constexpr std::size_t kMaxExponent = 62;  // int64 ns ceiling
  static constexpr std::size_t kBucketCount =
      kExactLimit + (kMaxExponent - kMantissaBits) * (1 << kMantissaBits);

  static std::size_t BucketIndex(std::uint64_t ns) noexcept;
  static std::uint64_t BucketLowerBound(std::size_t index) noexcept;

  std::uint64_t buckets_[kBucketCount]{};
  std::uint64_t count_{0};
  std::uint64_t sum_{0};
  std::uint64_t min_{0};
  std::uint64_t max_{0};
};

}  // namespace cts::metrics

#endif  // CTS_METRICS_LATENCY_HISTOGRAM_H_
