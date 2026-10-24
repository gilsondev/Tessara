#pragma once

#include <stdint.h>

// Which screen is in front. Opening settings never asks for a network fetch.
// Detail screens are identified by widget_id — the shell does not hardcode product screens.
namespace nav {

enum class Screen : uint8_t { Home, Detail, Settings };
enum class Tab : uint8_t { System, Todoist };

struct State {
    Screen screen = Screen::Home;
    Tab tab = Tab::System;
    bool wants_fetch = false;
    char widget_id[48] = {};
};

void open_detail(State &state, const char *widget_id);
// Compat wrappers — all route to Detail + widget id.
void open_todoist(State &state);
void open_weather(State &state);
void open_aitokens(State &state);
void open_clock(State &state);
void open_settings(State &state);
void select_tab(State &state, Tab tab);
void back(State &state);

}  // namespace nav
