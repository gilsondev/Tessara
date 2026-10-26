#include "media_widget.h"

#include "ui/media_page.h"

#include <lvgl.h>

namespace {

const tessera::WidgetManifest kManifest = {
    "org.tessera.media",
    "Música",
    160,
    100,
    false,
    tessera::HomeTile::Media,
    0xAF52DE,
    false,
};

class MediaWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessera::WidgetContext &context) override {
        generation_ = context.generation();
        if (context.logger()) context.logger()->info("media.mount");
        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (parent) media_page::mount(parent);
        else media_page::show();
        media_page::paint(media_page::last_view(), last_agent_ok_);
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        media_page::unmount();
        mounted_ = false;
        generation_ = 0;
    }

    void paint(const media::View &view, bool agent_ok) {
        last_agent_ok_ = agent_ok;
        if (mounted_) media_page::paint(view, agent_ok);
    }

    void apply_theme() {
        if (mounted_) media_page::apply_theme();
    }

private:
    bool mounted_ = false;
    bool last_agent_ok_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessera::media_widget_id() { return kManifest.id; }

tessera::TsWidget *tessera::create_media_widget() { return new MediaWidget(); }

void tessera::media_widget_paint(TsWidget *widget, const media::View &view, bool agent_ok) {
    if (!widget) return;
    static_cast<MediaWidget *>(widget)->paint(view, agent_ok);
}

void tessera::media_widget_apply_theme(TsWidget *widget) {
    if (!widget) return;
    static_cast<MediaWidget *>(widget)->apply_theme();
}
