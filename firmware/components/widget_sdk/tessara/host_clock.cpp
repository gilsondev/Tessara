#include "tessara/host_clock.h"

namespace {

tessara::HostNowMs g_now = nullptr;

}  // namespace

void tessara::set_host_now_ms(HostNowMs fn) { g_now = fn; }

uint32_t tessara::host_now_ms() { return g_now ? g_now() : 0; }
