#pragma once

#include "aitokens/snapshot.h"

#include <lvgl.h>

namespace aitokens_page {

void begin(lv_event_cb_t on_back, lv_event_cb_t on_refresh = nullptr);
void show();
// Reparent the long-lived page into the shell detail slot (or detach with nullptr).
void attach(lv_obj_t *parent);
void show_refreshing();
void refresh_done();
void apply_theme();
void paint(const aitokens::View &view);
// Last view passed to paint() — detail mount uses this so the controller need not strcmp.
const aitokens::View &last_view();

}  // namespace aitokens_page
