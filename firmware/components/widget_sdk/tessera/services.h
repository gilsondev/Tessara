#pragma once

#include <stddef.h>
#include <stdint.h>

// Tessera Widget SDK services (WIDGET-SDK.md). No Arduino / board headers.
namespace tessera {

struct TimeSnapshot {
    int hour = 0;
    int minute = 0;
    int second = 0;
    int wday = 0;  // 0 = Sunday
    int day = 1;
    int month = 0;  // 0-based, like tm_mon
    int year = 0;   // full year, e.g. 2026
    bool synced = false;
};

class ITime {
public:
    virtual ~ITime() = default;
    virtual TimeSnapshot now() const = 0;
};

class ISettings {
public:
    virtual ~ISettings() = default;
    // Key is already namespaced by WidgetContext (instance_id/key).
    virtual bool get(const char *key, char *out, size_t out_len) const = 0;
    virtual bool set(const char *key, const char *value) = 0;
};

enum class HttpStatus : uint8_t { Ok = 0, Timeout, Network, Invalid };

struct HttpResult {
    HttpStatus status = HttpStatus::Network;
    const char *body = nullptr;
    size_t len = 0;
};

using HttpCallback = void (*)(void *user, uint32_t generation, const HttpResult &result);

class IHttp {
public:
    virtual ~IHttp() = default;
    // Returns job id (>=0) or -1. Host must not invoke cb after cancel / cancel_generation.
    virtual int get(const char *url, uint32_t generation, HttpCallback cb, void *user) = 0;
    virtual void cancel(int job) = 0;
    virtual void cancel_generation(uint32_t generation) = 0;
};

using JobCallback = void (*)(void *user, uint32_t generation);

class IScheduler {
public:
    virtual ~IScheduler() = default;
    virtual int every_ms(uint32_t period_ms, uint32_t generation, JobCallback cb, void *user) = 0;
    virtual void cancel(int job) = 0;
    virtual void cancel_generation(uint32_t generation) = 0;
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void info(const char *msg) = 0;
    virtual void warn(const char *msg) = 0;
};

class IData;

// Non-owning bag; lifetime owned by host (shell / test harness).
struct HostServices {
    ITime *time = nullptr;
    ISettings *settings = nullptr;
    IHttp *http = nullptr;
    IScheduler *scheduler = nullptr;
    ILogger *logger = nullptr;
    IData *data = nullptr;
};

}  // namespace tessera
