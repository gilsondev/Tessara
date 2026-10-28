#include "weather/cache.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace weather {
namespace {

constexpr uint32_t kFreshMs = 1800000;
constexpr int64_t kFreshS = 1800;
constexpr int64_t kClockOk = 1700000000;

struct State {
    bool placed = false;
    bool ready = false;
    bool failed = false;
    uint32_t generation = 0;
    uint32_t stored_ms = 0;
    int64_t epoch_s = 0;
    char name[48] = {};
    float latitude = 0;
    float longitude = 0;
    Forecast forecast = {};
};

char disk[512];
bool disk_set = false;
char today_buf[11] = {};
int64_t last_flush_epoch = 0;
uint32_t last_date_refetch_ms = 0;
bool date_refetch_set = false;
bool date_mismatch_seen = false;
int clock_hour = -1;
int mismatch_hour = -1;
BlobLoad load_fn = nullptr;
BlobSave save_fn = nullptr;

bool mem_load(char *out, size_t out_len) {
    if (!disk_set || !out || out_len == 0) return false;
    std::strncpy(out, disk, out_len - 1);
    out[out_len - 1] = '\0';
    return out[0] != '\0';
}

bool mem_save(const char *blob) {
    if (!blob) return false;
    std::strncpy(disk, blob, sizeof(disk) - 1);
    disk[sizeof(disk) - 1] = '\0';
    disk_set = true;
    return true;
}

bool wall_fresh(int64_t epoch_s) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (epoch_s < kClockOk || now < kClockOk || now < epoch_s) return false;
    return now - epoch_s < kFreshS;
}

State state;

void copy_name(char *dest, size_t cap, const char *src) {
    size_t n = 0;
    if (src) {
        while (src[n] && n + 1 < cap) {
            dest[n] = src[n];
            ++n;
        }
    }
    dest[n] = '\0';
}

bool dates_match_today() {
    if (!today_buf[0] || !state.ready) return true;
    return std::strcmp(state.forecast.days[0].date, today_buf) == 0;
}

bool fresh(uint32_t now_ms) {
    if (!state.ready || !wall_fresh(state.epoch_s)) return false;
    if (now_ms < state.stored_ms) return false;
    return now_ms - state.stored_ms < kFreshMs;
}

bool same_place(float latitude, float longitude) {
    return std::fabs(latitude - state.latitude) < 0.001f && std::fabs(longitude - state.longitude) < 0.001f;
}

bool encode_blob(char *out, size_t cap, int64_t epoch_s) {
    int used = std::snprintf(out, cap, "1,%lld,%.4f,%.4f,%d,%d", static_cast<long long>(epoch_s),
                             state.latitude, state.longitude, state.forecast.temperature_c, state.forecast.code);
    if (used < 0 || static_cast<size_t>(used) >= cap) return false;
    for (const Day &day : state.forecast.days) {
        const char *date = day.date[0] ? day.date : "-";
        int next = std::snprintf(out + used, cap - static_cast<size_t>(used), ",%.10s,%d,%d,%d", date,
                                 day.temperature_min_c, day.temperature_max_c, day.code);
        if (next < 0 || static_cast<size_t>(used + next) >= cap) return false;
        used += next;
    }
    return true;
}

bool decode_blob(const char *blob, Forecast *forecast, float *latitude, float *longitude, int64_t *epoch_s) {
    long long epoch = 0;
    float lat = 0;
    float lon = 0;
    int temp = 0;
    int code = 0;
    int used = 0;
    if (std::sscanf(blob, "1,%lld,%f,%f,%d,%d%n", &epoch, &lat, &lon, &temp, &code, &used) != 5) return false;
    Forecast parsed = {};
    parsed.temperature_c = temp;
    parsed.code = code;
    const char *cursor = blob + used;
    for (Day &day : parsed.days) {
        int min_c = 0;
        int max_c = 0;
        int day_code = 0;
        int step = 0;
        if (std::sscanf(cursor, ",%10[^,],%d,%d,%d%n", day.date, &min_c, &max_c, &day_code, &step) != 4) return false;
        if (day.date[0] == '-' && day.date[1] == '\0') day.date[0] = '\0';
        day.temperature_min_c = min_c;
        day.temperature_max_c = max_c;
        day.code = day_code;
        cursor += step;
    }
    *forecast = parsed;
    *latitude = lat;
    *longitude = lon;
    *epoch_s = epoch;
    return true;
}

void flush_blob(int64_t epoch_s) {
    if (last_flush_epoch != 0 && epoch_s >= last_flush_epoch && epoch_s - last_flush_epoch < kFreshS) return;
    char blob[sizeof(disk)];
    if (!encode_blob(blob, sizeof(blob), epoch_s)) return;
    BlobSave save = save_fn ? save_fn : mem_save;
    if (save(blob)) last_flush_epoch = epoch_s;
}

