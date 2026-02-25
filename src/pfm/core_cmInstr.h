// PreenFM2 VCV Port - Shim for ARM core_cmInstr.h
// Replaces __USAT intrinsic with portable inline function
#pragma once

#include <cstdint>
#include <algorithm>

// ARM __USAT(val, bits): Unsigned saturate val to [0, 2^bits - 1]
#ifndef __USAT
inline uint32_t __USAT(int32_t val, uint32_t sat) {
    uint32_t max = (1u << sat) - 1;
    if (val < 0) return 0;
    if ((uint32_t)val > max) return max;
    return (uint32_t)val;
}
#endif
