#include "platform/host_services_bind.h"

#include "platform/daytime.h"
#include "platform/device_http.h"
#include "platform/widget_settings.h"
#include "tessara/host_clock.h"
#include "tessara/memory_services.h"
#include "tessara/runtime.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

tessara::MemoryTime g_time;
tessara::MemoryScheduler g_scheduler;
tessara::MemoryLogger g_logger;
tessara::HostServices g_bag;

class DaytimeBridge final : public tessara::ITime {
public:
    tessara::TimeSnapshot now() const override {
        tessara::TimeSnapshot snap = g_time.snap;
        int hour = 0, minute = 0, wday = 0, day = 1, month = 0;
        if (daytime::local(hour, minute, wday, day, month)) {
            snap.hour = hour;
            snap.minute = minute;
            snap.wday = wday;
            snap.day = day;
            snap.month = month;
            snap.synced = true;
        } else {
            snap.synced = false;
        }
        return snap;
    }
};

// ISettings keys arrive as "<instance_id>/<key>" from WidgetContext.
class NvsWidgetSettings final : public tessara::ISettings {
public:
    bool get(const char *key, char *out, size_t out_len) const override {
        char id[64] = {}, leaf[32] = {};
        if (!split(key, id, sizeof(id), leaf, sizeof(leaf))) return false;
        return widget_settings::get(id, leaf, out, out_len);
    }

    bool set(const char *key, const char *value) override {
        char id[64] = {}, leaf[32] = {};
        if (!split(key, id, sizeof(id), leaf, sizeof(leaf))) return false;
        return widget_settings::set(id, leaf, value);
    }

private:
    static bool split(const char *full, char *id, size_t id_len, char *leaf, size_t leaf_len) {
        if (!full || !full[0] || !id || !leaf || id_len < 2 || leaf_len < 2) return false;
        const char *slash = std::strrchr(full, '/');
        if (!slash || slash == full || !slash[1]) return false;
        size_t n = static_cast<size_t>(slash - full);
        if (n >= id_len) n = id_len - 1;
        std::memcpy(id, full, n);
        id[n] = '\0';
        std::snprintf(leaf, leaf_len, "%s", slash + 1);
        return id[0] && leaf[0];
    }
};

DaytimeBridge g_daytime;
NvsWidgetSettings g_settings;
bool g_bound = false;

}  // namespace

tessara::HostServices &tessara::host_services() { return g_bag; }

void tessara::bind_host_services() {
    if (!g_bound) {
        g_bag.time = &g_daytime;
        g_bag.settings = &g_settings;
        g_bag.http = &device_http();
        g_bag.scheduler = &g_scheduler;
        g_bag.logger = &g_logger;
        set_host_now_ms([]() -> uint32_t { return millis(); });
        g_bound = true;
    }
    runtime().set_services(&g_bag);
}
