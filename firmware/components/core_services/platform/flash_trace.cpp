#include "platform/flash_trace.h"

#if defined(ARDUINO)

#include "platform/display_host.h"
#include "platform/log.h"

void flash_trace::note(const char *func) {
    if (!func || !func[0]) return;
    if (display_host::push_paused() || !display_host::push_busy()) return;
    static const char *seen[32];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (seen[i] == func) return;
    if (count < 32) seen[count++] = func;
    logger::printf("FLASH_COM_PUSH %s\n", func);
}

#else

void flash_trace::note(const char *) {}

#endif
