/*
 * PhaseonMacros.hpp â€” Macro-to-parameter mapping for Phaseon
 *
 * Maps 5 user-facing macro knobs + 6 CV performance inputs to
 * internal operator-level parameters.  All "operator math" is hidden.
 *
 * Macro Knobs:     Timbre, Motion, Density, Edge, FM Character
 * CV Performance:  HarmonicDensity, FMChaos, TransientSpike,
 *                  WTRegionBias, SpectralTilt, Instability
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary â€” not licensed under GPL or any open-source license.
 */
#pragma once
#include "PhaseonVoice.hpp"
#include "PhaseonAlgorithm.hpp"
#include "PhaseonModMatrix.hpp"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace phaseon {

// â”€â”€â”€ Sweet-spot ratio sets â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// FM Character macro selects/blends between these curated ratio sets.
// Each set has 6 ratios (one per operator).
struct RatioSet {
    const char* name;
    float ratios[6];
};

// 7 sets: first 4 are harmonic (knob 0..~0.80), last 3 are inharmonic-but-musical (knob ~0.80..1.0).
// kHarmonicSetCount marks the boundary.
static constexpr int kRatioSetCount    = 7;
static constexpr int kHarmonicSetCount = 4;   // indices 0..3 are "safe" harmonic territory

inline const RatioSet& getRatioSet(int index) {
    static const RatioSet sets[kRatioSetCount] = {
        // ── Harmonic territory (0..3) ──────────────────────────────
        { "Harmonic",     { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f } },
        { "Sub-heavy",    { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 0.25f } },
        { "Detuned",      { 1.0f, 1.003f, 2.0f, 2.004f, 3.0f, 3.007f } },
        { "Aggressive",   { 1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f } },

        // ── Inharmonic-but-musical territory (4..6) ───────────────
        // "Bright Bell": classic DX7 bell recipe — integer anchors (1,2,4) keep
        // pitch clear, √2 multiples (1.414, 2.828, 5.657) add bell shimmer.
        // Great for plucky FM stabs, e-piano, and melodic techno leads.
        { "Bright Bell",  { 1.0f, 1.414f, 2.0f, 2.828f, 4.0f, 5.657f } },

        // "Dark Clang": π-anchored "struck tube" character mixed with integers.
        // 0.707 (1/√2) adds a dark sub-tritone; π and 2π create tube resonance.
        // Pitched but gritty — excellent for dark techno stabs and percussion.
        { "Dark Clang",   { 1.0f, 0.707f, 2.0f, 3.14f, 4.0f, 6.28f } },

        // "Metallic": all-irrational ratios (√2, π, √5, etc.) for maximum
        // inharmonicity. The "extreme" end of the knob for noise/FX textures.
        { "Metallic",     { 1.0f, 1.414f, 3.14159f, 2.236f, 5.831f, 7.071f } },
    };
    if (index < 0) index = 0;
    if (index >= kRatioSetCount) index = kRatioSetCount - 1;
    return sets[index];
}

namespace detail {

inline float clamp01(float v) {
    if (!(v >= 0.0f)) return 0.0f; // also catches NaN
    if (v > 1.0f) return 1.0f;
    return v;
}

inline char lowerAscii(char c) {
    if (c >= 'A') {
        if (c <= 'Z') return (char)(c + ('a' - 'A'));
    }
    return c;
}

inline bool iequalsAscii(const char* a, const char* b) {
    if (a == b) return true;
    if (!a) return false;
    if (!b) return false;

    for (;;) {
        const char ca = lowerAscii(*a);
        const char cb = lowerAscii(*b);
        if (ca != cb) return false;
        if (ca == '\0') return true;
        ++a;
        ++b;
    }
}

inline int findRatioSetIndexByName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kRatioSetCount; ++i) {
        const RatioSet& rs = getRatioSet(i);
        if (rs.name) {
            if (iequalsAscii(rs.name, name)) return i;
        }
    }
    return -1;
}

} // namespace detail

