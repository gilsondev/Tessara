#include "services/todoist_client.h"

#include "platform/log.h"
#include "services/text.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace {
constexpr size_t kBodyCap = 160 * 1024;
constexpr char kHost[] = "api.todoist.com";
constexpr uint32_t kReadTimeoutMs = 12000;
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr int kConnectAttempts = 3;

char token[128] = "";

char *body_buffer() {
    static char *buffer = nullptr;
    if (!buffer) buffer = static_cast<char *>(heap_caps_malloc(kBodyCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return buffer;
}

// One reused TLS client: avoids fragmenting internal DRAM on every request.
WiFiClientSecure &tls() {
    static WiFiClientSecure client;
    static bool ready = false;
    if (!ready) {
        client.setHandshakeTimeout(kConnectTimeoutMs / 1000);
        client.setTimeout(kReadTimeoutMs);
        // Builtin Mozilla bundle (same as setCACertBundle of the embedded CRT).
        client.useBuiltinCACertBundle();
        ready = true;
    }
    return client;
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

bool open_tls() {
    WiFiClientSecure &client = tls();
    if (client.connected()) client.stop();
    for (int attempt = 1; attempt <= kConnectAttempts; ++attempt) {
        size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (client.connect(kHost, 443, kConnectTimeoutMs)) return true;
        char err[96] = "";
        client.lastError(err, sizeof(err));
        logger::printf("todoist TLS falhou try=%d wifi=%d heap_int=%u largest=%u %s\n", attempt,
                       static_cast<int>(WiFi.status()), static_cast<unsigned>(free_int),
                       static_cast<unsigned>(largest), err[0] ? err : "(sem detalhe)");
        client.stop();
        delay(150 * attempt);
    }
    return false;
}

bool request(const char *method, const char *path, const char *payload, int *status, char *body, size_t *got) {
    *status = 0;
    *got = 0;
    if (!body) return false;
    body[0] = '\0';
    if (!token[0]) {
        logger::printf("todoist sem token na NVS\n");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        logger::printf("todoist sem wi-fi\n");
        return false;
    }
    if (!open_tls()) return false;

    WiFiClientSecure &client = tls();
    // Avoid Arduino String (fragments internal heap during TLS lifetime).
    // Path can be >1KB on filter queries — print in pieces, never one fixed buffer.
    size_t payload_len = payload ? strlen(payload) : 0;
    client.printf("%s %s HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\nConnection: close\r\n", method,
                  path, kHost, token);
    if (payload) {
        client.printf("Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n\r\n",
                      static_cast<unsigned>(payload_len));
        if (payload_len) client.write(reinterpret_cast<const uint8_t *>(payload), payload_len);
    } else {
        client.print("\r\n");
    }

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
        } else if (!strncasecmp(header, "Content-Length:", 15)) content_length = atoi(header + 15);
        else if (!strncasecmp(header, "Transfer-Encoding:", 18) && strstr(header, "chunked")) chunked = true;
    }
    bool ok = read_body(client, body, kBodyCap, content_length, chunked, got);
    client.stop();
    logger::printf("todoist %.48s %d (%u bytes)\n", path, *status, static_cast<unsigned>(*got));
    if (*status == 401 || *status == 403) {
        logger::printf("todoist auth recusada (%d) — token inválido ou revogado\n", *status);
    }
    return ok && *status >= 200 && *status < 300;
}

const char *json_text(JsonVariant value) { return value.is<const char *>() ? value.as<const char *>() : nullptr; }

void report_missing(uint32_t cp) {
    static uint32_t seen[24];
    static int seen_n = 0;
    for (int i = 0; i < seen_n; ++i) {
        if (seen[i] == cp) return;
    }
    if (seen_n < 24) seen[seen_n++] = cp;
    logger::printf("GLYPH U+%04X\n", static_cast<unsigned>(cp));
}

void fill_meta(JsonObject task, char *dest, size_t cap) {
    dest[0] = '\0';
    if (task["due"].is<JsonObject>()) {
        JsonObject due = task["due"];
        const char *phrase = json_text(due["string"]);
        if (phrase && phrase[0] && text::localize_due(phrase, dest, cap)) return;
        const char *when = json_text(due["datetime"]);
        if (!when || !when[0]) when = json_text(due["date"]);
        char date[40];
        if (text::format_date(when, date, sizeof(date))) {
            if (due["is_recurring"] | false) snprintf(dest, cap, "repete · %s", date);
            else text::copy(dest, cap, date);
        }
        if (phrase && phrase[0]) logger::printf("DUE %s\n", phrase);
        return;
    }
    if (task["deadline"].is<JsonObject>()) {
        char date[40];
        if (text::format_date(json_text(task["deadline"]["date"]), date, sizeof(date))) {
            snprintf(dest, cap, "prazo %s", date);
        }
    }
}

const ViewItem kFallback[] = {
    {"Hoje", "today | overdue"},
    {"Casa", "@Casa & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho"},
    {"Computador", "@Computador & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho"},
    {"Trabalho", "@Trabalho & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho"},
    {"Rua", "@Rua & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho"},
    {"Celular", "@Celular & !@Delegado & !#🗓️ Tickler & !#🗓️ Tickler Trabalho"},
    {"Aguardando", "@Delegado"},
};

int use_fallback(ViewItem *views, int cap) {
    int count = static_cast<int>(sizeof(kFallback) / sizeof(kFallback[0]));
    if (count > cap) count = cap;
    for (int i = 0; i < count; ++i) views[i] = kFallback[i];
    return count;
}
}  // namespace

