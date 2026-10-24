#include "domain/dashboard_layout.h"

#include <cstring>

namespace {

void copy_id(char *dest, size_t cap, const char *src) {
    if (!dest || cap == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

bool eq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

int find_index(const dashboard::Layout &layout, const char *type_id) {
    for (int i = 0; i < layout.count; ++i) {
        if (eq(layout.ids[i], type_id)) return i;
    }
    return -1;
}

void remove_at(dashboard::Layout &layout, int index) {
    if (index < 0 || index >= layout.count) return;
    for (int i = index; i + 1 < layout.count; ++i) {
        copy_id(layout.ids[i], dashboard::kIdLen, layout.ids[i + 1]);
    }
    --layout.count;
    if (layout.count >= 0) layout.ids[layout.count][0] = '\0';
}

}  // namespace

const char *dashboard::id_todoist() { return "org.tessara.todoist"; }
const char *dashboard::id_weather() { return "org.tessara.weather"; }
const char *dashboard::id_aitokens() { return "org.tessara.aitokens"; }
const char *dashboard::id_clock() { return "org.tessara.clock"; }
const char *dashboard::id_greeting() { return "org.tessara.greeting"; }
const char *dashboard::id_date() { return "org.tessara.date"; }
// Legacy id kept so old CSV layouts that listed hello parse cleanly (dropped by known_type).
const char *dashboard::id_hello() { return "org.tessara.hello"; }

bool dashboard::known_type(const char *type_id) {
    return eq(type_id, id_todoist()) || eq(type_id, id_weather()) || eq(type_id, id_aitokens()) ||
           eq(type_id, id_clock()) || eq(type_id, id_greeting()) || eq(type_id, id_date());
}

dashboard::Layout dashboard::defaults() {
    Layout out = {};
    copy_id(out.ids[0], kIdLen, id_greeting());
    copy_id(out.ids[1], kIdLen, id_date());
    copy_id(out.ids[2], kIdLen, id_clock());
    copy_id(out.ids[3], kIdLen, id_todoist());
    copy_id(out.ids[4], kIdLen, id_weather());
    copy_id(out.ids[5], kIdLen, id_aitokens());
    out.count = 6;
    return out;
}

bool dashboard::parse(const char *csv, Layout &out) {
    out = {};
    if (!csv || !csv[0]) {
        out = defaults();
        return true;
    }
    const char *p = csv;
    while (*p && out.count < kMaxSlots) {
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ',') ++p;
        size_t n = static_cast<size_t>(p - start);
        while (n > 0 && (start[n - 1] == ' ')) --n;
        if (n == 0 || n >= kIdLen) continue;
        char id[kIdLen];
        std::memcpy(id, start, n);
        id[n] = '\0';
        if (!known_type(id)) continue;
        if (find_index(out, id) >= 0) continue;
        copy_id(out.ids[out.count], kIdLen, id);
        ++out.count;
    }
    if (out.count == 0) {
        out = defaults();
        return true;
    }
    return true;
}

void dashboard::format(const Layout &in, char *dest, size_t cap) {
    if (!dest || cap == 0) return;
    dest[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < in.count; ++i) {
        if (!in.ids[i][0]) continue;
        size_t need = std::strlen(in.ids[i]) + (used ? 1 : 0);
        if (used + need + 1 > cap) break;
        if (used) dest[used++] = ',';
        std::strcpy(dest + used, in.ids[i]);
        used += std::strlen(in.ids[i]);
    }
}

bool dashboard::is_enabled(const Layout &layout, const char *type_id) {
    return find_index(layout, type_id) >= 0;
}

void dashboard::set_enabled(Layout &layout, const char *type_id, bool on) {
    if (!known_type(type_id)) return;
    int idx = find_index(layout, type_id);
    if (on) {
        if (idx >= 0) return;
        if (layout.count >= kMaxSlots) return;
        copy_id(layout.ids[layout.count], kIdLen, type_id);
        ++layout.count;
    } else if (idx >= 0) {
        remove_at(layout, idx);
    }
}

int dashboard::compose(const Layout &enabled, const char *const *registered, int registered_n,
                       char out[][kIdLen], int out_cap) {
    if (!out || out_cap <= 0 || !registered || registered_n <= 0) return 0;
    int n = 0;
    for (int i = 0; i < enabled.count && n < out_cap; ++i) {
        const char *id = enabled.ids[i];
        bool ok = false;
        for (int r = 0; r < registered_n; ++r) {
            if (eq(id, registered[r])) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;
        copy_id(out[n], kIdLen, id);
        ++n;
    }
    return n;
}
