/**
 * @file rate_table.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Per-instrument margin/commission rates, queried on demand.
 */
#pragma once

#ifndef CTS_TD_RATE_TABLE_H_
#define CTS_TD_RATE_TABLE_H_

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "td/order.h"

namespace cts::td {

// Margin/commission rates, filled by targeted per-instrument queries (the
// 1/sec throttle forbids a whole-market pull). Owned by TdGateway; the SPI
// adapter writes on the CTP callback thread, the risk sink reads on any
// thread. Plain mutex, copies out -- rates are a handful of entries
class RateTable {
 public:
  void Update(MarginRate const& rate) {
    std::lock_guard lock(mutex_);
    margins_[rate.instrument_id] = rate;
  }
  void Update(CommissionRate const& rate) {
    std::lock_guard lock(mutex_);
    commissions_[rate.instrument_id] = rate;
  }

  std::optional<MarginRate> FindMargin(std::string const& instrument) const {
    std::lock_guard lock(mutex_);
    auto const it = margins_.find(instrument);
    return it == margins_.end() ? std::nullopt
                                : std::optional<MarginRate>(it->second);
  }
  std::optional<CommissionRate> FindCommission(
      std::string const& instrument) const {
    std::lock_guard lock(mutex_);
    auto const it = commissions_.find(instrument);
    return it == commissions_.end() ? std::nullopt
                                    : std::optional<CommissionRate>(it->second);
  }

  std::size_t size() const {
    std::lock_guard lock(mutex_);
    return margins_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, MarginRate> margins_;
  std::unordered_map<std::string, CommissionRate> commissions_;
};

}  // namespace cts::td

#endif  // CTS_TD_RATE_TABLE_H_
