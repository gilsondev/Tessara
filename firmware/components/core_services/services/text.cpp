#include "services/text.h"

#include <cstdio>
#include <cstring>

namespace {
void normalize_due(char *dest, size_t cap, const char *src) {
    size_t used = 0;
    bool space = false;
    for (size_t i = 0; src && src[i] && used + 1 < cap; ++i) {
        char c = src[i];
        if (c == '!') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == ' ') {
            if (used == 0 || space) continue;
            space = true;
        } else space = false;
        dest[used++] = c;
    }
    while (used > 0 && dest[used - 1] == ' ') --used;
    dest[used] = '\0';
}

const char *weekday_pt(const char *word) {
    if (!strcmp(word, "monday") || !strcmp(word, "mon")) return "segunda";
    if (!strcmp(word, "tuesday") || !strcmp(word, "tue")) return "terça";
    if (!strcmp(word, "wednesday") || !strcmp(word, "wed")) return "quarta";
    if (!strcmp(word, "thursday") || !strcmp(word, "thu")) return "quinta";
    if (!strcmp(word, "friday") || !strcmp(word, "fri")) return "sexta";
    if (!strcmp(word, "saturday") || !strcmp(word, "sat")) return "sábado";
    if (!strcmp(word, "sunday") || !strcmp(word, "sun")) return "domingo";
    return nullptr;
}

const char *unit_pt(const char *word, bool plural) {
    if (!strcmp(word, "day") || !strcmp(word, "days")) return plural ? "dias" : "dia";
    if (!strcmp(word, "week") || !strcmp(word, "weeks")) return plural ? "semanas" : "semana";
    if (!strcmp(word, "month") || !strcmp(word, "months")) return plural ? "meses" : "mês";
    if (!strcmp(word, "year") || !strcmp(word, "years")) return plural ? "anos" : "ano";
    if (!strcmp(word, "morning")) return "manhã";
    if (!strcmp(word, "evening") || !strcmp(word, "night")) return "noite";
    if (!strcmp(word, "weekday") || !strcmp(word, "weekdays")) return plural ? "dias de semana" : "dia de semana";
    if (!strcmp(word, "weekend") || !strcmp(word, "weekends")) return plural ? "fins de semana" : "fim de semana";
    return nullptr;
}

bool localize_base(const char *phrase, char *dest, size_t cap) {
    static const char *const kFixed[][2] = {
        {"today", "hoje"},
        {"tomorrow", "amanhã"},
        {"yesterday", "ontem"},
        {"every day", "todo dia"},
        {"daily", "todo dia"},
        {"every morning", "toda manhã"},
        {"every afternoon", "toda tarde"},
        {"every evening", "toda noite"},
        {"every night", "toda noite"},
        {"every weekday", "todo dia útil"},
        {"every workday", "todo dia útil"},
        {"every weekend", "todo fim de semana"},
        {"every week", "toda semana"},
        {"weekly", "toda semana"},
        {"every month", "todo mês"},
        {"monthly", "todo mês"},
        {"every year", "todo ano"},
        {"yearly", "todo ano"},
    };
    for (const auto &pair : kFixed) {
        if (!strcmp(phrase, pair[0])) {
            text::copy(dest, cap, pair[1]);
            return true;
        }
    }
    if (!strncmp(phrase, "every other ", 12)) {
        const char *unit = unit_pt(phrase + 12, true);
        if (unit) {
            snprintf(dest, cap, "a cada 2 %s", unit);
            return true;
        }
    }
    int amount = 0;
    char word[24];
    if (sscanf(phrase, "every %d %23s", &amount, word) == 2 && amount > 0) {
        const char *unit = unit_pt(word, amount > 1);
        const char *day = weekday_pt(word);
        if (unit || day) {
            snprintf(dest, cap, "a cada %d %s", amount, unit ? unit : day);
            return true;
        }
    }
    if (!strncmp(phrase, "every ", 6)) {
        const char *day = weekday_pt(phrase + 6);
        if (day) {
            snprintf(dest, cap, "toda %s", day);
            return true;
        }
        const char *unit = unit_pt(phrase + 6, false);
        if (unit) {
            snprintf(dest, cap, "todo %s", unit);
            return true;
        }
    }
    return false;
}

bool format_clock(const char *phrase, char *dest, size_t cap) {
    if (!strcmp(phrase, "noon")) {
        text::copy(dest, cap, "12h");
        return true;
    }
    if (!strcmp(phrase, "midnight")) {
        text::copy(dest, cap, "0h");
        return true;
    }
    int hour = 0;
    int minute = 0;
    int used = 0;
    if (sscanf(phrase, "%d:%d%n", &hour, &minute, &used) != 2) {
        minute = 0;
        if (sscanf(phrase, "%d%n", &hour, &used) != 1) return false;
    }
    const char *rest = phrase + used;
    while (*rest == ' ') ++rest;
    if (!strcmp(rest, "pm") && hour < 12) hour += 12;
    else if (!strcmp(rest, "am") && hour == 12) hour = 0;
    else if (rest[0] && strcmp(rest, "am") && strcmp(rest, "pm")) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    if (minute) snprintf(dest, cap, "%d:%02d", hour, minute);
    else snprintf(dest, cap, "%dh", hour);
    return true;
}

bool font_covers(uint32_t cp) {
    static const uint32_t kRanges[][2] = {
        {0x20, 0x7E},       {0xA0, 0xFF},       {0x2013, 0x205E},   {0x20A0, 0x20BF},   {0x2100, 0x214F},
        {0x2190, 0x21FF},   {0x2300, 0x23FF},   {0x2460, 0x24FF},   {0x25A0, 0x27BF},   {0x2B00, 0x2BFF},
        {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF},
    };
    for (const auto &range : kRanges) {
        if (cp >= range[0] && cp <= range[1]) return true;
    }
    return false;
}

