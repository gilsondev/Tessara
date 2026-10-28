#pragma once

// One FLASH_COM_PUSH line per function per boot, and only when a frame push
// owns the semaphore. A pause_push hold is the safe path and stays quiet.
namespace flash_trace {

void note(const char *func);

}  // namespace flash_trace
