#pragma once

#include "domain/dashboard_layout.h"
#include "domain/device_settings.h"

// Persistent device settings (NVS namespace "todoist"); compile-time secrets.h only seeds empty values.
namespace settings {

struct Credentials {
    char ssid[64];
    char pass[64];
    char token[128];
};

Credentials load_credentials();
void save_wifi(const char *ssid, const char *pass);
void save_token(const char *token);
void clear_token();
// Keeps a private copy of the current credentials, then clear_credentials() can empty them.
// A second stash while the live values are empty does not overwrite that copy.
void stash();
bool unstash();
void clear_credentials();

DeviceSettings load_device();
void save_device(const DeviceSettings &value);

struct WeatherPlace {
    char name[48];
    float latitude;
    float longitude;
    bool set;
};

WeatherPlace load_weather();
void save_weather(const char *name, float latitude, float longitude);
void clear_weather();

dashboard::Layout load_dashboard();
void save_dashboard(const dashboard::Layout &layout);

}  // namespace settings
