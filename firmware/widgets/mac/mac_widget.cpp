#include "mac_widget.h"

#include "ui/mac_page.h"

#include <lvgl.h>

namespace {

const tessera::WidgetManifest kManifest = {
    "org.tessera.mac",
    "Mac",
    160,
    100,
    false,
    tessera::HomeTile::Mac,
    0x5B8DEF,
    false,
};

class MacWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessera::WidgetContext &context) override {
        generation_ = context.generation();
        if (context.logger()) context.logger()->info("mac.mount");
        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (parent) mac_page::mount(parent);
        else mac_page::show();
        mac_page::paint(mac_page::last_view(), last_agent_ok_);
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        mac_page::unmount();
        mounted_ = false;
        generation_ = 0;
    }

    void paint(const focus::View &view, bool agent_ok) {
        last_agent_ok_ = agent_ok;
        if (mounted_) mac_page::paint(view, agent_ok);
    }

    void apply_theme() {
        if (mounted_) mac_page::apply_theme();
    }

private:
    bool mounted_ = false;
    bool last_agent_ok_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessera::mac_widget_id() { return kManifest.id; }

tessera::TsWidget *tessera::create_mac_widget() { return new MacWidget(); }

void tessera::mac_widget_paint(TsWidget *widget, const focus::View &view, bool agent_ok) {
    if (!widget) return;
    static_cast<MacWidget *>(widget)->paint(view, agent_ok);
}

void tessera::mac_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<MacWidget *>(widget)->apply_theme();
}
