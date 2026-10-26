#pragma once

#include <stdint.h>

// Parsed FOCUS snapshot for the Mac tile and detail screen.
namespace focus {

constexpr uint32_t kStaleMs = 45000;
constexpr int kIdMax = 64;
constexpr int kNameMax = 41;
constexpr int kMaxModes = 12;

struct Mode {
    char id[kIdMax] = {};
    char name[kNameMax] = {};
};

struct View {
    bool ready = false;
    bool stale = false;
    bool ok = false;
    uint32_t received_ms = 0;
    char active_id[kIdMax] = {};
    char active_name[kNameMax] = {};
    int mode_count = 0;
    Mode modes[kMaxModes] = {};
};

// Mutates `view` from a full line starting with "FOCUS\t".
bool apply_line(View &view, const char *line, uint32_t now_ms);

// Marks stale when ready and age >= kStaleMs. Returns true if stale flag changed.
bool refresh_age(View &view, uint32_t now_ms);

}  // namespace focus
