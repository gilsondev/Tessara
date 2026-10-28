#include "tessera/data.h"

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif

namespace {

struct Pending {
    int index = -1;
    uint32_t generation = 0;
    tessera::DataCallback cb = nullptr;
    void *user = nullptr;
    tessera::DataRef ref;
};

}  // namespace

struct tessera::TopicBus::Lock {
#if defined(ESP_PLATFORM)
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    void enter() { portENTER_CRITICAL(&mux); }
    void leave() { portEXIT_CRITICAL(&mux); }
#else
    std::mutex mu;
    void enter() { mu.lock(); }
    void leave() { mu.unlock(); }
#endif
};

tessera::TopicBus::TopicBus() : lock_(new Lock) {}

tessera::TopicBus::~TopicBus() { delete lock_; }

void tessera::TopicBus::lock() const { lock_->enter(); }

void tessera::TopicBus::unlock() const { lock_->leave(); }

tessera::DataRef tessera::TopicBus::latest(TopicId id) const {
    if (!valid(id)) return {};
    lock();
    const Slot &slot = slots_[static_cast<int>(id)];
    DataRef ref = slot.version == 0 ? DataRef{} : DataRef{slot.ptr, slot.size, slot.version};
    unlock();
    return ref;
}

bool tessera::TopicBus::holds(const void *ptr) const {
    if (!ptr) return false;
    lock();
    bool yes = reading_ == ptr;
    if (!yes) {
        for (const auto &slot : slots_) {
            if (slot.ptr == ptr) {
                yes = true;
                break;
            }
        }
    }
    unlock();
    return yes;
}

int tessera::TopicBus::subscribe(TopicId id, uint32_t generation, DataCallback cb, void *user) {
    if (!valid(id) || !cb || generation == 0) return -1;
    lock();
    int id_out = -1;
    for (int i = 0; i < kMaxSubs; ++i) {
        if (subs_[i].active) continue;
        subs_[i] = Sub{true, id, generation, cb, user};
        id_out = i;
        break;
    }
    unlock();
    return id_out;
}

void tessera::TopicBus::cancel(int sub) {
    if (sub < 0 || sub >= kMaxSubs) return;
    lock();
    subs_[sub].active = false;
    subs_[sub].cb = nullptr;
    unlock();
}

void tessera::TopicBus::cancel_generation(uint32_t generation) {
    lock();
    for (auto &sub : subs_) {
        if (sub.active && sub.generation == generation) {
            sub.active = false;
            sub.cb = nullptr;
        }
    }
    unlock();
}

bool tessera::TopicBus::publish(TopicId id, const void *snapshot, uint16_t size) {
    if (!valid(id) || (size > 0 && !snapshot)) return false;
    lock();
    Slot &slot = slots_[static_cast<int>(id)];
    slot.ptr = size ? snapshot : nullptr;
    slot.size = size;
    slot.version += 1;
    slot.dirty = true;
    unlock();
    return true;
}

void tessera::TopicBus::dispatch() {
    Pending pending[kMaxSubs];
    int count = 0;
    lock();
    for (int topic = 0; topic < static_cast<int>(TopicId::Count); ++topic) {
        Slot &slot = slots_[topic];
        if (!slot.dirty) continue;
        const DataRef ref{slot.ptr, slot.size, slot.version};
        slot.dirty = false;
        const auto id = static_cast<TopicId>(topic);
        for (int i = 0; i < kMaxSubs; ++i) {
            const Sub &sub = subs_[i];
            if (!sub.active || sub.topic != id || !sub.cb) continue;
            if (count >= kMaxSubs) break;
            pending[count++] = Pending{i, sub.generation, sub.cb, sub.user, ref};
        }
    }
    if (count > 0) reading_ = pending[0].ref.ptr;
    unlock();
    for (int i = 0; i < count; ++i) {
        lock();
        const Sub &sub = subs_[pending[i].index];
        const bool live = sub.active && sub.generation == pending[i].generation && sub.cb == pending[i].cb;
        if (live) reading_ = pending[i].ref.ptr;
        else if (i + 1 < count) reading_ = pending[i + 1].ref.ptr;
        unlock();
        if (!live) continue;
        pending[i].cb(pending[i].ref, pending[i].user);
    }
    lock();
    reading_ = nullptr;
    unlock();
}
