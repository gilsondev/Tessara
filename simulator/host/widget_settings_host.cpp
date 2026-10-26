#include "platform/widget_settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// In-memory stand-in for NVS widget settings on the host simulator.
namespace {

struct Entry {
    char id[48] = {};
    char key[24] = {};
    char value[64] = {};
    bool used = false;
};

constexpr int kMax = 32;
Entry g_entries[kMax];

Entry *find(const char *id, const char *key, bool create) {
    if (!id || !key || !id[0] || !key[0]) return nullptr;
    for (int i = 0; i < kMax; ++i) {
        if (g_entries[i].used && std::strcmp(g_entries[i].id, id) == 0 &&
            std::strcmp(g_entries[i].key, key) == 0) {
            return &g_entries[i];
        }
    }
    if (!create) return nullptr;
    for (int i = 0; i < kMax; ++i) {
        if (g_entries[i].used) continue;
        g_entries[i].used = true;
        std::strncpy(g_entries[i].id, id, sizeof(g_entries[i].id) - 1);
        std::strncpy(g_entries[i].key, key, sizeof(g_entries[i].key) - 1);
        g_entries[i].value[0] = '\0';
        return &g_entries[i];
    }
    return nullptr;
}

}  // namespace

bool widget_settings::get(const char *instance_id, const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    Entry *e = find(instance_id, key, false);
    if (!e) return false;
    std::strncpy(out, e->value, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

bool widget_settings::set(const char *instance_id, const char *key, const char *value) {
    Entry *e = find(instance_id, key, true);
    if (!e) return false;
    std::strncpy(e->value, value ? value : "", sizeof(e->value) - 1);
    e->value[sizeof(e->value) - 1] = '\0';
    return true;
}

uint8_t widget_settings::get_u8(const char *instance_id, const char *key, uint8_t fallback) {
    char buf[16] = {};
    if (!get(instance_id, key, buf, sizeof(buf)) || !buf[0]) return fallback;
    return static_cast<uint8_t>(::atoi(buf));
}

bool widget_settings::set_u8(const char *instance_id, const char *key, uint8_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(value));
    return set(instance_id, key, buf);
}

bool widget_settings::get_bool(const char *instance_id, const char *key, bool fallback) {
    char buf[8] = {};
    if (!get(instance_id, key, buf, sizeof(buf)) || !buf[0]) return fallback;
    return buf[0] == '1' || buf[0] == 't' || buf[0] == 'T';
}

bool widget_settings::set_bool(const char *instance_id, const char *key, bool value) {
    return set(instance_id, key, value ? "1" : "0");
}

uint8_t widget_settings::todoist_sync_minutes(uint8_t fallback) {
    return get_u8("org.tessera.todoist", "sync_minutes", fallback);
}

void widget_settings::set_todoist_sync_minutes(uint8_t minutes) {
    set_u8("org.tessera.todoist", "sync_minutes", minutes);
}
