#include "media_widget.h"

#include "tessera/runtime.h"
#include "ui/media_page.h"
#include "ui/media_tile.h"

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
    true,
};

class MediaWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessera::WidgetContext &context) override {
        generation_ = context.generation();
        surface_ = context.surface();
        if (context.logger()) context.logger()->info("media.mount");
        auto *parent = static_cast<lv_obj_t *>(context.ui_parent());
        if (surface_ == tessera::Surface::Tile) {
            auto open = reinterpret_cast<lv_event_cb_t>(tessera::runtime().tile_open());
            media_tile::add(parent, open);
        } else {
            auto back = reinterpret_cast<lv_event_cb_t>(tessera::runtime().detail_back());
            media_page::begin(back, tessera::runtime().page_command());
            if (parent) media_page::mount(parent);
            else media_page::show();
            media_page::paint(media_page::last_view(), last_agent_ok_);
        }
        mounted_ = true;
        if (context.data()) {
            context.data()->subscribe(tessera::TopicId::Media, generation_, on_data, this);
            const tessera::DataRef latest = context.data()->latest(tessera::TopicId::Media);
            on_data(latest, this);
        }
    }

    static void on_data(const tessera::DataRef &ref, void *user) {
        if (!ref.ptr || ref.size != sizeof(media::Published)) return;
        const auto *snap = static_cast<const media::Published *>(ref.ptr);
        static_cast<MediaWidget *>(user)->paint(snap->view, snap->agent_ok);
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        if (surface_ == tessera::Surface::Tile) media_tile::detach();
        else media_page::unmount();
        mounted_ = false;
        generation_ = 0;
    }

    void on_theme() override { apply_theme(); }

    void paint(const media::View &view, bool agent_ok) {
        last_agent_ok_ = agent_ok;
        if (!mounted_) return;
        if (surface_ == tessera::Surface::Tile) media_tile::set(view, agent_ok);
        else media_page::paint(view, agent_ok);
    }

    void apply_theme() {
        if (!mounted_) return;
        if (surface_ == tessera::Surface::Tile) media_tile::apply_theme();
        else media_page::apply_theme();
    }

private:
    bool mounted_ = false;
    bool last_agent_ok_ = false;
    uint32_t generation_ = 0;
    tessera::Surface surface_ = tessera::Surface::Detail;
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
