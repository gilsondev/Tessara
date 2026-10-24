#include "date_widget.h"

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.date",
    "Data",
    0,
    0,
    true,
    tessara::HomeTile::None,
    0,
};

// Hero-only: shell paints via clock_face; no detail mount.
class DateWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }
    void mount(tessara::WidgetContext &ctx) override {
        generation_ = ctx.generation();
        if (ctx.logger()) ctx.logger()->info("date.mount");
        mounted_ = true;
    }
    void on_resize(uint16_t, uint16_t) override {}
    void unmount() override {
        mounted_ = false;
        generation_ = 0;
    }

private:
    bool mounted_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessara::date_widget_id() { return kManifest.id; }
tessara::TsWidget *tessara::create_date_widget() { return new DateWidget(); }
