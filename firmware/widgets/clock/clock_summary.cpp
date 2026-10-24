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

void capitalize_first(char *text) {
    if (text && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
}

}  // namespace

clock_face::Summary clock_face::from_parts(int hour, int minute, int wday, int day, int month, bool synced) {
    Summary out{};
    out.synced = synced;
    if (!synced || wday < 0 || wday > 6 || month < 0 || month > 11) {
        std::snprintf(out.time, sizeof(out.time), "--:--");
        out.greeting[0] = '\0';
        out.date_short[0] = '\0';
        out.date_long[0] = '\0';
        return out;
    }
    std::snprintf(out.time, sizeof(out.time), "%02d:%02d", hour, minute);
    std::snprintf(out.greeting, sizeof(out.greeting), "%s", greeting_for(hour));
    std::snprintf(out.date_short, sizeof(out.date_short), "%s, %d de %s", weekday_short(wday), day,
                  month_short(month));
    capitalize_first(out.date_short);
    std::snprintf(out.date_long, sizeof(out.date_long), "%s, %d %s", weekday_long(wday), day,
                  month_short(month));
    return out;
}

clock_face::Summary clock_face::from_snapshot(const tessara::TimeSnapshot &snap) {
    return from_parts(snap.hour, snap.minute, snap.wday, snap.day, snap.month, snap.synced);
}
