#include "platform/daytime.h"

#include <cstring>

namespace {
int g_hour = 14;
int g_minute = 30;
int g_second = 0;
int g_wday = 5;  // Friday
int g_day = 24;
int g_month = 9;  // 0-based Oct
int g_year = 2025;
bool g_ok = true;
char g_tz[24] = "sao_paulo";
}  // namespace

namespace sim_time {
void set(int hour, int minute, int wday, int day, int month, bool ok = true) {
    g_hour = hour;
    g_minute = minute;
    g_wday = wday;
    g_day = day;
    g_month = month;
    g_ok = ok;
}
}  // namespace sim_time

namespace daytime {
void start() {}

bool set_timezone(const char *tz_id) {
    if (!tz_id || !tz_id[0]) return false;
    std::strncpy(g_tz, tz_id, sizeof(g_tz) - 1);
    g_tz[sizeof(g_tz) - 1] = '\0';
    return true;
}

const char *timezone_id() { return g_tz; }

const char *timezone_label(const char *tz_id) {
    if (!tz_id) return "";
    if (std::strcmp(tz_id, "sao_paulo") == 0) return "Brasília";
    if (std::strcmp(tz_id, "utc") == 0) return "UTC";
    return tz_id;
}

void restore() {}

bool local(int &hour, int &minute, int &wday, int &day, int &month) {
    int second = 0;
    int year = 0;
    return local(hour, minute, second, wday, day, month, year);
}

bool local(int &hour, int &minute, int &second, int &wday, int &day, int &month, int &year) {
    if (!g_ok) return false;
    hour = g_hour;
    minute = g_minute;
    second = g_second;
    wday = g_wday;
    day = g_day;
    month = g_month;
    year = g_year;
    return true;
}
}  // namespace daytime
