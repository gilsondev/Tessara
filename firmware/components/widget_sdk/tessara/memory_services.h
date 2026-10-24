#pragma once

#include "tessara/services.h"

#include <stddef.h>
#include <stdint.h>

// In-memory host services for native tests (and optional sim stubs).
namespace tessara {

class MemoryTime final : public ITime {
public:
    TimeSnapshot snap{};
    TimeSnapshot now() const override { return snap; }
};

class MemorySettings final : public ISettings {
public:
    static constexpr int kMaxEntries = 32;
    static constexpr int kKeyLen = 96;
    static constexpr int kValLen = 128;

    bool get(const char *key, char *out, size_t out_len) const override;
    bool set(const char *key, const char *value) override;
    void clear();

private:
    struct Entry {
        char key[kKeyLen] = {};
        char value[kValLen] = {};
        bool used = false;
    };
    Entry entries_[kMaxEntries] = {};
};

class MemoryHttp final : public IHttp {
public:
    static constexpr int kMaxJobs = 8;

    int get(const char *url, uint32_t generation, HttpCallback cb, void *user) override;
    void cancel(int job) override;
    void cancel_generation(uint32_t generation) override;

    // Test harness: deliver a result to a pending job (no-op if canceled).
    bool complete(int job, HttpStatus status, const char *body, size_t len);
    int pending_count() const;

private:
    struct Job {
        bool active = false;
        uint32_t generation = 0;
        HttpCallback cb = nullptr;
        void *user = nullptr;
        char url[160] = {};
    };
    Job jobs_[kMaxJobs] = {};
};

class MemoryScheduler final : public IScheduler {
public:
    static constexpr int kMaxJobs = 8;

    int every_ms(uint32_t period_ms, uint32_t generation, JobCallback cb, void *user) override;
    void cancel(int job) override;
    void cancel_generation(uint32_t generation) override;

    bool tick(int job);  // fire once if still active
    int active_count() const;

private:
    struct Job {
        bool active = false;
        uint32_t generation = 0;
        uint32_t period_ms = 0;
        JobCallback cb = nullptr;
        void *user = nullptr;
    };
    Job jobs_[kMaxJobs] = {};
};

class MemoryLogger final : public ILogger {
public:
    static constexpr int kMaxLines = 16;
    static constexpr int kLineLen = 96;

    void info(const char *msg) override;
    void warn(const char *msg) override;
    void clear();
    int line_count() const { return count_; }
    const char *line(int i) const;

private:
    char lines_[kMaxLines][kLineLen] = {};
    int count_ = 0;
    void push(char tag, const char *msg);
};

struct MemoryHost {
    MemoryTime time;
    MemorySettings settings;
    MemoryHttp http;
    MemoryScheduler scheduler;
    MemoryLogger logger;

    HostServices bag();
};

}  // namespace tessara
