#include "tessara/memory_services.h"

#include <cstdio>
#include <cstring>

namespace {

bool key_eq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

}  // namespace

bool tessara::MemorySettings::get(const char *key, char *out, size_t out_len) const {
    if (!key || !out || out_len == 0) return false;
    out[0] = '\0';
    for (int i = 0; i < kMaxEntries; ++i) {
        if (!entries_[i].used) continue;
        if (!key_eq(entries_[i].key, key)) continue;
        std::strncpy(out, entries_[i].value, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

bool tessara::MemorySettings::set(const char *key, const char *value) {
    if (!key || !key[0] || !value) return false;
    int free_slot = -1;
    for (int i = 0; i < kMaxEntries; ++i) {
        if (entries_[i].used && key_eq(entries_[i].key, key)) {
            std::strncpy(entries_[i].value, value, kValLen - 1);
            entries_[i].value[kValLen - 1] = '\0';
            return true;
        }
        if (!entries_[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return false;
    std::strncpy(entries_[free_slot].key, key, kKeyLen - 1);
    entries_[free_slot].key[kKeyLen - 1] = '\0';
    std::strncpy(entries_[free_slot].value, value, kValLen - 1);
    entries_[free_slot].value[kValLen - 1] = '\0';
    entries_[free_slot].used = true;
    return true;
}

void tessara::MemorySettings::clear() {
    for (int i = 0; i < kMaxEntries; ++i) entries_[i] = {};
}

int tessara::MemoryHttp::get(const char *url, uint32_t generation, HttpCallback cb, void *user) {
    if (!url || !cb || generation == 0) return -1;
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active) continue;
        jobs_[i].active = true;
        jobs_[i].generation = generation;
        jobs_[i].cb = cb;
        jobs_[i].user = user;
        std::strncpy(jobs_[i].url, url, sizeof(jobs_[i].url) - 1);
        jobs_[i].url[sizeof(jobs_[i].url) - 1] = '\0';
        return i;
    }
    return -1;
}

void tessara::MemoryHttp::cancel(int job) {
    if (job < 0 || job >= kMaxJobs) return;
    jobs_[job] = {};
}

void tessara::MemoryHttp::cancel_generation(uint32_t generation) {
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active && jobs_[i].generation == generation) jobs_[i] = {};
    }
}

bool tessara::MemoryHttp::complete(int job, HttpStatus status, const char *body, size_t len) {
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

int tessara::MemoryHttp::pending_count() const {
    int n = 0;
    for (int i = 0; i < kMaxJobs; ++i)
        if (jobs_[i].active) ++n;
    return n;
}

int tessara::MemoryScheduler::every_ms(uint32_t period_ms, uint32_t generation, JobCallback cb, void *user) {
    if (!cb || generation == 0 || period_ms == 0) return -1;
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active) continue;
        jobs_[i].active = true;
        jobs_[i].generation = generation;
        jobs_[i].period_ms = period_ms;
        jobs_[i].cb = cb;
        jobs_[i].user = user;
        return i;
    }
    return -1;
}

void tessara::MemoryScheduler::cancel(int job) {
    if (job < 0 || job >= kMaxJobs) return;
    jobs_[job] = {};
}

void tessara::MemoryScheduler::cancel_generation(uint32_t generation) {
    for (int i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active && jobs_[i].generation == generation) jobs_[i] = {};
    }
}

bool tessara::MemoryScheduler::tick(int job) {
    if (job < 0 || job >= kMaxJobs || !jobs_[job].active) return false;
    Job snap = jobs_[job];
    snap.cb(snap.user, snap.generation);
    return jobs_[job].active;  // still active unless cancel during cb
}

int tessara::MemoryScheduler::active_count() const {
    int n = 0;
    for (int i = 0; i < kMaxJobs; ++i)
        if (jobs_[i].active) ++n;
    return n;
}

void tessara::MemoryLogger::push(char tag, const char *msg) {
    if (count_ >= kMaxLines) return;
    std::snprintf(lines_[count_], kLineLen, "%c:%s", tag, msg ? msg : "");
    ++count_;
}

void tessara::MemoryLogger::info(const char *msg) { push('I', msg); }

void tessara::MemoryLogger::warn(const char *msg) { push('W', msg); }

void tessara::MemoryLogger::clear() {
    count_ = 0;
    for (int i = 0; i < kMaxLines; ++i) lines_[i][0] = '\0';
}

const char *tessara::MemoryLogger::line(int i) const {
    if (i < 0 || i >= count_) return "";
    return lines_[i];
}

tessara::HostServices tessara::MemoryHost::bag() {
    HostServices s;
    s.time = &time;
    s.settings = &settings;
    s.http = &http;
    s.scheduler = &scheduler;
    s.logger = &logger;
    return s;
}
