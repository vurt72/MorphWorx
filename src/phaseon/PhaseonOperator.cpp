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

static inline float softSatRational(float x) {
    float a = std::fabs(x);
    return x * (1.0f + 0.15f * a) / (1.0f + a + 0.15f * x * x);
}

static inline float safeSin01(float p01) {
    if (!std::isfinite(p01)) return 0.0f;
    p01 -= std::floor(p01);
    if (p01 < 0.0f) p01 += 1.0f;
#ifdef METAMODULE
    return phaseon::phaseon_fast_sin_01(p01);
#else
    return (float)std::sin((double)(p01 * 6.283185307f));
#endif
}

static inline float fastExp2Approx(float x) {
    int i = (int)std::floor(x);
    float f = x - (float)i;
    float poly = 1.0f + f * (0.69314718f + f * (0.24022651f + f * 0.05550411f));
    return std::ldexp(poly, i);
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

static inline float applyPhaseWarp(float phase, float warpAmt) {
    if (std::fabs(warpAmt) < 0.001f) return phase;
    // Clamp extended to ±1.99 so incoming values up to 2.0 are fully usable.
    // Phaseon1 maps its [0,1] WARP knob to [0,2] before calling here,
    // so knob@0.5 ≈ former max (exp2f(4)≈16), knob@1.0 = extreme (exp2f(7.96)≈246).
    float w = clampf(warpAmt, -1.99f, 1.99f);
    float curve = fastExp2Approx(w * 4.0f);
    float denom = phase + curve * (1.0f - phase);
    if (denom <= 1e-9f) return phase;
    float warpedPhase = phase / denom;
    return clampf(warpedPhase, 0.0f, 1.0f);
}

void Operator::tick(float fundamentalHz, float modInput, const Wavetable* table, float sampleRate, float currentEnv) {
    // Use precomputed phase increment (set at control rate via cachedPhaseInc)
    const float phaseInc = cachedPhaseInc;
    float sr = std::max(1000.0f, sampleRate);
    if (sr != cachedSampleRate) {
        cachedSampleRate = sr;
        const float cutoffHz = 2000.0f;
        const float rc = 1.0f / (6.283185307f * cutoffHz);
        const float dt = 1.0f / sr;
        cachedFbAlpha = dt / (rc + dt);
        cachedPmSlewCoef = dt / (0.002f + dt);
    }

    // Safety: recover from invalid numeric state instead of latching silence.
    if (!std::isfinite(phaseInc) || !std::isfinite(modInput) || !std::isfinite(prevOut)) {
        reset();
        output = outputL = outputR = 0.0f;
        return;
    }

    const bool phaseWarpEnabled = std::fabs(phaseWarp) >= 0.001f;
    if (phaseWarpEnabled) {
        float clampedWarp = clampf(phaseWarp, -1.99f, 1.99f);
        if (clampedWarp != cachedPhaseWarpInput) {
            cachedPhaseWarpInput = clampedWarp;
            cachedPhaseWarpCurve = fastExp2Approx(clampedWarp * 4.0f);
        }
    }
    auto applyPhaseWarpCached = [&](float phase) -> float {
        if (!phaseWarpEnabled) return phase;
        float denom = phase + cachedPhaseWarpCurve * (1.0f - phase);
        if (denom <= 1e-9f) return phase;
        float warpedPhase = phase / denom;
        return clampf(warpedPhase, 0.0f, 1.0f);
    };

#ifdef METAMODULE
    // ═══ MetaModule fast path: single voice, no unison ═══════════════
    float env = currentEnv;
    if (!std::isfinite(env)) env = 0.0f;
    env = clampf(env, 0.0f, 2.0f);

    // Feedback PM core: DC block -> damped LP -> cubic depth with env tracking.
    float feedbackPm = 0.0f;
    if (feedback > 0.0005f) {
        float fbNorm = clampf(feedback, 0.0f, 1.0f);
        float fbRaw = prevOut;

        float fbClean = fbRaw - fbDcBlockerPrev + 0.995f * fbDcBlockerState;
        fbDcBlockerState = fbClean;
        fbDcBlockerPrev = fbRaw;

        filteredFeedback += cachedFbAlpha * (fbClean - filteredFeedback);

        // feedbackWarmth: blend linear feedback → tanh-saturated feedback.
        // 0 = clean/glassy, 1 = warm/harmonically rich. Fully backward compat at default 0.
        if (feedbackWarmth > 0.001f) {
            float w = clampf(feedbackWarmth, 0.0f, 1.0f);
            float sat = phaseon_fast_tanh(filteredFeedback * (1.0f + w * 3.0f));
            filteredFeedback = filteredFeedback + (sat - filteredFeedback) * w;
        }

        float maxFbIndex = 1.5f;
        float targetFbDepth = (fbNorm * fbNorm * fbNorm) * maxFbIndex;
        feedbackPm = filteredFeedback * targetFbDepth;
    }

    float totalMod = clampf(modInput, -4.0f, 4.0f);
    float feedbackPhase = clampf(feedbackPm, -1.0f, 1.0f);

    // PM engine update: DC-blocking + cubic depth scaling + asymmetric slew.
    float modClean = totalMod - modDcBlockerPrev + 0.995f * modDcBlockerState;
    modDcBlockerState = modClean;
    modDcBlockerPrev = totalMod;
    modClean = clampf(modClean, -1.0f, 1.0f);

    float sign = (fmDepth < 0.0f) ? -1.0f : 1.0f;
    float absDepth = std::fabs(fmDepth);
    float maxPmIndex = 4.0f;
    float targetPmDepth = sign * (absDepth * absDepth * absDepth) * maxPmIndex * env;

    currentPmDepth += cachedPmSlewCoef * (targetPmDepth - currentPmDepth);

    // Advance phase
    if (!std::isfinite(phase[0])) {
        phase[0] = 0.0f;
    }
    phase[0] += phaseInc;
    phase[0] -= (float)(int)phase[0]; // wrap01 inline
    if (phase[0] < 0.0f) phase[0] += 1.0f;

    // Phase modulation
    float lookupPhase = phase[0] + (modClean * currentPmDepth) + feedbackPhase;
    lookupPhase -= (float)(int)lookupPhase;
    if (lookupPhase < 0.0f) lookupPhase += 1.0f;
    lookupPhase = applyPhaseWarpCached(lookupPhase);
    if (tear > 0.0001f) {
        lookupPhase = warpAsym(lookupPhase, clampf(tear, 0.0f, 1.0f));
    }

    // PD warp modes — pure ALU, no state, zero cost when pdAmount==0.
    {
        float pdMix = clampf(pdAmount, 0.0f, 1.0f);
        if (pdMix > 0.0001f && warpMode != 0) {
            float warped;
            switch (warpMode) {
            case 1: warped = warpBendPlus(lookupPhase, pdMix);  break;
            case 2: warped = warpBendMinus(lookupPhase, pdMix); break;
            case 3: warped = warpSync(lookupPhase, pdMix);      break;
            case 4: {
                float qMix = clampf(pdMix * 1.35f, 0.0f, 1.0f);
                float q = warpQuantize(lookupPhase, qMix);
                warped = warpAsym(q, clampf(qMix * 0.85f, 0.0f, 1.0f));
                break;
            }
            case 5: {
                // Asym with extra bend and internal feedback; matches Rack path.
                float aMix = clampf(pdMix * 1.25f, 0.0f, 1.0f);
                float a = warpAsym(lookupPhase, aMix);
                float b = warpBendPlus(a, clampf(aMix * 0.8f, 0.0f, 1.0f));
                // Subtle self-FM on the phase for extra squelch (matches Rack path).
                float fb = clampf(fmDepth * totalMod * 0.20f, -0.35f, 0.35f);
                warped = wrap01(b + fb);
                break;
            }
            case 6:
            default: {
                float pd = fmDepth * totalMod;
                float bias = clampf(totalMod * 0.35f, -0.65f, 0.65f);
                pd = clampf(pd * 1.20f + bias, -1.2f, 1.2f);
                float pdPhase = phaseDistort(phase[0], pd);
                float pdWarp = wrap01(lookupPhase + (pdPhase - lookupPhase) * pdMix);
                float syncWarp = warpSync(lookupPhase, clampf(pdMix * 0.65f, 0.0f, 1.0f));
                float t = clampf(pdMix * 0.8f, 0.0f, 1.0f);
                warped = wrap01(pdWarp * (1.0f - t) + syncWarp * t);
                break;
            }
            }
            lookupPhase = warped;
        }
    }

    // Noise domain
    if (domain == OpDomain::Noise_PM) {
        float n = xorshiftNoise01(rngState) * 2.0f - 1.0f;
        lookupPhase = phase[0] + fmDepth * (n + totalMod * 0.20f) + feedbackPhase;
        lookupPhase -= (float)(int)lookupPhase;
        if (lookupPhase < 0.0f) lookupPhase += 1.0f;
        lookupPhase = applyPhaseWarpCached(lookupPhase);
        if (tear > 0.0001f) {
            lookupPhase = warpAsym(lookupPhase, clampf(tear, 0.0f, 1.0f));
        }
    }

    // Sample wavetable
    float sample;
    if (table && table->frameCount > 0 && table->frameSize > 0) {
        int mip = mipLevel;
        if (mip < 0) mip = 0;
        if (mip >= table->mipCount()) mip = table->mipCount() - 1;
        sample = table->sampleMipFast(lookupPhase, framePos, mip);
    } else {
        float p = lookupPhase;
        switch (tableIndex) {
        default:
        case -1: sample = safeSin01(p); break;
        case -2: {
            // FAT SUB-TRIANGLE
            float tri = 2.0f * std::fabs(2.0f * p - 1.0f) - 1.0f;
            sample = phaseon::phaseon_fast_tanh(tri * 1.5f);
            break;
        }
        case -3: {
            // SHARKTOOTH (PM-ready saw)
            float s = safeSin01(p);
            sample = safeSin01(wrap01(p + s * 0.08f));
            break;
        }
        case -4: {
            float s = safeSin01(p);
            s += 0.35f * safeSin01(p * 2.0f);
            s += 0.22f * safeSin01(p * 3.0f);
            s += 0.14f * safeSin01(p * 4.0f);
            s += 0.10f * safeSin01(p * 5.0f);
            sample = phaseon::phaseon_fast_tanh(s * 0.90f);
            break;
        }
        case -5: {
            // ASYMMETRIC PULSE-SINE
            float skew = 0.8f;
            if (p < skew) {
                float t = p / skew;
                sample = phaseon::phaseon_fast_sin_01(t * 0.5f);
            } else {
                float t = (p - skew) / (1.0f - skew);
                sample = -phaseon::phaseon_fast_sin_01(t * 0.5f);
            }
            break;
        }
        case -6: sample = std::tanh(safeSin01(p) * 4.0f); break;
        case -7: {
            // FOLDED SINE
            float driven = safeSin01(p) * 2.2f;
            sample = phaseon::phaseon_fast_sin_w0(driven * 1.5f);
            break;
        }
        case -8: {
            // OCTAVE SUB-GROWL
            float sub = safeSin01(p);
            float octaveUp = std::fabs(sub) * 2.0f - 1.0f;
            sample = sub * 0.6f + octaveUp * 0.4f;
            break;
        }
        }
    }

    // Waveshaping (same guard as Rack path: zero cost when wsMix==0).
    {
        float wsm = clampf(wsMix, 0.0f, 1.0f);
        if (wsm > 0.0001f || domain == OpDomain::WT_WS) {
            float drive = std::max(1.0f, wsDrive);
            if (drive > 3.0f) {
                float excess = drive - 3.0f;
                float blend = std::min(1.0f, excess * 0.20f);
                float softDrive = 3.0f + phaseon_fast_tanh(excess * 0.5f) * 2.0f;
                drive = drive + blend * (softDrive - drive);
            }
            if (domain == OpDomain::WT_WS) {
                drive = std::max(drive, 1.0f + std::min(5.0f, std::fabs(fmDepth * totalMod) * 2.0f));
                wsm = std::max(wsm, 0.65f);
            }
            float asym = clampf(totalMod * fmDepth * 0.08f, -0.25f, 0.25f);
            float wet = phaseon_fast_tanh((sample + asym) * drive);
            sample = sample + (wet - sample) * wsm;
        }
    }

    // Output — with optional Braids-style inline second voice.
    // When unisonPhaseSpread > 0 (MOTION > 0): advance a second detuned phase,
    // do a second lightweight lookup, pan the two voices L/R.
    // Zero extra cost when MOTION = 0.
    if (unisonPhaseSpread > 0.0001f) {
        // Advance detuned second phase (no extra state machines — purely additive).
        float phaseInc2 = phaseInc * (1.0f + unisonDetune);
        phase[1] += phaseInc2;
        phase[1] -= (float)(int)phase[1];
        if (phase[1] < 0.0f) phase[1] += 1.0f;

        // Second lookup phase: same PM modulation, new base phase.
        float lp2 = phase[1] + (modClean * currentPmDepth) + feedbackPhase;
        lp2 -= (float)(int)lp2;
        if (lp2 < 0.0f) lp2 += 1.0f;
        lp2 = applyPhaseWarpCached(lp2);

        float sample2;
        if (table && table->frameCount > 0 && table->frameSize > 0) {
            int mip = mipLevel < 0 ? 0 : mipLevel;
            if (mip >= table->mipCount()) mip = table->mipCount() - 1;
            sample2 = table->sampleMipFast(lp2, framePos, mip);
        } else {
            sample2 = safeSin01(lp2);
        }

        // Stereo pan: voice1 → left-biased, voice2 → right-biased.
        float sp = clampf(unisonPhaseSpread * 6.0f, 0.0f, 1.0f);
        float cross = sp * 0.38f;
        outputL = (sample  * (1.0f - cross) + sample2 * cross)       * 0.5f * level;
        outputR = (sample  * cross          + sample2 * (1.0f - cross)) * 0.5f * level;
        output  = (sample + sample2) * 0.5f * level;
    } else {
        outputL = sample * 0.5f * level;
        outputR = outputL;
        output  = sample * level;
    }
    if (!std::isfinite(output) || !std::isfinite(outputL) || !std::isfinite(outputR)) {
        reset();
        output = outputL = outputR = 0.0f;
        return;
    }
    prevOut = output;

#else
    // ═══ VCV Rack path: full unison support ═══════════════════════════
    float env = currentEnv;
    if (!std::isfinite(env)) env = 0.0f;
    env = clampf(env, 0.0f, 2.0f);

    // Feedback PM core: DC block -> damped LP -> cubic depth with env tracking.
    float feedbackPm = 0.0f;
    if (feedback > 0.0005f) {
        float fbNorm = clampf(feedback, 0.0f, 1.0f);
        float fbRaw = prevOut;

        float fbClean = fbRaw - fbDcBlockerPrev + 0.995f * fbDcBlockerState;
        fbDcBlockerState = fbClean;
        fbDcBlockerPrev = fbRaw;

        filteredFeedback += cachedFbAlpha * (fbClean - filteredFeedback);

        // feedbackWarmth: blend linear feedback → tanh-saturated feedback.
        // 0 = clean/glassy, 1 = warm/harmonically rich. Fully backward compat at default 0.
        if (feedbackWarmth > 0.001f) {
            float w = clampf(feedbackWarmth, 0.0f, 1.0f);
            float sat = phaseon_fast_tanh(filteredFeedback * (1.0f + w * 3.0f));
            filteredFeedback = filteredFeedback + (sat - filteredFeedback) * w;
        }

        float maxFbIndex = 1.5f;
        float targetFbDepth = (fbNorm * fbNorm * fbNorm) * maxFbIndex;
        feedbackPm = filteredFeedback * targetFbDepth;
    }

    const float totalMod = clampf(modInput, -4.0f, 4.0f);
    const float feedbackPhase = clampf(feedbackPm, -1.0f, 1.0f);

    // PM engine update: DC-blocking + cubic depth scaling + asymmetric slew.
    float modClean = totalMod - modDcBlockerPrev + 0.995f * modDcBlockerState;
    modDcBlockerState = modClean;
    modDcBlockerPrev = totalMod;
    modClean = clampf(modClean, -1.0f, 1.0f);

    float sign = (fmDepth < 0.0f) ? -1.0f : 1.0f;
    float absDepth = std::fabs(fmDepth);
    float maxPmIndex = 4.0f;
    float targetPmDepth = sign * (absDepth * absDepth * absDepth) * maxPmIndex * env;

    currentPmDepth += cachedPmSlewCoef * (targetPmDepth - currentPmDepth);

    float sumL = 0.0f, sumR = 0.0f;
    const int voices = std::max(1, std::min(unisonCount, kMaxUnison));

    for (int v = 0; v < voices; ++v) {
        if (!std::isfinite(phase[v])) {
            phase[v] = 0.0f;
        }
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
        float pmPhase = basePhase + (modClean * currentPmDepth) + feedbackPhase;
        pmPhase -= (float)(int)pmPhase;
        if (pmPhase < 0.0f) pmPhase += 1.0f;
        pmPhase = applyPhaseWarp(pmPhase, phaseWarp);

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
            lookupPhase = wrap01(basePhase + fmDepth * modSig + feedbackPhase);
            lookupPhase = applyPhaseWarp(lookupPhase, phaseWarp);
        }

        // TEAR: extra asymmetric phase warp (cheap, no buffers)
        if (tear > 0.0001f) {
            lookupPhase = warpAsym(lookupPhase, clampf(tear, 0.0f, 1.0f));
        }

        // Sample wavetable (or procedural fallback for negative tableIndex)
        float sample;
        if (table && table->frameCount > 0 && table->frameSize > 0) {
#ifdef METAMODULE
            // On MetaModule, use a mip level chosen at control-rate to
            // avoid expensive log/exp work inside the audio loop.
            int mip = mipLevel;
            if (mip < 0) mip = 0;
            if (mip >= table->mipCount()) mip = table->mipCount() - 1;
#else
            float freqHz = fundamentalHz * ratio;
            int mip = table->pickMipLevel(freqHz, sampleRate, bandlimitBias);
#endif
            sample = table->sampleMipFast(lookupPhase, framePos, mip);
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
            // -9: Sync saw
            float p = lookupPhase;
            switch (tableIndex) {
            default:
            case -1: {
                sample = phaseon_fast_sin_01(p);
                break;
            }
            case -2: {
                // FAT SUB-TRIANGLE
                float tri = 2.0f * std::fabs(2.0f * p - 1.0f) - 1.0f;
                sample = phaseon_fast_tanh(tri * 1.5f);
                break;
            }
            case -3: {
                // SHARKTOOTH (PM-ready saw)
                float s = phaseon_fast_sin_01(p);
                sample = phaseon_fast_sin_01(Operator::wrap01(p + s * 0.08f));
                break;
            }
            case -4: {
                // Controlled harmonic stack (odd+even, quickly rolled off)
                float s = 0.0f;
                s += 1.00f * phaseon_fast_sin_01(p * 1.0f);
                s += 0.35f * phaseon_fast_sin_01(p * 2.0f);
                s += 0.22f * phaseon_fast_sin_01(p * 3.0f);
                s += 0.14f * phaseon_fast_sin_01(p * 4.0f);
                s += 0.10f * phaseon_fast_sin_01(p * 5.0f);
                sample = phaseon_fast_tanh(s * 0.90f);
                break;
            }
            case -5: {
                // ASYMMETRIC PULSE-SINE
                float skew = 0.8f;
                if (p < skew) {
                    float t = p / skew;
                    sample = std::sin(t * 3.14159265f);
                } else {
                    float t = (p - skew) / (1.0f - skew);
                    sample = -std::sin(t * 3.14159265f);
                }
                break;
            }
            case -6: {
                // Soft square: avoid brutal discontinuities but still aggressive.
                sample = phaseon_fast_tanh(phaseon_fast_sin_01(p) * 4.0f);
                break;
            }
            case -7: {
                // FOLDED SINE
                float driven = phaseon_fast_sin_01(p) * 2.2f;
                sample = std::sin(driven * 1.5f);
                break;
            }
            case -8: {
                // OCTAVE SUB-GROWL
                float sub = phaseon_fast_sin_01(p);
                float octaveUp = std::fabs(sub) * 2.0f - 1.0f;
                sample = sub * 0.6f + octaveUp * 0.4f;
                break;
            }
            case -9: {
                // SYNC SAW
                float saw = 2.0f * p - 1.0f;
                float syncAmt = 0.58f;
                float syncPhase = Operator::wrap01(p * (1.0f + syncAmt * 3.0f));
                float syncSaw = 2.0f * syncPhase - 1.0f;
                float resetEdge = phaseon_fast_sin_01(Operator::wrap01(p * (2.0f + syncAmt))) * 0.18f;
                sample = phaseon_fast_tanh((saw * 0.45f + syncSaw * 0.85f + resetEdge) * 1.35f);
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
                float softDrive = 3.0f + phaseon_fast_tanh(excess * 0.5f) * 2.0f;
                drive = drive + blend * (softDrive - drive);
            }            // If someone explicitly set WT_WS domain, allow modulation to push drive.
            if (domain == OpDomain::WT_WS) {
                drive = std::max(drive, 1.0f + std::min(5.0f, std::fabs(fmDepth * totalMod) * 2.0f));
                wsm = std::max(wsm, 0.65f);
            }
            float asym = clampf(totalMod * fmDepth * 0.08f, -0.25f, 0.25f);
            float wet = phaseon_fast_tanh((sample + asym) * drive);
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
    if (!std::isfinite(output) || !std::isfinite(outputL) || !std::isfinite(outputR)) {
        reset();
        output = outputL = outputR = 0.0f;
        return;
    }
    prevOut = output;
#endif // VCV Rack path
}

} // namespace phaseon
