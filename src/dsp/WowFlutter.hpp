#pragma once
#include "LookupTables.hpp"
#include "RandomGenerator.hpp"
#include <cmath>

namespace morphworx {

static constexpr float kPi = 3.14159265358979323846f;

// Per-line wow + flutter modulator for tape-style pitch drift.
// Produces a fractional-sample offset applied to an FDN read pointer.
//
// Wow:     ~0.3 Hz sine + low-pass-filtered noise for irregularity
// Flutter: ~11 Hz sine, small amplitude
//
// Amplitude calibration:
//   wow max:     700 samples at depth=1
//   flutter max: 8 samples at depth=1
//
// Audible pitch deviation = amplitude × 2π × freq / sampleRate.
// At 48kHz, 0.5Hz: 700 × π / 48000 = 4.6% ≈ 78 cents. Very obvious
// VHS-like pitch instability on sustained notes.
// Flutter at 8 samples, 11Hz: 8 × 2π × 11 / 48000 = 1.1% ≈ 20 cents.
//
// One instance per FDN line (8 total). Each has a phase offset so the
// modulation is decorrelated across lines.

class WowFlutter {
public:
    WowFlutter() = default;

    // Call once at module construction. lineIndex in [0,7].
    // init() MUST be called before process() — seed 0 is a placeholder only.
    void init(int lineIndex, uint32_t seed) {
        // Wow and flutter get independent offsets so they don't peak together.
        // Wow:     lineIndex / 8   spans [0, 0.875] across lines
        // Flutter: offset by half a step to break correlation with wow
        wowOffset_     = lineIndex * 0.125f;
        flutterOffset_ = lineIndex * 0.125f + 0.0625f;
        rng_.setSeed(seed);
    }

    // Call from onSampleRateChange() or module constructor — never per-sample.
    void setSampleRate(float sr) {
        if (sr <= 0.f) return;
        wowInc_      = 0.5f / sr;   // 0.5 Hz — classic VHS transport sway rate
        flutterInc_  = 11.0f / sr;  // 11 Hz flutter
        // One-pole LP coefficient for ~2 Hz noise smoothing.
        // expf is acceptable here — called only on sample rate change.
        noiseLpCoeff_ = 1.0f - std::exp(-2.0f * kPi * 2.0f / sr);
    }

    // Per-sample. Returns fractional-sample modulation offset.
    // depth in [0,1]: 0 = clean (returns 0), 1 = full wow+flutter.
    // setSampleRate() MUST be called first — returns 0 if increments are zero.
    float process(float depth) {
        // Defensive guard: if setSampleRate() was never called, increments
        // are 0 and both phases stay fixed, producing a DC constant instead
        // of modulation — completely inaudible as pitch drift.
        if (wowInc_ == 0.f) return 0.f;
        // Depth-0 shortcut: skip phase/LUT/RNG work when Mod knob is at zero.
        // Saves ~15 cycles × 8 FDN lines = ~120 cycles/sample at default settings.
        // Phasors stay frozen until depth rises — no jump on resume.
        if (depth < 0.001f) return 0.f;

        // --- Advance phase accumulators ---
        wowPhase_ += wowInc_;
        if (wowPhase_ >= 1.0f) wowPhase_ -= 1.0f;

        flutterPhase_ += flutterInc_;
        if (flutterPhase_ >= 1.0f) flutterPhase_ -= 1.0f;

        // --- Wow: sine + LP-filtered noise ---
        // Explicitly wrap phase to [0,1) before samplePhase() to avoid the
        // while-loop in LookupTable::lookup(). Sum can reach up to 1.875
        // (phase 0.875 + wowOffset 0.875), so one subtraction suffices —
        // but std::floor is cleaner and makes the intent explicit.
        // rng_.uniform(min,max) is verified in RandomGenerator.hpp line 22.
        float wowPhase = wowPhase_ + wowOffset_;
        wowPhase -= std::floor(wowPhase);
        float wowSine = pwmt::g_sineTable.samplePhase(wowPhase);

        float noise = rng_.uniform(-1.0f, 1.0f);
        noiseLpState_ += noiseLpCoeff_ * (noise - noiseLpState_);

        float wowMod = wowSine * 0.7f + noiseLpState_ * 0.3f;

        // --- Flutter: sine only (independent offset) ---
        float flutterPhase = flutterPhase_ + flutterOffset_;
        flutterPhase -= std::floor(flutterPhase);
        float flutterSine = pwmt::g_sineTable.samplePhase(flutterPhase);

        // Wow max 700 samples + flutter max 8 samples at depth=1.
        // 700 samples at 0.5Hz, 48kHz ≈ 78 cents. Very obvious VHS pitch sway.
        return depth * (wowMod * 700.0f + flutterSine * 8.0f);
    }

    // Zero accumulators and filter state. Does not re-seed RNG.
    // Intentionally does not reset wowOffset_/flutterOffset_ — set once by
    // init(), stable for the lifetime of this instance.
    void reset() {
        wowPhase_     = 0.f;
        flutterPhase_ = 0.f;
        noiseLpState_ = 0.f;
    }

    // Call at control rate (every 16 samples) to prevent noiseLpState_
    // from decaying into subnormal range during silence.
    // 1e-15f is well above the subnormal threshold (~1.18e-38) and is the
    // standard anti-denormal offset for one-pole filter states.
    void flushDenormals() {
        noiseLpState_ += 1e-15f;
    }

private:
    float wowPhase_     = 0.f;
    float flutterPhase_ = 0.f;
    float wowInc_       = 0.f;
    float flutterInc_   = 0.f;
    float noiseLpState_ = 0.f;
    float noiseLpCoeff_ = 0.f;
    float wowOffset_     = 0.f;   // set by init(), stable
    float flutterOffset_ = 0.f;   // independent from wowOffset_
    // Seed 0 is a placeholder — init() must be called before process().
    pwmt::RandomGenerator rng_{0};
};

} // namespace morphworx
