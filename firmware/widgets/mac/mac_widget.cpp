#include "mac_widget.h"

#include "ui/mac_page.h"

#include <lvgl.h>

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.mac",
    "Mac",
    160,
    100,
    false,
    tessara::HomeTile::Mac,
    0x5B8DEF,
};

class MacWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessara::WidgetContext &context) override {
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

    void paint(const mac::View &view, bool agent_ok) {
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

const char *tessara::mac_widget_id() { return kManifest.id; }

tessara::TsWidget *tessara::create_mac_widget() { return new MacWidget(); }

void tessara::mac_widget_paint(TsWidget *widget, const mac::View &view, bool agent_ok) {
    if (!widget) return;
    static_cast<MacWidget *>(widget)->paint(view, agent_ok);
}

void tessara::mac_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<MacWidget *>(widget)->apply_theme();
}
