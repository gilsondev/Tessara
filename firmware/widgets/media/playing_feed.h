#pragma once

#include "tessera/data.h"

#include <stdint.h>

// USB now-playing feed. The shell calls this without naming the package.
namespace playing {

const char *tile_id();
bool is_tile(const char *widget_id);

// Call once on the UI task before USB lines arrive.
void prepare();
// MEDIA_COVER is applied off the UI task. True when the line was a cover chunk.
bool absorb_cover(const char *line, uint32_t now_ms);
// True once, after a cover chunk completed. The UI task publishes.
bool take_cover();

// Applies one USB line. Publishes only when the visible snapshot changes
// (a track line, or the last chunk of a cover).
bool ingest(tessera::IData *data, const char *line, uint32_t now_ms, bool agent_ok);
// Publishes when the stale flag flips.
bool refresh(tessera::IData *data, uint32_t now_ms, bool agent_ok);
// Fills `out` with a retry for the cover chunks still missing.
bool cover_request(uint32_t now_ms, char *out, size_t cap);
void publish(tessera::IData *data, bool agent_ok);

}  // namespace playing
