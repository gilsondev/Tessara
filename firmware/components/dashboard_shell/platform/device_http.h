#pragma once

#include "tessara/services.h"

#include <stddef.h>
#include <stdint.h>

// Device IHttp: queues GETs; UI/net_worker takes URL, then complete() delivers.
namespace tessara {

class DeviceHttp final : public IHttp {
public:
    static constexpr int kMaxJobs = 8;

    int get(const char *url, uint32_t generation, HttpCallback cb, void *user) override;
    void cancel(int job) override;
    void cancel_generation(uint32_t generation) override;

    // Pop next queued job that is not yet in-flight. Returns job id or -1.
    int take_pending(char *url_out, size_t url_len, uint32_t *generation_out);
    bool complete(int job, HttpStatus status, const char *body, size_t len);
    int pending_count() const;

private:
    struct Job {
        bool active = false;
        bool taken = false;
        uint32_t generation = 0;
        HttpCallback cb = nullptr;
        void *user = nullptr;
        char url[200] = {};
    };
    Job jobs_[kMaxJobs] = {};
};

DeviceHttp &device_http();

}  // namespace tessara
