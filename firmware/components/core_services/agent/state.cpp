#include "agent/state.h"

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

}  // namespace

bool agent::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "AGENT", 5) != 0) return false;
    if (line[5] == '?') return false;

    char buf[96];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[kDetailMax];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "AGENT")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    View next = {};
    next.ready = true;
    next.ok = field[0] == '1';
    next.received_ms = now_ms;
    if (next_field(&cursor, field, sizeof(field))) {
        strncpy(next.detail, field, sizeof(next.detail) - 1);
        next.detail[sizeof(next.detail) - 1] = '\0';
    }
    view = next;
    return true;
}

agent::View agent::for_display(const View &view, uint32_t now_ms) {
    View out = view;
    if (!out.ready) return out;
    uint32_t age = now_ms - out.received_ms;
    if (age > kFreshMs) {
        out.ok = false;
        strncpy(out.detail, "agent offline", sizeof(out.detail) - 1);
        out.detail[sizeof(out.detail) - 1] = '\0';
    }
    return out;
}
