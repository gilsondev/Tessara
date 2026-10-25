#include "platform/console.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

namespace {
char line[2048];  // calendar catalog chunks need headroom
size_t used = 0;

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
        // Panel→host queries end with '?'; agent pushes have no '?'.
        if (strchr(text, '?')) return;
        handler.on_calendar(text);
    } else if (!strncmp(text, "MEDIA", 5)) {
        if (text[5] == '?') return;
        handler.on_media(text);
    } else if (!strcmp(text, "REST") || !strncmp(text, "REST ", 5)) {
        handler.on_rest(!strcmp(text, "REST") ? "" : text + 5);
    }
}
}  // namespace

void console::poll(ConsoleHandler &handler) {
    while (Serial.available()) {
        char value = static_cast<char>(Serial.read());
        if (value == '\r') continue;
        if (value == '\n') {
            line[used] = '\0';
            if (used) dispatch(line, handler);
            used = 0;
            continue;
        }
        if (used + 1 < sizeof(line)) line[used++] = value;
    }
}
