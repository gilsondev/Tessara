#pragma once

#include "tessera/services.h"

#include <stddef.h>
#include <stdint.h>

// Tessera Widget SDK v0 — mirrors Terrara WIDGET-SDK.md.
// No Arduino / board / Wi-Fi headers. LVGL stays behind WidgetContext::ui_parent().
namespace tessera {

// How the home grid should render this type (package decides; shell only switches on the enum).
enum class HomeTile : uint8_t {
    None = 0,
    Generic = 1,
    Todoist = 2,
    Weather = 3,
    AiTokens = 4,
    Calendar = 5,
    Mac = 6,
    Media = 7,
};

struct WidgetManifest {
    const char *id;     // e.g. "org.tessera.weather"
    const char *title;  // UI label
    uint16_t min_width_px;
    uint16_t min_height_px;
    // Hero-only packages (greeting/date): registered for compose, never open a detail screen.
    bool hero_only;
    HomeTile home_tile;
    uint32_t accent_rgb;  // 0 = use shell primary
    // Opt-in: portal/settings expose S–M–L–XL home grid height (`home_tile_size`).
    bool supports_home_tile_size;
};

// Non-owning; valid only while the instance is mounted.
class WidgetContext {
public:
    WidgetContext(void *ui_parent, uint32_t generation, const char *instance_id, HostServices *services)
        : ui_parent_(ui_parent), generation_(generation), instance_id_(instance_id), services_(services) {}

    void *ui_parent() const { return ui_parent_; }
    uint32_t generation() const { return generation_; }
    const char *instance_id() const { return instance_id_ ? instance_id_ : ""; }

    ITime *time() const { return services_ ? services_->time : nullptr; }
    IHttp *http() const { return services_ ? services_->http : nullptr; }
    IScheduler *scheduler() const { return services_ ? services_->scheduler : nullptr; }
    ILogger *logger() const { return services_ ? services_->logger : nullptr; }

    // Namespaced get/set: stores as "<instance_id>/<key>".
    bool settings_get(const char *key, char *out, size_t out_len) const;
    bool settings_set(const char *key, const char *value) const;

private:
    void *ui_parent_;
    uint32_t generation_;
    const char *instance_id_;
    HostServices *services_;

    bool make_key(const char *key, char *out, size_t out_len) const;
};

class TsWidget {
public:
    virtual ~TsWidget() = default;
    virtual const WidgetManifest &manifest() const = 0;
    // Called on the UI executor. Parent container is owned by the shell/runtime.
    virtual void mount(WidgetContext &context) = 0;
    virtual void on_resize(uint16_t width, uint16_t height) = 0;
    virtual void unmount() = 0;
};

}  // namespace tessera
