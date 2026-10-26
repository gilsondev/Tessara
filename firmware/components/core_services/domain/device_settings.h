#pragma once

#include <stdint.h>

// User-facing device preferences, edited on the panel or from the browser.
enum class Appearance : uint8_t { Light = 0, Dark = 1, Auto = 2 };

struct DeviceSettings {
    bool portrait = true;
    bool sound = true;
    Appearance appearance = Appearance::Light;
    // Last prefers-color-scheme reported by the phone/computer portal (used when Auto).
    bool os_prefers_dark = false;
    bool os_theme_known = false;
    uint8_t brightness = 100;  // percent
    uint8_t sync_minutes = 15;  // how often an untouched list is refreshed in the background
    uint8_t rest_minutes = 5;   // idle before screen rest; 0 = never
    uint8_t clock_minutes = 1;  // flip-clock on screen before fade; 0 = until touch
    // Which AI Token sources appear on home / detail / settings (default all on).
    bool show_aitokens_codex = true;
    bool show_aitokens_claude = true;
    bool show_aitokens_cursor = true;
};

// Qualidade do link Wi-Fi a partir do RSSI (dBm) atual.
enum class WifiStrength : uint8_t { None = 0, Weak = 1, Medium = 2, Strong = 3 };

inline WifiStrength wifi_strength(bool online, int rssi) {
    if (!online) return WifiStrength::None;
    // ESP32 com antena PCB costuma ler ~10 dB pior que um celular na mesma distância.
    // Faixas alinhadas a link útil (MetaGeek/AOSP): ≥−65 pleno, ≥−75 ok, abaixo fraco.
    if (rssi >= -65) return WifiStrength::Strong;
    if (rssi >= -75) return WifiStrength::Medium;
    return WifiStrength::Weak;
}

inline const char *wifi_strength_label(WifiStrength level) {
    switch (level) {
        case WifiStrength::Strong:
            return "sinal forte";
        case WifiStrength::Medium:
            return "sinal médio";
        case WifiStrength::Weak:
            return "sinal fraco";
        default:
            return "sem sinal";
    }
}

// What the settings screens show about the connection.
struct DeviceInfo {
    char host[40];  // "tessera.local"
    char ip[16];
    char ssid[64];
    bool online;
    int8_t rssi;  // dBm quando online; 0 se offline
};

inline uint8_t clamp_brightness(int value) { return value < 10 ? 10 : value > 100 ? 100 : static_cast<uint8_t>(value); }

inline uint8_t clamp_sync(int value) { return value < 1 ? 1 : value > 120 ? 120 : static_cast<uint8_t>(value); }

// Allowed: 0 (never), 2, 5, 10, 20, 30. Anything else snaps to nearest option (default 5).
inline uint8_t clamp_rest_minutes(int value) {
    static constexpr int kOpts[] = {0, 2, 5, 10, 20, 30};
    int best = 5;
    int best_gap = 1000;
    for (int opt : kOpts) {
        int gap = value - opt;
        if (gap < 0) gap = -gap;
        if (gap < best_gap) {
            best_gap = gap;
            best = opt;
        }
    }
    return static_cast<uint8_t>(best);
}

// How long the flip clock stays visible: 0 (until touch), 1, 2, 5, 10. Default 1 min.
inline uint8_t clamp_clock_minutes(int value) {
    static constexpr int kOpts[] = {0, 1, 2, 5, 10};
    int best = 1;
    int best_gap = 1000;
    for (int opt : kOpts) {
        int gap = value - opt;
        if (gap < 0) gap = -gap;
        if (gap < best_gap) {
            best_gap = gap;
            best = opt;
        }
    }
    return static_cast<uint8_t>(best);
}

inline Appearance clamp_appearance(int value) {
    if (value == static_cast<int>(Appearance::Dark)) return Appearance::Dark;
    if (value == static_cast<int>(Appearance::Auto)) return Appearance::Auto;
    return Appearance::Light;
}

// Resolves Light / Dark / Auto → effective dark mode for the palette.
// Auto follows macOS (Agente `THEME` push) or the portal `os_dark`; until known, evening (19–7).
inline bool effective_dark(const DeviceSettings &settings, int local_hour = -1) {
    if (settings.appearance == Appearance::Dark) return true;
    if (settings.appearance == Appearance::Light) return false;
    if (settings.os_theme_known) return settings.os_prefers_dark;
    if (local_hour < 0) return false;
    return local_hour < 7 || local_hour >= 19;
}
