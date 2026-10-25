#include "calendar/snapshot.h"

#include <cstdlib>
#include <cstring>

namespace {

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

bool next_field(char **cursor, char *out, size_t out_n) {
    if (!cursor || !*cursor || !**cursor) {
        if (out && out_n) out[0] = '\0';
        return false;
    }
    char *start = *cursor;
    char *end = start;
    while (*end && *end != '\t' && *end != '\r' && *end != '\n') ++end;
    size_t n = static_cast<size_t>(end - start);
    if (out && out_n) {
        if (n >= out_n) n = out_n - 1;
        memcpy(out, start, n);
        out[n] = '\0';
    }
    if (*end == '\t') *cursor = end + 1;
    else *cursor = end;
    return true;
}

int parse_hhmm(const char *text) {
    if (!text || strlen(text) < 4) return 0;
    int h = (text[0] - '0') * 10 + (text[1] - '0');
    int m = (text[2] - '0') * 10 + (text[3] - '0');
    if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
    return h * 60 + m;
}

uint32_t parse_rgb(const char *text) {
    if (!text || !text[0]) return 0x1A73E8;
    return static_cast<uint32_t>(strtoul(text, nullptr, 16)) & 0xFFFFFFu;
}

}  // namespace

bool calendar::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "CALENDAR", 8) != 0) return false;
    // Catalog / auth lines are handled elsewhere.
    if (strncmp(line, "CALENDAR_CATALOG", 16) == 0 || strncmp(line, "CALENDAR_AUTH", 13) == 0) return false;

    char buf[1536];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[64];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "CALENDAR")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    View next = {};
    next.bridge_ok = field[0] == '1';
    next.ready = true;
    next.stale = false;
    next.received_ms = now_ms;

    if (!next_field(&cursor, field, sizeof(field))) return false;
    next.linked = field[0] == '1';

    while (next.event_count < kMaxEvents) {
        char start[8];
        char end[8];
        char allday[4];
        char title[kTitleMax];
        char location[kLocationMax];
        char duration[12];
        char color[12];
        char day[8];
        if (!next_field(&cursor, start, sizeof(start)) || !start[0]) break;
        // HHMM starts with digit — stop if stream drifted into another capability.
        if (start[0] < '0' || start[0] > '9') break;
        if (!next_field(&cursor, end, sizeof(end))) break;
        if (!next_field(&cursor, allday, sizeof(allday))) break;
        if (!next_field(&cursor, title, sizeof(title))) break;
        if (!next_field(&cursor, location, sizeof(location))) location[0] = '\0';
        if (!next_field(&cursor, duration, sizeof(duration))) duration[0] = '\0';
        if (!next_field(&cursor, color, sizeof(color))) color[0] = '\0';
        // Optional day_offset (0=today). If the next token looks like HHMM, it's the next event.
        day[0] = '0';
        day[1] = '\0';
        char *before_day = cursor;
        if (next_field(&cursor, day, sizeof(day)) && day[0]) {
            const bool looks_hhmm = strlen(day) == 4 && day[0] >= '0' && day[0] <= '9' &&
                                   day[1] >= '0' && day[1] <= '9';
            if (looks_hhmm) {
                cursor = before_day;
                day[0] = '0';
                day[1] = '\0';
            }
        } else {
            day[0] = '0';
            day[1] = '\0';
        }

        Event &e = next.events[next.event_count++];
        e.all_day = allday[0] == '1';
        e.start_min = e.all_day ? 0 : parse_hhmm(start);
        e.end_min = e.all_day ? 24 * 60 : parse_hhmm(end);
        e.day_offset = static_cast<int>(strtol(day, nullptr, 10));
        if (e.day_offset < 0) e.day_offset = 0;
        if (e.day_offset > 14) e.day_offset = 14;
        strncpy(e.title, title, sizeof(e.title) - 1);
        e.title[sizeof(e.title) - 1] = '\0';
        strncpy(e.location, location, sizeof(e.location) - 1);
        e.location[sizeof(e.location) - 1] = '\0';
        e.duration_min = static_cast<int>(strtol(duration, nullptr, 10));
        if (e.duration_min <= 0 && !e.all_day && e.end_min > e.start_min) {
            e.duration_min = e.end_min - e.start_min;
        }
        e.color_rgb = parse_rgb(color);
    }

    view = next;
    return true;
}

bool calendar::refresh_age(View &view, uint32_t now_ms) {
    if (!view.ready) {
        bool changed = view.stale;
        view.stale = false;
        return changed;
    }
    bool want = (now_ms - view.received_ms) >= kStaleMs;
    if (want == view.stale) return false;
    view.stale = want;
    return true;
}
