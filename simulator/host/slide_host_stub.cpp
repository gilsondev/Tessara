#include "platform/display_host.h"

// Host stub: SDL owns the window, so there is no framebuffer to compose into. Refusing the fast
// path makes bitmaps::freeze() fall back to the LVGL images, which the simulator renders fine.
namespace display_host {

bool slide_begin(const SlideSpec &) { return false; }
void slide_frame(int32_t) {}
void slide_end() {}

}  // namespace display_host
