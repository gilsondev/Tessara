#pragma once

#include <stdint.h>

// USB/HTTP window that allows PUT /agent/agent.tgz (flash-agent / SD pack).
namespace agent_push {

void arm();
void disarm();
bool armed();

// Auto-arm for a few minutes after boot so `make flash-agent` works without USB arm.
void arm_boot();

}  // namespace agent_push
