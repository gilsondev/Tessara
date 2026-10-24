#include "platform/chime.h"

#include <cmath>

namespace {
constexpr uint32_t kRate = 16000;
constexpr int kFrames = 2400;

hal::SoundPort *port = nullptr;
int16_t pcm[kFrames * 2];
int position = kFrames;
bool enabled = true;

void synthesize() {
    for (int frame = 0; frame < kFrames; ++frame) {
        float t = frame / static_cast<float>(kRate);
        float envelope = expf(-t * 18.f);
        float tone = sinf(2.f * 3.1415926f * 660.f * t) * 0.72f + sinf(2.f * 3.1415926f * 990.f * t) * 0.28f;
        int sample = static_cast<int>(tone * envelope * 11000.f);
        if (sample > 14000) sample = 14000;
        if (sample < -14000) sample = -14000;
        pcm[frame * 2] = pcm[frame * 2 + 1] = static_cast<int16_t>(sample);
    }
}
}  // namespace

void chime::begin(hal::SoundPort *sound) {
    if (!sound || !sound->begin(kRate)) return;
    port = sound;
    synthesize();
}

void chime::set_enabled(bool value) { enabled = value; }

void chime::play() {
    if (port && enabled) position = 0;
}

void chime::pump() {
    while (port && position < kFrames) {
        int chunk = kFrames - position;
        if (chunk > 256) chunk = 256;
        size_t accepted = port->write(pcm + position * 2, chunk);
        if (!accepted) return;
        position += static_cast<int>(accepted);
        if (accepted < static_cast<size_t>(chunk)) return;
    }
}
