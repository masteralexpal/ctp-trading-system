/**
 * @file session.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Trading session templates and the holiday calendar.
 */
#include "session/session.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

#include "base/time_utils.h"

namespace cts::session {
namespace {

// Howard Hinnant's civil calendar algorithms (proleptic Gregorian)
std::int64_t DaysFromCivil(std::int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  std::int64_t const era = (y >= 0 ? y : y - 399) / 400;
  auto const yoe = static_cast<unsigned>(y - era * 400);
  unsigned const doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::int32_t CivilFromDays(std::int64_t z) {
  z += 719468;
  std::int64_t const era = (z >= 0 ? z : z - 146096) / 146097;
  auto const doe = static_cast<unsigned>(z - era * 146097);
  unsigned const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  auto const y = static_cast<std::int64_t>(yoe) + era * 400;
  unsigned const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned const mp = (5 * doy + 2) / 153;
  unsigned const d = doy - (153 * mp + 2) / 5 + 1;
  unsigned const m = mp + (mp < 10 ? 3 : -9);
  return static_cast<std::int32_t>((y + (m <= 2)) * 10000 + m * 100 + d);
}

std::int64_t DaysOf(std::int32_t yyyymmdd) {
  return DaysFromCivil(yyyymmdd / 10000, (yyyymmdd / 100) % 100,
                       yyyymmdd % 100);
}

// 0 = Sunday; 1970-01-01 was a Thursday
int WeekdayOf(std::int32_t yyyymmdd) {
  return static_cast<int>((DaysOf(yyyymmdd) % 7 + 7 + 4) % 7);
}

bool ParseInt(std::string_view tok, std::int32_t& out) {
  auto const r = std::from_chars(tok.data(), tok.data() + tok.size(), out);
  return r.ec == std::errc{} && r.ptr == tok.data() + tok.size();
}

// "HH:MM" -> minutes from midnight
bool ParseMinute(std::string_view tok, std::int32_t& out) {
  auto const colon = tok.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }
  std::int32_t hh = 0;
  std::int32_t mm = 0;
  if (!ParseInt(tok.substr(0, colon), hh) || !ParseInt(tok.substr(colon + 1), mm) ||
      hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    return false;
  }
  out = hh * 60 + mm;
  return true;
}

// "HH:MM-HH:MM"; end before start means the section crosses midnight
bool ParseSection(std::string_view tok, Section& out) {
  auto const dash = tok.find('-');
  if (dash == std::string_view::npos) {
    return false;
  }
  if (!ParseMinute(tok.substr(0, dash), out.start_min) ||
      !ParseMinute(tok.substr(dash + 1), out.end_min)) {
    return false;
  }
  if (out.end_min <= out.start_min) {
    out.end_min += 1440;
  }
  return true;
}

bool HasNightSection(Template const& t) {
  return std::any_of(t.sections.begin(), t.sections.end(), [](Section const& s) {
    return s.start_min >= 1200;  // 20:00+
  });
}

}  // namespace

void Calendar::AddHoliday(std::int32_t yyyymmdd) {
  holidays_.insert(yyyymmdd);
}

bool Calendar::IsTradingDay(std::int32_t yyyymmdd) const {
  int const wd = WeekdayOf(yyyymmdd);
  return wd != 0 && wd != 6 && !holidays_.contains(yyyymmdd);
}

std::int32_t Calendar::NextTradingDay(std::int32_t yyyymmdd) const {
  std::int64_t days = DaysOf(yyyymmdd);
  for (;;) {
    std::int32_t const next = CivilFromDays(++days);
    if (IsTradingDay(next)) {
      return next;
    }
  }
}

void SessionTable::Add(Template t) {
  std::sort(t.sections.begin(), t.sections.end(),
            [](Section const& a, Section const& b) {
              return a.start_min < b.start_min;
            });
  templates_[t.product] = std::move(t);
}

Template const* SessionTable::Find(std::string_view product) const {
  if (auto const it = templates_.find(std::string(product));
      it != templates_.end()) {
    return &it->second;
  }
  if (auto const it = templates_.find("DEFAULT"); it != templates_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::int32_t TradingDayOf(Calendar const& calendar,
                          Template const& t,
                          std::int32_t yyyymmdd,
                          std::int32_t minute_of_day) {
  // Evening part of a night session: the next trading day after the
  // calendar date (Friday 21:30 -> Monday, holidays permitting)
  if (minute_of_day >= 1200 && HasNightSection(t)) {
    return calendar.NextTradingDay(yyyymmdd);
  }
  // Day session or after-midnight tail: the date itself when it trades,
  // else the next trading day (Saturday 00:30 -> Monday)
  return calendar.IsTradingDay(yyyymmdd) ? yyyymmdd
                                         : calendar.NextTradingDay(yyyymmdd);
}

bool IsTradingTime(Template const& t, std::int32_t minute_of_day) {
  for (auto const& s : t.sections) {
    if (minute_of_day >= s.start_min && minute_of_day < s.end_min) {
      return true;
    }
    // Cross-midnight tail: 01:00 is inside 21:00-02:30 ({1260, 1590})
    if (s.end_min > 1440 && minute_of_day < s.end_min - 1440) {
      return true;
    }
  }
  return false;
}

std::pair<std::int32_t, std::int32_t> LocalDateAndMinute(
    std::int64_t epoch_ms) {
  std::int64_t const local_s =
      epoch_ms / 1000 + base::kUtcPlus8Seconds;
  std::int32_t const date = CivilFromDays(local_s / 86400);
  std::int32_t const minute =
      static_cast<std::int32_t>((local_s % 86400) / 60);
  return {date, minute};
}

bool LoadTemplates(std::filesystem::path const& file, SessionTable& out) {
  std::FILE* f = std::fopen(file.c_str(), "r");
  if (f == nullptr) {
    return false;
  }
  SessionTable table;
  char line[512];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    std::string_view sv(line);
    sv = sv.substr(0, sv.find('#'));  // strip comments
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' ||
                           sv.back() == ' ' || sv.back() == '\t')) {
      sv.remove_suffix(1);
    }
    if (sv.empty()) {
      continue;
    }
    auto const sp = sv.find(' ');
    if (sp == std::string_view::npos) {
      std::fclose(f);
      return false;
    }
    Template t;
    t.product = std::string(sv.substr(0, sp));
    std::string_view rest = sv.substr(sp + 1);
    while (!rest.empty()) {
      auto const comma = rest.find(',');
      auto const tok = rest.substr(0, comma);
      Section s;
      if (!ParseSection(tok, s)) {
        std::fclose(f);
        return false;
      }
      t.sections.push_back(s);
      rest = comma == std::string_view::npos ? "" : rest.substr(comma + 1);
    }
    if (t.sections.empty()) {
      std::fclose(f);
      return false;
    }
    table.Add(std::move(t));
  }
  std::fclose(f);
  out = std::move(table);
  return true;
}

bool LoadHolidays(std::filesystem::path const& file, Calendar& out) {
  std::FILE* f = std::fopen(file.c_str(), "r");
  if (f == nullptr) {
    return false;
  }
  Calendar cal;
  char line[64];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    std::string_view sv(line);
    sv = sv.substr(0, sv.find('#'));
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' ||
                           sv.back() == ' ' || sv.back() == '\t')) {
      sv.remove_suffix(1);
    }
    if (sv.empty()) {
      continue;
    }
    std::int32_t day = 0;
    if (!ParseInt(sv, day) || day < 19000101 || day > 29991231) {
      std::fclose(f);
      return false;
    }
    cal.AddHoliday(day);
  }
  std::fclose(f);
  out = cal;
  return true;
}

}  // namespace cts::session
