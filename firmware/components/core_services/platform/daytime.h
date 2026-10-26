#pragma once

// Wall clock for the panel. Time comes from NTP once the network is up; until then local() is false.
namespace daytime {

void start();
// Apply a Tessera timezone id (e.g. "sao_paulo", "utc"). Idempotent; restarts NTP mapping.
bool set_timezone(const char *tz_id);
const char *timezone_id();
const char *timezone_label(const char *tz_id);

// If the RTC is still at epoch (power-on), seed it from the last persisted wall time.
void restore();

// Local civil time in the configured zone. wday is 0 = Sunday; month is 0-based.
bool local(int &hour, int &minute, int &wday, int &day, int &month);
bool local(int &hour, int &minute, int &second, int &wday, int &day, int &month, int &year);

}  // namespace daytime
