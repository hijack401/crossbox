#pragma once

#include <cstdint>
#include <ctime>

namespace casino_date {

// Local calendar days since 1970-01-01; zero means the RTC date is unusable.
// Clock time, timezone offsets, and DST do not affect the day number.
inline int32_t civilDay(const tm& date) {
  if (date.tm_year < 124 || date.tm_year > 199 || date.tm_mon < 0 || date.tm_mon > 11) return 0;

  static constexpr uint16_t DAYS_BEFORE_MONTH[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  static constexpr uint8_t DAYS_IN_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const int year = date.tm_year + 1900;
  const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int monthLength = DAYS_IN_MONTH[date.tm_mon] + (date.tm_mon == 1 && leapYear ? 1 : 0);
  if (date.tm_mday < 1 || date.tm_mday > monthLength) return 0;

  const int precedingYear = year - 1;
  const int leapDays =
      precedingYear / 4 - precedingYear / 100 + precedingYear / 400 - (1969 / 4 - 1969 / 100 + 1969 / 400);
  return (year - 1970) * 365 + leapDays + DAYS_BEFORE_MONTH[date.tm_mon] + (date.tm_mon > 1 && leapYear ? 1 : 0) +
         date.tm_mday - 1;
}

}  // namespace casino_date
