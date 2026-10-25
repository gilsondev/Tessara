#pragma once

#include "clock_summary.h"

// Cached clock/date display prefs (RAM). Reload after NVS changes — never hit NVS from the 1 Hz UI timer.
namespace face_prefs {

void reload();
clock_face::Options options();

}  // namespace face_prefs
