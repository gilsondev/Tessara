#include "scenario.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>

bool load_scenario(const std::string &path, Scenario &out) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    JsonDocument doc;
    if (deserializeJson(doc, buf.str())) return false;

    out = Scenario{};
    if (doc["name"].is<const char *>()) out.name = doc["name"].as<const char *>();
    out.wifi_online = doc["wifi"]["online"] | out.wifi_online;
    out.wifi_rssi = doc["wifi"]["rssi"] | out.wifi_rssi;
    if (doc["wifi"]["ssid"].is<const char *>()) out.wifi_ssid = doc["wifi"]["ssid"].as<const char *>();
    if (doc["wifi"]["ip"].is<const char *>()) out.wifi_ip = doc["wifi"]["ip"].as<const char *>();
    out.battery_percent = doc["battery_percent"] | out.battery_percent;

    JsonObject clock = doc["clock"].as<JsonObject>();
    if (!clock.isNull()) {
        out.hour = clock["hour"] | out.hour;
        out.minute = clock["minute"] | out.minute;
        out.wday = clock["wday"] | out.wday;
        out.day = clock["day"] | out.day;
        out.month = clock["month"] | out.month;
        out.time_ok = clock["ok"] | out.time_ok;
    }

    auto parse_tasks = [](JsonArray arr, std::vector<ScenarioTask> &dest) {
        for (JsonVariant v : arr) {
            ScenarioTask t;
            if (v.is<const char *>()) {
                t.content = v.as<const char *>();
            } else if (v.is<JsonObject>()) {
                JsonObject o = v.as<JsonObject>();
                if (o["id"].is<const char *>()) t.id = o["id"].as<const char *>();
                else if (o["id"].is<long>()) t.id = std::to_string(o["id"].as<long>());
                if (o["content"].is<const char *>()) t.content = o["content"].as<const char *>();
                if (o["meta"].is<const char *>()) t.meta = o["meta"].as<const char *>();
                t.priority = o["priority"] | t.priority;
                t.completed = o["completed"] | t.completed;
            }
            if (!t.content.empty() && !t.completed) dest.push_back(t);
        }
    };

    JsonObject todoist = doc["todoist"].as<JsonObject>();
    bool total_set = false;
    if (!todoist.isNull()) {
        if (todoist["total"].is<int>()) {
            out.todoist_total = todoist["total"].as<int>();
            total_set = true;
        }
        out.todoist_pending = todoist["pending"] | out.todoist_pending;
        if (todoist["tasks"].is<JsonArray>()) parse_tasks(todoist["tasks"].as<JsonArray>(), out.tasks);
        if (todoist["lists"].is<JsonArray>()) {
            for (JsonVariant v : todoist["lists"].as<JsonArray>()) {
                if (!v.is<JsonObject>()) continue;
                JsonObject o = v.as<JsonObject>();
                ScenarioList list;
                if (o["name"].is<const char *>()) list.name = o["name"].as<const char *>();
                if (o["query"].is<const char *>()) list.query = o["query"].as<const char *>();
                if (list.name.empty()) list.name = "Lista";
                if (o["tasks"].is<JsonArray>()) parse_tasks(o["tasks"].as<JsonArray>(), list.tasks);
                out.lists.push_back(list);
            }
        }
    }
    if (!out.lists.empty() && !total_set) {
        int sum = 0;
        for (const auto &list : out.lists) sum += static_cast<int>(list.tasks.size());
        out.todoist_total = sum;
    }
    if (out.lists.empty() && out.tasks.empty() && out.todoist_total > 0 && !out.todoist_pending) {
        // Minimal stand-in when a scenario only sets a home total (no detail lists).
        const char *defaults[] = {"Revisar PRs do painel", "Comprar café", "Atualizar Tessera docs"};
        const char *metas[] = {"hoje", "amanhã", "26 out"};
        const int prios[] = {4, 1, 2};
        int n = out.todoist_total < 3 ? out.todoist_total : 3;
        for (int i = 0; i < n; ++i) {
            ScenarioTask t;
            t.id = "t" + std::to_string(i + 1);
            t.content = defaults[i];
            t.meta = metas[i];
            t.priority = prios[i];
            out.tasks.push_back(t);
        }
    }

    JsonObject weather = doc["weather"].as<JsonObject>();
    if (!weather.isNull()) {
        out.weather_placed = weather["placed"] | out.weather_placed;
        out.weather_ready = weather["ready"] | out.weather_ready;
        out.weather_stale = weather["stale"] | out.weather_stale;
        out.weather_failed = weather["failed"] | out.weather_failed;
        out.weather_temp_c = weather["temp_c"] | out.weather_temp_c;
        out.weather_code = weather["code"] | out.weather_code;
        out.weather_hi_c = weather["hi_c"] | out.weather_hi_c;
        out.weather_lo_c = weather["lo_c"] | out.weather_lo_c;
        if (weather["place"].is<const char *>()) out.weather_place = weather["place"].as<const char *>();
    }

    JsonObject ai = doc["aitokens"].as<JsonObject>();
    if (!ai.isNull()) {
        out.aitokens_ready = ai["ready"] | out.aitokens_ready;
        out.aitokens_stale = ai["stale"] | out.aitokens_stale;
        // Prefer dual Codex fields; fall back to legacy codex_pct for both.
        int legacy = ai["codex_pct"] | out.aitokens_codex_week;
        out.aitokens_codex_5h = ai["codex_5h"] | legacy;
        out.aitokens_codex_week = ai["codex_week"] | legacy;
        out.aitokens_claude_pct = ai["claude_pct"] | out.aitokens_claude_pct;
        out.aitokens_cursor_a = ai["cursor_a"] | out.aitokens_cursor_a;
        out.aitokens_cursor_b = ai["cursor_b"] | out.aitokens_cursor_b;
        if (ai["codex_reset"].is<const char *>()) out.aitokens_codex_reset = ai["codex_reset"].as<const char *>();
        if (ai["claude_reset"].is<const char *>()) out.aitokens_claude_reset = ai["claude_reset"].as<const char *>();
    }

    JsonObject agent = doc["agent"].as<JsonObject>();
    if (!agent.isNull()) {
        out.agent_ready = agent["ready"] | out.agent_ready;
        out.agent_ok = agent["ok"] | out.agent_ok;
        out.agent_pack_on_sd = agent["pack_on_sd"] | out.agent_pack_on_sd;
        if (agent["detail"].is<const char *>()) out.agent_detail = agent["detail"].as<const char *>();
    }

    if (doc["dashboard"].is<const char *>()) {
        dashboard::parse(doc["dashboard"].as<const char *>(), out.dashboard);
    } else if (doc["dashboard"].is<JsonArray>()) {
        out.dashboard = {};
        for (JsonVariant v : doc["dashboard"].as<JsonArray>()) {
            const char *id = v.as<const char *>();
            if (id) dashboard::set_enabled(out.dashboard, id, true);
        }
        if (out.dashboard.count == 0) out.dashboard = dashboard::defaults();
    }
    return true;
}
