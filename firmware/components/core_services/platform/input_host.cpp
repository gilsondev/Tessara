#include "platform/input_host.h"

#include "platform/display_host.h"

#include <Arduino.h>
#include <lvgl.h>

namespace {
hal::TouchPort *touch = nullptr;
hal::ButtonPort *key = nullptr;
const hal::BoardDescriptor *board = nullptr;

// Press arms on the first solid contact so swipes feel instant; release still needs two quiet samples
// so a single dropped frame does not split one gesture into two. The controller can answer only garbage
// after the finger lifts, so a press without a valid contact for kLostMs ends anyway — otherwise LVGL
// would keep the gesture alive and the page would stop mid-slide.
constexpr uint32_t kLostMs = 60;
constexpr uint8_t kPressStreak = 1;
constexpr uint8_t kReleaseStreak = 2;

struct TouchFilter {
    bool pressed = false;
    uint8_t streak = 0;
    int16_t x = 0;
    int16_t y = 0;
    uint32_t last_contact = 0;
    uint32_t errors = 0;
    uint32_t lost = 0;

    void feed(hal::TouchRead read, int16_t nx, int16_t ny) {
        uint32_t now = millis();
        if (read == hal::TouchRead::Contact) last_contact = now;
        if (read == hal::TouchRead::Error) {
            ++errors;
            if (pressed && now - last_contact > kLostMs) {
                pressed = false;
                streak = 0;
                ++lost;
            }
            return;
        }
        bool contact = read == hal::TouchRead::Contact;
        if (contact == pressed) {
            streak = 0;
            if (contact) {
                x = nx;
                y = ny;
            }
            return;
        }
        if (contact) {
            x = nx;
            y = ny;
        }
        const uint8_t need = contact ? kPressStreak : kReleaseStreak;
        if (++streak >= need) {
            pressed = contact;
            streak = 0;
        }
    }
} filter;

struct Drag {
    bool active;
    int x0, y0, x1, y1;
    uint32_t start;
    uint32_t duration;
} drag;

struct Key {
    bool down;
    uint32_t since;
    uint32_t ready_at;
    bool tapped;
} key_state;

bool resting = false;
bool swallowing = false;
bool was_pressed = false;
void (*wake_handler)() = nullptr;
void (*activity_handler)() = nullptr;

void logical_to_native(int lx, int ly, int16_t &x, int16_t &y) {
    switch (display_host::rotation()) {
        case hal::Rotation::R270:
            x = static_cast<int16_t>(board->width - 1 - ly);
            y = static_cast<int16_t>(lx);
            break;
        case hal::Rotation::R90:
            x = static_cast<int16_t>(ly);
            y = static_cast<int16_t>(board->height - 1 - lx);
            break;
        case hal::Rotation::R180:
            x = static_cast<int16_t>(board->width - 1 - lx);
            y = static_cast<int16_t>(board->height - 1 - ly);
            break;
        default:
            x = static_cast<int16_t>(lx);
            y = static_cast<int16_t>(ly);
    }
}

bool scripted(int16_t &x, int16_t &y, bool &pressed) {
    if (!drag.active) return false;
    uint32_t elapsed = millis() - drag.start;
    if (elapsed > drag.duration) {
        drag.active = false;
        pressed = false;
        return true;
    }
    int lx = drag.x0 + static_cast<int>((drag.x1 - drag.x0) * static_cast<int64_t>(elapsed) / drag.duration);
    int ly = drag.y0 + static_cast<int>((drag.y1 - drag.y0) * static_cast<int64_t>(elapsed) / drag.duration);
    logical_to_native(lx, ly, x, y);
    pressed = true;
    return true;
}

void read_pointer(lv_indev_t *, lv_indev_data_t *data) {
    int16_t x = 0;
    int16_t y = 0;
    bool pressed = false;
    if (!scripted(x, y, pressed) && touch) {
        int16_t nx = 0;
        int16_t ny = 0;
        hal::TouchRead read = touch->read(nx, ny);
        filter.feed(read, nx, ny);
        pressed = filter.pressed;
        x = filter.x;
        y = filter.y;
    }
    if (pressed && !was_pressed) {
        if (resting) {
            swallowing = true;
            if (wake_handler) wake_handler();
        } else if (activity_handler) {
            activity_handler();
        }
    }
    was_pressed = pressed;
    if (swallowing) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_RELEASED;
        if (!pressed) swallowing = false;
        return;
    }
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void poll_key(lv_timer_t *) {
    if (!key) return;
    bool down = key->down();
    uint32_t now = millis();
    if (now < key_state.ready_at) {
        key_state.down = down;
        return;
    }
    if (down && !key_state.down) key_state.since = now;
    if (!down && key_state.down) {
        uint32_t held = now - key_state.since;
        if (held >= 40 && held <= 800) key_state.tapped = true;
    }
    key_state.down = down;
}
}  // namespace

void input_host::begin(hal::TouchPort *touch_port, hal::ButtonPort *button, const hal::BoardDescriptor &descriptor) {
    touch = touch_port;
    key = button;
    board = &descriptor;
    if (touch && !touch->begin()) touch = nullptr;
    if (key && !key->begin()) key = nullptr;

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_indev_set_scroll_limit(pointer, 14);
    lv_timer_set_period(lv_indev_get_read_timer(pointer), 8);

    key_state.ready_at = millis() + 3000;
    key_state.down = key && key->down();
    lv_timer_create(poll_key, 20, nullptr);
}

void input_host::inject_drag(int x0, int y0, int x1, int y1, uint32_t duration_ms) {
    if (resting) {
        swallowing = true;
        if (wake_handler) wake_handler();
        // Still run the scripted drag as releases-only so LVGL sees no press.
    } else if (activity_handler) {
        activity_handler();
    }
    drag = Drag{true, x0, y0, x1, y1, millis(), duration_ms ? duration_ms : 1};
}

void input_host::set_resting(bool on) {
    resting = on;
    // Keep swallowing across wake: inject_drag/touch sets it before calling the wake
    // handler, which clears resting — clearing swallow here would leak the press into LVGL.
}

void input_host::set_wake_handler(void (*on_wake)()) { wake_handler = on_wake; }

void input_host::set_activity_handler(void (*on_activity)()) { activity_handler = on_activity; }

void input_host::report(Print &out) {
    out.printf("TOUCH errors=%lu lost=%lu pressed=%d\n", static_cast<unsigned long>(filter.errors),
               static_cast<unsigned long>(filter.lost), filter.pressed ? 1 : 0);
}

bool input_host::take_key_tap() {
    bool tapped = key_state.tapped;
    key_state.tapped = false;
    return tapped;
}
