#include "clock_summary.h"

#include <cstdio>
#include <cstring>

namespace {

const char *greeting_for(int hour) {
    if (hour < 5 || hour >= 18) return "Boa noite";
    if (hour < 12) return "Bom dia";
    return "Boa tarde";
}

const char *weekday_short(int wday) {
    static const char *names[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sáb"};
    if (wday < 0 || wday > 6) return "—";
    return names[wday];
}

const char *weekday_long(int wday) {
    static const char *names[] = {"Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};
    if (wday < 0 || wday > 6) return "—";
    return names[wday];
}

const char *month_short(int month) {
    static const char *names[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                  "jul", "ago", "set", "out", "nov", "dez"};
    if (month < 0 || month > 11) return "—";
    return names[month];
}

const char *month_long(int month) {
    static const char *names[] = {"janeiro", "fevereiro", "março", "abril", "maio", "junho",
                                  "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"};
    if (month < 0 || month > 11) return "—";
    return names[month];
}

void capitalize_first(char *text) {
    if (text && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
}

void format_date(char *dest, size_t cap, clock_face::DateFormat fmt, int wday, int day, int month,
                 int year) {
    switch (fmt) {
        case clock_face::DateFormat::Dmy:
            std::snprintf(dest, cap, "%02d/%02d/%04d", day, month + 1, year);
            break;
        case clock_face::DateFormat::Ymd:
            std::snprintf(dest, cap, "%04d-%02d-%02d", year, month + 1, day);
            break;
        case clock_face::DateFormat::Mdy:
            std::snprintf(dest, cap, "%02d/%02d/%04d", month + 1, day, year);
            break;
        case clock_face::DateFormat::LongPt:
            std::snprintf(dest, cap, "%s, %d de %s", weekday_long(wday), day, month_long(month));
            break;
        case clock_face::DateFormat::ShortPt:
        default:
            std::snprintf(dest, cap, "%s, %d de %s", weekday_short(wday), day, month_short(month));
            capitalize_first(dest);
            break;
    }
}

}  // namespace

clock_face::TimeFormat clock_face::parse_time_format(const char *value) {
    if (value && (std::strcmp(value, "hms") == 0 || std::strcmp(value, "HH:MM:SS") == 0 ||
                  std::strcmp(value, "1") == 0)) {
        return TimeFormat::Hms;
    }
    return TimeFormat::Hm;
}

clock_face::DateFormat clock_face::parse_date_format(const char *value) {
    if (!value || !value[0]) return DateFormat::ShortPt;
    if (std::strcmp(value, "dmy") == 0 || std::strcmp(value, "DD/MM/YYYY") == 0) return DateFormat::Dmy;
    if (std::strcmp(value, "ymd") == 0 || std::strcmp(value, "YYYY-MM-DD") == 0) return DateFormat::Ymd;
    if (std::strcmp(value, "mdy") == 0 || std::strcmp(value, "MM/DD/YYYY") == 0) return DateFormat::Mdy;
    if (std::strcmp(value, "long_pt") == 0 || std::strcmp(value, "long") == 0) return DateFormat::LongPt;
    return DateFormat::ShortPt;
}

const char *clock_face::time_format_id(TimeFormat fmt) {
    return fmt == TimeFormat::Hms ? "hms" : "hm";
}

const char *clock_face::date_format_id(DateFormat fmt) {
    switch (fmt) {
        case DateFormat::Dmy:
            return "dmy";
        case DateFormat::Ymd:
            return "ymd";
        case DateFormat::Mdy:
            return "mdy";
        case DateFormat::LongPt:
            return "long_pt";
        case DateFormat::ShortPt:
        default:
            return "short_pt";
    }
}

clock_face::Summary clock_face::from_parts(int hour, int minute, int wday, int day, int month, int year,
                                           bool synced, Options opts) {
    Summary out{};
    out.synced = synced;
    if (!synced || wday < 0 || wday > 6 || month < 0 || month > 11) {
        if (opts.time == TimeFormat::Hms) std::snprintf(out.time, sizeof(out.time), "--:--:--");
        else std::snprintf(out.time, sizeof(out.time), "--:--");
        out.greeting[0] = '\0';
        out.date_short[0] = '\0';
        out.date_long[0] = '\0';
        return out;
    }
    if (opts.time == TimeFormat::Hms) {
        int sec = opts.second;
        if (sec < 0) sec = 0;
        if (sec > 59) sec = 59;
        std::snprintf(out.time, sizeof(out.time), "%02d:%02d:%02d", hour, minute, sec);
    } else {
        std::snprintf(out.time, sizeof(out.time), "%02d:%02d", hour, minute);
    }
    std::snprintf(out.greeting, sizeof(out.greeting), "%s", greeting_for(hour));
    const int y = year >= 2024 ? year : 2024;
    format_date(out.date_short, sizeof(out.date_short), opts.date, wday, day, month, y);
    // Detail line prefers long Portuguese unless a numeric format was chosen.
    if (opts.date == DateFormat::ShortPt || opts.date == DateFormat::LongPt) {
        format_date(out.date_long, sizeof(out.date_long), DateFormat::LongPt, wday, day, month, y);
    } else {
        format_date(out.date_long, sizeof(out.date_long), opts.date, wday, day, month, y);
    }
    return out;
}

clock_face::Summary clock_face::from_snapshot(const tessara::TimeSnapshot &snap, Options opts) {
    opts.second = snap.second;
    return from_parts(snap.hour, snap.minute, snap.wday, snap.day, snap.month, snap.year, snap.synced,
                      opts);
}
