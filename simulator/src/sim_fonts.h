#pragma once

namespace sim_fonts {

// Load system TTF via LVGL FreeType (accents, ·, etc.). Call after lv_init().
// Returns false if the font file cannot be opened.
bool init();

}  // namespace sim_fonts
