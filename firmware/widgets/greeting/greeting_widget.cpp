#include "greeting_widget.h"

namespace {

const tessera::WidgetManifest kManifest = {
    "org.tessera.greeting",
    "Saudação",
    0,
    0,
    true,
    tessera::HomeTile::None,
    0,
    false,
};

// Hero-only: shell paints via clock_face; no detail mount.
class GreetingWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }
    void mount(tessera::WidgetContext &ctx) override {
        generation_ = ctx.generation();
        if (ctx.logger()) ctx.logger()->info("greeting.mount");
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

const char *tessera::greeting_widget_id() { return kManifest.id; }
tessera::TsWidget *tessera::create_greeting_widget() { return new GreetingWidget(); }
