#pragma once

// Serial output shared by several tasks. Binary dumps hold the lock so log lines never land inside them.
namespace logger {

void printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void lock();
void unlock();

}  // namespace logger
