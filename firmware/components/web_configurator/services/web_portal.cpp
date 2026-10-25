#include "services/web_portal.h"

#include "platform/agent_push.h"
#include "platform/log.h"
#include "platform/sd_host.h"
#include "services/text.h"
#include "services/setup_page.h"
#include "services/web_page.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

namespace {
// Browsers open spare connections and leave them idle. Every slot is served without blocking, so an idle
// socket can't hold the page hostage; it is simply dropped after kIdleMs.
constexpr int kSlots = 4;
constexpr uint32_t kIdleMs = 3000;
constexpr size_t kMaxRequest = 2048;
// Upload do pacote do Agente (stream direto pro SD; Python hoje, binário depois).
constexpr size_t kMaxAgentUpload = 8 * 1024 * 1024;

struct Reply {
    int code;
    const char *type;
    String body;
    const char *flash = nullptr;
};

struct Found {
    char ssid[33];
    int rssi;
};

constexpr int kMaxNets = 12;
Found nets[kMaxNets];
int net_count = 0;
// 0 home Wi-Fi, 1 start the setup network, 2 setup network is up, 3 stop it.
// Only the web task calls WiFi while this is changing; the UI loop just requests a phase.
volatile int wifi_phase = 0;
volatile bool scanning = false;
volatile int serve_want = 2;  // 1 = setup AP, 2 = home network
bool setup_on = false;
DNSServer dns;

struct Slot {
    WiFiClient client;
    String raw;
    uint32_t since;
    bool used;
};

char hostname[32] = "tessara";
char host_local[40] = "tessara.local";
WiFiServer server(80);
Slot slots[kSlots];
TaskHandle_t task = nullptr;
QueueHandle_t requests = nullptr;
SemaphoreHandle_t guard = nullptr;
web_portal::Snapshot snapshot = {};
web_portal::CalendarCatalog calendar_catalog = {};

Reply text_reply(int code, const char *body) { return Reply{code, "text/plain; charset=utf-8", body}; }

Reply accept(const web_portal::Request &request) {
    if (xQueueSend(requests, &request, 0) != pdTRUE) return text_reply(503, "ocupado");
    return Reply{200, "application/json", "{\"ok\":true}"};
}

Reply state() {
    JsonDocument doc;
    xSemaphoreTake(guard, portMAX_DELAY);
    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["portrait"] = snapshot.settings.portrait;
    settings["sound"] = snapshot.settings.sound;
    settings["appearance"] = static_cast<int>(snapshot.settings.appearance);
    settings["os_dark"] = snapshot.settings.os_prefers_dark;
    settings["os_theme_known"] = snapshot.settings.os_theme_known;
    settings["brightness"] = snapshot.settings.brightness;
    // Device snapshot keeps sync for legacy; Todoist page reads widget_settings.
    settings["sync_minutes"] = snapshot.settings.sync_minutes;
    settings["show_aitokens_codex"] = snapshot.settings.show_aitokens_codex;
    settings["show_aitokens_claude"] = snapshot.settings.show_aitokens_claude;
    settings["show_aitokens_cursor"] = snapshot.settings.show_aitokens_cursor;
    settings["rest_minutes"] = snapshot.settings.rest_minutes;
    settings["clock_minutes"] = snapshot.settings.clock_minutes;
    JsonObject wset = doc["widget_settings"].to<JsonObject>();
    JsonObject todoist_w = wset[dashboard::id_todoist()].to<JsonObject>();
    todoist_w["sync_minutes"] = snapshot.settings.sync_minutes;
    JsonObject aitokens_w = wset[dashboard::id_aitokens()].to<JsonObject>();
    aitokens_w["show_aitokens_codex"] = snapshot.settings.show_aitokens_codex;
    aitokens_w["show_aitokens_claude"] = snapshot.settings.show_aitokens_claude;
    aitokens_w["show_aitokens_cursor"] = snapshot.settings.show_aitokens_cursor;
    JsonObject clock_w = wset[dashboard::id_clock()].to<JsonObject>();
    clock_w["timezone"] = snapshot.clock_timezone[0] ? snapshot.clock_timezone : "sao_paulo";
    clock_w["time_format"] = snapshot.clock_time_format[0] ? snapshot.clock_time_format : "hm";
    JsonObject date_w = wset[dashboard::id_date()].to<JsonObject>();
    date_w["date_format"] = snapshot.date_format[0] ? snapshot.date_format : "short_pt";
    JsonObject info = doc["info"].to<JsonObject>();
    info["host"] = snapshot.info.host;
    info["ip"] = snapshot.info.ip;
    info["ssid"] = snapshot.info.ssid;
    info["online"] = snapshot.info.online;
    info["rssi"] = snapshot.info.rssi;
    JsonObject res = doc["resources"].to<JsonObject>();
    const auto &r = snapshot.resources;
    res["heap_free"] = r.heap_free_b;
    res["heap_total"] = r.heap_total_b;
    res["heap_largest"] = r.heap_largest_b;
    res["heap_min"] = r.heap_min_b;
    res["psram_free"] = r.psram_free_b;
    res["psram_total"] = r.psram_total_b;
    res["psram_largest"] = r.psram_largest_b;
    res["flash_sketch"] = r.flash_sketch_b;
    res["flash_sketch_free"] = r.flash_sketch_free_b;
    res["storage_ready"] = r.storage_ready;
    res["storage_kind"] = r.storage_kind;
    res["storage_free"] = r.storage_free_b;
    res["storage_total"] = r.storage_total_b;
    res["uptime_s"] = r.uptime_s;
    JsonArray lists = doc["lists"].to<JsonArray>();
    for (int i = 0; i < snapshot.list_count; ++i) {
        JsonObject item = lists.add<JsonObject>();
        item["name"] = snapshot.lists[i].name;
        item["shown"] = snapshot.lists[i].shown;
    }
    doc["token_set"] = snapshot.token_set;
    JsonObject weather = doc["weather"].to<JsonObject>();
    weather["city"] = snapshot.weather_city;
    weather["ready"] = snapshot.weather_ready;
    JsonObject agent = doc["agent"].to<JsonObject>();
    agent["ready"] = snapshot.agent.ready;
    agent["ok"] = snapshot.agent.ok;
    agent["detail"] = snapshot.agent.detail;
    doc["agent_pack"] = snapshot.agent_pack;
    doc["agent_push_armed"] = agent_push::armed();
    doc["agent_pack_version"] = snapshot.agent_pack_version;
    JsonObject aitokens = doc["aitokens"].to<JsonObject>();
    aitokens["ready"] = snapshot.aitokens_ready;
    aitokens["stale"] = snapshot.aitokens_stale;
    aitokens["age_s"] = snapshot.aitokens_age_s;
    JsonObject calendar = doc["calendar"].to<JsonObject>();
    calendar["ready"] = snapshot.calendar_ready;
    calendar["stale"] = snapshot.calendar_stale;
    calendar["linked"] = snapshot.calendar_linked;
    calendar["auth_pending"] = snapshot.calendar_auth_pending;
    calendar["age_s"] = snapshot.calendar_age_s;
    calendar["status"] = snapshot.calendar_status;
    calendar["selection"] = snapshot.calendar_selection;
    JsonArray cal_rows = calendar["calendars"].to<JsonArray>();
    for (int i = 0; i < calendar_catalog.count; ++i) {
        JsonObject row = cal_rows.add<JsonObject>();
        row["account"] = calendar_catalog.rows[i].account;
        row["id"] = calendar_catalog.rows[i].id;
        row["name"] = calendar_catalog.rows[i].name;
        char color[8];
        std::snprintf(color, sizeof(color), "%06X", calendar_catalog.rows[i].color_rgb & 0xFFFFFFu);
        row["color"] = color;
        row["selected"] = calendar_catalog.rows[i].selected;
    }
    JsonArray widgets = doc["widgets"].to<JsonArray>();
    const char *catalog[] = {dashboard::id_greeting(), dashboard::id_date(),    dashboard::id_clock(),
                             dashboard::id_todoist(),  dashboard::id_weather(), dashboard::id_aitokens(),
                             dashboard::id_calendar(), dashboard::id_mac()};
    const char *titles[] = {"Saudação", "Data", "Relógio", "Tarefas", "Clima", "AI Tokens", "Calendário",
                            "Mac"};
    static_assert(sizeof(catalog) / sizeof(catalog[0]) == sizeof(titles) / sizeof(titles[0]), "catalog/titles");
    for (size_t i = 0; i < sizeof(catalog) / sizeof(catalog[0]); ++i) {
        JsonObject item = widgets.add<JsonObject>();
        item["id"] = catalog[i];
        item["title"] = titles[i];
        item["shown"] = dashboard::is_enabled(snapshot.dashboard, catalog[i]);
    }
    xSemaphoreGive(guard);
    Reply reply{200, "application/json", ""};
    serializeJson(doc, reply.body);
    return reply;
}

Reply widget_settings_edit(JsonDocument &doc) {
    const char *id = doc["id"] | "";
    const char *key = doc["key"] | "";
    if (!id[0] || !key[0]) return text_reply(400, "faltam id/key");
    web_portal::Request request = {};
    request.kind = web_portal::Kind::WidgetSettings;
    text::copy(request.widget_id, sizeof(request.widget_id), id);
    text::copy(request.setting_key, sizeof(request.setting_key), key);
    if (doc["value"].is<bool>()) {
        text::copy(request.setting_value, sizeof(request.setting_value), doc["value"].as<bool>() ? "1" : "0");
    } else if (doc["value"].is<int>()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", doc["value"].as<int>());
        text::copy(request.setting_value, sizeof(request.setting_value), buf);
    } else {
        text::copy(request.setting_value, sizeof(request.setting_value), doc["value"] | "");
    }
    return accept(request);
}

Reply dashboard_edit(JsonDocument &doc) {
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Dashboard;
    JsonArray rows = doc["widgets"].as<JsonArray>();
    if (rows.isNull()) return text_reply(400, "Faltam os widgets");
    request.dashboard = {};
    int shown = 0;
    for (JsonObject item : rows) {
        const char *id = item["id"] | "";
        bool on = item["shown"] | false;
        if (!id[0] || !on) continue;
        dashboard::set_enabled(request.dashboard, id, true);
        if (dashboard::is_enabled(request.dashboard, id)) ++shown;
    }
    if (!shown) return text_reply(400, "Deixe ao menos um widget");
    return accept(request);
}

Reply settings(JsonDocument &doc) {
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Settings;
    xSemaphoreTake(guard, portMAX_DELAY);
    request.settings = snapshot.settings;
    xSemaphoreGive(guard);
    request.settings.portrait = doc["portrait"] | request.settings.portrait;
    request.settings.sound = doc["sound"] | request.settings.sound;
    if (doc["appearance"].is<int>()) {
        request.settings.appearance = clamp_appearance(doc["appearance"].as<int>());
    } else if (doc["dark"].is<bool>()) {
        // Legacy portal payloads: bool dark → Light/Dark (not Auto).
        request.settings.appearance = doc["dark"].as<bool>() ? Appearance::Dark : Appearance::Light;
    }
    if (doc["os_dark"].is<bool>()) {
        request.settings.os_prefers_dark = doc["os_dark"].as<bool>();
        request.settings.os_theme_known = true;
    }
    if (doc["os_theme_known"].is<bool>()) {
        request.settings.os_theme_known = doc["os_theme_known"].as<bool>();
    }
    request.settings.brightness = clamp_brightness(doc["brightness"] | static_cast<int>(request.settings.brightness));
    request.settings.sync_minutes = clamp_sync(doc["sync_minutes"] | static_cast<int>(request.settings.sync_minutes));
    if (!doc["rest_minutes"].isNull())
        request.settings.rest_minutes = clamp_rest_minutes(doc["rest_minutes"].as<int>());
    if (!doc["clock_minutes"].isNull())
        request.settings.clock_minutes = clamp_clock_minutes(doc["clock_minutes"].as<int>());
    if (!doc["show_aitokens_codex"].isNull()) request.settings.show_aitokens_codex = doc["show_aitokens_codex"].as<bool>();
    if (!doc["show_aitokens_claude"].isNull())
        request.settings.show_aitokens_claude = doc["show_aitokens_claude"].as<bool>();
    if (!doc["show_aitokens_cursor"].isNull())
        request.settings.show_aitokens_cursor = doc["show_aitokens_cursor"].as<bool>();
    return accept(request);
}

Reply wifi(JsonDocument &doc) {
    const char *ssid = doc["ssid"] | "";
    if (!ssid[0]) return text_reply(400, "falta a rede");
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Wifi;
    text::copy(request.ssid, sizeof(request.ssid), ssid);
    text::copy(request.pass, sizeof(request.pass), doc["pass"] | "");
    return accept(request);
}

Reply token(JsonDocument &doc) {
    const char *value = doc["token"] | "";
    if (strlen(value) < 20) return text_reply(400, "token invalido");
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Token;
    text::copy(request.token, sizeof(request.token), value);
    return accept(request);
}

Reply networks() {
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    xSemaphoreTake(guard, portMAX_DELAY);
    for (int i = 0; i < net_count; ++i) {
        JsonObject item = list.add<JsonObject>();
        item["ssid"] = nets[i].ssid;
        item["rssi"] = nets[i].rssi;
    }
    xSemaphoreGive(guard);
    Reply reply{200, "application/json", ""};
    serializeJson(doc, reply.body);
    return reply;
}

Reply setup(JsonDocument &doc) {
    const char *ssid = doc["ssid"] | "";
    const char *token = doc["token"] | "";
    if (!ssid[0]) return text_reply(400, "Falta a rede");
    if (token[0] && strlen(token) < 20) return text_reply(400, "Token invalido");
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Setup;
    text::copy(request.ssid, sizeof(request.ssid), ssid);
    text::copy(request.pass, sizeof(request.pass), doc["pass"] | "");
    text::copy(request.token, sizeof(request.token), token);
    return accept(request);
}

Reply weather(JsonDocument &doc) {
    // Never call Open-Meteo from the web task: TLS there races the net worker and reverse
    // geocode is not available on their API. The controller resolves city names on the worker.
    web_portal::Request request = {};
    request.kind = web_portal::Kind::WeatherPlace;
    const char *city = doc["city"] | "";
    const char *name = doc["name"] | "";
    bool has_coords = !doc["latitude"].isNull() && !doc["longitude"].isNull();
    if (has_coords) {
        request.weather_has_coords = true;
        request.weather_lat = doc["latitude"].as<float>();
        request.weather_lon = doc["longitude"].as<float>();
        text::copy(request.weather_name, sizeof(request.weather_name),
                   name[0] ? name : (city[0] ? city : "Minha localização"));
        return accept(request);
    }
    if (!city[0]) return text_reply(400, "Falta a cidade");
    text::copy(request.weather_city, sizeof(request.weather_city), city);
    if (name[0]) text::copy(request.weather_name, sizeof(request.weather_name), name);
    return accept(request);
}

Reply lists_edit(JsonDocument &doc) {
    web_portal::Request request = {};
    request.kind = web_portal::Kind::Lists;
    JsonArray rows = doc["lists"].as<JsonArray>();
    if (rows.isNull()) return text_reply(400, "Faltam as listas");
    int shown = 0;
    for (JsonObject item : rows) {
        if (request.list_count >= kMaxViews) break;
        const char *name = item["name"] | "";
        if (!name[0]) continue;
        web_portal::ListRow &row = request.lists[request.list_count++];
        text::copy(row.name, sizeof(row.name), name);
        row.shown = item["shown"] | false;
        if (row.shown) shown++;
    }
    if (!request.list_count || !shown) return text_reply(400, "Deixe ao menos uma lista");
    return accept(request);
}

Reply route(const String &method, const String &path, const char *body) {
    if (setup_on && method == "GET") {
        if (path == "/api/networks") return networks();
        return Reply{200, "text/html; charset=utf-8", "", web_portal::kSetupPage};
    }
    if (method == "GET") {
        if (path == "/") return Reply{200, "text/html; charset=utf-8", "", web_portal::kPage};
        if (path == "/api/state") return state();
        return text_reply(404, "nada aqui");
    }
    if (method != "POST") return text_reply(405, "metodo nao suportado");
    if (setup_on && path != "/api/setup") return text_reply(404, "nada aqui");
    if (path == "/api/refresh") {
        web_portal::Request request = {};
        request.kind = web_portal::Kind::Refresh;
        return accept(request);
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) return text_reply(400, "json invalido");
    if (path == "/api/setup") return setup_on ? setup(doc) : text_reply(404, "nada aqui");
    if (path == "/api/settings") return settings(doc);
    if (path == "/api/wifi") return wifi(doc);
    if (path == "/api/token") return token(doc);
    if (path == "/api/lists") return lists_edit(doc);
    if (path == "/api/weather") return weather(doc);
    if (path == "/api/dashboard") return dashboard_edit(doc);
    if (path == "/api/widget_settings") return widget_settings_edit(doc);
    if (path == "/api/calendar/add_account") {
        web_portal::Request request = {};
        request.kind = web_portal::Kind::CalendarAdd;
        return accept(request);
    }
    if (path == "/api/calendar/selection") {
        web_portal::Request request = {};
        request.kind = web_portal::Kind::CalendarSelection;
        const char *sel = doc["selection"] | "";
        text::copy(request.calendar_selection, sizeof(request.calendar_selection), sel);
        return accept(request);
    }
    if (path == "/api/agent/arm") {
        if (doc["off"] | false) {
            agent_push::disarm();
            return text_reply(200, "off");
        }
        agent_push::arm();
        return text_reply(200, "armado");
    }
    return text_reply(404, "nada aqui");
}

void send(WiFiClient &client, const Reply &reply) {
    size_t length = reply.flash ? strlen_P(reply.flash) : reply.body.length();
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  reply.code, reply.code < 300 ? "OK" : "Erro", reply.type, static_cast<unsigned>(length));
    if (reply.flash) client.write(reinterpret_cast<const uint8_t *>(reply.flash), length);
    else client.print(reply.body);
    client.flush();
}

