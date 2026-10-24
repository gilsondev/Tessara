#pragma once

namespace weather {

struct Place {
    char name[48];
    float latitude;
    float longitude;
};

struct Day {
    char date[11];  // YYYY-MM-DD
    int temperature_min_c;
    int temperature_max_c;
    int code;
};

struct Forecast {
    int temperature_c;
    int code;
    Day days[7];  // index 0 = today
};

// First geocoding hit. False leaves `out` untouched.
bool parse_place(const char *json, Place &out);
// Current conditions plus 7 daily rows. False leaves `out` untouched.
bool parse_forecast(const char *json, Forecast &out);

}  // namespace weather
