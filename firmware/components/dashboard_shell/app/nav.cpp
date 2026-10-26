#include "app/nav.h"

#include <cstring>

namespace {

void set_detail(nav::State &state, const char *widget_id) {
    state.screen = nav::Screen::Detail;
    state.wants_fetch = false;
    if (!widget_id) {
        state.widget_id[0] = '\0';
        return;
    }
    std::strncpy(state.widget_id, widget_id, sizeof(state.widget_id) - 1);
    state.widget_id[sizeof(state.widget_id) - 1] = '\0';
}

}  // namespace

void nav::open_detail(State &state, const char *widget_id) { set_detail(state, widget_id); }

void nav::open_todoist(State &state) { set_detail(state, "org.tessera.todoist"); }

void nav::open_weather(State &state) { set_detail(state, "org.tessera.weather"); }

void nav::open_aitokens(State &state) { set_detail(state, "org.tessera.aitokens"); }

void nav::open_clock(State &state) { set_detail(state, "org.tessera.clock"); }

void nav::open_settings(State &state) {
    state.screen = Screen::Settings;
    state.wants_fetch = false;
}

void nav::select_tab(State &state, Tab tab) {
    state.tab = tab;
    state.wants_fetch = false;
}

void nav::back(State &state) {
    state.screen = Screen::Home;
    state.wants_fetch = false;
    state.widget_id[0] = '\0';
}
