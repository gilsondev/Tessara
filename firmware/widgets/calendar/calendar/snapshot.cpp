#include "calendar/snapshot.h"

#include <cstdio>
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

bool hhmm_field(const char *text) {
    if (!text || std::strlen(text) != 4) return false;
    for (int i = 0; i < 4; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

// USB sometimes delivers the next capability on the same line (newline dropped).
// Those headers are uppercase protocol words, not event titles.
char *protocol_leak(char *text) {
    if (!text || !text[0]) return nullptr;
    static const char *kTokens[] = {
        "FOCUS", "AITOKENS", "AITOKEN", "CALENDAR", "TODOIST", "MEDIA", "THEME", "AGENT", "COVER",
    };
    char *found = nullptr;
    for (const char *token : kTokens) {
        if (char *at = std::strstr(text, token)) {
            if (!found || at < found) found = at;
        }
    }
    return found;
}

void trim_tail(char *text) {
    if (!text) return;
    size_t n = std::strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t')) text[--n] = '\0';
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

    bool saw_leak = false;
    while (next.event_count < kMaxEvents) {
        char start[16];
        char end[8];
        char allday[4];
        char title[kTitleMax];
        char location[kLocationMax];
        char duration[12];
        char color[16];
        char day[24];
        if (!next_field(&cursor, start, sizeof(start)) || !start[0]) break;
        // A full HHMM, not merely a leading digit: "0FOCUS" is a glued next line.
        if (!hhmm_field(start)) {
            if (protocol_leak(start)) saw_leak = true;
            break;
        }
        if (!next_field(&cursor, end, sizeof(end))) break;
        if (!hhmm_field(end)) {
            saw_leak = true;
            break;
        }
        if (!next_field(&cursor, allday, sizeof(allday))) break;
        if (!next_field(&cursor, title, sizeof(title))) break;
        if (char *junk = protocol_leak(title)) {
            *junk = '\0';
            trim_tail(title);
            saw_leak = true;
        }
        if (!title[0]) break;
        if (saw_leak) {
            location[0] = '\0';
            duration[0] = '\0';
            color[0] = '\0';
            day[0] = '0';
            day[1] = '\0';
        } else {
            if (!next_field(&cursor, location, sizeof(location))) location[0] = '\0';
            if (char *junk = protocol_leak(location)) {
                *junk = '\0';
                trim_tail(location);
                saw_leak = true;
            }
            if (!next_field(&cursor, duration, sizeof(duration))) duration[0] = '\0';
            if (!next_field(&cursor, color, sizeof(color))) color[0] = '\0';
            if (protocol_leak(color)) {
                color[0] = '\0';
                saw_leak = true;
            }
            // Optional day_offset (0=today). If the next token looks like HHMM, it's the next event.
            day[0] = '0';
            day[1] = '\0';
            char *before_day = cursor;
            if (!saw_leak && next_field(&cursor, day, sizeof(day)) && day[0]) {
                if (char *junk = protocol_leak(day)) {
                    *junk = '\0';
                    trim_tail(day);
                    saw_leak = true;
                }
                if (hhmm_field(day)) {
                    cursor = before_day;
                    day[0] = '0';
                    day[1] = '\0';
                }
            }
            if (!day[0]) {
                day[0] = '0';
                day[1] = '\0';
            }
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
        if (saw_leak) break;
    }

    // A glued line that yielded nothing must not wipe events already on the tile.
    if (saw_leak && next.event_count == 0 && view.event_count > 0) return false;

    view = next;
    return true;
}

int calendar::next_day(const View &view) {
    int nearest = -1;
    for (int i = 0; i < view.event_count; ++i) {
        const int day = view.events[i].day_offset;
        if (nearest < 0 || day < nearest) nearest = day;
    }
    return nearest;
}

int calendar::events_on_day(const View &view, int day_offset, int *out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < view.event_count && n < cap; ++i) {
        if (view.events[i].day_offset == day_offset) out[n++] = i;
    }
    // Insertion sort: Dia todo first, then by start time. The wire order is already sorted, but a
    // merged cache (several lists, old snapshot) is not guaranteed to be.
    for (int i = 1; i < n; ++i) {
        const int key = out[i];
        const Event &k = view.events[key];
        int j = i - 1;
        while (j >= 0) {
            const Event &cur = view.events[out[j]];
            const bool key_first = (k.all_day && !cur.all_day) ||
                                   (k.all_day == cur.all_day && k.start_min < cur.start_min);
            if (!key_first) break;
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = key;
    }
    return n;
}

void calendar::day_label(char *out, size_t cap, int day_offset, int weekday_today, int count) {
    if (!out || cap == 0) return;
    char head[16] = "";
    if (day_offset <= 0) {
        snprintf(head, sizeof(head), "Hoje");
    } else if (day_offset == 1) {
        snprintf(head, sizeof(head), "Amanhã");
    } else if (weekday_today >= 0) {
        static const char *kDays[7] = {"Domingo", "Segunda", "Terça",  "Quarta",
                                       "Quinta",  "Sexta",   "Sábado"};
        snprintf(head, sizeof(head), "%s", kDays[(weekday_today + day_offset) % 7]);
    } else {
        snprintf(head, sizeof(head), "Em %d dias", day_offset);
    }
    if (count > 0) snprintf(out, cap, "%s · %d", head, count);
    else snprintf(out, cap, "%s", head);
}

static int days_in_month(int year, int month) {
    static const int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    int days = kDays[month];
    const bool leap = month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (leap) days = 29;
    return days;
}

void calendar::day_prefix(char *out, size_t cap, int day_offset, int day, int month, int year) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (day_offset <= 0) return;
    if (day_offset == 1) {
        snprintf(out, cap, "Amanhã:");
        return;
    }
    if (day < 1 || month < 1 || month > 12 || year < 1970) return;
    int d = day;
    int m = month;
    int y = year;
    for (int i = 0; i < day_offset; ++i) {
        ++d;
        if (d > days_in_month(y, m)) {
            d = 1;
            ++m;
            if (m > 12) {
                m = 1;
                ++y;
            }
        }
    }
    snprintf(out, cap, "%02d/%02d:", d, m);
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
