#pragma once

#include "domain/dashboard_layout.h"

#include <string>
#include <vector>

struct ScenarioTask {
    std::string id;
    std::string content;
    std::string meta;   // due / prazo as shown under the title (from Todoist fill_meta)
    int priority = 1;   // Todoist API: 4 = P1, 3 = P2, 2 = P3, 1 = natural
    bool completed = false;
};

struct ScenarioList {
    std::string name;
    std::string query;  // filter query; defaults to name when empty
    std::vector<ScenarioTask> tasks;
};

struct ScenarioAiSource {
    bool available = true;
    int value = 0;
    std::string display;
    std::string reset;
};

struct Scenario {
    std::string name;
    bool wifi_online = true;
    int wifi_rssi = -50;
    std::string wifi_ssid = "CasaFibra";
    std::string wifi_ip = "192.168.15.42";
    int battery_percent = 80;
    int hour = 14;
    int minute = 30;
    int wday = 5;   // 0=dom … 5=sex (tm_wday)
    int day = 24;
    int month = 9;  // 0–11 (tm_mon); 9 = outubro
    bool time_ok = true;

    bool todoist_pending = false;
    int todoist_total = 3;
    std::vector<ScenarioTask> tasks;
    std::vector<ScenarioList> lists;  // multi-list → chips + slide no todoist_panel

    bool weather_placed = true;
    bool weather_ready = true;
    bool weather_stale = false;
    bool weather_failed = false;
    int weather_temp_c = 24;
    int weather_code = 0;
    std::string weather_place = "São Paulo";
    int weather_hi_c = 28;
    int weather_lo_c = 18;

    bool aitokens_ready = true;
    bool aitokens_stale = false;
    // Codex: remaining 5h · week (same shape as agent/codex.py).
    int aitokens_codex_5h = 100;
    int aitokens_codex_week = 88;
    int aitokens_claude_pct = 70;
    int aitokens_cursor_a = 3;  // models used %
    int aitokens_cursor_b = 13; // other used %
    std::string aitokens_codex_reset = "5h";
    std::string aitokens_claude_reset = "5h";

    bool agent_ready = true;
    bool agent_ok = true;
    std::string agent_detail;
    bool agent_pack_on_sd = true;

    dashboard::Layout dashboard = dashboard::defaults();
};

// Load JSON fixture. Returns false on I/O or parse errors.
bool load_scenario(const std::string &path, Scenario &out);