void load_blob() {
    char blob[sizeof(disk)];
    blob[0] = '\0';
    BlobLoad load = load_fn ? load_fn : mem_load;
    if (!load(blob, sizeof(blob)) || !blob[0]) return;
    Forecast forecast = {};
    float latitude = 0;
    float longitude = 0;
    int64_t epoch_s = 0;
    if (!decode_blob(blob, &forecast, &latitude, &longitude, &epoch_s)) return;
    if (!same_place(latitude, longitude)) return;
    state.forecast = forecast;
    state.ready = true;
    state.failed = false;
    state.epoch_s = epoch_s;
    state.stored_ms = 0;
    last_flush_epoch = epoch_s;
}

}  // namespace

void reset() {
    state = {};
    today_buf[0] = '\0';
    last_flush_epoch = 0;
    last_date_refetch_ms = 0;
    date_refetch_set = false;
    date_mismatch_seen = false;
    clock_hour = -1;
    mismatch_hour = -1;
    if (!load_fn && !save_fn) {
        disk[0] = '\0';
        disk_set = false;
    }
}

void set_blob_io(BlobLoad load, BlobSave save) {
    load_fn = load;
    save_fn = save;
}

void set_place(const char *name, float latitude, float longitude) {
    state.placed = true;
    copy_name(state.name, sizeof(state.name), name);
    state.latitude = latitude;
    state.longitude = longitude;
    state.forecast = {};
    state.ready = false;
    state.failed = false;
    state.stored_ms = 0;
    state.epoch_s = 0;
    state.generation += 1;
    last_date_refetch_ms = 0;
    date_refetch_set = false;
    date_mismatch_seen = false;
    mismatch_hour = -1;
    load_blob();
}

uint32_t note_request() { return state.generation; }

bool store_at(uint32_t generation, const Forecast &forecast, uint32_t now_ms, int64_t epoch_s) {
    if (!state.placed || generation != state.generation) return false;
    state.forecast = forecast;
    state.ready = true;
    state.failed = false;
    state.stored_ms = now_ms;
    state.epoch_s = epoch_s > 0 ? epoch_s : static_cast<int64_t>(std::time(nullptr));
    if (today_buf[0] && std::strcmp(state.forecast.days[0].date, today_buf) != 0) {
        date_mismatch_seen = true;
        date_refetch_set = true;
        last_date_refetch_ms = now_ms;
        mismatch_hour = clock_hour;
    } else {
        date_mismatch_seen = false;
        date_refetch_set = false;
    }
    flush_blob(state.epoch_s);
    return true;
}

bool store(uint32_t generation, const Forecast &forecast, uint32_t now_ms) {
    return store_at(generation, forecast, now_ms, static_cast<int64_t>(std::time(nullptr)));
}

void note_failure() {
    if (!state.placed) return;
    state.failed = true;
}

View view(uint32_t now_ms) {
    View seen = {};
    seen.placed = state.placed;
    seen.ready = state.ready;
    seen.stale = state.ready && (!fresh(now_ms) || !dates_match_today());
    seen.failed = state.failed;
    copy_name(seen.name, sizeof(seen.name), state.name);
    seen.forecast = state.forecast;
    return seen;
}

void note_today(const char *yyyy_mm_dd) {
    const char *src = yyyy_mm_dd ? yyyy_mm_dd : "";
    char next[sizeof(today_buf)] = {};
    size_t n = 0;
    while (src[n] && n + 1 < sizeof(next)) {
        next[n] = src[n];
        ++n;
    }
    next[n] = '\0';
    if (std::strcmp(today_buf, next) != 0) {
        date_refetch_set = false;
        date_mismatch_seen = false;
    }
    std::memcpy(today_buf, next, sizeof(today_buf));
}

void note_hour(int hour) { clock_hour = hour; }

const char *today() { return today_buf; }

int day_offset(const Forecast &forecast, const char *today_date) {
    if (!today_date || !today_date[0]) return -1;
    for (int i = 0; i < 7; ++i) {
        if (std::strcmp(forecast.days[i].date, today_date) == 0) return i;
    }
    return -1;
}

bool wants_fetch(uint32_t now_ms) {
    if (!state.placed) return false;
    if (!fresh(now_ms)) return true;
    if (dates_match_today()) return false;
    // One extra fetch when the saved day is not today. A reply that repeats that day
    // waits for the next hour and for 30 minutes, so a timezone gap cannot spin.
    if (!date_refetch_set) return true;
    const bool hour_changed = clock_hour >= 0 && mismatch_hour >= 0 && clock_hour != mismatch_hour;
    if (date_mismatch_seen && !hour_changed) return false;
    if (now_ms < last_date_refetch_ms) return true;
    return now_ms - last_date_refetch_ms >= kFreshMs;
}

uint32_t next_retry_ms(uint32_t failures) {
    uint32_t shift = failures < 6 ? failures : 6;
    uint32_t delay = 30000u << shift;
    return delay > 1800000u ? 1800000u : delay;
}

bool retry_allowed(uint32_t now_ms, bool backoff, uint32_t next_ms) {
    if (!backoff) return true;
    return static_cast<int32_t>(now_ms - next_ms) >= 0;
}

bool coords(float *latitude, float *longitude) {
    if (!state.placed) return false;
    if (latitude) *latitude = state.latitude;
    if (longitude) *longitude = state.longitude;
    return true;
}

}  // namespace weather
