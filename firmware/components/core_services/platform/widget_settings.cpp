#include "platform/widget_settings.h"

#include "domain/dashboard_layout.h"
#include "hal/board.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr char kTodoistNs[] = "w_todoist";
constexpr char kAitokensNs[] = "w_aitoken";
constexpr char kWeatherNs[] = "w_weather";
constexpr char kClockNs[] = "w_clock";
constexpr char kDateNs[] = "w_date";
constexpr char kMiscNs[] = "w_misc";

hal::StoragePort *store() { return hal::board().storage; }

const char *ns_for(const char *instance_id) {
    if (!instance_id || !instance_id[0]) return kMiscNs;
    if (std::strstr(instance_id, "todoist")) return kTodoistNs;
    if (std::strstr(instance_id, "aitokens")) return kAitokensNs;
    if (std::strstr(instance_id, "weather")) return kWeatherNs;
    if (std::strstr(instance_id, "date")) return kDateNs;
    if (std::strstr(instance_id, "clock")) return kClockNs;
    return kMiscNs;
}

// Logical key → NVS key (≤15 chars).
const char *nvs_key(const char *key) {
    if (!key) return "";
    if (!std::strcmp(key, "sync_minutes") || !std::strcmp(key, "sync")) return "sync";
    if (!std::strcmp(key, "show_codex") || !std::strcmp(key, "show_aitokens_codex")) return "ai_codex";
    if (!std::strcmp(key, "show_claude") || !std::strcmp(key, "show_aitokens_claude")) return "ai_claude";
    if (!std::strcmp(key, "show_cursor") || !std::strcmp(key, "show_aitokens_cursor")) return "ai_cursor";
    if (!std::strcmp(key, "timezone")) return "tz";
    if (!std::strcmp(key, "time_format")) return "time_fmt";
    if (!std::strcmp(key, "date_format")) return "date_fmt";
    // Truncate unknown keys to fit NVS.
    return key;
}

bool key_ok(const char *key) {
    return key && key[0] && std::strlen(key) <= 15;
}

}  // namespace

bool widget_settings::get(const char *instance_id, const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return false;
    auto *s = store();
    if (!s) return false;
    return s->get(ns_for(instance_id), nk, out, out_len);
}

bool widget_settings::set(const char *instance_id, const char *key, const char *value) {
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return false;
    auto *s = store();
    if (!s) return false;
    return s->set(ns_for(instance_id), nk, value ? value : "");
}

uint8_t widget_settings::get_u8(const char *instance_id, const char *key, uint8_t fallback) {
    uint32_t v = fallback;
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return fallback;
    auto *s = store();
    if (!s || !s->get_u32(ns_for(instance_id), nk, &v)) return fallback;
    if (v > 255) v = 255;
    return static_cast<uint8_t>(v);
}

bool widget_settings::set_u8(const char *instance_id, const char *key, uint8_t value) {
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return false;
    auto *s = store();
    if (!s) return false;
    return s->set_u32(ns_for(instance_id), nk, value);
}

bool widget_settings::get_bool(const char *instance_id, const char *key, bool fallback) {
    bool v = fallback;
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return fallback;
    auto *s = store();
    if (!s || !s->get_bool(ns_for(instance_id), nk, &v)) return fallback;
    return v;
}

bool widget_settings::set_bool(const char *instance_id, const char *key, bool value) {
    const char *nk = nvs_key(key);
    if (!key_ok(nk)) return false;
    auto *s = store();
    if (!s) return false;
    return s->set_bool(ns_for(instance_id), nk, value);
}

uint8_t widget_settings::todoist_sync_minutes(uint8_t fallback) {
    return get_u8(dashboard::id_todoist(), "sync_minutes", fallback);
}

void widget_settings::set_todoist_sync_minutes(uint8_t minutes) {
    set_u8(dashboard::id_todoist(), "sync_minutes", minutes);
}
