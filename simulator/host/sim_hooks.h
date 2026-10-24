#pragma once

#include "domain/dashboard_layout.h"

namespace sim_time {
void set(int hour, int minute, int wday, int day, int month, bool ok = true);
}

namespace sim_power {
void set_battery(int percent);  // <0 hides battery
}

namespace sim_settings {
void set_dir(const char *path);
const dashboard::Layout &layout();
}
