/**
 * @file session.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Trading session templates and the holiday calendar: the time base
 *        of the whole system.
 */
#pragma once

#ifndef CTS_SESSION_SESSION_H_
#define CTS_SESSION_SESSION_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cts::session {

// One trading section in minutes from midnight. Night sessions may cross
// midnight: end_min > 1440, e.g. 21:00-01:00 is {1260, 1500}
struct Section {
  std::int32_t start_min;
  std::int32_t end_min;
};

struct Template {
  std::string product;            // "rb"; "DEFAULT" is the fallback entry
  std::vector<Section> sections;  // sorted by start_min
};

// Trading-day calendar: weekdays minus the listed holidays
class Calendar {
 public:
  void AddHoliday(std::int32_t yyyymmdd);
  bool IsTradingDay(std::int32_t yyyymmdd) const;
  // First trading day strictly after yyyymmdd
  std::int32_t NextTradingDay(std::int32_t yyyymmdd) const;
  std::size_t size() const { return holidays_.size(); }

 private:
  std::unordered_set<std::int32_t> holidays_;
};

class SessionTable {
 public:
  void Add(Template t);
  // Exact product match, falling back to "DEFAULT"; nullptr when empty
  Template const* Find(std::string_view product) const;
  std::size_t size() const { return templates_.size(); }

 private:
  std::unordered_map<std::string, Template> templates_;
};

// Which trading day owns a wall-clock moment under the template. Evening
// sections (start >= 20:00) belong to the next trading day after the
// calendar date; the after-midnight tail belongs to the night session that
// started yesterday
std::int32_t TradingDayOf(Calendar const& calendar,
                          Template const& t,
                          std::int32_t yyyymmdd,
                          std::int32_t minute_of_day);

bool IsTradingTime(Template const& t, std::int32_t minute_of_day);

// (yyyymmdd, minute_of_day) of an epoch ms under fixed UTC+8
std::pair<std::int32_t, std::int32_t> LocalDateAndMinute(
    std::int64_t epoch_ms);

// Strict line formats; '#' starts a comment, any bad line fails the load:
//   templates: product rb 21:00-23:00,09:00-10:15,10:30-11:30,13:30-15:00
//   holidays:  20261001
bool LoadTemplates(std::filesystem::path const& file, SessionTable& out);
bool LoadHolidays(std::filesystem::path const& file, Calendar& out);

}  // namespace cts::session

#endif  // CTS_SESSION_SESSION_H_
