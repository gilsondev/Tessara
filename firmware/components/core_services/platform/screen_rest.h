#pragma once

#include <stdint.h>

// Idle → flip-clock saver → brightness fade → backlight off. Pure phase machine (no LVGL).
namespace screen_rest {

enum class Phase : uint8_t { Awake, Saver, Fading, Off };

constexpr uint32_t kDefaultIdleBeforeSaverMs = 5u * 60u * 1000u;
constexpr uint32_t kDefaultSaverBeforeFadeMs = 60u * 1000u;
constexpr uint32_t kSaverBeforeFadeMs = kDefaultSaverBeforeFadeMs;  // legacy alias for tests
constexpr uint32_t kFadeMs = 10u * 1000u;
// Legacy alias used by older tests/docs.
constexpr uint32_t kIdleBeforeSaverMs = kDefaultIdleBeforeSaverMs;

struct State {
    Phase phase = Phase::Awake;
    uint32_t last_activity_ms = 0;
    uint32_t phase_entered_ms = 0;
    uint32_t idle_before_saver_ms = kDefaultIdleBeforeSaverMs;  // 0 = never
    uint32_t saver_before_fade_ms = kDefaultSaverBeforeFadeMs;  // 0 = until touch
    uint8_t saved_brightness = 100;
    bool wake_touch_pending = false;  // first contact after rest must be swallowed
};

void begin(State &state, uint32_t now_ms, uint8_t brightness);
void note_activity(State &state, uint32_t now_ms);
void set_brightness_setting(State &state, uint8_t brightness);
// 0 = never enter saver from idle; otherwise minutes until saver.
void set_idle_minutes(State &state, uint8_t minutes);
// How long the flip clock stays on: 0 = until touch; otherwise minutes before fade.
void set_clock_minutes(State &state, uint8_t minutes);
// Advances phases. Returns true if phase changed.
bool tick(State &state, uint32_t now_ms, bool rest_allowed);
// Brightness to apply now (0–100). During Fading, interpolates down.
uint8_t brightness_now(const State &state, uint32_t now_ms);
// Force phase (console REST).
void force(State &state, Phase phase, uint32_t now_ms);
bool consume_wake_touch(State &state);

}  // namespace screen_rest
