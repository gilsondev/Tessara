#pragma once

#include <stddef.h>
#include <stdint.h>

// Typed topic bus. Producers publish a snapshot; widgets subscribe and drop the
// subscription when their generation ends. Independent of LVGL and of the board.
namespace tessera {

enum class TopicId : uint8_t {
    Media = 0,
    Focus,
    AiTokens,
    Calendar,
    Todoist,
    Weather,
    Agent,
    Clock,
    Count
};

struct DataRef {
    const void *ptr = nullptr;
    uint16_t size = 0;
    uint32_t version = 0;
};

using DataCallback = void (*)(const DataRef &ref, void *user);

class IData {
public:
    virtual ~IData() = default;
    virtual DataRef latest(TopicId id) const = 0;
    // Returns a subscription id, or -1 when the table is full.
    // Generations >= 0xFFFFFF00 are reserved for the host. The runtime never
    // assigns them to a widget, so cancel_generation of a widget cannot drop
    // a host subscription.
    virtual int subscribe(TopicId id, uint32_t generation, DataCallback cb, void *user) = 0;
    virtual void cancel(int sub) = 0;
    virtual void cancel_generation(uint32_t generation) = 0;
    // Stores the pointer. See TopicBus::publish for the lifetime rule.
    virtual bool publish(TopicId id, const void *snapshot, uint16_t size) = 0;
    // True while `ptr` is the published snapshot or the one a callback is reading.
    virtual bool holds(const void *ptr) const = 0;
    // Delivers each dirty topic once, with the latest version. Call on the UI
    // thread, before lv_timer_handler. On the test host, call it by hand.
    virtual void dispatch() = 0;
};

// Three slots: one published, one being read during dispatch, one free to write.
template <typename T, int N = 3>
class SnapshotWriter {
public:
    static_assert(N >= 3, "a writer needs a free buffer besides the published one and the one being read");
    static_assert(sizeof(T) <= 65535, "snapshot size is stored in uint16_t");

    SnapshotWriter() = default;
    SnapshotWriter(IData &bus, TopicId topic) : bus_(&bus), topic_(topic) {}

    void attach(IData &bus, TopicId topic) {
        bus_ = &bus;
        topic_ = topic;
    }

    // A buffer that is neither the published snapshot nor the one dispatch is delivering.
    T &begin() {
        if (!bus_) return slots_[0];
        for (int n = 0; n < N; ++n) {
            cursor_ = (cursor_ + 1) % N;
            if (!bus_->holds(&slots_[cursor_])) return slots_[cursor_];
        }
        return slots_[cursor_];
    }

    bool commit() {
        if (!bus_) return false;
        return bus_->publish(topic_, &slots_[cursor_], static_cast<uint16_t>(sizeof(T)));
    }

private:
    IData *bus_ = nullptr;
    TopicId topic_ = TopicId::Media;
    T slots_[N]{};
    int cursor_ = 0;
};

class TopicBus final : public IData {
public:
    static constexpr int kMaxSubs = 16;

    TopicBus();
    ~TopicBus() override;
    TopicBus(const TopicBus &) = delete;
    TopicBus &operator=(const TopicBus &) = delete;

    DataRef latest(TopicId id) const override;
    int subscribe(TopicId id, uint32_t generation, DataCallback cb, void *user) override;
    void cancel(int sub) override;
    void cancel_generation(uint32_t generation) override;
    void dispatch() override;
    bool holds(const void *ptr) const override;

    // Stores the pointer. The producer keeps the bytes immutable while a callback
    // may still be reading them. Use SnapshotWriter so the next write lands in a
    // buffer that is neither published nor being delivered. `publish` may run on
    // any task. It does not call widgets.
    bool publish(TopicId id, const void *snapshot, uint16_t size) override;

private:
    struct Slot {
        const void *ptr = nullptr;
        uint16_t size = 0;
        uint32_t version = 0;
        bool dirty = false;
    };
    struct Sub {
        bool active = false;
        TopicId topic = TopicId::Media;
        uint32_t generation = 0;
        DataCallback cb = nullptr;
        void *user = nullptr;
    };

    struct Lock;

    bool valid(TopicId id) const { return static_cast<uint8_t>(id) < static_cast<uint8_t>(TopicId::Count); }
    void lock() const;
    void unlock() const;

    Slot slots_[static_cast<int>(TopicId::Count)] = {};
    Sub subs_[kMaxSubs] = {};
    const void *reading_ = nullptr;
    // Critical section. ESP32 uses portMUX; the host uses a mutex. Defined in the cpp.
    Lock *lock_ = nullptr;
};

}  // namespace tessera
