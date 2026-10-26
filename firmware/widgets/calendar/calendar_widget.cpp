#include "calendar_widget.h"

#include "ui/calendar_page.h"

#include <lvgl.h>

namespace {

const tessera::WidgetManifest kManifest = {
    "org.tessera.calendar",
    "Calendário",
    160,
    120,
    false,
    tessera::HomeTile::Calendar,
    0x1A73E8,
    true,  // supports_home_tile_size
};

class CalendarWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessera::WidgetContext &context) override {
        generation_ = context.generation();
        if (context.logger()) context.logger()->info("calendar.mount");
        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (parent) calendar_page::mount(parent);
        else calendar_page::show();
        // Day cards past tomorrow need a weekday name; the SDK clock is the only source here.
        calendar_page::set_weekday(context.time() ? context.time()->now().wday : -1);
        calendar_page::paint(calendar_page::last_view());
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        calendar_page::unmount();
        mounted_ = false;
        generation_ = 0;
    }

    void paint(const calendar::View &view) {
        if (mounted_) calendar_page::paint(view);
    }

    void apply_theme() {
        if (mounted_) calendar_page::apply_theme();
    }

private:
    bool mounted_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessera::calendar_widget_id() { return kManifest.id; }

tessera::TsWidget *tessera::create_calendar_widget() { return new CalendarWidget(); }

void tessera::calendar_widget_paint(TsWidget *widget, const calendar::View &view) {
    if (!widget) return;
    static_cast<CalendarWidget *>(widget)->paint(view);
}

void tessera::calendar_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<CalendarWidget *>(widget)->apply_theme();
}
