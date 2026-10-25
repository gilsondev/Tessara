#pragma once

#include "mac/snapshot.h"

#include <lvgl.h>

namespace mac_page {

using CmdFn = void (*)(const char *cmd);  // "PLAYPAUSE" | "NEXT" | "PREV" | "?"

void begin(lv_event_cb_t on_back, CmdFn on_cmd);
void mount(lv_obj_t *parent);
void unmount();
void show();
void apply_theme();
void paint(const mac::View &view, bool agent_ok);
const mac::View &last_view();

}  // namespace mac_page
