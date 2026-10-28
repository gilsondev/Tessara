#include "playing_feed.h"

#include "media/snapshot.h"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

media::View g_view{};
tessera::SnapshotWriter<media::Published> g_writer{};
SemaphoreHandle_t view_mu = nullptr;
bool cover_ready = false;

void lock_view() {
    if (view_mu) xSemaphoreTakeRecursive(view_mu, portMAX_DELAY);
}

void unlock_view() {
    if (view_mu) xSemaphoreGiveRecursive(view_mu);
}

}  // namespace

const char *playing::tile_id() { return "org.tessera.media"; }

bool playing::is_tile(const char *widget_id) {
    return widget_id && std::strcmp(widget_id, tile_id()) == 0;
}

void playing::prepare() {
    if (!view_mu) view_mu = xSemaphoreCreateRecursiveMutex();
}

void playing::publish(tessera::IData *data, bool agent_ok) {
    if (!data) return;
    g_writer.attach(*data, tessera::TopicId::Media);
    media::Published &slot = g_writer.begin();
    lock_view();
    slot.view = g_view;
    unlock_view();
    slot.agent_ok = agent_ok;
    g_writer.commit();
}

bool playing::absorb_cover(const char *line, uint32_t now_ms) {
    if (!line || std::strncmp(line, "MEDIA_COVER\t", 12) != 0) return false;
    lock_view();
    if (media::apply_cover_line(g_view, line, now_ms)) cover_ready = true;
    unlock_view();
    return true;
}

bool playing::take_cover() {
    lock_view();
    bool ready = cover_ready;
    cover_ready = false;
    unlock_view();
    return ready;
}

bool playing::ingest(tessera::IData *data, const char *line, uint32_t now_ms, bool agent_ok) {
    if (!line) return false;
    bool applied = false;
    if (std::strncmp(line, "MEDIA_COVER\t", 12) == 0) {
        lock_view();
        applied = media::apply_cover_line(g_view, line, now_ms);
        unlock_view();
    } else {
        lock_view();
        applied = media::apply_line(g_view, line, now_ms);
        unlock_view();
    }
    if (!applied) return false;
    publish(data, agent_ok);
    return true;
}

bool playing::cover_request(uint32_t now_ms, char *out, size_t cap) {
    lock_view();
    bool ask = media::cover_poll(now_ms, out, cap);
    unlock_view();
    return ask;
}

bool playing::refresh(tessera::IData *data, uint32_t now_ms, bool agent_ok) {
    lock_view();
    bool aged = media::refresh_age(g_view, now_ms);
    unlock_view();
    if (!aged) return false;
    publish(data, agent_ok);
    return true;
}
