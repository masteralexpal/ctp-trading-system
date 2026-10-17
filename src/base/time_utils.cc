/**
 * @file time_utils.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Time utilities; the whole system standardizes on fixed UTC+8.
 */
#include "base/time_utils.h"

#include <cstdio>

namespace cts::base {
namespace {

// Howard Hinnant's days_from_civil: proleptic Gregorian days since 1970-01-01
std::int64_t DaysFromCivil(std::int64_t year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  std::int64_t const era = (year >= 0 ? year : year - 399) / 400;
  unsigned const yoe = static_cast<unsigned>(year - era * 400);
  unsigned const doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

int Parse2(char a, char b) {
  if (a < '0' || a > '9' || b < '0' || b > '9') {
    return -1;
  }
  return (a - '0') * 10 + (b - '0');
}

}  // namespace

std::int64_t LocalDateTimeToEpochMs(std::string_view day,
                                    std::string_view time,
                                    int millisec) {
  if (day.size() < 8 || time.size() < 8) {
    return 0;
  }
  int const year_hi = Parse2(day[0], day[1]);
  int const year_lo = Parse2(day[2], day[3]);
  int const month = Parse2(day[4], day[5]);
  int const dom = Parse2(day[6], day[7]);
  int const hour = Parse2(time[0], time[1]);
  int const minute = Parse2(time[3], time[4]);  // skip ':'
  int const second = Parse2(time[6], time[7]);
  if (year_hi < 0 || year_lo < 0 || month < 1 || month > 12 || dom < 1 ||
      dom > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || millisec < 0 || millisec > 999) {
    return 0;
  }
  std::int64_t const days =
      DaysFromCivil(year_hi * 100 + year_lo, static_cast<unsigned>(month),
                    static_cast<unsigned>(dom));
  return (days * 86400 + hour * 3600 + minute * 60 + second -
          kUtcPlus8Seconds) *
             1000 +
         millisec;
}

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
