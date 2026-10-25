#include "sim_app.h"

#include "agent/state.h"
#include "domain/dashboard_layout.h"
#include "domain/device_settings.h"
#include "platform/settings.h"
#include "sim_detail.h"
#include "ui/aitokens_page.h"
#include "ui/clock_page.h"
#include "ui/home.h"
#include "ui/palette.h"
#include "ui/settings_panel.h"
#include "ui/todoist_panel.h"
#include "ui/weather_page.h"

#include <cstdio>
#include <cstring>

namespace {

sim_app::State *g_app = nullptr;

DeviceInfo device_info_from(const Scenario &s) {
    DeviceInfo info = {};
    std::snprintf(info.host, sizeof(info.host), "tessara.local");
    if (s.wifi_online && !s.wifi_ip.empty()) {
        std::snprintf(info.ip, sizeof(info.ip), "%s", s.wifi_ip.c_str());
    }
    if (!s.wifi_ssid.empty()) {
        std::snprintf(info.ssid, sizeof(info.ssid), "%s", s.wifi_ssid.c_str());
    }
    info.online = s.wifi_online;
    info.rssi = static_cast<int8_t>(s.wifi_rssi);
    return info;
}

agent::View agent_view_from(const Scenario &s) {
    agent::View v = {};
    v.ready = s.agent_ready;
    v.ok = s.agent_ok;
    if (!s.agent_detail.empty()) {
        std::snprintf(v.detail, sizeof(v.detail), "%s", s.agent_detail.c_str());
    }
    return v;
}

void apply_theme_from(const DeviceSettings &settings) {
    bool dark = effective_dark(settings, -1);
    palette::set_mode(dark ? palette::Mode::Dark : palette::Mode::Light);
    home::apply_theme();
    todoist_panel::apply_theme();
    weather_page::apply_theme();
    aitokens_page::apply_theme();
    clock_page::apply_theme();
    if (settings_panel::visible()) settings_panel::relayout();
}

void open_widget(lv_event_t *e) {
    const char *id = static_cast<const char *>(lv_event_get_user_data(e));
    if (!g_app || !id) return;
    if (!std::strcmp(id, dashboard::id_todoist())) sim_app::show(*g_app, sim_app::Screen::Todoist);
    else if (!std::strcmp(id, dashboard::id_weather())) sim_app::show(*g_app, sim_app::Screen::Weather);
    else if (!std::strcmp(id, dashboard::id_aitokens())) sim_app::show(*g_app, sim_app::Screen::Aitokens);
    else if (!std::strcmp(id, dashboard::id_clock())) sim_app::show(*g_app, sim_app::Screen::Clock);
}

void go_home(lv_event_t *e) {
    (void)e;
    if (g_app) sim_app::show(*g_app, sim_app::Screen::Home);
}

void on_settings_changed(const DeviceSettings &next) {
    if (!g_app) return;
    DeviceSettings prev = settings::load_device();
    bool theme = effective_dark(next) != effective_dark(prev);
    settings::save_device(next);
    if (theme) apply_theme_from(next);
    // Keep connection card / home status in sync with the fixture network.
    DeviceInfo info = device_info_from(g_app->scenario);
    settings_panel::show_values(next, info);
    home::set_link(info.online, info.rssi);
}

void on_settings_dismissed() {
    if (!g_app) return;
    g_app->screen = sim_app::Screen::Home;
    home::show();
}

void on_pedir_cotas() {
    if (!g_app) return;
    // Simulate a fresh agent push: refresh paint on the AI Tokens settings page.
    settings_panel::show_aitokens(sim_detail::aitokens_view(g_app->scenario));
    std::fprintf(stderr, "sim: pedir cotas → snapshot AI Tokens reaplicado\n");
}

void on_settings(lv_event_t *) {
    if (!g_app) return;
    sim_app::show(*g_app, sim_app::Screen::Settings);
}

void fill_compose_flags(sim_app::State &app) {
    const char *registered[] = {
        dashboard::id_greeting(), dashboard::id_date(),    dashboard::id_clock(),
        dashboard::id_todoist(),  dashboard::id_weather(), dashboard::id_aitokens(),
    };
    char slots[dashboard::kMaxSlots][dashboard::kIdLen];
    int n = dashboard::compose(app.scenario.dashboard, registered, 6, slots, dashboard::kMaxSlots);
    app.tile_count = n;
    app.has_todoist = app.has_weather = app.has_aitokens = app.has_clock_tile = false;
    for (int i = 0; i < n; ++i) {
        if (!std::strcmp(slots[i], dashboard::id_todoist())) app.has_todoist = true;
        else if (!std::strcmp(slots[i], dashboard::id_weather())) app.has_weather = true;
        else if (!std::strcmp(slots[i], dashboard::id_aitokens())) app.has_aitokens = true;
        else if (!std::strcmp(slots[i], dashboard::id_clock())) app.has_clock_tile = true;
    }
}

}  // namespace

