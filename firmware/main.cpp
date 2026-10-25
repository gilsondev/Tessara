#include "app/controller.h"
#include "hal/board.h"

#include <Arduino.h>

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
    Serial.begin(115200);
    const esp_reset_reason_t rr = esp_reset_reason();
    const char *why = "other";
    switch (rr) {
        case ESP_RST_POWERON: why = "poweron"; break;
        case ESP_RST_SW: why = "software"; break;
        case ESP_RST_PANIC: why = "panic"; break;
        case ESP_RST_INT_WDT: why = "int_wdt"; break;
        case ESP_RST_TASK_WDT: why = "task_wdt"; break;
        case ESP_RST_WDT: why = "wdt"; break;
        case ESP_RST_BROWNOUT: why = "brownout"; break;
        case ESP_RST_USB: why = "usb"; break;
        default: break;
    }
    Serial.printf("tessara reset=%d (%s) board=%s\n", static_cast<int>(rr), why, hal::board().descriptor.id);
    app::begin(hal::board());
}

void loop() {
    app::loop();
    delay(1);
}
