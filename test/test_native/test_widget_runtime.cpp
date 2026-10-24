#include "tessara/memory_services.h"
#include "tessara/runtime.h"
#include "tessara/widget.h"

#include <unity.h>

#include <cstring>

namespace {

const tessara::WidgetManifest kFakeManifest = {"org.tessara.fake", "Fake", 100, 100, false,
                                               tessara::HomeTile::Generic, 0};

struct FakeWidget : tessara::TsWidget {
    const tessara::WidgetManifest &manifest() const override { return kFakeManifest; }
    void mount(tessara::WidgetContext &ctx) override {
        mounted = true;
        gen = ctx.generation();
        parent = ctx.ui_parent();
        time = ctx.time();
        logger = ctx.logger();
        if (ctx.logger()) ctx.logger()->info("fake.mount");
        char place[32] = {};
        if (ctx.settings_get("place", place, sizeof(place))) {
            std::strncpy(settings_place, place, sizeof(settings_place) - 1);
        }
        if (ctx.http()) {
            http_job = ctx.http()->get("https://example.test/x", gen, on_http, this);
        }
        if (ctx.scheduler()) {
            sched_job = ctx.scheduler()->every_ms(1000, gen, on_sched, this);
        }
        ++mounts;
    }
    void on_resize(uint16_t, uint16_t) override {}
    void unmount() override {
        mounted = false;
        ++unmounts;
    }

    static void on_http(void *user, uint32_t generation, const tessara::HttpResult &result) {
        auto *self = static_cast<FakeWidget *>(user);
        self->http_calls += 1;
        self->last_http_gen = generation;
        self->last_http_status = result.status;
    }

    static void on_sched(void *user, uint32_t generation) {
        auto *self = static_cast<FakeWidget *>(user);
        self->sched_calls += 1;
        self->last_sched_gen = generation;
    }

    bool mounted = false;
    void *parent = nullptr;
    uint32_t gen = 0;
    int mounts = 0;
    int unmounts = 0;
    tessara::ITime *time = nullptr;
    tessara::ILogger *logger = nullptr;
    char settings_place[32] = {};
    int http_job = -1;
    int sched_job = -1;
    int http_calls = 0;
    int sched_calls = 0;
    uint32_t last_http_gen = 0;
    uint32_t last_sched_gen = 0;
    tessara::HttpStatus last_http_status = tessara::HttpStatus::Network;
};

FakeWidget *g_last = nullptr;

tessara::TsWidget *make_fake() {
    g_last = new FakeWidget();
    return g_last;
}

}  // namespace

void test_runtime_register_mount_unmount_bumps_generation(void) {
    tessara::Runtime rt;
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int a = 42;
    int slot = rt.mount("org.tessara.fake", &a);
    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_NOT_NULL(g_last);
    TEST_ASSERT_TRUE(g_last->mounted);
    TEST_ASSERT_EQUAL_PTR(&a, g_last->parent);
    uint32_t gen1 = rt.generation(slot);
    TEST_ASSERT_TRUE(gen1 != 0);
    TEST_ASSERT_TRUE(rt.generation_live(slot, gen1));

    TEST_ASSERT_TRUE(rt.unmount(slot));
    TEST_ASSERT_FALSE(rt.generation_live(slot, gen1));
    TEST_ASSERT_NULL(rt.widget(slot));
}

void test_runtime_unknown_type_fails(void) {
    tessara::Runtime rt;
    TEST_ASSERT_EQUAL(-1, rt.mount("org.tessara.missing", nullptr));
}

void test_runtime_remount_same_type_reuses_slot(void) {
    tessara::Runtime rt;
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int slot1 = rt.mount("org.tessara.fake", nullptr);
    uint32_t g1 = rt.generation(slot1);
    int slot2 = rt.mount("org.tessara.fake", nullptr);
    TEST_ASSERT_EQUAL(slot1, slot2);
    uint32_t g2 = rt.generation(slot2);
    TEST_ASSERT_TRUE(g2 != g1);
    TEST_ASSERT_FALSE(rt.generation_live(slot1, g1));
    TEST_ASSERT_TRUE(rt.generation_live(slot2, g2));
    rt.unmount(slot2);
}

void test_context_exposes_services_and_namespaced_settings(void) {
    tessara::MemoryHost host;
    host.time.snap = {14, 30, 2, 24, 9, true};
    host.settings.set("org.tessara.fake/place", "Campinas");
    tessara::HostServices bag = host.bag();

    tessara::Runtime rt;
    rt.set_services(&bag);
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int slot = rt.mount("org.tessara.fake", nullptr);
    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_NOT_NULL(g_last->time);
    TEST_ASSERT_TRUE(g_last->time->now().synced);
    TEST_ASSERT_EQUAL(14, g_last->time->now().hour);
    TEST_ASSERT_EQUAL_STRING("Campinas", g_last->settings_place);
    TEST_ASSERT_TRUE(host.logger.line_count() >= 1);
    rt.unmount(slot);
}

void test_unmount_cancels_http_so_late_result_is_dropped(void) {
    tessara::MemoryHost host;
    tessara::HostServices bag = host.bag();
    tessara::Runtime rt;
    rt.set_services(&bag);
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int slot = rt.mount("org.tessara.fake", nullptr);
    TEST_ASSERT_TRUE(g_last->http_job >= 0);
    int job = g_last->http_job;
    TEST_ASSERT_EQUAL(1, host.http.pending_count());

    TEST_ASSERT_TRUE(rt.unmount(slot));
    TEST_ASSERT_EQUAL(0, host.http.pending_count());
    // Completing a canceled job must be a no-op (FakeWidget already destroyed).
    TEST_ASSERT_FALSE(host.http.complete(job, tessara::HttpStatus::Ok, "{}", 2));
}

