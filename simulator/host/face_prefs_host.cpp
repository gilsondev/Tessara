#include "platform/face_prefs.h"

namespace {

clock_face::Options g_opts{};

}  // namespace

void face_prefs::reload() {}

clock_face::Options face_prefs::options() { return g_opts; }
