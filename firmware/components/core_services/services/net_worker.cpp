#include "services/net_worker.h"

#include "platform/daytime.h"
#include "platform/log.h"
#include "services/text.h"
#include "services/todoist_client.h"
#include "services/weather_client.h"
#include "weather/cache.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t kConnectTimeoutMs = 15000;

TaskHandle_t task = nullptr;
volatile bool working = false;
volatile bool finished = false;
net_worker::Job job;
net_worker::Result result;
ViewItem views[kMaxViews];
TaskItem *tasks = nullptr;
char ssid[64];
char pass[64];

bool connect() {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (!ssid[0]) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname("tessera");
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kConnectTimeoutMs) delay(20);
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
        daytime::start();
        logger::printf("wi-fi ok %s\n", WiFi.localIP().toString().c_str());
    }
    else logger::printf("wi-fi falhou\n");
    return ok;
}

void run() {
    result = net_worker::Result{};
    result.kind = job.kind;
    result.tag = job.tag;
    result.view = job.view;
    text::copy(result.query, sizeof(result.query), job.query);
    text::copy(result.id, sizeof(result.id), job.id);
    if (job.kind != net_worker::Kind::Connect && WiFi.status() != WL_CONNECTED && !connect()) {
        text::copy(result.error, sizeof(result.error), "sem wi-fi");
        return;
    }
    switch (job.kind) {
        case net_worker::Kind::Connect:
            result.ok = connect();
            if (!result.ok) text::copy(result.error, sizeof(result.error), ssid[0] ? "wi-fi falhou" : "falta o wi-fi");
            break;
        case net_worker::Kind::Views:
            result.count = todoist::load_views(views, kMaxViews);
            result.views = views;
            result.ok = result.count > 0;
            break;
        case net_worker::Kind::Tasks:
            result.ok = todoist::load_tasks(job.query, tasks, &result.count, result.error, sizeof(result.error));
            result.tasks = tasks;
            break;
        case net_worker::Kind::Close:
            result.ok = todoist::close_task(job.id, result.error, sizeof(result.error), &result.status);
            break;
        case net_worker::Kind::WeatherGeocode:
            result.ok = weather_client::geocode(job.query, result.place, result.error, sizeof(result.error));
            break;
        case net_worker::Kind::WeatherForecast: {
            float lat = 0;
            float lon = 0;
            if (!weather::coords(&lat, &lon)) {
                text::copy(result.error, sizeof(result.error), "sem local");
                break;
            }
            result.ok = weather_client::forecast(lat, lon, result.forecast, result.error, sizeof(result.error));
            break;
        }
        case net_worker::Kind::HttpGet: {
            result.http_job = job.http_job;
            const char *body = nullptr;
            size_t got = 0;
            int status = 0;
            result.ok = weather_client::http_get_url(job.query, &status, &body, &got);
            result.status = status;
            result.body = body;
            result.body_len = got;
            if (!result.ok) text::copy(result.error, sizeof(result.error), "rede falhou");
            break;
        }
    }
}

void loop(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run();
        finished = true;
        working = false;
    }
}
}  // namespace

bool net_worker::begin() {
    if (task) return true;
    tasks = static_cast<TaskItem *>(heap_caps_malloc(sizeof(TaskItem) * kMaxTasks, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!tasks) return false;
    // Stack in PSRAM: TLS to api.todoist.com needs ~32KB contiguous internal DRAM for
    // mbedTLS record buffers; a 24KB internal stack left only ~40KB free and broke connect.
    constexpr size_t kStackBytes = 24 * 1024;
    static StaticTask_t tcb;
    static StackType_t *stack = nullptr;
    if (!stack) {
        stack = static_cast<StackType_t *>(heap_caps_malloc(kStackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!stack) return false;
    }
    task = xTaskCreateStaticPinnedToCore(loop, "net", kStackBytes, nullptr, 3, stack, &tcb, 0);
    return task != nullptr;
}

void net_worker::configure(const char *wifi_ssid, const char *wifi_pass, const char *token) {
    if (working) return;
    text::copy(ssid, sizeof(ssid), wifi_ssid);
    text::copy(pass, sizeof(pass), wifi_pass);
    todoist::set_token(token);
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
}

bool net_worker::busy() { return working || finished; }

bool net_worker::submit(const Job &next) {
    if (!task || busy()) return false;
    job = next;
    working = true;
    xTaskNotifyGive(task);
    return true;
}

bool net_worker::take(Result &out) {
    if (!finished) return false;
    out = result;
    finished = false;
    return true;
}

bool net_worker::online() { return WiFi.status() == WL_CONNECTED; }

void net_worker::describe(DeviceInfo &info) {
    info.online = online();
    text::copy(info.ssid, sizeof(info.ssid), info.online ? WiFi.SSID().c_str() : "");
    text::copy(info.ip, sizeof(info.ip), info.online ? WiFi.localIP().toString().c_str() : "");
    if (info.online) {
        int raw = WiFi.RSSI();
        if (raw > 0) raw = 0;
        if (raw < -127) raw = -127;
        info.rssi = static_cast<int8_t>(raw);
    } else {
        info.rssi = 0;
    }
}
