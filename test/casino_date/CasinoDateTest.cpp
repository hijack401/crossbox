#include <gtest/gtest.h>

#include <climits>
#include <ctime>

#include "util/CasinoDate.h"

namespace {
tm date(int year, int month, int day) {
  tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  return value;
}
}  // namespace

TEST(CasinoDate, ProducesKnownUnixCalendarDays) {
  EXPECT_EQ(casino_date::civilDay(date(2024, 1, 1)), 19723);
  EXPECT_EQ(casino_date::civilDay(date(2024, 2, 29)), 19782);
  EXPECT_EQ(casino_date::civilDay(date(2099, 12, 31)), 47481);
}

TEST(CasinoDate, LeapDayMonthAndYearTransitionsAreConsecutive) {
  EXPECT_EQ(casino_date::civilDay(date(2024, 2, 29)) - casino_date::civilDay(date(2024, 2, 28)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2024, 3, 1)) - casino_date::civilDay(date(2024, 2, 29)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2025, 3, 1)) - casino_date::civilDay(date(2025, 2, 28)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2026, 5, 1)) - casino_date::civilDay(date(2026, 4, 30)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2025, 1, 1)) - casino_date::civilDay(date(2024, 12, 31)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2099, 1, 1)) - casino_date::civilDay(date(2098, 12, 31)), 1);
  EXPECT_EQ(casino_date::civilDay(date(2025, 1, 1)) - casino_date::civilDay(date(2024, 1, 1)), 366);
  EXPECT_EQ(casino_date::civilDay(date(2026, 1, 1)) - casino_date::civilDay(date(2025, 1, 1)), 365);
}

TEST(CasinoDate, RejectsUntrustedYearsWithoutIntegerOverflow) {
  EXPECT_EQ(casino_date::civilDay(date(2000, 1, 1)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2023, 12, 31)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2100, 1, 1)), 0);
  tm value = date(2026, 1, 1);
  value.tm_year = INT_MIN;
  EXPECT_EQ(casino_date::civilDay(value), 0);
  value.tm_year = INT_MAX;
  EXPECT_EQ(casino_date::civilDay(value), 0);
}

TEST(CasinoDate, RejectsInvalidMonthsAndDaysInsteadOfNormalizing) {
  EXPECT_EQ(casino_date::civilDay(date(2026, 0, 1)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2026, 13, 1)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2026, 1, 0)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2026, 1, 32)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2026, 4, 31)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2025, 2, 29)), 0);
  EXPECT_EQ(casino_date::civilDay(date(2024, 2, 30)), 0);
  tm value = date(2026, 1, 1);
  value.tm_mon = INT_MAX;
  EXPECT_EQ(casino_date::civilDay(value), 0);
  value.tm_mon = INT_MIN;
  EXPECT_EQ(casino_date::civilDay(value), 0);
  value.tm_mon = 0;
  value.tm_mday = INT_MAX;
  EXPECT_EQ(casino_date::civilDay(value), 0);
  value.tm_mday = INT_MIN;
  EXPECT_EQ(casino_date::civilDay(value), 0);
}

TEST(CasinoDate, IgnoresClockTimeDstAndDerivedCalendarFields) {
  tm value = date(2026, 10, 25);
  const int32_t expected = casino_date::civilDay(value);
  value.tm_hour = 2;
  value.tm_min = 30;
  value.tm_sec = 59;
  value.tm_isdst = 1;
  EXPECT_EQ(casino_date::civilDay(value), expected);
  value.tm_isdst = 0;
  EXPECT_EQ(casino_date::civilDay(value), expected);
  value.tm_isdst = -1;
  value.tm_hour = 23;
  value.tm_wday = 6;
  value.tm_yday = 0;
  EXPECT_EQ(casino_date::civilDay(value), expected);
  EXPECT_EQ(casino_date::civilDay(date(2026, 10, 26)) - expected, 1);
  EXPECT_EQ(casino_date::civilDay(date(2026, 3, 30)) - casino_date::civilDay(date(2026, 3, 29)), 1);
}
