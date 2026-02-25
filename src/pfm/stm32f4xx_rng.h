// PreenFM2 VCV Port - Shim for STM32 RNG
// Replaces hardware RNG with software PRNG
#pragma once

#include <cstdint>

// Simple xorshift32 PRNG as replacement for hardware RNG
inline uint32_t RNG_GetRandomNumber() {
    static uint32_t state = 2463534242u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
