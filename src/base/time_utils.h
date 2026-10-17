/**
 * @file time_utils.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Time utilities; the whole system standardizes on fixed UTC+8.
 */
#pragma once

#ifndef CTS_BASE_TIME_UTILS_H_
#define CTS_BASE_TIME_UTILS_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace cts::base {

// China local time is a fixed UTC+8 (Asia/Shanghai has had no DST since
// 1991), so epoch conversions never touch the process timezone or tzdata
inline constexpr std::int64_t kUtcPlus8Seconds = 8 * 3600;

// Folds local "YYYYMMDD" + "HH:MM:SS" + millisec into Unix epoch
// milliseconds. Returns 0 on malformed input.
std::int64_t LocalDateTimeToEpochMs(std::string_view day,
                                    std::string_view time,
                                    int millisec);

// Wall clock (system_clock) as Unix epoch milliseconds
std::int64_t NowEpochMs();

// Renders Unix epoch milliseconds as local "HH:MM:SS.mmm" (12 chars)
std::string FormatLocalTimeOfDay(std::int64_t epoch_ms);

}  // namespace cts::base

#endif  // CTS_BASE_TIME_UTILS_H_
