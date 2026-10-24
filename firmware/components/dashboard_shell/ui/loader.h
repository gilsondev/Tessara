#pragma once

// Full-screen modal for long operations (boot, refresh, firmware update). Swallows touches while visible.
namespace loader {

void show(const char *title, const char *detail);
// progress in [0, 100]; negative hides the bar.
void set_progress(int percent);
void hide();
bool visible();
// Rebuilds for the current screen size if visible.
void relayout();

}  // namespace loader
