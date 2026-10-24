#pragma once

// Wall clock for the panel. Time comes from NTP once the network is up; until then local() is false.
namespace daytime {

void start();
// Local time in America/Sao_Paulo (UTC−3, no daylight saving). wday is 0 = Sunday.
bool local(int &hour, int &minute, int &wday, int &day, int &month);

}  // namespace daytime
