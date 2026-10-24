#pragma once

#include "weather/parse.h"

#include <stddef.h>

namespace weather_client {

bool geocode(const char *city, weather::Place &out, char *error, size_t error_cap);
bool reverse(float latitude, float longitude, weather::Place &out, char *error, size_t error_cap);
bool forecast(float latitude, float longitude, weather::Forecast &out, char *error, size_t error_cap);
// Absolute http://host/path GET into SPIRAM body buffer. Used by Widget SDK DeviceHttp.
bool http_get_url(const char *url, int *status, const char **body_out, size_t *got);

}  // namespace weather_client