void todoist::set_token(const char *value) { text::copy(token, sizeof(token), value); }

int todoist::load_views(ViewItem *views, int cap) {
    char *body = body_buffer();
    int status = 0;
    size_t got = 0;
    if (!request("POST", "/api/v1/sync", "sync_token=*&resource_types=%5B%22filters%22%5D", &status, body, &got)) {
        return use_fallback(views, cap);
    }
    JsonDocument filter;
    filter["filters"][0]["name"] = true;
    filter["filters"][0]["query"] = true;
    filter["filters"][0]["is_favorite"] = true;
    filter["filters"][0]["is_deleted"] = true;
    filter["filters"][0]["item_order"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return use_fallback(views, cap);

    struct Row {
        int order;
        int index;
    } rows[16];
    int rows_n = 0;
    JsonArray filters = doc["filters"].as<JsonArray>();
    int raw = 0;
    for (JsonObject item : filters) {
        if (!(item["is_deleted"] | false) && (item["is_favorite"] | false) && rows_n < 16) {
            rows[rows_n++] = Row{item["item_order"] | 0, raw};
        }
        raw++;
    }
    for (int i = 1; i < rows_n; ++i) {
        Row key = rows[i];
        int j = i;
        for (; j > 0 && rows[j - 1].order > key.order; --j) rows[j] = rows[j - 1];
        rows[j] = key;
    }
    text::copy(views[0].name, sizeof(views[0].name), "Hoje");
    text::copy(views[0].query, sizeof(views[0].query), "today | overdue");
    int used = 1;
    for (int i = 0; i < rows_n && used < cap; ++i) {
        JsonObject item = filters[rows[i].index];
        text::copy(views[used].name, sizeof(views[used].name), text::visible_name(item["name"] | "Filtro"));
        text::copy(views[used].query, sizeof(views[used].query), item["query"] | "");
        if (views[used].query[0]) used++;
    }
    return used == 1 ? use_fallback(views, cap) : used;
}

bool todoist::load_tasks(const char *query, TaskItem *tasks, int *count, char *error, size_t error_len) {
    *count = 0;
    char *body = body_buffer();
    if (!body) {
        text::copy(error, error_len, "sem memória");
        return false;
    }
    char cursor[160] = "";
    do {
        char encoded[700];
        char path[1100];
        text::url_encode(encoded, sizeof(encoded), query);
        snprintf(path, sizeof(path), "/api/v1/tasks/filter?limit=40&query=%s", encoded);
        if (cursor[0]) {
            char encoded_cursor[240];
            text::url_encode(encoded_cursor, sizeof(encoded_cursor), cursor);
            strlcat(path, "&cursor=", sizeof(path));
            strlcat(path, encoded_cursor, sizeof(path));
        }
        int status = 0;
        size_t got = 0;
        if (!request("GET", path, nullptr, &status, body, &got)) {
            if (status == 401 || status == 403) text::copy(error, error_len, "token recusado");
            else if (status) snprintf(error, error_len, "Todoist respondeu %d", status);
            else text::copy(error, error_len, "sem conexão com o Todoist");
            return false;
        }
        JsonDocument filter;
        filter["results"][0]["id"] = true;
        filter["results"][0]["content"] = true;
        filter["results"][0]["priority"] = true;
        filter["results"][0]["due"] = true;
        filter["results"][0]["deadline"] = true;
        filter["next_cursor"] = true;
        JsonDocument doc;
        if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
            text::copy(error, error_len, "resposta inválida");
            return false;
        }
        cursor[0] = '\0';
        const char *next = doc["next_cursor"] | nullptr;
        if (next && next[0]) text::copy(cursor, sizeof(cursor), next);
        for (JsonObject item : doc["results"].as<JsonArray>()) {
            if (*count >= kMaxTasks) {
                cursor[0] = '\0';
                break;
            }
            TaskItem &task = tasks[*count];
            text::copy(task.id, sizeof(task.id), item["id"] | "");
            text::clean_title(task.content, sizeof(task.content), item["content"] | "", report_missing);
            fill_meta(item, task.meta, sizeof(task.meta));
            task.priority = item["priority"] | 1;
            if (task.id[0]) (*count)++;
        }
    } while (cursor[0] && *count < kMaxTasks);
    error[0] = '\0';
    return true;
}

static bool already_closed(int status, const char *body) {
    if (status == 404) return true;
    if (status != 400 || !body) return false;
    return strstr(body, "already") || strstr(body, "ALREADY") || strstr(body, "NOT_FOUND") || strstr(body, "not found");
}

bool todoist::close_task(const char *id, char *error, size_t error_len, int *status_out) {
    char path[80];
    snprintf(path, sizeof(path), "/api/v1/tasks/%s/close", id);
    int status = 0;
    size_t got = 0;
    char *body = body_buffer();
    bool http_ok = request("POST", path, "", &status, body, &got);
    if (status_out) *status_out = status;
    if (http_ok || status == 204 || already_closed(status, body)) {
        if (error && error_len) error[0] = '\0';
        return true;
    }
    if (error && error_len) {
        if (status) snprintf(error, error_len, "não concluiu (%d)", status);
        else text::copy(error, error_len, "não concluiu");
    }
    return false;
}
