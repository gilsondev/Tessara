#include "platform/device_resources.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include "platform/sd_host.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#endif

namespace {

void write_scaled(uint64_t bytes, unsigned scale, const char *unit, char *dest, size_t cap) {
    // One decimal when scale >= 1024 (KB+) and value isn't huge.
    uint64_t whole = bytes / scale;
    uint64_t frac = (bytes % scale) * 10 / scale;
    if (whole >= 100 || scale == 1) {
        std::snprintf(dest, cap, "%llu %s", static_cast<unsigned long long>(whole), unit);
    } else {
        std::snprintf(dest, cap, "%llu,%llu %s", static_cast<unsigned long long>(whole),
                      static_cast<unsigned long long>(frac), unit);
    }
}

}  // namespace

void device_resources::format_bytes(uint64_t bytes, char *dest, size_t cap) {
    if (!dest || cap == 0) return;
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        write_scaled(bytes, 1024ull * 1024ull * 1024ull, "GB", dest, cap);
    } else if (bytes >= 1024ull * 1024ull) {
        write_scaled(bytes, 1024ull * 1024ull, "MB", dest, cap);
    } else if (bytes >= 1024ull) {
        write_scaled(bytes, 1024ull, "KB", dest, cap);
    } else {
        std::snprintf(dest, cap, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

void device_resources::format_pair(uint64_t free_b, uint64_t total_b, char *dest, size_t cap) {
    if (!dest || cap == 0) return;
    char a[24], b[24];
    format_bytes(free_b, a, sizeof(a));
    format_bytes(total_b, b, sizeof(b));
    std::snprintf(dest, cap, "%s livres de %s", a, b);
}

int device_resources::percent_used(uint64_t free_b, uint64_t total_b) {
    if (total_b == 0) return 0;
    uint64_t used = total_b > free_b ? total_b - free_b : 0;
    if (used >= total_b) return 100;
    return static_cast<int>((used * 100ull) / total_b);
}

device_resources::Sample device_resources::sample() {
    Sample out = {};
#if defined(ARDUINO)
    out.heap_free_b = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    out.heap_total_b = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    out.heap_largest_b = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    out.heap_min_b = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    out.psram_free_b = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    out.psram_total_b = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    out.psram_largest_b = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    out.flash_sketch_b = ESP.getSketchSize();
    // Dual-OTA: FreeSketchSpace is the other slot. Factory-only tables return 0 even when
    // the running partition still has room — fall back to partition size − sketch.
    const uint32_t ota_free = ESP.getFreeSketchSpace();
    uint32_t part_size = 0;
    if (const esp_partition_t *running = esp_ota_get_running_partition()) {
        part_size = running->size;
    } else if (const esp_partition_t *factory =
                   esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr)) {
        part_size = factory->size;
    }
    if (ota_free > 0) {
        out.flash_sketch_free_b = ota_free;
    } else if (part_size > out.flash_sketch_b) {
        out.flash_sketch_free_b = part_size - out.flash_sketch_b;
    } else {
        out.flash_sketch_free_b = 0;
    }

    sd_host::ensure();
    out.storage_ready = sd_host::ready();
    if (out.storage_ready) {
        out.storage_free_b = sd_host::free_bytes();
        out.storage_total_b = sd_host::total_bytes();
        const char *kind = sd_host::backend_name();
        if (kind) {
            std::strncpy(out.storage_kind, kind, sizeof(out.storage_kind) - 1);
            out.storage_kind[sizeof(out.storage_kind) - 1] = '\0';
        }
    }

    out.uptime_s = millis() / 1000u;
#endif
    return out;
}
