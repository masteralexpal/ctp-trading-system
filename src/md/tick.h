/**
 * @file tick.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Normalized depth market data tick.
 */
#pragma once

#ifndef CTS_MD_TICK_H_
#define CTS_MD_TICK_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cts::md {

// Normalized depth tick: the only market data type visible outside the md
// module. Translated once from the vendor struct at the gateway boundary
// (see tick_ctp.h); everything downstream is vendor-free.
//
// Trivially copyable on purpose: SPSC queues and binary tick recording both
// rely on plain memcpy. Never add std::string / std::vector members here.
struct Tick {
  static constexpr std::size_t kDepthLevels = 5;

  // Identity and time
  char instrument_id[31];  // NUL-terminated, e.g. "rb2701"
  char exchange_id[9];     // e.g. "SHFE"
  char trading_day[9];     // "YYYYMMDD", the exchange trading day
  // Vendor date/time/millisec folded to Unix epoch ms (fixed UTC+8, see
  // base/time_utils.h). 0 = unparseable source
  std::int64_t epoch_ms;

  double last_price;
  std::int64_t volume;  // cumulative volume of the day
  double turnover;      // cumulative turnover of the day
  double open_interest;

  // 5-level order book; [0] is the best level
  double bid_price[kDepthLevels];
  double ask_price[kDepthLevels];
  std::int32_t bid_volume[kDepthLevels];
  std::int32_t ask_volume[kDepthLevels];

  double upper_limit_price;
  double lower_limit_price;
  double pre_settlement_price;  // previous settlement
  double settlement_price;      // today's settlement, meaningful after close
};

static_assert(
    std::is_trivially_copyable_v<Tick>,
    "Tick must stay trivially copyable (SPSC queues, binary recording)");

}  // namespace cts::md

#endif  // CTS_MD_TICK_H_
