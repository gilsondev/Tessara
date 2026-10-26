#pragma once

#include "domain/dashboard_layout.h"
#include "domain/device_settings.h"
#include "domain/model.h"
#include "platform/device_resources.h"

// Settings page served on the home network as http://<host>.local (mDNS). Runs on its own task:
// it reads a published snapshot and hands edits back through take(), never touching the UI directly.
namespace web_portal {

struct ListRow {
    char name[28];
    bool shown;
};

struct AgentInfo {
    bool ready;
    bool ok;
    char detail[48];
};

struct Snapshot {
    DeviceSettings settings;
    DeviceInfo info;
    ListRow lists[kMaxViews];
    int list_count;
    bool token_set;
    char weather_city[48];
    bool weather_ready;
    AgentInfo agent;
    bool agent_pack;  // agentd.tgz no SD (install.command é gerado no GET)
    bool agent_push_armed;
    char agent_pack_version[24];
    dashboard::Layout dashboard;
    bool aitokens_ready;
    bool aitokens_stale;
    uint32_t aitokens_age_s;  // idade da última cotas no publish
    // Calendar (agent bridge) — small flags only; catalog is publish_calendar_catalog().
    bool calendar_ready;
    bool calendar_stale;
    bool calendar_linked;
    bool calendar_auth_pending;
    uint32_t calendar_age_s;
    char calendar_status[48];
    char calendar_selection[512];
    device_resources::Sample resources;
    // Clock / date widget prefs (copied on publish — web task must not hit NVS).
    char clock_timezone[24];
    char clock_time_format[12];
    char date_format[16];
    // Calendar home tile height: S | M | L | XL
    char calendar_home_tile_size[8];
};

enum class Kind : uint8_t {
    Settings,
    Wifi,
    Token,
    Refresh,
    Setup,
    Lists,
    WeatherPlace,
    Dashboard,
    WidgetSettings,
    CalendarAdd,
    CalendarSelection,
};

const char *setup_ap();
const char *setup_url();
// Opens the configuration Wi-Fi. Does nothing if it is already up.
void start_setup();
void stop_setup();
bool setup_active();

struct Request {
    Kind kind;
    DeviceSettings settings;
    char ssid[64];
    char pass[64];
    char token[128];
    ListRow lists[kMaxViews];
    int list_count;
    char weather_name[48];
    char weather_city[48];  // if set, controller geocodes via net_worker
    float weather_lat;
    float weather_lon;
    bool weather_has_coords;
    dashboard::Layout dashboard;
    // Kind::WidgetSettings — namespaced per widget instance_id
    char widget_id[48];
    char setting_key[32];
    char setting_value[64];
    // Kind::CalendarSelection — comma-separated calendar ids
    char calendar_selection[512];
};

// Catalog lives outside Snapshot so publish() stays stack-safe on the UI task.
struct CalRow {
    char account[48];
    char id[96];
    char name[40];
    uint32_t color_rgb;
    bool selected;
};

constexpr int kMaxCalendarRows = 16;

struct CalendarCatalog {
    CalRow rows[kMaxCalendarRows];
    int count;
};

bool begin(const char *host);
void publish(const Snapshot &snapshot);
void publish_calendar_catalog(const CalendarCatalog &catalog);
bool take(Request &request);
const char *host();

}  // namespace web_portal
