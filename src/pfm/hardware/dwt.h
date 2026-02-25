// PreenFM2 VCV Port - Shim for ARM DWT cycle counter
// Stubs out all cycle measurement macros
#pragma once

struct CYCCNT_buffer {
    int remove() { return 0; }
};

#define CYCLE_MEASURE_START(x)
#define CYCLE_MEASURE_END()
