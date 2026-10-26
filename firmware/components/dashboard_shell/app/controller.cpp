#include "app/controller.h"

#include "agent/state.h"
#include "aitokens/snapshot.h"
#include "calendar/catalog.h"
#include "calendar/snapshot.h"
#include "mac/focus_snapshot.h"
#include "media/snapshot.h"
#include "app/nav.h"
#include "domain/list_prefs.h"
#include "domain/pending_closes.h"
#include "domain/task_store.h"
#include "todoist/snapshot.h"
#include "ui/palette.h"
#include "platform/chime.h"
#include "platform/agent_push.h"
#include "platform/console.h"
#include "platform/daytime.h"
#include "platform/device_resources.h"
#include "platform/face_prefs.h"
#include "platform/display_host.h"
#include "platform/input_host.h"
#include "platform/log.h"
#include "platform/screen_rest.h"
#include "platform/sd_host.h"
#include "platform/settings.h"
#include "platform/widget_settings.h"
#include "domain/dashboard_layout.h"
#include "domain/home_tile_size.h"
#include "services/net_worker.h"
#include "services/text.h"
#include "services/todoist_bridge.h"
#include "services/web_portal.h"
#include "platform/device_http.h"
#include "tessera/runtime.h"
#include "ui/screensaver.h"
#include "ui/ui.h"
#include "ui/weather_icons.h"
#include "weather/cache.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t kRetryMs = 6000;
constexpr uint32_t kWarmGapMs = 20000;
constexpr uint32_t kFailRetryMs = 8000;
constexpr uint32_t kForgetClosedMs = 60000;
// Checks often come in bursts; closes wait for a pause so the network stays quiet while the user taps.
// If the link is down, those ids stay saved and the close is sent again when it returns.
constexpr uint32_t kCloseSettleMs = 3000;
constexpr int kMaxCloses = 16;
constexpr int kMaxPending = pending_closes::kMax;
constexpr char kHostName[] = "tessera";

bool resolve_dark(const DeviceSettings &settings) {
    int hour = 0, minute = 0, wday = 0, day = 0, month = 0;
    int local_hour = daytime::local(hour, minute, wday, day, month) ? hour : -1;
    return effective_dark(settings, local_hour);
}

enum class Phase : uint8_t { Unconfigured, Connecting, Views, Running };

struct Close {
    int view;
    char id[20];
};

struct Closed {
    char id[20];
    uint32_t at;
};

class Controller;
Controller *g_controller = nullptr;

class Controller final : public UiEvents, public ConsoleHandler {
public:
    void begin(hal::Board &board) {
        g_controller = this;
        board_ = &board;
        device_ = settings::load_device();
        // Prefer namespaced Todoist sync when present; seed device value into wset otherwise.
        device_.sync_minutes = widget_settings::todoist_sync_minutes(device_.sync_minutes);
        widget_settings::set_todoist_sync_minutes(device_.sync_minutes);
        char tz[24] = {};
        if (!widget_settings::get(dashboard::id_clock(), "timezone", tz, sizeof(tz)) || !tz[0]) {
            text::copy(tz, sizeof(tz), "sao_paulo");
            widget_settings::set(dashboard::id_clock(), "timezone", tz);
        }
        daytime::set_timezone(tz);
        daytime::restore();  // last wall clock from NVS until NTP arrives
        char time_fmt[12] = {};
        if (!widget_settings::get(dashboard::id_clock(), "time_format", time_fmt, sizeof(time_fmt)) ||
            !time_fmt[0]) {
            widget_settings::set(dashboard::id_clock(), "time_format", "hm");
        }
        char date_fmt[16] = {};
        if (!widget_settings::get(dashboard::id_date(), "date_format", date_fmt, sizeof(date_fmt)) ||
            !date_fmt[0]) {
            widget_settings::set(dashboard::id_date(), "date_format", "short_pt");
        }
        face_prefs::reload();
        palette::set_mode(resolve_dark(device_) ? palette::Mode::Dark : palette::Mode::Light);
        display_host::begin(*board.display, board.backlight, board.descriptor, rotation_for(device_.portrait));
        display_host::set_brightness(device_.brightness);
        // Branded splash before the rest of boot (SD / Wi‑Fi / widgets).
        ui::show_loader("", "");
        display_host::refresh_now();
        // Deploy window: flash-agent can PUT as soon as Wi‑Fi is up (no extra USB arm).
        agent_push::arm_boot();
        input_host::begin(board.touch, board.button, board.descriptor);
        input_host::set_wake_handler([]() {
            if (g_controller) g_controller->on_rest_wake();
        });
        input_host::set_activity_handler([]() {
            if (g_controller) g_controller->on_rest_activity();
        });
        chime::begin(board.sound);
        chime::set_enabled(device_.sound);
        sd_host::begin();
        task_store::begin();
        load_closes();
        net_worker::begin();
        web_portal::begin(kHostName);
        ui::begin(*this);
        screensaver::begin();
        screen_rest::begin(rest_, millis(), device_.brightness);
        screen_rest::set_idle_minutes(rest_, device_.rest_minutes);
        screen_rest::set_clock_minutes(rest_, device_.clock_minutes);
        settings::WeatherPlace saved = settings::load_weather();
        if (saved.set) weather::set_place(saved.name, saved.latitude, saved.longitude);
        reload_dash();
        if (tile_on(dashboard::id_weather())) paint_weather();
        if (tile_on(dashboard::id_aitokens())) paint_aitokens();
        {
            char sel[512] = {};
            if (widget_settings::get(dashboard::id_calendar(), "selection", sel, sizeof(sel)) && sel[0]) {
                calendar::apply_selection(calendar_catalog_, sel);
                // Trust saved selection until a real CALENDAR / catalog reply — avoid "ligar agenda" flash.
                calendar_.linked = true;
            }
        }
        if (tile_on(dashboard::id_calendar())) {
            dashboard::set_enabled(dash_, dashboard::id_calendar(), true);  // snap under AI Tokens
            settings::save_dashboard(dash_);
            paint_calendar();
            ask_calendar_refresh();
        }
        if (!ui::loader_visible()) ui::show_loader("", "");
        display_host::refresh_now();
        Serial.printf("tela %s\n", device_.portrait ? "em pe" : "deitada");
        configure();
        publish();
    }

    void loop() {
        console::poll(*this);
        if (setup_commit_at_ && millis() >= setup_commit_at_) {
            setup_commit_at_ = 0;
            configure();
        }
        if (arm_setup_) try_start_setup();
        if (input_host::take_key_tap()) {
            if (rest_.phase != screen_rest::Phase::Awake) {
                on_rest_wake();
            } else {
                on_rest_activity();
                DeviceSettings next = device_;
                next.portrait = !next.portrait;
                on_settings_changed(next);
            }
        }
        // SD: se o boot falhou, retenta em background sem comando serial.
        const bool sd_before = sd_host::ready();
        sd_host::poll();
        if (!sd_before && sd_host::ready()) {
            weather_icons::apply_theme();
            paint_weather();
            publish();
        }
        serve_portal();
        chime::pump();
        drain_worker();
        pump_widget_http();
        feed_worker();
        pump_screen_rest();
        display_host::tick();
        chime::pump();
        // Stale paint / USB pulls only for tiles that are on the home (hidden = not allocated).
        if (tile_on(dashboard::id_aitokens()) && aitokens::refresh_age(aitokens_, millis())) {
            paint_aitokens();
        }
        if (tile_on(dashboard::id_calendar()) && calendar::refresh_age(calendar_, millis())) {
            paint_calendar();
        }
        if (tile_on(dashboard::id_media()) && media::refresh_age(media_, millis())) paint_media();
        if (tile_on(dashboard::id_mac()) && focus::refresh_age(focus_, millis())) paint_mac();
        // USB drops often — re-ask while the agent is up but agenda never arrived.
        if (tile_on(dashboard::id_calendar()) && agent_.ok) {
            if (!calendar_.ready && millis() - calendar_ask_at_ > 5000u) {
                calendar_ask_at_ = millis();
                ask_calendar_refresh();
            } else if (calendar_.ready && millis() - calendar_ask_at_ > 45000u) {
                calendar_ask_at_ = millis();
                ask_calendar_refresh();
            }
        }
        // Same safety net for Tarefas: a dropped bundle must not leave the lists
        // empty until the Mac cache happens to change.
        if (tile_on(dashboard::id_todoist()) && agent_.ok) {
            todoist_bridge::refresh_age(millis());
            const todoist_bridge::Status todoist_usb = todoist_bridge::status();
            const bool warm = todoist_usb.ready && !todoist_usb.stale && task_store::view_count() > 0;
            if (millis() - todoist_ask_at_ > (warm ? 45000u : 5000u)) ask_todoist_refresh();
            give_up_on_todoist();
        }
        // Auto appearance: re-check when the clock crosses the evening/morning window
        // (or after the portal reports a new OS preference).
        if (device_.appearance == Appearance::Auto && millis() - theme_check_at_ > 30000u) {
            theme_check_at_ = millis();
            bool want = resolve_dark(device_);
            bool have = palette::mode() == palette::Mode::Dark;
            if (want != have) {
                palette::set_mode(want ? palette::Mode::Dark : palette::Mode::Light);
                ui::apply_theme();
            }
        }
    }

