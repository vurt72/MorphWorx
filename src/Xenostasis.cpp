/*

 * Xenostasis -- Autonomous hybrid synthesis organism
 *
 * Self-regulating spectral drone with wavetable + organic bytebeat
 * cross-modulation and an internal metabolism system.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary -- not licensed under GPL or any open-source license.
 */

#include "plugin.hpp"

#include <new>
#include <array>
#include <cmath>

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#endif

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// Constants
// ============================================================================

static constexpr int XS_NUM_TABLES    = 8;
#ifdef METAMODULE
// Reduced dimensions for MetaModule to cut wavetable bank from 4MB to 1MB,
// halving L1/L2 cache pressure on Cortex-A7.
static constexpr int XS_FRAMES        = 32;
static constexpr int XS_FRAME_SIZE    = 1024;
#else
static constexpr int XS_FRAMES        = 64;
static constexpr int XS_FRAME_SIZE    = 2048;
#endif
static constexpr int XS_CONTROL_RATE  = 32;  // process metabolism every N samples

// ============================================================================
// Wavetable data container
// ============================================================================

struct XsWavetableBank {
    // tables[table][frame][sample]
    float data[XS_NUM_TABLES][XS_FRAMES][XS_FRAME_SIZE];

    void generate() {
        generateDarkConsonant(0);
        generateHollowResonant(1);
        generateDenseOrganicMass(2);
        generateSubstrate(3);
        generatePercussiveStrike(4);
        generateHarshNoise(5);
        generateArcadeFX(6);
        generateSpectralClusters(7);
    }

#ifdef METAMODULE
    // MetaModule: fast wavetable bank init to avoid watchdog/alloc issues during plugin scan.
    // This keeps Xenostasis functional for CPU testing without spending seconds generating
    // expensive procedural tables at startup.
    void generateMetaFast() {
        static bool inited = false;
        static std::array<float, XS_FRAME_SIZE> sSine;
        static std::array<float, XS_FRAME_SIZE> sTri;
        static std::array<float, XS_FRAME_SIZE> sSaw;
        static std::array<float, XS_FRAME_SIZE> sSq;

        if (!inited) {
            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;
                float u = 2.f * t - 1.f;
                sSine[s] = std::sin(2.f * (float)M_PI * t);
                sSaw[s] = u;
                sTri[s] = 1.f - 2.f * std::fabs(u);
                sSq[s] = (t < 0.5f) ? 1.f : -1.f;
            }
            inited = true;
        }

        for (int table = 0; table < XS_NUM_TABLES; table++) {
            for (int f = 0; f < XS_FRAMES; f++) {
                float p = (float)f / (float)(XS_FRAMES - 1);
                for (int s = 0; s < XS_FRAME_SIZE; s++) {
                    float t = (float)s / (float)XS_FRAME_SIZE;
                    float u = 2.f * t - 1.f;
                    float v = 0.f;

                    switch (table) {
                    case 0: {
                        // Bright asym tri->saw morph
                        float a = sTri[s] * (1.f - p) + sSaw[s] * p;
                        v = a + 0.25f * a * a * a;
                        break;
                    }
                    case 1: {
                        // Hollow-ish sine with weak 3rd
                        float a = sSine[s];
                        float b = sSine[(s * 3) & (XS_FRAME_SIZE - 1)] * (0.18f + 0.12f * p);
                        v = a * 0.92f + b;
                        break;
                    }
                    case 2: {
                        // Dense harmonic-ish: saw + soft clip
                        float a = sSaw[s] * (0.85f + 0.25f * p);
                        v = a / (1.f + 0.75f * std::fabs(a));
                        break;
                    }
                    case 3: {
                        // Substrate: sine + sub + slight asym
                        float a = sSine[s] * 0.85f;
                        float sub = sSine[(s / 2) & (XS_FRAME_SIZE - 1)] * (0.12f + 0.10f * (1.f - p));
                        v = a + sub;
                        v += 0.12f * v * v * v;
                        break;
                    }
                    case 4: {
                        // Strike-ish: squared sine (adds upper harm) morph
                        float a = sSine[s];
                        float b = (a >= 0.f ? 1.f : -1.f) * (a * a);
                        v = a * (1.f - 0.55f * p) + b * (0.55f * p);
                        break;
                    }
                    case 5: {
                        // Harsh: pulse + fold-ish
                        float a = sSq[s] * (0.75f + 0.25f * p) + sSaw[s] * (0.25f + 0.15f * p);
                        float folded = std::sin(a * (2.2f + 4.0f * p));
                        v = a * 0.35f + folded * 0.65f;
                        break;
                    }
                    case 6: {
                        // FX: stepped triangle + slight duty modulation
                        float a = sTri[s];
                        float q = 8.f + p * 24.f;
                        v = std::floor(a * q + 0.5f) / q;
                        if (u > (0.15f + 0.25f * p)) v *= -1.f;
                        break;
                    }
                    case 7: {
                        // Chaos-lite: deterministic logistic AM + phase bend (no heavy trig)
                        float x = 0.2f + 0.6f * p;
                        // A few iterations to push into chaotic region
                        for (int k = 0; k < 3; k++) x = (3.75f + 0.20f * p) * x * (1.f - x);
                        float am = (2.f * x - 1.f);
                        float bend = u + am * 0.55f;
                        v = bend / (1.f + 0.9f * std::fabs(bend));
                        break;
                    }
                    }

                    data[table][f][s] = v;
                }

                // DC removal + normalize
                float mean = 0.f;
                for (int s = 0; s < XS_FRAME_SIZE; s++) mean += data[table][f][s];
                mean /= (float)XS_FRAME_SIZE;
                for (int s = 0; s < XS_FRAME_SIZE; s++) data[table][f][s] -= mean;
                normalizeFrame(table, f);
            }
        }
    }
#endif

