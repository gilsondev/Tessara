#include "platform/log.h"

#include <cstdarg>
#include <cstdio>

namespace logger {
void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void lock() {}
void unlock() {}
}  // namespace logger