    void on_rest_activity() {
        screen_rest::note_activity(rest_, millis());
        // note_activity only wakes if already resting; while awake it just bumps the timer.
        if (rest_.phase == screen_rest::Phase::Awake && screensaver::visible()) restore_after_rest();
        sync_rest_input();
    }

    void on_rest_wake() {
        screen_rest::note_activity(rest_, millis());
        restore_after_rest();
        sync_rest_input();
        apply_rest_brightness();
    }

    // UiEvents
    void on_view_shown(int view) override {
        if (task_store::state(view) != task_store::State::Ready || task_store::age_ms(view) > stale_ms()) wanted_ = view;
    }

    void on_refresh_requested() override {
        if (need_token_) return;
        if (phase_ != Phase::Running) {
            configure();
            return;
        }
        refreshing_ = true;
        refresh_next_ = -1;
        ui::show_loader("Carregando", "");
        ui::set_loader_progress(0);
    }

    void on_open_widget(const char *widget_id) override {
        if (!widget_id || !widget_id[0]) return;
        auto &rt = tessera::runtime();
        if (!rt.type_registered(widget_id)) return;
        if (rt.type_hero_only(widget_id)) return;
        // Prefetch Agente state before paint so pages don't flash offline.
        if (std::strcmp(widget_id, dashboard::id_media()) == 0) ask_media_refresh();
        if (std::strcmp(widget_id, dashboard::id_mac()) == 0) ask_focus_refresh();
        nav::open_detail(nav_, widget_id);
        ui::show_widget(widget_id);  // widget paints itself on mount
    }

    void on_aitokens_refresh() override {
        // Refresh updates the AI Tokens screen in place — do not navigate home.
        if (nav_.screen != nav::Screen::Detail ||
            std::strcmp(nav_.widget_id, "org.tessera.aitokens") != 0) {
            nav::open_aitokens(nav_);
        }
        ui::aitokens_refreshing();
    }

    void on_media_cmd(const char *cmd) override {
        if (!cmd || !cmd[0]) return;
        if (cmd[0] == '?' && cmd[1] == '\0') {
            ask_media_refresh();
            return;
        }
        push_media_cmd(cmd);
    }

    void on_mac_cmd(const char *cmd) override {
        if (!cmd || !cmd[0]) return;
        if (cmd[0] == '?' && cmd[1] == '\0') {
            ask_focus_refresh();
            return;
        }
        push_focus_cmd(cmd);
    }

    void on_back_home() override {
        nav::back(nav_);
        todoist::bump(gate_);
        ui::show_home();
        paint_summary();
        paint_aitokens();
        paint_media();
        paint_mac();
    }

    void on_open_settings() override { beneath_ = nav_.screen; nav::open_settings(nav_); }

    void on_settings_closed() override { nav_.screen = beneath_; }

    void on_widget_pref(const char *widget_id, const char *key, const char *value) override {
        if (!widget_id || !key || !value) return;
        web_portal::Request request = {};
        request.kind = web_portal::Kind::WidgetSettings;
        text::copy(request.widget_id, sizeof(request.widget_id), widget_id);
        text::copy(request.setting_key, sizeof(request.setting_key), key);
        text::copy(request.setting_value, sizeof(request.setting_value), value);
        on_widget_settings(request);
    }

    void reload_dash() { dash_ = settings::load_dashboard(); }

    bool tile_on(const char *id) const { return dashboard::is_enabled(dash_, id); }

    void paint_weather() {
        if (!tile_on(dashboard::id_weather())) return;
        ui::set_weather(weather::view(millis()));
    }

    void paint_aitokens() {
        if (!tile_on(dashboard::id_aitokens())) return;
        ui::set_aitokens(aitokens_);
    }

    void paint_media() {
        if (!tile_on(dashboard::id_media())) return;
        ui::set_media(media_, agent_.ok);
    }

    void paint_mac() {
        if (!tile_on(dashboard::id_mac())) return;
        ui::set_mac(focus_, agent_.ok);
    }

    void ask_media_refresh() {
        if (!tile_on(dashboard::id_media())) return;
        Serial.println("MEDIA?");
    }

    void ask_focus_refresh() {
        if (!tile_on(dashboard::id_mac())) return;
        Serial.println("FOCUS?");
    }

    void push_media_cmd(const char *cmd) {
        if (!cmd || !cmd[0]) return;
        char line[32];
        snprintf(line, sizeof(line), "MEDIA\t%s", cmd);
        Serial.println(line);
    }

    void push_focus_cmd(const char *cmd) {
        if (!cmd || !cmd[0]) return;
        char line[96];
        if (strcmp(cmd, "OFF") == 0) {
            snprintf(line, sizeof(line), "FOCUS\tOFF");
        } else {
            snprintf(line, sizeof(line), "FOCUS\t%s", cmd);  // SET\t<id>
        }
        Serial.println(line);
    }

    void paint_summary() {
        if (!tile_on(dashboard::id_todoist())) return;
        todoist::ListTally rows[kMaxViews];
        int count = task_store::view_count();
        for (int i = 0; i < count; ++i) {
            int tasks = 0;
            task_store::tasks(i, &tasks);
            rows[i].shown = true;
            rows[i].ready = task_store::state(i) == task_store::State::Ready;
            rows[i].count = tasks;
        }
        todoist::Summary summary = todoist::summarize(rows, count);
        ui::set_summary(summary.total, summary.pending);
    }

    void on_settings_changed(const DeviceSettings &next) override {
        bool turned = next.portrait != device_.portrait;
        bool was_dark = resolve_dark(device_);
        device_ = next;
        bool theme = resolve_dark(device_) != was_dark;
        palette::set_mode(resolve_dark(device_) ? palette::Mode::Dark : palette::Mode::Light);
        settings::save_device(device_);
        // Dual-write: Todoist sync lives in widget namespace (portal/device panel).
        widget_settings::set_todoist_sync_minutes(device_.sync_minutes);
        widget_settings::set_bool(dashboard::id_aitokens(), "show_aitokens_codex", device_.show_aitokens_codex);
        widget_settings::set_bool(dashboard::id_aitokens(), "show_aitokens_claude", device_.show_aitokens_claude);
        widget_settings::set_bool(dashboard::id_aitokens(), "show_aitokens_cursor", device_.show_aitokens_cursor);
        chime::set_enabled(device_.sound);
        screen_rest::set_brightness_setting(rest_, device_.brightness);
        screen_rest::set_idle_minutes(rest_, device_.rest_minutes);
        screen_rest::set_clock_minutes(rest_, device_.clock_minutes);
        if (rest_.phase == screen_rest::Phase::Awake) display_host::set_brightness(device_.brightness);
        if (device_.rest_minutes == 0 && rest_.phase != screen_rest::Phase::Awake) {
            screen_rest::force(rest_, screen_rest::Phase::Awake, millis());
            if (screensaver::visible()) restore_after_rest();
            display_host::set_brightness(device_.brightness);
            sync_rest_input();
        }
        if (turned) {
            display_host::set_rotation(rotation_for(device_.portrait));
            ui::relayout();
            Serial.printf("tela %s\n", device_.portrait ? "em pe" : "deitada");
        }
        if (theme) ui::apply_theme();
        // Auto → ask Agente for current macOS appearance (portal alone is the browser, not the Mac).
        if (device_.appearance == Appearance::Auto) Serial.println("THEME?");
        publish();
        paint_summary();
    }

