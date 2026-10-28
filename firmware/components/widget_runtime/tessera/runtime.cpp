#include "tessera/runtime.h"

#include <cstring>

namespace {

tessera::Runtime g_runtime;

bool id_eq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

}  // namespace

void tessera::Runtime::cancel_jobs(uint32_t generation) {
    if (!services_ || generation == 0) return;
    if (services_->http) services_->http->cancel_generation(generation);
    if (services_->scheduler) services_->scheduler->cancel_generation(generation);
    if (services_->data) services_->data->cancel_generation(generation);
}

bool tessera::Runtime::register_type(const char *id, WidgetFactory factory) {
    if (!id || !id[0] || !factory) return false;
    for (int i = 0; i < type_count_; ++i) {
        if (id_eq(types_[i].id, id)) {
            types_[i].factory = factory;
            return true;
        }
    }
    if (type_count_ >= kMaxWidgetTypes) return false;
    RegistryEntry &entry = types_[type_count_];
    entry = {};
    std::strncpy(entry.id, id, sizeof(entry.id) - 1);
    entry.factory = factory;
    TsWidget *probe = factory();
    if (probe) {
        const WidgetManifest &m = probe->manifest();
        if (m.title) {
            std::strncpy(entry.title, m.title, sizeof(entry.title) - 1);
        }
        entry.hero_only = m.hero_only;
        entry.home_tile = m.home_tile;
        entry.accent_rgb = m.accent_rgb;
        entry.supports_home_tile_size = m.supports_home_tile_size;
        entry.draws_tile = m.draws_tile;
        delete probe;
    }
    ++type_count_;
    return true;
}

int tessera::Runtime::find_registered(const char *type_id) const {
    if (!type_id || !type_id[0]) return -1;
    for (int i = 0; i < type_count_; ++i) {
        if (id_eq(types_[i].id, type_id)) return i;
    }
    return -1;
}

bool tessera::Runtime::type_hero_only(const char *type_id) const {
    int i = find_registered(type_id);
    return i >= 0 && types_[i].hero_only;
}

tessera::HomeTile tessera::Runtime::type_home_tile(const char *type_id) const {
    int i = find_registered(type_id);
    return i >= 0 ? types_[i].home_tile : HomeTile::None;
}

uint32_t tessera::Runtime::type_accent(const char *type_id) const {
    int i = find_registered(type_id);
    return i >= 0 ? types_[i].accent_rgb : 0;
}

bool tessera::Runtime::type_supports_home_tile_size(const char *type_id) const {
    int i = find_registered(type_id);
    return i >= 0 && types_[i].supports_home_tile_size;
}

bool tessera::Runtime::type_draws_tile(const char *type_id) const {
    int i = find_registered(type_id);
    return i >= 0 && types_[i].draws_tile;
}

uint32_t tessera::Runtime::take_generation() {
    if (next_generation_ == 0 || next_generation_ >= 0xFFFFFF00u) next_generation_ = 1;
    return next_generation_++;
}

int tessera::Runtime::find_type(const char *type_id, Surface surface) const {
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].widget && instances_[i].surface == surface && id_eq(instances_[i].type_id, type_id))
            return i;
    }
    return -1;
}

const char *tessera::Runtime::type_id(int index) const {
    if (index < 0 || index >= type_count_) return nullptr;
    return types_[index].id;
}

const char *tessera::Runtime::type_title(int index) const {
    if (index < 0 || index >= type_count_) return nullptr;
    return types_[index].title[0] ? types_[index].title : types_[index].id;
}

int tessera::Runtime::mount_exclusive(const char *type_id, void *ui_parent) {
    if (!type_id || !type_id[0]) return -1;
    for (int i = 0; i < instance_count_; ++i) {
        if (!instances_[i].widget || !instances_[i].mounted) continue;
        if (instances_[i].surface != Surface::Detail) continue;
        if (id_eq(instances_[i].type_id, type_id)) continue;
        unmount(i);
    }
    return mount(type_id, ui_parent);
}

void tessera::Runtime::unmount_all() {
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].widget) unmount(i);
    }
}

void tessera::Runtime::unmount_surface(Surface surface) {
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].widget && instances_[i].surface == surface) unmount(i);
    }
}

void tessera::Runtime::notify_theme() {
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].mounted && instances_[i].widget) instances_[i].widget->on_theme();
    }
}

int tessera::Runtime::mount(const char *type_id, void *ui_parent, Surface surface) {
    if (!type_id || !type_id[0]) return -1;
    WidgetFactory factory = nullptr;
    for (int i = 0; i < type_count_; ++i) {
        if (id_eq(types_[i].id, type_id)) {
            factory = types_[i].factory;
            break;
        }
    }
    if (!factory) return -1;

    int slot = -1;
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].widget && instances_[i].surface == surface && id_eq(instances_[i].type_id, type_id)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < instance_count_; ++i) {
            if (!instances_[i].widget) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            if (instance_count_ >= kMaxInstances) return -1;
            slot = instance_count_++;
            instances_[slot] = {};
        }
        instances_[slot].widget = factory();
        if (!instances_[slot].widget) {
            instances_[slot].type_id[0] = '\0';
            return -1;
        }
        std::strncpy(instances_[slot].type_id, type_id, sizeof(instances_[slot].type_id) - 1);
        instances_[slot].type_id[sizeof(instances_[slot].type_id) - 1] = '\0';
        instances_[slot].surface = surface;
    }

    Instance &inst = instances_[slot];
    inst.surface = surface;
    if (inst.mounted) {
        cancel_jobs(inst.generation);
        inst.widget->unmount();
    }
    inst.generation = take_generation();
    WidgetContext ctx(ui_parent, inst.generation, inst.type_id, services_, inst.surface);
    inst.widget->mount(ctx);
    inst.mounted = true;
    return slot;
}

bool tessera::Runtime::unmount(int instance) {
    if (instance < 0 || instance >= instance_count_) return false;
    Instance &inst = instances_[instance];
    if (!inst.widget) return false;
    uint32_t old_gen = inst.generation;
    if (inst.mounted) {
        inst.widget->unmount();
        inst.mounted = false;
    }
    cancel_jobs(old_gen);
    inst.generation = take_generation();
    cancel_jobs(inst.generation);  // new tag also inert
    delete inst.widget;
    inst.widget = nullptr;
    inst.type_id[0] = '\0';
    return true;
}

bool tessera::Runtime::unmount_type(const char *type_id, Surface surface) {
    int slot = find_type(type_id, surface);
    if (slot < 0) return false;
    return unmount(slot);
}

tessera::TsWidget *tessera::Runtime::widget(int instance) const {
    if (instance < 0 || instance >= instance_count_) return nullptr;
    return instances_[instance].widget;
}

uint32_t tessera::Runtime::generation(int instance) const {
    if (instance < 0 || instance >= instance_count_) return 0;
    return instances_[instance].generation;
}

bool tessera::Runtime::generation_live(int instance, uint32_t tag) const {
    if (instance < 0 || instance >= instance_count_) return false;
    const Instance &inst = instances_[instance];
    return inst.mounted && inst.widget && inst.generation == tag && tag != 0;
}

tessera::Runtime &tessera::runtime() { return g_runtime; }
