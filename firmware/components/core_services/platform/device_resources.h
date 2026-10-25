#pragma once

#include <stddef.h>
#include <stdint.h>

// Live sample of DRAM / PSRAM / flash sketch / volume (SD or FFat).
// Used by device settings and the web portal — no LVGL / Arduino in the header.
namespace device_resources {

struct Sample {
    uint32_t heap_free_b = 0;
    uint32_t heap_total_b = 0;
    uint32_t heap_largest_b = 0;
    uint32_t heap_min_b = 0;
    uint32_t psram_free_b = 0;
    uint32_t psram_total_b = 0;
    uint32_t psram_largest_b = 0;
    uint32_t flash_sketch_b = 0;
    // Free room in the running app partition (factory or OTA slot). Not "whole chip".
    uint32_t flash_sketch_free_b = 0;
    bool storage_ready = false;
    char storage_kind[16] = {};  // "Cartão SD", "Flash", or empty
    uint64_t storage_free_b = 0;
    uint64_t storage_total_b = 0;
    uint32_t uptime_s = 0;
};

Sample sample();

// "12,3 MB" / "841 KB" / "512 B" into dest.
void format_bytes(uint64_t bytes, char *dest, size_t cap);
// "X livres de Y", e.g. "1,2 MB livres de 8,0 MB".
void format_pair(uint64_t free_b, uint64_t total_b, char *dest, size_t cap);
// Percent of capacity in use (0–100). 0 if total is 0.
int percent_used(uint64_t free_b, uint64_t total_b);

}  // namespace device_resources