void sim_app::paint_fixture(State &app) {
    const Scenario &s = app.scenario;
    home::set_link(s.wifi_online, s.wifi_rssi);
    home::set_battery(s.battery_percent);
    home::set_summary(s.todoist_total, s.todoist_pending);
    home::set_weather(sim_detail::weather_view(s));
    home::set_aitokens(sim_detail::aitokens_view(s));
}

void sim_app::init(State &app, const Scenario &scenario) {
    g_app = &app;
    app.scenario = scenario;
    app.quit_requested = false;
    app.screen = Screen::Home;
    fill_compose_flags(app);

    // Seed device prefs once from defaults (or prior --settings dir).
    DeviceSettings device = settings::load_device();
    settings::save_device(device);
    apply_theme_from(device);

    sim_detail::load_todoist_store(app.scenario);

    home::begin(open_widget, on_settings);
    // begin() monta a partir de settings; reforça o layout do fixture.
    home::rebuild(app.scenario.dashboard);
    paint_fixture(app);
    home::show();
    app.home_ready = true;
}

void sim_app::show(State &app, Screen screen) {
    if (settings_panel::visible() && screen != Screen::Settings) {
        settings_panel::close();
    }
    if (screen == Screen::Home) {
        home::show();
        app.screen = Screen::Home;
        return;
    }
    if (screen == Screen::Settings) {
        DeviceSettings device = settings::load_device();
        DeviceInfo info = device_info_from(app.scenario);
        settings_panel::open(on_settings_changed, on_settings_dismissed, on_pedir_cotas);
        settings_panel::show_values(device, info);
        settings_panel::show_aitokens(sim_detail::aitokens_view(app.scenario));
        settings_panel::show_agent(agent_view_from(app.scenario), app.scenario.agent_pack_on_sd);
        app.screen = Screen::Settings;
        return;
    }
    if (screen == Screen::Todoist) {
        sim_detail::open_todoist(go_home);
    } else if (screen == Screen::Weather) {
        sim_detail::open_weather(app.scenario, go_home);
    } else if (screen == Screen::Aitokens) {
        sim_detail::open_aitokens(app.scenario, go_home);
    } else if (screen == Screen::Clock) {
        sim_detail::open_clock(app.scenario, go_home);
    }
    app.screen = screen;
}

void sim_app::destroy(State &app) {
    (void)app;
    if (settings_panel::visible()) settings_panel::close();
    if (g_app == &app) g_app = nullptr;
}

void sim_app::on_escape(State &app) {
    if (settings_panel::visible()) {
        settings_panel::close();
        app.screen = Screen::Home;
        home::show();
        return;
    }
    if (app.screen != Screen::Home) {
        show(app, Screen::Home);
        return;
    }
    app.quit_requested = true;
}

sim_app::Screen sim_app::parse_screen(const char *name) {
    if (!name) return Screen::Home;
    if (!std::strcmp(name, "todoist") || !std::strcmp(name, "tarefas")) return Screen::Todoist;
    if (!std::strcmp(name, "weather") || !std::strcmp(name, "clima")) return Screen::Weather;
    if (!std::strcmp(name, "aitokens") || !std::strcmp(name, "ai")) return Screen::Aitokens;
    if (!std::strcmp(name, "clock") || !std::strcmp(name, "relogio")) return Screen::Clock;
    if (!std::strcmp(name, "settings") || !std::strcmp(name, "ajustes") || !std::strcmp(name, "config"))
        return Screen::Settings;
    return Screen::Home;
}

const char *sim_app::screen_name(Screen s) {
    switch (s) {
        case Screen::Todoist:
            return "todoist";
        case Screen::Weather:
            return "weather";
        case Screen::Aitokens:
            return "aitokens";
        case Screen::Clock:
            return "clock";
        case Screen::Settings:
            return "settings";
        case Screen::Home:
        default:
            return "home";
    }
}
