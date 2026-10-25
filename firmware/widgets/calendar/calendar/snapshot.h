#pragma once

#include <stdint.h>

// Parsed agent snapshot for the Calendar home tile and detail screen.
namespace calendar {

constexpr uint32_t kStaleMs = 1800000;  // 30 min
constexpr int kMaxEvents = 12;
constexpr int kTitleMax = 48;
constexpr int kLocationMax = 32;

struct Event {
    int start_min = 0;   // minutes from midnight (local)
    int end_min = 0;
    bool all_day = false;
    int day_offset = 0;  // 0 = today, 1 = tomorrow, …
    char title[kTitleMax] = {};
    char location[kLocationMax] = {};
    int duration_min = 0;
    uint32_t color_rgb = 0x1A73E8;
};

struct View {
    bool bridge_ok = false;
    bool linked = false;  // at least one Google account / selection
    bool ready = false;   // received at least one valid CALENDAR line this boot
    bool stale = false;
    uint32_t received_ms = 0;
    Event events[kMaxEvents] = {};
    int event_count = 0;
};

// Mutates `view` from a full line starting with "CALENDAR". Returns false if ignored.
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

}  // namespace calendar
