#include "platform/daytime.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {

char g_tz_id[24] = "sao_paulo";
bool g_started = false;
constexpr time_t kMinEpoch = 1704067200;  // 2024-01-01 UTC
uint32_t g_last_persist_ms = 0;

void persist_epoch(time_t epoch) {
    if (epoch < kMinEpoch) return;
    const uint32_t now_ms = millis();
    if (g_last_persist_ms != 0 && (now_ms - g_last_persist_ms) < 60000) return;
    g_last_persist_ms = now_ms;
    Preferences prefs;
    if (!prefs.begin("daytime", false)) return;
    prefs.putLong64("epoch", static_cast<int64_t>(epoch));
    prefs.end();
}

struct Zone {
    const char *id;
    const char *label;
    const char *posix;
};

// POSIX TZ strings for configTzTime (no tzdata on device).
constexpr Zone kZones[] = {
    {"sao_paulo", "Brasília (UTC−3)", "BRT3"},
    {"utc", "UTC", "UTC0"},
    {"new_york", "Nova York", "EST5EDT,M3.2.0,M11.1.0"},
    {"los_angeles", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"london", "Londres", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"lisbon", "Lisboa", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"buenos_aires", "Buenos Aires", "ART3"},
    {"mexico_city", "Cidade do México", "CST6"},
};

const Zone *find_zone(const char *id) {
    if (!id || !id[0]) return &kZones[0];
    for (const Zone &z : kZones) {
        if (std::strcmp(z.id, id) == 0) return &z;
    }
    return &kZones[0];
}

void apply_posix(const char *posix) {
    // BRT3 is POSIX for UTC−3 with no daylight saving (Brazil since 2019).
    // configTime() would overwrite that with UTC.
    configTzTime(posix, "pool.ntp.br", "pool.ntp.org");
}

}  // namespace

void daytime::start() {
    if (g_started) return;
    g_started = true;
    apply_posix(find_zone(g_tz_id)->posix);
}

bool daytime::set_timezone(const char *tz_id) {
    const Zone *zone = find_zone(tz_id);
    const bool changed = std::strcmp(g_tz_id, zone->id) != 0;
    if (!changed && g_started) return false;
    std::snprintf(g_tz_id, sizeof(g_tz_id), "%s", zone->id);
    g_started = true;
    apply_posix(zone->posix);
    return changed;
}

const char *daytime::timezone_id() { return g_tz_id; }

const char *daytime::timezone_label(const char *tz_id) { return find_zone(tz_id)->label; }

void daytime::restore() {
    time_t now = time(nullptr);
    if (now >= kMinEpoch) return;
    Preferences prefs;
    if (!prefs.begin("daytime", true)) return;
    const int64_t saved = prefs.getLong64("epoch", 0);
    prefs.end();
    if (saved < static_cast<int64_t>(kMinEpoch)) return;
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(saved);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

bool daytime::local(int &hour, int &minute, int &wday, int &day, int &month) {
    int second = 0, year = 0;
    return local(hour, minute, second, wday, day, month, year);
}

bool daytime::local(int &hour, int &minute, int &second, int &wday, int &day, int &month, int &year) {
    time_t now = time(nullptr);
    tm parts{};
    if (!localtime_r(&now, &parts) || parts.tm_year + 1900 < 2024) return false;
    hour = parts.tm_hour;
    minute = parts.tm_min;
    second = parts.tm_sec;
    wday = parts.tm_wday;
    day = parts.tm_mday;
    month = parts.tm_mon;
    year = parts.tm_year + 1900;
    persist_epoch(now);
    return true;
}
