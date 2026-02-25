/*
 * PhaseonModMatrix.hpp — Lightweight modulation matrix for Phaseon
 *
 * 6 slots, each routing a source signal to a parameter destination
 * with bipolar depth and optional slew smoothing.  Evaluated at
 * control rate inside applyMacros(); zero per-sample overhead.
 *
 * Sources:  Velocity, Keytrack, AmpEnv, LFO1/2, RandomPerNote,
 *           and any existing CV performance input.
 * Dests:    Global voice params + per-operator fields with targeting.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#pragma once
#include "PhaseonVoice.hpp"
#include "PhaseonAlgorithm.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace phaseon {

// ─── Constants ──────────────────────────────────────────────────────
// kModSlots and PhaseonModRuntime are defined in PhaseonVoice.hpp.

// ─── Modulation source ──────────────────────────────────────────────
enum class ModSource : uint8_t {
    Off = 0,
    Velocity,        // 0..1  — note-on velocity (cached per note)
    Keytrack,        // 0..1  — fundamentalHz mapped C0..C8
    AmpEnv,          // 0..1  — master amp envelope level
    Lfo1,            // -1..+1 — motion LFO 1
    Lfo2,            // -1..+1 — motion LFO 2
    RandomPerNote,   // -1..+1 — deterministic random, re-seeds on noteOn
    CvWtSelect,      // 0..1  — WT Select CV (normalized; drives wtFamily offset in process)
    CvTimbre,        // 0..1  — Timbre CV perf section (already baked into timbre; use as mod source)
    CvMotion,        // 0..1  — Motion CV (was Instability CV)
    CvTilt,          // -1..+1 — cvSpectralTilt
    CvDensity,       // -1..+1 — cvHarmonicDensity
    CvWtBias,        // -1..+1 — cvWtRegionBias
    CvEdge,          // 0..1  — Edge CV
    CvEnvSpread,     // 0..1  — Env Spread CV
    CvComplexity,    // 0..1  — Complexity CV
    CvMorph,         // -1..+1 — Morph CV (algorithm morph)
    kCount
};

// ─── Modulation destination ─────────────────────────────────────────
enum class ModDest : uint8_t {
    Off = 0,
    // ── Global params
    AlgorithmMorph,  // 0..1
    NetworkAmount,   // 0..1
    EdgeAmount,      // 0..1
    TimbreAmount,    // 0..1
    TiltAmount,      // -1..+1
    MotionAmount,    // 0..1
    ComplexAmount,   // 0..1
    ChaosAmount,     // 0..1
    SpikeIntensity,  // 0..1
    Instability,     // 0..1
    MasterLevel,     // 0..1
    // ── Per-operator params (ModOpTarget selects which ops are hit)
    FramePos,        // 0..1
    FmDepth,         // 0..9
    Feedback,        // 0..0.98
    PdAmount,        // 0..1
    WsMix,           // 0..1
    WsDrive,         // 1..8
    FormantAmount,   // 0..1
    VowelPos,        // 0..1
    kCount
};

// ─── Per-op targeting ───────────────────────────────────────────────
enum class ModOpTarget : uint8_t {
    All = 0,
    CarriersOnly,
    ModulatorsOnly,
    Op1, Op2, Op3, Op4, Op5, Op6,   // 1-indexed names (user-friendly)
};

// ─── Single slot config ─────────────────────────────────────────────
struct ModSlot {
    ModSource   src    = ModSource::Off;
    ModDest     dst    = ModDest::Off;
    ModOpTarget target = ModOpTarget::All;
    float       amount = 0.0f;   // bipolar -1..+1
    float       slewMs = 5.0f;   // 0 = instant, >0 = 1-pole smoothing
};

// ─── Full matrix config (preset data) ───────────────────────────────
struct PhaseonModConfig {
    ModSlot slots[kModSlots];

    bool allOff() const {
        for (int i = 0; i < kModSlots; ++i)
            if (slots[i].src != ModSource::Off && slots[i].dst != ModDest::Off)
                return false;
        return true;
    }
};

// ─── Human-readable names (for UI / context menu) ───────────────────
inline const char* modSourceName(ModSource s) {
    switch (s) {
    case ModSource::Off:           return "Off";
    case ModSource::Velocity:      return "Velocity";
    case ModSource::Keytrack:      return "Keytrack";
    case ModSource::AmpEnv:        return "AmpEnv";
    case ModSource::Lfo1:          return "LFO 1";
    case ModSource::Lfo2:          return "LFO 2";
    case ModSource::RandomPerNote: return "Rand/Note";
    case ModSource::CvWtSelect:    return "CV: WtSelect";
    case ModSource::CvTimbre:      return "CV: Timbre";
    case ModSource::CvMotion:      return "CV: Motion";
    case ModSource::CvTilt:        return "CV: Tilt";
    case ModSource::CvDensity:     return "CV: Density";
    case ModSource::CvWtBias:      return "CV: WtBias";
    case ModSource::CvEdge:        return "CV: Edge";
    case ModSource::CvEnvSpread:   return "CV: EnvSpread";
    case ModSource::CvComplexity:  return "CV: Complexity";
    case ModSource::CvMorph:       return "CV: Morph";
    default:                       return "?";
    }
}

inline const char* modDestName(ModDest d) {
    switch (d) {
    case ModDest::Off:            return "Off";
    case ModDest::AlgorithmMorph: return "AlgoMorph";
    case ModDest::NetworkAmount:  return "Network";
    case ModDest::EdgeAmount:     return "Edge";
    case ModDest::TimbreAmount:   return "Timbre";
    case ModDest::TiltAmount:     return "Tilt";
    case ModDest::MotionAmount:   return "Motion";
    case ModDest::ComplexAmount:  return "Complex";
    case ModDest::ChaosAmount:    return "Chaos";
    case ModDest::SpikeIntensity: return "Spike";
    case ModDest::Instability:    return "Instab";
    case ModDest::MasterLevel:    return "Master";
    case ModDest::FramePos:       return "FramePos";
    case ModDest::FmDepth:        return "FmDepth";
    case ModDest::Feedback:       return "Feedback";
    case ModDest::PdAmount:       return "PD Amt";
    case ModDest::WsMix:          return "WS Mix";
    case ModDest::WsDrive:        return "WS Drive";
    case ModDest::FormantAmount:  return "Formant";
    case ModDest::VowelPos:       return "VowelPos";
    default:                      return "?";
    }
}

inline const char* modOpTargetName(ModOpTarget t) {
    switch (t) {
    case ModOpTarget::All:           return "All Ops";
    case ModOpTarget::CarriersOnly:  return "Carriers";
    case ModOpTarget::ModulatorsOnly:return "Mods";
    case ModOpTarget::Op1: return "Op 1";
    case ModOpTarget::Op2: return "Op 2";
    case ModOpTarget::Op3: return "Op 3";
    case ModOpTarget::Op4: return "Op 4";
    case ModOpTarget::Op5: return "Op 5";
    case ModOpTarget::Op6: return "Op 6";
    default: return "?";
    }
}

// ─── Source computation ─────────────────────────────────────────────
// Returns raw signal in source-native range.
// Unipolar sources:  0..1  (Velocity, Keytrack, AmpEnv, CvChaos, ...)
// Bipolar sources:  -1..+1 (Lfo1, Lfo2, RandomPerNote, CvTilt, ...)
// The final delta = rawSignal * slot.amount, so amount is always
// "max deflection from nominal."
inline float computeModSource(ModSource src,
                               const PhaseonVoice& voice,
                               float cvWtSelect, float cvTimbre,
                               float cvMotion, float cvTilt,
                               float cvDensity, float cvWtBias,
                               float cvEdge, float cvEnvSpread,
                               float cvComplexity, float cvMorph)
{
    switch (src) {
    case ModSource::Velocity:
        return std::max(0.0f, std::min(1.0f, voice.cachedVelocity));
    case ModSource::Keytrack:
        return std::max(0.0f, std::min(1.0f, voice.cachedKeytrack));
    case ModSource::AmpEnv:
        return std::max(0.0f, std::min(1.0f, voice.ampEnv.level));
    case ModSource::Lfo1:
        return sinf(voice.lfo1Phase * 6.283185307f);
    case ModSource::Lfo2:
        return sinf(voice.lfo2Phase * 6.283185307f + 1.7f);
    case ModSource::RandomPerNote:
        return std::max(-1.0f, std::min(1.0f, voice.cachedRandPerNote));
    case ModSource::CvWtSelect:
        return std::max(0.0f, std::min(1.0f, cvWtSelect));
    case ModSource::CvTimbre:
        return std::max(0.0f, std::min(1.0f, cvTimbre));
    case ModSource::CvMotion:
        return std::max(0.0f, std::min(1.0f, cvMotion));
    case ModSource::CvTilt:
        return std::max(-1.0f, std::min(1.0f, cvTilt));
    case ModSource::CvDensity:
        return std::max(-1.0f, std::min(1.0f, cvDensity));
    case ModSource::CvWtBias:
        return std::max(-1.0f, std::min(1.0f, cvWtBias));
    case ModSource::CvEdge:
        return std::max(0.0f, std::min(1.0f, cvEdge));
    case ModSource::CvEnvSpread:
        return std::max(0.0f, std::min(1.0f, cvEnvSpread));
    case ModSource::CvComplexity:
        return std::max(0.0f, std::min(1.0f, cvComplexity));
    case ModSource::CvMorph:
        return std::max(-1.0f, std::min(1.0f, cvMorph));
    case ModSource::Off:
    default:
        return 0.0f;
    }
}

// ─── Per-op targeting predicate ─────────────────────────────────────
inline bool opTargetMatches(ModOpTarget target, int opIdx, bool isCarrier) {
    switch (target) {
    case ModOpTarget::All:            return true;
    case ModOpTarget::CarriersOnly:   return isCarrier;
    case ModOpTarget::ModulatorsOnly: return !isCarrier;
    case ModOpTarget::Op1: return opIdx == 0;
    case ModOpTarget::Op2: return opIdx == 1;
    case ModOpTarget::Op3: return opIdx == 2;
    case ModOpTarget::Op4: return opIdx == 3;
    case ModOpTarget::Op5: return opIdx == 4;
    case ModOpTarget::Op6: return opIdx == 5;
    default:               return true;
    }
}

// ─── Destination: is it per-op? ─────────────────────────────────────
inline bool isPerOpDest(ModDest d) {
    return d == ModDest::FramePos  || d == ModDest::FmDepth  ||
           d == ModDest::Feedback  || d == ModDest::PdAmount ||
           d == ModDest::WsMix     || d == ModDest::WsDrive;
}

// ─── Main apply function ─────────────────────────────────────────────
// Call at end of applyMacros(), after all base parameter values have
// been written to the voice.  blockDt = kControlRate / sampleRate.
inline void applyModMatrix(PhaseonVoice& voice,
                           const PhaseonModConfig& cfg,
                           float cvWtSelect, float cvTimbre,
                           float cvMotion, float cvTilt,
                           float cvDensity, float cvWtBias,
                           float cvEdge, float cvEnvSpread,
                           float cvComplexity, float cvMorph,
                           float blockDt)
{
    // Fast-path: skip entirely if all slots are inactive
    if (cfg.allOff()) return;

    const Algorithm& algo = getAlgorithm(voice.algorithmIndex);

    // ── Per-destination accumulators ────────────────────────────────
    // Global (indexed by ModDest enum value, minus the Off case)
    float globalDelta[(int)ModDest::kCount] = {};
    float perOpDelta[PhaseonVoice::kNumOps][(int)ModDest::kCount] = {};

    // ── Process each slot ───────────────────────────────────────────
    for (int s = 0; s < kModSlots; ++s) {
        const ModSlot& slot = cfg.slots[s];
        if (slot.src == ModSource::Off || slot.dst == ModDest::Off) {
            voice.modRuntime.smoothed[s] = 0.0f;
            continue;
        }

        // Compute raw source value and target delta
        float raw = computeModSource(slot.src, voice,
                                     cvWtSelect, cvTimbre, cvMotion,
                                     cvTilt, cvDensity, cvWtBias,
                                     cvEdge, cvEnvSpread, cvComplexity, cvMorph);
        float targetVal = raw * slot.amount;

        // 1-pole slew smoothing (control-rate)
        float& sm = voice.modRuntime.smoothed[s];
        if (slot.slewMs <= 0.1f) {
            sm = targetVal;
        } else {
            float tau = slot.slewMs * 0.001f;
            float alpha = 1.0f - expf(-blockDt / tau);
            sm += (targetVal - sm) * alpha;
        }

        float delta = sm;
        if (delta == 0.0f) continue;

        int dstIdx = (int)slot.dst;

        if (isPerOpDest(slot.dst)) {
            // Accumulate into per-op table
            for (int op = 0; op < PhaseonVoice::kNumOps; ++op) {
                if (opTargetMatches(slot.target, op, (bool)algo.isCarrier[op])) {
                    perOpDelta[op][dstIdx] += delta;
                }
            }
        } else {
            globalDelta[dstIdx] += delta;
        }
    }

    // ── Apply global destinations ────────────────────────────────────
    auto applyGlobal = [&](ModDest d, float& field, float lo, float hi) {
        int idx = (int)d;
        if (globalDelta[idx] != 0.0f) {
            float v = field + globalDelta[idx];
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            field = v;
        }
    };

    applyGlobal(ModDest::AlgorithmMorph, voice.algorithmMorph, 0.0f, 1.0f);
    applyGlobal(ModDest::NetworkAmount,  voice.networkAmount,  0.0f, 1.0f);
    applyGlobal(ModDest::EdgeAmount,     voice.edgeAmount,     0.0f, 1.0f);
    applyGlobal(ModDest::TimbreAmount,   voice.timbreAmount,   0.0f, 1.0f);
    applyGlobal(ModDest::TiltAmount,     voice.tiltAmount,    -1.0f, 1.0f);
    applyGlobal(ModDest::MotionAmount,   voice.motionAmount,   0.0f, 1.0f);
    applyGlobal(ModDest::ComplexAmount,  voice.complexAmount,  0.0f, 1.0f);
    applyGlobal(ModDest::ChaosAmount,    voice.chaosAmount,    0.0f, 1.0f);
    applyGlobal(ModDest::SpikeIntensity, voice.spike.intensity,0.0f, 1.0f);
    applyGlobal(ModDest::Instability,    voice.instability,    0.0f, 1.0f);
    applyGlobal(ModDest::MasterLevel,    voice.masterLevel,    0.0f, 1.0f);
    applyGlobal(ModDest::FormantAmount,  voice.formantAmount,  0.0f, 1.0f);
    applyGlobal(ModDest::VowelPos,       voice.vowelPos,       0.0f, 1.0f);

    // ── Apply per-operator destinations ─────────────────────────────
    for (int op = 0; op < PhaseonVoice::kNumOps; ++op) {
        auto applyOp = [&](ModDest d, float& field, float lo, float hi) {
            int idx = (int)d;
            if (perOpDelta[op][idx] != 0.0f) {
                float v = field + perOpDelta[op][idx];
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                field = v;
            }
        };

        applyOp(ModDest::FramePos,  voice.ops[op].framePos,  0.0f, 1.0f);
        applyOp(ModDest::FmDepth,   voice.ops[op].fmDepth,   0.0f, 9.0f);
        applyOp(ModDest::Feedback,  voice.ops[op].feedback,  0.0f, 0.98f);
        applyOp(ModDest::PdAmount,  voice.ops[op].pdAmount,  0.0f, 1.0f);
        applyOp(ModDest::WsMix,     voice.ops[op].wsMix,     0.0f, 1.0f);
        applyOp(ModDest::WsDrive,   voice.ops[op].wsDrive,   1.0f, 8.0f);
    }
}

} // namespace phaseon