bool send_sd_file(WiFiClient &client, const char *sd_path, const char *content_type, const char *download_name) {
    size_t total = 0;
    if (!sd_host::open_read(sd_path, &total)) {
        send(client, text_reply(404, "pacote do Agente ausente no SD"));
        return true;
    }
    client.printf("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                  "Content-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  content_type, static_cast<unsigned>(total), download_name);
    uint8_t buf[2048];
    size_t sent = 0;
    while (sent < total) {
        size_t n = sd_host::read(buf, sizeof(buf));
        if (!n) break;
        client.write(buf, n);
        sent += n;
    }
    sd_host::close_read();
    client.flush();
    return true;
}

void read_agent_pack_version(char *dest, size_t cap) {
    if (!dest || cap == 0) return;
    dest[0] = '\0';
    size_t n = 0;
    uint8_t *raw = sd_host::read_file("/tessera/agent.version", &n);
    if (!raw || n == 0) {
        sd_host::free_buf(raw);
        return;
    }
    size_t len = n < cap - 1 ? n : cap - 1;
    while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r' || raw[len - 1] == ' ')) --len;
    std::memcpy(dest, raw, len);
    dest[len] = '\0';
    sd_host::free_buf(raw);
}

void refresh_agent_pack_flag() {
    xSemaphoreTake(guard, portMAX_DELAY);
    snapshot.agent_pack = sd_host::ready() && sd_host::exists("/tessera/agent.tgz");
    read_agent_pack_version(snapshot.agent_pack_version, sizeof(snapshot.agent_pack_version));
    xSemaphoreGive(guard);
}

