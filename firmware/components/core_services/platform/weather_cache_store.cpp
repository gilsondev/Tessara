#include "platform/weather_cache_store.h"

#include "domain/dashboard_layout.h"
#include "hal/board.h"
#include "platform/widget_settings.h"

namespace {

// Forecast state, separate from user settings. A settings backup must leave this namespace out.
constexpr char kNs[] = "cache_wx";
constexpr char kKey[] = "forecast";

hal::StoragePort *store() { return hal::board().storage; }

}  // namespace

namespace weather_cache_store {

bool load(char *out, size_t out_len) {
    auto *storage = store();
    if (!storage || !out || out_len == 0) return false;
    return storage->get(kNs, kKey, out, out_len) && out[0];
}

bool save(const char *blob) {
    auto *storage = store();
    if (!storage || !blob) return false;
    return storage->set(kNs, kKey, blob);
}

void migrate_from_settings() {
    char legacy[512];
    if (!widget_settings::get(dashboard::id_weather(), "forecast", legacy, sizeof(legacy)) || !legacy[0]) return;
    auto *storage = store();
    if (!storage) return;
    char current[8] = {};
    if (!storage->get(kNs, kKey, current, sizeof(current)) || !current[0]) {
        storage->set(kNs, kKey, legacy);
    }
    storage->erase("w_weather", "forecast");
}

}  // namespace weather_cache_store
