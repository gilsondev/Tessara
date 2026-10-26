#include "media/snapshot.h"

#include "platform/sd_host.h"

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

// One shared cover buffer in PSRAM — View stays tiny (BSS + stack copies stay safe).
uint8_t *cover_pixels() {
    static uint8_t *buf = nullptr;
    if (!buf) buf = sd_host::alloc_spiram(media::kCoverBytes);
    return buf;
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

bool media::apply_cover_line(View &view, const char *line) {
    if (!line || strncmp(line, "MEDIA_COVER\t", 12) != 0) return false;

    uint8_t *pixels = cover_pixels();
    if (!pixels) return false;

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

    if (w != kCoverEdge || h != kCoverEdge || chunks <= 0 || idx < 0 || idx >= chunks) return false;

    if (strcmp(view.cover.key, key) != 0) {
        reset_cover_meta(view.cover, pixels);
        strncpy(view.cover.key, key, sizeof(view.cover.key) - 1);
        view.cover.w = static_cast<uint16_t>(w);
        view.cover.h = static_cast<uint16_t>(h);
        view.cover.chunks = static_cast<uint16_t>(chunks);
    } else {
        view.cover.rgb565 = pixels;
    }

    constexpr size_t kChunkRaw = 480;
    size_t offset = static_cast<size_t>(idx) * kChunkRaw;
    if (offset >= kCoverBytes) return false;
    size_t room = kCoverBytes - offset;
    size_t wrote = decode_hex(hex, pixels + offset, room);
    if (!wrote) return false;

    // Count unique chunks simply: mark completion when last index arrives and
    // we've seen chunks times (host sends 0..n-1 in order).
    if (idx + 1 > view.cover.got) view.cover.got = static_cast<uint16_t>(idx + 1);
    if (view.cover.got >= view.cover.chunks) {
        view.cover.ready = true;
        return true;
    }
    return false;
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
