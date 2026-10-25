#pragma once

#include <stddef.h>
#include <stdint.h>

// Agent-pushed Google calendar catalog + local selection (no tokens).
namespace calendar {

constexpr int kMaxAccounts = 4;
constexpr int kMaxCalendars = 16;
constexpr int kEmailMax = 48;
constexpr int kCalIdMax = 96;  // Google group/import ids often exceed 64
constexpr int kCalNameMax = 40;
constexpr size_t kCatalogLineMax = 2048;

struct CalEntry {
    char account[kEmailMax] = {};
    char id[kCalIdMax] = {};
    char name[kCalNameMax] = {};
    uint32_t color_rgb = 0x1A73E8;
    bool selected = false;
};

struct Catalog {
    bool ready = false;
    bool auth_pending = false;
    char status[48] = {};  // short hint for portal ("abra o browser no Mac", …)
    CalEntry items[kMaxCalendars] = {};
    int count = 0;
};

// CALENDAR_CATALOG\tv1\tok\tstatus\taccount\tid\tname\tcolor\tselected\t…
bool apply_catalog_line(Catalog &catalog, const char *line);

// CALENDAR_AUTH\tv1\t1|0\tstatus
bool apply_auth_line(Catalog &catalog, const char *line);

// Apply selection flags from comma-separated ids (NVS / portal).
void apply_selection(Catalog &catalog, const char *csv_ids);

// Write selected ids as CSV into out.
void format_selection(const Catalog &catalog, char *out, size_t out_len);

}  // namespace calendar