private:
    // Helper: fill one table via additive synthesis
    void addHarmonic(int table, int frame, int harmonic, float amplitude, float phaseOffset) {
        for (int s = 0; s < XS_FRAME_SIZE; s++) {
            float t = (float)s / (float)XS_FRAME_SIZE;
            data[table][frame][s] += amplitude * std::sin(2.f * M_PI * harmonic * t + phaseOffset);
        }
    }

    // Table 0: Feral Machine -- wild, metallic, noisy, experimental
    // Combines inharmonic clusters, noise bursts, ring-mod textures, FM screech
    // Each frame is a radically different metallic/noise character
    void generateDarkConsonant(int table) {
        uint32_t rng = 0xDEAD1337u;
        auto xorshift = [&]() -> float {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float)(rng & 0xFFFF) / 32768.f - 1.f;
        };
        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            float framePos = (float)f / (float)(XS_FRAMES - 1);
            int mode = f % 8;
            rng ^= (uint32_t)(f * 13337 + 54321);
            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;
                float v = 0.f;
                switch (mode) {
                case 0: { // Inharmonic metal cluster
                    float ratios[] = {1.f, 1.59f, 2.83f, 4.17f, 5.43f, 7.11f};
                    for (int i = 0; i < 6; i++) {
                        float amp = 0.5f / (1.f + (float)i * 0.4f);
                        float detune = 1.f + (framePos - 0.5f) * 0.02f * (float)i;
                        v += amp * std::sin(2.f * (float)M_PI * t * ratios[i] * detune);
                    }
                    break;
                }
                case 1: { // Ring-mod screech
                    float carrier = std::sin(2.f * (float)M_PI * t * 1.f);
                    float modFreq = 3.71f + framePos * 8.f;
                    float mod = std::sin(2.f * (float)M_PI * t * modFreq);
                    v = carrier * mod;
                    v = std::tanh(v * (2.f + framePos * 3.f));
                    break;
                }
                case 2: { // Filtered noise burst with resonance
                    float noise = xorshift();
                    float resonFreq = 2.f + framePos * 10.f;
                    v = noise * std::sin(2.f * (float)M_PI * t * resonFreq) * 0.7f;
                    v += std::sin(2.f * (float)M_PI * t * 1.f) * 0.3f;  // sub anchor
                    break;
                }
                case 3: { // FM growl (high index)
                    float modIdx = 6.f + framePos * 12.f;
                    float modSig = std::sin(2.f * (float)M_PI * t * 3.17f) * modIdx;
                    v = std::sin(2.f * (float)M_PI * t * 1.f + modSig);
                    v = std::tanh(v * 2.f);
                    break;
                }
                case 4: { // Wavefolder cascade
                    float sine = std::sin(2.f * (float)M_PI * t * 1.f);
                    float amount = 3.f + framePos * 8.f;
                    v = std::sin(sine * amount);
                    v = std::sin(v * (2.f + framePos * 3.f));  // double fold
                    break;
                }
                case 5: { // Broken oscillator (sub-octave + noise)
                    float sub = (std::sin(2.f * (float)M_PI * t * 0.5f) > 0.f) ? 1.f : -1.f;
                    float hiNoise = xorshift() * (0.3f + framePos * 0.7f);
                    v = sub * 0.5f + hiNoise * 0.5f;
                    v += std::sin(2.f * (float)M_PI * t * 5.73f) * 0.3f;
                    break;
                }
                case 6: { // Harsh sync-like harmonic explosion
                    float master = std::fmod(t * 1.f, 1.f);
                    float slaveRatio = 3.5f + framePos * 8.f;
                    v = std::sin(2.f * (float)M_PI * master * slaveRatio);
                    v *= (1.f - master * 0.3f);  // decay within cycle
                    break;
                }
                case 7: { // Granular metallic texture
                    float grain1 = std::sin(2.f * (float)M_PI * t * 1.41f) * std::sin(2.f * (float)M_PI * t * 7.07f);
                    float grain2 = std::sin(2.f * (float)M_PI * t * 2.23f) * std::cos(2.f * (float)M_PI * t * 11.3f);
                    v = grain1 * 0.5f + grain2 * 0.5f;
                    v = std::tanh(v * (2.f + framePos * 4.f));
                    break;
                }
                }
                data[table][f][s] = v;
            }
            normalizeFrame(table, f);
        }
    }

    // Table 1: Hollow Resonant -- breathy, formant-like
    // Peaks at harmonics 3, 7, 11 with resonant bumps
    void generateHollowResonant(int table) {
        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            float framePos = (float)f / (float)(XS_FRAMES - 1);
            for (int h = 1; h <= 24; h++) {
                float amp = 0.1f / (float)h;
                // Resonant peaks
                float peak3 = std::exp(-0.5f * (float)((h - 3) * (h - 3)));
                float peak7 = std::exp(-0.5f * (float)((h - 7) * (h - 7)));
                float peak11 = std::exp(-0.5f * (float)((h - 11) * (h - 11)));
                float peaks = (peak3 + peak7 + peak11) * 0.5f;
                // Peaks shift with frame
                float shift = framePos * 2.f;
                float peak3s = std::exp(-0.5f * (float)((h - (3 + shift)) * (h - (3 + shift))));
                float peak7s = std::exp(-0.5f * (float)((h - (7 + shift)) * (h - (7 + shift))));
                float peak11s = std::exp(-0.5f * (float)((h - (11 + shift)) * (h - (11 + shift))));
                float peaksShifted = (peak3s + peak7s + peak11s) * 0.5f;
                amp += peaks * (1.f - framePos) + peaksShifted * framePos;
                addHarmonic(table, f, h, amp, framePos * 0.5f);
            }
            normalizeFrame(table, f);
        }
    }

    // Table 2: Abyssal Alloy -- chaotic metallic with immense sub
    // Frames are intentionally less smoothly related (to avoid "motorboat" morph feel).
    // Strong fundamental/sub-like component + inharmonic metallic clusters + controlled noise.
    void generateDenseOrganicMass(int table) {
        auto hash32 = [&](uint32_t x) -> uint32_t {
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return x;
        };

        uint32_t rng = 0xA11E011Au;
        auto rand01 = [&]() -> float {
            rng = hash32(rng);
            return (float)(rng & 0xFFFFu) / 65535.f;
        };
        auto randBip = [&]() -> float {
            return rand01() * 2.f - 1.f;
        };

        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));

            float framePos = (float)f / (float)(XS_FRAMES - 1);
            int mode = f % 8;
            rng ^= hash32((uint32_t)f * 0x9E3779B9u + 0xBEEF1234u);

            // Randomized per-frame character knobs (deterministic)
            float chaosA = rand01();
            float chaosB = rand01();
            float chaosC = rand01();

            // --- Sub/fundamental anchor (huge low harmonic energy) ---
            // Note: the wavetable fundamental is the playback pitch.
            // Add a half-cycle sine for a sub-like weight (slope discontinuity but no click at wrap).
            float subAmt = 0.55f + 0.35f * (1.f - std::fabs(framePos - 0.5f) * 2.f);
            float fundAmt = 0.85f;
            float octAmt = 0.25f + 0.15f * chaosA;

            // --- Metallic layer parameters ---
            // Use a mix of near-harmonics and inharmonics for clang.
            float inharm1 = 1.41f + chaosB * 5.7f;
            float inharm2 = 2.09f + chaosC * 9.3f;
            float fmRatio = 2.0f + (float)(mode % 5) * 0.73f + chaosA * 2.0f;
            float fmIndex = 2.0f + chaosB * 12.0f + framePos * 6.0f;
            float ringRatio = 3.0f + (float)(mode % 3) * 2.0f + chaosC * 6.0f;

            // --- Periodic noise: filtered random per-sample (repeats each cycle) ---
            float noiseLP = 0.f;
            float noiseHP = 0.f;
            float prevNoiseLP = 0.f;
            float noiseCoeff = 0.004f + (0.08f + chaosB * 0.25f) * (0.25f + framePos);
            noiseCoeff = clamp(noiseCoeff, 0.004f, 0.35f);
            float noiseAmt = 0.04f + 0.22f * framePos;
            if (mode == 6 || mode == 7) noiseAmt += 0.10f;

            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;

                // Sub + fundamental
                float fund = std::sin(2.f * (float)M_PI * t);
                float oct = std::sin(2.f * (float)M_PI * 2.f * t);
                float subHalf = std::sin((float)M_PI * t);
                float base = fund * fundAmt + oct * octAmt + subHalf * subAmt;

                // Metallic components
                float mA = std::sin(2.f * (float)M_PI * t * inharm1);
                float mB = std::sin(2.f * (float)M_PI * t * inharm2 + mA * (1.0f + chaosA * 2.0f));

                float fm = std::sin(2.f * (float)M_PI * t + std::sin(2.f * (float)M_PI * t * fmRatio) * fmIndex);
                float ring = std::sin(2.f * (float)M_PI * t * ringRatio);

                float metal = 0.f;
                switch (mode) {
                default:
                case 0: // clang additive
                    metal = 0.55f * mA + 0.45f * mB;
                    break;
                case 1: // FM bell/grind
                    metal = fm;
                    break;
                case 2: // ring-mod shimmer
                    metal = fm * ring;
                    break;
                case 3: // hard sync-ish
                    metal = std::sin(2.f * (float)M_PI * std::fmod(t * (2.0f + chaosC * 10.0f), 1.f) * (3.0f + chaosA * 6.0f));
                    break;
                case 4: // wavefolded metal
                    metal = std::sin((mA + fm * 0.7f) * (3.5f + chaosB * 10.f));
                    break;
                case 5: // bitmask-y edge
                    metal = std::tanh((mB * 1.7f + fm * 0.9f) * (2.0f + chaosC * 4.f));
                    break;
                case 6: // noise-metal hybrid
                    metal = (mA * 0.4f + fm * 0.6f) * (0.6f + 0.4f * ring);
                    break;
                case 7: // extreme alloy
                    metal = std::sin((fm + mB) * (5.0f + chaosA * 14.f)) * (0.7f + 0.3f * ring);
                    break;
                }

                // Noise layer
                float n = randBip();
                noiseLP += noiseCoeff * (n - noiseLP);
                noiseHP = noiseLP - prevNoiseLP;
                prevNoiseLP = noiseLP;

                // Combine: keep sub huge, metal chaotic, noise as texture
                float x = base + metal * (0.55f + framePos * 0.55f) + noiseHP * noiseAmt;

                // Gentle DC removal per-sample (very small), keeps extremes controlled
                x = x - (x * 0.02f);

                // Saturate + compress: preserves sub while adding bite
                float drive = 1.2f + framePos * 2.2f + chaosB * 1.5f;
                x = std::tanh(x * drive);
                // Extra fold for metallic edges
                if (mode >= 4) x = std::sin(x * (2.0f + chaosC * 8.0f));

                data[table][f][s] = x;
            }

            normalizeFrame(table, f);
        }
    }

    // Table 3: Substrate -- sub-heavy anchor
    // Very strong fundamental + octave, rapid rolloff above 5
    void generateSubstrate(int table) {
        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            float framePos = (float)f / (float)(XS_FRAMES - 1);
            for (int h = 1; h <= 16; h++) {
                float amp;
                if (h == 1)      amp = 1.0f;
                else if (h == 2) amp = 0.7f;
                else if (h <= 5) amp = 0.2f / (float)h;
                else             amp = 0.03f / (float)h;

                float frameMod = 1.f + 0.1f * std::sin(framePos * M_PI + (float)h * 0.9f);
                addHarmonic(table, f, h, amp * frameMod, framePos * 0.3f * (float)h);
            }
            normalizeFrame(table, f);
        }
    }

    // Table 4: Submerged Monolith -- oppressive, alien, cinematic
    // 64-frame morph, 2048 samples each. Starts near-sine and gradually introduces
    // inharmonics (irrational ratios), phase-warp/asymmetry, and subtle fold/FM.
    // Avoids white noise: uses controlled band-limited "noise clusters" via dense
    // partial clouds with random phases.
    void generatePercussiveStrike(int table) {
        uint32_t rng = 0xC1AE3471u;
        auto xorshift01 = [&]() -> float {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float)(rng & 0xFFFFu) / 65535.f;
        };
		auto smooth01 = [&](float x) -> float {
			x = clamp(x, 0.f, 1.f);
			return x * x * (3.f - 2.f * x);
		};

        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            float framePos = (float)f / (float)(XS_FRAMES - 1);

            // Stable, sub-heavy base that remains throughout the morph
            float baseFund = 1.0f;
            float baseAmp = 0.98f;
            float subWeight = 0.10f + 0.18f * smooth01(framePos * 1.2f);
            float lowMidWeight = 0.05f + 0.35f * smooth01(framePos);  // ramps density in low mids

            // Inharmonic ratios (irrational-ish)
            const float r1 = 1.37f;
            const float r2 = 1.618f;
            const float r3 = 2.71f;

            // Controlled "noise clusters" (band-limited partial clouds)
            // Centered around low-mid harmonics (roughly 200–800Hz for typical pitches)
            float clusterCenter = 2.8f + framePos * 6.0f + std::sin(framePos * 2.f * (float)M_PI) * 0.35f;
            float clusterWidth = 1.2f + framePos * 1.8f;

            // Phase warp and asymmetry grow with frame
            float warpAmt = 0.00f + 0.18f * std::pow(framePos, 1.5f);
            int warpK1 = 2 + (f % 5);
            int warpK2 = 5 + (f % 7);
            float asymAmt = 0.00f + 0.55f * std::pow(framePos, 1.2f);

            // Late-frame artifacts
            float late = smooth01((framePos - 0.62f) / 0.38f);
            float foldAmt = late * (0.0f + 0.40f);
            float fmAmt = late * (0.0f + 0.55f);

            // Deterministic per-frame random phases for clusters
            float phA = (xorshift01() * 2.f - 1.f) * (float)M_PI;
            float phB = (xorshift01() * 2.f - 1.f) * (float)M_PI;
            float phC = (xorshift01() * 2.f - 1.f) * (float)M_PI;

            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;

                // Phase warp (kept periodic by using integer-rate modulators)
                float warp = t;
                warp += warpAmt * std::sin(2.f * (float)M_PI * (float)warpK1 * t + phA);
                warp += warpAmt * 0.35f * std::sin(2.f * (float)M_PI * (float)warpK2 * t + phB);
                warp = warp - std::floor(warp);

                float carrier = std::sin(2.f * (float)M_PI * baseFund * warp);
                float x = carrier * baseAmp;

                // Sub-heavy reinforcement without breaking the "near-sine" start
                x += std::sin(2.f * (float)M_PI * 2.f * warp) * (subWeight * 0.6f);
                x += std::sin(2.f * (float)M_PI * 3.f * warp) * (subWeight * 0.25f);

                // Gradually introduce inharmonic partials (irrational ratios)
                float inharmAmt = std::pow(framePos, 1.25f) * 0.65f;
                float inharm = 0.f;
                inharm += std::sin(2.f * (float)M_PI * (r1 * t) + phA) * 0.55f;
                inharm += std::sin(2.f * (float)M_PI * (r2 * t) + phB) * 0.45f;
                inharm += std::sin(2.f * (float)M_PI * (r3 * t) + phC) * 0.35f;
                x += inharm * inharmAmt;

                // Low-mid density via partial cloud (band-limited "noise cluster")
                // No per-sample RNG, only random phases: periodic and controlled.
                float cloud = 0.f;
                for (int h = 2; h <= 20; h++) {
                    float hh = (float)h;
                    float d = (hh - clusterCenter) / std::max(0.6f, clusterWidth);
                    float env = std::exp(-0.5f * d * d);
                    float ph = (float)h * 0.37f + phA * 0.3f + phB * 0.2f;
                    cloud += env * std::sin(2.f * (float)M_PI * hh * warp + ph);
                }
                x += cloud * lowMidWeight * 0.10f;

                // Asymmetry / throat pressure (adds odd harmonics)
                x += asymAmt * 0.22f * x * x * x;

                // Late subtle FM + wavefolding artifacts
                if (late > 0.001f) {
                    float mod = std::sin(2.f * (float)M_PI * (r2 * t) + phC) * (2.0f + 9.0f * fmAmt);
                    float fm = std::sin(2.f * (float)M_PI * warp + mod);
                    x = x * (1.f - fmAmt * 0.25f) + fm * (fmAmt * 0.25f);
                    float folded = std::sin(x * (1.2f + foldAmt * 5.0f));
                    x = x * (1.f - foldAmt) + folded * foldAmt;
                }

                // Cinematic clamp
                x = std::tanh(x * (1.1f + framePos * 1.8f));
                data[table][f][s] = x;
            }

            // Endpoint smoothing to keep the irrational inharmonics from clicking at wrap.
            // Crossfade a small region between start/end.
            const int xf = 96;
            for (int i = 0; i < xf; i++) {
                float a = (float)i / (float)(xf - 1);
                float s0 = data[table][f][i];
                float s1 = data[table][f][XS_FRAME_SIZE - xf + i];
                float m = s0 * (1.f - a) + s1 * a;
                data[table][f][i] = m;
                data[table][f][XS_FRAME_SIZE - xf + i] = m;
            }

            // Remove DC
            float mean = 0.f;
            for (int s = 0; s < XS_FRAME_SIZE; s++) mean += data[table][f][s];
            mean /= (float)XS_FRAME_SIZE;
            for (int s = 0; s < XS_FRAME_SIZE; s++) data[table][f][s] -= mean;

            normalizeFrame(table, f);
        }
    }

    // Table 5: Harsh Noise -- distorted, aggressive, drum-optimized
    // Each frame is a radically different noise/distortion character
    // No smooth morphing -- frames are independent harsh textures
    void generateHarshNoise(int table) {
        uint32_t rng = 0xBADF00Du;
        auto xorshift = [&]() -> float {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float)(rng & 0xFFFF) / 32768.f - 1.f;
        };
        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            int mode = f % 8;
            rng ^= (uint32_t)(f * 77777 + 12345);
            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;
                float v = 0.f;
                switch (mode) {
                case 0: { // Hard-clipped square burst
                    v = std::sin(2.f * (float)M_PI * t * (1.f + f * 0.3f));
                    v = (v > 0.f) ? 1.f : -1.f;
                    v *= (1.f - t * 0.5f);
                    break;
                }
                case 1: { // Bitcrushed sine
                    float sine = std::sin(2.f * (float)M_PI * t * 3.f);
                    int bits = 2 + (f / 8);
                    float quant = (float)(1 << bits);
                    v = std::floor(sine * quant) / quant;
                    break;
                }
                case 2: { // Ring-modulated harsh
                    v = std::sin(2.f * (float)M_PI * t * 1.f) * std::sin(2.f * (float)M_PI * t * 7.33f);
                    v = std::tanh(v * 4.f);
                    break;
                }
                case 3: { // Noise burst
                    v = xorshift();
                    float nfreq = 2.f + (float)f * 0.5f;
                    v *= std::sin(2.f * (float)M_PI * t * nfreq);
                    break;
                }
                case 4: { // FM distortion
                    float mod = std::sin(2.f * (float)M_PI * t * (5.f + f * 0.2f));
                    v = std::sin(2.f * (float)M_PI * t * 1.f + mod * (3.f + f * 0.1f));
                    v = std::tanh(v * 3.f);
                    break;
                }
                case 5: { // Wavefolder
                    float sine = std::sin(2.f * (float)M_PI * t * 2.f);
                    float fold = sine * (2.f + f * 0.15f);
                    v = std::sin(fold);
                    break;
                }
                case 6: { // Absolute value harshness
                    v = std::fabs(std::sin(2.f * (float)M_PI * t * 3.f)) * 2.f - 1.f;
                    v += std::fabs(std::sin(2.f * (float)M_PI * t * 5.17f)) * 0.5f;
                    v = std::tanh(v * 2.f);
                    break;
                }
                case 7: { // Digital noise + sub
                    float sub = std::sin(2.f * (float)M_PI * t);
                    float noise = xorshift() * (1.f - t);
                    v = sub * 0.5f + noise * 0.8f;
                    break;
                }
                }
                data[table][f][s] = v;
            }
            normalizeFrame(table, f);
        }
    }

    // Table 6: Arcade FX -- lasers, bleeps, zaps, digital chaos
    // Each frame is a completely different sound effect
    // Designed for unpredictable, sudden, harsh character
    void generateArcadeFX(int table) {
        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            int mode = f % 10;
            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                float t = (float)s / (float)XS_FRAME_SIZE;
                float v = 0.f;
                switch (mode) {
                case 0: { // Descending laser sweep
                    float sweepFreq = 20.f * (1.f - t * 0.9f);
                    v = std::sin(2.f * (float)M_PI * t * sweepFreq);
                    v = (v > 0.f) ? 1.f : -1.f;
                    break;
                }
                case 1: { // Rising laser zap
                    float sweepFreq = 2.f + t * 30.f;
                    v = std::sin(2.f * (float)M_PI * t * sweepFreq);
                    break;
                }
                case 2: { // Bit-pattern bleep
                    int pattern = (int)(t * 256.f);
                    v = ((pattern ^ (pattern >> 3) ^ (pattern >> 5)) & 1) ? 1.f : -1.f;
                    break;
                }
                case 3: { // Exponential chirp
                    float chirp = std::exp(t * 4.f) * 0.5f;
                    v = std::sin(2.f * (float)M_PI * chirp);
                    v = std::tanh(v * 3.f);
                    break;
                }
                case 4: { // Dual-tone interference
                    v = std::sin(2.f * (float)M_PI * t * 8.f) + std::sin(2.f * (float)M_PI * t * 11.3f);
                    v = std::tanh(v * 2.f);
                    break;
                }
                case 5: { // PWM pulse with duty sweep
                    float duty = 0.1f + t * 0.8f;
                    float phase = std::fmod(t * 6.f, 1.f);
                    v = (phase < duty) ? 1.f : -1.f;
                    break;
                }
                case 6: { // Bytebeat fragment
                    int bt = (int)(t * 8000.f) + f * 200;
                    int raw = ((bt >> 4) | (bt << 1)) ^ (bt >> 8);
                    v = (float)(raw & 0xFF) / 128.f - 1.f;
                    break;
                }
                case 7: { // Ring-mod alien
                    v = std::sin(2.f * (float)M_PI * t * 3.f) * std::cos(2.f * (float)M_PI * t * 13.7f);
                    v += std::sin(2.f * (float)M_PI * t * 0.5f) * 0.3f;
                    break;
                }
                case 8: { // Sample-rate reduction
                    float srDiv = 4.f + (float)f * 0.5f;
                    float quantT = std::floor(t * XS_FRAME_SIZE / srDiv) * srDiv / XS_FRAME_SIZE;
                    v = std::sin(2.f * (float)M_PI * quantT * 5.f);
                    break;
                }
                case 9: { // Metallic zap (descending FM)
                    float modDepth = 8.f * (1.f - t);
                    float mod = std::sin(2.f * (float)M_PI * t * 12.f) * modDepth;
                    v = std::sin(2.f * (float)M_PI * t * 2.f + mod);
                    break;
                }
                }
                data[table][f][s] = v;
            }
            normalizeFrame(table, f);
        }
    }

    // Table 7: Alien Physics Collapse -- chaos modes
    // 64 frames, 2048 samples. 8 radically different chaos-based synthesis modes.
    // Avoids per-sample RNG (no white noise); instability comes from nonlinear maps,
    // recursive phase injection, fractal warping, and nonlinear phase lensing.
    // Allows discontinuities and asymmetry.
    void generateSpectralClusters(int table) {
        uint32_t rng = 0xC0A1A7E7u;
        auto xorshift01 = [&]() -> float {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float)(rng & 0xFFFFu) / 65535.f;
        };
        auto randBip = [&]() -> float {
            return xorshift01() * 2.f - 1.f;
        };
        auto fract01 = [&](float x) -> float {
            return x - std::floor(x);
        };
        auto lens = [&](float x, float g) -> float {
            // Nonlinear phase lensing / waveshaping with controllable asymmetry.
            // g in ~[0.5..6].
            float a = std::atan(g);
            if (a < 1e-6f) return x;
            return std::atan(x * g) / a;
        };

        for (int f = 0; f < XS_FRAMES; f++) {
            std::memset(data[table][f], 0, XS_FRAME_SIZE * sizeof(float));
            float framePos = (float)f / (float)(XS_FRAMES - 1);

            // 8 modes, 8 frames each
            int mode = f / 8;
            float z = (float)(f % 8) / 7.f;

            // Deterministic per-frame seed
            rng ^= (uint32_t)(0x9E3779B9u * (uint32_t)(f + 1) + 0x7F4A7C15u);

            // Common evolving states
            float x = 0.15f + 0.7f * xorshift01();  // logistic state
            float y = 0.10f + 0.8f * xorshift01();
            float phi = xorshift01();               // phase accumulator (0..1)
            float fb = 0.f;
            float prev = 0.f;

            // Map parameters
            float r = 3.57f + 0.40f * z + 0.02f * std::sin(framePos * 2.f * (float)M_PI);
            float r2 = 3.62f + 0.36f * (1.f - z);
            float K = 0.10f + 1.75f * (0.25f + 0.75f * z);  // circle-map / lens strength
            float warpAmt = 0.00f + 0.22f * std::pow(framePos, 1.2f);
            float foldAmt = 0.00f + 0.28f * std::pow(framePos, 1.6f);
            float asymAmt = 0.08f + 0.40f * framePos;

            // Recursion rates (kept modest to avoid DC runaway)
            float inj = 0.06f + 0.22f * z;
            float damp = 0.985f - 0.02f * z;

            // A few fixed phases for deterministic "jitter" (not noise)
            float phA = randBip() * (float)M_PI;
            float phB = randBip() * (float)M_PI;
            float phC = randBip() * (float)M_PI;

            for (int s = 0; s < XS_FRAME_SIZE; s++) {
                // Iterate nonlinear maps (no per-sample RNG)
                x = r * x * (1.f - x);
                y = r2 * y * (1.f - y);
                float cx = 2.f * x - 1.f;
                float cy = 2.f * y - 1.f;

                // Recursive feedback phase injection
                fb = fb * damp + cx * inj;
                float phiStep = 1.f / (float)XS_FRAME_SIZE;
                phi = fract01(phi + phiStep + fb * 0.015f);

                // Recursive phase distortion (within-cycle)
                float warp = phi;
                warp += warpAmt * 0.08f * std::sin(2.f * (float)M_PI * (2.f + (float)(mode % 5)) * warp + phA);
                warp += warpAmt * 0.03f * std::sin(2.f * (float)M_PI * (7.f + (float)(mode % 7)) * warp + phB);
                warp = fract01(warp);

                // Base "carrier" is not stable fundamental: it is derived from maps and warped phase.
                float u = 2.f * warp - 1.f;
                float tri = 1.f - 2.f * std::fabs(u);          // triangle-like
                float para = u - (u * u * u) * 0.3333333f;     // parabolic-ish

                float v = 0.f;

                switch (mode) {
                default:
                case 0: {
                    // Logistic-lensed triangle with asymmetric pressure
                    float a = lens(tri + cx * 0.35f, 1.4f + 4.0f * z);
                    v = a + asymAmt * 0.18f * a * a * a;
                    break;
                }
                case 1: {
                    // Circle map / phase lensing (quasi-chaotic phase rotation)
                    // theta_{n+1} = theta + omega + K*sin(2pi*theta)
                    float theta = warp;
                    float omega = 0.15f + 0.35f * z;
                    theta = fract01(theta + omega + (K * 0.07f) * std::sin(2.f * (float)M_PI * theta + phC));
                    float uu = 2.f * theta - 1.f;
                    v = lens(uu, 0.9f + 5.0f * z);
                    break;
                }
                case 2: {
                    // Tent map fracture: discontinuous, edgy, pitch-reactive
                    float tx = x;
                    // tent(x): 2x if x<0.5 else 2(1-x)
                    tx = (tx < 0.5f) ? (2.f * tx) : (2.f * (1.f - tx));
                    float w = 2.f * tx - 1.f;
                    // internal discontinuity injection
                    if (cy > 0.995f) w = (w > 0.f) ? 1.f : -1.f;
                    v = lens(w + para * 0.35f, 1.2f + 3.5f * z);
                    break;
                }
                case 3: {
                    // Fractal warping: nested warp on parabolic wave
                    float ww = warp;
                    ww += 0.09f * warpAmt * std::sin(2.f * (float)M_PI * (3.f + 7.f * x) * ww + phA);
                    ww += 0.05f * warpAmt * std::sin(2.f * (float)M_PI * (5.f + 11.f * y) * ww + phB);
                    ww = fract01(ww);
                    float uu = 2.f * ww - 1.f;
                    float p = uu - (uu * uu * uu) * 0.28f;
                    v = lens(p, 1.0f + 5.5f * z);
                    break;
                }
                case 4: {
                    // Recursive phase lens with feedback injection (alien "gravity well")
                    float g = 0.8f + 6.0f * z;
                    float well = lens(u + fb * 0.9f, g);
                    v = well;
                    // occasional phase slip discontinuity
                    if (x > 0.995f) v = -v;
                    break;
                }
                case 5: {
                    // Coupled map product (instability without FM)
                    float prod = cx * cy;
                    float shaped = lens(prod + tri * 0.25f, 1.6f + 4.0f * z);
                    v = shaped + 0.15f * lens(para + cx * 0.15f, 2.2f + 3.0f * z);
                    break;
                }
                case 6: {
                    // Recursive edge sharpening: derivative-like emphasis + discontinuities
                    float d = (tri - prev);
                    prev = tri;
                    float edge = lens(d * (6.0f + 10.0f * z) + u * 0.35f, 1.4f + 5.0f * z);
                    v = edge;
                    // hard clip discontinuity in later frames
                    if (framePos > 0.5f && std::fabs(edge) > 0.85f) v = (edge > 0.f) ? 1.f : -1.f;
                    break;
                }
                case 7: {
                    // Collapse mode: recursive injection + lensing + fold. Feels like physics failing.
                    float collapse = lens(u + cx * 0.55f + cy * 0.25f, 2.0f + 6.0f * z);
                    collapse += 0.22f * lens(std::sin(2.f * (float)M_PI * (warp + cx * 0.05f) * (3.f + 9.f * z) + phA), 1.0f + 3.0f * z);
                    v = collapse;
                    // abrupt sign inversion when map hits extremes
                    if (x < 0.02f || x > 0.98f) v = -v;
                    break;
                }
                }

                // Nonlinear asymmetry and fold (late frames more aggressive)
                v += asymAmt * 0.10f * v * v * v;
                if (foldAmt > 0.0001f) {
                    float folded = std::sin(v * (1.4f + foldAmt * 7.0f));
                    v = v * (1.f - foldAmt) + folded * foldAmt;
                }

                // Soft clamp for stability; keep tension
                v = std::tanh(v * (1.2f + framePos * 2.3f));
                data[table][f][s] = v;
            }

            // DC removal
            float mean = 0.f;
            for (int s = 0; s < XS_FRAME_SIZE; s++) mean += data[table][f][s];
            mean /= (float)XS_FRAME_SIZE;
            for (int s = 0; s < XS_FRAME_SIZE; s++) data[table][f][s] -= mean;

            normalizeFrame(table, f);
        }
    }

    void normalizeFrame(int table, int frame) {
        float peak = 0.f;
        for (int s = 0; s < XS_FRAME_SIZE; s++) {
            float v = std::fabs(data[table][frame][s]);
            if (v > peak) peak = v;
        }
        if (peak > 0.001f) {
            float scale = 1.f / peak;
            for (int s = 0; s < XS_FRAME_SIZE; s++)
                data[table][frame][s] *= scale;
        }
    }
};