inline int fmCharacterToRatioIndex(float fmCharacter01) {
    float fc = detail::clamp01(fmCharacter01);

    if (kRatioSetCount <= 1)
        return 0;

    // ── Zone mapping ────────────────────────────────────────────
    // 0.00 .. 0.80  →  harmonic sets 0..kHarmonicSetCount-1 (quadratic bias)
    // 0.80 .. 1.00  →  inharmonic sets kHarmonicSetCount..kRatioSetCount-1
    const float harmonicEnd = 0.80f;
    const int inharmonicCount = kRatioSetCount - kHarmonicSetCount;

    // ── Inharmonic zone (last 20%) ──────────────────────────────
    if (fc >= harmonicEnd && inharmonicCount > 0) {
        float u = (fc - harmonicEnd) / (1.0f - harmonicEnd); // 0..1
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        int inhIdx = (inharmonicCount == 1)
            ? 0
            : (int)(u * (float)(inharmonicCount - 1) + 0.5f);
        if (inhIdx < 0) inhIdx = 0;
        if (inhIdx >= inharmonicCount) inhIdx = inharmonicCount - 1;
        return kHarmonicSetCount + inhIdx;
    }

    // ── Harmonic zone (first 80%) ───────────────────────────────
    if (kHarmonicSetCount <= 1)
        return 0;
    float t = fc / harmonicEnd;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Quadratic bias: spend more travel on the earlier (more harmonic) sets.
    t = t * t;

    int idx = (int)(t * (float)(kHarmonicSetCount - 1) + 0.5f);
    if (idx < 0) idx = 0;
    if (idx >= kHarmonicSetCount) idx = kHarmonicSetCount - 1;
    return idx;
}

// â”€â”€â”€ Macro state (all 0..1 normalized) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct PhaseonMacroState {
    // Knobs (top half)
    float timbre      = 0.28f;
    float motion      = 0.26f;   // internal LFO depth
    float density     = 0.55f;
    float edge        = 1.0f;
    float fmCharacter = 0.52f;   // selects ratio set + algo tweaks

    // Extra macro knobs
    float morph       = 0.52f;  // algorithm morph between current and next (0..1)
    float complex     = 0.0f;   // complexity (domain + 2nd-order depth modulation)

    // Cross-operator coupling (0..1)
    float network     = 0.0f;

    // Scene morph envelopes
    float envStyle    = 0.0f;   // 0..4 continuous
    float envSpread   = 1.0f;   // 0..1

    // TAME: global softener for harshness/clipping (0=off, 1=max tame)
    float tame        = 0.0f;

    // Spike (transient punch): 0..1, drives TransientSpike intensity + duration
    float spike       = 0.0f;

    // Master tail control (0 = ultra short, 1 = very long / drone-ish)
    float tail        = 0.73f;

    // Drift: combined Chaos + Instability (was WT Family)
    // 0..0.5 = instability ramps up, 0.5..1.0 = chaos joins in
    float drift       = 0.0f;

    // Explicit wavetable selector (table index in the loaded bank)
    int   wtSelect    = 0;

    // Per-operator waveform mode (0=WT, 1=Sine, 2=Triangle, 3=Saw, 4=Harmonic Sine, 5=Skewed Sine)
    // Stored/owned by the module/preset system; applied here at control-rate.
    uint8_t opWaveMode[PhaseonVoice::kNumOps] = { 0, 0, 0, 0, 0, 0 };

    // SCRAMBLE: per-operator envelope diversity (0 = uniform, 1 = wildly different)
    float scramble    = 0.0f;

    // CV performance (bottom half) â€” added on top of knob values
    float cvHarmonicDensity = 0.0f;  // -1..+1 (bipolar)
    float cvWtSelect        = 0.0f;  // 0..1   â€” normalized WT-Select CV (for mod matrix source)
    float cvTimbre2         = 0.0f;  // 0..1   â€” perf timbre CV (for mod matrix source; already baked into timbre)
    float cvMotion          = 0.0f;  // 0..1   â€” additive on top of motion knob
    float cvWtRegionBias    = 0.0f;  // -1..+1
    float cvSpectralTilt    = 0.0f;  // -1..+1
    float formant           = 0.0f;  // 0..1 (formant amount knob)
    float cvFormant         = 0.0f;  // -1..+1 (formant CV, +-5V bipolar)
    float cvEnvSpread       = 0.0f;  // 0..1   â€” additive on top of envSpread knob
    float cvEdge            = 0.0f;  // 0..1   â€” additive on top of edge knob
    float cvComplexity      = 0.0f;  // 0..1   â€” additive on top of complex knob
    float cvMorph           = 0.0f;  // -1..+1 â€” additive on top of morph knob

    // Algorithm (typically from FM Character or separate selector)
    int algorithmIndex = 0;
    // Warp mode selector (0=Classic PD, 1=Bend+, 2=Bend-, 3=Sync, 4=Quantize, 5=Asym)
    int warpMode = 0;
    // Modulation matrix (6 slots: source â†’ destination + amount)
    PhaseonModConfig modMatrix;
};

