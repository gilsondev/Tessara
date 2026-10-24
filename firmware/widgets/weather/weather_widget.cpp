#include "weather_widget.h"

#include "tessara/host_clock.h"
#include "ui/weather_page.h"
#include "weather/cache.h"
#include "weather/http_contract.h"

#include <lvgl.h>

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.weather",
    "Clima",
    140,
    100,
    false,
    tessara::HomeTile::Weather,
    0x3D9AE8,
};

class WeatherWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessara::WidgetContext &context) override {
        generation_ = context.generation();
        cache_gen_ = 0;
        if (context.logger()) context.logger()->info("weather.mount");

        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (parent) weather_page::mount(parent);
        else weather_page::show();

        weather_page::paint(weather::view(tessara::host_now_ms()));
        mounted_ = true;

        float lat = 0, lon = 0;
        if (!weather::coords(&lat, &lon)) return;
        if (!context.http()) return;

        char url[400];
        if (!weather::forecast_url(lat, lon, url, sizeof(url))) return;
        cache_gen_ = weather::note_request();
        context.http()->get(url, generation_, on_http, this);
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        weather_page::unmount();
        mounted_ = false;
        generation_ = 0;
        cache_gen_ = 0;
    }

    void paint(const weather::View &view) {
        if (mounted_) weather_page::paint(view);
    }

    void apply_theme() {
        if (mounted_) weather_page::apply_theme();
    }

private:
    static void on_http(void *user, uint32_t generation, const tessara::HttpResult &result) {
        auto *self = static_cast<WeatherWidget *>(user);
        if (!self || !self->mounted_ || generation != self->generation_) return;
        const uint32_t now = tessara::host_now_ms();
        if (result.status != tessara::HttpStatus::Ok || !result.body) {
            weather::note_failure();
            weather_page::paint(weather::view(now));
            return;
        }
        if (!weather::accept_http_forecast(self->generation_, generation, self->cache_gen_, result.body, now)) {
            weather::note_failure();
        }
        weather_page::paint(weather::view(now));
    }

    bool mounted_ = false;
    uint32_t generation_ = 0;
    uint32_t cache_gen_ = 0;
};

}  // namespace

const char *tessara::weather_widget_id() { return kManifest.id; }

tessara::TsWidget *tessara::create_weather_widget() { return new WeatherWidget(); }

void tessara::weather_widget_paint(TsWidget *widget, const weather::View &view) {
    if (!widget) return;
    static_cast<WeatherWidget *>(widget)->paint(view);
}

void tessara::weather_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<WeatherWidget *>(widget)->apply_theme();
}