// ============================================================================
// Static wavetable bank -- shared across all instances
// ============================================================================

static XsWavetableBank* xsBank = nullptr;
static int xsBankRefCount = 0;

static XsWavetableBank* xsAcquireBank() {
    if (!xsBank) {
        xsBank = new (std::nothrow) XsWavetableBank();
        if (xsBank) {
#ifdef METAMODULE
            xsBank->generateMetaFast();
#else
            xsBank->generate();
#endif
        }
    }
    if (xsBank)
        xsBankRefCount++;
    return xsBank;
}

static void xsReleaseBank() {
    if (!xsBank)
        return;
    if (--xsBankRefCount <= 0) {
        delete xsBank;
        xsBank = nullptr;
        xsBankRefCount = 0;
    }
}

// ============================================================================
// Simple high-quality random walk (low-CPU)
// ============================================================================

struct XsRandomWalk {
    float value = 0.f;
    uint32_t state = 12345;

    // Simple xorshift32
    float nextRandom() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float)(state & 0xFFFF) / 32768.f - 1.f;  // -1..1
    }

    // Returns slowly drifting value in range [lo, hi]
    float step(float rate, float lo = 0.f, float hi = 1.f) {
        value += nextRandom() * rate;
        value = clamp(value, lo, hi);
        return value;
    }

    void seed(uint32_t s) {
        state = s ? s : 1;
    }
};

// ============================================================================
// Module struct
// ============================================================================

struct Xenostasis : Module {
    enum ParamId {
        PITCH_PARAM,
        CHAOS_PARAM,
        STABILITY_PARAM,
        HOMEOSTASIS_PARAM,
        CROSS_PARAM,
        DENSITY_PARAM,
        TABLE_PARAM,
        BBCHAR_PARAM,
        BBVOL_PARAM,
        BBMODE_PARAM,
        BBPITCH_PARAM,
        DECAY_PARAM,
        PUNCH_PARAM,
        WTVOL_PARAM,
        DRIVE_PARAM,
        FMVOL_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        CHAOS_CV_INPUT,
        CROSS_CV_INPUT,
        VOCT_INPUT,
        TABLE_CV_INPUT,
        DENSITY_CV_INPUT,
        BBCHAR_CV_INPUT,
        BBMODE_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        ENERGY_LIGHT,
        STORM_LIGHT,
        LIGHTS_LEN
    };

    // ── Wavetable bank ──
    XsWavetableBank* bank = nullptr;

    // ── Wavetable oscillator state ──
    float wtPhaseL = 0.f;        // Phase accumulator left
    float wtPhaseR = 0.f;        // Phase accumulator right (slight offset for stereo)
    float frameDrift = 0.f;      // Current frame position (0..63)
    float phaseWarpDepth = 0.f;  // Current phase warp amount

    // ── Bytebeat state ──
    uint32_t bbTimeI = 0;        // Integer bytebeat time accumulator
    uint32_t bbTime2I = 0;       // Second layer time
    uint32_t bbTime3I = 0;       // Third layer time (sub-harmonic)
    float bbTimeFrac = 0.f;      // Fractional accumulator for time 1
    float bbTime2Frac = 0.f;     // Fractional accumulator for time 2
    float bbTime3Frac = 0.f;     // Fractional accumulator for time 3
    float bbSmoothed = 0.f;      // Smoothed bytebeat output
    float bbLayer2 = 0.f;        // Second layer smoothed
    float bbLayer3 = 0.f;        // Third layer smoothed
    float bbEpsilon = 0.0003f;   // Slowly drifting non-repetition variable
    float bbFilterState = 0.f;   // One-pole bandpass state for tonal shaping
    float bbLowpassed = 0.f;     // Lowpass state for BB modulation path
    float bbSlewed = 0.f;        // Slew-limited BB control signal (crackle-free)

    // ── Metabolism ──
    float energy = 0.f;          // Current system excitation (0..1)
    float residualBias = 0.01f;  // Prevents guaranteed return to calm
    float targetBias = 0.02f;    // Wander target for residualBias
    float stressExposure = 0.f;  // Long-term storm memory

    // ── Random walks ──
    XsRandomWalk frameDriftWalk;
    XsRandomWalk epsilonWalk;
    XsRandomWalk biasWalk;

