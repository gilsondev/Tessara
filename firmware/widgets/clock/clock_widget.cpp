#include "clock_widget.h"

#include "ui/clock_page.h"

#include <lvgl.h>

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.clock",
    "Relógio",
    160,
    80,
    false,  // no grid tile; detalhe abre ao tocar a hora no hero
    tessara::HomeTile::None,
    0,
};

class ClockWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessara::WidgetContext &ctx) override {
        generation_ = ctx.generation();
        if (ctx.logger()) ctx.logger()->info("clock.mount");
        auto *parent = static_cast<lv_obj_t *>(ctx.ui_parent());
        if (parent) clock_page::mount(parent);
        else clock_page::show();
        if (ctx.time()) clock_page::paint(ctx.time()->now());
        else {
            tessara::TimeSnapshot unsynced;
            clock_page::paint(unsynced);
        }
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        clock_page::unmount();
        mounted_ = false;
        generation_ = 0;
    }

    void paint(const tessara::TimeSnapshot &snap) {
        if (mounted_) clock_page::paint(snap);
    }

    void apply_theme() {
        if (mounted_) clock_page::apply_theme();
    }

private:
    bool mounted_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessara::clock_widget_id() { return kManifest.id; }

tessara::TsWidget *tessara::create_clock_widget() { return new ClockWidget(); }

void tessara::clock_widget_paint(TsWidget *widget, const TimeSnapshot &snap) {
    if (!widget) return;
    static_cast<ClockWidget *>(widget)->paint(snap);
}

void tessara::clock_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<ClockWidget *>(widget)->apply_theme();
}
