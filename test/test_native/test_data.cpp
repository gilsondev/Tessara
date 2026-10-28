#include <unity.h>

#include "mac/focus_snapshot.h"
#include "tessera/data.h"
#include "tessera/memory_services.h"
#include "tessera/widget.h"

#include <atomic>
#include <cstring>
#include <thread>

static int g_hits = 0;
static uint32_t g_version = 0;
static char g_text[32];

static void on_data(const tessera::DataRef &ref, void *) {
    g_hits += 1;
    g_version = ref.version;
    g_text[0] = '\0';
    if (ref.ptr && ref.size > 0 && ref.size < sizeof(g_text)) {
        std::memcpy(g_text, ref.ptr, ref.size);
        g_text[ref.size] = '\0';
    }
}

void test_topic_publish_reaches_a_subscriber_and_latest(void) {
    tessera::TopicBus bus;
    g_hits = 0;
    g_version = 0;
    const int sub = bus.subscribe(tessera::TopicId::Media, 7, on_data, nullptr);
    TEST_ASSERT_TRUE(sub >= 0);
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Media, "orvalho", 7));
    TEST_ASSERT_EQUAL(0, g_hits);
    bus.dispatch();
    TEST_ASSERT_EQUAL(1, g_hits);
    TEST_ASSERT_EQUAL_STRING("orvalho", g_text);
    const tessera::DataRef latest = bus.latest(tessera::TopicId::Media);
    TEST_ASSERT_EQUAL(1, latest.version);
    TEST_ASSERT_EQUAL(7, latest.size);
    TEST_ASSERT_EQUAL_STRING("orvalho", static_cast<const char *>(latest.ptr));
}

void test_topic_cancel_generation_drops_the_subscriber(void) {
    tessera::TopicBus bus;
    g_hits = 0;
    TEST_ASSERT_TRUE(bus.subscribe(tessera::TopicId::Media, 7, on_data, nullptr) >= 0);
    bus.cancel_generation(7);
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Media, "x", 1));
    bus.dispatch();
    TEST_ASSERT_EQUAL(0, g_hits);
    TEST_ASSERT_EQUAL(1, bus.latest(tessera::TopicId::Media).version);
}

void test_context_exposes_the_data_bus(void) {
    tessera::MemoryHost host;
    tessera::HostServices bag = host.bag();
    tessera::WidgetContext ctx(nullptr, 3, "org.tessera.media", &bag);
    TEST_ASSERT_EQUAL_PTR(&host.data, ctx.data());
    TEST_ASSERT_TRUE(host.data.publish(tessera::TopicId::Clock, "t", 1));
    TEST_ASSERT_EQUAL(1, ctx.data()->latest(tessera::TopicId::Clock).version);
}

void test_topic_dispatch_keeps_only_the_latest(void) {
    tessera::TopicBus bus;
    g_hits = 0;
    g_version = 0;
    g_text[0] = '\0';
    TEST_ASSERT_TRUE(bus.subscribe(tessera::TopicId::Media, 1, on_data, nullptr) >= 0);
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Media, "a", 1));
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Media, "bb", 2));
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Media, "ccc", 3));
    TEST_ASSERT_EQUAL(0, g_hits);
    TEST_ASSERT_EQUAL(3, bus.latest(tessera::TopicId::Media).version);
    bus.dispatch();
    TEST_ASSERT_EQUAL(1, g_hits);
    TEST_ASSERT_EQUAL(3, g_version);
    TEST_ASSERT_EQUAL_STRING("ccc", g_text);
    bus.dispatch();
    TEST_ASSERT_EQUAL(1, g_hits);
}

static uint16_t g_size = 0;
static char g_mode[41];

static void on_focus(const tessera::DataRef &ref, void *) {
    g_hits += 1;
    g_size = ref.size;
    g_mode[0] = '\0';
    if (ref.ptr && ref.size == sizeof(focus::View)) {
        const auto *view = static_cast<const focus::View *>(ref.ptr);
        std::strncpy(g_mode, view->modes[11].name, sizeof(g_mode) - 1);
    }
}

