#pragma once

#include "tessara/services.h"
#include "tessara/widget.h"

#include <stddef.h>
#include <stdint.h>

// Compile-time style registry + mount lifecycle with generation tags.
namespace tessara {

constexpr int kMaxWidgetTypes = 16;
constexpr int kMaxInstances = 8;

using WidgetFactory = TsWidget *(*)();

struct RegistryEntry {
    char id[48] = {};
    char title[32] = {};
    bool hero_only = false;
    HomeTile home_tile = HomeTile::Generic;
    uint32_t accent_rgb = 0;
    WidgetFactory factory = nullptr;
};

class Runtime {
public:
    void set_services(HostServices *services) { services_ = services; }
    HostServices *services() const { return services_; }

    bool register_type(const char *id, WidgetFactory factory);
    // Creates (if needed) and mounts into ui_parent. Returns instance index or -1.
    int mount(const char *type_id, void *ui_parent);
    // Unmount every other mounted type, then mount. Detail shell uses this so only one
    // widget owns the slot (stale HTTP/timers from the previous screen are canceled).
    int mount_exclusive(const char *type_id, void *ui_parent);
    // Unmount + destroy instance; bumps generation and cancels http/scheduler jobs.
    bool unmount(int instance);
    bool unmount_type(const char *type_id);
    // Unmount all mounted instances (e.g. back to home).
    void unmount_all();

    TsWidget *widget(int instance) const;
    uint32_t generation(int instance) const;
    int find_type(const char *type_id) const;
    int instance_count() const { return instance_count_; }

    int type_count() const { return type_count_; }
    const char *type_id(int index) const;
    const char *type_title(int index) const;
    // Index in the type registry, or -1.
    int find_registered(const char *type_id) const;
    bool type_registered(const char *type_id) const { return find_registered(type_id) >= 0; }
    bool type_hero_only(const char *type_id) const;
    HomeTile type_home_tile(const char *type_id) const;
    uint32_t type_accent(const char *type_id) const;

    // True if tag still matches a live mounted instance.
    bool generation_live(int instance, uint32_t tag) const;

private:
    struct Instance {
        TsWidget *widget = nullptr;
        char type_id[48] = {};
        uint32_t generation = 0;
        bool mounted = false;
    };

    void cancel_jobs(uint32_t generation);

    RegistryEntry types_[kMaxWidgetTypes] = {};
    int type_count_ = 0;
    Instance instances_[kMaxInstances] = {};
    int instance_count_ = 0;
    uint32_t next_generation_ = 1;
    HostServices *services_ = nullptr;
};

Runtime &runtime();

}  // namespace tessara
