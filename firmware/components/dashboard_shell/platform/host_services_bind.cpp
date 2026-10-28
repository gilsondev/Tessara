#include "platform/host_services_bind.h"

#include "platform/daytime.h"
#include "platform/device_http.h"
#include "platform/widget_settings.h"
#include "tessera/host_clock.h"
#include "tessera/memory_services.h"
#include "tessera/runtime.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

tessera::MemoryTime g_time;
tessera::MemoryScheduler g_scheduler;
tessera::MemoryLogger g_logger;
tessera::TopicBus g_bus;
tessera::HostServices g_bag;

class DaytimeBridge final : public tessera::ITime {
public:
    tessera::TimeSnapshot now() const override {
        tessera::TimeSnapshot snap = g_time.snap;
        int hour = 0, minute = 0, second = 0, wday = 0, day = 1, month = 0, year = 0;
        if (daytime::local(hour, minute, second, wday, day, month, year)) {
            snap.hour = hour;
            snap.minute = minute;
            snap.second = second;
            snap.wday = wday;
            snap.day = day;
            snap.month = month;
            snap.year = year;
            snap.synced = true;
        } else {
            snap.synced = false;
        }
        return snap;
    }
};

// ISettings keys arrive as "<instance_id>/<key>" from WidgetContext.
class NvsWidgetSettings final : public tessera::ISettings {
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

tessera::HostServices &tessera::host_services() { return g_bag; }

void tessera::bind_host_services() {
    if (!g_bound) {
        g_bag.time = &g_daytime;
        g_bag.settings = &g_settings;
        g_bag.http = &device_http();
        g_bag.scheduler = &g_scheduler;
        g_bag.logger = &g_logger;
        g_bag.data = &g_bus;
        set_host_now_ms([]() -> uint32_t { return millis(); });
        g_bound = true;
    }
    runtime().set_services(&g_bag);
}
