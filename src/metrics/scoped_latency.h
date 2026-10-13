/**
 * @file scoped_latency.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RAII latency guard: records scope duration into a LatencyHistogram.
 */
#pragma once

#ifndef CTS_METRICS_SCOPED_LATENCY_H_
#define CTS_METRICS_SCOPED_LATENCY_H_

#include <chrono>

#include "metrics/latency_histogram.h"

namespace cts::metrics {

// RAII guard: records construction-to-destruction wall time into the
// histogram (~20ns overhead via vDSO steady_clock). Same thread contract as
// the histogram.
class ScopedLatency {
 public:
  explicit ScopedLatency(LatencyHistogram& histogram) noexcept
      : histogram_(histogram), start_(std::chrono::steady_clock::now()) {}

  ~ScopedLatency() {
    histogram_.Record(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_));
  }

  ScopedLatency(ScopedLatency const&) = delete;
  ScopedLatency& operator=(ScopedLatency const&) = delete;

 private:
  LatencyHistogram& histogram_;
  std::chrono::steady_clock::time_point const start_;
};

}  // namespace cts::metrics

#endif  // CTS_METRICS_SCOPED_LATENCY_H_