    void on_task_checked() override {
        chime::play();
        last_check_ = millis();
    }

    void on_task_completed(int view, int row) override {
        int count = 0;
        const TaskItem *tasks = task_store::tasks(view, &count);
        if (!tasks || row < 0 || row >= count) return;
        char id[20];
        text::copy(id, sizeof(id), tasks[row].id);
        task_store::remove(view, id);
        ui::drop_row(view, row);
        remember_closed(id);
        for (int i = 0; i < task_store::view_count(); ++i) {
            if (i != view && task_store::remove(i, id) >= 0) ui::view_changed(i);
        }
        enqueue_close(view, id);
        last_check_ = millis();
        paint_summary();
    }

    // ConsoleHandler
    void on_wifi(const char *ssid, const char *pass) override {
        if (!pass || !pass[0]) {
            settings::Credentials kept = settings::load_credentials();
            settings::save_wifi(ssid, kept.pass);
        } else {
            settings::save_wifi(ssid, pass);
        }
        Serial.println("wi-fi salvo");
        configure();
    }

    void on_token(const char *token) override {
        settings::save_token(token);
        Serial.println("token salvo");
        configure();
    }

    void on_reload() override { on_refresh_requested(); }

    void on_snapshot() override {
        lv_timer_handler();
        display_host::refresh_now();
        display_host::capture(Serial);
    }

    void on_perf() override {
        ui::Stats stats = ui::take_stats();
        logger::lock();
        display_host::report(Serial);
        Serial.printf("UI strip=%.1fms land=%.1fms preview=%.1fms repaint=%.1fms sliding=%d tracking=%d completing=%d\n",
                      stats.strip_us / 1000.0, stats.land_us / 1000.0, stats.preview_us / 1000.0,
                      stats.repaint_us / 1000.0, stats.sliding, stats.tracking, stats.completing);
        input_host::report(Serial);
        logger::unlock();
    }

    void on_drag(int x0, int y0, int x1, int y1, uint32_t ms) override { input_host::inject_drag(x0, y0, x1, y1, ms); }

    void on_enter_setup() override {
        settings::stash();
        settings::clear_credentials();
        ++epoch_;
        phase_ = Phase::Unconfigured;
        need_token_ = false;
        arm_setup_ = true;
        Serial.println("setup");
    }

    void on_restore() override {
        if (!settings::unstash()) {
            Serial.println("sem copia");
            return;
        }
        setup_commit_at_ = 0;
        arm_setup_ = false;
        ++epoch_;
        configure();
        Serial.println("restaurado");
    }

    void on_drop_token() override {
        settings::stash();
        settings::clear_token();
        ++epoch_;
        todoist_ui_mounted_ = false;
        todoist_token_seeded_ = false;
        configure();
        Serial.println("sem token");
    }

    void on_sd_status() override {
        sd_host::remount();
        char line[96];
        sd_host::describe(line, sizeof(line));
        Serial.println(line);
        if (sd_host::ready()) sd_host::list_dir("/tessera");
        weather_icons::apply_theme();
        paint_weather();
    }

    void on_aitokens(const char *line) override {
        if (!aitokens::apply_line(aitokens_, line, millis())) return;
        ui::aitokens_refresh_done();
        paint_aitokens();
    }

    void on_agent(const char *line) override {
        const bool was_ok = agent_.ok;
        if (!agent::apply_line(agent_, line, millis())) return;
        if (agent_.ok) {
            // Bridge is up even before the first CALENDAR events line lands.
            if (!calendar_.bridge_ok) {
                calendar_.bridge_ok = true;
                paint_calendar();
            }
            // Full capability refresh only on connect edge — not every AGENT heartbeat.
            if (!was_ok) {
                ask_calendar_refresh();
                calendar_ask_at_ = millis();
                ask_todoist_refresh();
                todoist_ask_at_ = millis();
                ask_media_refresh();
                ask_focus_refresh();
                if (device_.appearance == Appearance::Auto) Serial.println("THEME?");
            }
            paint_media();
            paint_mac();
        } else {
            paint_media();
            paint_mac();
        }
        settings_panel_show_agent();
        publish();
    }

    void on_media(const char *line) override {
        if (!line) return;
        if (strncmp(line, "MEDIA_COVER\t", 12) == 0) {
            if (media::apply_cover_line(media_, line)) paint_media();
            return;
        }
        if (!media::apply_line(media_, line, millis())) return;
        paint_media();
    }

    void on_focus(const char *line) override {
        if (!focus::apply_line(focus_, line, millis())) return;
        paint_mac();
    }

    void on_theme(const char *line) override {
        if (!line || strncmp(line, "THEME\t", 6) != 0) return;
        // THEME\tv1\t1\tdark|light
        const char *p = line + 6;
        if (strncmp(p, "v1\t", 3) != 0) return;
        p += 3;
        if (*p != '1') return;
        ++p;
        if (*p == '\t') ++p;
        bool dark = false;
        if (!strncmp(p, "dark", 4)) dark = true;
        else if (!strncmp(p, "light", 5)) dark = false;
        else return;
        bool was = resolve_dark(device_);
        device_.os_prefers_dark = dark;
        device_.os_theme_known = true;
        settings::save_device(device_);
        if (device_.appearance != Appearance::Auto) return;
        bool want = resolve_dark(device_);
        if (want == was && (palette::mode() == palette::Mode::Dark) == want) return;
        palette::set_mode(want ? palette::Mode::Dark : palette::Mode::Light);
        ui::apply_theme();
    }

    void sync_calendar_link_from_catalog() {
        // Empty catalog (agent offline / still loading) must not demote linked or wipe
        // the event cache. Only a real CALENDAR line may clear link.
        if (calendar_catalog_.count <= 0) return;
        const bool was_linked = calendar_.linked;
        const bool was_bridge = calendar_.bridge_ok;
        calendar_.linked = true;
        if (agent_.ok) calendar_.bridge_ok = true;
        if (was_linked && was_bridge == calendar_.bridge_ok) return;
        paint_calendar();
    }

    void on_calendar(const char *line) override {
        if (!line || !line[0]) return;
        bool changed = false;
        if (calendar::apply_auth_line(calendar_catalog_, line)) changed = true;
        else if (calendar::apply_catalog_line(calendar_catalog_, line)) {
            char sel[512] = {};
            if (widget_settings::get(dashboard::id_calendar(), "selection", sel, sizeof(sel)) && sel[0]) {
                calendar::apply_selection(calendar_catalog_, sel);
            }
            sync_calendar_link_from_catalog();
            changed = true;
        } else if (calendar::apply_line(calendar_, line, millis())) {
            changed = true;
            paint_calendar();
        }
        if (changed) publish();
    }

    void on_todoist(const char *line) override {
        if (!line || !line[0]) return;
        bool ok = false;
        char id[20] = {};
        if (todoist_bridge::apply_close_ack(line, &ok, id, sizeof(id))) {
            if (ok) drop_close(id);
            else {
                drop_close(id);
                forget_closed(id);
                ui::set_status("falha ao fechar");
            }
            close_usb_wait_ = false;
            return;
        }
        if (!todoist_bridge::apply_line(line, millis())) return;
        const todoist_bridge::Snapshot *snap = todoist_bridge::last();
        if (!snap) return;
        ingest_todoist_usb(*snap);
    }

    bool todoist_via_agent() const {
        const todoist_bridge::Status st = todoist_bridge::status();
        return agent_.ok && st.bridge_ok && st.linked && st.ready;
    }

    void ask_todoist_refresh(bool force = false) {
        if (!tile_on(dashboard::id_todoist())) return;
        // One in-flight ask — overlapping USB bundles used to zero list counts.
        if (todoist_awaiting_ && millis() - todoist_ask_at_ < 15000u) return;
        seed_todoist_token_to_agent();
        if (force || refreshing_) Serial.println("TODOIST?\tforce");
        else Serial.println("TODOIST?");
        todoist_awaiting_ = true;
        todoist_ask_at_ = millis();
        if (!todoist_quiet_since_) todoist_quiet_since_ = millis();
    }

