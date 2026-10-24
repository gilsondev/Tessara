#include "app/controller.h"
#include "hal/board.h"

#include <Arduino.h>

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
    Serial.begin(115200);
    Serial.printf("tessera reset=%d board=%s\n", static_cast<int>(esp_reset_reason()), hal::board().descriptor.id);
    app::begin(hal::board());
}

void loop() {
    app::loop();
    delay(1);
}
