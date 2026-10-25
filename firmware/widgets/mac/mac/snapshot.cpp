#include "mac/snapshot.h"

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

mac::MediaState parse_state(const char *text) {
    if (eq(text, "playing")) return mac::MediaState::Playing;
    if (eq(text, "paused")) return mac::MediaState::Paused;
    return mac::MediaState::Stopped;
}

}  // namespace

bool mac::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "MEDIA", 5) != 0) return false;
    if (line[5] == '?') return false;

    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[64];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "MEDIA")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    View next = {};
    next.ready = true;
    next.stale = false;
    next.received_ms = now_ms;
    next.media.ok = field[0] == '1';

    char state[16] = {};
    next_field(&cursor, state, sizeof(state));
    next.media.state = parse_state(state);

    next_field(&cursor, next.media.title, sizeof(next.media.title));
    next_field(&cursor, next.media.artist, sizeof(next.media.artist));
    next_field(&cursor, next.media.app, sizeof(next.media.app));

    if (!next.media.ok) {
        next.media.state = MediaState::Stopped;
        next.media.title[0] = '\0';
        next.media.artist[0] = '\0';
        next.media.app[0] = '\0';
    }

    view = next;
    return true;
}

bool mac::refresh_age(View &view, uint32_t now_ms) {
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
