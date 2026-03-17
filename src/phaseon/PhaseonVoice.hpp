/*
 * PhaseonVoice.hpp — Single voice: 6 operators + routing + output
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#pragma once
#include "PhaseonOperator.hpp"
#include "PhaseonAlgorithm.hpp"
#include "PhaseonWavetable.hpp"
#include <cmath>
#include <algorithm>

namespace phaseon {

// ─── Envelope (ADSR, per-operator or global) ────────────────────────
struct PhaseonEnvelope {
    float attack  = 0.005f;  // seconds
    float decay   = 0.3f;
    float sustain  = 0.7f;   // level 0..1
    float release  = 0.3f;

    // Envelope curve shape: -1 = logarithmic/soft, 0 = linear, +1 = exponential/snappy
    float curve = 0.0f;

    // Optional AD loop mode (used for GrowlLoop archetype on operator envelopes)
    bool  loop = false;

    enum Stage { Off, Attack, Decay, Sustain, Release };
    Stage stage = Off;
    float level = 0.0f;

    // Block-linearized: increment per sample (set by updateBlock)
    float levelInc = 0.0f;
    float levelTarget = 0.0f;

    void gate(bool on) {
        if (on) {
            stage = Attack;
        } else if (stage != Off) {
            stage = Release;
        }
    }

    void reset() { stage = Off; level = 0.0f; levelInc = 0.0f; }

    // Apply curve shaping to a linear 0..1 value.
    // curve > 0 = exponential (snappy attack, lingering decay)
    // curve < 0 = logarithmic (soft fade-in, quick drop decay)
    static float shapeCurve(float x, float c) {
        x = std::max(0.0f, std::min(1.0f, x));
        if (c > 0.01f) {
            float w = c * 0.9f; 
            return (x * (1.0f - w)) / (1.0f - w * x);
        } else if (c < -0.01f) {
            float ac = -c;
            float w = ac * 0.9f;
            float omx = 1.0f - x;
            return 1.0f - (omx * (1.0f - w)) / (1.0f - w * omx);
        }
        return x;
    }

    // Call once per block (e.g. every 16 or 32 samples) to compute
    // the linearized increment. Then per-sample just call tickFast().
    void updateBlock(float blockDt) {
        // Compute where the envelope will be after blockDt seconds.
        // This mirrors the old per-sample logic but runs once per block.
        float future = level;
        switch (stage) {
        case Attack:
            future += blockDt / std::max(attack, 0.0005f);
            if (future >= 1.0f) { future = 1.0f; }
            break;
        case Decay: {
            float decayRate = blockDt / std::max(decay, 0.0005f) * (1.0f - sustain);
            if (curve > 0.01f) decayRate *= (1.0f - curve * 0.6f);
            else if (curve < -0.01f) decayRate *= (1.0f - curve * 0.4f);
            future -= decayRate;
            if (future <= sustain) future = sustain;
            break;
        }
        case Sustain:
            future = sustain;
            break;
        case Release:
            future -= blockDt / std::max(release, 0.0005f) * level;
            if (future <= 0.0001f) future = 0.0f;
            break;
        case Off:
            future = 0.0f;
            break;
        }
        levelTarget = future;
    }

    // Call once per block AFTER the per-sample loop to commit stage transitions
    void commitBlock() {
        level = levelTarget;
        switch (stage) {
        case Attack:
            if (level >= 1.0f) { level = 1.0f; stage = Decay; }
            break;
        case Decay:
            if (level <= sustain) {
                level = sustain;
                stage = loop ? Attack : Sustain;
            }
            break;
        case Release:
            if (level <= 0.0001f) { level = 0.0f; stage = Off; }
            break;
        default: break;
        }
    }

    // Per-sample: just linear interpolation toward target (ONE add)
    inline float tickFast(float invBlockSize) {
        level += (levelTarget - level) * invBlockSize;
        if (stage == Attack && curve != 0.0f) {
            return shapeCurve(level, curve);
        }
        return level;
    }

    // Legacy per-sample tick (used for non-block-processed paths)
    float tick(float dt) {
        switch (stage) {
        case Attack:
            level += dt / std::max(attack, 0.0005f);
            if (level >= 1.0f) { level = 1.0f; stage = Decay; }
            break;
        case Decay: {
            float decayRate = dt / std::max(decay, 0.0005f) * (1.0f - sustain);
            if (curve > 0.01f) decayRate *= (1.0f - curve * 0.6f);
            else if (curve < -0.01f) decayRate *= (1.0f - curve * 0.4f);
            level -= decayRate;
            if (level <= sustain) {
                level = sustain;
                stage = loop ? Attack : Sustain;
            }
            break;
        }
        case Sustain:
            level = sustain;
            break;
        case Release:
            level -= dt / std::max(release, 0.0005f) * level;
            if (level <= 0.0001f) { level = 0.0f; stage = Off; }
            break;
        case Off:
            level = 0.0f;
            break;
        }
        if (stage == Attack && curve != 0.0f) {
            return shapeCurve(level, curve);
        }
        return level;
    }

    bool isActive() const { return stage != Off; }
};

// ─── Transient spike envelope (one-shot on note-on) ─────────────────
struct TransientSpike {
    float intensity = 0.0f;  // CV-driven intensity (0..1)
    float duration  = 0.015f; // seconds (typically 5-50ms)

    float phase = 0.0f;
    bool  active = false;

    // Last computed raw env (0..1) for multi-target spikes
    float lastEnv = 0.0f;

    void trigger() {
        if (intensity > 0.001f) {
            phase = 0.0f;
            active = true;
        }
    }

    void reset() { phase = 0.0f; active = false; }

    // Returns spike multiplier (0 = no spike, >0 = FM index boost)
    float tick(float dt) {
        if (!active) return 0.0f;
        phase += dt;
        if (phase >= duration) {
            active = false;
            lastEnv = 0.0f;
            return 0.0f;
        }
        // Exponential decay shape
        float env = expf(-phase / (duration * 0.25f));
        lastEnv = env;
        return env * intensity;
    }
};

// ─── Mod matrix runtime (smoothing state, not preset data) ────────
static constexpr int kModSlots = 6;

struct PhaseonModRuntime {
    float smoothed[kModSlots] = {};
    void reset() { for (int i = 0; i < kModSlots; ++i) smoothed[i] = 0.0f; }
};

// ─── Voice ──────────────────────────────────────────────────────────
struct PhaseonVoice {
#ifdef METAMODULE
    static constexpr int kNumOps = 4;
#else
    static constexpr int kNumOps = 6;
#endif

    Operator ops[kNumOps];
    PhaseonEnvelope ampEnv;        // master amplitude envelope
    PhaseonEnvelope opEnvs[kNumOps]; // per-operator mod envelopes (optional)
    TransientSpike spike;

    int   algorithmIndex = 0;
    float algorithmMorph = 0.0f; // 0..1, morph between algorithmIndex and next
    float fundamentalHz  = 261.63f; // C4 default
    float masterLevel    = 1.0f;

    // Complexity (domain + depth modulation)
    float complexAmount  = 0.0f; // 0..1
    float depthModMix    = 0.0f; // 0..1 split: phase vs depth modulation
    float depthModAmount = 0.0f; // 0..~0.6 scale of depth modulation

    // Instability parameters (bounded jitter, not chaos)
    float instability = 0.0f; // 0..1

    // Motion (macro life system)
    float motionAmount = 0.0f; // 0..1

    // Macro LFO amount (repurposed from TAIL knob). 0 = off, 1 = max.
    float macroLfoAmount = 0.0f;

    // Per-LFO user controls (set by applyMacros from module trimpots), per-operator
    float lfoRateUser[2][6]    = { {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f} };    // 0..1 mapped to Hz mult
    float lfoPhaseUser[2][6]   = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };    // 0..1 phase offset
    float lfoDeformUser[2][6]  = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };    // 0..1 shape morph
    float lfoAmpUser[2][6]     = { {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f} };    // 0..1 amplitude
    int   lfoTargetOp[2]             = {-1, -1};        // -1=ALL, 0..5=specific op

    // Cross-operator coupling network (0..1)
    float networkAmount = 0.0f;

    // Wavetable family selection + drivers (set at control rate)
    // 0 = Classic, 1 = Formant, 2 = Hybrid
    int   wtFamily = 0;
    float edgeAmount = 0.0f;    // 0..1
    float timbreAmount = 0.0f;  // 0..1
    float tiltAmount = 0.0f;    // -1..+1 (performance CV)

    // Per-note organic variation for formant centers
    float formantRandMul = 1.0f;

    // FM Chaos (performance layer)
    float chaosAmount = 0.0f;  // 0..1

    // SCRAMBLE: per-operator envelope secondary modulation targets
    // targetId: 0=framePos, 1=fmDepth, 2=feedback, 3=pdAmount, 4=wsMix, 5=wsDrive
    float scrambleSecAmount[kNumOps] = {};  // 0..1 blend amount
    int   scrambleTargetId[kNumOps] = {};   // which field to modulate

    // Scene morph envelopes + deterministic roles
    float envStyle  = 2.0f;  // 0..4 continuous
    float envSpread = 0.0f;  // 0..1
    float roleBias  = 0.0f;  // -1..+1
    uint32_t roleSeed = 0xC0FFEE11u;
    float opRolePos[kNumOps] = {}; // 0..4 per operator (continuous role coordinate)

    // Output
    float outL = 0.0f;
    float outR = 0.0f;

    // Bitcrusher amount (0 = clean, 1 = heavy). Set by macros via the
    // repurposed SPIKE/Bitcrush knob.
    float bitcrushAmount = 0.0f;
    // Bitcrusher internal state: sample-and-hold downsampler + quantizer.
    float bitcrushHoldL = 0.0f;
    float bitcrushHoldR = 0.0f;
    int   bitcrushSamplesLeft = 0;

    // ── Mod matrix runtime (smoothed signal per slot) ──────────────
    PhaseonModRuntime modRuntime;

    // ── Cached per-note signals (set in noteOn; read by applyModMatrix) ──
    float cachedVelocity    = 1.0f;   // 0..1
    float cachedKeytrack    = 0.5f;   // 0..1, C0=0 .. C8=1
    float cachedRandPerNote = 0.0f;   // -1..+1, deterministic from roleSeed

    // Internal RNG state for instability (xorshift32)
    uint32_t rngState = 0x12345678;

    // Motion state
    float lfo1Phase = 0.0f;
    float lfo2Phase = 0.0f;
    // Per-operator LFO2 phase (required for per-op rate control)
    float lfo2PhaseOp[kNumOps] = {};
    float randLane = 0.0f;
    float randTarget = 0.0f;
    float randSlew = 0.0f;
    float randTimer = 0.0f;
    float microPhase = 0.0f;

    // Network state (filtered cross-mod bus)
    float networkBusZ = 0.0f;
    int   networkDivider = 0;
    float cachedNetworkStrength = 0.0f;
    float cachedNetworkCoeff = 0.0f;

    // Cached per-sample constants (set at control rate or sample rate change)
    float cachedDt = 1.0f / 48000.0f;
    float cachedRandCoeff = 0.05f;
    float cachedChaosSlewCoeff = 0.05f;
    float cachedChaosCleanCoeff = 0.05f;

    // Cached block-rate LFO values
    float cachedLfo1Op[kNumOps] = {};
    float cachedLfo2Op[kNumOps] = {};
    float cachedMicro = 0.0f;

    // Cached formant saturation normalization (only depends on formantAmount)
    float cachedFormantTanhNorm = 1.0f;

    // Chaos state (per operator)
    float chaosValue[kNumOps] = {};
    float chaosTarget[kNumOps] = {};
    float chaosRetargetTimer = 0.0f;
    struct RouteDest { int d; float wp; float wd; };
    RouteDest opRoutes[kNumOps][kNumOps];
    int opRouteCount[kNumOps] = {0};

    // Formant shaper (subtle by default, can become obvious when driven)
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() { z1 = 0.0f; z2 = 0.0f; }

        float process(float x) {
            // Direct Form II
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setPeaking(float sampleRate, float f0, float Q, float gainDb) {
            float fs = std::max(1.0f, sampleRate);
            float f = std::max(10.0f, std::min(f0, fs * 0.45f));
            float q = std::max(0.30f, std::min(Q, 32.0f));

            // Peaking EQ using fast approximations for trig
            float A = powf(10.0f, gainDb / 40.0f);
            float w0 = 6.283185307f * (f / fs);
            // Fast sin/cos approximation for biquad coefficients
            float cw = phaseon_fast_cos_w0(w0);
            float sw = phaseon_fast_sin_w0(w0);
            float alpha = sw / (2.0f * q);

            float b0n = 1.0f + alpha * A;
            float b1n = -2.0f * cw;
            float b2n = 1.0f - alpha * A;
            float a0n = 1.0f + alpha / A;
            float a1n = -2.0f * cw;
            float a2n = 1.0f - alpha / A;

            float invA0 = 1.0f / a0n;
            b0 = b0n * invA0;
            b1 = b1n * invA0;
            b2 = b2n * invA0;
            a1 = a1n * invA0;
            a2 = a2n * invA0;
        }
    };

    Biquad formantL[3];
    Biquad formantR[3];
    int formantDivider = 0;
    float cachedFormantStrength = 0.0f;
    float formantAmount = 0.0f;  // 0..1, user-facing formant intensity (knob + CV)
    float vowelPos = 0.0f;       // 0..1, vowel position (decoupled from timbre)

    void reset() {
        for (int i = 0; i < kNumOps; ++i) {
            ops[i].reset();
            opEnvs[i].reset();
            opEnvs[i].loop = false;
            opRolePos[i] = 2.0f; // Lead-ish baseline
        }
        ampEnv.reset();
        spike.reset();
        outL = outR = 0.0f;

        lfo1Phase = lfo2Phase = 0.0f;
        for (int i = 0; i < kNumOps; ++i)
            lfo2PhaseOp[i] = 0.0f;
        randLane = randTarget = 0.0f;
        randSlew = 0.0f;
        randTimer = 0.0f;
        microPhase = 0.0f;
        networkBusZ = 0.0f;
        networkDivider = 0;
        cachedNetworkStrength = 0.0f;
        cachedNetworkCoeff = 0.0f;
        chaosRetargetTimer = 0.0f;
        for (int i = 0; i < kNumOps; ++i) {
            chaosValue[i] = 0.0f;
            chaosTarget[i] = 0.0f;
            scrambleSecAmount[i] = 0.0f;
            scrambleTargetId[i] = 0;
        }

        wtFamily = 0;
        edgeAmount = 0.0f;
        timbreAmount = 0.0f;
        tiltAmount = 0.0f;
        networkAmount = 0.0f;
        formantRandMul = 1.0f;
        modRuntime.reset();
        cachedVelocity    = 1.0f;
        cachedKeytrack    = 0.5f;
        cachedRandPerNote = 0.0f;
        formantDivider = 0;
        cachedFormantStrength = 0.0f;
        formantAmount = 0.0f;
        vowelPos = 0.0f;
        for (int i = 0; i < 3; ++i) {
            formantL[i].reset();
            formantR[i].reset();
        }
    }

    void noteOn(float hz, float velocity) {
        fundamentalHz = hz;
        ampEnv.gate(true);
        for (int i = 0; i < kNumOps; ++i)
            opEnvs[i].gate(true);
        spike.trigger();

        // Cache per-note signals for mod matrix
        cachedVelocity = std::max(0.0f, std::min(1.0f, velocity));
        // Keytrack: map Hz to 0..1 over C0 (16.35Hz) .. C8 (4186Hz), ~8 octaves
        {
            float kHz = std::max(16.35f, hz);
            cachedKeytrack = std::max(0.0f, std::min(1.0f, log2f(kHz / 16.35f) / 8.0f));
        }
        // Deterministic random per note (seeded from roleSeed, stable across SHUFFLE)
        {
            uint32_t h = hash32(roleSeed ^ (uint32_t)(hz * 1000.0f) ^ 0xF00DCAFEu);
            cachedRandPerNote = (float)(h & 0xFFFFu) / 32768.0f - 1.0f; // -1..+1
        }

        // Deterministic per-note role positions (continuous 0..4)
        generateRolePositions();

        // New note = new chaos sweet-spot targets
        chaosRetargetTimer = 0.0f;
        retargetChaos(true);

        // Per-note organic formant offset (deterministic)
        // Small multiplier ~0.97..1.03
        uint32_t h = hash32(roleSeed ^ (uint32_t)(hz * 1000.0f) ^ 0xA53C9E19u);
        float r = u01(h) * 2.0f - 1.0f;
        formantRandMul = 1.0f + r * 0.03f;
    }

    void noteOff() {
        ampEnv.gate(false);
        for (int i = 0; i < kNumOps; ++i)
            opEnvs[i].gate(false);
    }

    bool isActive() const {
        if (ampEnv.isActive()) return true;
        for (int i = 0; i < kNumOps; ++i) {
            if (opEnvs[i].isActive()) return true;
        }
        return false;
    }

    // Apply a new role seed immediately (no re-gate). Used by SHUFFLE for mid-note re-voicing.
    void applyRoleSeed(uint32_t newSeed) {
        roleSeed = newSeed;
        generateRolePositions();
        retargetChaos(true);
        chaosRetargetTimer = 0.0f;

        // Update per-note formant offset as well so SHUFFLE is immediately audible
        // even when the note is held.
        {
            uint32_t h = hash32(roleSeed ^ (uint32_t)(fundamentalHz * 1000.0f) ^ 0xA53C9E19u);
            float r = u01(h) * 2.0f - 1.0f;
            formantRandMul = 1.0f + r * 0.03f;
        }

        // Make the change audible immediately (avoid long slews masking SHUFFLE)
        randTimer = 0.0f;
        randLane = 0.0f;
        randTarget = cheapRandom();

        // Reset network filter state so the new coupling topology speaks instantly
        networkBusZ = 0.0f;
        networkDivider = 0;
    }

    // Call once when sample rate changes (or at init)
    void setSampleRate(float sampleRate) {
        cachedDt = 1.0f / std::max(1.0f, sampleRate);
    }

    // Main per-sample processing
    void tick(const WavetableBank& bank, float sampleRate) {
        const float dt = cachedDt;

        // Block-rate envelope linearization (every 16 samples)
        // This replaces 7 divisions per sample with 7 divisions per 16 samples
        static constexpr int kEnvBlockSize = 16;
        static constexpr float kInvEnvBlock = 1.0f / (float)kEnvBlockSize;
        if (networkDivider == 0) {
            float blockDt = dt * kEnvBlockSize;
            ampEnv.updateBlock(blockDt);
            for (int i = 0; i < kNumOps; ++i)
                opEnvs[i].updateBlock(blockDt);
        }

        // Per-sample: just linear interpolation (1 multiply + 1 add each)
        float envLevel = ampEnv.tickFast(kInvEnvBlock);

        float modEnv[kNumOps];
        for (int i = 0; i < kNumOps; ++i) {
            modEnv[i] = opEnvs[i].tickFast(kInvEnvBlock);
        }

        // Commit envelope stage transitions at block boundary
        if (networkDivider == 0) {
            ampEnv.commitBlock();
            for (int i = 0; i < kNumOps; ++i)
                opEnvs[i].commitBlock();
        }

        // Transient spike
        float spikeVal = spike.tick(dt);

        // Motion engine (life system): 2 LFOs + smoothed random lane + micro movement
        float motion = std::max(0.0f, std::min(1.0f, motionAmount));

        // ── Block-rate LFO + random updates (every 16 samples) ────────
        float macroLfo = clamp01(macroLfoAmount);
        if (networkDivider == 0) {
            auto getRateMult = [](float v) {
                if (v >= 0.49f && v <= 0.51f) return 1.0f;
                if (v > 0.5f) return 1.0f + (v - 0.5f) * 2.0f * 7.0f;
                int divs[] = {32, 16, 8, 4, 3, 2};
                int idx = (int)(v * 2.0f * 5.99f);
                if (idx < 0) idx = 0; if (idx > 5) idx = 5;
                return 1.0f / (float)divs[idx];
            };

            float dtBlock = dt * 16.0f;
            float rate1 = 0.18f * getRateMult(lfoRateUser[0][0]);
            float rate2Base = 0.41f * getRateMult(lfoRateUser[1][0]);
            float rateScale = 0.25f + motion * 2.5f;

            lfo1Phase += dtBlock * (rate1 * rateScale);
            lfo2Phase += dtBlock * (rate2Base * rateScale);
            if (lfo1Phase >= 1.0f) lfo1Phase -= std::floor(lfo1Phase);
            if (lfo2Phase >= 1.0f) lfo2Phase -= std::floor(lfo2Phase);

            for (int i = 0; i < kNumOps; ++i) {
                float rate2 = 0.41f * getRateMult(lfoRateUser[1][i]);
                lfo2PhaseOp[i] += dtBlock * (rate2 * rateScale);
                if (lfo2PhaseOp[i] >= 1.0f) lfo2PhaseOp[i] -= std::floor(lfo2PhaseOp[i]);
            }

            float lfo1StaggerAmt = (lfoTargetOp[0] == -1) ? motion : 0.0f;
            float lfo2StaggerAmt = (lfoTargetOp[1] == -1) ? macroLfo : 0.0f;

            for (int i = 0; i < kNumOps; ++i) {
                float p1 = lfo1Phase + lfoPhaseUser[0][0];
                if (lfo1StaggerAmt > 0.0001f) p1 += ((float)i / 6.0f) * lfo1StaggerAmt;
                p1 -= std::floor(p1);
                cachedLfo1Op[i] = deformLfo(p1, lfoDeformUser[0][0]) * lfoAmpUser[0][0];

                float p2 = lfo2PhaseOp[i] + lfoPhaseUser[1][i];
                if (lfo2StaggerAmt > 0.0001f) p2 += ((float)i / 6.0f) * lfo2StaggerAmt;
                p2 -= std::floor(p2);
                cachedLfo2Op[i] = deformLfo(p2, lfoDeformUser[1][i]) * lfoAmpUser[1][i];
            }

            // Random lane
            randTimer -= dtBlock;
            if (randTimer <= 0.0f) {
                randTimer = 0.18f + (1.0f - motion) * 0.25f;
                randTarget = cheapRandom();
            }

            // Precompute slew coefficients (depend only on motion+sampleRate)
            cachedRandCoeff = 1.0f - expf(-dtBlock / (0.06f + (1.0f - motion) * 0.08f));

            // Micro movement
            microPhase += dtBlock * (220.0f + motion * 520.0f);
            if (microPhase >= 1.0f) microPhase -= std::floor(microPhase);
            cachedMicro = phaseon_fast_sin_01(microPhase);

            // Chaos slew coefficients
            if (chaosAmount > 0.001f) {
                float tau = 0.035f + (1.0f - chaosAmount) * 0.12f;
                cachedChaosSlewCoeff = 1.0f - expf(-dtBlock / tau);
            }
            cachedChaosCleanCoeff = 1.0f - expf(-dtBlock / 0.08f);

            // Precompute operator phase increments (avoids division per sample per operator)
            for (int i = 0; i < kNumOps; ++i) {
                ops[i].cachedPhaseInc = fundamentalHz * ops[i].ratio * cachedDt;
            }
        }

        // Smooth random lane every sample (cheap: 1 multiply+add)
        randLane += (randTarget - randLane) * cachedRandCoeff;

        // Use cached LFO values
        const float* lfo1Op = cachedLfo1Op;
        const float* lfo2Op = cachedLfo2Op;
        float micro = cachedMicro;

        // Instability: small random perturbations
        float phaseJitter = 0.0f;
        float fmMicroVar  = 0.0f;
        if (instability > 0.001f) {
            phaseJitter = cheapRandom() * instability * 0.005f;
            fmMicroVar  = cheapRandom() * instability * 0.12f;
        }

        // FM Chaos: bounded, zone-aware targets that glide
#ifndef METAMODULE
        // Chaos disabled on MetaModule for CPU savings
        if (chaosAmount > 0.001f) {
            chaosRetargetTimer -= dt;
            float retargetRate = 0.8f + 5.0f * chaosAmount + 2.0f * motion;
            if (chaosRetargetTimer <= 0.0f) {
                chaosRetargetTimer = 1.0f / retargetRate;
                retargetChaos(false);
            }
            for (int i = 0; i < kNumOps; ++i) {
                chaosValue[i] += (chaosTarget[i] - chaosValue[i]) * cachedChaosSlewCoeff;
            }
        } else {
            for (int i = 0; i < kNumOps; ++i)
                chaosValue[i] += (0.0f - chaosValue[i]) * cachedChaosCleanCoeff;
        }
#endif // !METAMODULE

        // Algorithm morph: interpolate routing weights between algo A and algo B
        int algoAIdx = algorithmIndex;
        int algoBIdx = algoAIdx + 1;
        if (algoBIdx >= kAlgorithmCount) algoBIdx = kAlgorithmCount - 1;
        const Algorithm& algoA = getAlgorithm(algoAIdx);
        const Algorithm& algoB = getAlgorithm(algoBIdx);
        float morph = std::max(0.0f, std::min(1.0f, algorithmMorph));

        // ── NETWORK (cross-operator coupling) ─────────────────────
        // Deterministic, filtered bus built from previous op outputs.
        // Injected as extra phase modulation on top of the selected algorithm.
        float netAmt = 0.0f;
        float netBus = 0.0f;
#ifndef METAMODULE
        // Network disabled on MetaModule for CPU savings
        netAmt = clamp01(networkAmount);
        if (netAmt > 0.0005f) {
            // Update strength + filter coefficient at control rate
            if (++networkDivider >= 32) {
                networkDivider = 0;

                float sr = std::max(1000.0f, sampleRate);
                float ny = sr * 0.5f;

                float e = clamp01(edgeAmount);
                // Make NET come in earlier but still explode near the top.
                float n = powf(netAmt, 0.55f);

                // Nonlinear strength curve: subtle low, strong high
                float strength = n * n;
                // Edge makes it more "bite" capable
                float edgeDrive = 0.55f + 0.70f * powf(e, 1.25f);
                strength *= edgeDrive;

                // User request: make NET much more pronounced
                strength *= 10.0f;

                // Pitch safety: clamp at higher fundamentals
                float pitchNorm = clamp01(fundamentalHz / (ny * 0.35f));
                float atten = 1.0f - 0.60f * powf(pitchNorm, 1.35f);
                if (atten < 0.25f) atten = 0.25f;
                strength *= atten;

                // Keep it bounded
                cachedNetworkStrength = clampf(strength, 0.0f, 11.00f);

                // Filter cutoff: low NETWORK = body, high NETWORK = bite
                float fc = 55.0f + 5000.0f * powf(n, 1.25f);
                // Edge biases cutoff upward a bit
                fc *= (0.75f + 0.55f * e);
                if (fc > ny * 0.40f) fc = ny * 0.40f;
                if (fc < 10.0f) fc = 10.0f;
                float c = 1.0f - expf(-6.283185307f * fc / sr);
                cachedNetworkCoeff = clampf(c, 0.0001f, 0.35f);
            }

            // Build raw bus from previous outputs, preferring modulators.
            float raw = 0.0f;
            for (int j = 0; j < kNumOps; ++j) {
                float cA = algoA.isCarrier[j] ? 1.0f : 0.0f;
                float cB = algoB.isCarrier[j] ? 1.0f : 0.0f;
                float carrierW = cA + (cB - cA) * morph;
                float modW = 1.0f - clamp01(carrierW);

                uint32_t h = hash32(roleSeed ^ (uint32_t)(0xB5297A4Du * (uint32_t)(algoAIdx + 1)) ^ (uint32_t)(0x9E3779B9u * (uint32_t)(j + 1)));
                float w = u01(h) * 2.0f - 1.0f; // -1..+1

                // Keep weights in a musically useful window
                w *= 1.15f;
                raw += ops[j].prevOut * w * (0.25f + 0.75f * modW);
            }

            // One-pole filtering + soft bounding
            networkBusZ += (raw - networkBusZ) * cachedNetworkCoeff;
            netBus = phaseon_fast_tanh(networkBusZ * 3.60f);
        }
#endif // !METAMODULE

        if (networkDivider == 0) {
            float dmix = depthModMix;
            if (dmix < 0.0f) dmix = 0.0f;
            if (dmix > 1.0f) dmix = 1.0f;
            auto hasConn_cache = [](const Algorithm& a, int src, int dst) -> float {
                for (int m = 0; m < a.connectionCount; ++m) {
                    if (a.connections[m].src == src && a.connections[m].dst == dst) return 1.0f;
                }
                return 0.0f;
            };
            for (int oi = 0; oi < kNumOps; ++oi) {
                opRouteCount[oi] = 0;
                for (int dst = 0; dst < kNumOps; ++dst) {
                    float wA = hasConn_cache(algoA, oi, dst);
                    float wB = hasConn_cache(algoB, oi, dst);
                    float w = wA + (wB - wA) * morph;
                    if (w > 0.0f) {
                        opRoutes[oi][opRouteCount[oi]++] = {dst, w * (1.0f - dmix), w * dmix};
                    }
                }
            }
        }

        // Macro LFO: use LFO2 as the wobble source, scaled by the macro knob.
        float lfoGrowlOp[kNumOps] = {};
        if (macroLfo > 0.001f) {
            float shape = macroLfo * macroLfo; // nonlinear: subtle low, strong high
            for (int i = 0; i < kNumOps; ++i) {
                lfoGrowlOp[i] = lfo2Op[i] * shape; // -shape..+shape
            }
        }

        // Clear modulation accumulators
        float phaseModAccum[kNumOps] = {};
        float depthModAccum[kNumOps] = {};

        auto hasConn = [](const Algorithm& a, int src, int dst) -> float {
            for (int c = 0; c < a.connectionCount; ++c) {
                if (a.connections[c].src == src && a.connections[c].dst == dst) return 1.0f;
            }
            return 0.0f;
        };

        // Process operators in reverse order (modulators before carriers)
        for (int i = kNumOps - 1; i >= 0; --i) {
            Operator& op = ops[i];
            const Wavetable* wt = bank.get(op.tableIndex);

            float netDrive = 0.0f;

            // NETWORK injection: extra PM from global bus, reduced for carriers.
            if (netAmt > 0.0005f && cachedNetworkStrength > 0.0005f) {
                float cA = algoA.isCarrier[i] ? 1.0f : 0.0f;
                float cB = algoB.isCarrier[i] ? 1.0f : 0.0f;
                float carrierW = cA + (cB - cA) * morph;
                float modW = 1.0f - clamp01(carrierW);
                float dstScale = 0.30f + 0.70f * modW;

                uint32_t h = hash32(roleSeed ^ (uint32_t)(0x7F4A7C15u * (uint32_t)(algoAIdx + 1)) ^ (uint32_t)(0xC2B2AE35u * (uint32_t)(i + 1)));
                float s = u01(h) * 2.0f - 1.0f;
                // Center weight a bit to avoid pure random sign flips dominating
                float perDst = 0.45f + 0.55f * s;

                // Cache a per-operator network drive used for both PM and FM-depth shaping.
                netDrive = netBus * cachedNetworkStrength * dstScale * perDst;

                float add = netDrive;
                // Hard safety clamp (in PM modulation units)
                add = clampf(add, -2.80f, 2.80f);
                phaseModAccum[i] += add;
            }

            // Impact-ish transient: short pitch-up + phase nudge at note start
            float impact = 0.0f;
            {
                float p = opRolePos[i];
                // 0..1 weight for rolePos in [0..~1] (Impact -> Stab transition)
                impact = 1.0f - std::max(0.0f, std::min(1.0f, p));
            }

            // Apply instability
            float extraPhase = phaseJitter;
            float extraFm = fmMicroVar;

            // 2nd-order depth modulation: modulate FM depth itself
            float depthFactor = 1.0f + depthModAccum[i] * depthModAmount;
            if (depthFactor < 0.0f) depthFactor = 0.0f;
            if (depthFactor > 3.0f) depthFactor = 3.0f;

            // PUNCH: boost FM depth on note start (modulators only; morph-aware)
            float effectiveFmDepth = op.fmDepth * depthFactor;

#ifndef METAMODULE
            // NETWORK also shapes FM depth (more profound than PM alone)
            if (netAmt > 0.0005f && cachedNetworkStrength > 0.0005f) {
                // netDrive is already carrier-protected via dstScale.
                float factor = 1.0f + clampf(netDrive, -1.2f, 1.2f) * 1.60f;
                if (factor < 0.0f) factor = 0.0f;
                if (factor > 6.0f) factor = 6.0f;
                effectiveFmDepth *= factor;
            }
#endif
            float punchW = 0.0f;
            if (spikeVal > 0.0001f) {
                float cA = algoA.isCarrier[i] ? 1.0f : 0.0f;
                float cB = algoB.isCarrier[i] ? 1.0f : 0.0f;
                float carrierW = cA + (cB - cA) * morph;
                float modW = 1.0f - clamp01(carrierW);
                punchW = modW;
            }
            effectiveFmDepth += effectiveFmDepth * (spikeVal * punchW);
            effectiveFmDepth += extraFm;

#ifndef METAMODULE
            // GrowlLoop: treat the operator envelope as an internal LFO that scales FM depth.
            // This makes the wobble clearly audible (dubstep movement) without adding noise.
            float loopW = 0.0f;
            {
                float p = opRolePos[i];
                // Weight for rolePos near GrowlLoop (4)
                loopW = std::max(0.0f, std::min(1.0f, (p - 3.20f) / 0.80f));
            }
            if (loopW > 0.001f) {
                float l = modEnv[i];
                // Scale depth around unity: 0.35..1.65 based on envelope, then blend by loopW.
                float lfo = 0.35f + 1.30f * l;
                effectiveFmDepth *= (1.0f - loopW) + loopW * lfo;
            }

            // Impact roles (kick-like). Keep it tiny and musical.
            float opFundHz = fundamentalHz;
            if (impact > 0.001f) {
                // (No longer driven by PUNCH; keep this block for future non-PUNCH impact behaviors.)
            }

            // Motion drives spectral wobble (liquid, not linear)
            if (motion > 0.001f) {
                // Per-operator LFO2 response (always active; Target switch only changes edit mode)
                float wobble = 0.10f * lfo2Op[i] + 0.08f * randLane + 0.02f * micro;
                effectiveFmDepth *= (1.0f + motion * wobble);
            }

            // Chaos layer multiplies FM depth within sweet-spot bounds
            if (chaosAmount > 0.001f) {
                effectiveFmDepth *= (1.0f + chaosValue[i]);
            }

            // Macro LFO: additional FM index wobble (PM depth). Nonlinear so
            // low settings stay subtle while high settings become extreme.
            if (macroLfo > 0.001f) {
                float maxSwing = 0.30f + 1.50f * macroLfo; // 0.3..1.8
                float delta = 1.0f + lfoGrowlOp[i] * maxSwing;
                if (delta < 0.10f) delta = 0.10f;
                if (delta > 3.0f)  delta = 3.0f;
                effectiveFmDepth *= delta;
            }
#else
            // MetaModule simplified: no GrowlLoop, motion wobble, chaos, macro LFO per-op
            float opFundHz = fundamentalHz;
#endif

            // Save original, apply boosted depth
            float origDepth = op.fmDepth;
            op.fmDepth = effectiveFmDepth;

            // Apply instability to wavetable position
            float origFrame = op.framePos;
            float fp = op.framePos;

            float origFeedback = op.feedback;
            float origPd = op.pdAmount;
            float origWsMix = op.wsMix;
            float origWsDrive = op.wsDrive;

#ifndef METAMODULE
            // ── SCRAMBLE secondary target modulation ────────────────
            // Modulate one secondary operator field using this op's envelope.
            // The modulation is bipolar around the envelope's sustain level:
            //   (modEnv[i] - sustain) so attack phase adds, sustain is neutral, release subtracts.

            if (scrambleSecAmount[i] > 0.001f) {
                float envDelta = modEnv[i] - opEnvs[i].sustain;
                float amt = scrambleSecAmount[i];
                int tid = scrambleTargetId[i];
                switch (tid) {
                case 0: // framePos
                    fp += envDelta * amt * 0.70f;
                    break;
                case 1: // fmDepth
                    op.fmDepth += envDelta * amt * 3.0f;
                    if (op.fmDepth < 0.0f) op.fmDepth = 0.0f;
                    break;
                case 2: // feedback
                    op.feedback += envDelta * amt * 0.50f;
                    op.feedback = std::max(0.0f, std::min(0.98f, op.feedback));
                    break;
                case 3: // pdAmount
                    op.pdAmount += envDelta * amt * 0.60f;
                    op.pdAmount = std::max(0.0f, std::min(1.0f, op.pdAmount));
                    break;
                case 4: // wsMix
                    op.wsMix += envDelta * amt * 0.50f;
                    op.wsMix = std::max(0.0f, std::min(1.0f, op.wsMix));
                    break;
                case 5: // wsDrive
                    op.wsDrive += envDelta * amt * 1.2f;
                    op.wsDrive = std::max(1.0f, std::min(4.5f, op.wsDrive));
                    break;
                default: break;
                }
            }

            // Motion WT orbit drift
            if (motion > 0.001f) {
                float orbit = 0.035f * lfo1Op[i] + 0.020f * randLane + 0.006f * micro;
                fp += orbit * motion;
            }

            // Instability micro offset
            fp += cheapRandom() * instability * 0.025f;
#endif // !METAMODULE
            op.framePos = std::max(0.0f, std::min(1.0f, fp));

            // Stereo phase spread (width without detune chorus)
            float width = 0.0f;
#ifndef METAMODULE
            if (motion > 0.001f) {
                width = motion * (0.06f + 0.04f * (0.5f + 0.5f * lfo1Op[i]));
            }
#endif
#ifdef METAMODULE
            op.unisonCount = 1; // Save ~50% operator cost on embedded
#else
            op.unisonCount = (motion > 0.08f) ? 2 : 1;
#endif
            op.unisonDetune = 0.0f; // explicitly avoid chorus detune feel
            op.unisonStereo = 1.0f;
            op.unisonPhaseSpread = width;

            // Conservative bandlimit under heavy motion/chaos (helps perception)
            op.bandlimitBias = 1.0f + 0.6f * motion + 0.7f * chaosAmount;

            // Tick operator with accumulated modulation from other ops
            op.tick(opFundHz + extraPhase, phaseModAccum[i], wt, sampleRate);

            // Restore original values
            op.fmDepth  = origDepth;
            op.framePos = origFrame;
            op.feedback = origFeedback;
            op.pdAmount = origPd;
            op.wsMix    = origWsMix;
            op.wsDrive  = origWsDrive;

            // Route this operator's output with interpolated routing weights.
            // PUNCH injects a short extra modulation burst for modulators only.
            float modSignal = op.output * modEnv[i];
            if (spikeVal > 0.0001f) {
                float cA = algoA.isCarrier[i] ? 1.0f : 0.0f;
                float cB = algoB.isCarrier[i] ? 1.0f : 0.0f;
                float carrierW = cA + (cB - cA) * morph;
                float modW = 1.0f - clamp01(carrierW);

                float punchVal = spikeVal * modW;
                // Convert to an additive transient modulation amount.
                // This avoids "sometimes nothing" when modEnv starts near 0.
                float extra = punchVal * 0.85f;
                if (extra > 2.25f) extra = 2.25f;
                modSignal += op.output * extra;
            }
            int rc = opRouteCount[i];
            for (int r = 0; r < rc; ++r) {
                const auto& rt = opRoutes[i][r];
                phaseModAccum[rt.d] += modSignal * rt.wp;
                depthModAccum[rt.d] += modSignal * rt.wd;
            }
        }

        // Sum carriers to output (carrier membership is also morphed)
        // Per-operator envelopes (modEnv) now also gate carrier amplitude so
        // ENV edits are clearly audible per operator.
        float sumL = 0.0f, sumR = 0.0f;
        float carrierWeightSum = 0.0f;
        for (int i = 0; i < kNumOps; ++i) {
            float cA = algoA.isCarrier[i] ? 1.0f : 0.0f;
            float cB = algoB.isCarrier[i] ? 1.0f : 0.0f;
            float cw = cA + (cB - cA) * morph;

#ifndef METAMODULE
            // Macro LFO: slight carrier/modulator drift. At low settings this
            // just adds motion; at high settings it can tear the bass apart.
            if (macroLfo > 0.001f) {
                float drift = lfoGrowlOp[i] * 0.25f * macroLfo; // small, signed offset
                cw = clamp01(cw + drift);
            }
#endif

            if (cw <= 0.0001f) continue;
            float envAmp = modEnv[i];
            sumL += ops[i].outputL * cw * envAmp;
            sumR += ops[i].outputR * cw * envAmp;
            carrierWeightSum += cw;
        }

        if (carrierWeightSum > 0.0001f) {
            float norm = 1.0f / carrierWeightSum;
            sumL *= norm;
            sumR *= norm;
        }

#ifndef METAMODULE
        // Gentle soft clip on carrier sum — knee at +/-3.0, ceiling +/-4.5
        auto softClipCarrier = [](float x) -> float {
            if (std::fabs(x) > 3.0f) {
                float sign = (x > 0.f) ? 1.f : -1.f;
                float over = std::fabs(x) - 3.0f;
                x = sign * (3.0f + phaseon_fast_tanh(over) * 1.5f);
            }
            return x;
        };
        sumL = softClipCarrier(sumL);
        sumR = softClipCarrier(sumR);

        // PUNCH: add a short amplitude "thump" on note-on.
        // This is intentionally independent of routing topology so the control stays consistent.
        if (spikeVal > 0.0001f) {
            // Smooth, bounded gain bump: ~0..+35% depending on PUNCH intensity.
            float bump = phaseon_fast_tanh(spikeVal * 0.85f) * 0.35f;
            float g = 1.0f + bump;
            sumL *= g;
            sumR *= g;
        }
#endif // !METAMODULE

        outL = sumL * masterLevel;
        outR = sumR * masterLevel;

#ifndef METAMODULE
        // Global bitcrusher (post-FM, pre-formant). Combines sample-rate
        // reduction (sample-and-hold) with bit-depth reduction so the effect
        // is clearly audible from subtle to extreme.
        if (bitcrushAmount > 0.0005f) {
            float amt = clamp01(bitcrushAmount);

            // Hold length in samples: 1 (clean) .. ~64 (strong downsample).
            int hold = 1 + (int)std::floor(amt * 63.0f);
            if (hold < 1) hold = 1;

            if (bitcrushSamplesLeft <= 0) {
                // Re-sample and quantize the current signal.
                // Bit depth: 12 bits at low amt, down toward 4 bits at high amt.
                float bits = 4.0f + (1.0f - amt) * 8.0f; // 4..12 bits
                float levels = (float)(1 << (int)(bits + 0.5f)); // fast integer power-of-2
                auto crushSample = [levels](float s) {
                    float v = s * levels;
                    v = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)); // fast round-to-int
                    return v / levels;
                };
                bitcrushHoldL = crushSample(outL);
                bitcrushHoldR = crushSample(outR);
                bitcrushSamplesLeft = hold;
            }

            // Output held (crushed) value and count down.
            outL = bitcrushHoldL;
            outR = bitcrushHoldR;
            --bitcrushSamplesLeft;
        }

        // Formant shaping -- skip entirely when formantAmount is zero
        if (formantAmount > 0.001f)
            processFormant(outL, outR, sampleRate);
#endif // !METAMODULE
    }

private:
    static inline uint32_t hash32(uint32_t x) {
        // Small integer hash (deterministic, cheap)
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static inline float u01(uint32_t x) {
        return (float)(x & 0x00FFFFFFu) / 16777216.0f; // 0..1
    }

    static inline float clamp01(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    static inline float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static inline float lerpf(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static inline float deformLfo(float phase01, float deform) {
        // Sine (use fast approximation — this runs at block rate on MetaModule)
        float sine = phaseon_fast_sin_01(phase01);
        if (deform <= 0.001f) return sine;
        // Triangle: 4*|p-0.5| - 1
        float tri = 4.0f * fabsf(phase01 - 0.5f) - 1.0f;
        if (deform <= 0.333f) {
            float t = deform * 3.0f; // 0..1
            return sine * (1.0f - t) + tri * t;
        }
        // Saw: 2*p - 1
        float saw = 2.0f * phase01 - 1.0f;
        if (deform <= 0.666f) {
            float t = (deform - 0.333f) * 3.0f; // 0..1
            return tri * (1.0f - t) + saw * t;
        }
        // Square: sign of sine
        float sq = (phase01 < 0.5f) ? 1.0f : -1.0f;
        float t = (deform - 0.666f) * 3.0f; // 0..1
        if (t > 1.0f) t = 1.0f;
        return saw * (1.0f - t) + sq * t;
    }

    static inline void vowelFormants(float t01, float& f1, float& f2, float& f3) {
        // 5 targets: A, E, I, O, U (rough, musical—not strict phonetics)
        struct V { float f1, f2, f3; };
        const V v[5] = {
            { 800.0f, 1150.0f, 2900.0f }, // A
            { 500.0f, 1700.0f, 2500.0f }, // E
            { 300.0f, 2200.0f, 3000.0f }, // I
            { 400.0f,  900.0f, 2600.0f }, // O
            { 350.0f,  650.0f, 2400.0f }, // U
        };

        float x = clamp01(t01) * 4.0f;
        int i = (int)x;
        float frac = x - (float)i;
        if (i < 0) { i = 0; frac = 0.0f; }
        if (i >= 4) { i = 4; frac = 0.0f; }
        const V& a = v[i];
        const V& b = v[std::min(4, i + 1)];
        f1 = lerpf(a.f1, b.f1, frac);
        f2 = lerpf(a.f2, b.f2, frac);
        f3 = lerpf(a.f3, b.f3, frac);
    }

    void updateFormantCoeffs(float sampleRate) {
        float sr = std::max(1000.0f, sampleRate);
        float ny = sr * 0.5f;

        float e = clamp01(edgeAmount);
        float m = clamp01(motionAmount);
        float t = clamp01(timbreAmount);
        float inst = clamp01(instability);

        // Nonlinear drive: gentle at low values, ramps up fast when pushed
        float edgeD = e * e * (0.2f + 0.8f * e); // approx powf(e, 2.2)
        float motionD = m * m;
        float extreme = fabsf(2.0f * t - 1.0f); extreme *= extreme * (0.3f + 0.7f * extreme); // approx powf(.., 1.7)
        float instD = inst * inst * (0.4f + 0.6f * inst); // approx powf(inst, 1.6)
        float formD = formantAmount * formantAmount;
        // Dubstep refactor: FORM should be authoritative. Other macros enhance it,
        // but they shouldn't gate it.
        float drive = 0.60f * edgeD + 0.30f * motionD + 0.35f * extreme + 0.20f * instD + 1.25f * formD;
        drive = clamp01(drive);

        float sRaw = 0.07f + 0.78f * drive;
        float strength = clamp01(sRaw);
        // Musical curve: low stays subtle, high becomes character-defining
        strength = strength * strength;
        // Still allow high FORM to speak, but not as overpowering.
        strength = clamp01(strength + 0.16f * formD);
        // Scale by user formant knob (0=off, 1=full natural strength)
        cachedFormantStrength = strength * formantAmount;

        // Partial pitch tracking default ~0.4, can increase when driven
        float track = 0.40f + 0.22f * edgeD + 0.12f * motionD;
        track = clampf(track, 0.0f, 0.80f);

        // Vowel selection uses vowelPos (decoupled from TIMBRE for growl control)
        // LFO Growl modulates the vowel position to create a "talking" or "yoy" effect
        float vp = clamp01(vowelPos);
        
        float macroLfo = clamp01(macroLfoAmount);
        if (macroLfo > 0.001f) {
            float shape = macroLfo * macroLfo;
            // Calculate LFO2 here (not passed in). Formant modulation follows OP 2's
            // LFO2 settings (op index 1), regardless of which operator is being edited.
            constexpr int kFormantOp = 1; // OP 2 (0-based)

            float p2 = lfo2PhaseOp[kFormantOp] + lfoPhaseUser[1][kFormantOp];
            // If Target=ALL, apply the same internal staggering (scaled by Growl).
            // If Target=SELECT OP (edit mode), staggering is disabled.
            if (lfoTargetOp[1] == -1) {
                p2 += ((float)kFormantOp / 6.0f) * macroLfo;
            }
            p2 -= floorf(p2);
            float lfo2 = deformLfo(p2, lfoDeformUser[1][kFormantOp]) * lfoAmpUser[1][kFormantOp];
            
            float lfoGrowl = lfo2 * shape;
            // Modulate vowel position by up to +/- 0.5 (half the vowel space)
            vp = clamp01(vp + lfoGrowl * 0.5f);
        }
        
        float f1, f2, f3;
        vowelFormants(vp, f1, f2, f3);

        // Apply per-note organic offset
        f1 *= formantRandMul;
        f2 *= formantRandMul;
        f3 *= formantRandMul;

        // Tracking: blend absolute-ish formants with pitch-relative motion
        float refHz = 110.0f;
        float ratio = std::max(0.25f, fundamentalHz / refHz);
        float ptrack = 1.0f + (ratio - 1.0f) * track; // linear approx of powf(ratio, track)
        f1 *= ptrack;
        f2 *= ptrack;
        f3 *= ptrack;

        // Pitch safety: reduce max strength/Q as we approach Nyquist
        float pitchNorm = clamp01(fundamentalHz / (ny * 0.35f));
        float strengthPitchAtten = 1.0f - 0.65f * pitchNorm * (0.5f + 0.5f * pitchNorm); // approx powf(pitchNorm, 1.5)
        if (strengthPitchAtten < 0.25f) strengthPitchAtten = 0.25f;

        // Q driven by vowelPos (not timbreAmount) for independent growl control
        float qWide = 0.75f;
        float qNarrow = 9.0f;
        float q = lerpf(qWide, qNarrow, vp * (0.5f + 0.5f * vp)); // approx powf(vp, 1.25)
        // Extra contrast when FORM is high (more vowel definition), but significantly tamer now.
        q *= (1.0f + 0.25f * formD);

        // Base gain in dB (stronger and more contrasty at high FORM)
        float gBase = lerpf(1.0f, 10.5f, strength);
        float gHot  = 4.0f * formD; // extra vowel bite when FORM is pushed
        float gMax = (gBase + gHot) * strengthPitchAtten;
        float g1 = gMax * 1.00f;
        float g2 = gMax * 0.85f;
        float g3 = gMax * 0.70f;

        // Family-dependent scaling: Hybrid stays a bit more glue by default
        if (wtFamily == 2) {
            g1 *= 0.80f;
            g2 *= 0.85f;
            g3 *= 0.90f;
        }

        // Per-band clamp vs center frequency proximity to Nyquist
        auto attenHigh = [ny](float fc) {
            float hf = fc / ny; // 0..1
            float t = (hf - 0.20f) / 0.35f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            // At very high frequencies, strongly clamp resonance and gain
            return 1.0f - 0.85f * t;
        };

        float a1 = attenHigh(f1);
        float a2 = attenHigh(f2);
        float a3 = attenHigh(f3);
        g1 *= a1;
        g2 *= a2;
        g3 *= a3;

        // Clamp Q harder at high centers
        float q1 = std::min(q, lerpf(10.0f, 2.2f, 1.0f - a1));
        float q2 = std::min(q, lerpf(10.0f, 2.2f, 1.0f - a2));
        float q3 = std::min(q, lerpf(10.0f, 2.2f, 1.0f - a3));

        // Slight tilt interaction: bright tilt reduces boost a bit (avoids brittle highs)
        float tilt = clampf(tiltAmount, -1.0f, 1.0f);
        // Dubstep: keep formant bite even when tilt is bright.
        float tiltGain = (tilt > 0.0f) ? (1.0f - 0.12f * tilt) : (1.0f + 0.06f * (-tilt));
        g1 *= tiltGain;
        g2 *= tiltGain;
        g3 *= tiltGain;

        for (int ch = 0; ch < 2; ++ch) {
            Biquad* bq = (ch == 0) ? formantL : formantR;
            bq[0].setPeaking(sr, f1, q1, g1);
            bq[1].setPeaking(sr, f2, q2, g2);
            bq[2].setPeaking(sr, f3, q3, g3);
        }

        // Cache formant saturation normalization
        float hot = clamp01(formantAmount);
        if (hot > 0.001f) {
            float d = 1.0f + 3.0f * hot * hot;
            cachedFormantTanhNorm = 1.0f / phaseon_fast_tanh(d);
        } else {
            cachedFormantTanhNorm = 1.0f;
        }
    }

    void processFormant(float& l, float& r, float sampleRate) {
        // Update coefficients at a relaxed control rate (/64 = ~750Hz at 48kHz)
        if (++formantDivider >= 64) {
            formantDivider = 0;
            updateFormantCoeffs(sampleRate);
        }

        // If strength is extremely low, skip to save cycles
        if (cachedFormantStrength < 0.0025f) return;

        float xl = l;
        float xr = r;
        // 3 gentle peaking bands in series
        for (int i = 0; i < 3; ++i) {
            xl = formantL[i].process(xl);
            xr = formantR[i].process(xr);
        }

        // Controlled aggression: a touch of saturation on the formant-processed signal.
        // Keeps vowel peaks biting without requiring runaway EQ gain.
        float hot = clamp01(formantAmount);
        if (hot > 0.001f) {
            float d = 1.0f + 3.0f * hot * hot;
            float mix = 0.05f + 0.16f * hot; // ~5%..21%
            float norm = cachedFormantTanhNorm;
            auto sat = [&](float x) {
                return phaseon_fast_tanh(x * d) * norm;
            };
            xl = xl * (1.0f - mix) + sat(xl) * mix;
            xr = xr * (1.0f - mix) + sat(xr) * mix;
        }
        l = xl;
        r = xr;
    }

    void generateRolePositions() {
        float style = std::max(0.0f, std::min(4.0f, envStyle));
        float spread = std::max(0.0f, std::min(1.0f, envSpread));
        float bias = std::max(-1.0f, std::min(1.0f, roleBias));

        // Bias shifts the mean role coordinate: -1 => transient-heavy (toward 0), +1 => sustain/loop-heavy (toward 4)
        float biasShift = bias * 0.85f;

        // Spread sets how far operators deviate from the current style.
        // Keep a meaningful baseline deviation so SHUF is audible even at low spread.
        float maxDev = 0.90f + 2.20f * spread; // ~0.90..3.10

        for (int i = 0; i < kNumOps; ++i) {
            uint32_t h = hash32(roleSeed ^ (uint32_t)(0x9E3779B9u * (uint32_t)(i + 1)));
            float r = u01(h) * 2.0f - 1.0f; // -1..+1
            float pos = style + biasShift + r * maxDev;
            if (pos < 0.0f) pos = 0.0f;
            if (pos > 4.0f) pos = 4.0f;
            opRolePos[i] = pos;
        }

        // Pitch anchor: ensure at least one carrier-ish operator remains in the Stab/Lead zone.
        // (Uses the current algorithm's carrier map.)
        const Algorithm& algo = getAlgorithm(algorithmIndex);
        bool hasAnchor = false;
        int firstCarrier = -1;
        for (int i = 0; i < kNumOps; ++i) {
            if (!algo.isCarrier[i]) continue;
            if (firstCarrier < 0) firstCarrier = i;
            float p = opRolePos[i];
            if (p >= 0.9f && p <= 2.6f) {
                hasAnchor = true;
                break;
            }
        }
        if (!hasAnchor && firstCarrier >= 0) {
            opRolePos[firstCarrier] = 2.0f;
        }

        // Limit loop-heavy roles unless bias is strongly positive.
        int loopCount = 0;
        int maxLoops = (bias > 0.45f || style > 3.2f) ? 2 : 1;
        for (int i = 0; i < kNumOps; ++i) {
            if (opRolePos[i] > 3.35f) loopCount++;
        }
        if (loopCount > maxLoops) {
            for (int i = 0; i < kNumOps && loopCount > maxLoops; ++i) {
                if (opRolePos[i] > 3.35f) {
                    opRolePos[i] = 2.2f; // steer extra loops back toward Lead
                    loopCount--;
                }
            }
        }

        // If the user is clearly asking for GrowlLoop, guarantee at least one modulator gets it.
        if (style > 3.45f) {
            int chosen = -1;
            for (int i = 0; i < kNumOps; ++i) {
                if (!algo.isCarrier[i]) { chosen = i; break; }
            }
            if (chosen >= 0) {
                opRolePos[chosen] = 4.0f;
            }
        }
    }

    void retargetChaos(bool hardReset) {
        float chaos = std::max(0.0f, std::min(1.0f, chaosAmount));
        for (int i = 0; i < kNumOps; ++i) {
            // Zone personality: low WT = stable, high WT = chaos exaggeration
            float fp = ops[i].framePos;
            float zone;
            if (fp < 0.33f) zone = 0.35f + fp * 1.0f;
            else if (fp < 0.66f) zone = 0.85f;
            else zone = 0.95f + (fp - 0.66f) * 1.8f;

            // Sweet-spot bounded target. Keep it musical and not too wide.
            float r = cheapRandom(); // -1..+1
            float depth = (0.20f + 0.65f * chaos) * zone;
            float t = r * depth;
            chaosTarget[i] = std::max(-0.90f, std::min(1.20f, t));
            if (hardReset)
                chaosValue[i] = chaosTarget[i];
        }
    }

    // Cheap random: xorshift32, returns -1..+1
    float cheapRandom() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float)(rngState & 0xFFFF) / 32768.0f - 1.0f;
    }
};

} // namespace phaseon
