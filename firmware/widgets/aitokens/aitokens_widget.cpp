#include "aitokens_widget.h"

#include "ui/aitokens_page.h"

#include <lvgl.h>

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.aitokens",
    "AI Tokens",
    160,
    120,
    false,
    tessara::HomeTile::AiTokens,
    0x8B5CF6,
};

class AiTokensWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessara::WidgetContext &context) override {
        generation_ = context.generation();
        if (context.logger()) context.logger()->info("aitokens.mount");
        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (parent) aitokens_page::attach(parent);
        else aitokens_page::show();
        aitokens_page::paint(aitokens_page::last_view());
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        aitokens_page::attach(nullptr);
        mounted_ = false;
        generation_ = 0;
    }

    void paint(const aitokens::View &view) {
        if (mounted_) aitokens_page::paint(view);
    }

    void apply_theme() {
        if (mounted_) aitokens_page::apply_theme();
    }

private:
    bool mounted_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessara::aitokens_widget_id() { return kManifest.id; }

tessara::TsWidget *tessara::create_aitokens_widget() { return new AiTokensWidget(); }

void tessara::aitokens_widget_paint(TsWidget *widget, const aitokens::View &view) {
    if (!widget) return;
    static_cast<AiTokensWidget *>(widget)->paint(view);
}

void tessara::aitokens_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<AiTokensWidget *>(widget)->apply_theme();
}