    // ── Frame inertia (second-order traversal) ──
    float frameVelocity = 0.f;   // Frame movement has momentum

    // ── Organism pulse (ultra-slow internal oscillator) ──
    double organismPhase = 0.0;  // 30-120 second cycles
    float cachedPulse = 0.f;     // Organism pulse sin value (control-rate)

    // ── Spectral tilt (energy-driven harmonic falloff) ──
    float wtTiltStateL = 0.f;    // One-pole lowpass state for left
    float wtTiltStateR = 0.f;    // One-pole lowpass state for right

    // ── Smoothed params ──
    float smoothedChaos = 0.f;
    float smoothedCross = 0.15f;
    float smoothedDensity = 0.2f;

    // ── Clock detection ──
    dsp::SchmittTrigger clockTrigger;
    bool clockJustFired = false;

    // ── Percussive excitation (clock-driven nervous system) ──
    float strikeEnv = 0.f;           // Exponential decay envelope (0..1)
    float strikeBpStateL = 0.f;      // Bandpass filter state left
    float strikeBpStateR = 0.f;      // Bandpass filter state right
    float strikeBpPrevL = 0.f;       // Previous BP input for highpass
    float strikeBpPrevR = 0.f;
    uint32_t strikeNoiseRng = 77777; // Noise PRNG state
    bool percussiveMode = false;      // True when clock is connected    // Per-strike randomized variation (set on each clock edge)
    float strikeFrameJump = 0.f;     // Random frame position offset per hit
    float strikeWarpAmount = 0.f;    // Random phase warp intensity per hit
    float strikeBbKick = 0.f;        // Random bb time kick per hit
    float strikeResonMult = 1.f;     // Random resonant freq multiplier per hit
    float strikeDecayMult = 1.f;     // Random decay length variation per hit

    // -- FM drum synthesis (2-op: modulator -> carrier) --
    float fmCarrierPhase = 0.f;      // Carrier oscillator phase
    float fmModPhase = 0.f;          // Modulator oscillator phase
    float strikeFmRatio = 1.f;       // Mod:carrier frequency ratio (randomized per hit)
    float strikeFmIndex = 5.f;       // FM index at strike peak (randomized per hit)
    float strikeFmPitchDrop = 0.f;   // Pitch drop amount per hit (randomized)

    // -- Drone FM state (evolving texture when no clock) --
    double droneFmLfo1 = 0.0;         // Slow LFO for ratio wandering
    double droneFmLfo2 = 0.0;         // Slower LFO for index breathing
    double droneFmLfo3 = 0.0;         // Very slow LFO for stereo detuning
    float droneFmMod2Phase = 0.f;     // Second modulator phase (3-op in drone)

    // ── Control rate counter ──
    int controlCounter = 0;
    float prevChaosCV = 0.f;

    // ── Sub oscillator ──
    float subPhase = 0.f;

    // ── Sample rate ──
    float sampleRate = 48000.f;

    // Cached coefficients (avoid per-sample exp)
    float smoothCoeff20Hz = 0.f;
    float bbMaxStep = 0.002f;

    // Cached strike envelope decay (avoid per-sample exp)
    float cachedDecayKnob = -1.f;
    float cachedStrikeDecayMult = -1.f;
    float strikeDecayRateCached = 0.999f;

    // Cached control-rate values (updated every XS_CONTROL_RATE samples)
    float cachedBbPitchMult = 1.f;    // exp2(bbPitch) — avoids per-sample exp2
    float cachedCurvatureSin = 0.f;   // sin(bbTimeI * 0.00003) for time curvature
    float cachedLfo1 = 0.f;           // Drone FM LFO 1 sin value
    float cachedLfo2 = 0.f;           // Drone FM LFO 2 sin value
    float cachedLfo3 = 0.f;           // Drone FM LFO 3 sin value
    float cachedDroneRatio = 2.f;     // Drone FM mod:carrier ratio
    float cachedDroneIndex = 0.5f;    // Drone FM modulation index
    float cachedMod2Ratio = 0.5f;     // Drone FM 2nd modulator ratio
    float cachedMod2Index = 0.f;      // Drone FM 2nd modulator index
    float cachedSatAmount = 1.f;      // Drone FM saturation amount

#ifdef METAMODULE
    // MetaModule may instantiate modules during plugin scan; avoid doing any
    // heavy allocation/generation work in the constructor.
    bool bankInitAttempted = false;
#endif

#ifdef METAMODULE
    // Fast math helpers for embedded target
    static constexpr int XS_SIN_LUT_SIZE = 2048;
    static constexpr float XS_INV_TWO_PI = 0.15915494309189535f;  // 1/(2*pi)
    static std::array<float, XS_SIN_LUT_SIZE> xsSinLut;
    static bool xsSinLutInited;

    static inline void xsInitSinLut() {
        if (xsSinLutInited)
            return;
        for (int i = 0; i < XS_SIN_LUT_SIZE; i++) {
            float p = (float)i / (float)XS_SIN_LUT_SIZE;
            xsSinLut[i] = ::sinf(2.f * (float)M_PI * p);
        }
        xsSinLutInited = true;
    }

    static inline float xsWrap01(float x) {
        if (x >= 1.f || x < 0.f)
            x -= ::floorf(x);
        return x;
    }

    static inline float xsSin2Pi(float phase01) {
        // LUT guaranteed initialized in constructor — no per-call check.
        phase01 = xsWrap01(phase01);
        float idx = phase01 * (float)XS_SIN_LUT_SIZE;
        int i0 = (int)idx;
        float frac = idx - (float)i0;
        int i1 = (i0 + 1) & (XS_SIN_LUT_SIZE - 1);
        float a = xsSinLut[i0 & (XS_SIN_LUT_SIZE - 1)];
        float b = xsSinLut[i1];
        return a + (b - a) * frac;
    }

    static inline float xsSinRad(float xRad) {
        // Convert to cycles and use the LUT (1 cycle = 2*pi)
        return xsSin2Pi(xRad * XS_INV_TWO_PI);
    }

    static inline float xsTanhFast(float x) {
        // Cheap saturating nonlinearity; close enough for embedded testing
        float ax = ::fabsf(x);
        return x / (1.f + ax);
    }

    static inline float xsExp2Fast(float x) {
        // Prefer exp2f if available; fall back to powf otherwise.
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
        return ::exp2f(x);
#else
        return ::powf(2.f, x);
#endif
    }
#endif

    Xenostasis() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(PITCH_PARAM, -4.f, 4.f, 0.f, "Pitch", " V");
        configParam(CHAOS_PARAM, 0.f, 1.f, 0.f, "Chaos", "%", 0.f, 100.f);
        configParam(STABILITY_PARAM, 0.f, 1.f, 0.6f, "Stability", "%", 0.f, 100.f);
        configParam(HOMEOSTASIS_PARAM, 0.f, 1.f, 0.5f, "Homeostasis", "%", 0.f, 100.f);
        configParam(CROSS_PARAM, 0.f, 1.f, 0.15f, "Cross Modulation", "%", 0.f, 100.f);
        configParam(DENSITY_PARAM, 0.f, 1.f, 0.2f, "Density", "%", 0.f, 100.f);
        configParam(TABLE_PARAM, 0.f, 7.f, 0.f, "Table Select");
        getParamQuantity(TABLE_PARAM)->snapEnabled = true;
        configParam(BBCHAR_PARAM, 0.f, 1.f, 0.5f, "Bytebeat Character", "%", 0.f, 100.f);
        configParam(BBVOL_PARAM, 0.f, 2.f, 1.f, "Bytebeat Volume", "%", 0.f, 100.f);
		configSwitch(BBMODE_PARAM, 0.f, 7.f, 0.f, "Bytebeat Mode", {
			"Xeno",
			"Melodic",
			"Rhythmic",
			"Harsh",
			"Tonal",
			"Arp",
			"Crackle",
			"Organism",
		});
        configParam(BBPITCH_PARAM, -2.f, 2.f, 0.f, "Bytebeat Pitch", " oct");
        configParam(DECAY_PARAM, 0.f, 1.f, 0.3f, "Decay", "%", 0.f, 100.f);
        configParam(PUNCH_PARAM, 0.f, 1.f, 0.5f, "Punch", "%", 0.f, 100.f);
        configParam(WTVOL_PARAM, 0.f, 1.f, 0.7f, "Wavetable Volume", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.3f, "Drive", "%", 0.f, 100.f);
        configParam(FMVOL_PARAM, 0.f, 1.f, 0.5f, "FM Volume", "%", 0.f, 100.f);

        configInput(CLOCK_INPUT, "Trigger");
        configInput(CHAOS_CV_INPUT, "Chaos CV (0-10V)");
        configInput(CROSS_CV_INPUT, "Cross CV (0-10V)");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(TABLE_CV_INPUT, "Table CV (0-10V)");
        configInput(DENSITY_CV_INPUT, "Density CV (0-10V)");
        configInput(BBCHAR_CV_INPUT, "Bytebeat Character CV (0-10V)");
        configInput(BBMODE_CV_INPUT, "Bytebeat Mode CV (0-10V)");

        configOutput(LEFT_OUTPUT, "Left Audio");
        configOutput(RIGHT_OUTPUT, "Right Audio");

    #ifndef METAMODULE
        bank = xsAcquireBank();
    #else
        // Initialize sin LUT once in constructor (not per-call)
        xsInitSinLut();
    #endif

        // Pre-compute sample-rate-dependent coefficients
        {
            float dt = 1.f / sampleRate;
            smoothCoeff20Hz = 1.f - ::expf(-2.f * (float)M_PI * 20.f * dt);
            bbMaxStep = 0.002f * (sampleRate / 48000.f);
        }

