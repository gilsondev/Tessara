#pragma once

#include "tessara/services.h"

// Pure face copy for home hero + clock detail (option C: greeting+date+time live here).
namespace clock_face {

struct Summary {
    char greeting[24];
    char date_short[40];  // hero: "Sáb, 24 de out"
    char date_long[48];   // detail: "Sábado, 24 out"
    char time[8];         // "14:30" or "--:--"
    bool synced;
};

Summary from_parts(int hour, int minute, int wday, int day, int month, bool synced);
Summary from_snapshot(const tessara::TimeSnapshot &snap);

}  // namespace clock_face
