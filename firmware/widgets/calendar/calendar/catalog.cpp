#include "calendar/catalog.h"

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

uint32_t parse_rgb(const char *text) {
    if (!text || !text[0]) return 0x1A73E8;
    return static_cast<uint32_t>(strtoul(text, nullptr, 16)) & 0xFFFFFFu;
}

}  // namespace

bool calendar::apply_catalog_line(Catalog &catalog, const char *line) {
    if (!line || strncmp(line, "CALENDAR_CATALOG", 16) != 0) return false;
    char buf[kCatalogLineMax];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[96];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "CALENDAR_CATALOG")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    const bool ready = field[0] == '1';
    if (!next_field(&cursor, field, sizeof(field))) field[0] = '\0';
    // status "+" = append chunk (multi-account / long catalogs); otherwise replace.
    const bool append = field[0] == '+';

    Catalog next = {};
    if (append) {
        next = catalog;
    } else {
        next.auth_pending = catalog.auth_pending;
    }
    next.ready = ready || (append && catalog.ready);
    if (!append) {
        strncpy(next.status, field, sizeof(next.status) - 1);
        next.status[sizeof(next.status) - 1] = '\0';
    } else if (!next.status[0]) {
        strncpy(next.status, "ok", sizeof(next.status) - 1);
    }

    while (next.count < kMaxCalendars) {
        char account[kEmailMax];
        char id[kCalIdMax];
        char name[kCalNameMax];
        char color[12];
        char selected[4];
        if (!next_field(&cursor, account, sizeof(account)) || !account[0]) break;
        // Stop chunk on garbage (e.g. USB line merge with AITOKENS / CALENDAR).
        if (!strchr(account, '@')) break;
        if (!next_field(&cursor, id, sizeof(id)) || !id[0]) break;
        if (!next_field(&cursor, name, sizeof(name))) break;
        if (!next_field(&cursor, color, sizeof(color))) break;
        if (!next_field(&cursor, selected, sizeof(selected))) selected[0] = '0';
        if (eq(id, "v1") || eq(name, "v1") || eq(name, "ok") || eq(id, "ok")) continue;
        if (strncmp(id, "CALENDAR", 8) == 0 || strncmp(name, "CALENDAR", 8) == 0) break;

        // Replace existing row with same id (re-push), else append.
        int slot = -1;
        for (int i = 0; i < next.count; ++i) {
            if (eq(next.items[i].id, id)) {
                slot = i;
                break;
            }
        }
        if (char *junk = strstr(name, "AITOKEN")) *junk = '\0';
        if (char *junk = strstr(name, "CALENDAR")) *junk = '\0';
        // trim trailing space after strip
        size_t nlen = strlen(name);
        while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\t')) name[--nlen] = '\0';
        if (!name[0]) strncpy(name, id, sizeof(name) - 1);

        CalEntry &e = (slot >= 0) ? next.items[slot] : next.items[next.count++];
        strncpy(e.account, account, sizeof(e.account) - 1);
        e.account[sizeof(e.account) - 1] = '\0';
        strncpy(e.id, id, sizeof(e.id) - 1);
        e.id[sizeof(e.id) - 1] = '\0';
        strncpy(e.name, name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        e.color_rgb = parse_rgb(color);
        e.selected = selected[0] == '1';
    }

    catalog = next;
    return true;
}

bool calendar::apply_auth_line(Catalog &catalog, const char *line) {
    if (!line || strncmp(line, "CALENDAR_AUTH", 13) != 0) return false;
    if (line[13] == '?') return false;
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *cursor = buf;
    char field[80];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "CALENDAR_AUTH")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;
    catalog.auth_pending = field[0] == '1';
    if (next_field(&cursor, field, sizeof(field))) {
        strncpy(catalog.status, field, sizeof(catalog.status) - 1);
        catalog.status[sizeof(catalog.status) - 1] = '\0';
    }
    return true;
}

void calendar::apply_selection(Catalog &catalog, const char *csv_ids) {
    for (int i = 0; i < catalog.count; ++i) catalog.items[i].selected = false;
    if (!csv_ids || !csv_ids[0]) return;
    const char *p = csv_ids;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ',') ++p;
        size_t n = static_cast<size_t>(p - start);
        while (n > 0 && start[n - 1] == ' ') --n;
        if (n == 0 || n >= kCalIdMax) continue;
        char id[kCalIdMax];
        memcpy(id, start, n);
        id[n] = '\0';
        for (int i = 0; i < catalog.count; ++i) {
            if (eq(catalog.items[i].id, id)) catalog.items[i].selected = true;
        }
    }
}

void calendar::format_selection(const Catalog &catalog, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < catalog.count; ++i) {
        if (!catalog.items[i].selected || !catalog.items[i].id[0]) continue;
        size_t id_len = strlen(catalog.items[i].id);
        size_t need = id_len + (used ? 1 : 0);
        if (used + need + 1 > out_len) break;
        if (used) out[used++] = ',';
        memcpy(out + used, catalog.items[i].id, id_len);
        used += id_len;
        out[used] = '\0';
    }
}
