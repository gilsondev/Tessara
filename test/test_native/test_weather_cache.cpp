#include "weather/cache.h"

#include "domain/dashboard_layout.h"
#include "hal/board.h"
#include "platform/weather_cache_store.h"
#include "platform/widget_settings.h"

#include <cstring>
#include <ctime>
#include <map>
#include <string>

#include <unity.h>

namespace {

class MemStorage final : public hal::StoragePort {
public:
    bool get(const char *ns, const char *key, char *out, size_t out_len) override {
        if (!out || out_len == 0) return false;
        out[0] = '\0';
        auto found = values.find(id(ns, key));
        if (found == values.end()) return false;
        std::strncpy(out, found->second.c_str(), out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    bool set(const char *ns, const char *key, const char *value) override {
        values[id(ns, key)] = value ? value : "";
        return true;
    }
    bool get_u32(const char *, const char *, uint32_t *) override { return false; }
    bool set_u32(const char *, const char *, uint32_t) override { return false; }
    bool get_bool(const char *, const char *, bool *) override { return false; }
    bool set_bool(const char *, const char *, bool) override { return false; }
    bool get_f32(const char *, const char *, float *) override { return false; }
    bool set_f32(const char *, const char *, float) override { return false; }
    bool erase(const char *ns, const char *key) override { return values.erase(id(ns, key)) > 0; }
    bool clear_ns(const char *ns) override {
        const std::string prefix = std::string(ns ? ns : "") + "\n";
        for (auto it = values.begin(); it != values.end();) {
            if (it->first.rfind(prefix, 0) == 0) it = values.erase(it);
            else ++it;
        }
        return true;
    }
    void clear() { values.clear(); }

private:
    static std::string id(const char *ns, const char *key) {
        return std::string(ns ? ns : "") + "\n" + (key ? key : "");
    }
    std::map<std::string, std::string> values;
};

MemStorage g_storage;

}  // namespace

hal::Board &hal::board() {
    static hal::BoardDescriptor descriptor{"test", "test", 320, 480, hal::Rotation::R0, hal::Rotation::R90};
    static hal::Board board{descriptor, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &g_storage, {}};
    return board;
}

namespace {

char kept_forecast[512];

bool load_kept(char *out, size_t out_len) {
    if (!kept_forecast[0] || out_len == 0) return false;
    std::strncpy(out, kept_forecast, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

bool save_kept(const char *blob) {
    std::strncpy(kept_forecast, blob, sizeof(kept_forecast) - 1);
    kept_forecast[sizeof(kept_forecast) - 1] = '\0';
    return true;
}

}  // namespace

void test_weather_retry_backs_off_to_thirty_minutes(void) {
    TEST_ASSERT_EQUAL_UINT32(30000u, weather::next_retry_ms(0));
    TEST_ASSERT_EQUAL_UINT32(60000u, weather::next_retry_ms(1));
    TEST_ASSERT_EQUAL_UINT32(120000u, weather::next_retry_ms(2));
    TEST_ASSERT_EQUAL_UINT32(1800000u, weather::next_retry_ms(6));
    TEST_ASSERT_EQUAL_UINT32(1800000u, weather::next_retry_ms(12));
}

void test_weather_retry_stays_open_after_the_millis_sign_bit(void) {
    TEST_ASSERT_TRUE(weather::retry_allowed(0x90000000u, false, 0));
    TEST_ASSERT_FALSE(weather::retry_allowed(1000u, true, 31000u));
    TEST_ASSERT_TRUE(weather::retry_allowed(31000u, true, 31000u));
    TEST_ASSERT_TRUE(weather::retry_allowed(0x90000000u, true, 0x90000000u - 30000u));
}

void test_saved_forecast_reloads_ready_and_stale(void) {
    kept_forecast[0] = '\0';
    weather::set_blob_io(load_kept, save_kept);
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    weather::Forecast forecast = {};
    forecast.temperature_c = 18;
    const int64_t old = static_cast<int64_t>(std::time(nullptr)) - 31 * 60;
    const bool stored = weather::store_at(weather::note_request(), forecast, 0, old);
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    weather::View seen = weather::view(0);
    const bool ready = seen.ready;
    const bool stale = seen.stale;
    const int temperature = seen.forecast.temperature_c;
    weather::set_place("Curitiba", -25.43f, -49.27f);
    const bool other_ready = weather::view(0).ready;
    weather::set_blob_io(nullptr, nullptr);
    weather::reset();
    TEST_ASSERT_TRUE(stored);
    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_TRUE(stale);
    TEST_ASSERT_EQUAL(18, temperature);
    TEST_ASSERT_FALSE(other_ready);
}

void test_forecast_cache_stays_out_of_widget_settings(void) {
    g_storage.clear();
    TEST_ASSERT_TRUE(widget_settings::set(dashboard::id_weather(), "forecast", "legacy-forecast"));
    weather_cache_store::migrate_from_settings();
    char settings_buf[64];
    const bool legacy_gone =
        !widget_settings::get(dashboard::id_weather(), "forecast", settings_buf, sizeof(settings_buf));
    char moved[64];
    const bool moved_ok = weather_cache_store::load(moved, sizeof(moved));
    const bool moved_text = moved_ok && std::strcmp(moved, "legacy-forecast") == 0;
    g_storage.clear();
    weather::set_blob_io(weather_cache_store::load, weather_cache_store::save);
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    weather::Forecast forecast = {};
    forecast.temperature_c = 18;
    const bool stored = weather::store(weather::note_request(), forecast, 0);
    const bool still_absent =
        !widget_settings::get(dashboard::id_weather(), "forecast", settings_buf, sizeof(settings_buf));
    char blob[512];
    const bool cached = weather_cache_store::load(blob, sizeof(blob));
    const bool has_temp = cached && std::strstr(blob, ",18,") != nullptr;
    g_storage.clear();
    const bool saved_new = weather_cache_store::save("new-forecast");
    const bool saved_legacy = widget_settings::set(dashboard::id_weather(), "forecast", "legacy-forecast");
    weather_cache_store::migrate_from_settings();
    char kept[64];
    const bool kept_new = weather_cache_store::load(kept, sizeof(kept)) && std::strcmp(kept, "new-forecast") == 0;
    const bool legacy_removed =
        !widget_settings::get(dashboard::id_weather(), "forecast", settings_buf, sizeof(settings_buf));
    weather::set_blob_io(nullptr, nullptr);
    weather::reset();
    g_storage.clear();
    TEST_ASSERT_TRUE(legacy_gone);
    TEST_ASSERT_TRUE(moved_text);
    TEST_ASSERT_TRUE(stored);
    TEST_ASSERT_TRUE(still_absent);
    TEST_ASSERT_TRUE(has_temp);
    TEST_ASSERT_TRUE(saved_new);
    TEST_ASSERT_TRUE(saved_legacy);
    TEST_ASSERT_TRUE(kept_new);
    TEST_ASSERT_TRUE(legacy_removed);
}

void test_a_shifted_forecast_day_is_fetched_again(void) {
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    weather::note_today("2026-10-26");
    weather::Forecast forecast = {};
    forecast.temperature_c = 20;
    std::strncpy(forecast.days[0].date, "2026-10-26", sizeof(forecast.days[0].date));
    std::strncpy(forecast.days[1].date, "2026-10-27", sizeof(forecast.days[1].date));
    const bool stored = weather::store(weather::note_request(), forecast, 0);
    weather::note_today("2026-10-27");
    const int offset = weather::day_offset(forecast, "2026-10-27");
    const bool fetch = weather::wants_fetch(0);
    const bool again = weather::store(weather::note_request(), forecast, 1000);
    const bool spinning = weather::wants_fetch(1000);
    const bool stale = weather::view(1000).stale;
    weather::reset();
    TEST_ASSERT_TRUE(stored);
    TEST_ASSERT_EQUAL(1, offset);
    TEST_ASSERT_TRUE(fetch);
    TEST_ASSERT_TRUE(again);
    TEST_ASSERT_FALSE(spinning);
    TEST_ASSERT_TRUE(stale);
}

void test_a_repeated_wrong_day_is_fetched_once_per_half_hour(void) {
    weather::reset();
    weather::set_place("Lisboa", 38.72f, -9.14f);
    weather::note_today("2026-10-28");
    weather::note_hour(0);
    weather::Forecast forecast = {};
    forecast.temperature_c = 16;
    std::strncpy(forecast.days[0].date, "2026-10-27", sizeof(forecast.days[0].date));
    std::strncpy(forecast.days[1].date, "2026-10-28", sizeof(forecast.days[1].date));
    int opens = 0;
    for (int i = 0; i < 10; ++i) {
        const uint32_t now = static_cast<uint32_t>(i) * 1000u;
        if (weather::wants_fetch(now)) ++opens;
        TEST_ASSERT_TRUE(weather::store(weather::note_request(), forecast, now));
    }
    weather::note_hour(1);
    const bool same_hour_window = weather::wants_fetch(5 * 60 * 1000);
    const bool still_fresh = weather::wants_fetch(10 * 60 * 1000);
    const bool stale_again = weather::wants_fetch(31 * 60 * 1000);
    const bool marked_stale = weather::view(10 * 60 * 1000).stale;
    weather::reset();
    TEST_ASSERT_EQUAL(1, opens);
    TEST_ASSERT_FALSE(same_hour_window);
    TEST_ASSERT_FALSE(still_fresh);
    TEST_ASSERT_TRUE(marked_stale);
    TEST_ASSERT_TRUE(stale_again);
}

void test_without_a_place_nothing_is_fetched(void) {
    weather::reset();
    TEST_ASSERT_FALSE(weather::wants_fetch(0));
    weather::View seen = weather::view(0);
    TEST_ASSERT_FALSE(seen.placed);
    TEST_ASSERT_FALSE(seen.ready);
}

void test_cache_is_fresh_at_ten_minutes_and_stale_at_thirty_one(void) {
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    TEST_ASSERT_TRUE(weather::wants_fetch(0));
    weather::Forecast forecast = {};
    forecast.temperature_c = 21;
    forecast.code = 0;
    const uint32_t generation = weather::note_request();
    TEST_ASSERT_TRUE(weather::store(generation, forecast, 0));
    weather::View fresh = weather::view(10 * 60 * 1000);
    TEST_ASSERT_TRUE(fresh.ready);
    TEST_ASSERT_FALSE(fresh.stale);
    TEST_ASSERT_FALSE(weather::wants_fetch(10 * 60 * 1000));
    TEST_ASSERT_EQUAL(21, fresh.forecast.temperature_c);
    weather::View stale = weather::view(31 * 60 * 1000);
    TEST_ASSERT_TRUE(stale.stale);
    TEST_ASSERT_TRUE(weather::wants_fetch(31 * 60 * 1000));
    TEST_ASSERT_EQUAL(21, stale.forecast.temperature_c);
}

void test_an_older_generation_does_not_replace_the_new_city(void) {
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    const uint32_t first = weather::note_request();
    weather::set_place("Curitiba", -25.43f, -49.27f);
    weather::Forecast forecast = {};
    forecast.temperature_c = 9;
    TEST_ASSERT_FALSE(weather::store(first, forecast, 1000));
    weather::View seen = weather::view(1000);
    TEST_ASSERT_EQUAL_STRING("Curitiba", seen.name);
    TEST_ASSERT_FALSE(seen.ready);
    TEST_ASSERT_EQUAL(0, seen.forecast.temperature_c);
}
