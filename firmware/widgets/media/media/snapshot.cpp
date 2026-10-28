#include "media/snapshot.h"

#include "platform/sd_host.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

bool next_field(char **cursor, char *out, size_t out_n) {
    if (!cursor || !*cursor || !**cursor) {
        if (out && out_n) out[0] = '\0';
        return false;
    }
    char *start = *cursor;
    char *end = start;
    while (*end && *end != '\t' && *end != '\r' && *end != '\n') ++end;
    size_t n = static_cast<size_t>(end - start);
    if (out && out_n) {
        if (n >= out_n) n = out_n - 1;
        memcpy(out, start, n);
        out[n] = '\0';
    }
    if (*end == '\t') *cursor = end + 1;
    else *cursor = end;
    return true;
}

media::MediaState parse_state(const char *text) {
    if (eq(text, "playing")) return media::MediaState::Playing;
    if (eq(text, "paused")) return media::MediaState::Paused;
    return media::MediaState::Stopped;
}

// Two PSRAM cover buffers. Chunks land in `back`. The picture on screen stays
// in `front` until every chunk of one key has arrived, then the two swap.
struct CoverBuffers {
    uint8_t *front = nullptr;
    uint8_t *back = nullptr;
};

CoverBuffers &cover_buffers() {
    static CoverBuffers bufs;
    if (!bufs.front) bufs.front = sd_host::alloc_spiram(media::kCoverBytes);
    if (!bufs.back) bufs.back = sd_host::alloc_spiram(media::kCoverBytes);
    return bufs;
}

constexpr int kChunkRaw = 480;
constexpr int kMaxChunks = (media::kCoverBytes + kChunkRaw - 1) / kChunkRaw;
constexpr int kSeenBytes = (kMaxChunks + 7) / 8;

struct CoverBuild {
    char key[media::kCoverKeyMax] = {};
    uint16_t chunks = 0;
    uint16_t got = 0;
    uint16_t errors = 0;
    bool active = false;
    bool heard = false;
    uint32_t started_ms = 0;
    uint32_t last_rx_ms = 0;
    uint32_t asked_ms = 0;
    uint8_t seen[kSeenBytes] = {};
};

bool bit_on(const uint8_t *seen, int idx) {
    return (seen[idx / 8] & static_cast<uint8_t>(1u << (idx % 8))) != 0;
}

void bit_set(uint8_t *seen, int idx) { seen[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8)); }

uint32_t since(uint32_t now_ms, uint32_t then_ms) {
    if (static_cast<int32_t>(now_ms - then_ms) < 0) return 0;
    return now_ms - then_ms;
}

CoverBuild &cover_build() {
    static CoverBuild build;
    return build;
}

void reset_cover_meta(media::Cover &cover, uint8_t *pixels) {
    cover = {};
    cover.rgb565 = pixels;
}

}  // namespace

bool media::apply_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "MEDIA\t", 6) != 0) return false;
    if (strncmp(line, "MEDIA_COVER", 11) == 0) return false;

    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cursor = buf;
    char field[64];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "MEDIA")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    const Cover keep_cover = view.cover;
    View next = {};
    next.ready = true;
    next.stale = false;
    next.received_ms = now_ms;
    next.media.ok = field[0] == '1';

    char state[16] = {};
    next_field(&cursor, state, sizeof(state));
    next.media.state = parse_state(state);

    next_field(&cursor, next.media.title, sizeof(next.media.title));
    next_field(&cursor, next.media.artist, sizeof(next.media.artist));
    next_field(&cursor, next.media.app, sizeof(next.media.app));

    const bool same_track = eq(view.media.title, next.media.title) &&
                            eq(view.media.artist, next.media.artist) &&
                            eq(view.media.app, next.media.app);
    if (same_track) {
        next.cover = keep_cover;
    } else {
        reset_cover_meta(next.cover, keep_cover.rgb565);
    }

    if (!next.media.ok) {
        next.media.state = MediaState::Stopped;
        next.media.title[0] = '\0';
        next.media.artist[0] = '\0';
        next.media.app[0] = '\0';
        reset_cover_meta(next.cover, keep_cover.rgb565);
    }

    view = next;
    return true;
}

