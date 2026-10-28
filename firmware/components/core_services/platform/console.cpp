#include "platform/console.h"

#include "platform/boot_notice.h"

#include <Arduino.h>
#include <esp_core_dump.h>

#include <cstdio>
#include <cstring>

namespace {
char line[4096];  // Todoist TASKS lines pack full lists (was 2KB → truncava)
size_t used = 0;
BootNotice boot_notice;

// The host writes one capability per line. When the panel drops the newline, the next
// header (`FOCUS\tv1`, `CALENDAR_CATALOG`, …) sticks to the previous line and shows up
// inside a tile. Split those back apart. A field that merely says "FOCUS" is left alone.
void dispatch(char *text, ConsoleHandler &handler);

char *glued_header(char *text) {
    if (!text || !text[0]) return nullptr;
    static const char *kHeaders[] = {
        "FOCUS\tv1",     "AITOKENS\tv1", "CALENDAR_CATALOG", "CALENDAR_AUTH", "CALENDAR\tv1",
        "TODOIST_TASKS", "TODOIST_END",  "TODOIST_CLOSE",   "TODOIST\tv1",    "MEDIA_COVER",
        "MEDIA\tv1",     "THEME\tv1",    "AGENT\tv1",
    };
    char *found = nullptr;
    for (const char *header : kHeaders) {
        char *at = strstr(text, header);
        if (at == text) at = strstr(text + 1, header);
        if (at && (!found || at < found)) found = at;
    }
    return found;
}

void dispatch_line(char *text, ConsoleHandler &handler) {
    char *glued = glued_header(text);
    if (!glued) {
        dispatch(text, handler);
        return;
    }
    char saved = *glued;
    *glued = '\0';
    if (glued > text && glued[-1] == '\t') glued[-1] = '\0';
    if (text[0]) dispatch(text, handler);
    *glued = saved;
    dispatch_line(glued, handler);
}

void dispatch(char *text, ConsoleHandler &handler) {
    if (!strncmp(text, "WIFI\t", 5)) {
        char *ssid = text + 5;
        char *pass = strchr(ssid, '\t');
        if (!pass) return;
        *pass++ = '\0';
        handler.on_wifi(ssid, pass);
    } else if (!strncmp(text, "TOKEN\t", 6)) {
        handler.on_token(text + 6);
    } else if (!strcmp(text, "RELOAD")) {
        handler.on_reload();
    } else if (!strcmp(text, "SNAP")) {
        handler.on_snapshot();
    } else if (!strcmp(text, "PERF")) {
        handler.on_perf();
    } else if (!strcmp(text, "FLASH")) {
        handler.on_flash();
    } else if (!strcmp(text, "AGENT_PUSH")) {
        handler.on_agent_push(true);
    } else if (!strcmp(text, "AGENT_PUSH_OFF")) {
        handler.on_agent_push(false);
    } else if (!strcmp(text, "SETUP")) {
        handler.on_enter_setup();
    } else if (!strcmp(text, "RESTORE")) {
        handler.on_restore();
    } else if (!strcmp(text, "NOTOKEN")) {
        handler.on_drop_token();
    } else if (!strcmp(text, "SD")) {
        handler.on_sd_status();
    } else if (!strncmp(text, "DRAG ", 5)) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0, ms = 0;
        if (sscanf(text + 5, "%d %d %d %d %d", &x0, &y0, &x1, &y1, &ms) == 5) {
            handler.on_drag(x0, y0, x1, y1, static_cast<uint32_t>(ms));
        }
    } else if (!strncmp(text, "AITOKENS", 8)) {
        // Push from Agent, or ignore AITOKENS? which is panel→host.
        if (text[8] == '?' ) return;
        handler.on_aitokens(text);
    } else if (!strncmp(text, "AGENT", 5)) {
        if (text[5] == '?') return;
        handler.on_agent(text);
    } else if (!strncmp(text, "CALENDAR", 8)) {
        // A query is `CALENDAR?`. A '?' inside an event title is payload.
        if (text[8] == '?') return;
        handler.on_calendar(text);
    } else if (!strncmp(text, "TODOIST", 7)) {
        if (text[7] == '?') return;
        handler.on_todoist(text);
    } else if (!strncmp(text, "MEDIA", 5)) {
        if (text[5] == '?') return;
        handler.on_now_playing(text);
    } else if (!strncmp(text, "FOCUS", 5)) {
        if (text[5] == '?') return;
        handler.on_focus(text);
    } else if (!strncmp(text, "THEME", 5)) {
        if (text[5] == '?') return;
        handler.on_theme(text);
    } else if (!strcmp(text, "REST") || !strncmp(text, "REST ", 5)) {
        handler.on_rest(!strcmp(text, "REST") ? "" : text + 5);
    } else if (!strcmp(text, "REBOOT")) {
        handler.on_reboot();
    }
}
}  // namespace

void console::set_boot_reason(const char *why) { boot_notice.set(why); }

static void send_panic_summary() {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t summary = {};
    if (esp_core_dump_get_summary(&summary) != ESP_OK) return;
    int depth = static_cast<int>(summary.exc_bt_info.depth);
    char line[192];
    if (!format_panic_line(line, sizeof(line), summary.exc_pc, summary.ex_info.exc_cause, summary.exc_bt_info.bt,
                           depth)) {
        return;
    }
    Serial.print(line);
    esp_core_dump_image_erase();
#else
    (void)0;
#endif
}

void console::send_boot_reason_once() {
    char line[96];
    if (!boot_notice.take(line, sizeof(line))) return;
    Serial.print(line);
    if (!std::strcmp(line, "BOOT\tpanic\n")) send_panic_summary();
}

void console::poll(ConsoleHandler &handler) {
    while (Serial.available()) {
        char value = static_cast<char>(Serial.read());
        if (value == '\r') continue;
        if (value == '\n') {
            line[used] = '\0';
            if (used) dispatch_line(line, handler);
            used = 0;
            continue;
        }
        if (used + 1 < sizeof(line)) line[used++] = value;
    }
}