    // A silent agent used to leave every list spinning forever. After a few seconds say so instead:
    // lists that already hold cached tasks keep showing them, the rest get a message.
    void give_up_on_todoist() {
        if (!todoist_quiet_since_ || millis() - todoist_quiet_since_ < 12000u) return;
        todoist_quiet_since_ = 0;
        if (task_store::view_count() == 0) {
            // Nothing cached to fall back to: at least stop claiming the lists are on their way.
            if (ui::loader_visible()) ui::show_loader("Sem resposta do Mac", "Tentando de novo as tarefas");
            return;
        }
        bool changed = false;
        for (int view = 0; view < task_store::view_count(); ++view) {
            if (task_store::state(view) != task_store::State::Empty) continue;
            task_store::fail(view, "Sem resposta do Mac.");
            changed = true;
        }
        if (changed) ui::view_changed(ui::active_view());
    }

    void seed_todoist_token_to_agent() {
        if (todoist_token_seeded_ || !agent_.ok) return;
        settings::Credentials credentials = settings::load_credentials();
        if (!credentials.token[0]) return;
        // Migrate NVS token → Mac (~/.tessera/todoist/) once per boot; never log the value.
        Serial.printf("TODOIST_TOKEN\t%s\n", credentials.token);
        todoist_token_seeded_ = true;
    }

    void ingest_todoist_usb(const todoist_bridge::Snapshot &snap) {
        todoist_awaiting_ = false;
        todoist_quiet_since_ = 0;
        if (!snap.linked) {
            // Stay in Views until a linked snapshot builds chips — otherwise we skip
            // set_views and the horizontal list slide never mounts.
            if (ui::loader_visible()) ui::hide_loader();
            paint_summary();
            publish();
            return;
        }
        need_token_ = false;
        ui::hide_token_needed();
        ViewItem catalog[kMaxViews];
        for (int i = 0; i < snap.view_count; ++i) catalog[i] = snap.views[i].item;
        char active_name[sizeof(ViewItem::name)] = "";
        int keep = ui::active_view();
        if (keep >= 0 && keep < task_store::view_count()) {
            text::copy(active_name, sizeof(active_name), task_store::view(keep).name);
        }
        const int prev_n = task_store::view_count();
        remember_catalog(catalog, snap.view_count);
        int selected = apply_visible(active_name);
        for (int i = 0; i < task_store::view_count(); ++i) {
            const char *q = task_store::view(i).query;
            for (int v = 0; v < snap.view_count; ++v) {
                if (strcmp(snap.views[v].item.query, q)) continue;
                // Skip views whose TASKS line never arrived (overlap left count=0).
                if (!snap.views[v].tasks_received) break;
                store_filtered(i, snap.views[v].tasks, snap.views[v].count);
                break;
            }
        }
        const bool need_mount =
            !todoist_ui_mounted_ || prev_n == 0 || task_store::view_count() != prev_n || refreshing_;
        phase_ = Phase::Running;
        if (need_mount) {
            ui::set_views(selected);
            todoist_ui_mounted_ = true;
        } else {
            // Soft refresh of the visible page only — do not rebuild chips/sheet (kills slide).
            ui::view_changed(ui::active_view());
        }
        if (refreshing_) finish_refresh();
        if (ui::loader_visible()) ui::hide_loader();
        paint_summary();
        publish();
    }

    void paint_calendar() {
        if (!tile_on(dashboard::id_calendar())) return;
        ui::set_calendar(calendar_);
    }

    void ask_calendar_refresh() {
        if (!tile_on(dashboard::id_calendar())) return;
        Serial.println("CALENDAR?");
    }

    void ask_calendar_auth() { Serial.println("CALENDAR_AUTH?"); }

    void push_calendar_selection() {
        char sel[512] = {};
        calendar::format_selection(calendar_catalog_, sel, sizeof(sel));
        widget_settings::set(dashboard::id_calendar(), "selection", sel);
        // USB: CALENDAR_SEL\tid1\tid2… (may span long Google ids)
        char line[768] = "CALENDAR_SEL";
        for (int i = 0; i < calendar_catalog_.count; ++i) {
            if (!calendar_catalog_.items[i].selected || !calendar_catalog_.items[i].id[0]) continue;
            size_t used = strlen(line);
            size_t id_len = strlen(calendar_catalog_.items[i].id);
            if (used + 1 + id_len + 1 >= sizeof(line)) break;
            line[used] = '\t';
            memcpy(line + used + 1, calendar_catalog_.items[i].id, id_len);
            line[used + 1 + id_len] = '\0';
        }
        Serial.println(line);
        ask_calendar_refresh();
    }

    bool agent_pack_ready() const {
        return sd_host::ready() && sd_host::exists("/tessera/agent.tgz");
    }

    void read_agent_pack_version(char *dest, size_t cap) const {
        if (!dest || !cap) return;
        dest[0] = '\0';
        size_t n = 0;
        uint8_t *raw = sd_host::read_file("/tessera/agent.version", &n);
        if (!raw || !n) {
            sd_host::free_buf(raw);
            return;
        }
        size_t len = n < cap - 1 ? n : cap - 1;
        while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r' || raw[len - 1] == ' ')) --len;
        std::memcpy(dest, raw, len);
        dest[len] = '\0';
        sd_host::free_buf(raw);
    }

    void settings_panel_show_agent() {
        char ver[24] = {};
        if (agent_pack_ready()) read_agent_pack_version(ver, sizeof(ver));
        ui::set_agent(agent::for_display(agent_, millis()), agent_pack_ready(), ver);
    }

    void on_flash() override {
        screen_rest::force(rest_, screen_rest::Phase::Awake, millis());
        if (screensaver::visible()) restore_after_rest();
        display_host::set_brightness(device_.brightness);
        sync_rest_input();
        ui::show_loader("Atualizando firmware", "Não desligue o painel");
        ui::set_loader_progress(-1);
        lv_timer_handler();
        display_host::refresh_now();
        if (board_->backlight) board_->backlight->hold_through_reset();
        Serial.println("flash pronto");
        Serial.flush();
    }

    void on_agent_push(bool on) override {
        if (on) {
            agent_push::arm();
            Serial.println("agent push pronto");
        } else {
            agent_push::disarm();
            Serial.println("agent push off");
        }
        Serial.flush();
    }

    void on_rest(const char *arg) override {
        uint32_t now = millis();
        if (!arg || !arg[0] || !strcmp(arg, "wake")) {
            rest_.wake_touch_pending = false;
            on_rest_wake();
            Serial.println("rest awake");
            return;
        }
        if (!strcmp(arg, "saver")) {
            screen_rest::force(rest_, screen_rest::Phase::Saver, now);
            enter_saver_ui();
            apply_rest_brightness();
            sync_rest_input();
            Serial.println("rest saver");
            return;
        }
        if (!strcmp(arg, "flip")) {
            if (!screensaver::visible()) {
                screen_rest::force(rest_, screen_rest::Phase::Saver, now);
                enter_saver_ui();
                apply_rest_brightness();
                sync_rest_input();
            }
            screensaver::debug_flip();
            Serial.println("rest flip");
            return;
        }
        if (!strcmp(arg, "fade")) {
            if (!screensaver::visible()) enter_saver_ui();
            screen_rest::force(rest_, screen_rest::Phase::Fading, now);
            apply_rest_brightness();
            sync_rest_input();
            Serial.println("rest fade");
            return;
        }
        if (!strcmp(arg, "off")) {
            if (!screensaver::visible()) enter_saver_ui();
            screen_rest::force(rest_, screen_rest::Phase::Off, now);
            apply_rest_brightness();
            sync_rest_input();
            Serial.println("rest off");
            return;
        }
        Serial.println("rest ?");
    }

private:
    bool rest_allowed() const {
        return !ui::loader_visible() && !web_portal::setup_active() && !arm_setup_;
    }

