#pragma once

#include "tessera/services.h"

#include <stdint.h>

// Pure face copy for home hero + clock detail (option C: greeting+date+time live here).
namespace clock_face {

enum class TimeFormat : uint8_t { Hm = 0, Hms = 1 };

enum class DateFormat : uint8_t {
    ShortPt = 0,  // "Sáb, 24 de out"
    Dmy = 1,      // "24/10/2025"
    Ymd = 2,      // "2025-10-24"
    Mdy = 3,      // "10/24/2025"
    LongPt = 4,   // "Sábado, 24 de outubro"
};

struct Options {
    TimeFormat time = TimeFormat::Hm;
    DateFormat date = DateFormat::ShortPt;
    int second = 0;
};

struct Summary {
    char greeting[24];
    char date_short[48];
    char date_long[56];
    char time[12];  // "14:30", "14:30:05", or "--:--"
    bool synced;
};

Summary from_parts(int hour, int minute, int wday, int day, int month, int year, bool synced,
                   Options opts = {});
Summary from_snapshot(const tessera::TimeSnapshot &snap, Options opts = {});

TimeFormat parse_time_format(const char *value);
DateFormat parse_date_format(const char *value);
const char *time_format_id(TimeFormat fmt);
const char *date_format_id(DateFormat fmt);

}  // namespace clock_face
