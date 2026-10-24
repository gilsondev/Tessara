#include "aitokens/snapshot.h"

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

aitokens::Kind parse_kind(const char *text) {
    if (eq(text, "tokens")) return aitokens::Kind::Tokens;
    if (eq(text, "pct")) return aitokens::Kind::Pct;
    return aitokens::Kind::None;
}

}  // namespace

bool aitokens::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "AITOKENS", 8) != 0) return false;
    char buf[320];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[48];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "AITOKENS")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    View next = {};
    next.bridge_ok = field[0] == '1';
    next.ready = true;
    next.stale = false;
    next.received_ms = now_ms;

    while (next.source_count < kMaxSources) {
        char id[kIdMax];
        char avail[4];
        char kind[12];
        char value[16];
        char display[kDisplayMax];
        char reset[kResetMax];
        if (!next_field(&cursor, id, sizeof(id)) || !id[0]) break;
        if (!next_field(&cursor, avail, sizeof(avail))) break;
        if (!next_field(&cursor, kind, sizeof(kind))) break;
        if (!next_field(&cursor, value, sizeof(value))) break;
        if (!next_field(&cursor, display, sizeof(display))) break;
        if (!next_field(&cursor, reset, sizeof(reset))) reset[0] = '\0';

        Source &s = next.sources[next.source_count++];
        strncpy(s.id, id, sizeof(s.id) - 1);
        s.id[sizeof(s.id) - 1] = '\0';
        s.available = avail[0] == '1';
        s.kind = parse_kind(kind);
        s.value = static_cast<int32_t>(strtol(value, nullptr, 10));
        strncpy(s.display, display, sizeof(s.display) - 1);
        s.display[sizeof(s.display) - 1] = '\0';
        strncpy(s.reset, reset, sizeof(s.reset) - 1);
        s.reset[sizeof(s.reset) - 1] = '\0';
    }

    view = next;
    return true;
}

bool aitokens::refresh_age(View &view, uint32_t now_ms) {
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

const aitokens::Source *aitokens::find(const View &view, const char *id) {
    for (int i = 0; i < view.source_count; ++i) {
        if (eq(view.sources[i].id, id)) return &view.sources[i];
    }
    return nullptr;
}
