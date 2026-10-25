#pragma once

#include <stdint.h>

// Parsed MEDIA snapshot for the Mac card and detail screen (Phase 1).
namespace mac {

constexpr uint32_t kStaleMs = 45000;
constexpr int kTitleMax = 41;
constexpr int kArtistMax = 41;
constexpr int kAppMax = 25;

enum class MediaState : uint8_t { Stopped = 0, Playing = 1, Paused = 2 };

struct Media {
    bool ok = false;
    MediaState state = MediaState::Stopped;
    char title[kTitleMax] = {};
    char artist[kArtistMax] = {};
    char app[kAppMax] = {};
};

struct View {
    bool ready = false;
    bool stale = false;
    uint32_t received_ms = 0;
    Media media;
};

// Mutates `view` from a full line starting with "MEDIA". Returns false if ignored.
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

}  // namespace mac