void test_unmount_cancels_scheduler_jobs(void) {
    tessara::MemoryHost host;
    tessara::HostServices bag = host.bag();
    tessara::Runtime rt;
    rt.set_services(&bag);
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int slot = rt.mount("org.tessara.fake", nullptr);
    int job = g_last->sched_job;
    TEST_ASSERT_TRUE(job >= 0);
    TEST_ASSERT_EQUAL(1, host.scheduler.active_count());
    TEST_ASSERT_TRUE(rt.unmount(slot));
    TEST_ASSERT_EQUAL(0, host.scheduler.active_count());
    TEST_ASSERT_FALSE(host.scheduler.tick(job));
}

void test_runtime_registry_exposes_titles(void) {
    tessara::Runtime rt;
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    TEST_ASSERT_EQUAL(1, rt.type_count());
    TEST_ASSERT_EQUAL_STRING("org.tessara.fake", rt.type_id(0));
    TEST_ASSERT_EQUAL_STRING("Fake", rt.type_title(0));
}

namespace {

const tessara::WidgetManifest kOtherManifest = {"org.tessara.other", "Other", 80, 80, false,
                                                tessara::HomeTile::Generic, 0};

struct OtherWidget;
OtherWidget *g_other = nullptr;

struct OtherWidget : tessara::TsWidget {
    static void on_http(void *, uint32_t, const tessara::HttpResult &) {}
    ~OtherWidget() override {
        mounted = false;
        if (g_other == this) g_other = nullptr;
    }
    const tessara::WidgetManifest &manifest() const override { return kOtherManifest; }
    void mount(tessara::WidgetContext &ctx) override {
        mounted = true;
        gen = ctx.generation();
        parent = ctx.ui_parent();
        if (ctx.http()) http_job = ctx.http()->get("https://example.test/other", gen, on_http, this);
    }
    void on_resize(uint16_t, uint16_t) override {}
    void unmount() override { mounted = false; }
    bool mounted = false;
    void *parent = nullptr;
    uint32_t gen = 0;
    int http_job = -1;
};

tessara::TsWidget *make_other() {
    g_other = new OtherWidget();
    return g_other;
}

}  // namespace

void test_mount_exclusive_unmounts_previous_and_cancels_its_http(void) {
    tessara::MemoryHost host;
    tessara::HostServices bag = host.bag();
    tessara::Runtime rt;
    rt.set_services(&bag);
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.other", make_other));

    int parent_a = 1;
    int parent_b = 2;
    int slot_a = rt.mount("org.tessara.fake", &parent_a);
    TEST_ASSERT_TRUE(slot_a >= 0);
    TEST_ASSERT_TRUE(g_last->mounted);
    TEST_ASSERT_EQUAL_PTR(&parent_a, g_last->parent);
    uint32_t gen_a = g_last->gen;
    int job_a = g_last->http_job;
    TEST_ASSERT_TRUE(job_a >= 0);
    TEST_ASSERT_EQUAL(1, host.http.pending_count());

    int slot_b = rt.mount_exclusive("org.tessara.other", &parent_b);
    TEST_ASSERT_TRUE(slot_b >= 0);
    TEST_ASSERT_NOT_EQUAL(slot_a, slot_b);
    TEST_ASSERT_NOT_NULL(g_other);
    TEST_ASSERT_TRUE(g_other->mounted);
    TEST_ASSERT_EQUAL_PTR(&parent_b, g_other->parent);
    TEST_ASSERT_NULL(rt.widget(slot_a));
    TEST_ASSERT_FALSE(rt.generation_live(slot_a, gen_a));
    // Fake's in-flight job was canceled; only the new widget may have a pending GET.
    // (Job ids are reused — do not assert on the old index.)
    TEST_ASSERT_EQUAL(1, host.http.pending_count());
    TEST_ASSERT_TRUE(g_other->http_job >= 0);
    (void)job_a;

    rt.unmount_all();
    TEST_ASSERT_EQUAL(0, host.http.pending_count());
    TEST_ASSERT_NULL(g_other);  // destroyed on unmount — do not touch dangling pointer
}

void test_mount_passes_detail_slot_parent(void) {
    tessara::Runtime rt;
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    int slot_obj = 99;
    int idx = rt.mount("org.tessara.fake", &slot_obj);
    TEST_ASSERT_TRUE(idx >= 0);
    TEST_ASSERT_EQUAL_PTR(&slot_obj, g_last->parent);
    rt.unmount(idx);
}

namespace {

const tessara::WidgetManifest kHeroManifest = {"org.tessara.hero", "Hero", 0, 0, true, tessara::HomeTile::None, 0};

struct HeroWidget : tessara::TsWidget {
    const tessara::WidgetManifest &manifest() const override { return kHeroManifest; }
    void mount(tessara::WidgetContext &) override {}
    void on_resize(uint16_t, uint16_t) override {}
    void unmount() override {}
};

tessara::TsWidget *make_hero() { return new HeroWidget(); }

}  // namespace

void test_runtime_exposes_hero_only_flag(void) {
    tessara::Runtime rt;
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.fake", make_fake));
    TEST_ASSERT_TRUE(rt.register_type("org.tessara.hero", make_hero));
    TEST_ASSERT_TRUE(rt.type_registered("org.tessara.fake"));
    TEST_ASSERT_FALSE(rt.type_hero_only("org.tessara.fake"));
    TEST_ASSERT_TRUE(rt.type_hero_only("org.tessara.hero"));
    TEST_ASSERT_FALSE(rt.type_registered("org.tessara.missing"));
    TEST_ASSERT_FALSE(rt.type_hero_only("org.tessara.missing"));
}