    void sync_rest_input() {
        input_host::set_resting(rest_.phase != screen_rest::Phase::Awake);
    }

    void apply_rest_brightness() {
        display_host::set_brightness(screen_rest::brightness_now(rest_, millis()));
    }

    void enter_saver_ui() {
        under_rest_ = nav_.screen;
        std::strncpy(under_rest_widget_, nav_.widget_id, sizeof(under_rest_widget_) - 1);
        under_rest_widget_[sizeof(under_rest_widget_) - 1] = '\0';
        screensaver::show();
    }

    void restore_after_rest() {
        screensaver::hide();
        switch (under_rest_) {
            case nav::Screen::Detail:
                if (under_rest_widget_[0]) {
                    nav::open_detail(nav_, under_rest_widget_);
                    ui::show_widget(under_rest_widget_);
                } else {
                    nav::back(nav_);
                    ui::show_home();
                }
                break;
            case nav::Screen::Settings:
            case nav::Screen::Home:
            default:
                nav::back(nav_);
                ui::show_home();
                break;
        }
        under_rest_ = nav::Screen::Home;
        under_rest_widget_[0] = '\0';
    }

    void pump_screen_rest() {
        uint32_t now = millis();
        screen_rest::Phase before = rest_.phase;
        bool changed = screen_rest::tick(rest_, now, rest_allowed());
        if (rest_.phase == screen_rest::Phase::Saver || rest_.phase == screen_rest::Phase::Fading) {
            if (!screensaver::visible()) enter_saver_ui();
            screensaver::tick(now);
        }
        if (changed) {
            if (rest_.phase == screen_rest::Phase::Saver && before == screen_rest::Phase::Awake) enter_saver_ui();
            if (rest_.phase == screen_rest::Phase::Awake && screensaver::visible()) restore_after_rest();
            sync_rest_input();
            Serial.printf("rest %s\n",
                          rest_.phase == screen_rest::Phase::Awake   ? "awake"
                          : rest_.phase == screen_rest::Phase::Saver ? "saver"
                          : rest_.phase == screen_rest::Phase::Fading ? "fade"
                                                                     : "off");
        }
        if (rest_.phase == screen_rest::Phase::Fading || rest_.phase == screen_rest::Phase::Off || changed) {
            apply_rest_brightness();
        }
    }
    hal::Rotation rotation_for(bool portrait) const {
        return portrait ? board_->descriptor.portrait : board_->descriptor.landscape;
    }

    uint32_t stale_ms() const {
        return widget_settings::todoist_sync_minutes(device_.sync_minutes) * 60000u;
    }

    void on_widget_settings(const web_portal::Request &request) {
        const char *id = request.widget_id;
        const char *key = request.setting_key;
        const char *value = request.setting_value;
        if (!id[0] || !key[0]) return;
        if (std::strcmp(id, dashboard::id_todoist()) == 0 &&
            (std::strcmp(key, "sync_minutes") == 0 || std::strcmp(key, "sync") == 0)) {
            uint8_t minutes = clamp_sync(std::atoi(value));
            widget_settings::set_todoist_sync_minutes(minutes);
            device_.sync_minutes = minutes;
            settings::save_device(device_);
            publish();
            return;
        }
        if (std::strcmp(id, dashboard::id_aitokens()) == 0) {
            bool on = value[0] == '1' || value[0] == 't' || value[0] == 'T' || std::atoi(value) != 0;
            if (std::strstr(key, "codex")) {
                widget_settings::set_bool(id, key, on);
                device_.show_aitokens_codex = on;
            } else if (std::strstr(key, "claude")) {
                widget_settings::set_bool(id, key, on);
                device_.show_aitokens_claude = on;
            } else if (std::strstr(key, "cursor")) {
                widget_settings::set_bool(id, key, on);
                device_.show_aitokens_cursor = on;
            } else {
                widget_settings::set(id, key, value);
                publish();
                return;
            }
            settings::save_device(device_);
            paint_aitokens();
            publish();
            return;
        }
        if (std::strcmp(id, dashboard::id_clock()) == 0) {
            widget_settings::set(id, key, value);
            if (std::strcmp(key, "timezone") == 0) {
                daytime::set_timezone(value);
            }
            face_prefs::reload();
            publish();
            return;
        }
        if (std::strcmp(id, dashboard::id_date()) == 0) {
            widget_settings::set(id, key, value);
            face_prefs::reload();
            publish();
            return;
        }
        if (std::strcmp(id, dashboard::id_calendar()) == 0) {
            widget_settings::set(id, key, value);
            if (std::strcmp(key, home_tile::kSettingKey) == 0) {
                paint_calendar();
            }
            publish();
            return;
        }
        widget_settings::set(id, key, value);
        publish();
    }

    bool settling() const { return millis() - last_check_ < kCloseSettleMs; }

    void try_start_setup() {
        if (net_worker::busy()) return;
        arm_setup_ = false;
        phase_ = Phase::Unconfigured;
        need_token_ = false;
        ui::hide_loader();
        ui::show_setup("Primeiro uso", "Conecte o celular na rede", web_portal::setup_ap(),
                       "A página abre sozinha. Se não abrir, escaneie o QR ou digite o endereço abaixo.",
                       web_portal::setup_url());
        display_host::refresh_now();
        web_portal::start_setup();
    }

    void on_setup(const web_portal::Request &request) {
        settings::save_wifi(request.ssid, request.pass);
        if (request.token[0]) settings::save_token(request.token);
        setup_commit_at_ = millis() + 600;
    }

    void publish() {
        DeviceInfo info = {};
        text::copy(info.host, sizeof(info.host), web_portal::host());
        net_worker::describe(info);
        ui::set_device(device_, info);
        // Static: Snapshot is large; stack allocation here was rebooting the board.
        static web_portal::Snapshot snapshot;
        snapshot = {};
        snapshot.settings = device_;
        snapshot.info = info;
        list_prefs::Choice choices[kMaxViews];
        snapshot.list_count = list_prefs::arrange(catalog_, catalog_n_, choices, kMaxViews);
        for (int i = 0; i < snapshot.list_count; ++i) {
            text::copy(snapshot.lists[i].name, sizeof(snapshot.lists[i].name), choices[i].name);
            snapshot.lists[i].shown = choices[i].shown;
        }
        snapshot.token_set = settings::load_credentials().token[0] != '\0';
        settings::WeatherPlace saved = settings::load_weather();
        if (saved.set) text::copy(snapshot.weather_city, sizeof(snapshot.weather_city), saved.name);
        weather::View seen = weather::view(millis());
        snapshot.weather_ready = seen.placed && seen.ready && !seen.stale;
        snapshot.dashboard = settings::load_dashboard();
        agent::View agent_ui = agent::for_display(agent_, millis());
        snapshot.agent.ready = agent_ui.ready;
        snapshot.agent.ok = agent_ui.ok;
        text::copy(snapshot.agent.detail, sizeof(snapshot.agent.detail), agent_ui.detail);
        snapshot.agent_pack = agent_pack_ready();
        snapshot.agent_push_armed = agent_push::armed();
        snapshot.agent_pack_version[0] = '\0';
        if (snapshot.agent_pack) read_agent_pack_version(snapshot.agent_pack_version, sizeof(snapshot.agent_pack_version));
        snapshot.aitokens_ready = aitokens_.ready;
        snapshot.aitokens_stale = aitokens_.stale;
        snapshot.aitokens_age_s =
            aitokens_.ready && aitokens_.received_ms ? (millis() - aitokens_.received_ms) / 1000u : 0;
        snapshot.calendar_ready = calendar_.ready;
        snapshot.calendar_stale = calendar_.stale;
        snapshot.calendar_linked = calendar_.linked || calendar_catalog_.count > 0;
        snapshot.calendar_auth_pending = calendar_catalog_.auth_pending;
        snapshot.calendar_age_s =
            calendar_.ready && calendar_.received_ms ? (millis() - calendar_.received_ms) / 1000u : 0;
        text::copy(snapshot.calendar_status, sizeof(snapshot.calendar_status), calendar_catalog_.status);
        calendar::format_selection(calendar_catalog_, snapshot.calendar_selection,
                                   sizeof(snapshot.calendar_selection));
        static web_portal::CalendarCatalog catalog_view;
        catalog_view = {};
        for (int i = 0; i < calendar_catalog_.count && catalog_view.count < web_portal::kMaxCalendarRows; ++i) {
            auto &dst = catalog_view.rows[catalog_view.count++];
            text::copy(dst.account, sizeof(dst.account), calendar_catalog_.items[i].account);
            text::copy(dst.id, sizeof(dst.id), calendar_catalog_.items[i].id);
            text::copy(dst.name, sizeof(dst.name), calendar_catalog_.items[i].name);
            dst.color_rgb = calendar_catalog_.items[i].color_rgb;
            dst.selected = calendar_catalog_.items[i].selected;
        }
        snapshot.resources = device_resources::sample();
        if (!widget_settings::get(dashboard::id_clock(), "timezone", snapshot.clock_timezone,
                                 sizeof(snapshot.clock_timezone)) ||
            !snapshot.clock_timezone[0]) {
            text::copy(snapshot.clock_timezone, sizeof(snapshot.clock_timezone), "sao_paulo");
        }
        if (!widget_settings::get(dashboard::id_clock(), "time_format", snapshot.clock_time_format,
                                 sizeof(snapshot.clock_time_format)) ||
            !snapshot.clock_time_format[0]) {
            text::copy(snapshot.clock_time_format, sizeof(snapshot.clock_time_format), "hm");
        }
        if (!widget_settings::get(dashboard::id_date(), "date_format", snapshot.date_format,
                                 sizeof(snapshot.date_format)) ||
            !snapshot.date_format[0]) {
            text::copy(snapshot.date_format, sizeof(snapshot.date_format), "short_pt");
        }
        if (!widget_settings::get(dashboard::id_calendar(), home_tile::kSettingKey,
                                  snapshot.calendar_home_tile_size,
                                  sizeof(snapshot.calendar_home_tile_size)) ||
            !snapshot.calendar_home_tile_size[0]) {
            text::copy(snapshot.calendar_home_tile_size, sizeof(snapshot.calendar_home_tile_size),
                       home_tile::to_string(home_tile::Size::M));
        }
        ui::set_face(snapshot.clock_timezone, snapshot.clock_time_format, snapshot.date_format);
        web_portal::publish(snapshot);
        web_portal::publish_calendar_catalog(catalog_view);
        ui::set_agent(agent_ui, agent_pack_ready(), snapshot.agent_pack_version);
        paint_summary();
        paint_weather();
    }

