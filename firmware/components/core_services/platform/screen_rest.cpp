#include "platform/screen_rest.h"

namespace {

void enter(screen_rest::State &state, screen_rest::Phase phase, uint32_t now_ms) {
    state.phase = phase;
    state.phase_entered_ms = now_ms;
}

}  // namespace

void screen_rest::begin(State &state, uint32_t now_ms, uint8_t brightness) {
    state = {};
    state.last_activity_ms = now_ms;
    state.phase_entered_ms = now_ms;
    state.idle_before_saver_ms = kDefaultIdleBeforeSaverMs;
    state.saver_before_fade_ms = kDefaultSaverBeforeFadeMs;
    state.saved_brightness = brightness ? brightness : 100;
    state.phase = Phase::Awake;
}

void screen_rest::note_activity(State &state, uint32_t now_ms) {
    const bool was_resting = state.phase != Phase::Awake;
    state.last_activity_ms = now_ms;
    if (was_resting) {
        state.wake_touch_pending = true;
        enter(state, Phase::Awake, now_ms);
    }
}

void screen_rest::set_brightness_setting(State &state, uint8_t brightness) {
    state.saved_brightness = brightness ? brightness : 100;
}

void screen_rest::set_idle_minutes(State &state, uint8_t minutes) {
    if (minutes == 0) {
        state.idle_before_saver_ms = 0;
        return;
    }
    state.idle_before_saver_ms = static_cast<uint32_t>(minutes) * 60u * 1000u;
}

void screen_rest::set_clock_minutes(State &state, uint8_t minutes) {
    if (minutes == 0) {
        state.saver_before_fade_ms = 0;
        return;
    }
    state.saver_before_fade_ms = static_cast<uint32_t>(minutes) * 60u * 1000u;
}

bool screen_rest::tick(State &state, uint32_t now_ms, bool rest_allowed) {
    if (!rest_allowed) {
        if (state.phase != Phase::Awake) {
            enter(state, Phase::Awake, now_ms);
            return true;
        }
        return false;
    }
    Phase before = state.phase;
    switch (state.phase) {
        case Phase::Awake:
            if (state.idle_before_saver_ms != 0 && now_ms - state.last_activity_ms >= state.idle_before_saver_ms)
                enter(state, Phase::Saver, now_ms);
            break;
        case Phase::Saver:
            if (state.saver_before_fade_ms != 0 && now_ms - state.phase_entered_ms >= state.saver_before_fade_ms)
                enter(state, Phase::Fading, now_ms);
            break;
        case Phase::Fading:
            if (now_ms - state.phase_entered_ms >= kFadeMs) enter(state, Phase::Off, now_ms);
            break;
        case Phase::Off:
            break;
    }
    return state.phase != before;
}

uint8_t screen_rest::brightness_now(const State &state, uint32_t now_ms) {
    switch (state.phase) {
        case Phase::Awake:
        case Phase::Saver:
            return state.saved_brightness;
        case Phase::Off:
            return 0;
        case Phase::Fading: {
            uint32_t elapsed = now_ms - state.phase_entered_ms;
            if (elapsed >= kFadeMs) return 0;
            uint32_t left = kFadeMs - elapsed;
            return static_cast<uint8_t>((static_cast<uint32_t>(state.saved_brightness) * left) / kFadeMs);
        }
    }
    return state.saved_brightness;
}

void screen_rest::force(State &state, Phase phase, uint32_t now_ms) {
    if (phase == Phase::Awake) {
        state.last_activity_ms = now_ms;
        state.wake_touch_pending = false;
    }
    enter(state, phase, now_ms);
}

bool screen_rest::consume_wake_touch(State &state) {
    if (!state.wake_touch_pending) return false;
    state.wake_touch_pending = false;
    return true;
}
