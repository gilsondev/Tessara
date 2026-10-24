#pragma once

#include <stdint.h>

enum { kMaxTasks = 60, kMaxViews = 8 };

struct TaskItem {
    char id[20];
    char content[180];
    char meta[72];
    uint8_t priority;
};

struct ViewItem {
    char name[28];
    char query[220];
};