// â”€â”€â”€ Apply macros to voice â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Call once per control-rate block (e.g. every 32 samples).
inline void applyMacros(PhaseonVoice& voice, const PhaseonMacroState& m,
                        const WavetableBank& bank, float sampleRate = 44100.0f) {
    const int numTables = bank.count();

    const float tame = std::max(0.0f, std::min(1.0f, m.tame));

    // â”€â”€ FM Character â†’ ratio set + algorithm â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Maps 0..1 to ratio sets 0..N-1, snapping to curated sets
    int ratioIdx = fmCharacterToRatioIndex(m.fmCharacter);
    const RatioSet& ratios = getRatioSet(ratioIdx);

    voice.algorithmIndex = m.algorithmIndex;
    voice.algorithmMorph = std::max(0.0f, std::min(1.0f, m.morph + m.cvMorph));
    voice.complexAmount  = std::max(0.0f, std::min(1.0f, m.complex + m.cvComplexity));
    voice.networkAmount  = std::max(0.0f, std::min(1.0f, m.network));

    // Scene morph envelope controls
    voice.envStyle  = std::max(0.0f, std::min(4.0f, m.envStyle));
    voice.envSpread = std::max(0.0f, std::min(1.0f, m.envSpread + m.cvEnvSpread));
    voice.roleBias  = 0.78f;  // hardcoded default (was BIAS knob)

    // â”€â”€ Timbre â†’ wavetable frame position (per-op spread) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Operators spread across table frames, with Timbre controlling center.
    // Carrier ops: closer to Timbre value
    // Modulator ops: offset outward for timbral variety
    float baseFP = std::max(0.0f, std::min(1.0f, m.timbre + m.cvWtRegionBias * 0.5f));
    const Algorithm& algo = getAlgorithm(m.algorithmIndex);

    // â”€â”€ COMPLEX â†’ spectral warp / pad motion â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // No extra noise: keep domains stable and use continuous phase distortion + gentle waveshaping.
    float cx = voice.complexAmount;
    float cx2 = cx * cx;
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        // Keep everything in WT_PM domain for pad-friendly behavior.
        voice.ops[i].domain = OpDomain::WT_PM;
        voice.ops[i].warpMode = m.warpMode;

        // Per-operator warp personality (subtle spread avoids â€œsameyâ€ motion)
        float t = (float)i / (float)(PhaseonVoice::kNumOps - 1); // 0..1
        float spread = 0.85f + 0.30f * t;

        // Carriers: more stable, less distortion; modulators can be more warped.
        float carrierScale = algo.isCarrier[i] ? 0.60f : 1.00f;

        // Phase distortion blend grows with COMPLEX (spectral warp without harshness)
        voice.ops[i].pdAmount = std::max(0.0f, std::min(1.0f, cx2 * 0.95f * spread * carrierScale));

        // Gentle waveshaping mix and base drive (kept conservative for pads)
        voice.ops[i].wsMix   = std::max(0.0f, std::min(1.0f, cx * 0.55f * spread * carrierScale));
        voice.ops[i].wsDrive = 1.0f + cx * (algo.isCarrier[i] ? 1.0f : 1.6f);

        // TAME reduces distortion contributors even when COMPLEX/EDGE are high.
        voice.ops[i].pdAmount *= (1.0f - 0.60f * tame);
        voice.ops[i].wsMix    *= (1.0f - 0.70f * tame);
        voice.ops[i].wsDrive   = 1.0f + (voice.ops[i].wsDrive - 1.0f) * (1.0f - 0.75f * tame);
    }

    // 2nd-order depth modulation: keep it musical and pad-safe.
    // Split connection energy between phase and depth modulation, then depth-mod scales FM depth.
    voice.depthModMix    = cx2 * 0.65f;   // 0..~0.65
    voice.depthModAmount = cx * 0.40f;    // 0..~0.40

    auto hash32_local = [](uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };
    auto u01_local = [](uint32_t x) { return (float)(x & 0x00FFFFFFu) / 16777216.0f; };

    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        float offset = (float)i * 0.08f; // slight spread
        if (!algo.isCarrier[i]) offset += 0.15f; // modulators push further

        // SHUFFLE imprint: deterministic per-op region offsets from roleSeed.
        // This makes the button dramatically re-voice the engine without breaking tuning.
        uint32_t seed = voice.roleSeed ^ (uint32_t)(0xA511E9B3u * (uint32_t)(i + 1));
        float r = u01_local(hash32_local(seed)) * 2.0f - 1.0f; // -1..+1
        float spreadAmt = std::max(0.0f, std::min(1.0f, m.envSpread));
        float edgeAmt = std::max(0.0f, std::min(1.0f, m.edge));
        // Bigger re-voice range than before; still clamped to [0..1].
        float shuffleOffset = r * (0.30f + 0.45f * spreadAmt + 0.20f * edgeAmt);

        voice.ops[i].framePos = std::max(0.0f, std::min(1.0f, baseFP + offset + shuffleOffset));
    }

    // â”€â”€ SHUFFLE timbral personality (seeded by roleSeed) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Make SHUFFLE more exciting by altering operator FM depth and adding a bit of PD/WS
    // drive even when COMPLEX is low. Carriers are protected vs wild jumps.
    {
        auto clamp01_local = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
        auto clampf_local = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };

        float spreadAmt = clamp01_local(m.envSpread);
        float edgeAmt = clamp01_local(m.edge);
        float dens = std::max(0.0f, std::min(1.0f, m.density + m.cvHarmonicDensity * 0.5f));

        // Global shuffle intensity: audible at low settings, wilder with edge/density.
        float shGlobal = (0.35f + 0.55f * edgeAmt + 0.25f * dens) * (0.75f + 0.55f * spreadAmt);

        auto hash32_local = [](uint32_t x) {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        };
        auto u01_local = [](uint32_t x) { return (float)(x & 0x00FFFFFFu) / 16777216.0f; };

        for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
            uint32_t seed = voice.roleSeed ^ (uint32_t)(0xA511E9B3u * (uint32_t)(i + 1));
            float rA = u01_local(hash32_local(seed ^ 0x11u)) * 2.0f - 1.0f;
            float rB = u01_local(hash32_local(seed ^ 0x22u)) * 2.0f - 1.0f;

            float isCarrier = algo.isCarrier[i] ? 1.0f : 0.0f;
            float modScale = 1.0f - 0.55f * isCarrier;
            float sh = shGlobal * modScale;

            // FM depth re-voice (timbre changes without breaking tuning)
            float fmMul = 1.0f + clampf_local(rA, -1.0f, 1.0f) * (1.20f * sh);
            voice.ops[i].fmDepth *= clampf_local(fmMul, 0.15f, 5.00f);

            // Extra warp/drive personality; stays bounded.
            float pdAdd = (0.10f + 0.35f * edgeAmt) * sh * (0.35f + 0.65f * std::fabs(rB));
            voice.ops[i].pdAmount = clamp01_local(voice.ops[i].pdAmount + pdAdd);

            float wsAdd = (0.08f + 0.22f * edgeAmt) * sh * (0.50f + 0.50f * rB);
            voice.ops[i].wsMix = clamp01_local(voice.ops[i].wsMix + wsAdd);
            voice.ops[i].wsDrive = std::max(1.0f, voice.ops[i].wsDrive + (0.55f + 1.10f * edgeAmt) * sh * std::fabs(rA));

            // Encourage more conservative mip choice under heavier shuffle drive.
            voice.ops[i].bandlimitBias = 1.0f + 0.90f * sh;
        }
    }

    // â”€â”€ Edge + WT Select â†’ wavetable selection + waveform harshness â”€
    // Edge controls harshness/drive; WT Select picks the actual table.
    {
        float e = std::max(0.0f, std::min(1.0f, m.edge));

        voice.wtFamily = 1;  // hardcoded default (was WT Family knob)
        voice.edgeAmount = std::max(0.0f, std::min(1.0f, e + m.cvEdge));
        voice.edgeAmount *= (1.0f - 0.55f * tame);
        voice.timbreAmount = std::max(0.0f, std::min(1.0f, m.timbre));
        voice.tiltAmount = std::max(-1.0f, std::min(1.0f, m.cvSpectralTilt));

        int baseTableIndex = m.wtSelect;
        if (numTables <= 0) {
            baseTableIndex = 0;
        } else {
            if (baseTableIndex < 0) baseTableIndex = 0;
            if (baseTableIndex >= numTables) baseTableIndex = numTables - 1;
        }

        // Operator waveform modes:
        // Keep the wavetable bank compact (curated tables only). Basic shapes are
        // implemented procedurally via negative sentinel indices. PhaseonVoice
        // will pass a null wavetable pointer for any negative index; Operator::tick()
        // then generates the requested shape.
        constexpr int kProcSine         = -1;
        constexpr int kProcTriangle     = -2;
        constexpr int kProcSaw          = -3;
        constexpr int kProcHarmonicSin  = -4;
        constexpr int kProcSkewedSin    = -5;

        auto clampIndex = [&](int idx) {
            if (idx < 0) return idx; // preserve procedural sentinel indices
            if (numTables <= 0) return 0;
            if (idx >= numTables) return numTables - 1;
            return idx;
        };

        for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
            int mode = (int)m.opWaveMode[i];
            int idx = baseTableIndex;
            switch (mode) {
            default:
            case 0: idx = baseTableIndex; break; // WT
            case 1: idx = kProcSine;      break; // Sine
            case 2: idx = kProcTriangle;  break; // Triangle
            case 3: idx = kProcSaw;       break; // Saw
            case 4: idx = kProcHarmonicSin; break; // Harmonic-enhanced sine (controlled additive)
            case 5: idx = kProcSkewedSin; break; // Skewed/phase-warped sine
            }
            voice.ops[i].tableIndex = clampIndex(idx);

            // All waveform modes remain in WT_PM domain; "Skewed sine" forces extra phase warp.
            voice.ops[i].domain = OpDomain::WT_PM;
            if (mode == 5) {
                // Force a strong, audible warp even when COMPLEX is low.
                voice.ops[i].warpMode = 5; // Asym
                voice.ops[i].pdAmount = std::max(voice.ops[i].pdAmount, 0.85f);
                voice.ops[i].wsMix = std::max(voice.ops[i].wsMix, 0.10f);
                voice.ops[i].wsDrive = std::max(voice.ops[i].wsDrive, 1.25f);
            }
        }
    }

    // â”€â”€ Density â†’ FM indices â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Signature 1: "FM That Feels Liquid"
    // Lower-ratio pairs increase FM faster (filling low harmonics first),
    // higher-ratio pairs kick in later via exponential curves.
    float totalDensity = std::max(0.0f, std::min(1.0f,
        m.density + m.cvHarmonicDensity * 0.5f));
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        float r = ratios.ratios[i];
        // Spectral curve: low ratios respond earlier, high ratios later
        float sensitivity = 1.0f / (1.0f + r * 0.3f);

        // Density can get harsh quickly with squared scaling (especially with warp/edge).
        // Keep the same overall behavior but tame the extreme top end.
        float rawDepth = totalDensity * sensitivity * 2.20f;

        // Softer-than-square curve: preserves low/mid movement but avoids "scrape" at max.
        float fmIdx = rawDepth * rawDepth;
        fmIdx = 6.00f * tanhf(fmIdx / 6.00f);

        voice.ops[i].fmDepth = fmIdx;

        // TAME reduces overall FM depth (prevents scrape/aliasy chaos at high density).
        voice.ops[i].fmDepth *= (1.0f - 0.55f * tame);

        // More conservative mip selection as density rises (reduces alias-like scraping).
        voice.ops[i].bandlimitBias = std::max(voice.ops[i].bandlimitBias, 1.0f + totalDensity * 1.25f + tame * 0.75f);
    }

    // â”€â”€ Signature 2: WT Region Personality â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // FM depth scaled by wavetable zone:
    //   Low zone (0-0.33):  attenuate FM (stable)
    //   Mid zone (0.33-0.66): normal (aggressive)
    //   High zone (0.66-1.0): amplify FM (chaotic)
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        float fp = voice.ops[i].framePos;
        float zoneMult;
        if (fp < 0.33f)
            zoneMult = 0.5f + fp * 1.5f;        // 0.5 .. 1.0
        else if (fp < 0.66f)
            zoneMult = 1.0f;                     // 1.0
        else
            zoneMult = 1.0f + (fp - 0.66f) * 1.8f; // 1.0 .. ~1.61
        voice.ops[i].fmDepth *= zoneMult;
    }

    // â”€â”€ Operator ratios (from FM Character set) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        voice.ops[i].ratio = ratios.ratios[i];
    }

    // â”€â”€ Operator levels â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Carriers at full level, modulators scale with density
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        if (algo.isCarrier[i]) {
            voice.ops[i].level = 1.0f;
        } else {
            // Keep modulators from overwhelming the bus at high density.
            voice.ops[i].level = 0.25f + totalDensity * 0.55f;
        }

        // TAME reduces operator level for extra headroom.
        voice.ops[i].level *= (1.0f - 0.40f * tame);
    }

    // â”€â”€ Per-operator modulation envelopes (Scene Morph) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Bass-first: Impact/Stab/Lead/Growl dominate; Swell exists but isn't the focus.
    // rolePos is generated deterministically per note in PhaseonVoice.
    auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };

    auto archetype = [](int idx, float& a, float& d, float& s, float& r, bool& loop) {
        loop = false;
        switch (idx) {
        default:
        case 0: // Impact (transient + body)
            a = 0.0012f; d = 0.070f; s = 0.02f; r = 0.060f; loop = false;
            break;
        case 1: // Stab (tight, almost no release)
            a = 0.0016f; d = 0.095f; s = 0.00f; r = 0.012f; loop = false;
            break;
        case 2: // Lead (sustain-capable)
            a = 0.0040f; d = 0.220f; s = 0.60f; r = 0.160f; loop = false;
            break;
        case 3: // Swell (pad-ish, optional)
            a = 0.080f;  d = 0.650f; s = 0.80f; r = 0.750f; loop = false;
            break;
        case 4: // Growl Loop (looping AD)
            // Slow enough to feel like dubstep movement when used on modulators.
            a = 0.030f;  d = 0.220f; s = 0.25f; r = 0.020f; loop = true;
            break;
        }
    };

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    // Deterministic per-op "personality" offsets (seeded by roleSeed)
    auto hash32 = [](uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };
    auto u01 = [](uint32_t x) { return (float)(x & 0x00FFFFFFu) / 16777216.0f; };

    float edgeV = clamp01(m.edge);
    float spread = clamp01(m.envSpread);
    float scramble = clamp01(m.scramble);
    float loopRate = 1.0f; // LOOP_RATE_CV removed; loop rate fixed at unity

    // â”€â”€ SCRAMBLE: assign unique secondary targets per operator â”€â”€â”€â”€â”€â”€â”€
    // Pre-compute target assignments so no two ops share the same target.
    {
        bool usedTarget[6] = {};
        for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
            uint32_t tseed = voice.roleSeed ^ (uint32_t)(0xC3D2E1F0u * (uint32_t)(i + 1));
            int tid = (int)(hash32(tseed ^ 0x600u) % 6u);
            // Resolve collisions: find next free slot
            for (int tries = 0; tries < 6; ++tries) {
                if (!usedTarget[tid]) break;
                tid = (tid + 1) % 6;
            }
            usedTarget[tid] = true;
            voice.scrambleTargetId[i] = tid;
            // Quadratic ramp: subtle at low SCRAMBLE, strong at high
            voice.scrambleSecAmount[i] = scramble * scramble * 1.6f;
        }
    }

    // â”€â”€ Carrier protection: find the most stable carrier â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    int protectedCarrier = -1;
    {
        float bestPos = 999.0f;
        for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
            if (algo.isCarrier[i]) {
                float p = voice.opRolePos[i];
                // Prefer carriers closest to Lead zone (rolePos ~2)
                float dist = std::fabs(p - 2.0f);
                if (dist < bestPos) {
                    bestPos = dist;
                    protectedCarrier = i;
                }
            }
        }
    }

    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        PhaseonEnvelope& e = voice.opEnvs[i];

        float p = voice.opRolePos[i];
        p = clampf(p, 0.0f, 4.0f);
        int i0 = (int)p;
        int i1 = std::min(i0 + 1, 4);
        float t = p - (float)i0;

        float a0, d0, s0, r0;
        float a1, d1, s1, r1;
        bool loop0, loop1;
        archetype(i0, a0, d0, s0, r0, loop0);
        archetype(i1, a1, d1, s1, r1, loop1);

        float a = lerp(a0, a1, t);
        float d = lerp(d0, d1, t);
        float s = lerp(s0, s1, t);
        float r = lerp(r0, r1, t);
        bool loop = (t < 0.5f) ? loop0 : loop1;

        // Carriers are a bit more stable; modulators are more animated.
        float carrierScale = algo.isCarrier[i] ? 0.85f : 1.0f;

        // â”€â”€ Personality offsets: SPREAD + SCRAMBLE widen diversity â”€â”€â”€
        // At SCRAMBLE=0, identical to old behavior (narrow Â±10-20% offsets).
        // At SCRAMBLE=1, operators can differ by 10x or more in time values.
        uint32_t seed = voice.roleSeed ^ (uint32_t)(0xA511E9B3u * (uint32_t)(i + 1));
        float ra = u01(hash32(seed ^ 0x100u)) * 2.0f - 1.0f;
        float rd = u01(hash32(seed ^ 0x200u)) * 2.0f - 1.0f;
        float rr = u01(hash32(seed ^ 0x300u)) * 2.0f - 1.0f;
        float rs = u01(hash32(seed ^ 0x400u)) * 2.0f - 1.0f;

        // Effective diversity = spread baseline + scramble amplification
        float diversity = spread + scramble * 2.5f;

        float aMul = 1.0f + diversity * ra * 0.35f;
        float dMul = 1.0f + diversity * rd * 0.40f;
        float rMul = 1.0f + diversity * rr * 0.45f;
        float sAdd = (spread + scramble * 0.90f) * rs * 0.45f;

        // SCRAMBLE widens clamp ranges dramatically
        float aMin = 0.55f - scramble * 0.50f; // down to 0.05 at max
        float aMax = 1.75f + scramble * 12.0f;  // up to 13.75 at max
        float dMin = 0.60f - scramble * 0.52f;  // down to 0.08
        float dMax = 1.90f + scramble * 8.0f;   // up to 9.9
        float rMin = 0.50f - scramble * 0.45f;  // down to 0.05
        float rMax = 2.20f + scramble * 5.5f;   // up to 7.7

        a *= clampf(aMul, aMin, aMax);
        d *= clampf(dMul, dMin, dMax);
        r *= clampf(rMul, rMin, rMax);
        s = clamp01(s + sAdd);

        // â”€â”€ Envelope curve (SCRAMBLE-driven, per-operator) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // curve = -1..+1: negative = logarithmic/soft, positive = exponential/snappy
        float rc = u01(hash32(seed ^ 0x500u)) * 2.0f - 1.0f;
        e.curve = scramble * rc * 1.5f;

        // Edge tightens the envelope a bit (more percussive when Edge high)
        // Keep Swell less affected so pads remain possible.
        float swellness = clamp01((p - 2.8f) / 1.2f);
        float tight = 1.0f - edgeV * (0.55f - 0.20f * swellness);
        tight = clampf(tight, 0.30f, 1.0f);
        a *= tight;
        d *= tight;
        r *= tight;

        // Density pushes modulator sustain a touch (more evolving tones)
        if (!algo.isCarrier[i]) {
            s = clamp01(s + totalDensity * 0.10f);
        }

        // â”€â”€ Per-operator release independence (SCRAMBLE) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // At higher SCRAMBLE, each op's release diverges independently of TAIL.
        if (scramble > 0.01f) {
            r *= 1.0f + scramble * rr * 1.5f;
        }

        // Apply final
        // Loop rate CV only affects looped envelopes
        if (loop) {
            a /= loopRate;
            d /= loopRate;
        }

        e.attack  = std::max(0.0008f, a * carrierScale);
        e.decay   = std::max(0.0020f, d * carrierScale);
        e.sustain = s;
        e.release = std::max(0.0020f, r * carrierScale);
        e.loop    = loop;

        // â”€â”€ Carrier protection â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // The most stable carrier always gets a safe, predictable envelope
        // so the fundamental pitch anchor is never lost.
        if (i == protectedCarrier && scramble > 0.01f) {
            e.attack  = std::min(e.attack, 0.010f);
            e.sustain = std::max(e.sustain, 0.50f);
            e.release = std::max(e.release, 0.050f);
            e.curve   = 0.0f;
            voice.scrambleSecAmount[i] = 0.0f;
        }
    }

    // ── Chaos / Spike / Instability ──────────────────────────────────
    // DRIFT knob: 0..0.5 = instability ramps up (warm analog drift)
    //             0.5..1.0 = chaos also ramps in (organic FM sweet-spot exploration)
    // Spike: driven by the SPIKE knob. Mod matrix can still push all three.
    {
        float d = std::max(0.0f, std::min(1.0f, m.drift));
        // Instability: ramps 0→1.0 over the first half, stays at 1.0 for the second half
        float inst = std::min(1.0f, d * 2.0f);
        // Chaos: silent for the first half, ramps 0→1.0 over the second half
        float chaos = std::max(0.0f, (d - 0.5f) * 2.0f);
        voice.chaosAmount = chaos;
        voice.instability = inst;
    }
    {
        float sk = std::max(0.0f, std::min(1.0f, m.spike));
        // PUNCH: keep it clearly audible. (Old SPIKE was ~15..55ms @ intensity up to 2.)
        // Stronger intensity + slightly longer window makes the transient read as "attack punch".
        voice.spike.intensity = sk * 3.0f;  // knob 1.0 → intensity 3.0
        // Transient window: 12..48ms (still "attack" but not just a 1-sample click)
        voice.spike.duration  = 0.012f + sk * 0.036f;
    }

    // Motion â€” knob + Motion CV (was Instability CV jack)
    voice.motionAmount = std::max(0.0f, std::min(1.0f, m.motion + m.cvMotion));

    // â”€â”€ Master amp envelope timing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Edge still tightens, but bass-focused ENV STYLE must be able to do very short releases.
    // TAIL overrides this and can extend release to near-infinite.
    edgeV = std::max(0.0f, std::min(1.0f, m.edge));
    float style = std::max(0.0f, std::min(4.0f, m.envStyle));
    float bias  = 0.78f;  // hardcoded default (was m.roleBias)
    float tail  = std::max(0.0f, std::min(1.0f, m.tail));

    // Tightness: 1 at Impact/Stab, fades out toward Swell.
    float tight = 1.0f - std::max(0.0f, std::min(1.0f, (style - 1.8f) / 1.6f));
    // More transient-heavy bias => tighter.
    tight = std::max(0.0f, std::min(1.0f, tight + std::max(0.0f, -bias) * 0.35f));

    voice.ampEnv.attack  = 0.001f + (1.0f - edgeV) * 0.05f;
    voice.ampEnv.decay   = 0.05f  + (1.0f - edgeV) * 0.8f;
    voice.ampEnv.sustain = 0.3f   + (1.0f - edgeV) * 0.5f;

    // TAIL is the master release control:
    // - TAIL=0 => near-instant release (no ringing)
    // - TAIL=1 => very long / drone
    float tailT = std::max(0.0f, std::min(1.0f, tail));
    float tailCurve = tailT * tailT;
    const float minRel = 0.0015f;
    // minRel * 20000 ~= 30s at tailCurve=1, then clamp to 25s.
    float rel = minRel * powf(20000.0f, tailCurve);
    if (rel > 25.0f) rel = 25.0f;

    // Edge/tightness can only shorten the release (never lengthen at TAIL=0)
    float shorten = 1.0f - edgeV * (0.70f * tight);
    if (shorten < 0.25f) shorten = 0.25f;
    rel *= shorten;
    if (rel < minRel) rel = minRel;

    voice.ampEnv.release = rel;

    // â”€â”€ Operator self-feedback from Edge (+ SHUFFLE personality) â”€â”€â”€
    for (int i = 0; i < PhaseonVoice::kNumOps; ++i) {
        // Only modulators get feedback (carriers stay clean)
        if (!algo.isCarrier[i]) {
            uint32_t seed = voice.roleSeed ^ (uint32_t)(0xBADC0FFEu * (uint32_t)(i + 1));
            float r = u01(hash32(seed ^ 0x777u)) * 2.0f - 1.0f;
            float spreadAmt = clamp01(m.envSpread);

            // Base feedback is stronger than before and varies per-op with SHUFFLE.
            float base = 0.06f + edgeV * 0.58f;
            base *= (0.85f + 0.55f * spreadAmt);
            float var = 0.70f + 0.65f * std::fabs(r);
            voice.ops[i].feedback = clampf(base * var, 0.0f, 0.98f);
        } else {
            voice.ops[i].feedback = 0.0f;
        }
    }

    // ── Formant: FORM knob drives both vowel position and intensity ──
    // vowelPos = knob + CV (clamped 0..1) — this is the "mouth movement" axis.
    // formantAmount = same value — self-powers the gain curve in updateFormantCoeffs().
    // Mod matrix can push either independently after this point.
    {
        float fv = std::max(0.0f, std::min(1.0f, m.formant + m.cvFormant));
        voice.vowelPos      = fv;
        voice.formantAmount = fv;
    }

    // ── Modulation matrix (applied last, after all base values are set) ──
    if (!m.modMatrix.allOff()) {
        float sr = std::max(1.0f, sampleRate);
        float blockDt = 32.0f / sr; // control block = 32 samples (matches kControlRate in Phaseon.cpp)
        applyModMatrix(voice, m.modMatrix,
                       m.cvWtSelect, m.cvTimbre2, m.cvMotion,
                       m.cvSpectralTilt, m.cvHarmonicDensity, m.cvWtRegionBias,
                       m.cvEdge, m.cvEnvSpread, m.cvComplexity, m.cvMorph,
                       blockDt);
    }
}