    void serve_portal() {
        web_portal::Request request;
        if (!web_portal::take(request)) return;
        switch (request.kind) {
            case web_portal::Kind::Settings: on_settings_changed(request.settings); break;
            case web_portal::Kind::Wifi: on_wifi(request.ssid, request.pass); break;
            case web_portal::Kind::Token: on_token(request.token); break;
            case web_portal::Kind::Refresh: on_refresh_requested(); break;
            case web_portal::Kind::Setup: on_setup(request); break;
            case web_portal::Kind::Lists: on_lists(request); break;
            case web_portal::Kind::WeatherPlace: on_weather_place(request); break;
            case web_portal::Kind::Dashboard: {
                const bool had_cal = tile_on(dashboard::id_calendar());
                const bool had_media = tile_on(dashboard::id_media());
                const bool had_mac = tile_on(dashboard::id_mac());
                settings::save_dashboard(request.dashboard);
                dash_ = request.dashboard;
                ui::rebuild_home(dash_);
                paint_summary();
                paint_weather();
                paint_aitokens();
                paint_calendar();
                // Kick USB only when a tile was just turned on (hidden stays quiet).
                if (!had_cal && tile_on(dashboard::id_calendar())) ask_calendar_refresh();
                if (!had_media && tile_on(dashboard::id_media())) ask_media_refresh();
                if (!had_mac && tile_on(dashboard::id_mac())) ask_focus_refresh();
                publish();
                break;
            }
            case web_portal::Kind::WidgetSettings:
                on_widget_settings(request);
                break;
            case web_portal::Kind::CalendarAdd:
                ask_calendar_auth();
                publish();
                break;
            case web_portal::Kind::CalendarSelection: {
                widget_settings::set(dashboard::id_calendar(), "selection", request.calendar_selection);
                calendar::apply_selection(calendar_catalog_, request.calendar_selection);
                push_calendar_selection();
                publish();
                break;
            }
        }
        memset(&request, 0, sizeof(request));
    }

    void configure() {
        settings::Credentials credentials = settings::load_credentials();
        if (net_worker::busy()) {
            reconfigure_ = true;
            return;
        }
        reconfigure_ = false;
        if (!credentials.ssid[0]) {
            phase_ = Phase::Unconfigured;
            need_token_ = false;
            ui::hide_token_needed();
            if (net_worker::busy()) arm_setup_ = true;
            else try_start_setup();
            return;
        }
        arm_setup_ = false;
        web_portal::stop_setup();
        ui::hide_setup();
        ui::hide_token_needed();
        if (!credentials.token[0]) {
            // Token may live only on the Mac (agent). Still bring Wi‑Fi up for weather/portal.
            need_token_ = true;
            phase_ = Phase::Connecting;
            net_worker::configure(credentials.ssid, credentials.pass, "");
            submit(net_worker::Kind::Connect);
            ui::show_loader("Conectando ao Wi-Fi", credentials.ssid);
            return;
        }
        need_token_ = false;
        net_worker::configure(credentials.ssid, credentials.pass, credentials.token);
        phase_ = Phase::Connecting;
        retry_at_ = 0;
        ++epoch_;
        ui::show_loader("Conectando ao Wi-Fi", credentials.ssid);
    }

    bool submit(net_worker::Kind kind, int view = -1, const char *id = nullptr) {
        net_worker::Job job = {};
        job.kind = kind;
        job.tag = epoch_;
        job.view = view;
        if (view >= 0) text::copy(job.query, sizeof(job.query), task_store::view(view).query);
        if (id) text::copy(job.id, sizeof(job.id), id);
        if (kind == net_worker::Kind::Tasks) inflight_gen_ = todoist::stamp(gate_);
        return net_worker::submit(job);
    }

    void feed_worker() {
        if (net_worker::busy()) return;
        if (reconfigure_) {
            configure();
            return;
        }
        if (web_portal::setup_active() || arm_setup_ || setup_commit_at_) return;
        if (pending_weather_city_[0]) {
            try_weather_geocode();
            if (net_worker::busy()) return;
        }
        if (millis() < retry_at_) return;
        switch (phase_) {
            case Phase::Unconfigured:
                if (need_token_ && !net_worker::online()) submit(net_worker::Kind::Connect);
                return;
            case Phase::Connecting: submit(net_worker::Kind::Connect); return;
            case Phase::Views:
                if (agent_.ok) {
                    ask_todoist_refresh();
                    return;
                }
                submit(net_worker::Kind::Views);
                return;
            case Phase::Running: break;
        }
        if (refreshing_) {
            // Pull-to-refresh is Todoist-centric — skip when that tile is off the home.
            if (!tile_on(dashboard::id_todoist())) {
                refreshing_ = false;
                ui::hide_loader();
                return;
            }
            if (todoist_via_agent() || (agent_.ok && todoist_bridge::status().bridge_ok)) {
                ask_todoist_refresh(true);
                return;
            }
            if (refresh_next_ < 0) submit(net_worker::Kind::Views);
            else submit(net_worker::Kind::Tasks, refresh_order(refresh_next_));
            return;
        }
        bool quiet = settling();
        if (closes_n > 0 && !quiet && !close_usb_wait_) {
            if (todoist_via_agent() || agent_.ok) {
                Serial.printf("TODOIST_CLOSE\t%s\n", closes[0].id);
                close_usb_wait_ = true;
                return;
            }
            submit(net_worker::Kind::Close, closes[0].view, closes[0].id);
            return;
        }
        if ((quiet || closes_n > 0) && wanted_ < 0) return;
        // Prefer agent USB for Todoist — avoids mbedTLS DRAM pressure on the panel.
        if (todoist_via_agent()) {
            // Warm via agent heartbeat / TODOIST?; no Wi‑Fi Tasks.
        } else if (tile_on(dashboard::id_todoist())) {
            int view = next_fetch();
            if (view >= 0) {
                submit(net_worker::Kind::Tasks, view);
                return;
            }
        }
        if (tile_on(dashboard::id_weather()) && !refreshing_ && closes_n == 0 && !quiet && wanted_ < 0 &&
            weather::wants_fetch(millis()) && millis() - last_weather_warm_ >= kWarmGapMs) {
            last_weather_warm_ = millis();
            weather_gen_ = weather::note_request();
            submit(net_worker::Kind::WeatherForecast);
        }
    }