bool invisible_mark(uint32_t cp) {
    return cp == 0xFE0E || cp == 0xFE0F || cp == 0x200D || cp == 0x200B || cp == 0x20E3 ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F);
}

size_t utf8_decode(const char *src, uint32_t *cp) {
    uint8_t lead = static_cast<uint8_t>(src[0]);
    if (lead < 0x80) {
        *cp = lead;
        return 1;
    }
    size_t width = (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 1;
    uint32_t value = width == 2 ? lead & 0x1f : width == 3 ? lead & 0x0f : lead & 0x07;
    for (size_t i = 1; i < width; ++i) {
        uint8_t next = static_cast<uint8_t>(src[i]);
        if ((next & 0xc0) != 0x80) {
            *cp = 0xFFFD;
            return i;
        }
        value = (value << 6) | (next & 0x3f);
    }
    *cp = width == 1 ? 0xFFFD : value;
    return width;
}

const char *const kMonths[] = {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"};
}  // namespace

void text::copy(char *dest, size_t cap, const char *src) {
    if (!cap) return;
    if (!src) src = "";
    size_t used = 0;
    while (src[used] && used + 1 < cap) {
        uint8_t lead = static_cast<uint8_t>(src[used]);
        size_t width = 1;
        if ((lead & 0xe0) == 0xc0) width = 2;
        else if ((lead & 0xf0) == 0xe0) width = 3;
        else if ((lead & 0xf8) == 0xf0) width = 4;
        if (used + width >= cap) break;
        memcpy(dest + used, src + used, width);
        used += width;
    }
    dest[used] = '\0';
}

void text::url_encode(char *dest, size_t cap, const char *src) {
    static const char *hex = "0123456789ABCDEF";
    size_t used = 0;
    for (size_t i = 0; src[i] && used + 1 < cap; ++i) {
        uint8_t c = static_cast<uint8_t>(src[i]);
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                     c == '_' || c == '.' || c == '~';
        if (plain) dest[used++] = static_cast<char>(c);
        else if (used + 3 < cap) {
            dest[used++] = '%';
            dest[used++] = hex[c >> 4];
            dest[used++] = hex[c & 0x0f];
        }
    }
    dest[used] = '\0';
}

const char *text::visible_name(const char *name) {
    const char *cursor = name ? name : "";
    while (*cursor) {
        uint8_t c = static_cast<uint8_t>(*cursor);
        if (c < 0x80 && *cursor != ' ') break;
        if (c < 0x80) {
            ++cursor;
            continue;
        }
        if (c >= 0xf0) cursor += 4;
        else if (c >= 0xe0) cursor += 3;
        else if (c >= 0xc0) cursor += 2;
        else ++cursor;
    }
    while (*cursor == ' ') ++cursor;
    return *cursor ? cursor : name;
}

bool text::localize_due(const char *due_string, char *dest, size_t cap) {
    char phrase[96];
    normalize_due(phrase, sizeof(phrase), due_string);
    char *at = strstr(phrase, " at ");
    const char *clock = nullptr;
    if (at) {
        *at = '\0';
        clock = at + 4;
    }
    char base[64];
    if (!localize_base(phrase, base, sizeof(base))) return false;
    if (!clock || !clock[0]) {
        copy(dest, cap, base);
        return true;
    }
    char time[16];
    if (!format_clock(clock, time, sizeof(time))) return false;
    snprintf(dest, cap, "%s às %s", base, time);
    return true;
}

bool text::format_date(const char *iso, char *dest, size_t cap) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (!iso || sscanf(iso, "%d-%d-%d", &year, &month, &day) != 3 || month < 1 || month > 12) return false;
    int hour = -1;
    int minute = 0;
    const char *t = strchr(iso, 'T');
    if (t && sscanf(t + 1, "%d:%d", &hour, &minute) != 2) hour = -1;
    if (hour >= 0) snprintf(dest, cap, "%d %s, %d:%02d", day, kMonths[month - 1], hour, minute);
    else snprintf(dest, cap, "%d %s", day, kMonths[month - 1]);
    return true;
}

void text::clean_title(char *dest, size_t cap, const char *src, void (*on_missing)(uint32_t)) {
    if (!src || !src[0]) src = "(sem título)";
    size_t used = 0;
    bool space = true;
    const char *p = src;
    while (*p && used + 1 < cap) {
        if (p[0] == '*' && p[1] == '*') {
            p += 2;
            continue;
        }
        if (p[0] == ']' && p[1] == '(') {
            const char *close = strchr(p + 2, ')');
            if (close) {
                p = close + 1;
                continue;
            }
        }
        if (p[0] == '[' && strstr(p, "](")) {
            ++p;
            continue;
        }
        uint32_t cp = 0;
        size_t width = utf8_decode(p, &cp);
        if (invisible_mark(cp)) {
            p += width;
            continue;
        }
        if (!font_covers(cp)) {
            if (on_missing) on_missing(cp);
            p += width;
            continue;
        }
        if (cp == ' ') {
            if (space) {
                ++p;
                continue;
            }
            space = true;
        } else space = false;
        if (used + width >= cap) break;
        memcpy(dest + used, p, width);
        used += width;
        p += width;
    }
    while (used > 0 && dest[used - 1] == ' ') --used;
    dest[used] = '\0';
}
