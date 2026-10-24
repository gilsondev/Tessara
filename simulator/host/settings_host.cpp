#include "platform/settings.h"

#include "domain/dashboard_layout.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
std::string g_dir = "/tmp/tessara-sim-settings";
dashboard::Layout g_layout = dashboard::defaults();
DeviceSettings g_device = {};

std::string dash_path() { return g_dir + "/dash_ids.txt"; }

void ensure_dir() { ::mkdir(g_dir.c_str(), 0755); }
}  // namespace

namespace sim_settings {
void set_dir(const char *path) {
    if (path && path[0]) g_dir = path;
    ensure_dir();
    std::ifstream in(dash_path());
    if (in) {
        std::string csv;
        std::getline(in, csv);
        dashboard::parse(csv.c_str(), g_layout);
    } else {
        g_layout = dashboard::defaults();
    }
}
const dashboard::Layout &layout() { return g_layout; }
}  // namespace sim_settings

namespace settings {

Credentials load_credentials() {
    Credentials c = {};
    return c;
}
void save_wifi(const char *, const char *) {}
void save_token(const char *) {}
void clear_token() {}
void stash() {}
bool unstash() { return false; }
void clear_credentials() {}

DeviceSettings load_device() { return g_device; }
void save_device(const DeviceSettings &value) { g_device = value; }

WeatherPlace load_weather() {
    WeatherPlace p = {};
    return p;
}
void save_weather(const char *, float, float) {}
void clear_weather() {}

dashboard::Layout load_dashboard() { return g_layout; }

void save_dashboard(const dashboard::Layout &layout) {
    g_layout = layout;
    ensure_dir();
    char csv[192];
    dashboard::format(layout, csv, sizeof(csv));
    std::ofstream out(dash_path());
    out << csv;
}

}  // namespace settings
