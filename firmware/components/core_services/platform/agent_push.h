#pragma once

#include <stdint.h>

// USB/HTTP window that allows PUT /agentd/agentd.tgz (SD pack / portal upload).
namespace agent_push {

void arm();
void disarm();
bool armed();

// Auto-arm for a few minutes after boot so `make flash-agent` works without USB arm.
void arm_boot();

}  // namespace agent_push
