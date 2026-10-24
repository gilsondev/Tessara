#pragma once

#include "domain/device_settings.h"
#include "domain/model.h"
#include "weather/parse.h"

#include <stddef.h>
#include <stdint.h>

// Runs blocking network work on its own core-0 task so the UI loop never waits on Wi-Fi or TLS.
// One job at a time: submit() while idle, then poll take() from the UI loop.
namespace net_worker {

enum class Kind : uint8_t { Connect, Views, Tasks, Close, WeatherGeocode, WeatherForecast, HttpGet };

struct Job {
    Kind kind;
    uint32_t tag;  // echoed back so the caller can drop stale results
    int view;
    int http_job;  // DeviceHttp job id when kind == HttpGet
    char query[400];
    char id[20];
};

struct Result {
    Kind kind;
    uint32_t tag;
    int view;
    int http_job;
    char query[400];
    bool ok;
    int status;  // HTTP status, or 0 when the link never answered
    char error[48];
    char id[20];
    int count;
    const ViewItem *views;  // valid until the next submit()
    const TaskItem *tasks;  // valid until the next submit()
    weather::Place place;
    weather::Forecast forecast;
    const char *body;  // HttpGet — valid until the next submit()
    size_t body_len;
};

bool begin();
void configure(const char *ssid, const char *pass, const char *token);
bool busy();
bool submit(const Job &job);
bool take(Result &result);
bool online();
void describe(DeviceInfo &info);  // fills ip, ssid and online

}  // namespace net_worker
