#include "platform/log.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdarg>

namespace {
SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();
    return handle;
}
}  // namespace

void logger::lock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }

void logger::unlock() { xSemaphoreGiveRecursive(mutex()); }

void logger::printf(const char *format, ...) {
    char line[200];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    lock();
    Serial.print(line);
    unlock();
}