bool write_agent_pack_version(const char *version) {
    if (!version || !version[0]) return false;
    char clean[24] = {};
    size_t n = 0;
    for (const char *p = version; *p && n + 1 < sizeof(clean); ++p) {
        const char c = *p;
        if (c == '\r' || c == '\n') break;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' ||
            c == '_') {
            clean[n++] = c;
        }
    }
    if (!n) return false;
    clean[n++] = '\n';
    return sd_host::write_file("/tessera/agent.version", reinterpret_cast<const uint8_t *>(clean), n);
}

bool send_install_command(WiFiClient &client) {
    if (!sd_host::ready() || !sd_host::exists("/tessera/agent.tgz")) {
        send(client, text_reply(404, "pacote do Agente ausente no SD"));
        return true;
    }
    char panel[72];
    xSemaphoreTake(guard, portMAX_DELAY);
    if (snapshot.info.ip[0]) {
        snprintf(panel, sizeof(panel), "http://%s", snapshot.info.ip);
    } else if (snapshot.info.host[0]) {
        if (strncmp(snapshot.info.host, "http://", 7) == 0 ||
            strncmp(snapshot.info.host, "https://", 8) == 0) {
            snprintf(panel, sizeof(panel), "%s", snapshot.info.host);
        } else {
            snprintf(panel, sizeof(panel), "http://%s", snapshot.info.host);
        }
    } else {
        snprintf(panel, sizeof(panel), "http://tessara.local");
    }
    xSemaphoreGive(guard);

    String body;
    body.reserve(3200);
    body += "#!/bin/bash\n";
    body += "# Tessara Agent — gerado pelo painel\n";
    body += "# Uso: curl -fsSL http://painel/agent/install.command | bash\n";
    body += "#  ou: bash install-tessera-agent.command   (não use duplo clique: Downloads sem +x)\n";
    body += "set -euo pipefail\n";
    body += "PANEL=\"${1:-";
    body += panel;
    body += "}\"\n";
    // Com curl|bash, $0/`BASH_SOURCE` não apontam para um arquivo — só use HERE se for path.
    body += "SRC=\"${BASH_SOURCE[0]:-$0}\"\n";
    body += "HERE=\"\"\n";
    body += "case \"$SRC\" in\n";
    body += "  */*) HERE=\"$(CDPATH= cd -- \"$(dirname \"$SRC\")\" && pwd)\" ;;\n";
    body += "esac\n";
    body += "INSTALL_ROOT=\"$HOME/.tessera/agent\"\n";
    body += "LABEL=com.tessera.agent\n";
    body += "OLD_LABEL=com.tessera.aitokens-bridge\n";
    body += "PLIST=\"$HOME/Library/LaunchAgents/$LABEL.plist\"\n";
    body += "LOG_DIR=\"$HOME/.tessera\"\n";
    body += "LOG=\"$LOG_DIR/agent.log\"\n";
    body += "mkdir -p \"$INSTALL_ROOT\" \"$LOG_DIR\" \"$HOME/Library/LaunchAgents\"\n";
    body += "if [ -n \"$HERE\" ] && [ -f \"$HERE/agent.tgz\" ]; then TGZ=\"$HERE/agent.tgz\"; else\n";
    body += "  echo \"baixando agent.tgz de $PANEL …\"\n";
    body += "  curl -fsSL \"$PANEL/agent/agent.tgz\" -o /tmp/tessera-agent.tgz\n";
    body += "  TGZ=/tmp/tessera-agent.tgz\nfi\n";
    body += "find \"$INSTALL_ROOT\" -mindepth 1 -maxdepth 1 ! -name '.venv' -exec rm -rf {} +\n";
    body += "tar -C \"$INSTALL_ROOT\" -xzf \"$TGZ\"\n";
    body += "chmod +x \"$INSTALL_ROOT/scripts/statusline_claude.sh\" 2>/dev/null || true\n";
    body += "PY=\"$INSTALL_ROOT/.venv/bin/python\"\n";
    body += "if [ ! -x \"$PY\" ]; then\n";
    body += "  python3 -m venv \"$INSTALL_ROOT/.venv\"\nfi\n";
    body += "echo \"deps…\"\n";
    body += "\"$INSTALL_ROOT/.venv/bin/pip\" install -q -r \"$INSTALL_ROOT/requirements.txt\"\n";
    body += "STATUSLINE=\"$INSTALL_ROOT/scripts/statusline_claude.sh\"\n";
    body += "CLAUDE_SETTINGS=\"$HOME/.claude/settings.json\"\n";
    body += "if [ -x \"$STATUSLINE\" ]; then\n";
    body += "  mkdir -p \"$HOME/.claude\"\n";
    body += "  if [ ! -f \"$CLAUDE_SETTINGS\" ]; then\n";
    body += "    printf '{\\n  \"statusLine\": { \"type\": \"command\", \"command\": \"%s\" }\\n}\\n' \"$STATUSLINE\" > \"$CLAUDE_SETTINGS\"\n";
    body += "  else\n";
    body += "    python3 - \"$CLAUDE_SETTINGS\" \"$STATUSLINE\" <<'PY'\n";
    body += "import json, sys\nfrom pathlib import Path\n";
    body += "path, cmd = Path(sys.argv[1]), sys.argv[2]\n";
    body += "data = json.loads(path.read_text(encoding=\"utf-8\")) if path.is_file() else {}\n";
    body += "data[\"statusLine\"] = {\"type\": \"command\", \"command\": cmd}\n";
    body += "path.write_text(json.dumps(data, indent=2) + \"\\n\", encoding=\"utf-8\")\n";
    body += "PY\n  fi\nfi\n";
    body += "UID_NUM=\"$(id -u)\"\n";
    body += "launchctl bootout \"gui/$UID_NUM/$OLD_LABEL\" 2>/dev/null || true\n";
    body += "launchctl bootout \"gui/$UID_NUM/$LABEL\" 2>/dev/null || true\n";
    body += "rm -f \"$HOME/Library/LaunchAgents/$OLD_LABEL.plist\"\n";
    body += "cat > \"$PLIST\" <<PLIST\n";
    body += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    body += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    body += "<plist version=\"1.0\">\n<dict>\n";
    body += "  <key>Label</key><string>$LABEL</string>\n";
    body += "  <key>ProgramArguments</key>\n  <array>\n";
    body += "    <string>$PY</string>\n    <string>-m</string>\n    <string>agent</string>\n  </array>\n";
    body += "  <key>WorkingDirectory</key><string>$INSTALL_ROOT</string>\n";
    body += "  <key>RunAtLoad</key><true/>\n  <key>KeepAlive</key><true/>\n";
    body += "  <key>ThrottleInterval</key><integer>5</integer>\n";
    body += "  <key>StandardOutPath</key><string>$LOG</string>\n";
    body += "  <key>StandardErrorPath</key><string>$LOG</string>\n";
    body += "  <key>EnvironmentVariables</key>\n  <dict>\n";
    body += "    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>\n";
    body += "    <key>HOME</key><string>$HOME</string>\n";
    body += "    <key>PYTHONPATH</key><string>$INSTALL_ROOT/src</string>\n";
    body += "  </dict>\n</dict>\n</plist>\nPLIST\n";
    body += "launchctl bootstrap \"gui/$UID_NUM\" \"$PLIST\"\n";
    body += "launchctl enable \"gui/$UID_NUM/$LABEL\" 2>/dev/null || true\n";
    body += "launchctl kickstart -k \"gui/$UID_NUM/$LABEL\" 2>/dev/null || true\n";
    body += "echo \"Tessara Agent instalado → $INSTALL_ROOT\"\n";
    body += "echo \"log: $LOG\"\n";

    client.printf("HTTP/1.1 200 OK\r\nContent-Type: application/x-sh\r\nContent-Length: %u\r\n"
                  "Content-Disposition: attachment; filename=\"install-tessera-agent.command\"\r\n"
                  "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                  static_cast<unsigned>(body.length()));
    client.print(body);
    client.flush();
    return true;
}

// PUT body streamado para o SD (não cabe em kMaxRequest).
bool put_agent_file(Slot &slot, int head_end, int length, const char *sd_path, const char *version) {
    // ensure: tenta montar sem derrubar um mount saudável (remount destrutivo era a falha).
    if (!sd_host::ensure()) {
        send(slot.client, text_reply(503, "storage indisponivel"));
        return true;
    }
    if (length < 1 || static_cast<size_t>(length) > kMaxAgentUpload) {
        send(slot.client, text_reply(413, "arquivo grande demais"));
        return true;
    }
    // Mesmo padrão dos ícones de clima: arquivo em /tessera/… (sem subpasta).
    if (!(sd_host::mkdir_p("/tessera") || sd_host::exists("/tessera"))) {
        send(slot.client, text_reply(500, "nao criou pasta /tessera"));
        return true;
    }
    if (!sd_host::open_write(sd_path)) {
        send(slot.client, text_reply(500, "falha ao abrir SD para escrita"));
        return true;
    }

    size_t written = 0;
    const int body_off = head_end + 4;
    const int have = static_cast<int>(slot.raw.length()) - body_off;
    if (have > 0) {
        size_t n = static_cast<size_t>(have);
        if (n > static_cast<size_t>(length)) n = static_cast<size_t>(length);
        if (sd_host::write(reinterpret_cast<const uint8_t *>(slot.raw.c_str() + body_off), n) != n) {
            sd_host::close_write(0);
            send(slot.client, text_reply(500, "falha ao gravar no SD"));
            return true;
        }
        written = n;
    }

    uint8_t buf[1024];
    uint32_t idle_since = millis();
    while (written < static_cast<size_t>(length)) {
        if (millis() - idle_since > 60000) {
            sd_host::close_write(0);
            send(slot.client, text_reply(408, "timeout no upload"));
            return true;
        }
        size_t want = static_cast<size_t>(length) - written;
        if (want > sizeof(buf)) want = sizeof(buf);
        int avail = slot.client.available();
        if (avail <= 0) {
            if (!slot.client.connected()) {
                sd_host::close_write(0);
                send(slot.client, text_reply(400, "conexao fechou no upload"));
                return true;
            }
            delay(1);
            continue;
        }
        size_t chunk = want;
        if (static_cast<size_t>(avail) < chunk) chunk = static_cast<size_t>(avail);
        size_t got = slot.client.read(buf, chunk);
        if (!got) {
            delay(1);
            continue;
        }
        if (sd_host::write(buf, got) != got) {
            sd_host::close_write(0);
            send(slot.client, text_reply(500, "falha ao gravar no SD"));
            return true;
        }
        written += got;
        idle_since = millis();
    }

    if (!sd_host::close_write(static_cast<size_t>(length))) {
        send(slot.client, text_reply(500, "gravacao incompleta no SD"));
        return true;
    }
    if (version && version[0]) write_agent_pack_version(version);
    else sd_host::remove_file("/tessera/agent.version");  // portal upload without header → force next push
    agent_push::disarm();
    refresh_agent_pack_flag();
    send(slot.client, text_reply(200, "ok"));
    return true;
}

// True once the request is complete and answered (or hopeless), so the slot can be closed.
bool serve(Slot &slot) {
    while (slot.client.available() && slot.raw.length() < kMaxRequest) slot.raw += static_cast<char>(slot.client.read());
    int head_end = slot.raw.indexOf("\r\n\r\n");
    if (head_end < 0) return slot.raw.length() >= kMaxRequest;
    int line_end = slot.raw.indexOf("\r\n");
    int first = slot.raw.indexOf(' ');
    int second = slot.raw.indexOf(' ', first + 1);
    if (first < 0 || second < 0 || second > line_end) return true;
    String method = slot.raw.substring(0, first);
    String path = slot.raw.substring(first + 1, second);
    int query = path.indexOf('?');
    if (query >= 0) path.remove(query);
    String head = slot.raw.substring(0, head_end);
    head.toLowerCase();
    int length = 0;
    int field = head.indexOf("\r\ncontent-length:");
    if (field >= 0) length = head.substring(field + 17).toInt();

    if (method == "PUT" && path == "/agent/agent.tgz") {
        if (!agent_push::armed()) {
            send(slot.client, text_reply(403, "push nao armado"));
            return true;
        }
        char version[24] = {};
        int vf = head.indexOf("\r\nx-tessara-agent-version:");
        if (vf >= 0) {
            String v = head.substring(vf + 26);
            int end = v.indexOf("\r\n");
            if (end >= 0) v = v.substring(0, end);
            v.trim();
            text::copy(version, sizeof(version), v.c_str());
        }
        return put_agent_file(slot, head_end, length, "/tessera/agent.tgz", version);
    }

    if (length < 0 || head_end + 4 + length > static_cast<int>(kMaxRequest)) {
        send(slot.client, text_reply(413, "grande demais"));
        return true;
    }
    if (static_cast<int>(slot.raw.length()) < head_end + 4 + length) return false;
    if (method == "GET" && path == "/agent/install.command") {
        return send_install_command(slot.client);
    }
    if (method == "GET" && path == "/agent/agent.tgz") {
        return send_sd_file(slot.client, "/tessera/agent.tgz", "application/gzip", "agent.tgz");
    }
    String body = slot.raw.substring(head_end + 4, head_end + 4 + length);
    Reply reply = route(method, path, body.c_str());
    send(slot.client, reply);
    return true;
}

void release(Slot &slot) {
    slot.client.stop();
    slot.raw = String();
    slot.used = false;
}

void take_clients() {
    for (;;) {
        WiFiClient client = server.accept();
        if (!client) return;
        Slot *free_slot = nullptr;
        for (Slot &slot : slots) {
            if (!slot.used) {
                free_slot = &slot;
                break;
            }
        }
        if (!free_slot) {
            free_slot = &slots[0];
            for (Slot &slot : slots) {
                if (slot.since < free_slot->since) free_slot = &slot;
            }
            release(*free_slot);
        }
        free_slot->client = client;
        free_slot->client.setNoDelay(true);
        free_slot->raw.reserve(512);
        free_slot->since = millis();
        free_slot->used = true;
    }
}

void collect_scan() {
    if (!scanning) return;
    int found = WiFi.scanComplete();
    if (found == -1) return;
    scanning = false;
    if (found < 0) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    net_count = 0;
    for (int i = 0; i < found && net_count < kMaxNets; ++i) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        int rssi = WiFi.RSSI(i);
        int slot = -1;
        for (int n = 0; n < net_count; ++n) {
            if (!strcmp(nets[n].ssid, name.c_str())) slot = n;
        }
        if (slot >= 0) {
            if (rssi > nets[slot].rssi) nets[slot].rssi = rssi;
            continue;
        }
        text::copy(nets[net_count].ssid, sizeof(nets[net_count].ssid), name.c_str());
        nets[net_count].rssi = rssi;
        ++net_count;
    }
    xSemaphoreGive(guard);
    WiFi.scanDelete();
    logger::printf("setup redes %d\n", net_count);
}

