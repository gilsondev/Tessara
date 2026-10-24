#pragma once

#include "tessara/services.h"

// Binds daytime / settings / log adapters into the global Widget Runtime.
namespace tessara {

void bind_host_services();
HostServices &host_services();

}  // namespace tessara
