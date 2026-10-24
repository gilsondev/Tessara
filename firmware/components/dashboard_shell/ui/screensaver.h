#pragma once

#include <stdint.h>

// Fullscreen NeXtime-style flip clock (centered) while screen_rest is in Saver/Fading.
namespace screensaver {

void begin();
void show();
void hide();
bool visible();
void tick(uint32_t now_ms);
void apply_theme();
// Avança 1 minuto com animação (console REST flip / smoke).
void debug_flip();

}  // namespace screensaver
