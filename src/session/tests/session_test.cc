/**
 * @file session_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief SessionTable/Calendar tests: attribution, trading time, loaders.
 */
#include "session/session.h"

#include <cstdio>
#include <filesystem>

#include <gtest/gtest.h>

namespace cts::session {
namespace {

// 2026-10-16 is a Friday, 17 Saturday, 18 Sunday, 19 Monday
Template RbTemplate() {
  return Template{.product = "rb",
                  .sections = {{1260, 1380},  // 21:00-23:00
                               {540, 615},    // 09:00-10:15
                               {630, 690},    // 10:30-11:30
                               {810, 900}}};  // 13:30-15:00
}

Template AuTemplate() {
  return Template{.product = "au",
                  .sections = {{1260, 1590},  // 21:00-02:30
                               {540, 615},
                               {630, 690},
                               {810, 900}}};
}

TEST(CalendarTest, WeekendsAreNotTradingDays) {
  Calendar cal;
  EXPECT_TRUE(cal.IsTradingDay(20261016));   // Friday
  EXPECT_FALSE(cal.IsTradingDay(20261017));  // Saturday
  EXPECT_FALSE(cal.IsTradingDay(20261018));  // Sunday
  EXPECT_TRUE(cal.IsTradingDay(20261019));   // Monday
}

TEST(CalendarTest, HolidayOverridesWeekdayAndShiftsNext) {
  Calendar cal;
  cal.AddHoliday(20261019);  // Monday off
  EXPECT_FALSE(cal.IsTradingDay(20261019));
  EXPECT_EQ(cal.NextTradingDay(20261016), 20261020);  // Friday -> Tuesday
}

TEST(CalendarTest, NextTradingDaySkipsWeekend) {
  Calendar cal;
  EXPECT_EQ(cal.NextTradingDay(20261016), 20261019);
  EXPECT_EQ(cal.NextTradingDay(20261015), 20261016);  // Thursday -> Friday
}

TEST(SessionTest, EveningAttributesToNextTradingDay) {
  Calendar cal;
  auto const rb = RbTemplate();
  // Friday 21:30 belongs to Monday
  EXPECT_EQ(TradingDayOf(cal, rb, 20261016, 21 * 60 + 30), 20261019);
  // Wednesday 21:30 belongs to Thursday
  EXPECT_EQ(TradingDayOf(cal, rb, 20261014, 21 * 60 + 30), 20261015);
}

TEST(SessionTest, EveningRespectsHolidays) {
  Calendar cal;
  cal.AddHoliday(20261019);  // Monday off
  auto const rb = RbTemplate();
  EXPECT_EQ(TradingDayOf(cal, rb, 20261016, 21 * 60 + 30), 20261020);
}

TEST(SessionTest, AfterMidnightTailAttributesToTheSessionDay) {
  Calendar cal;
  auto const au = AuTemplate();
  // Wednesday 01:00 is the tail of Tuesday's night session -> Wednesday
  EXPECT_EQ(TradingDayOf(cal, au, 20261014, 60), 20261014);
  // Saturday 00:30 is the tail of Friday's night session -> Monday
  EXPECT_EQ(TradingDayOf(cal, au, 20261017, 30), 20261019);
}

TEST(SessionTest, DaySessionAttributesToTheSameDay) {
  Calendar cal;
  auto const rb = RbTemplate();
  EXPECT_EQ(TradingDayOf(cal, rb, 20261016, 600), 20261016);  // Friday 10:00
}

TEST(SessionTest, IsTradingTime) {
  auto const rb = RbTemplate();
  auto const au = AuTemplate();
  EXPECT_TRUE(IsTradingTime(rb, 1260));   // 21:00 open
  EXPECT_TRUE(IsTradingTime(rb, 600));    // 10:00
  EXPECT_FALSE(IsTradingTime(rb, 620));   // 10:20 morning break
  EXPECT_FALSE(IsTradingTime(rb, 1380));  // 23:00 closed
  EXPECT_TRUE(IsTradingTime(au, 90));     // 01:30, night tail
  EXPECT_FALSE(IsTradingTime(rb, 90));    // rb has no after-midnight tail
}

TEST(SessionTest, TableFallsBackToDefault) {
  SessionTable table;
  table.Add(Template{.product = "DEFAULT", .sections = {{540, 900}}});
  table.Add(RbTemplate());
  ASSERT_NE(table.Find("rb"), nullptr);
  EXPECT_EQ(table.Find("rb")->sections.size(), 4u);
  auto const* any = table.Find("xx9999");
  ASSERT_NE(any, nullptr);
  EXPECT_EQ(any->sections.size(), 1u);
  EXPECT_EQ(table.Find("xx9999"), any);  // DEFAULT
}

TEST(SessionTest, LocalDateAndMinuteUsesUtcPlus8) {
  // 2026-10-16 21:35:00 +0800 == 13:35:00 UTC; 2026-10-16 is day 20742
  std::int64_t const utc_ms =
      (20742LL * 86400 + 13 * 3600 + 35 * 60) * 1000;
  auto const [date, minute] = LocalDateAndMinute(utc_ms);
  EXPECT_EQ(date, 20261016);
  EXPECT_EQ(minute, 21 * 60 + 35);
}

class SessionFileTest : public testing::Test {
 protected:
  void TearDown() override {
    std::filesystem::remove(path_);
  }
  std::filesystem::path path_ = std::filesystem::temp_directory_path() /
                                "cts_session_test.txt";

  void Write(char const* content) {
    std::FILE* f = std::fopen(path_.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs(content, f);
    std::fclose(f);
  }
};

TEST_F(SessionFileTest, LoadTemplates) {
  Write("# comment\n"
        "DEFAULT 09:00-10:15,10:30-11:30,13:30-15:00\n"
        "rb 21:00-23:00,09:00-10:15,10:30-11:30,13:30-15:00\n"
        "au 21:00-02:30,09:00-10:15,10:30-11:30,13:30-15:00  # gold\n");
  SessionTable table;
  ASSERT_TRUE(LoadTemplates(path_, table));
  EXPECT_EQ(table.size(), 3u);
  auto const* au = table.Find("au");
  ASSERT_NE(au, nullptr);
  // Sections are sorted by start; the night section is the last one
  EXPECT_EQ(au->sections.back().start_min, 1260);
  EXPECT_EQ(au->sections.back().end_min, 1590);  // crosses midnight
  auto const* rb = table.Find("rb");
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->sections.back().end_min, 1380);
}

TEST_F(SessionFileTest, LoadTemplatesRejectsBadLines) {
  Write("rb 21:00-23:00\nbroken line without times\n");
  SessionTable table;
  EXPECT_FALSE(LoadTemplates(path_, table));
}

TEST_F(SessionFileTest, LoadHolidays) {
  Write("# 2026\n20261001\n20261002\n");
  Calendar cal;
  ASSERT_TRUE(LoadHolidays(path_, cal));
  EXPECT_EQ(cal.size(), 2u);
  EXPECT_FALSE(cal.IsTradingDay(20261001));  // Thursday, but a holiday
  EXPECT_TRUE(cal.IsTradingDay(20261015));
}

TEST_F(SessionFileTest, LoadHolidaysRejectsGarbage) {
  Write("20261001\nnot-a-date\n");
  Calendar cal;
  EXPECT_FALSE(LoadHolidays(path_, cal));
}

}  // namespace
}  // namespace cts::session
