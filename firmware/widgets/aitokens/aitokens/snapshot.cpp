#include "aitokens/snapshot.h"

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

int aitokens::home_row_text(const char *name, const Source &src, bool stale, char *out, size_t out_n,
                            RowLevel *level) {
    if (!out || out_n == 0) {
        if (level) *level = RowLevel::Ok;
        return 0;
    }
    int rem = 100;
    if (src.kind == Kind::Pct) {
        rem = static_cast<int>(src.value);
        if (rem < 0) rem = 0;
        if (rem > 100) rem = 100;
    }
    RowLevel tone = RowLevel::Ok;
    if (src.kind == Kind::Pct) {
        if (rem < kRemainAlert) tone = RowLevel::Alert;
        else if (rem < kRemainLow) tone = RowLevel::Low;
    }
    if (level) *level = tone;

    const bool cursor = eq(src.id, "cursor");
    const bool codex = eq(src.id, "codex");
    const bool claude = eq(src.id, "claude");

    char shown[48];
    if (cursor) {
        int a = -1;
        int b = -1;
        if (sscanf(src.display, "%d%% · %d%%", &a, &b) == 2 && a >= 0 && b >= 0)
            snprintf(shown, sizeof(shown), "Modelos %d%% · Outros %d%%", a, b);
        else
            snprintf(shown, sizeof(shown), "%s", src.display);
    } else if (claude) {
        if (src.display[0]) snprintf(shown, sizeof(shown), "%s", src.display);
        else snprintf(shown, sizeof(shown), "%d%%", rem);
    } else if (codex) {
        snprintf(shown, sizeof(shown), "%d%%", rem);
    } else if (src.display[0]) {
        snprintf(shown, sizeof(shown), "%s", src.display);
    } else {
        snprintf(shown, sizeof(shown), "%d%%", rem);
    }

    const char *unit = (claude || codex) ? "restante" : nullptr;
    const char *label = (name && name[0]) ? name : src.id;

    if (tone == RowLevel::Alert) {
        if (src.reset[0] && unit)
            snprintf(out, out_n, "%s · %s %s · alerta · %s", label, shown, unit, src.reset);
        else if (src.reset[0])
            snprintf(out, out_n, "%s · %s · alerta · %s", label, shown, src.reset);
        else if (unit)
            snprintf(out, out_n, "%s · %s %s · alerta", label, shown, unit);
        else
            snprintf(out, out_n, "%s · %s · alerta", label, shown);
    } else if (tone == RowLevel::Low) {
        if (src.reset[0] && unit)
            snprintf(out, out_n, "%s · %s %s · baixo · %s", label, shown, unit, src.reset);
        else if (src.reset[0])
            snprintf(out, out_n, "%s · %s · baixo · %s", label, shown, src.reset);
        else if (unit)
            snprintf(out, out_n, "%s · %s %s · baixo", label, shown, unit);
        else
            snprintf(out, out_n, "%s · %s · baixo", label, shown);
    } else if (stale) {
        if (unit) snprintf(out, out_n, "%s · %s %s · desatualizado", label, shown, unit);
        else snprintf(out, out_n, "%s · %s · desatualizado", label, shown);
    } else if (src.reset[0]) {
        if (unit) snprintf(out, out_n, "%s · %s %s · %s", label, shown, unit, src.reset);
        else snprintf(out, out_n, "%s · %s · %s", label, shown, src.reset);
    } else if (unit) {
        snprintf(out, out_n, "%s · %s %s", label, shown, unit);
    } else {
        snprintf(out, out_n, "%s · %s", label, shown);
    }
    return rem;
}
