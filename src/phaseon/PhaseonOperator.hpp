/*
 * PhaseonOperator.hpp — Single oscillator/operator for Phaseon
 *
 * Phase-modulation oscillator with wavetable lookup, self-feedback,
 * and per-operator unison.  Pure C++, no framework dependencies.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace phaseon {

// Forward
struct Wavetable;

#ifdef METAMODULE

inline float phaseon_fast_sin_01(float p) {
    p = p - std::floor(p);
    float x = p * 2.0f;
    if (x > 1.0f) x -= 2.0f;
    return 4.0f * x * (1.0f - std::fabs(x));
}
inline float phaseon_fast_tanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// Fast sin/cos for radian argument (used by biquad coefficient calculation)
// Bhaskara I approximation — accurate to ~0.1% over full range
inline float phaseon_fast_sin_w0(float w) {
    // Normalize to [0, 2pi)
    const float TWO_PI = 6.283185307f;
    w = w - std::floor(w / TWO_PI) * TWO_PI;
    // Map to [-pi, pi]
    const float PI = 3.14159265f;
    if (w > PI) w -= TWO_PI;
    float x = w;
    float x2 = x * x;
    return x * (PI * PI - x2) / (PI * PI + 0.2f * x2); // ~Bhaskara
}
inline float phaseon_fast_cos_w0(float w) {
    return phaseon_fast_sin_w0(w + 1.5707963f); // cos(w) = sin(w + pi/2)
}
#else

inline float phaseon_fast_sin_01(float p) {
    p = p - std::floor(p);
    float x = p * 2.0f;
    if (x > 1.0f) x -= 2.0f;
    return 4.0f * x * (1.0f - std::fabs(x));
}
inline float phaseon_fast_tanh(float x) { return std::tanh(x); }
inline float phaseon_fast_sin_w0(float w) { return std::sin(w); }
inline float phaseon_fast_cos_w0(float w) { return std::cos(w); }
#endif


// Operator domain: how modulation energy is applied.
// WT_PM    = wavetable phase modulation (classic)
// WT_PD    = wavetable phase distortion (warp phase curve)
// WT_WS    = wavetable waveshaping (drive/output shaping)
// Noise_PM = phase modulation driven by internal noise (plus a little input)
enum class OpDomain : uint8_t {
    WT_PM = 0,
    WT_PD = 1,
    WT_WS = 2,
    Noise_PM = 3,
};

// ─── Operator ───────────────────────────────────────────────────────
// One operator = one phase-modulation oscillator reading from a wavetable.
//
// Core per-sample:
//   pmPhase  = wrap01(basePhase + pmDepth * modInput)
//   output   = wavetable.sample(pmPhase, framePos)
//   basePhase += freq / sampleRate
//
// Self-feedback:
//   modInput += feedback * prevOutput

struct Operator {
    // ── Parameters (set before processing) ──────────────────────────
    float ratio      = 1.0f;   // frequency ratio relative to fundamental
    float level      = 1.0f;   // output level (0..1+)
    float fmDepth    = 0.0f;   // phase-mod depth from incoming modulator(s)
    float feedback   = 0.0f;   // self-feedback amount (0..~1)
    float feedbackWarmth = 0.0f; // 0..1, blends linear feedback into warm soft saturation
    float framePos   = 0.0f;   // wavetable frame position (0..1)
    int   tableIndex = 0;      // which wavetable to read from

    OpDomain domain  = OpDomain::WT_PM;

    // Spectral warp controls (pad-friendly, continuous)
    // pdAmount: blends phase distortion / warp depth into PM (0..1)
    // warpMode: selects warp shape (0=Off, 1=Bend+, 2=Bend-, 3=Sync, 4=Quantize, 5=Asym, 6=Classic PD hybrid)
    // wsMix:    blends gentle waveshaping post-lookup (0..1)
    // wsDrive:  base drive amount (1..~4 typical)
    float pdAmount = 0.0f;
    int   warpMode = 0;
    float phaseWarp = 0.0f; // Serum-style Bend +/- warp amount (-1..+1)
    float tear     = 0.0f;  // Extra asymmetric phase warp (0..1), applied after phaseWarp
    float wsMix    = 0.0f;
    float wsDrive  = 1.0f;

    // ── Unison ──────────────────────────────────────────────────────
    static constexpr int kMaxUnison = 5;
    int   unisonCount  = 1;        // 1 = off
    float unisonDetune = 0.005f;   // cents-ish spread (0..0.05 typical)
    float unisonStereo = 0.5f;     // stereo spread (0..1)
    float unisonPhaseSpread = 0.0f; // 0..~0.25 cycles, width without detune

    // Bandlimit bias (higher = more conservative mip choice under heavy FM/drive)
    float bandlimitBias = 1.0f;

    // Cached mip level for the current table (computed at control-rate).
    int   mipLevel = 0;

    // Cached phase increment (set at control rate: freq * ratio / sampleRate)
    float cachedPhaseInc = 0.0f;
    float cachedSampleRate = 0.0f;
    float cachedPmSlewCoef = 0.0f;
    float cachedFbAlpha = 0.0f;
    float cachedPhaseWarpInput = 999.0f;
    float cachedPhaseWarpCurve = 1.0f;

    // ── State (internal) ────────────────────────────────────────────
    float phase[kMaxUnison] = {};
    float prevOut = 0.0f;
    float output  = 0.0f;       // mono-summed output after last tick
    float outputL = 0.0f;       // stereo unison L
    float outputR = 0.0f;       // stereo unison R

    // PM engine state (DC blocking + slew-smoothed depth)
    float currentPmDepth = 0.0f;
    float modDcBlockerState = 0.0f;
    float modDcBlockerPrev = 0.0f;

    // Feedback PM core state
    float filteredFeedback = 0.0f;
    float fbDcBlockerState = 0.0f;
    float fbDcBlockerPrev = 0.0f;

    // Local RNG for Noise_PM domain (xorshift32)
    uint32_t rngState = 0xA5A5A5A5u;

    void reset() {
        for (int i = 0; i < kMaxUnison; ++i) phase[i] = 0.0f;
        prevOut = 0.0f;
        output = outputL = outputR = 0.0f;
        currentPmDepth = 0.0f;
        modDcBlockerState = 0.0f;
        modDcBlockerPrev = 0.0f;
        filteredFeedback = 0.0f;
        fbDcBlockerState = 0.0f;
        fbDcBlockerPrev = 0.0f;
        rngState = 0xA5A5A5A5u;
        mipLevel = 0;
        cachedPhaseInc = 0.0f;
        cachedSampleRate = 0.0f;
        cachedPmSlewCoef = 0.0f;
        cachedFbAlpha = 0.0f;
        cachedPhaseWarpInput = 999.0f;
        cachedPhaseWarpCurve = 1.0f;
    }

    // Call once per sample.
    // `fundamentalHz` = note frequency (already including pitch bend, etc.)
    // `modInput`      = summed phase-mod signal from other operators
    // `table`         = pointer to current Wavetable (may be nullptr → sine fallback)
    // `sampleRate`    = host sample rate
    // `currentEnv`    = external envelope follower state (0..2 typical)
    void tick(float fundamentalHz, float modInput, const Wavetable* table, float sampleRate, float currentEnv = 1.0f);

    // ── Helpers ─────────────────────────────────────────────────────
    static inline float wrap01(float p) {
        p -= (float)(int)p;
        if (p < 0.0f) p += 1.0f;
        return p;
    }

    // Fast sine fallback (no table needed)
    static inline float fastSin(float phase01) {
        float x = phase01 * 6.283185307f; // 2π
        // Bhaskara approximation, good enough for FM
        float x2 = x * x;
        return x * (3.14159265f - x) / (2.46740110f * x2 + 0.5f);
    }
};

} // namespace phaseon
