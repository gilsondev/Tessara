#include "platform/settings.h"

#include "hal/board.h"
#include "services/text.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef TODOIST_TOKEN
#define TODOIST_TOKEN ""
#endif

namespace {
constexpr char kNamespace[] = "todoist";
constexpr char kBackup[] = "todobak";

hal::StoragePort *store() { return hal::board().storage; }

void put_str(const char *key, const char *value) {
    if (auto *s = store()) s->set(kNamespace, key, value ? value : "");
}

bool get_str(const char *key, char *out, size_t cap, const char *fallback = "") {
    if (auto *s = store()) {
        if (s->get(kNamespace, key, out, cap)) return true;
    }
    text::copy(out, cap, fallback ? fallback : "");
    return false;
}

uint32_t get_u32(const char *key, uint32_t fallback) {
    uint32_t v = fallback;
    if (auto *s = store()) s->get_u32(kNamespace, key, &v);
    return v;
}

bool get_bool(const char *key, bool fallback) {
    bool v = fallback;
    if (auto *s = store()) {
        if (s->get_bool(kNamespace, key, &v)) return v;
    }
    return fallback;
}
}  // namespace

settings::Credentials settings::load_credentials() {
    Credentials out = {};
    get_str("ssid", out.ssid, sizeof(out.ssid), WIFI_SSID);
    get_str("pass", out.pass, sizeof(out.pass), WIFI_PASS);
    get_str("token", out.token, sizeof(out.token), TODOIST_TOKEN);
    return out;
}

void settings::save_wifi(const char *ssid, const char *pass) {
    put_str("ssid", ssid);
    put_str("pass", pass);
}

void settings::save_token(const char *token) { put_str("token", token); }

void settings::clear_token() { save_token(""); }

void settings::clear_credentials() {
    save_wifi("", "");
    save_token("");
}

void settings::stash() {
    Credentials live = load_credentials();
    if (!live.ssid[0] && !live.token[0]) return;
    auto *s = store();
    if (!s) return;
    bool ok = false;
    if (s->get_bool(kBackup, "ok", &ok) && ok) return;
    s->set(kBackup, "ssid", live.ssid);
    s->set(kBackup, "pass", live.pass);
    s->set(kBackup, "token", live.token);
    s->set_bool(kBackup, "ok", true);
}

bool settings::unstash() {
    auto *s = store();
    if (!s) return false;
    bool ok = false;
    if (!s->get_bool(kBackup, "ok", &ok) || !ok) return false;
    char ssid[64] = {}, pass[64] = {}, token[128] = {};
    s->get(kBackup, "ssid", ssid, sizeof(ssid));
    s->get(kBackup, "pass", pass, sizeof(pass));
    s->get(kBackup, "token", token, sizeof(token));
    s->set_bool(kBackup, "ok", false);
    save_wifi(ssid, pass);
    save_token(token);
    return true;
}

DeviceSettings settings::load_device() {
    DeviceSettings out;
    out.portrait = get_u32("lado", 0) == 0;
    out.sound = get_bool("som", out.sound);
    out.appearance = clamp_appearance(static_cast<int>(get_u32("aparencia", 0)));
    out.os_prefers_dark = get_bool("os_dark", false);
    out.os_theme_known = get_bool("os_known", false);
    out.brightness = clamp_brightness(static_cast<int>(get_u32("brilho", out.brightness)));
    out.sync_minutes = clamp_sync(static_cast<int>(get_u32("sync", out.sync_minutes)));
    out.rest_minutes = clamp_rest_minutes(static_cast<int>(get_u32("descanso", out.rest_minutes)));
    out.clock_minutes = clamp_clock_minutes(static_cast<int>(get_u32("relogio", out.clock_minutes)));
    out.show_aitokens_codex = get_bool("ai_codex", true);
    out.show_aitokens_claude = get_bool("ai_claude", true);
    out.show_aitokens_cursor = get_bool("ai_cursor", true);
    return out;
}

void settings::save_device(const DeviceSettings &value) {
    auto *s = store();
    if (!s) return;
    s->set_u32(kNamespace, "lado", value.portrait ? 0 : 1);
    s->set_bool(kNamespace, "som", value.sound);
    s->set_u32(kNamespace, "aparencia", static_cast<uint32_t>(value.appearance));
    s->set_bool(kNamespace, "os_dark", value.os_prefers_dark);
    s->set_bool(kNamespace, "os_known", value.os_theme_known);
    s->set_u32(kNamespace, "brilho", value.brightness);
    s->set_u32(kNamespace, "sync", value.sync_minutes);
    s->set_u32(kNamespace, "descanso", value.rest_minutes);
    s->set_u32(kNamespace, "relogio", value.clock_minutes);
    s->set_bool(kNamespace, "ai_codex", value.show_aitokens_codex);
    s->set_bool(kNamespace, "ai_claude", value.show_aitokens_claude);
    s->set_bool(kNamespace, "ai_cursor", value.show_aitokens_cursor);
}

settings::WeatherPlace settings::load_weather() {
    WeatherPlace out = {};
    char name[48] = {};
    if (!get_str("clima_nome", name, sizeof(name)) || !name[0]) return out;
    text::copy(out.name, sizeof(out.name), name);
    auto *s = store();
    if (s) {
        s->get_f32(kNamespace, "clima_lat", &out.latitude);
        s->get_f32(kNamespace, "clima_lon", &out.longitude);
    }
    out.set = true;
    return out;
}

void settings::save_weather(const char *name, float latitude, float longitude) {
    put_str("clima_nome", name);
    if (auto *s = store()) {
        s->set_f32(kNamespace, "clima_lat", latitude);
        s->set_f32(kNamespace, "clima_lon", longitude);
    }
}

void settings::clear_weather() {
    if (auto *s = store()) {
        s->erase(kNamespace, "clima_nome");
        s->erase(kNamespace, "clima_lat");
        s->erase(kNamespace, "clima_lon");
    }
}

dashboard::Layout settings::load_dashboard() {
    char csv[192] = {};
    get_str("dash_ids", csv, sizeof(csv));
    dashboard::Layout layout = {};
    dashboard::parse(csv, layout);
    // Hero widgets (saudação / data / relógio) were omitted from early NVS layouts.
    // Restore them once so the home hero is not blank after the portal-era toggle.
    const bool has_hero = dashboard::is_enabled(layout, dashboard::id_greeting()) ||
                          dashboard::is_enabled(layout, dashboard::id_date()) ||
                          dashboard::is_enabled(layout, dashboard::id_clock());
    const bool has_tiles = dashboard::is_enabled(layout, dashboard::id_todoist()) ||
                           dashboard::is_enabled(layout, dashboard::id_weather()) ||
                           dashboard::is_enabled(layout, dashboard::id_aitokens());
    if (has_tiles && !has_hero) {
        dashboard::set_enabled(layout, dashboard::id_greeting(), true);
        dashboard::set_enabled(layout, dashboard::id_date(), true);
        dashboard::set_enabled(layout, dashboard::id_clock(), true);
        save_dashboard(layout);
    }
    return layout;
}

void settings::save_dashboard(const dashboard::Layout &layout) {
    char csv[192];
    dashboard::format(layout, csv, sizeof(csv));
    put_str("dash_ids", csv);
}