        // Seed random walks with different values
        frameDriftWalk.seed(54321);
        epsilonWalk.seed(98765);
        epsilonWalk.value = 0.0003f;
        biasWalk.seed(11111);
        biasWalk.value = 0.02f;
    }

    ~Xenostasis() {
        if (bank)
            xsReleaseBank();
    }

    void onSampleRateChange() override {
        sampleRate = APP->engine->getSampleRate();

        // Cache coefficients that only depend on sample rate
        float dt = 1.f / sampleRate;
        smoothCoeff20Hz = 1.f - ::expf(-2.f * (float)M_PI * 20.f * dt);
        bbMaxStep = 0.002f * (sampleRate / 48000.f);
    }

    // ── Wavetable read (smooth interp for most, hard switch for harsh/FX) ──
    float readWavetable(int tableIdx, float frame, float phase) {
        // If the shared bank failed to allocate (e.g. low-memory embedded),
        // fall back to a simple oscillator so the module still loads.
        if (__builtin_expect(!bank, 0)) {
            phase = phase - std::floor(phase);
            float t = phase;
            float u = 2.f * t - 1.f;
            switch (clamp(tableIdx, 0, XS_NUM_TABLES - 1)) {
            default:
            case 0: return 1.f - 2.f * std::fabs(u); // tri
            case 1: return std::sin(2.f * (float)M_PI * t);
            case 2: return u; // saw
            case 3: return (t < 0.5f) ? 1.f : -1.f;
            case 4: {
                float s = std::sin(2.f * (float)M_PI * t);
                return s * 0.7f + (s >= 0.f ? 1.f : -1.f) * (s * s) * 0.3f;
            }
            case 5: {
                float sq = (t < 0.5f) ? 1.f : -1.f;
                float a = sq * 0.8f + u * 0.2f;
                return std::tanh(a * 2.f);
            }
            case 6: {
                float a = 1.f - 2.f * std::fabs(u);
                float q = 12.f;
                return std::floor(a * q + 0.5f) / q;
            }
            case 7: {
                float a = u + (1.f - 2.f * std::fabs(u)) * 0.35f;
                return a / (1.f + 0.85f * std::fabs(a));
            }
            }
        }

        tableIdx = clamp(tableIdx, 0, XS_NUM_TABLES - 1);
        frame = clamp(frame, 0.f, (float)(XS_FRAMES - 1));

        // Tables 5-6: hard frame switching (no interpolation -- sudden, unpredictable)
        // Table 7 is also hard-switched: frames are distinct chaos modes.
        bool hardSwitch = (tableIdx == 5 || tableIdx == 6 || tableIdx == 7);

        int f0 = (int)frame;
        int f1 = std::min(f0 + 1, XS_FRAMES - 1);
        float fFrac = hardSwitch ? 0.f : (frame - (float)f0);  // 0 = no interp

        // Wrap phase to 0..1
        phase = phase - std::floor(phase);
        float samplePos = phase * (float)XS_FRAME_SIZE;
        int s0 = (int)samplePos & (XS_FRAME_SIZE - 1);
        int s1 = (s0 + 1) & (XS_FRAME_SIZE - 1);
        float sFrac = samplePos - std::floor(samplePos);

        // Bilinear: interpolate within each frame, then between frames
        float v0 = bank->data[tableIdx][f0][s0] * (1.f - sFrac)
                  + bank->data[tableIdx][f0][s1] * sFrac;
        float v1 = bank->data[tableIdx][f1][s0] * (1.f - sFrac)
                  + bank->data[tableIdx][f1][s1] * sFrac;

        return v0 * (1.f - fFrac) + v1 * fFrac;
    }

    // ── Soft clipper ──
    static float softClip(float x) {
        if (x > 1.5f) return 1.f;
        if (x < -1.5f) return -1.f;
        return x - (x * x * x) / 6.75f;
    }

    // ── Smoothstep [0,1] ──
    static float smoothstep01(float x) {
        x = clamp(x, 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }

    void process(const ProcessArgs& args) override {
        sampleRate = args.sampleRate;
        float dt = 1.f / sampleRate;

#ifdef METAMODULE
        // Lazy init: do the shared bank allocate/generate only once we are actually running.
        if (__builtin_expect(!bank && !bankInitAttempted, 0)) {
            bankInitAttempted = true;
            bank = xsAcquireBank();
        }
#endif

        // ── 1. Read params + CVs ──
        float pitchV = params[PITCH_PARAM].getValue();
        if (inputs[VOCT_INPUT].isConnected())
            pitchV += inputs[VOCT_INPUT].getVoltage();

        float chaosParam = params[CHAOS_PARAM].getValue();
        float chaosCV = 0.f;
        if (inputs[CHAOS_CV_INPUT].isConnected())
            chaosCV = clamp(inputs[CHAOS_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
        float chaosTotal = clamp(chaosParam + chaosCV, 0.f, 1.f);

        float stability = params[STABILITY_PARAM].getValue();
        float homeostasis = params[HOMEOSTASIS_PARAM].getValue();

        float crossParam = params[CROSS_PARAM].getValue();
        float crossCV = 0.f;
        if (inputs[CROSS_CV_INPUT].isConnected())
            crossCV = clamp(inputs[CROSS_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
        float crossTotal = clamp(crossParam + crossCV, 0.f, 1.f);

        float densityParam = params[DENSITY_PARAM].getValue();
        if (inputs[DENSITY_CV_INPUT].isConnected()) {
            float densityCV = clamp(inputs[DENSITY_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            densityParam = clamp(densityParam + densityCV, 0.f, 1.f);
        }
        int tableIdx = (int)params[TABLE_PARAM].getValue();
        // Table CV: 0-10V maps across all tables
        if (inputs[TABLE_CV_INPUT].isConnected()) {
            float tableCv = clamp(inputs[TABLE_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            tableIdx = clamp((int)(tableCv * ((float)XS_NUM_TABLES - 0.001f)), 0, XS_NUM_TABLES - 1);
        }

        // Bytebeat character knob (0..1): controls shift amounts + morphing (timbre)
        float bbChar = params[BBCHAR_PARAM].getValue();
        if (inputs[BBCHAR_CV_INPUT].isConnected()) {
            float bbCharCV = clamp(inputs[BBCHAR_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            bbChar = clamp(bbChar + bbCharCV, 0.f, 1.f);
        }
        // Bytebeat volume: 0..200%
        float bbVol = params[BBVOL_PARAM].getValue();
        int bbMode = (int)params[BBMODE_PARAM].getValue();
        bbMode = clamp(bbMode, 0, 7);
        // BB Mode CV: 0-10V maps across all 8 modes (CV overrides the knob)
        if (inputs[BBMODE_CV_INPUT].isConnected()) {
            float bbModeCv = clamp(inputs[BBMODE_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            bbMode = clamp((int)(bbModeCv * (8.f - 0.001f)), 0, 7);
        }
        // bbPitch read moved to control rate (cachedBbPitchMult)

        // Decay knob: 0 = ultra-short click (~5ms), 1 = medium length (~200ms)
        float decayKnob = params[DECAY_PARAM].getValue();
        // Punch knob: 0 = soft, 1 = maximum transient attack
        float punchKnob = params[PUNCH_PARAM].getValue();
        // Wavetable volume: 0 = silent, 1 = full
        float wtVol = params[WTVOL_PARAM].getValue();
        // Drive: 0 = clean, 1 = heavy compression+saturation
        float driveKnob = params[DRIVE_PARAM].getValue();
        // FM volume: 0 = silent, 1 = full
        float fmVol = params[FMVOL_PARAM].getValue();

        // Smooth params to prevent zipper noise
        float smoothCoeff = smoothCoeff20Hz;  // ~20Hz smoothing
        smoothedChaos += (chaosTotal - smoothedChaos) * smoothCoeff;
        smoothedCross += (crossTotal - smoothedCross) * smoothCoeff;
        smoothedDensity += (densityParam - smoothedDensity) * smoothCoeff;

        // Cross scaling (smoothstep curve)
        float crossScaled = smoothstep01(smoothedCross);

        // Clock detection + percussive mode
        clockJustFired = false;
        percussiveMode = inputs[CLOCK_INPUT].isConnected();
        if (percussiveMode) {
            clockJustFired = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
        }

        // ── 2a. Strike envelope (percussive excitation) ──
        if (clockJustFired) {
            strikeEnv = 1.f;
            // Seed noise burst on each strike (different each time)
            strikeNoiseRng ^= bbTimeI ^ 0xDEAD;
            // Generate per-strike random variation for unique hits
            // Use xorshift to get several random floats
            auto strikeRand = [&]() -> float {
                strikeNoiseRng ^= strikeNoiseRng << 13;
                strikeNoiseRng ^= strikeNoiseRng >> 17;
                strikeNoiseRng ^= strikeNoiseRng << 5;
                return (float)(strikeNoiseRng & 0xFFFF) / 65535.f;  // 0..1
            };
            strikeFrameJump = (strikeRand() - 0.5f) * 20.f;  // -10..+10 frames
            strikeWarpAmount = strikeRand() * 0.3f + 0.05f;   // 0.05..0.35
            strikeBbKick = strikeRand() * 40.f + 10.f;         // 10..50
            strikeResonMult = 0.5f + strikeRand() * 3.f;      // 0.5..3.5x
            strikeDecayMult = 0.7f + strikeRand() * 0.6f;     // 0.7..1.3x
            // FM drum: randomize ratio, index, and pitch drop per hit
            float fmRatios[] = {1.f, 1.41f, 2.f, 2.76f, 3.f, 3.5f, 4.f, 5.33f, 7.f};
            int ratioIdx = (int)(strikeRand() * 8.99f);
            strikeFmRatio = fmRatios[ratioIdx];
            strikeFmIndex = 2.f + strikeRand() * 12.f;       // 2..14
            strikeFmPitchDrop = strikeRand() * 3.f;           // 0..3 octaves drop
            // Reset bandpass filter for clean transient
            strikeBpStateL = 0.f;
            strikeBpStateR = 0.f;
            strikeBpPrevL = 0.f;
            strikeBpPrevR = 0.f;

            // Reset/reseed bytebeat timing on clock so patterns can lock to tempo.
            // Seed is deterministic from mode+character so you can find repeatable sweet spots.
            uint32_t bbSeed = 0x9E3779B9u;
            bbSeed ^= (uint32_t)(bbMode + 1) * 0x85EBCA6Bu;
            bbSeed ^= (uint32_t)(int)(bbChar * 65535.f + 0.5f) * 0xC2B2AE35u;
            bbTimeI = bbSeed;
            bbTimeFrac = 0.f;
            bbTime2I = bbSeed ^ 0xA5A5A5A5u;
            bbTime2Frac = 0.f;
            bbTime3I = bbSeed ^ 0x5A5A5A5Au;
            bbTime3Frac = 0.f;
        }
        // Exponential decay -- controlled by DECAY knob
        // decayKnob 0 = ~5ms, 0.5 = ~40ms, 1.0 = ~200ms (exponential curve)
        // Cache strike envelope decay rate when the knob or per-strike multiplier changes.
        if (cachedDecayKnob != decayKnob || cachedStrikeDecayMult != strikeDecayMult) {
            cachedDecayKnob = decayKnob;
            cachedStrikeDecayMult = strikeDecayMult;
            float baseHalfLifeMs = 5.f + decayKnob * decayKnob * 195.f;
            float halfLifeMs = baseHalfLifeMs * strikeDecayMult;
            halfLifeMs = clamp(halfLifeMs, 3.f, 300.f);
            float halfLifeSamples = halfLifeMs * 0.001f * sampleRate;
            strikeDecayRateCached = ::expf(-0.6931f / halfLifeSamples);
        }
        strikeEnv *= strikeDecayRateCached;
        if (strikeEnv < 0.001f) strikeEnv = 0.f;  // clean cutoff

        // Frequency from pitch
        float freq = 261.626f *
    #ifdef METAMODULE
            xsExp2Fast(pitchV)
    #else
            std::pow(2.f, pitchV)
    #endif
            ;  // C4 = 261.626 Hz
        freq = clamp(freq, 8.f, 12000.f);

        // ── 2. Update metabolism (control rate) ──
        controlCounter++;
        if (controlCounter >= XS_CONTROL_RATE) {
            controlCounter = 0;
            float controlRate = sampleRate / (float)XS_CONTROL_RATE;
            float cdt = (float)XS_CONTROL_RATE / sampleRate;  // time per control tick

            // Energy accumulation -- rates scaled so energy responds in seconds, not minutes
            float cvRate = std::fabs(chaosCV - prevChaosCV) * controlRate;
            prevChaosCV = chaosCV;
            float clockBurst = clockJustFired ? 0.15f : 0.f;
            float gain = smoothedChaos * 4.f + cvRate * 0.5f + clockBurst + smoothedDensity * 1.5f;
            energy += gain * cdt;

            // Nonlinear decay -- homeostasis strongly controls recovery speed
            // At homeostasis=0: very slow decay (0.3/s base). At 1.0: fast decay (3.5/s at peak energy)
            float decayBase = 0.3f + homeostasis * 2.0f;
            float decayRate = decayBase + homeostasis * 4.0f * energy * energy;
            energy -= decayRate * cdt;
            energy = clamp(energy, residualBias, 1.f);

            // Residual bias wandering -- homeostasis lowers the floor
            float biasMax = 0.12f + stressExposure * 0.08f - homeostasis * 0.06f;
            biasMax = clamp(biasMax, 0.01f, 0.2f);
            targetBias = biasWalk.step(0.001f, 0.f, biasMax);
            residualBias += (targetBias - residualBias) * 0.005f;
            residualBias = clamp(residualBias, 0.f, 0.2f);

            // Stress exposure -- accumulates during storms, homeostasis speeds forgetting
            if (energy > 0.5f) stressExposure += 0.02f * cdt * (energy - 0.5f) * 2.f;
            stressExposure -= stressExposure * (0.001f + homeostasis * 0.01f) * cdt * 30.f;
            stressExposure = clamp(stressExposure, 0.f, 1.f);

            // Update frame drift -- inertia model (velocity + acceleration)
            // Stability controls range and speed. Low stability: wide fast drift. High: tight slow.
            float driftRate = 0.02f + (1.f - stability) * 0.6f;
            driftRate += smoothedChaos * 0.3f + energy * 0.2f;
            float driftRange = 4.f + (1.f - stability) * 56.f;  // 4..60 frames
            driftRange += energy * 10.f;  // storms widen range
            float driftCenter = (float)(XS_FRAMES - 1) * 0.5f;
            float driftLo = clamp(driftCenter - driftRange * 0.5f, 0.f, (float)(XS_FRAMES - 1));
            float driftHi = clamp(driftCenter + driftRange * 0.5f, 0.f, (float)(XS_FRAMES - 1));

            // Random acceleration force
            float accel = frameDriftWalk.nextRandom() * driftRate * 0.01f;
            frameVelocity += accel;
            // Damping -- prevents runaway
            frameVelocity *= 0.998f;
            frameDrift += frameVelocity;

            // Damped reflection at boundaries (not hard negate)
            if (frameDrift < driftLo) {
                frameDrift = driftLo;
                frameVelocity *= -0.6f;
            } else if (frameDrift > driftHi) {
                frameDrift = driftHi;
                frameVelocity *= -0.6f;
            }

            // Rare direction reversal -- only when nearly still and very high energy
            if (energy > 0.8f && std::fabs(frameVelocity) < 0.005f) {
                float flipChance = (energy - 0.8f) * 0.02f;
                if (std::fabs(frameDriftWalk.nextRandom()) < flipChance)
                    frameVelocity *= -1.f;
            }

            // Update epsilon (non-repetition variable for bytebeat)
            bbEpsilon = epsilonWalk.step(0.0005f, 0.0001f, 0.005f);

            // Update lights
            lights[ENERGY_LIGHT].setBrightness(energy);
            lights[STORM_LIGHT].setBrightness(energy > 0.6f ? (energy - 0.6f) * 2.5f : 0.f);

            // ── Control-rate: organism pulse (ultra-slow ~20–120s cycle) ──
            {
                float pulseRate = 0.05f * (1.f / (1.f + stressExposure * 4.f));
                organismPhase += (double)(pulseRate * cdt);
            #ifdef METAMODULE
                cachedPulse = xsSinRad((float)organismPhase);
            #else
                cachedPulse = std::sin((float)organismPhase);
            #endif
            }

            // ── Control-rate: bytebeat pitch multiplier (avoids per-sample exp2) ──
            {
                float bbPitchVal = params[BBPITCH_PARAM].getValue();
            #ifdef METAMODULE
                cachedBbPitchMult = xsExp2Fast(bbPitchVal);
            #else
                cachedBbPitchMult = std::pow(2.f, bbPitchVal);
            #endif
            }

            // ── Control-rate: bytebeat time curvature sin ──
            #ifdef METAMODULE
                cachedCurvatureSin = xsSinRad((float)bbTimeI * 0.00003f);
            #else
                cachedCurvatureSin = std::sin((float)bbTimeI * 0.00003f);
            #endif

            // ── Control-rate: drone FM LFOs + derived parameters ──
            droneFmLfo1 += (double)(0.037f * cdt);
            droneFmLfo2 += (double)(0.013f * cdt);
            droneFmLfo3 += (double)(0.0071f * cdt);
            #ifdef METAMODULE
                cachedLfo1 = xsSin2Pi((float)droneFmLfo1);
                cachedLfo2 = xsSin2Pi((float)droneFmLfo2);
                cachedLfo3 = xsSin2Pi((float)droneFmLfo3);
            #else
                cachedLfo1 = std::sin((float)droneFmLfo1 * 2.f * (float)M_PI);
                cachedLfo2 = std::sin((float)droneFmLfo2 * 2.f * (float)M_PI);
                cachedLfo3 = std::sin((float)droneFmLfo3 * 2.f * (float)M_PI);
            #endif

            cachedDroneRatio = 2.f + cachedLfo1 * (1.5f + smoothedChaos * 2.f)
                              + cachedLfo2 * 0.7f;
            cachedDroneRatio = std::fabs(cachedDroneRatio);
            if (cachedDroneRatio < 0.25f) cachedDroneRatio = 0.25f;

            cachedDroneIndex = 0.5f + smoothedChaos * 4.f
                             + cachedLfo2 * (1.f + smoothedChaos * 2.f)
                             + cachedPulse * 0.5f
                             + energy * 2.f;
            cachedDroneIndex = std::fabs(cachedDroneIndex);

            cachedMod2Ratio = 0.5f + cachedLfo3 * 0.3f + smoothedChaos * 0.5f;
            cachedMod2Index = smoothedChaos * 2.f + energy * 1.f + cachedLfo1 * 0.5f;
            cachedMod2Index = std::fabs(cachedMod2Index);

            cachedSatAmount = 1.f + smoothedChaos * 2.f - stability * 0.5f;
        }

        // ── 3. Organism pulse (cached at control rate) ──
        float pulse = cachedPulse;

        // ── 4. Compute bytebeat -- three layered expressions ──
        float bbFreq = freq * cachedBbPitchMult;
        bbFreq = clamp(bbFreq, 8.f, 12000.f);
        float pitchScale = std::max(0.1f, bbFreq / 261.6f);
        // Stability affects bytebeat character: low stability = faster, more chaotic stepping
        float bbSpeed = pitchScale * (1.2f + (1.f - stability) * 0.8f + energy * 0.5f);
        // Organism pulse breathes the bytebeat speed
        bbSpeed *= (1.f + pulse * 0.06f);

        // Time curvature -- bends pattern geometry over tens of seconds (sin cached at control rate)
        float curvature = 0.0003f * energy * cachedCurvatureSin;
        // Strike kicks bytebeat time -- randomized chaotic jump per hit
        float strikeTimeKick = strikeEnv * strikeBbKick;
        {
            float bbInc1 = bbSpeed * (1.f + bbEpsilon + curvature) + strikeTimeKick;
            bbTimeFrac += bbInc1;
            int32_t steps1 = (int32_t)bbTimeFrac;
            bbTimeI += (uint32_t)steps1;
            bbTimeFrac -= (float)steps1;
        }
        uint32_t t = bbTimeI;

        // bbChar modulates shift amounts: at 0 shifts are minimal, at 1 aggressive
        int s1a = 4 + (int)(bbChar * 4.f);   // 4..8
        int s1b = 7 + (int)(bbChar * 5.f);   // 7..12
        int s1c = 5 + (int)(bbChar * 3.f);   // 5..8
        int s1d = 9 + (int)(bbChar * 4.f);   // 9..13

        int s2a = 6 + (int)(bbChar * 3.f);   // 6..9
        int s2b = 3 + (int)(bbChar * 4.f);   // 3..7
        int s2c = 10 - (int)(bbChar * 3.f);  // 10..7
        int s2d = 8 + (int)(bbChar * 3.f);   // 8..11
        int s2e = 4 + (int)(bbChar * 2.f);   // 4..6

        int s3a = 5 + (int)(bbChar * 3.f);   // 5..8
        int s3b = 8 - (int)(bbChar * 3.f);   // 8..5

        {
            float bbInc2 = bbSpeed * 0.517f * (1.f + bbEpsilon * 1.7f);
            bbTime2Frac += bbInc2;
            int32_t steps2 = (int32_t)bbTime2Frac;
            bbTime2I += (uint32_t)steps2;
            bbTime2Frac -= (float)steps2;
        }
        uint32_t t2 = bbTime2I;
        {
            float bbInc3 = bbSpeed * 1.333f + energy * 0.2f;
            bbTime3Frac += bbInc3;
            int32_t steps3 = (int32_t)bbTime3Frac;
            bbTime3I += (uint32_t)steps3;
            bbTime3Frac -= (float)steps3;
        }
        uint32_t t3 = bbTime3I;

        auto clampU8 = [&](int v) -> uint32_t {
            return (uint32_t)clamp(v, 0, 255);
        };
        auto lerpU8 = [&](uint32_t a, uint32_t b, float m) -> uint32_t {
            float v = (1.f - m) * (float)(a & 0xFFu) + m * (float)(b & 0xFFu);
            return clampU8((int)(v + 0.5f));
        };

        // Bytebeat modes: distinct algorithm families. bbChar morphs between two variants per mode.
        uint32_t raw1 = 0, raw2 = 0, raw3 = 0;
        switch (bbMode) {
            default:
            case 0: {
                // Original Xenostasis bytebeat
                uint32_t a1 = (t >> s1a) + (t >> s1b);
                uint32_t b1 = (t >> s1c) ^ (t >> s1d);
                raw1 = (a1 + b1) & 0xFFu;

                uint32_t a2 = ((t2 >> s2a) | (t2 >> s2b)) + (t2 >> s2c);
                uint32_t b2 = (t2 >> s2d) ^ (t2 >> s2e);
                raw2 = ((a2 * b2) >> 4) & 0xFFu;

                raw3 = ((t3 * ((t3 >> s3a) | (t3 >> s3b))) >> 6) & 0xFFu;
            } break;
            case 1: {
                // Classic melodic-ish family
                uint32_t v1a = (t * ((t >> s1a) | (t >> s1b)) + (t >> s1c)) & 0xFFu;
                uint32_t v1b = (t * ((t >> s1b) & (t >> s1c)) + (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t v2a = (t2 * ((t2 >> s2a) | (t2 >> s2c)) + (t2 >> s2b)) & 0xFFu;
                uint32_t v2b = ((t2 * 3u) ^ (t2 >> s2d) ^ (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t v3a = ((t3 * (t3 >> s3a)) + (t3 >> s3b)) & 0xFFu;
                uint32_t v3b = ((t3 * (t3 >> s3b)) ^ (t3 >> s3a)) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 2: {
                // Rhythmic / gated family
                uint32_t gate = ((t >> s1a) & (t >> s1b) & 0x1Fu);
                uint32_t v1a = ((t >> s1c) + (gate * 7u)) & 0xFFu;
                uint32_t v1b = ((t * (gate + 1u)) ^ (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t g2 = ((t2 >> s2b) & (t2 >> s2d) & 0x0Fu);
                uint32_t v2a = ((t2 >> s2c) + (g2 * 13u)) & 0xFFu;
                uint32_t v2b = ((t2 * (g2 + 2u)) ^ (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t g3 = ((t3 >> s3a) & 0x0Fu);
                uint32_t v3a = ((t3 >> s3b) + (g3 * 19u)) & 0xFFu;
                uint32_t v3b = ((t3 * (g3 + 3u)) ^ (t3 >> (s3b + 2))) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 3: {
                // Harsh / XOR grind
                uint32_t v1a = ((t * 9u) ^ (t >> s1a) ^ (t >> s1b) ^ (t >> s1d)) & 0xFFu;
                uint32_t v1b = ((t * (uint32_t)(s1b + 3)) ^ (t >> s1c) ^ (t >> s1a)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t v2a = ((t2 * 7u) ^ (t2 >> s2a) ^ (t2 >> s2d)) & 0xFFu;
                uint32_t v2b = (((t2 >> s2c) * (t2 >> s2b)) ^ (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t v3a = ((t3 * 5u) ^ (t3 >> s3a) ^ (t3 >> s3b)) & 0xFFu;
                uint32_t v3b = (((t3 >> s3a) + (t3 >> s3b)) * 3u) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 4: {
                // Tonal-ish (more repeats, less hash)
                uint32_t v1a = ((t >> s1a) + (t >> s1b) + (t >> (s1c + 1))) & 0xFFu;
                uint32_t v1b = ((t * ((t >> s1c) | 1u)) + (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t v2a = ((t2 >> s2a) + (t2 >> s2b) + (t2 >> s2c)) & 0xFFu;
                uint32_t v2b = ((t2 * ((t2 >> s2d) | 1u)) + (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t v3a = ((t3 >> s3a) + (t3 >> s3b)) & 0xFFu;
                uint32_t v3b = ((t3 * ((t3 >> (s3b + 1)) | 1u)) + (t3 >> (s3a + 1))) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 5: {
                // Steppy / arpeggiated
                uint32_t step = (t >> s1a) & 0x0Fu;
                uint32_t v1a = ((t * (step + 1u)) + (t >> s1b)) & 0xFFu;
                uint32_t v1b = (((t >> s1c) * (step + 3u)) ^ (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t step2 = (t2 >> s2b) & 0x07u;
                uint32_t v2a = ((t2 * (step2 + 2u)) + (t2 >> s2c)) & 0xFFu;
                uint32_t v2b = (((t2 >> s2a) * (step2 + 5u)) ^ (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t step3 = (t3 >> s3a) & 0x07u;
                uint32_t v3a = ((t3 * (step3 + 2u)) + (t3 >> s3b)) & 0xFFu;
                uint32_t v3b = (((t3 >> s3b) * (step3 + 6u)) ^ (t3 >> (s3a + 2))) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 6: {
                // Crackle / bitmask textures
                uint32_t m1 = (t >> s1a) | (t >> s1b);
                uint32_t v1a = ((t * (m1 & 7u)) ^ (t >> s1c)) & 0xFFu;
                uint32_t v1b = ((t * ((m1 & 15u) + 1u)) + (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t m2 = (t2 >> s2a) | (t2 >> s2d);
                uint32_t v2a = ((t2 * (m2 & 7u)) ^ (t2 >> s2c)) & 0xFFu;
                uint32_t v2b = ((t2 * ((m2 & 15u) + 1u)) + (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t m3 = (t3 >> s3a) | (t3 >> s3b);
                uint32_t v3a = ((t3 * (m3 & 7u)) ^ (t3 >> (s3b + 1))) & 0xFFu;
                uint32_t v3b = ((t3 * ((m3 & 15u) + 1u)) + (t3 >> (s3a + 1))) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
            case 7: {
                // “Organism” family: more tied to pitch + energy
                uint32_t e = (uint32_t)clamp((int)(energy * 31.f + 0.5f), 0, 31);
                uint32_t v1a = ((t * ((t >> (s1a - 1)) | 1u)) + (t >> s1b) + e * 17u) & 0xFFu;
                uint32_t v1b = (((t >> s1c) ^ (t * (e + 1u))) + (t >> s1d)) & 0xFFu;
                raw1 = lerpU8(v1a, v1b, bbChar);

                uint32_t v2a = ((t2 * ((t2 >> (s2a - 1)) | 1u)) + (t2 >> s2c) + e * 23u) & 0xFFu;
                uint32_t v2b = (((t2 >> s2b) ^ (t2 * (e + 2u))) + (t2 >> s2e)) & 0xFFu;
                raw2 = lerpU8(v2a, v2b, bbChar);

                uint32_t v3a = ((t3 * ((t3 >> (s3a - 1)) | 1u)) + (t3 >> s3b) + e * 29u) & 0xFFu;
                uint32_t v3b = (((t3 >> s3b) ^ (t3 * (e + 3u))) + (t3 >> (s3a + 1))) & 0xFFu;
                raw3 = lerpU8(v3a, v3b, bbChar);
            } break;
        }

        // Convert to -1..1
        float bb1 = (float)raw1 / 128.f - 1.f;
        float bb2 = (float)raw2 / 128.f - 1.f;
        float bb3 = (float)raw3 / 128.f - 1.f;

        // Density controls how raw vs smooth the bytebeat is
        // Low density: heavily smoothed (warm drone). High density: raw texture.
        float bbSmoothCoeff = 0.01f + smoothedDensity * 0.4f;
        bbSmoothCoeff = clamp(bbSmoothCoeff, 0.01f, 0.45f);
        // Stability further tames the bytebeat -- high stability = smoother
        bbSmoothCoeff *= (0.4f + (1.f - stability) * 0.6f);
        // Let bytebeat breathe more when provoked -- reduce smoothing in high energy
        if (energy > 0.7f)
            bbSmoothCoeff *= (1.f - (energy - 0.7f) * 0.5f);

        bbSmoothed += (bb1 - bbSmoothed) * bbSmoothCoeff;
        bbLayer2 += (bb2 - bbLayer2) * (bbSmoothCoeff * 0.7f);
        bbLayer3 += (bb3 - bbLayer3) * (bbSmoothCoeff * 1.2f);

        // Mix layers: layer1 is main, layer2 adds body, layer3 adds shimmer
        float bbMixed = bbSmoothed * 0.5f
                      + bbLayer2 * 0.3f
                      + bbLayer3 * 0.2f * (0.3f + energy * 0.7f);  // L3 opens up with energy

        // Bandpass-ish filter tracked to fundamental -- keeps bytebeat tonal
        float bpCoeff = clamp(bbFreq / sampleRate * 6.2832f * 4.f, 0.001f, 0.5f);
        bbFilterState += (bbMixed - bbFilterState) * bpCoeff;
        float bbTonal = bbMixed - bbFilterState;  // highpass removes DC rumble
        // Blend filtered vs raw based on stability (high stability = more tonal filtering)
        float bbFinal = bbMixed * (1.f - stability * 0.5f) + bbTonal * stability * 0.5f;

        // ── 5. Slew-limit bytebeat for modulation (crackle-free control signal) ──
        // bbFinal stays raw for audio mix. bbSlewed is used for all modulation paths.
        bbLowpassed += 0.003f * (bbMixed - bbLowpassed);
        float slewDelta = bbLowpassed - bbSlewed;
        float maxStep = bbMaxStep;  // sample-rate aware
        if (slewDelta > maxStep) slewDelta = maxStep;
        if (slewDelta < -maxStep) slewDelta = -maxStep;
        bbSlewed += slewDelta;

        // ── 6. Cross-modulation (nonlinear, using slew-limited control) ──
        // Organism pulse modulates cross depth
        float crossWithPulse = crossScaled * (1.f + pulse * 0.025f);

        // BB → WT: phase warp (from slewed control)
        float bbToWtWarp = bbSlewed * crossWithPulse * 0.6f;

        // BB → WT: nonlinear frame expansion (tanh + square = asymmetric bloom)
    #ifdef METAMODULE
        float chaosShape = xsTanhFast(bbSlewed * (1.f + energy * 2.f));
    #else
        float chaosShape = std::tanh(bbSlewed * (1.f + energy * 2.f));
    #endif
        float nonlinearExpand = chaosShape * chaosShape * crossWithPulse * 14.f;

        // WT → BB: density + shift depth + time bias (precomputed for next iteration)
        // This is applied via the smoothedDensity and bbEpsilon update paths above

        // Phase warp depth modulated by cross
        phaseWarpDepth = bbToWtWarp * (0.5f + energy * 0.5f);

        // Strike excites frame velocity -- scaled by punch
        if (strikeEnv > 0.01f) {
            float strikeDir = (chaosShape > 0.f) ? 1.f : -1.f;
            strikeDir *= (pulse > 0.f) ? 1.f : -1.f;
            float frameKick = (0.005f + punchKnob * 0.03f + energy * 0.01f);
            frameVelocity += strikeEnv * strikeDir * frameKick;
        }

        // Strike jumps frame position (randomized, scaled by punch)
        float strikeFrameOffset = strikeEnv * strikeFrameJump * (0.3f + punchKnob * 0.7f);

        // Organism pulse leans frame velocity
        frameVelocity += pulse * 0.0002f;

        // Frame position: base drift + nonlinear bloom + strike jump
        float framePos = frameDrift + nonlinearExpand + strikeFrameOffset;
        framePos = clamp(framePos, 0.f, (float)(XS_FRAMES - 1));

        // ── 7. Compute wavetable sample ──
        float phaseInc = freq * dt;

        // Left channel
        wtPhaseL += phaseInc;
        if (wtPhaseL >= 1.f) wtPhaseL -= 1.f;
        // Strike adds randomized extra phase warp per hit
        float totalWarpL = phaseWarpDepth + strikeEnv * strikeWarpAmount;
        float warpedPhaseL = wtPhaseL + totalWarpL *
    #ifdef METAMODULE
            xsSin2Pi(wtPhaseL)
    #else
            std::sin(wtPhaseL * 2.f * M_PI)
    #endif
            ;
        float wtSampleL = readWavetable(tableIdx, framePos, warpedPhaseL);

        // Right channel (storm-divergent stereo)
        wtPhaseR += phaseInc;
        if (wtPhaseR >= 1.f) wtPhaseR -= 1.f;
        float stereoOffset = 0.003f + energy * 0.01f;
        // Storm stereo: spatial instability from BB + organism decorrelation
        float stereoChaos = energy * 0.025f *
    #ifdef METAMODULE
            xsSinRad(bbSlewed * 3.f + (float)organismPhase * 0.3f)
    #else
            std::sin(bbSlewed * 3.f + (float)organismPhase * 0.3f)
    #endif
            ;
        float totalWarpR = phaseWarpDepth + strikeEnv * strikeWarpAmount * 0.8f;  // slightly different per channel
        float warpedPhaseR = wtPhaseR + stereoOffset + stereoChaos + totalWarpR *
    #ifdef METAMODULE
            xsSin2Pi(wtPhaseR)
    #else
            std::sin(wtPhaseR * 2.f * M_PI)
    #endif
            ;
        float wtSampleR = readWavetable(tableIdx, framePos, warpedPhaseR);

        // ── 8. Energy-driven spectral tilt (harmonic falloff without table rebuild) ──
        // One-pole lowpass on WT output. High energy = filter opens (more harmonics).
        // Strike momentarily opens the filter wide (transient brightness)
        float tiltCutoff = 0.15f + energy * 0.75f;  // 0.15..0.90
        tiltCutoff += strikeEnv * (0.6f - tiltCutoff * 0.5f);  // strike pushes toward bright
        tiltCutoff = clamp(tiltCutoff, 0.05f, 0.98f);
        wtTiltStateL += tiltCutoff * (wtSampleL - wtTiltStateL);
        wtTiltStateR += tiltCutoff * (wtSampleR - wtTiltStateR);
        // Blend: at low energy, use filtered (warm). At high energy, use mostly raw (bright).
        float tiltMix = 0.3f + energy * 0.7f;  // 0.3..1.0
        wtSampleL = wtTiltStateL * (1.f - tiltMix) + wtSampleL * tiltMix;
        wtSampleR = wtTiltStateR * (1.f - tiltMix) + wtSampleR * tiltMix;

        // ── 8b. Strike harmonic boost (scaled by punch) ──
        if (strikeEnv > 0.01f) {
            float harmonicBoost = 1.f + strikeEnv * punchKnob * (1.2f + energy * 1.8f);
            wtSampleL *= harmonicBoost;
            wtSampleR *= harmonicBoost;
        }

        // ── 8c. Resonant noise burst (metallic transient) ──
        float metallicL = 0.f, metallicR = 0.f;
        if (strikeEnv > 0.005f) {
            // Generate noise (xorshift)
            strikeNoiseRng ^= strikeNoiseRng << 13;
            strikeNoiseRng ^= strikeNoiseRng >> 17;
            strikeNoiseRng ^= strikeNoiseRng << 5;
            float noiseL = (float)(strikeNoiseRng & 0xFFFF) / 32768.f - 1.f;
            strikeNoiseRng ^= strikeNoiseRng << 7;
            float noiseR = (float)(strikeNoiseRng & 0xFFFF) / 32768.f - 1.f;

            // Resonant frequency: pitch-tracked + randomized per hit
            float resonFreq = freq * strikeResonMult * (1.5f + energy * 2.f);
            float bpCoeffStrike = clamp(resonFreq / sampleRate * 6.2832f, 0.01f, 0.6f);

            // Bandpass: lowpass then subtract DC (simple resonant filter)
            strikeBpStateL += bpCoeffStrike * (noiseL - strikeBpStateL);
            strikeBpStateR += bpCoeffStrike * (noiseR - strikeBpStateR);
            metallicL = (strikeBpStateL - strikeBpPrevL) * strikeEnv * (0.3f + punchKnob * 0.7f + smoothedChaos * 0.3f);
            metallicR = (strikeBpStateR - strikeBpPrevR) * strikeEnv * (0.3f + punchKnob * 0.7f + smoothedChaos * 0.3f);
            strikeBpPrevL = strikeBpStateL;
            strikeBpPrevR = strikeBpStateR;
        }

        // ── 9. Sub stabilizer (under high chaos/energy) ──
        subPhase += freq * dt;
        if (subPhase >= 1.f) subPhase -= 1.f;
        float subSine =
    #ifdef METAMODULE
            xsSin2Pi(subPhase)
    #else
            std::sin(subPhase * 2.f * M_PI)
    #endif
            ;
        float subLevel = energy * smoothedChaos * 0.2f;  // only active under chaos + energy

        // -- 9b. FM synthesis --
        float fmSample = 0.f;
        float fmSampleR = 0.f;  // stereo FM for drone
        bool fmEnabled = (fmVol > 0.0005f);
        if (fmEnabled && percussiveMode && strikeEnv > 0.003f) {
            // Percussive FM drum (2-op with decaying index)
            float pitchDropMult = 1.f + strikeFmPitchDrop * punchKnob * strikeEnv * strikeEnv;
            float fmCarrierFreq = freq * pitchDropMult;
            float fmModFreq = fmCarrierFreq * strikeFmRatio;

            float fmIndex = strikeFmIndex * strikeEnv * (0.3f + punchKnob * 0.7f);
            fmIndex += smoothedChaos * 3.f * strikeEnv;

            fmModPhase += fmModFreq * dt;
            if (fmModPhase >= 1.f) fmModPhase -= 1.f;
            float modSignal =
#ifdef METAMODULE
                xsSin2Pi(fmModPhase)
#else
                std::sin(fmModPhase * 2.f * (float)M_PI)
#endif
                ;

            fmCarrierPhase += fmCarrierFreq * dt + modSignal * fmIndex * fmCarrierFreq * dt;
            if (fmCarrierPhase >= 1.f) fmCarrierPhase -= 1.f;
            if (fmCarrierPhase < 0.f) fmCarrierPhase += 1.f;
            fmSample =
#ifdef METAMODULE
                xsSin2Pi(fmCarrierPhase)
#else
                std::sin(fmCarrierPhase * 2.f * (float)M_PI)
#endif
                ;
            fmSample =
#ifdef METAMODULE
                xsTanhFast(fmSample * (1.f + smoothedChaos * 1.5f))
#else
                std::tanh(fmSample * (1.f + smoothedChaos * 1.5f))
#endif
                ;
            fmSampleR = fmSample;  // mono in perc mode
        }
        else if (fmEnabled && !percussiveMode) {
            // Drone FM: LFOs and derived parameters cached at control rate.
            // Only per-sample micro-chaos injection and oscillator phases run here.

            float droneIndex = cachedDroneIndex + bbSlewed * smoothedChaos * 1.5f;

            // Frequencies
            float fmCarrierFreq = freq;
            float fmMod1Freq = fmCarrierFreq * cachedDroneRatio;
            float fmMod2Freq = fmMod1Freq * cachedMod2Ratio;

            // Advance mod2 phase
            droneFmMod2Phase += fmMod2Freq * dt;
            if (droneFmMod2Phase >= 1.f) droneFmMod2Phase -= 1.f;
            float mod2Signal =
#ifdef METAMODULE
                xsSin2Pi(droneFmMod2Phase)
#else
                std::sin(droneFmMod2Phase * 2.f * (float)M_PI)
#endif
                ;

            // Advance mod1 phase (FM'd by mod2)
            fmModPhase += fmMod1Freq * dt + mod2Signal * cachedMod2Index * fmMod1Freq * dt;
            if (fmModPhase >= 1.f) fmModPhase -= 1.f;
            if (fmModPhase < 0.f) fmModPhase += 1.f;
            float mod1Signal =
#ifdef METAMODULE
                xsSin2Pi(fmModPhase)
#else
                std::sin(fmModPhase * 2.f * (float)M_PI)
#endif
                ;

            // Advance carrier phase (FM'd by mod1)
            fmCarrierPhase += fmCarrierFreq * dt + mod1Signal * droneIndex * fmCarrierFreq * dt;
            if (fmCarrierPhase >= 1.f) fmCarrierPhase -= 1.f;
            if (fmCarrierPhase < 0.f) fmCarrierPhase += 1.f;
            fmSample =
#ifdef METAMODULE
                xsSin2Pi(fmCarrierPhase)
#else
                std::sin(fmCarrierPhase * 2.f * (float)M_PI)
#endif
                ;

            // Stereo: right channel uses phase offset + slightly different index
            float stereoDetune = 1.003f + cachedLfo3 * 0.005f;
            float mod1R =
#ifdef METAMODULE
                xsSinRad(fmModPhase * 2.f * (float)M_PI + 0.3f)
#else
                std::sin(fmModPhase * 2.f * (float)M_PI + 0.3f)
#endif
                ;
            float phaseR = fmCarrierPhase + 0.07f;
            if (phaseR >= 1.f) phaseR -= 1.f;
            float phR = (phaseR + mod1R * droneIndex * 0.8f * dt * freq * stereoDetune);
            fmSampleR =
#ifdef METAMODULE
                xsSin2Pi(phR)
#else
                std::sin(phR * 2.f * (float)M_PI)
#endif
                ;

            // Waveshaping: chaos adds grit, stability keeps it cleaner
            fmSample =
#ifdef METAMODULE
                xsTanhFast(fmSample * cachedSatAmount)
#else
                std::tanh(fmSample * cachedSatAmount)
#endif
                ;
            fmSampleR =
#ifdef METAMODULE
                xsTanhFast(fmSampleR * cachedSatAmount)
#else
                std::tanh(fmSampleR * cachedSatAmount)
#endif
                ;
        }

        // ── 10. Mix ──
        // Wavetable volume knob directly scales WT contribution
        float wtWeight = wtVol * (0.55f + (1.f - smoothedDensity) * 0.3f);  // 0..0.85
        float bbWeight = 0.25f + smoothedDensity * 0.55f;          // 0.25..0.80
        // Energy adds harmonic bloom from bytebeat
        bbWeight += energy * 0.15f;
        // Stability slightly attenuates BB at high values (calmer sound)
        bbWeight *= (0.6f + (1.f - stability) * 0.4f);
        // BB volume trim: 0..200% (scaled vs original balance)
        bbWeight *= bbVol * 1.6f;

        // Slight stereo offset on BB via layer phase differences
        float bbL = bbFinal;
        float bbR = bbFinal * 0.8f + bbLayer2 * 0.2f;  // slightly different BB color per ear

        float mixL = wtSampleL * wtWeight + bbL * bbWeight + subSine * subLevel + metallicL + fmSample * fmVol * 0.8f;
        float mixR = wtSampleR * wtWeight + bbR * bbWeight + subSine * subLevel + metallicR + fmSampleR * fmVol * 0.8f;

        // ── 11. Percussive amplitude shaping (clock mode only) ──
        // True VCA gate: silent between hits, envelope opens on strike
        if (percussiveMode) {
            mixL *= strikeEnv;
            mixR *= strikeEnv;
        }

        // ── 11. Soft-clip, drive and output ──
        // Drive: soft-knee compressor + parallel tanh saturation
        // At 0: clean soft-clip. At 1: heavy punchy compression
        float outL = mixL;
        float outR = mixR;
        if (driveKnob > 0.01f) {
            // Pre-gain (push into saturation)
            float preGain = 1.f + driveKnob * 4.f;  // 1..5x
            float driveL = outL * preGain;
            float driveR = outR * preGain;

            // Soft-knee compression: reduce peaks, boost quiet parts
            // Threshold gets lower as drive increases
            float threshold = 1.2f - driveKnob * 0.8f;  // 1.2..0.4
            float ratio = 1.f + driveKnob * 5.f;  // 1:1..1:6 compression
            auto compress = [&](float x) -> float {
                float absX = std::fabs(x);
                if (absX > threshold) {
                    float excess = absX - threshold;
                    float compressed = threshold + excess / ratio;
                    return (x > 0.f ? compressed : -compressed);
                }
                return x;
            };
            float compL = compress(driveL);
            float compR = compress(driveR);

            // Parallel tanh saturation (warm harmonic distortion)
            float satL =
#ifdef METAMODULE
                xsTanhFast(driveL * (1.f + driveKnob))
#else
                std::tanh(driveL * (1.f + driveKnob))
#endif
                ;
            float satR =
#ifdef METAMODULE
                xsTanhFast(driveR * (1.f + driveKnob))
#else
                std::tanh(driveR * (1.f + driveKnob))
#endif
                ;

            // Blend: more drive = more saturated path
            float dryWet = driveKnob;  // 0..1
            outL = compL * (1.f - dryWet * 0.5f) + satL * dryWet * 0.5f;
            outR = compR * (1.f - dryWet * 0.5f) + satR * dryWet * 0.5f;

            // Makeup gain to compensate for compression
            float makeup = 1.f + driveKnob * 0.6f;
            outL *= makeup;
            outR *= makeup;
        } else {
            outL = softClip(outL);
            outR = softClip(outR);
        }
        outL *= 5.f;
        outR *= 5.f;

        outputs[LEFT_OUTPUT].setVoltage(outL);
        outputs[RIGHT_OUTPUT].setVoltage(outR);
    }
};

#ifdef METAMODULE
std::array<float, Xenostasis::XS_SIN_LUT_SIZE> Xenostasis::xsSinLut{};
bool Xenostasis::xsSinLutInited = false;
#endif

// ============================================================================
// Label helper (identical pattern to SlideWyrm)
// ============================================================================

#ifndef METAMODULE
struct XsPanelLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool isTitle;

    XsPanelLabel(Vec pos, const std::string& text, float fontSize, NVGcolor color, bool isTitle = false)
        : text(text), fontSize(fontSize), color(color), isTitle(isTitle)
    {
        box.pos = pos;
        box.size = Vec(200, fontSize + 4);
    }

    void draw(const DrawArgs& args) override {
        std::string fontPath = isTitle
            ? asset::plugin(pluginInstance, "res/CinzelDecorative-Bold.ttf")
            : asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        float y = fontSize / 2.f;
        nvgText(args.vg, 0, y, text.c_str(), NULL);
        if (isTitle) {
            nvgText(args.vg, 0.5f, y, text.c_str(), NULL);
            nvgText(args.vg, -0.5f, y, text.c_str(), NULL);
            nvgText(args.vg, 0, y + 0.4f, text.c_str(), NULL);
            nvgText(args.vg, 0, y - 0.4f, text.c_str(), NULL);
        }
    }
};

static XsPanelLabel* xsCreateLabelPxYOffset(Vec mmPos, float yOffsetPx, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
    Vec pxPos = mm2px(mmPos);
    pxPos.y += yOffsetPx;
    return new XsPanelLabel(pxPos, text, fontSize, color, isTitle);
}
#endif

// ============================================================================
// Widget
// ============================================================================

#ifndef METAMODULE
using XenostasisKnob = MVXKnob;
#else
using XenostasisKnob = RoundSmallBlackKnob;
#endif

struct XenostasisWidget : ModuleWidget {
    XenostasisWidget(Xenostasis* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Xenostasis.svg")));

#ifndef METAMODULE
        auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Xenostasis.png"));
        panelBg->box.pos = Vec(0, 0);
        panelBg->box.size = box.size;
    // Default is Cover; top-align so if there's any mismatch, it crops from the bottom
    // (preserving the top headline artwork).
    panelBg->alignY = 0.f;
        addChild(panelBg);
#endif

        // Screws

#ifndef METAMODULE
    NVGcolor uiText = nvgRGB(0xa8, 0xc7, 0x9f);
#endif

        // ── Panel layout constants (mm) ──
        // 16HP = 81.28mm wide
        float xL = 10.f;
        float xR = 71.28f;
        float xStep = (xR - xL) / 4.f;
        float x1 = xL;
        float x2 = xL + xStep;
        float x3 = xL + xStep * 2.f;
        float x4 = xL + xStep * 3.f;
        float x5 = xR;

        // Global UI shifts (pixels): move everything down by 14px.
        float knobsYOffsetPx = 38.f;
        #ifndef METAMODULE
            knobsYOffsetPx += 3.f;
        #endif
        float ioYOffsetPx = 54.f;
		float ioShiftUpPx = 0.f;
	#ifndef METAMODULE
		ioShiftUpPx = 12.f;
	#endif
        auto toKnobPx = [&](Vec mmPos) -> Vec {
            Vec pxPos = mm2px(mmPos);
            pxPos.y += knobsYOffsetPx;
            return pxPos;
        };
        auto toIoPx = [&](Vec mmPos) -> Vec {
            Vec pxPos = mm2px(mmPos);
			pxPos.y += ioYOffsetPx - ioShiftUpPx;
            return pxPos;
        };

        // ──────────────────────────────
        // Row 2: Core (even spacing, same knob size)
        // ──────────────────────────────
        float row2Y = 25.f;
    #ifndef METAMODULE
        addChild(xsCreateLabelPxYOffset(Vec(x1, row2Y - 7.f), knobsYOffsetPx, "PITCH", 7.5f, uiText));
        addChild(xsCreateLabelPxYOffset(Vec(x2, row2Y - 7.f), knobsYOffsetPx, "TABLE", 7.5f, uiText));
        addChild(xsCreateLabelPxYOffset(Vec(x3, row2Y - 7.f), knobsYOffsetPx, "CHAOS", 7.5f, uiText));
        addChild(xsCreateLabelPxYOffset(Vec(x4, row2Y - 7.f), knobsYOffsetPx, "STAB", 7.5f, uiText));
        addChild(xsCreateLabelPxYOffset(Vec(x5, row2Y - 7.f), knobsYOffsetPx, "HOMEO", 7.5f, uiText));
    #endif
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x1, row2Y)), module, Xenostasis::PITCH_PARAM));
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x2, row2Y)), module, Xenostasis::TABLE_PARAM));
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x3, row2Y)), module, Xenostasis::CHAOS_PARAM));
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x4, row2Y)), module, Xenostasis::STABILITY_PARAM));
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x5, row2Y)), module, Xenostasis::HOMEOSTASIS_PARAM));

        // ──────────────────────────────
        // Row 3: Hit + modulation
        // ──────────────────────────────
        float row3Y = 42.f;
#ifndef METAMODULE
    addChild(xsCreateLabelPxYOffset(Vec(x1, row3Y - 7.f), knobsYOffsetPx, "DECAY", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x2, row3Y - 7.f), knobsYOffsetPx, "PUNCH", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x3, row3Y - 7.f), knobsYOffsetPx, "CROSS", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x4, row3Y - 7.f), knobsYOffsetPx, "DENS", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x5, row3Y - 7.f), knobsYOffsetPx, "BB", 7.5f, uiText));
#endif
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x1, row3Y)), module, Xenostasis::DECAY_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x2, row3Y)), module, Xenostasis::PUNCH_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x3, row3Y)), module, Xenostasis::CROSS_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x4, row3Y)), module, Xenostasis::DENSITY_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x5, row3Y)), module, Xenostasis::BBCHAR_PARAM));

        // ──────────────────────────────
        // Row 4: Mix (volumes + drive grouped)
        // ──────────────────────────────
        float row4Y = 59.f;
        float lightX = 76.f;
        float lightRowY = row4Y + 17.f;
#ifndef METAMODULE
    addChild(xsCreateLabelPxYOffset(Vec(x2, row4Y - 7.f), knobsYOffsetPx, "WT", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x3, row4Y - 7.f), knobsYOffsetPx, "FM", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x4, row4Y - 7.f), knobsYOffsetPx, "DRIVE", 7.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(x1, row4Y - 7.f), knobsYOffsetPx, "BB VOL", 7.5f, uiText));
	addChild(xsCreateLabelPxYOffset(Vec(x5, row4Y - 7.f), knobsYOffsetPx, "MODE", 7.5f, uiText));
    {
		auto* energyLabel = xsCreateLabelPxYOffset(Vec(lightX, lightRowY - 4.f), knobsYOffsetPx - 12.f, "ENERGY", 5.5f, uiText);
        energyLabel->box.pos.x -= 15.f;
        addChild(energyLabel);

		auto* stormLabel = xsCreateLabelPxYOffset(Vec(lightX, lightRowY + 4.f), knobsYOffsetPx - 12.f, "STORM", 5.5f, uiText);
        stormLabel->box.pos.x -= 15.f;
        addChild(stormLabel);
    }
#endif
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x2, row4Y)), module, Xenostasis::WTVOL_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x3, row4Y)), module, Xenostasis::FMVOL_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x4, row4Y)), module, Xenostasis::DRIVE_PARAM));
    addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x1, row4Y)), module, Xenostasis::BBVOL_PARAM));
	addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x5, row4Y)), module, Xenostasis::BBMODE_PARAM));
    Vec energyLightPos = toKnobPx(Vec(lightX, lightRowY - 4.f));
    energyLightPos.x -= 15.f;
    addChild(createLightCentered<SmallLight<GreenLight>>(energyLightPos, module, Xenostasis::ENERGY_LIGHT));

    Vec stormLightPos = toKnobPx(Vec(lightX, lightRowY + 4.f));
    stormLightPos.x -= 15.f;
    addChild(createLightCentered<SmallLight<GreenLight>>(stormLightPos, module, Xenostasis::STORM_LIGHT));

        // ──────────────────────────────
        // Row 4b: Bytebeat tuning
        // ──────────────────────────────
        // Keep vertical spacing consistent with other rows (row2->row3 and row3->row4 are 17mm).
        float row4bY = 76.f;
