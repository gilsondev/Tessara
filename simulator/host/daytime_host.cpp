#include "platform/daytime.h"

namespace {
int g_hour = 14;
int g_minute = 30;
int g_wday = 5;  // Friday
int g_day = 24;
int g_month = 9;  // 0-based Oct
bool g_ok = true;
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
bool local(int &hour, int &minute, int &wday, int &day, int &month) {
    if (!g_ok) return false;
    hour = g_hour;
    minute = g_minute;
    wday = g_wday;
    day = g_day;
    month = g_month;
    return true;
}
}  // namespace daytime
