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
    float framePos   = 0.0f;   // wavetable frame position (0..1)
    int   tableIndex = 0;      // which wavetable to read from

    OpDomain domain  = OpDomain::WT_PM;

    // Spectral warp controls (pad-friendly, continuous)
    // pdAmount: blends phase distortion / warp depth into PM (0..1)
    // warpMode: selects warp shape (0=Classic PD, 1=Bend+, 2=Bend-, 3=Sync, 4=Quantize, 5=Asym)
    // wsMix:    blends gentle waveshaping post-lookup (0..1)
    // wsDrive:  base drive amount (1..~4 typical)
    float pdAmount = 0.0f;
    int   warpMode = 0;
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

    // ── State (internal) ────────────────────────────────────────────
    float phase[kMaxUnison] = {};
    float prevOut = 0.0f;
    float output  = 0.0f;       // mono-summed output after last tick
    float outputL = 0.0f;       // stereo unison L
    float outputR = 0.0f;       // stereo unison R

    // Local RNG for Noise_PM domain (xorshift32)
    uint32_t rngState = 0xA5A5A5A5u;

    void reset() {
        for (int i = 0; i < kMaxUnison; ++i) phase[i] = 0.0f;
        prevOut = 0.0f;
        output = outputL = outputR = 0.0f;
        rngState = 0xA5A5A5A5u;
    }

    // Call once per sample.
    // `fundamentalHz` = note frequency (already including pitch bend, etc.)
    // `modInput`      = summed phase-mod signal from other operators
    // `table`         = pointer to current Wavetable (may be nullptr → sine fallback)
    // `sampleRate`    = host sample rate
    void tick(float fundamentalHz, float modInput, const Wavetable* table, float sampleRate);

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
