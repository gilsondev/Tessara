#include "weather/parse.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

namespace weather {
namespace {

void copy_name(char *dest, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t n = 0;
    if (src) {
        while (src[n] && n + 1 < cap) {
            dest[n] = src[n];
            ++n;
        }
    }
    dest[n] = '\0';
}

int round_celsius(JsonVariant value) { return static_cast<int>(std::lround(value.as<float>())); }

}  // namespace

bool parse_place(const char *json, Place &out) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
    JsonArray results = doc["results"];
    if (results.isNull() || results.size() == 0) return false;
    JsonObject first = results[0];
    const char *name = first["name"] | "";
    if (!name[0] || first["latitude"].isNull() || first["longitude"].isNull()) return false;
    Place next = {};
    copy_name(next.name, sizeof(next.name), name);
    next.latitude = first["latitude"].as<float>();
    next.longitude = first["longitude"].as<float>();
    out = next;
    return true;
}

bool parse_forecast(const char *json, Forecast &out) {
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["daily"]["time"] = true;
    filter["daily"]["weather_code"] = true;
    filter["daily"]["temperature_2m_max"] = true;
    filter["daily"]["temperature_2m_min"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
    JsonObject current = doc["current"];
    JsonArray codes = doc["daily"]["weather_code"];
    JsonArray maxes = doc["daily"]["temperature_2m_max"];
    JsonArray mins = doc["daily"]["temperature_2m_min"];
    JsonArray times = doc["daily"]["time"];
    if (current.isNull() || current["temperature_2m"].isNull() || current["weather_code"].isNull()) return false;
    if (codes.isNull() || maxes.isNull() || mins.isNull()) return false;
    if (codes.size() < 7 || maxes.size() < 7 || mins.size() < 7) return false;
    Forecast next = {};
    next.temperature_c = round_celsius(current["temperature_2m"]);
    next.code = current["weather_code"].as<int>();
    for (int i = 0; i < 7; ++i) {
        next.days[i].code = codes[i].as<int>();
        next.days[i].temperature_max_c = round_celsius(maxes[i]);
        next.days[i].temperature_min_c = round_celsius(mins[i]);
        if (!times.isNull() && i < static_cast<int>(times.size())) {
            const char *day = times[i] | "";
            copy_name(next.days[i].date, sizeof(next.days[i].date), day);
        }
    }
    out = next;
    return true;
}

}  // namespace weather
