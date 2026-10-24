#pragma once

#include "aitokens/snapshot.h"
#include "scenario.h"
#include "weather/cache.h"

#include <lvgl.h>

// Host glue over real firmware pages (todoist_panel / weather_page / aitokens_page).
namespace sim_detail {

void load_todoist_store(const Scenario &scenario);
weather::View weather_view(const Scenario &scenario);
aitokens::View aitokens_view(const Scenario &scenario);

void open_todoist(lv_event_cb_t on_back);
void open_weather(const Scenario &scenario, lv_event_cb_t on_back);
void open_aitokens(const Scenario &scenario, lv_event_cb_t on_back);
void open_clock(const Scenario &scenario, lv_event_cb_t on_back);

}  // namespace sim_detail
