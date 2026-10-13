/**
 * @file time_utils.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Time utilities; the whole system standardizes on fixed UTC+8.
 */
#include "base/time_utils.h"

#include <cstdio>

namespace cts::base {

std::string FormatLocalTimeOfDay(std::int64_t epoch_ms) {
  constexpr std::int64_t kMsPerDay = 86400 * 1000;
  // Double-mod keeps the result positive for pre-1970 (negative) input
  std::int64_t const ms_of_day =
      ((epoch_ms + kUtcPlus8Seconds * 1000) % kMsPerDay + kMsPerDay) %
      kMsPerDay;
  std::int64_t const seconds = ms_of_day / 1000;
  char buf[13];
  std::snprintf(
      buf, sizeof(buf), "%02d:%02d:%02d.%03d", static_cast<int>(seconds / 3600),
      static_cast<int>(seconds / 60 % 60), static_cast<int>(seconds % 60),
      static_cast<int>(ms_of_day % 1000));
  return buf;
}

}  // namespace cts::base
