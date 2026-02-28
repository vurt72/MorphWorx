/*
 * PhaseonOperator.cpp — Operator tick implementation
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#include "PhaseonOperator.hpp"
#include "PhaseonWavetable.hpp"
#include <cmath>

namespace phaseon {

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float xorshiftNoise01(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)(state & 0xFFFFu) / 65535.0f; // 0..1
}

static inline float phaseDistort(float p01, float amount) {
    // amount in [-1..1] roughly; maps to breakpoint movement.
    amount = clampf(amount, -1.0f, 1.0f);
    float b = 0.5f + amount * 0.45f;      // 0.05..0.95
    b = clampf(b, 0.05f, 0.95f);
    if (p01 < b) {
        return p01 * (0.5f / b);
    }
    return 0.5f + (p01 - b) * (0.5f / (1.0f - b));
}

// ── Serum-style warp modes (phase remapping before wavetable lookup) ──────
// All functions remap phase [0..1] → [0..1]; depth controls intensity.
// Pure ALU math, no state, no buffers — MetaModule-safe.

static inline float warpBendPlus(float p, float depth) {
    // Polynomial crossfade p → p² → p³ → p⁴ (compresses early harmonics, thins sound)
    float p2 = p * p;
    float p3 = p2 * p;
    float d4 = depth * 4.0f;
    if (d4 <= 1.0f) return p + (p2 - p) * d4;
    if (d4 <= 2.0f) return p2 + (p3 - p2) * (d4 - 1.0f);
    float p4 = p3 * p;
    if (d4 <= 3.0f) return p3 + (p4 - p3) * (d4 - 2.0f);
    return p4;
}

static inline float warpBendMinus(float p, float depth) {
    // Inverse of Bend+: fattens partials
    return 1.0f - warpBendPlus(1.0f - p, depth);
}

static inline float warpSync(float p, float depth) {
    // Hard-sync sweep: multiply phase by ratio, wrap back to [0..1]
    float ratio = 1.0f + depth * 7.0f; // 1× to 8×
    float r = p * ratio;
    r -= (float)(int)r;
    if (r < 0.0f) r += 1.0f;
    return r;
}

static inline float warpQuantize(float p, float depth) {
    // Bit-crush the phase: reduce resolution from 256 steps down to 3
    float steps = 256.0f - depth * 253.0f; // 256 → 3
    if (steps < 3.0f) steps = 3.0f;
    return ((float)(int)(p * steps)) / steps;
}

static inline float warpAsym(float p, float depth) {
    // Different curve per half-cycle: adds even harmonics to odd-harmonic waves
    // First half: compressed (quadratic ease-in); second half: expanded (ease-out)
    if (p < 0.5f) {
        float t = p * 2.0f; // 0..1
        float curved = t * t; // ease-in
        return (t + (curved - t) * depth) * 0.5f;
    }
    float t = (p - 0.5f) * 2.0f; // 0..1
    float curved = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
    return 0.5f + (t + (curved - t) * depth) * 0.5f;
}

void Operator::tick(float fundamentalHz, float modInput, const Wavetable* table, float sampleRate) {
    const float freq = fundamentalHz * ratio;
    const float phaseInc = freq / sampleRate;

    // Self-feedback (soft-limited to tame harsh runaway at high feedback + scramble)
    const float fbSignal = tanhf(feedback * prevOut);
    const float totalMod = modInput + fbSignal;

    float sumL = 0.0f, sumR = 0.0f;
    const int voices = std::max(1, std::min(unisonCount, kMaxUnison));

    for (int v = 0; v < voices; ++v) {
        // Unison detune: symmetric spread around center
        float detuneFactor = 1.0f;
        if (voices > 1) {
            float t = (float)v / (float)(voices - 1) - 0.5f; // -0.5 .. +0.5
            detuneFactor = 1.0f + t * unisonDetune;
        }

        // Advance phase
        phase[v] += phaseInc * detuneFactor;
        phase[v] = wrap01(phase[v]);

        // Stereo width without detune: per-voice phase offsets
        float phaseOffset = 0.0f;
        if (voices > 1) {
            float t = (float)v / (float)(voices - 1) - 0.5f; // -0.5 .. +0.5
            phaseOffset = t * unisonPhaseSpread;
        }

        float basePhase = wrap01(phase[v] + phaseOffset);

        // Base phase modulation (PM)
        float pmPhase = wrap01(basePhase + fmDepth * totalMod);

        // Phase warp (Serum-style warp modes, selected by warpMode)
        // 0 = Off, 1=Bend+, 2=Bend-, 3=Sync, 4=Quantize, 5=Asym, 6=Classic PD hybrid
        float lookupPhase = pmPhase;
        float pdMix = clampf(pdAmount, 0.0f, 1.0f);
        if (pdMix > 0.0001f && warpMode != 0) {
            float warped;
            switch (warpMode) {
            case 1: warped = warpBendPlus(pmPhase, pdMix);   break;
            case 2: warped = warpBendMinus(pmPhase, pdMix);  break;
            case 3: warped = warpSync(pmPhase, pdMix);       break;
            case 4: {
                // Quantize + asym combo: more brutal, stepped movement.
                float qMix = clampf(pdMix * 1.35f, 0.0f, 1.0f);
                float q = warpQuantize(pmPhase, qMix);
                float a = warpAsym(q, clampf(qMix * 0.85f, 0.0f, 1.0f));
                warped = a;
                break;
            }
            case 5: {
                // Asym with extra bend and internal feedback; highly expressive.
                float aMix = clampf(pdMix * 1.25f, 0.0f, 1.0f);
                float a = warpAsym(pmPhase, aMix);
                float b = warpBendPlus(a, clampf(aMix * 0.8f, 0.0f, 1.0f));
                // Subtle self-FM on the phase for extra squelch.
                float fb = clampf(fmDepth * totalMod * 0.20f, -0.35f, 0.35f);
                warped = wrap01(b + fb);
                break;
            }
            case 6:
            default: {
                // Classic PD-inspired hybrid: aggressive but still continuous.
                float pd = fmDepth * totalMod;
                // Stronger drive and asymmetric bias for more aggressive motion.
                float bias = clampf(totalMod * 0.35f, -0.65f, 0.65f);
                pd = clampf(pd * 1.20f + bias, -1.2f, 1.2f);
                float pdPhase = phaseDistort(basePhase, pd);
                // Blend toward a warped+sync hybrid for extra grit.
                float pdWarp = wrap01(pmPhase + (pdPhase - pmPhase) * pdMix);
                float syncWarp = warpSync(pmPhase, clampf(pdMix * 0.65f, 0.0f, 1.0f));
                float t = clampf(pdMix * 0.8f, 0.0f, 1.0f);
                warped = wrap01(pdWarp * (1.0f - t) + syncWarp * t);
                break;
            }
            }
            lookupPhase = warped;
        }

        // Noise domain remains available (not used by pad-motion mapping)
        if (domain == OpDomain::Noise_PM) {
            float n = xorshiftNoise01(rngState) * 2.0f - 1.0f; // -1..+1
            float modSig = n + totalMod * 0.20f;
            lookupPhase = wrap01(basePhase + fmDepth * modSig);
        }

        // Sample wavetable (or procedural fallback for negative tableIndex)
        float sample;
        if (table && table->frameCount > 0 && table->frameSize > 0) {
            int mip = table->pickMipLevel(freq, sampleRate, bandlimitBias);
            sample = table->sampleMip(lookupPhase, framePos, mip);
        } else {
            // Procedural basic shapes (selected via negative sentinel indices)
            // -1: Sine
            // -2: Triangle
            // -3: Saw
            // -4: Harmonic sine stack
            // -5: Skewed sine (phase-warped)
            // -6: Square (softened)
            // -7: Warp sine (fixed bend+ warp)
            // -8: Rectified sine
            float p = lookupPhase;
            switch (tableIndex) {
            default:
            case -1: {
                sample = sinf(p * 6.283185307f);
                break;
            }
            case -2: {
                // Triangle in [-1..1]
                float u = 2.0f * p - 1.0f;
                sample = 1.0f - 2.0f * fabsf(u);
                break;
            }
            case -3: {
                // Saw in [-1..1]
                sample = 2.0f * p - 1.0f;
                break;
            }
            case -4: {
                // Controlled harmonic stack (odd+even, quickly rolled off)
                float w = p * 6.283185307f;
                float s = 0.0f;
                s += 1.00f * sinf(w * 1.0f);
                s += 0.35f * sinf(w * 2.0f);
                s += 0.22f * sinf(w * 3.0f);
                s += 0.14f * sinf(w * 4.0f);
                s += 0.10f * sinf(w * 5.0f);
                sample = tanhf(s * 0.90f);
                break;
            }
            case -5: {
                // Skewed sine: asymmetric phase distortion on a sine core.
                float warped = warpAsym(p, 1.0f);
                sample = sinf(warped * 6.283185307f);
                break;
            }
            case -6: {
                // Soft square: avoid brutal discontinuities but still aggressive.
                float w = p * 6.283185307f;
                sample = tanhf(sinf(w) * 4.0f);
                break;
            }
            case -7: {
                // Warp sine: fixed bend+ warp for a distinct, vocal-ish waveform.
                float warped = warpBendPlus(p, 0.70f);
                sample = sinf(warped * 6.283185307f);
                break;
            }
            case -8: {
                // Rectified sine in [-1..1]
                float w = p * 6.283185307f;
                float s = sinf(w);
                sample = fabsf(s) * 2.0f - 1.0f;
                break;
            }
            }
        }

        // Gentle waveshaping blend (pad-friendly; avoids “more noise”)
        float wsm = clampf(wsMix, 0.0f, 1.0f);
        if (wsm > 0.0001f || domain == OpDomain::WT_WS) {
            float drive = std::max(1.0f, wsDrive);            // Soft pre-saturation at extreme drive levels — tames harsh fold/wrap artifacts
            if (drive > 3.0f) {
                float excess = drive - 3.0f;
                float blend = std::min(1.0f, excess * 0.20f);
                float softDrive = 3.0f + tanhf(excess * 0.5f) * 2.0f;
                drive = drive + blend * (softDrive - drive);
            }            // If someone explicitly set WT_WS domain, allow modulation to push drive.
            if (domain == OpDomain::WT_WS) {
                drive = std::max(drive, 1.0f + std::min(5.0f, std::fabs(fmDepth * totalMod) * 2.0f));
                wsm = std::max(wsm, 0.65f);
            }
            float asym = clampf(totalMod * fmDepth * 0.08f, -0.25f, 0.25f);
            float wet = tanhf((sample + asym) * drive);
            sample = sample + (wet - sample) * wsm;
        }

        // Unison stereo spread
        if (voices > 1) {
            float pan = 0.5f;
            if (voices > 1) {
                float t = (float)v / (float)(voices - 1); // 0..1
                pan = 0.5f + (t - 0.5f) * unisonStereo;   // spread around center
            }
            sumL += sample * (1.0f - pan);
            sumR += sample * pan;
        } else {
            sumL += sample * 0.5f;
            sumR += sample * 0.5f;
        }
    }

    // Normalize by voice count
    float invN = 1.0f / (float)voices;
    outputL = sumL * invN * level;
    outputR = sumR * invN * level;
    output  = (outputL + outputR);  // mono sum
    prevOut = output;
}

} // namespace phaseon