namespace {

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t decode_hex(const char *hex, uint8_t *out, size_t out_n) {
    size_t n = 0;
    while (hex[0] && hex[1] && n < out_n) {
        int hi = hex_nibble(hex[0]);
        int lo = hex_nibble(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = static_cast<uint8_t>((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

}  // namespace

uint32_t media::cover_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool media::apply_cover_line(View &view, const char *line, uint32_t now_ms) {
    if (!line || strncmp(line, "MEDIA_COVER\t", 12) != 0) return false;

    CoverBuffers &bufs = cover_buffers();
    uint8_t *pixels = bufs.back;
    if (!bufs.front || !pixels) return false;

    // Parse in-place without truncating long hex payloads.
    char scratch[96];
    // Copy only the header fields before the hex blob (last field).
    const char *hex = strrchr(line, '\t');
    if (!hex || hex == line) return false;
    size_t head_len = static_cast<size_t>(hex - line);
    if (head_len >= sizeof(scratch)) return false;
    memcpy(scratch, line, head_len);
    scratch[head_len] = '\0';
    hex += 1;  // skip tab

    char *cursor = scratch;
    char field[32];
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "MEDIA_COVER")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || field[0] != '1') return false;

    char key[kCoverKeyMax] = {};
    next_field(&cursor, key, sizeof(key));
    next_field(&cursor, field, sizeof(field));
    int w = atoi(field);
    next_field(&cursor, field, sizeof(field));
    int h = atoi(field);
    next_field(&cursor, field, sizeof(field));  // fmt
    if (!eq(field, "rgb565le")) return false;
    next_field(&cursor, field, sizeof(field));
    int chunks = atoi(field);
    next_field(&cursor, field, sizeof(field));
    int idx = atoi(field);
    char crc_text[16] = {};
    const bool has_crc = next_field(&cursor, crc_text, sizeof(crc_text));

    if (w != kCoverEdge || h != kCoverEdge || chunks <= 0 || chunks > kMaxChunks || idx < 0 || idx >= chunks) {
        return false;
    }
    uint32_t expect = 0;
    if (has_crc) {
        if (strlen(crc_text) != 8) return false;
        for (int i = 0; i < 8; ++i) {
            int nibble = hex_nibble(crc_text[i]);
            if (nibble < 0) return false;
            expect = (expect << 4) | static_cast<uint32_t>(nibble);
        }
    } else {
        static bool logged_legacy = false;
        if (!logged_legacy) {
            logged_legacy = true;
            std::printf("media: capa sem crc (agent antigo)\n");
        }
    }

    // A new key throws away the back buffer. A repeat of chunk 0 is a retry, not a reset.
    // The front buffer (what the tile is showing) stays untouched.
    CoverBuild &build = cover_build();
    const bool restart = !build.active || strcmp(build.key, key) != 0 || build.chunks != chunks;
    if (restart) {
        build = {};
        strncpy(build.key, key, sizeof(build.key) - 1);
        build.chunks = static_cast<uint16_t>(chunks);
        build.active = true;
        build.started_ms = now_ms;
    }

    uint8_t raw[kChunkRaw];
    size_t wrote = decode_hex(hex, raw, sizeof(raw));
    if (!wrote) return false;
    if (has_crc && cover_crc32(raw, wrote) != expect) {
        build.errors = static_cast<uint16_t>(build.errors + 1);
        std::printf("media: capa %s %u chunks com erro\n", build.key, static_cast<unsigned>(build.errors));
        return false;
    }
    size_t offset = static_cast<size_t>(idx) * static_cast<size_t>(kChunkRaw);
    if (offset >= kCoverBytes || offset + wrote > kCoverBytes) return false;
    memcpy(pixels + offset, raw, wrote);

    if (!bit_on(build.seen, idx)) {
        bit_set(build.seen, idx);
        build.got = static_cast<uint16_t>(build.got + 1);
    }
    build.last_rx_ms = now_ms;
    build.heard = true;
    if (build.got < build.chunks) return false;

    view.cover.ready = true;
    strncpy(view.cover.key, key, sizeof(view.cover.key) - 1);
    view.cover.w = static_cast<uint16_t>(w);
    view.cover.h = static_cast<uint16_t>(h);
    view.cover.chunks = build.chunks;
    view.cover.got = build.got;
    view.cover.rgb565 = pixels;
    uint8_t *shown = bufs.front;
    bufs.front = bufs.back;
    bufs.back = shown;
    build.active = false;
    return true;
}

bool media::cover_poll(uint32_t now_ms, char *request, size_t request_cap) {
    CoverBuild &build = cover_build();
    if (!build.active || !request || request_cap < 32) return false;
    if (since(now_ms, build.started_ms) >= 10000) {
        if (build.errors) {
            std::printf("media: capa %s %u chunks com erro\n", build.key, static_cast<unsigned>(build.errors));
        }
        build.active = false;
        return false;
    }
    if (!build.heard || since(now_ms, build.last_rx_ms) < 2000) return false;
    if (build.asked_ms && since(now_ms, build.asked_ms) < 2000) return false;

    int used = std::snprintf(request, request_cap, "MEDIA_COVER?\t%s\t", build.key);
    if (used < 0 || static_cast<size_t>(used) >= request_cap) return false;
    bool any = false;
    for (int idx = 0; idx < build.chunks; ++idx) {
        if (bit_on(build.seen, idx)) continue;
        int next = std::snprintf(request + used, request_cap - static_cast<size_t>(used), "%s%d", any ? "," : "", idx);
        if (next < 0 || static_cast<size_t>(used + next) >= request_cap) return false;
        used += next;
        any = true;
    }
    if (!any) return false;
    build.asked_ms = now_ms;
    return true;
}

bool media::refresh_age(View &view, uint32_t now_ms) {
    if (!view.ready) {
        bool changed = view.stale;
        view.stale = false;
        return changed;
    }
    bool want = (now_ms - view.received_ms) >= kStaleMs;
    if (want == view.stale) return false;
    view.stale = want;
    return true;
}
