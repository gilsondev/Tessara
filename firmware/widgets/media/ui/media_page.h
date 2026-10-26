#pragma once

#include "media/snapshot.h"

#include <lvgl.h>

namespace media_page {

using CmdFn = void (*)(const char *cmd);  // "?" refresh only (read-only page)

void begin(lv_event_cb_t on_back, CmdFn on_cmd);
void mount(lv_obj_t *parent);
void unmount();
void show();
void apply_theme();
void paint(const media::View &view, bool agent_ok);
const media::View &last_view();

}  // namespace media_page