// A scan while the access point is up makes the ESP32 stop announcing it.
// Scan first, then open the network and leave it alone.
void raise_ap() {
    if (wifi_phase != 4) return;
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP);
    bool up = WiFi.softAP(web_portal::setup_ap(), nullptr, 6);
    if (!up && wifi_phase == 4) {
        delay(200);
        up = WiFi.softAP(web_portal::setup_ap(), nullptr, 6);
    }
    if (wifi_phase != 4) {
        WiFi.softAPdisconnect(true);
        return;
    }
    if (up) dns.start();
    setup_on = up;
    serve_want = 1;
    wifi_phase = 2;
    logger::printf("setup ap %s %s\n", up ? "ok" : "falhou", web_portal::setup_url());
}

void run(void *) {
    bool listening = false;
    int mode = 0;
    bool mdns_on = false;
    uint32_t mdns_refresh_at = 0;
    bool sta_was_up = false;
    for (;;) {
        if (wifi_phase == 1) {
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            xSemaphoreTake(guard, portMAX_DELAY);
            net_count = 0;
            xSemaphoreGive(guard);
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
            scanning = true;
            wifi_phase = 4;
        }
        if (wifi_phase == 3) {
            setup_on = false;
            scanning = false;
            dns.stop();
            WiFi.scanDelete();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            if (mdns_on) {
                MDNS.end();
                mdns_on = false;
            }
            serve_want = 2;
            wifi_phase = 0;
            sta_was_up = false;
        }
        collect_scan();
        if (wifi_phase == 4 && !scanning) raise_ap();
        const bool sta_up = WiFi.status() == WL_CONNECTED;
        if (sta_was_up && !sta_up) {
            // Drop stale mDNS / HTTP so a reconnect re-advertises .local.
            if (mdns_on) {
                MDNS.end();
                mdns_on = false;
            }
            if (listening && mode == 2) {
                for (Slot &slot : slots) {
                    if (slot.used) release(slot);
                }
                server.end();
                listening = false;
                mode = 0;
            }
        }
        sta_was_up = sta_up;
        int want = serve_want;
        if (listening && mode != want) {
            for (Slot &slot : slots) {
                if (slot.used) release(slot);
            }
            server.end();
            listening = false;
            mode = 0;
        }
        if (!listening && want == 1) {
            server.begin();
            listening = true;
            mode = 1;
        }
        if (!listening && want == 2 && sta_up) {
            if (!mdns_on) {
                if (MDNS.begin(hostname)) {
                    MDNS.addService("http", "tcp", 80);
                    logger::printf("web http://%s (%s)\n", host_local, WiFi.localIP().toString().c_str());
                    mdns_on = true;
                    mdns_refresh_at = millis();
                } else {
                    logger::printf("mdns falhou — use http://%s/\n", WiFi.localIP().toString().c_str());
                }
            }
            server.begin();
            listening = true;
            mode = 2;
        }
        // Re-advertise .local periodically — macOS/Tailscale DNS often drops the record.
        if (mdns_on && sta_up && millis() - mdns_refresh_at > 60000u) {
            mdns_refresh_at = millis();
            MDNS.end();
            if (MDNS.begin(hostname)) {
                MDNS.addService("http", "tcp", 80);
            } else {
                mdns_on = false;
            }
        }
        bool active = false;
        if (listening) {
            take_clients();
            for (Slot &slot : slots) {
                if (!slot.used) continue;
                active = true;
                if (serve(slot) || !slot.client.connected() || millis() - slot.since > kIdleMs) release(slot);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(!listening ? 40 : active ? 2 : 10));
    }
}
}  // namespace

