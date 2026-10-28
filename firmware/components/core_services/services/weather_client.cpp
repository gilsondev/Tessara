#include "services/weather_client.h"

#include "platform/log.h"
#include "services/text.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

namespace {
constexpr size_t kBodyCap = 48 * 1024;
constexpr uint32_t kReadTimeoutMs = 12000;

char *body_buffer() {
    static char *buffer = nullptr;
    if (!buffer) buffer = static_cast<char *>(heap_caps_malloc(kBodyCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return buffer;
}

int read_wait(WiFiClient &client) {
    uint32_t start = millis();
    while (!client.available()) {
        if (!client.connected() || millis() - start > kReadTimeoutMs) return -1;
        delay(2);
    }
    return client.read();
}

bool read_header_line(WiFiClient &client, char *dest, size_t cap) {
    size_t used = 0;
    for (;;) {
        int value = read_wait(client);
        if (value < 0) return false;
        if (value == '\n') {
            dest[used] = '\0';
            return true;
        }
        if (value != '\r' && used + 1 < cap) dest[used++] = static_cast<char>(value);
    }
}

bool read_body(WiFiClient &client, char *dest, size_t cap, int content_length, bool chunked, size_t *got) {
    size_t used = 0;
    if (chunked) {
        while (used + 1 < cap) {
            char size_line[16];
            if (!read_header_line(client, size_line, sizeof(size_line))) break;
            unsigned long chunk = strtoul(size_line, nullptr, 16);
            if (chunk == 0) break;
            for (unsigned long i = 0; i < chunk; ++i) {
                int value = read_wait(client);
                if (value < 0) return false;
                if (used + 1 < cap) dest[used++] = static_cast<char>(value);
            }
            read_header_line(client, size_line, sizeof(size_line));
        }
    } else if (content_length > 0) {
        size_t want = static_cast<size_t>(content_length);
        if (want >= cap) want = cap - 1;
        while (used < want) {
            int value = read_wait(client);
            if (value < 0) break;
            dest[used++] = static_cast<char>(value);
        }
    } else {
        uint32_t idle = millis();
        while (used + 1 < cap && millis() - idle < 800) {
            if (client.available()) {
                dest[used++] = static_cast<char>(client.read());
                idle = millis();
            } else if (!client.connected()) break;
            else delay(2);
        }
    }
    dest[used] = '\0';
    *got = used;
    return used > 0 || content_length == 0;
}

void append_hex(char *&out, char *end, uint8_t byte) {
    static const char *hex = "0123456789ABCDEF";
    if (out + 3 >= end) return;
    *out++ = '%';
    *out++ = hex[byte >> 4];
    *out++ = hex[byte & 0x0F];
}

void append_encoded(char *&out, char *end, const char *text) {
    if (!text) return;
    for (const uint8_t *p = reinterpret_cast<const uint8_t *>(text); *p; ++p) {
        uint8_t c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            if (out < end) *out++ = static_cast<char>(c);
        } else if (c == ' ') {
            if (out < end) *out++ = '+';
        } else {
            append_hex(out, end, c);
        }
    }
}

// Open-Meteo serves the same JSON on plain HTTP. Avoid WiFiClientSecure here: on this board
// TLS to api.open-meteo.com fails to handshake (connect returns false) even with setInsecure,
// while Todoist HTTPS with the CA bundle still works. HTTP keeps forecast off the TLS path.
bool http_get(const char *host, const char *path, int *status, char *body, size_t *got) {
    *status = 0;
    *got = 0;
    if (!body) return false;
    body[0] = '\0';
    WiFiClient client;
    client.setTimeout(kReadTimeoutMs);
    if (!client.connect(host, 80)) {
        logger::printf("clima connect falhou %s\n", host);
        return false;
    }
    String head = String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
                  "\r\nUser-Agent: tessera-panel\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    client.print(head);
    char header[160];
    int content_length = -1;
    bool chunked = false;
    for (;;) {
        if (!read_header_line(client, header, sizeof(header))) {
            client.stop();
            return false;
        }
        if (header[0] == '\0') break;
        if (!strncmp(header, "HTTP/", 5)) {
            const char *space = strchr(header, ' ');
            if (space) *status = atoi(space + 1);
        } else if (!strncasecmp(header, "Content-Length:", 15)) {
            content_length = atoi(header + 15);
        } else if (!strncasecmp(header, "Transfer-Encoding:", 18) && strstr(header, "chunked")) {
            chunked = true;
        }
    }
    bool ok = read_body(client, body, kBodyCap, content_length, chunked, got);
    client.stop();
    logger::printf("clima %.56s %d (%u bytes)\n", path, *status, static_cast<unsigned>(*got));
    return ok && *status >= 200 && *status < 300;
}
}  // namespace

bool weather_client::http_get_url(const char *url, int *status, const char **body_out, size_t *got) {
    if (status) *status = 0;
    if (got) *got = 0;
    if (body_out) *body_out = nullptr;
    if (!url || !status || !body_out || !got) return false;
    const char *rest = url;
    if (!strncmp(url, "http://", 7)) rest = url + 7;
    else if (!strncmp(url, "https://", 8)) return false;  // plain HTTP only (Open-Meteo path)
    const char *slash = strchr(rest, '/');
    if (!slash) return false;
    char host[96];
    size_t host_len = static_cast<size_t>(slash - rest);
    if (host_len == 0 || host_len >= sizeof(host)) return false;
    memcpy(host, rest, host_len);
    host[host_len] = '\0';
    char *body = body_buffer();
    if (!body) return false;
    if (!http_get(host, slash, status, body, got)) return false;
    *body_out = body;
    return true;
}

bool weather_client::geocode(const char *city, weather::Place &out, char *error, size_t error_cap) {
    if (!city || !city[0]) {
        text::copy(error, error_cap, "falta a cidade");
        return false;
    }
    char encoded[160];
    char *write = encoded;
    char *end = encoded + sizeof(encoded);
    append_encoded(write, end, city);
    *write = '\0';
    char path[320];
    snprintf(path, sizeof(path), "/v1/search?name=%s&count=1&language=pt&format=json", encoded);
    char *body = body_buffer();
    if (!body) {
        text::copy(error, error_cap, "sem memória");
        return false;
    }
    int status = 0;
    size_t got = 0;
    if (!http_get("geocoding-api.open-meteo.com", path, &status, body, &got)) {
        logger::printf("clima geocode http=%d\n", status);
        text::copy(error, error_cap, status == 404 ? "cidade não encontrada" : "rede falhou");
        return false;
    }
    if (!weather::parse_place(body, out)) {
        text::copy(error, error_cap, "cidade não encontrada");
        return false;
    }
    return true;
}

bool weather_client::reverse(float latitude, float longitude, weather::Place &out, char *error, size_t error_cap) {
    char path[160];
    snprintf(path, sizeof(path), "/v1/reverse?latitude=%.4f&longitude=%.4f&language=pt&format=json", latitude,
             longitude);
    char *body = body_buffer();
    if (!body) {
        text::copy(error, error_cap, "sem memória");
        return false;
    }
    int status = 0;
    size_t got = 0;
    if (!http_get("geocoding-api.open-meteo.com", path, &status, body, &got)) {
        text::copy(error, error_cap, "rede falhou");
        return false;
    }
    if (!weather::parse_place(body, out)) {
        weather::Place fallback = {};
        text::copy(fallback.name, sizeof(fallback.name), "Minha localização");
        fallback.latitude = latitude;
        fallback.longitude = longitude;
        out = fallback;
        return true;
    }
    out.latitude = latitude;
    out.longitude = longitude;
    return true;
}

bool weather_client::forecast(float latitude, float longitude, weather::Forecast &out, char *error, size_t error_cap) {
    char path[360];
    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&daily=weather_code,"
             "temperature_2m_max,temperature_2m_min&forecast_days=7&timezone=auto",
             latitude, longitude);
    char *body = body_buffer();
    if (!body) {
        text::copy(error, error_cap, "sem memória");
        return false;
    }
    int status = 0;
    size_t got = 0;
    if (!http_get("api.open-meteo.com", path, &status, body, &got)) {
        logger::printf("clima forecast http=%d\n", status);
        text::copy(error, error_cap, "rede falhou");
        return false;
    }
    if (!weather::parse_forecast(body, out)) {
        logger::printf("clima forecast parse falhou (%u bytes)\n", static_cast<unsigned>(got));
        text::copy(error, error_cap, "resposta inválida");
        return false;
    }
    return true;
}