#ifndef METAMODULE
    addChild(xsCreateLabelPxYOffset(Vec(x1, row4bY - 7.f), knobsYOffsetPx, "BB TUNE", 7.5f, uiText));
#endif
        addParam(createParamCentered<XenostasisKnob>(toKnobPx(Vec(x1, row4bY)), module, Xenostasis::BBPITCH_PARAM));

        // ──────────────────────────────
        // Row 5: CV inputs (5 jacks)
        // ──────────────────────────────
        float row5Y = 82.f;
        // Even port spacing: align to the same 5-column grid as the knobs.
        float cv1X = x1, cv2X = x2, cv3X = x3, cv4X = x4, cv5X = x5;
		const float cvExtraYOffsetPx = 20.f;
		auto toCvIoPx = [&](Vec mmPos) -> Vec {
			Vec pxPos = toIoPx(mmPos);
			pxPos.y += cvExtraYOffsetPx;
			return pxPos;
		};
#ifndef METAMODULE
    addChild(xsCreateLabelPxYOffset(Vec(cv1X, row5Y - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "V/OCT", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv2X, row5Y - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "TRIG", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv3X, row5Y - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "CHAOS", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv4X, row5Y - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "CROSS", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv5X, row5Y - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "TBL", 6.5f, uiText));
#endif
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv1X, row5Y)), module, Xenostasis::VOCT_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv2X, row5Y)), module, Xenostasis::CLOCK_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv3X, row5Y)), module, Xenostasis::CHAOS_CV_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv4X, row5Y)), module, Xenostasis::CROSS_CV_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv5X, row5Y)), module, Xenostasis::TABLE_CV_INPUT));

        // ──────────────────────────────
        // Row 5b: Extra CV inputs (3 jacks) — placed above TRIG/CHAOS/CROSS
        // ──────────────────────────────
        float row5bY = row5Y - 12.f;
        float cv6X = x2, cv7X = x3, cv8X = x4;
