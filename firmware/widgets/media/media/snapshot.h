#pragma once

#include <stdint.h>

// Parsed MEDIA snapshot for the Música tile and detail screen (Phase 1).
namespace media {

constexpr uint32_t kStaleMs = 45000;
constexpr int kTitleMax = 41;
constexpr int kArtistMax = 41;
constexpr int kAppMax = 25;
constexpr int kCoverEdge = 200;  // large Now Playing art (spec 2026-10-26)
constexpr int kCoverBytes = kCoverEdge * kCoverEdge * 2;  // RGB565
constexpr int kCoverKeyMax = 20;

enum class MediaState : uint8_t { Stopped = 0, Playing = 1, Paused = 2 };

struct Media {
    bool ok = false;
    MediaState state = MediaState::Stopped;
    char title[kTitleMax] = {};
    char artist[kArtistMax] = {};
    char app[kAppMax] = {};
};

struct Cover {
    bool ready = false;
    char key[kCoverKeyMax] = {};
    uint16_t w = 0;
    uint16_t h = 0;
    uint16_t chunks = 0;
    uint16_t got = 0;
    // SPIRAM buffer (sd_host::alloc_spiram) — never embed in BSS/stack; TLS needs DRAM.
    uint8_t *rgb565 = nullptr;
};

struct View {
    bool ready = false;
    bool stale = false;
    uint32_t received_ms = 0;
    Media media;
    Cover cover;
};

// Mutates `view` from a full line starting with "MEDIA\t" (not MEDIA_COVER).
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Assembles MEDIA_COVER chunks into `view.cover`. True when image complete.
bool apply_cover_line(View &view, const char *line);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

}  // namespace media
