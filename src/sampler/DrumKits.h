#pragma once

#include <cstdint>

namespace bdkit {

static constexpr int NUM_KITS = 5;
static constexpr int NUM_INST = 6;
static constexpr int NUM_LAYERS = 3;

enum Instrument : int {
    BD = 0,
    SN = 1,
    GSN = 2,
    CH = 3,
    OH = 4,
    RC = 5,
};

struct Sample {
    const int16_t* data = nullptr;
    uint32_t frames = 0;
    uint32_t sampleRate = 48000;
};

// Filled at runtime by initKits().
extern Sample g_kits[NUM_KITS][NUM_INST][NUM_LAYERS];

void initKits();

const char* instrumentName(int inst);

} // namespace bdkit