#ifndef METAMODULE
    addChild(xsCreateLabelPxYOffset(Vec(cv6X, row5bY - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "DENS", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv7X, row5bY - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "BB", 6.5f, uiText));
    addChild(xsCreateLabelPxYOffset(Vec(cv8X, row5bY - 5.f), (ioYOffsetPx - ioShiftUpPx) - 6.f + cvExtraYOffsetPx, "MODE", 6.5f, uiText));
#endif
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv6X, row5bY)), module, Xenostasis::DENSITY_CV_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv7X, row5bY)), module, Xenostasis::BBCHAR_CV_INPUT));
    addInput(createInputCentered<MVXPort>(toCvIoPx(Vec(cv8X, row5bY)), module, Xenostasis::BBMODE_CV_INPUT));

        // ──────────────────────────────
        // Row 6 (bottom): Outputs
        // ──────────────────────────────
        float row6Y = 108.f;
        // Even port spacing: align outputs to the same grid columns.
        float outLX = x2, outRX = x4;
        Vec outLPx = toIoPx(Vec(outLX, row6Y));
        Vec outRPx = toIoPx(Vec(outRX, row6Y));
        // Move outs outward by 25px.
        outLPx.x -= 25.f;
        outRPx.x += 25.f;
        // Global IO moved down by 14px; pull L/R outs back up.
        outLPx.y += -15.f;
        outRPx.y += -15.f;

    #ifndef METAMODULE
    		{
    			constexpr float outLabelFontPx = 8.5f;
    			constexpr float portHalfPx = 12.f;
    			constexpr float marginPx = 4.f;

    			float leftLabelY = outLPx.y - portHalfPx - marginPx - outLabelFontPx * 0.5f;
                auto* leftOutLabel = new XsPanelLabel(Vec(outLPx.x, leftLabelY), "LEFT", outLabelFontPx, uiText);
    			addChild(leftOutLabel);

    			float rightLabelY = outRPx.y - portHalfPx - marginPx - outLabelFontPx * 0.5f;
                auto* rightOutLabel = new XsPanelLabel(Vec(outRPx.x, rightLabelY), "RIGHT", outLabelFontPx, uiText);
    			addChild(rightOutLabel);
    		}
    #endif
        addOutput(createOutputCentered<MVXPort>(outLPx, module, Xenostasis::LEFT_OUTPUT));
        addOutput(createOutputCentered<MVXPort>(outRPx, module, Xenostasis::RIGHT_OUTPUT));
    }
};

// ============================================================================
// Model registration
// ============================================================================

Model* modelXenostasis = createModel<Xenostasis, XenostasisWidget>("Xenostasis");