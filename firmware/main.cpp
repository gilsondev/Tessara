#include "app/controller.h"
#include "hal/board.h"
#include "platform/console.h"

#include <Arduino.h>
#include "esp_heap_caps.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
    // A cover line is about 1 KB. The 256-byte default splits it and the USB
    // task blocks. 2 KB holds one line. Todoist TLS needs a 28 KB contiguous
    // internal block, so this queue cannot grow further without panicking.
    Serial.setRxBufferSize(2048);
    Serial.begin(115200);
    // A full TX buffer must not stall the UI task while the Mac is not reading.
    Serial.setTxTimeoutMs(0);
    // The agent's open toggles DTR/RTS. USB CDC treats that dance as the
    // bootloader sequence and reboots the panel, which looks like a freeze.
    // FLASH turns it back on so the 1200-baud reclaim still works.
    Serial.enableReboot(false);
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
    // The Mac opens the port later. Sending BOOT here is dropped because TX does not block.
    console::set_boot_reason(why);
    Serial.printf("tessera reset=%d (%s) board=%s largest=%u\n", static_cast<int>(rr), why,
                  hal::board().descriptor.id,
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    app::begin(hal::board());
}

void loop() {
    app::loop();
    delay(1);
}
