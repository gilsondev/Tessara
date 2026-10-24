#include "platform/power.h"

namespace {
int g_percent = 80;
}  // namespace

namespace sim_power {
void set_battery(int percent) { g_percent = percent; }
}  // namespace sim_power

namespace power {
bool battery_present() { return g_percent >= 0; }
int battery_percent() { return g_percent; }
}  // namespace power