    bool needs_fetch(int view) const {
        task_store::State state = task_store::state(view);
        if (state == task_store::State::Empty) return true;
        if (state == task_store::State::Failed && task_store::age_ms(view) >= kFailRetryMs) return true;
        return false;
    }

    // The visible list first, then its neighbours outward, then one stale list now and then.
    int next_fetch() {
        int count = task_store::view_count();
        if (wanted_ >= 0 && wanted_ < count) return wanted_;
        int active = ui::active_view();
        for (int d = 0; d < count; ++d) {
            for (int side : {active + d, active - d}) {
                if (side >= 0 && side < count && needs_fetch(side)) return side;
            }
        }
        if (millis() - last_warm_ < kWarmGapMs) return -1;
        int oldest = -1;
        for (int i = 0; i < count; ++i) {
            if (task_store::age_ms(i) > stale_ms() && (oldest < 0 || task_store::age_ms(i) > task_store::age_ms(oldest))) {
                oldest = i;
            }
        }
        if (oldest >= 0) last_warm_ = millis();
        return oldest;
    }

    int refresh_order(int step) {
        int count = task_store::view_count();
        int active = ui::active_view();
        return (active + step) % (count ? count : 1);
    }

    void pump_widget_http() {
        if (net_worker::busy()) return;
        char url[400];
        uint32_t gen = 0;
        int http_job = tessera::device_http().take_pending(url, sizeof(url), &gen);
        if (http_job < 0) return;
        (void)gen;
        net_worker::Job job = {};
        job.kind = net_worker::Kind::HttpGet;
        job.tag = epoch_;
        job.http_job = http_job;
        text::copy(job.query, sizeof(job.query), url);
        if (!net_worker::submit(job)) {
            tessera::device_http().complete(http_job, tessera::HttpStatus::Network, nullptr, 0);
        }
    }

    void drain_worker() {
        net_worker::Result result;
        if (!net_worker::take(result)) return;
        if (result.tag != epoch_ && result.kind != net_worker::Kind::Close &&
            result.kind != net_worker::Kind::HttpGet) {
            return;
        }
        switch (result.kind) {
            case net_worker::Kind::Connect: on_connected(result); break;
            case net_worker::Kind::Views: on_views(result); break;
            case net_worker::Kind::Tasks: on_tasks(result); break;
            case net_worker::Kind::Close: on_closed(result); break;
            case net_worker::Kind::WeatherGeocode:
            case net_worker::Kind::WeatherForecast: on_weather(result); break;
            case net_worker::Kind::HttpGet: {
                tessera::HttpStatus st =
                    result.ok ? tessera::HttpStatus::Ok : tessera::HttpStatus::Network;
                tessera::device_http().complete(result.http_job, st, result.body, result.body_len);
                paint_weather();
                publish();
                break;
            }
        }
    }

    void on_weather_place(const web_portal::Request &request) {
        if (request.weather_has_coords) {
            pending_weather_city_[0] = '\0';
            apply_weather_place(request.weather_name, request.weather_lat, request.weather_lon);
            return;
        }
        if (!request.weather_city[0]) return;
        text::copy(pending_weather_city_, sizeof(pending_weather_city_), request.weather_city);
        try_weather_geocode();
    }

    void try_weather_geocode() {
        if (!pending_weather_city_[0] || net_worker::busy()) return;
        net_worker::Job job = {};
        job.kind = net_worker::Kind::WeatherGeocode;
        job.tag = epoch_;
        text::copy(job.query, sizeof(job.query), pending_weather_city_);
        if (net_worker::submit(job)) pending_weather_city_[0] = '\0';
    }

    void apply_weather_place(const char *name, float lat, float lon) {
        settings::save_weather(name, lat, lon);
        weather::set_place(name, lat, lon);
        weather_gen_ = weather::note_request();
        last_weather_warm_ = millis();
        if (!submit(net_worker::Kind::WeatherForecast)) last_weather_warm_ = 0;
        publish();
        paint_weather();
    }

    void on_weather(const net_worker::Result &result) {
        if (result.kind == net_worker::Kind::WeatherGeocode) {
            if (!result.ok) {
                ui::set_status(result.error[0] ? result.error : "cidade não encontrada");
                publish();
                return;
            }
            apply_weather_place(result.place.name, result.place.latitude, result.place.longitude);
            return;
        }
        if (result.ok) {
            if (!weather::store(weather_gen_, result.forecast, millis())) {
                ui::set_status("previsão atrasada");
            }
        } else {
            weather::note_failure();
            ui::set_status(result.error[0] ? result.error : "clima falhou");
            last_weather_warm_ = 0;
        }
        paint_weather();
        publish();
    }

    void on_connected(const net_worker::Result &result) {
        if (phase_ == Phase::Unconfigured) {
            if (!result.ok) {
                retry_at_ = millis() + kRetryMs;
                ui::show_loader("Sem conexão", "Tentando de novo o Wi-Fi");
                return;
            }
            publish();
            if (!need_token_) return;
            ui::hide_loader();
            DeviceInfo info = {};
            net_worker::describe(info);
            char url[48];
            snprintf(url, sizeof(url), "http://%s/", info.ip[0] ? info.ip : "0.0.0.0");
            ui::show_token_needed(url);
            return;
        }
        if (!result.ok) {
            retry_at_ = millis() + kRetryMs;
            ui::show_loader("Sem conexão", "Tentando de novo o Wi-Fi");
            return;
        }
        phase_ = Phase::Views;
        ui::show_loader("Carregando", "");
        publish();
    }

    void remember_catalog(const ViewItem *views, int count) {
        if (count > kMaxViews) count = kMaxViews;
        if (count < 0) count = 0;
        for (int i = 0; i < count; ++i) catalog_[i] = views[i];
        catalog_n_ = count;
    }

    int apply_visible(const char *active_name) {
        ViewItem shown[kMaxViews];
        int count = list_prefs::visible(catalog_, catalog_n_, shown, kMaxViews);
        task_store::set_views(shown, count);
        int selected = 0;
        if (active_name && active_name[0]) {
            for (int i = 0; i < count; ++i) {
                if (!strcmp(shown[i].name, active_name)) selected = i;
            }
        }
        return selected;
    }

    void on_lists(const web_portal::Request &request) {
        if (!catalog_n_) return;
        char names[kMaxViews][28] = {};
        bool shown[kMaxViews] = {};
        int count = request.list_count;
        if (count > kMaxViews) count = kMaxViews;
        for (int i = 0; i < count; ++i) {
            text::copy(names[i], sizeof(names[i]), request.lists[i].name);
            shown[i] = request.lists[i].shown;
        }
        if (!list_prefs::save(catalog_, catalog_n_, names, shown, count)) return;
        char active_name[sizeof(ViewItem::name)] = "";
        int keep = ui::active_view();
        if (keep >= 0 && keep < task_store::view_count()) {
            text::copy(active_name, sizeof(active_name), task_store::view(keep).name);
        }
        int selected = apply_visible(active_name);
        ui::set_views(selected);
        // Order and visibility are local. Fetch only a visible list that has nothing cached yet.
        if (task_store::state(selected) == task_store::State::Empty) wanted_ = selected;
        else wanted_ = -1;
        publish();
    }

    int view_for(const net_worker::Result &result) const {
        if (result.query[0]) {
            for (int i = 0; i < task_store::view_count(); ++i) {
                if (!strcmp(task_store::view(i).query, result.query)) return i;
            }
            return -1;
        }
        if (result.view < 0 || result.view >= task_store::view_count()) return -1;
        return result.view;
    }

