#pragma once

#include <stddef.h>
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

// What the panel publishes on TopicId::Media. The producer keeps two of these
// and alternates them, so a callback can still read the previous one.
struct Published {
    View view;
    bool agent_ok = false;
};

// Mutates `view` from a full line starting with "MEDIA\t" (not MEDIA_COVER).
bool apply_line(View &view, const char *line, uint32_t now_ms);

// IEEE CRC32 of one cover chunk. The agent puts the same value on the line.
uint32_t cover_crc32(const uint8_t *data, size_t len);

// Writes MEDIA_COVER chunks into a back buffer. Leaves `view.cover` alone
// until every chunk of one key has arrived, then swaps that buffer in front
// and returns true. A new key mid-download discards the back buffer.
// A chunk whose CRC does not match is ignored.
bool apply_cover_line(View &view, const char *line, uint32_t now_ms = 0);

// After 2 s with holes, writes `MEDIA_COVER?\t<key>\t<idx,idx,…>`.
// After 10 s, drops the back buffer and keeps the cover on screen.
bool cover_poll(uint32_t now_ms, char *request, size_t request_cap);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

}  // namespace media
