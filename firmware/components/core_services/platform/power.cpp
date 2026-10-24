#include "platform/power.h"

#include "hal/board.h"

bool power::battery_present() {
    hal::PowerPort *port = hal::board().power;
    return port && port->battery_present();
}

int power::battery_percent() {
    hal::PowerPort *port = hal::board().power;
    if (!port) return -1;
    return port->battery_percent();
}
