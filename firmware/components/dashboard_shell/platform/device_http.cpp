#include "platform/device_http.h"

#include <cstring>

namespace {

tessera::DeviceHttp g_http;

}  // namespace

int tessera::DeviceHttp::get(const char *url, uint32_t generation, HttpCallback cb, void *user) {
    if (!url || !cb || generation == 0) return -1;
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active) continue;
        jobs_[i].active = true;
        jobs_[i].taken = false;
        jobs_[i].generation = generation;
        jobs_[i].cb = cb;
        jobs_[i].user = user;
        std::strncpy(jobs_[i].url, url, sizeof(jobs_[i].url) - 1);
        jobs_[i].url[sizeof(jobs_[i].url) - 1] = '\0';
        return i;
    }
    return -1;
}

void tessera::DeviceHttp::cancel(int job) {
    if (job < 0 || job >= kMaxJobs) return;
    jobs_[job] = {};
}

void tessera::DeviceHttp::cancel_generation(uint32_t generation) {
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active && jobs_[i].generation == generation) jobs_[i] = {};
    }
}

int tessera::DeviceHttp::pending_count() const {
    int n = 0;
    for (int i = 0; i < kMaxJobs; ++i)
        if (jobs_[i].active) ++n;
    return n;
}

int tessera::DeviceHttp::take_pending(char *url_out, size_t url_len, uint32_t *generation_out) {
    if (!url_out || url_len == 0) return -1;
    for (int i = 0; i < kMaxJobs; ++i) {
        if (!jobs_[i].active || jobs_[i].taken) continue;
        jobs_[i].taken = true;
        std::strncpy(url_out, jobs_[i].url, url_len - 1);
        url_out[url_len - 1] = '\0';
        if (generation_out) *generation_out = jobs_[i].generation;
        return i;
    }
    return -1;
}

bool tessera::DeviceHttp::complete(int job, HttpStatus status, const char *body, size_t len) {
    if (job < 0 || job >= kMaxJobs || !jobs_[job].active) return false;
    Job snap = jobs_[job];
    jobs_[job] = {};
    HttpResult result;
    result.status = status;
    result.body = body;
    result.len = len;
    snap.cb(snap.user, snap.generation, result);
    return true;
}

tessera::DeviceHttp &tessera::device_http() { return g_http; }
