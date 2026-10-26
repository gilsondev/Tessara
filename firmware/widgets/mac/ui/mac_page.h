#pragma once

#include "mac/focus_snapshot.h"

#include <lvgl.h>

namespace mac_page {

using CmdFn = void (*)(const char *cmd);  // "SET\t<id>" | "OFF" | "?"

void begin(lv_event_cb_t on_back, CmdFn on_cmd);
void mount(lv_obj_t *parent);
void unmount();
void show();
void apply_theme();
void paint(const focus::View &view, bool agent_ok);
const focus::View &last_view();

}  // namespace mac_page
