#pragma once

#include "tessera/services.h"

// Binds daytime / settings / log adapters into the global Widget Runtime.
namespace tessera {

void bind_host_services();
HostServices &host_services();

}  // namespace tessera
