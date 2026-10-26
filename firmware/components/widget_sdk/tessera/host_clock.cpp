#include "tessera/host_clock.h"

namespace {

tessera::HostNowMs g_now = nullptr;

}  // namespace

void tessera::set_host_now_ms(HostNowMs fn) { g_now = fn; }

uint32_t tessera::host_now_ms() { return g_now ? g_now() : 0; }
