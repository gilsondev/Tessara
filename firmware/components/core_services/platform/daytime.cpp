#include "platform/daytime.h"

#include <Arduino.h>

#include <ctime>

namespace daytime {

void start() {
    static bool started = false;
    if (started) return;
    started = true;
    // BRT3 is POSIX for UTC−3 with no daylight saving, which is Brazil since 2019.
    // configTime() would overwrite that with UTC.
    configTzTime("BRT3", "pool.ntp.br", "pool.ntp.org");
}

bool local(int &hour, int &minute, int &wday, int &day, int &month) {
    time_t now = time(nullptr);
    tm parts{};
    if (!localtime_r(&now, &parts) || parts.tm_year + 1900 < 2024) return false;
    hour = parts.tm_hour;
    minute = parts.tm_min;
    wday = parts.tm_wday;
    day = parts.tm_mday;
    month = parts.tm_mon;
    return true;
}

}  // namespace daytime