// â”€â”€â”€ Spectral tilt (post-output simple filter) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// -1 = dark (LP), 0 = flat, +1 = bright (HP emphasis)
// Implemented as a one-pole filter applied to the voice output.
struct SpectralTilt {
    float tilt = 0.0f;  // -1..+1
    float prevL = 0.0f, prevR = 0.0f;

    void process(float& l, float& r) {
        if (tilt > 0.01f) {
            // High emphasis: subtract lowpass
            float coeff = 1.0f - tilt * 0.9f;
            float lpL = prevL + coeff * (l - prevL);
            float lpR = prevR + coeff * (r - prevR);
            l = l + (l - lpL) * tilt * 2.0f;
            r = r + (r - lpR) * tilt * 2.0f;
            prevL = lpL; prevR = lpR;
        } else if (tilt < -0.01f) {
            // Low emphasis: apply lowpass
            float coeff = 1.0f + tilt * 0.8f; // 0.2..1.0
            if (coeff < 0.05f) coeff = 0.05f;
            l = prevL + coeff * (l - prevL);
            r = prevR + coeff * (r - prevR);
            prevL = l; prevR = r;
        } else {
            prevL = l; prevR = r;
        }
    }

    void reset() { prevL = prevR = 0.0f; }
};

} // namespace phaseon