void test_topic_publish_accepts_a_full_focus_view(void) {
    tessera::TopicBus bus;
    focus::View view;
    view.ready = true;
    view.mode_count = focus::kMaxModes;
    std::strncpy(view.modes[11].name, "Orvalho", sizeof(view.modes[11].name) - 1);
    g_hits = 0;
    g_size = 0;
    TEST_ASSERT_TRUE(bus.subscribe(tessera::TopicId::Focus, 4, on_focus, nullptr) >= 0);
    TEST_ASSERT_TRUE(bus.publish(tessera::TopicId::Focus, &view, sizeof(view)));
    bus.dispatch();
    TEST_ASSERT_EQUAL(1, g_hits);
    TEST_ASSERT_EQUAL(sizeof(focus::View), g_size);
    TEST_ASSERT_EQUAL_STRING("Orvalho", g_mode);
}

struct ThreadState {
    std::atomic<uint32_t> last{0};
    std::atomic<bool> bad{false};
    std::atomic<bool> stop{false};
};

static void on_ordered(const tessera::DataRef &ref, void *user) {
    auto *state = static_cast<ThreadState *>(user);
    if (!ref.ptr || ref.size != sizeof(uint32_t)) {
        state->bad.store(true);
        return;
    }
    const uint32_t body = *static_cast<const uint32_t *>(ref.ptr);
    const uint32_t prev = state->last.load();
    if (body != ref.version || ref.version < prev) state->bad.store(true);
    state->last.store(ref.version);
}

void test_topic_publish_and_dispatch_stay_ordered_across_threads(void) {
    tessera::TopicBus bus;
    tessera::SnapshotWriter<uint32_t> writer(bus, tessera::TopicId::Media);
    ThreadState state;
    TEST_ASSERT_TRUE(bus.subscribe(tessera::TopicId::Media, 1, on_ordered, &state) >= 0);
    std::thread dispatcher([&]() {
        while (!state.stop.load()) bus.dispatch();
        bus.dispatch();
    });
    constexpr uint32_t kCount = 100000;
    for (uint32_t i = 1; i <= kCount; ++i) {
        uint32_t &slot = writer.begin();
        slot = i;
        TEST_ASSERT_TRUE(writer.commit());
    }
    state.stop.store(true);
    dispatcher.join();
    bus.dispatch();
    TEST_ASSERT_FALSE(state.bad.load());
    TEST_ASSERT_EQUAL(kCount, state.last.load());
}

struct Row {
    uint32_t version = 0;
    uint32_t mirror = 0;
};

struct SlowState {
    std::atomic<int> phase{0};
    std::atomic<bool> torn{false};
};

static void on_slow_row(const tessera::DataRef &ref, void *user) {
    auto *state = static_cast<SlowState *>(user);
    if (!ref.ptr || ref.size != sizeof(Row)) {
        state->torn.store(true);
        state->phase.store(2);
        return;
    }
    const auto *row = static_cast<const Row *>(ref.ptr);
    const uint32_t version = row->version;
    state->phase.store(1);
    while (state->phase.load() < 2) {
    }
    if (row->version != version || row->mirror != version) state->torn.store(true);
}

void test_snapshot_writer_keeps_a_buffer_stable_during_dispatch(void) {
    tessera::TopicBus bus;
    tessera::SnapshotWriter<Row> writer(bus, tessera::TopicId::Media);
    SlowState state;
    TEST_ASSERT_TRUE(bus.subscribe(tessera::TopicId::Media, 3, on_slow_row, &state) >= 0);
    Row &first = writer.begin();
    first.version = 1;
    first.mirror = 1;
    TEST_ASSERT_TRUE(writer.commit());
    std::thread dispatcher([&]() { bus.dispatch(); });
    while (state.phase.load() < 1) {
    }
    for (uint32_t version = 2; version <= 4; ++version) {
        Row &slot = writer.begin();
        slot.version = version;
        slot.mirror = version;
        TEST_ASSERT_TRUE(writer.commit());
    }
    state.phase.store(2);
    dispatcher.join();
    TEST_ASSERT_FALSE(state.torn.load());
}
