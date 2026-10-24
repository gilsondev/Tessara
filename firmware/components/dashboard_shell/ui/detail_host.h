#pragma once

#include <lvgl.h>

// Shell-owned detail slot. Widgets mount into `slot()` — never create their own lv_screen.
namespace detail_host {

void begin();
// Show overlay and return the content parent (children cleared).
lv_obj_t *open();
void close();
bool visible();
lv_obj_t *slot();

}  // namespace detail_host
