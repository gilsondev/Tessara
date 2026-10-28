#pragma once

#include "media/snapshot.h"

#include <lvgl.h>

// Home tile for Música. The shell only reserves the row; this package draws the card.
namespace media_tile {

bool claims(const char *widget_id);
void add(lv_obj_t *row, lv_event_cb_t on_open);
void set(const media::View &view, bool agent_ok);
void apply_theme();
void detach();

}  // namespace media_tile
