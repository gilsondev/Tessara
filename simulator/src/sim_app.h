#pragma once

#include "scenario.h"

#include <lvgl.h>

// Host glue: fixtures → real home / settings_panel / todoist_panel / weather_page / aitokens_page / clock_page.
namespace sim_app {

enum class Screen { Home, Todoist, Weather, Aitokens, Clock, Settings };

struct State {
    Scenario scenario;
    Screen screen = Screen::Home;
    bool home_ready = false;
    bool quit_requested = false;
    // Compose smoke (same dashboard::compose the home uses).
    int tile_count = 0;
    bool has_todoist = false;
    bool has_weather = false;
    bool has_aitokens = false;
    bool has_clock_tile = false;
};

void init(State &app, const Scenario &scenario);
void show(State &app, Screen screen);
void destroy(State &app);
void paint_fixture(State &app);

void on_escape(State &app);

Screen parse_screen(const char *name);
const char *screen_name(Screen s);

}  // namespace sim_app