bool web_portal::begin(const char *name) {
    if (task) return true;
    text::copy(hostname, sizeof(hostname), name);
    snprintf(host_local, sizeof(host_local), "%s.local", hostname);
    requests = xQueueCreate(4, sizeof(Request));
    guard = xSemaphoreCreateMutex();
    if (!requests || !guard) return false;
    // Stack in PSRAM — keep internal DRAM free for Todoist TLS handshakes.
    constexpr size_t kStackBytes = 12 * 1024;
    static StaticTask_t tcb;
    static StackType_t *stack = nullptr;
    if (!stack) {
        stack = static_cast<StackType_t *>(heap_caps_malloc(kStackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!stack) return false;
    }
    task = xTaskCreateStaticPinnedToCore(run, "web", kStackBytes, nullptr, 2, stack, &tcb, 0);
    return task != nullptr;
}

const char *web_portal::setup_ap() { return "Tessara"; }

const char *web_portal::setup_url() { return "http://192.168.4.1/"; }

void web_portal::start_setup() {
    if (wifi_phase == 1 || wifi_phase == 2 || wifi_phase == 4) return;
    wifi_phase = 1;
}

void web_portal::stop_setup() {
    if (wifi_phase == 0) return;
    wifi_phase = 3;
    uint32_t start = millis();
    while (wifi_phase != 0 && millis() - start < 3000) delay(20);
}

bool web_portal::setup_active() { return wifi_phase == 1 || wifi_phase == 2 || wifi_phase == 4; }

void web_portal::publish(const Snapshot &next) {
    if (!guard) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    snapshot = next;
    xSemaphoreGive(guard);
}

void web_portal::publish_calendar_catalog(const CalendarCatalog &next) {
    if (!guard) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    calendar_catalog = next;
    xSemaphoreGive(guard);
}

bool web_portal::take(Request &request) { return requests && xQueueReceive(requests, &request, 0) == pdTRUE; }

const char *web_portal::host() { return host_local; }