    void on_views(const net_worker::Result &result) {
        if (!result.ok) {
            retry_at_ = millis() + kRetryMs;
            if (refreshing_) finish_refresh();
            // TLS/catalog can fail under low DRAM — never leave the shell on a stuck loader.
            if (ui::loader_visible()) {
                ui::hide_loader();
                ui::set_status(result.error[0] ? result.error : "listas indisponíveis");
            }
            return;
        }
        int keep = ui::active_view();
        char active_name[sizeof(ViewItem::name)] = "";
        if (keep < task_store::view_count()) text::copy(active_name, sizeof(active_name), task_store::view(keep).name);
        remember_catalog(result.views, result.count);
        int selected = apply_visible(active_name);
        publish();
        bool first = phase_ != Phase::Running || !todoist_ui_mounted_;
        phase_ = Phase::Running;
        if (first || refreshing_) {
            ui::set_views(selected);
            todoist_ui_mounted_ = true;
        }
        if (first) {
            wanted_ = selected;
            ui::show_loader("Carregando", "");
        }
        if (refreshing_) {
            refresh_next_ = 0;
            report_refresh();
        }
    }

    void on_tasks(const net_worker::Result &result) {
        int view = view_for(result);
        bool fresh = todoist::accept(gate_, inflight_gen_);
        if (result.ok) {
            if (view >= 0) store_filtered(view, result.tasks, result.count);
            else if (result.query[0]) store_filtered(-1, result.tasks, result.count, result.query);
        } else if (view >= 0) {
            task_store::fail(view, result.error);
            // Per-list retry is in next_fetch (kFailRetryMs). Global backoff when offline or TLS/DRAM.
            if (!net_worker::online()) retry_at_ = millis() + kRetryMs;
            else if (strstr(result.error, "sem conexão") || strstr(result.error, "sem memória")) {
                retry_at_ = millis() + kRetryMs;
            }
        }
        if (view >= 0 && wanted_ == view) wanted_ = -1;
        if (fresh && view >= 0) ui::view_changed(view);
        else if (!fresh && ui::loader_visible()) ui::hide_loader();
        paint_summary();
        if (refreshing_ && refresh_next_ >= 0 && view == refresh_order(refresh_next_)) {
            ++refresh_next_;
            if (refresh_next_ >= task_store::view_count()) finish_refresh();
            else report_refresh();
        } else if (ui::loader_visible() && view == ui::active_view()) {
            ui::hide_loader();
        }
    }

    void on_closed(const net_worker::Result &result) {
        if (result.ok) {
            drop_close(result.id);
            return;
        }
        // The tick already left the screen. Keep it there until Todoist can take the close.
        if (hold_until_online(result)) {
            retry_at_ = millis() + kRetryMs;
            return;
        }
        drop_close(result.id);
        forget_closed(result.id);
        ui::set_status(result.error);
        task_store::invalidate_all();
        wanted_ = ui::active_view();
    }

    bool hold_until_online(const net_worker::Result &result) const {
        if (!net_worker::online()) return true;
        return result.status == 0 || result.status == 408 || result.status == 429 || result.status >= 500;
    }

    void enqueue_close(int view, const char *id) {
        if (!id || !id[0]) return;
        for (int i = 0; i < closes_n; ++i) {
            if (!strcmp(closes[i].id, id)) return;
        }
        if (closes_n >= kMaxPending) return;
        closes[closes_n].view = view;
        text::copy(closes[closes_n].id, sizeof(closes[closes_n].id), id);
        ++closes_n;
        persist_closes();
    }

    void drop_close(const char *id) {
        for (int i = 0; i < closes_n; ++i) {
            if (strcmp(closes[i].id, id)) continue;
            memmove(closes + i, closes + i + 1, sizeof(Close) * static_cast<size_t>(closes_n - i - 1));
            --closes_n;
            persist_closes();
            return;
        }
    }

    void load_closes() {
        char ids[kMaxPending][20];
        int count = pending_closes::load(ids, kMaxPending);
        closes_n = 0;
        for (int i = 0; i < count; ++i) {
            closes[closes_n].view = -1;
            text::copy(closes[closes_n].id, sizeof(closes[closes_n].id), ids[i]);
            ++closes_n;
        }
    }

    void persist_closes() {
        char ids[kMaxPending][20];
        for (int i = 0; i < closes_n; ++i) text::copy(ids[i], sizeof(ids[i]), closes[i].id);
        pending_closes::save(ids, closes_n);
    }

    void report_refresh() {
        int count = task_store::view_count();
        int step = refresh_next_ < 0 ? 0 : refresh_next_;
        ui::show_loader("Carregando", "");
        ui::set_loader_progress(count ? step * 100 / count : 0);
    }

    void finish_refresh() {
        refreshing_ = false;
        refresh_next_ = -1;
        last_warm_ = millis();
        ui::hide_loader();
    }

    // A list fetched before a close reached Todoist would bring the task back.
    void store_filtered(int view, const TaskItem *tasks, int count, const char *query = nullptr) {
        static TaskItem kept[kMaxTasks];
        int used = 0;
        for (int i = 0; i < count && used < kMaxTasks; ++i) {
            if (!recently_closed(tasks[i].id)) kept[used++] = tasks[i];
        }
        if (view >= 0) task_store::store(view, kept, used);
        else if (query && query[0]) task_store::remember(query, kept, used);
    }

    void remember_closed(const char *id) {
        Closed &slot = closed_[closed_next_++ % kMaxCloses];
        text::copy(slot.id, sizeof(slot.id), id);
        slot.at = millis();
    }

    void forget_closed(const char *id) {
        for (Closed &slot : closed_) {
            if (!strcmp(slot.id, id)) slot.id[0] = '\0';
        }
    }

    bool recently_closed(const char *id) {
        for (const Closed &slot : closed_) {
            if (slot.id[0] && !strcmp(slot.id, id) && millis() - slot.at < kForgetClosedMs) return true;
        }
        for (int i = 0; i < closes_n; ++i) {
            if (!strcmp(closes[i].id, id)) return true;
        }
        return false;
    }

    hal::Board *board_ = nullptr;
    DeviceSettings device_;
    dashboard::Layout dash_{};  // cached home layout — hidden tiles skip USB/HTTP work
    uint32_t theme_check_at_ = 0;
    uint32_t last_check_ = 0;
    Phase phase_ = Phase::Unconfigured;
    uint32_t epoch_ = 1;
    uint32_t retry_at_ = 0;
    uint32_t last_warm_ = 0;
    uint32_t last_weather_warm_ = 0;
    uint32_t weather_gen_ = 0;
    char pending_weather_city_[48] = {};
    bool reconfigure_ = false;
    bool arm_setup_ = false;
    bool need_token_ = false;
    uint32_t setup_commit_at_ = 0;
    bool refreshing_ = false;
    int refresh_next_ = -1;
    int wanted_ = -1;
    nav::State nav_{};
    nav::Screen beneath_ = nav::Screen::Home;
    nav::Screen under_rest_ = nav::Screen::Home;
    char under_rest_widget_[48] = {};
    screen_rest::State rest_{};
    aitokens::View aitokens_{};
    calendar::View calendar_{};
    calendar::Catalog calendar_catalog_{};
    uint32_t calendar_ask_at_ = 0;
    media::View media_{};
    focus::View focus_{};
    agent::View agent_{};
    bool close_usb_wait_ = false;
    bool todoist_token_seeded_ = false;
    bool todoist_ui_mounted_ = false;
    bool todoist_awaiting_ = false;
    uint32_t todoist_ask_at_ = 0;
    // First ask still unanswered; zero once a bundle lands.
    uint32_t todoist_quiet_since_ = 0;
    todoist::Gate gate_{};
    uint32_t inflight_gen_ = 1;
    Close closes[kMaxPending];
    int closes_n = 0;
    Closed closed_[kMaxCloses] = {};
    uint32_t closed_next_ = 0;
    ViewItem catalog_[kMaxViews] = {};
    int catalog_n_ = 0;
};

Controller controller;
}  // namespace

void app::begin(hal::Board &board) { controller.begin(board); }

void app::loop() { controller.loop(); }
