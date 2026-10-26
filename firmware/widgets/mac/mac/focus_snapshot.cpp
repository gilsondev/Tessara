#include "mac/focus_snapshot.h"

#include <cstring>

namespace {

bool eq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

bool next_field(char **cursor, char *out, size_t out_n) {
    if (!cursor || !*cursor || !out || out_n == 0) return false;
    char *start = *cursor;
    char *tab = std::strchr(start, '\t');
    size_t n = tab ? static_cast<size_t>(tab - start) : std::strlen(start);
    if (n >= out_n) n = out_n - 1;
    std::memcpy(out, start, n);
    out[n] = '\0';
    *cursor = tab ? tab + 1 : start + std::strlen(start);
    return true;
}

}  // namespace

bool focus::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "FOCUS\t", 6) != 0) return false;
    if (line[6] == '?') return false;

    char buf[512];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[64];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "FOCUS")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    View next = {};
    next.ready = true;
    next.stale = false;
    next.received_ms = now_ms;
    next.ok = field[0] == '1';

    next_field(&cursor, next.active_id, sizeof(next.active_id));
    next_field(&cursor, next.active_name, sizeof(next.active_name));
    next_field(&cursor, field, sizeof(field));
    int n = 0;
    for (const char *p = field; *p >= '0' && *p <= '9'; ++p) n = n * 10 + (*p - '0');
    if (n < 0) n = 0;
    if (n > kMaxModes) n = kMaxModes;
    next.mode_count = n;
    for (int i = 0; i < n; ++i) {
        next_field(&cursor, next.modes[i].id, sizeof(next.modes[i].id));
        next_field(&cursor, next.modes[i].name, sizeof(next.modes[i].name));
    }

    if (!next.ok) {
        next.active_id[0] = '\0';
        next.active_name[0] = '\0';
        next.mode_count = 0;
    }

    view = next;
    return true;
}

bool focus::refresh_age(View &view, uint32_t now_ms) {
    if (!view.ready) {
        bool changed = view.stale;
        view.stale = false;
        return changed;
    }
    const bool stale = (now_ms - view.received_ms) >= kStaleMs;
    if (view.stale == stale) return false;
    view.stale = stale;
    return true;
}
