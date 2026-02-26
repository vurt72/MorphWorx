/*
 * PhaseonWavetable.cpp — Built-in table generation + WAV loader
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#include "PhaseonWavetable.hpp"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

// Rack SDK FFT wrapper (uses PFFFT under the hood)
#include <dsp/fft.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace phaseon {

static inline uint32_t hash32_local(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline float u01_local(uint32_t x) {
    return (float)(x & 0x00FFFFFFu) / 16777216.0f;
}

static inline float clamp01_local(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline float lerp_local(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float clamp_local(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void smoothEndpoints(float* frame, int N, int xf) {
    if (!frame || N <= 0) return;
    xf = std::max(0, std::min(xf, N / 2));
    if (xf <= 1) return;
    for (int i = 0; i < xf; ++i) {
        float a = (float)i / (float)(xf - 1);
        float s0 = frame[i];
        float s1 = frame[N - xf + i];
        float m = s0 * (1.0f - a) + s1 * a;
        frame[i] = m;
        frame[N - xf + i] = m;
    }
}

static void removeDcAndNormalize(float* frame, int N, float peakTarget = 0.95f) {
    if (!frame || N <= 0) return;
    float mean = 0.0f;
    for (int i = 0; i < N; ++i) mean += frame[i];
    mean /= (float)N;
    for (int i = 0; i < N; ++i) frame[i] -= mean;

    float peak = 0.0f;
    for (int i = 0; i < N; ++i) peak = std::max(peak, std::fabs(frame[i]));
    if (peak > 1e-6f) {
        float g = peakTarget / peak;
        for (int i = 0; i < N; ++i) frame[i] *= g;
    }
}

static Wavetable makeXenoTable_FeralMachine(int N, int frames) {
    Wavetable wt;
    wt.name = "FeralMachine";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        int mode = f % 8;
        uint32_t rng = 0xDEAD1337u;
        rng ^= (uint32_t)(f * 13337 + 54321);
        auto xorshift = [&]() -> float {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float)(rng & 0xFFFFu) / 32768.0f - 1.0f;
        };

        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            float t = (float)s / (float)N;
            float v = 0.0f;
            switch (mode) {
            default:
            case 0: {
                // Inharmonic metal cluster (non-periodic ratios, smoothed at endpoints)
                const float ratios[6] = { 1.f, 1.59f, 2.83f, 4.17f, 5.43f, 7.11f };
                for (int i = 0; i < 6; ++i) {
                    float amp = 0.5f / (1.0f + (float)i * 0.4f);
                    float detune = 1.0f + (framePos - 0.5f) * 0.02f * (float)i;
                    v += amp * sinf(2.f * (float)M_PI * t * ratios[i] * detune);
                }
                break;
            }
            case 1: {
                // Ring-mod screech
                float carrier = sinf(2.f * (float)M_PI * t);
                float modFreq = 3.71f + framePos * 8.f;
                float mod = sinf(2.f * (float)M_PI * t * modFreq);
                v = carrier * mod;
                v = tanhf(v * (2.f + framePos * 3.f));
                break;
            }
            case 2: {
                // Filtered noise burst with resonance
                float noise = xorshift();
                float resonFreq = 2.f + framePos * 10.f;
                v = noise * sinf(2.f * (float)M_PI * t * resonFreq) * 0.7f;
                v += sinf(2.f * (float)M_PI * t) * 0.3f;
                break;
            }
            case 3: {
                // FM growl (high index)
                float modIdx = 6.f + framePos * 12.f;
                float modSig = sinf(2.f * (float)M_PI * t * 3.17f) * modIdx;
                v = sinf(2.f * (float)M_PI * t + modSig);
                v = tanhf(v * 2.f);
                break;
            }
            case 4: {
                // Wavefolder cascade
                float sine = sinf(2.f * (float)M_PI * t);
                float amount = 3.f + framePos * 8.f;
                v = sinf(sine * amount);
                v = sinf(v * (2.f + framePos * 3.f));
                break;
            }
            case 5: {
                // Broken oscillator (sub-octave + noise)
                float sub = (sinf(2.f * (float)M_PI * t * 0.5f) > 0.f) ? 1.f : -1.f;
                float hiNoise = xorshift() * (0.3f + framePos * 0.7f);
                v = sub * 0.5f + hiNoise * 0.5f;
                v += sinf(2.f * (float)M_PI * t * 5.73f) * 0.3f;
                break;
            }
            case 6: {
                // Harsh sync-like harmonic explosion
                float master = fmodf(t, 1.f);
                float slaveRatio = 3.5f + framePos * 8.f;
                v = sinf(2.f * (float)M_PI * master * slaveRatio);
                v *= (1.f - master * 0.3f);
                break;
            }
            case 7: {
                // Granular metallic texture
                float grain1 = sinf(2.f * (float)M_PI * t * 1.41f) * sinf(2.f * (float)M_PI * t * 7.07f);
                float grain2 = sinf(2.f * (float)M_PI * t * 2.23f) * cosf(2.f * (float)M_PI * t * 11.3f);
                v = grain1 * 0.5f + grain2 * 0.5f;
                v = tanhf(v * (2.f + framePos * 4.f));
                break;
            }
            }
            dst[s] = v;
        }

        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_HollowResonant(int N, int frames) {
    Wavetable wt;
    wt.name = "HollowResonant";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        std::fill(dst, dst + N, 0.0f);

        for (int h = 1; h <= 24; ++h) {
            float amp = 0.1f / (float)h;
            float peak3 = expf(-0.5f * (float)((h - 3) * (h - 3)));
            float peak7 = expf(-0.5f * (float)((h - 7) * (h - 7)));
            float peak11 = expf(-0.5f * (float)((h - 11) * (h - 11)));
            float peaks = (peak3 + peak7 + peak11) * 0.5f;

            float shift = framePos * 2.f;
            float peak3s = expf(-0.5f * (float)((h - (3 + shift)) * (h - (3 + shift))));
            float peak7s = expf(-0.5f * (float)((h - (7 + shift)) * (h - (7 + shift))));
            float peak11s = expf(-0.5f * (float)((h - (11 + shift)) * (h - (11 + shift))));
            float peaksShifted = (peak3s + peak7s + peak11s) * 0.5f;

            amp += peaks * (1.f - framePos) + peaksShifted * framePos;

            float ph = framePos * 0.5f;
            for (int s = 0; s < N; ++s) {
                float t = (float)s / (float)N;
                dst[s] += amp * sinf(2.f * (float)M_PI * (float)h * t + ph);
            }
        }

        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_AbyssalAlloy(int N, int frames) {
    Wavetable wt;
    wt.name = "AbyssalAlloy";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    uint32_t rng = 0xA11E011Au;
    auto rand01 = [&]() -> float {
        rng = hash32_local(rng);
        return (float)(rng & 0xFFFFu) / 65535.f;
    };
    auto randBip = [&]() -> float {
        return rand01() * 2.f - 1.f;
    };

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        int mode = f % 8;
        rng ^= hash32_local((uint32_t)f * 0x9E3779B9u + 0xBEEF1234u);

        float chaosA = rand01();
        float chaosB = rand01();
        float chaosC = rand01();

        float subAmt = 0.55f + 0.35f * (1.f - std::fabs(framePos - 0.5f) * 2.f);
        float fundAmt = 0.85f;
        float octAmt = 0.25f + 0.15f * chaosA;

        float inharm1 = 1.41f + chaosB * 5.7f;
        float inharm2 = 2.09f + chaosC * 9.3f;
        float fmRatio = 2.0f + (float)(mode % 5) * 0.73f + chaosA * 2.0f;
        float fmIndex = 2.0f + chaosB * 12.0f + framePos * 6.0f;
        float ringRatio = 3.0f + (float)(mode % 3) * 2.0f + chaosC * 6.0f;

        float noiseLP = 0.f;
        float noiseHP = 0.f;
        float prevNoiseLP = 0.f;
        float noiseCoeff = 0.004f + (0.08f + chaosB * 0.25f) * (0.25f + framePos);
        noiseCoeff = clamp_local(noiseCoeff, 0.004f, 0.35f);
        float noiseAmt = 0.04f + 0.22f * framePos;
        if (mode == 6 || mode == 7) noiseAmt += 0.10f;

        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            float t = (float)s / (float)N;
            float fund = sinf(2.f * (float)M_PI * t);
            float oct = sinf(2.f * (float)M_PI * 2.f * t);
            float subHalf = sinf((float)M_PI * t);
            float base = fund * fundAmt + oct * octAmt + subHalf * subAmt;

            float mA = sinf(2.f * (float)M_PI * t * inharm1);
            float mB = sinf(2.f * (float)M_PI * t * inharm2 + mA * (1.0f + chaosA * 2.0f));

            float fm = sinf(2.f * (float)M_PI * t + sinf(2.f * (float)M_PI * t * fmRatio) * fmIndex);
            float ring = sinf(2.f * (float)M_PI * t * ringRatio);

            float metal = 0.f;
            switch (mode) {
            default:
            case 0: metal = 0.55f * mA + 0.45f * mB; break;
            case 1: metal = fm; break;
            case 2: metal = fm * ring; break;
            case 3: metal = sinf(2.f * (float)M_PI * fmodf(t * (2.0f + chaosC * 10.0f), 1.f) * (3.0f + chaosA * 6.0f)); break;
            case 4: metal = sinf((mA + fm * 0.7f) * (3.5f + chaosB * 10.f)); break;
            case 5: metal = tanhf((mB * 1.7f + fm * 0.9f) * (2.0f + chaosC * 4.f)); break;
            case 6: metal = (mA * 0.4f + fm * 0.6f) * (0.6f + 0.4f * ring); break;
            case 7: metal = sinf((fm + mB) * (5.0f + chaosA * 14.f)) * (0.7f + 0.3f * ring); break;
            }

            float n = randBip();
            noiseLP += noiseCoeff * (n - noiseLP);
            noiseHP = noiseLP - prevNoiseLP;
            prevNoiseLP = noiseLP;

            float x = base + metal * (0.55f + framePos * 0.55f) + noiseHP * noiseAmt;
            x = x - (x * 0.02f);

            float drive = 1.2f + framePos * 2.2f + chaosB * 1.5f;
            x = tanhf(x * drive);
            if (mode >= 4) x = sinf(x * (2.0f + chaosC * 8.0f));

            dst[s] = x;
        }

        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }

    return wt;
}

static Wavetable makeXenoTable_Substrate(int N, int frames) {
    Wavetable wt;
    wt.name = "Substrate";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        std::fill(dst, dst + N, 0.0f);
        // Heavier low-end and higher perceived loudness:
        // - emphasize low harmonics, roll off highs harder
        // - apply mild saturation before normalization to increase RMS
        for (int h = 1; h <= 24; ++h) {
            float amp;
            if (h == 1)      amp = 1.15f;
            else if (h == 2) amp = 0.95f;
            else if (h == 3) amp = 0.55f;
            else if (h == 4) amp = 0.30f;
            else if (h <= 8) amp = 0.18f / (float)h;
            else             amp = 0.04f / ((float)h * (0.65f + 0.10f * (float)h));

            // Keep frames related but not identical.
            float frameMod = 1.f + 0.12f * sinf(framePos * (float)M_PI + (float)h * 0.9f);
            float ph = framePos * 0.45f * (float)h;
            for (int s = 0; s < N; ++s) {
                float t = (float)s / (float)N;
                dst[s] += (amp * frameMod) * sinf(2.f * (float)M_PI * (float)h * t + ph);
            }
        }

        float drive = 1.20f + 0.55f * powf(framePos, 0.85f);
        for (int s = 0; s < N; ++s) {
            dst[s] = tanhf(dst[s] * drive);
        }
        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_SubmergedMonolith(int N, int frames) {
    Wavetable wt;
    wt.name = "SubmergedMonolith";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    uint32_t rng = 0xC1AE3471u;
    auto xorshift01 = [&]() -> float {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng & 0xFFFFu) / 65535.f;
    };
    auto smooth01 = [&](float x) -> float {
        x = clamp_local(x, 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    };

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;

        float baseFund = 1.0f;
        float baseAmp = 0.98f;
        float subWeight = 0.10f + 0.18f * smooth01(framePos * 1.2f);
        float lowMidWeight = 0.05f + 0.35f * smooth01(framePos);

        const float r1 = 1.37f;
        const float r2 = 1.618f;
        const float r3 = 2.71f;

        float clusterCenter = 2.8f + framePos * 6.0f + sinf(framePos * 2.f * (float)M_PI) * 0.35f;
        float clusterWidth = 1.2f + framePos * 1.8f;

        float warpAmt = 0.00f + 0.18f * powf(framePos, 1.5f);
        int warpK1 = 2 + (f % 5);
        int warpK2 = 5 + (f % 7);
        float asymAmt = 0.00f + 0.55f * powf(framePos, 1.2f);

        float late = smooth01((framePos - 0.62f) / 0.38f);
        float foldAmt = late * 0.40f;
        float fmAmt = late * 0.55f;

        float phA = (xorshift01() * 2.f - 1.f) * (float)M_PI;
        float phB = (xorshift01() * 2.f - 1.f) * (float)M_PI;
        float phC = (xorshift01() * 2.f - 1.f) * (float)M_PI;

        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            float t = (float)s / (float)N;

            float warp = t;
            warp += warpAmt * sinf(2.f * (float)M_PI * (float)warpK1 * t + phA);
            warp += warpAmt * 0.35f * sinf(2.f * (float)M_PI * (float)warpK2 * t + phB);
            warp = warp - floorf(warp);

            float carrier = sinf(2.f * (float)M_PI * baseFund * warp);
            float x = carrier * baseAmp;

            x += sinf(2.f * (float)M_PI * 2.f * warp) * (subWeight * 0.6f);
            x += sinf(2.f * (float)M_PI * 3.f * warp) * (subWeight * 0.25f);

            float inharmAmt = powf(framePos, 1.25f) * 0.65f;
            float inharm = 0.f;
            inharm += sinf(2.f * (float)M_PI * (r1 * t) + phA) * 0.55f;
            inharm += sinf(2.f * (float)M_PI * (r2 * t) + phB) * 0.45f;
            inharm += sinf(2.f * (float)M_PI * (r3 * t) + phC) * 0.35f;
            x += inharm * inharmAmt;

            float cloud = 0.f;
            for (int h = 2; h <= 20; ++h) {
                float hh = (float)h;
                float d = (hh - clusterCenter) / std::max(0.6f, clusterWidth);
                float env = expf(-0.5f * d * d);
                float ph = (float)h * 0.37f + phA * 0.3f + phB * 0.2f;
                cloud += env * sinf(2.f * (float)M_PI * hh * warp + ph);
            }
            x += cloud * lowMidWeight * 0.10f;

            x += asymAmt * 0.22f * x * x * x;

            if (late > 0.001f) {
                float mod = sinf(2.f * (float)M_PI * (r2 * t) + phC) * (2.0f + 9.0f * fmAmt);
                float fm = sinf(2.f * (float)M_PI * warp + mod);
                x = x * (1.f - fmAmt * 0.25f) + fm * (fmAmt * 0.25f);
                float folded = sinf(x * (1.2f + foldAmt * 5.0f));
                x = x * (1.f - foldAmt) + folded * foldAmt;
            }

            x = tanhf(x * (1.1f + framePos * 1.8f));
            dst[s] = x;
        }

        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_HarshNoise(int N, int frames) {
    Wavetable wt;
    wt.name = "HarshNoise";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    uint32_t rng = 0xBADF00Du;
    auto xorshift = [&]() -> float {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng & 0xFFFFu) / 32768.f - 1.f;
    };

    for (int f = 0; f < frames; ++f) {
        int mode = f % 8;
        rng ^= (uint32_t)(f * 77777 + 12345);
        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            float t = (float)s / (float)N;
            float v = 0.f;
            switch (mode) {
            default:
            case 0: {
                v = sinf(2.f * (float)M_PI * t * (1.f + (float)f * 0.3f));
                v = (v > 0.f) ? 1.f : -1.f;
                v *= (1.f - t * 0.5f);
                break;
            }
            case 1: {
                float sine = sinf(2.f * (float)M_PI * t * 3.f);
                int bits = 2 + (f / 8);
                float quant = (float)(1 << bits);
                v = floorf(sine * quant) / quant;
                break;
            }
            case 2: {
                v = sinf(2.f * (float)M_PI * t) * sinf(2.f * (float)M_PI * t * 7.33f);
                v = tanhf(v * 4.f);
                break;
            }
            case 3: {
                v = xorshift();
                float nfreq = 2.f + (float)f * 0.5f;
                v *= sinf(2.f * (float)M_PI * t * nfreq);
                break;
            }
            case 4: {
                float mod = sinf(2.f * (float)M_PI * t * (5.f + (float)f * 0.2f));
                v = sinf(2.f * (float)M_PI * t + mod * (3.f + (float)f * 0.1f));
                v = tanhf(v * 3.f);
                break;
            }
            case 5: {
                float sine = sinf(2.f * (float)M_PI * t * 2.f);
                float fold = sine * (2.f + (float)f * 0.15f);
                v = sinf(fold);
                break;
            }
            case 6: {
                v = fabsf(sinf(2.f * (float)M_PI * t * 3.f)) * 2.f - 1.f;
                v += fabsf(sinf(2.f * (float)M_PI * t * 5.17f)) * 0.5f;
                v = tanhf(v * 2.f);
                break;
            }
            case 7: {
                float sub = sinf(2.f * (float)M_PI * t);
                float noise = xorshift() * (1.f - t);
                v = sub * 0.5f + noise * 0.8f;
                break;
            }
            }
            dst[s] = v;
        }
        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_ArcadeFX(int N, int frames) {
    Wavetable wt;
    wt.name = "ArcadeFX";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    // Lower all internal pattern rates by ~3 octaves so the table reads less "ultrasonic".
    constexpr float kRate = 0.125f;

    for (int f = 0; f < frames; ++f) {
        int mode = f % 10;
        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            float t = (float)s / (float)N;
            float v = 0.f;
            switch (mode) {
            default:
            case 0: {
                float sweepFreq = (20.f * kRate) * (1.f - t * 0.9f);
                v = sinf(2.f * (float)M_PI * t * sweepFreq);
                v = (v > 0.f) ? 1.f : -1.f;
                break;
            }
            case 1: {
                float sweepFreq = (2.f + t * 30.f) * kRate;
                v = sinf(2.f * (float)M_PI * t * sweepFreq);
                break;
            }
            case 2: {
                int pattern = (int)(t * (256.f * kRate));
                v = ((pattern ^ (pattern >> 3) ^ (pattern >> 5)) & 1) ? 1.f : -1.f;
                break;
            }
            case 3: {
                float chirp = expf(t * 4.f) * (0.5f * kRate);
                v = sinf(2.f * (float)M_PI * chirp);
                v = tanhf(v * 3.f);
                break;
            }
            case 4: {
                v = sinf(2.f * (float)M_PI * t * (8.f * kRate)) + sinf(2.f * (float)M_PI * t * (11.3f * kRate));
                v = tanhf(v * 2.f);
                break;
            }
            case 5: {
                float duty = 0.1f + t * 0.8f;
                float phase = fmodf(t * (6.f * kRate), 1.f);
                v = (phase < duty) ? 1.f : -1.f;
                break;
            }
            case 6: {
                int bt = (int)(t * (8000.f * kRate)) + (int)((float)f * (200.f * kRate));
                int raw = ((bt >> 4) | (bt << 1)) ^ (bt >> 8);
                v = (float)(raw & 0xFF) / 128.f - 1.f;
                break;
            }
            case 7: {
                v = sinf(2.f * (float)M_PI * t * (3.f * kRate)) * cosf(2.f * (float)M_PI * t * (13.7f * kRate));
                v += sinf(2.f * (float)M_PI * t * (0.5f * kRate)) * 0.3f;
                break;
            }
            case 8: {
                float srDiv = 4.f + (float)f * 0.5f;
                float quantT = floorf(t * (float)N / srDiv) * srDiv / (float)N;
                v = sinf(2.f * (float)M_PI * quantT * (5.f * kRate));
                break;
            }
            case 9: {
                float modDepth = 8.f * (1.f - t);
                float mod = sinf(2.f * (float)M_PI * t * (12.f * kRate)) * modDepth;
                v = sinf(2.f * (float)M_PI * t * (2.f * kRate) + mod);
                break;
            }
            }
            dst[s] = v;
        }
        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }
    return wt;
}

static Wavetable makeXenoTable_AlienPhysicsCollapse(int N, int frames) {
    Wavetable wt;
    wt.name = "AlienPhysics";
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

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
        return x - floorf(x);
    };
    auto lens = [&](float x, float g) -> float {
        float a = atanf(g);
        if (a < 1e-6f) return x;
        return atanf(x * g) / a;
    };

    for (int f = 0; f < frames; ++f) {
        float framePos = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;

        // Map 16 frames -> 8 modes with 2 frames each.
        int mode = clamp_local(f / 2, 0, 7);
        float z = (f % 2) ? 1.f : 0.f;

        rng ^= (uint32_t)(0x9E3779B9u * (uint32_t)(f + 1) + 0x7F4A7C15u);

        float x = 0.15f + 0.7f * xorshift01();
        float y = 0.10f + 0.8f * xorshift01();
        float phi = xorshift01();
        float fb = 0.f;
        float prev = 0.f;

        float r = 3.57f + 0.40f * z + 0.02f * sinf(framePos * 2.f * (float)M_PI);
        float r2 = 3.62f + 0.36f * (1.f - z);
        float K = 0.10f + 1.75f * (0.25f + 0.75f * z);
        float warpAmt = 0.00f + 0.22f * powf(framePos, 1.2f);
        float foldAmt = 0.00f + 0.28f * powf(framePos, 1.6f);
        float asymAmt = 0.08f + 0.40f * framePos;

        float inj = 0.06f + 0.22f * z;
        float damp = 0.985f - 0.02f * z;

        float phA = randBip() * (float)M_PI;
        float phB = randBip() * (float)M_PI;
        float phC = randBip() * (float)M_PI;

        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        for (int s = 0; s < N; ++s) {
            x = r * x * (1.f - x);
            y = r2 * y * (1.f - y);
            float cx = 2.f * x - 1.f;
            float cy = 2.f * y - 1.f;

            fb = fb * damp + cx * inj;
            float phiStep = 1.f / (float)N;
            phi = fract01(phi + phiStep + fb * 0.015f);

            float warp = phi;
            warp += warpAmt * 0.08f * sinf(2.f * (float)M_PI * (2.f + (float)(mode % 5)) * warp + phA);
            warp += warpAmt * 0.03f * sinf(2.f * (float)M_PI * (7.f + (float)(mode % 7)) * warp + phB);
            warp = fract01(warp);

            float u = 2.f * warp - 1.f;
            float tri = 1.f - 2.f * fabsf(u);
            float para = u - (u * u * u) * 0.3333333f;

            float v = 0.f;
            switch (mode) {
            default:
            case 0: {
                float a = lens(tri + cx * 0.35f, 1.4f + 4.0f * z);
                v = a + asymAmt * 0.18f * a * a * a;
                break;
            }
            case 1: {
                float theta = warp;
                float omega = 0.15f + 0.35f * z;
                theta = fract01(theta + omega + (K * 0.07f) * sinf(2.f * (float)M_PI * theta + phC));
                float uu = 2.f * theta - 1.f;
                v = lens(uu, 0.9f + 5.0f * z);
                break;
            }
            case 2: {
                float tx = x;
                tx = (tx < 0.5f) ? (2.f * tx) : (2.f * (1.f - tx));
                float w = 2.f * tx - 1.f;
                if (cy > 0.995f) w = (w > 0.f) ? 1.f : -1.f;
                v = lens(w + para * 0.35f, 1.2f + 3.5f * z);
                break;
            }
            case 3: {
                float ww = warp;
                ww += 0.09f * warpAmt * sinf(2.f * (float)M_PI * (3.f + 7.f * x) * ww + phA);
                ww += 0.05f * warpAmt * sinf(2.f * (float)M_PI * (5.f + 11.f * y) * ww + phB);
                ww = fract01(ww);
                float uu = 2.f * ww - 1.f;
                float p = uu - (uu * uu * uu) * 0.28f;
                v = lens(p, 1.0f + 5.5f * z);
                break;
            }
            case 4: {
                float g = 0.8f + 6.0f * z;
                float well = lens(u + fb * 0.9f, g);
                v = well;
                if (x > 0.995f) v = -v;
                break;
            }
            case 5: {
                float prod = cx * cy;
                float shaped = lens(prod + tri * 0.25f, 1.6f + 4.0f * z);
                v = shaped + 0.15f * lens(para + cx * 0.15f, 2.2f + 3.0f * z);
                break;
            }
            case 6: {
                float d = (tri - prev);
                prev = tri;
                float edge = lens(d * (6.0f + 10.0f * z) + u * 0.35f, 1.4f + 5.0f * z);
                v = edge;
                if (framePos > 0.5f && fabsf(edge) > 0.85f) v = (edge > 0.f) ? 1.f : -1.f;
                break;
            }
            case 7: {
                float collapse = lens(u + cx * 0.55f + cy * 0.25f, 2.0f + 6.0f * z);
                collapse += 0.22f * lens(sinf(2.f * (float)M_PI * (warp + cx * 0.05f) * (3.f + 9.f * z) + phA), 1.0f + 3.0f * z);
                v = collapse;
                if (x < 0.02f || x > 0.98f) v = -v;
                break;
            }
            }

            v += asymAmt * 0.10f * v * v * v;
            if (foldAmt > 0.0001f) {
                float folded = sinf(v * (1.4f + foldAmt * 7.0f));
                v = v * (1.f - foldAmt) + folded * foldAmt;
            }

            v = tanhf(v * (1.2f + framePos * 2.3f));
            dst[s] = v;
        }

        smoothEndpoints(dst, N, 96);
        removeDcAndNormalize(dst, N);
    }

    return wt;
}

// Smooth multi-peak spectral envelope (Gaussian peaks in normalized freq 0..1)
static inline float formantEnv(float fNorm,
                               float c1, float c2, float c3,
                               float bw1, float bw2, float bw3,
                               float g1, float g2, float g3,
                               float tilt) {
    auto gauss = [](float x, float bw) {
        float b = std::max(1e-4f, bw);
        float z = x / b;
        return expf(-0.5f * z * z);
    };
    float e = 0.05f;
    e += g1 * gauss(fNorm - c1, bw1);
    e += g2 * gauss(fNorm - c2, bw2);
    e += g3 * gauss(fNorm - c3, bw3);

    // Tilt: -1 dark, +1 bright
    float tn = clamp01_local(fNorm);
    float tiltCurve = (tilt >= 0.0f)
        ? (1.0f + tilt * 0.65f * powf(tn, 1.15f))
        : (1.0f + tilt * 0.65f * powf(1.0f - tn, 1.15f));
    if (tiltCurve < 0.10f) tiltCurve = 0.10f;
    return e * tiltCurve;
}

// Build a time-domain frame from a magnitude spectrum (RealFFT layout) with deterministic random phase.
static void spectralToFrame(rack::dsp::RealFFT& fft, int N, const std::vector<float>& magBins,
                            uint32_t seed, float* dstOut) {
    std::vector<float> spec((size_t)N);
    std::vector<float> out((size_t)N);
    const int nyquistBin = N / 2;

    spec[0] = 0.0f; // DC
    spec[1] = 0.0f; // Nyquist (real)
    for (int k = 1; k < nyquistBin; ++k) {
        float mag = (k < (int)magBins.size()) ? magBins[(size_t)k] : 0.0f;
        if (mag <= 0.0f) {
            spec[2 * k + 0] = 0.0f;
            spec[2 * k + 1] = 0.0f;
            continue;
        }
        uint32_t h = hash32_local(seed ^ (uint32_t)(0x9E3779B9u * (uint32_t)(k + 1)));
        float ph = u01_local(h) * 6.283185307f;
        spec[2 * k + 0] = mag * cosf(ph);
        spec[2 * k + 1] = mag * sinf(ph);
    }

    fft.irfft(spec.data(), out.data());
    fft.scale(out.data());

    // Normalize
    float m = 0.0f;
    for (int i = 0; i < N; ++i) m = std::max(m, std::fabs(out[(size_t)i]));
    float g = (m > 1e-6f) ? (0.95f / m) : 1.0f;
    for (int i = 0; i < N; ++i) dstOut[i] = out[(size_t)i] * g;
}

static Wavetable makeSpectralMorphTable(const std::string& name, int N, int frames,
                                        void (*fill)(int bin, int nyquistBin, float frameNorm, float& harmMag, float& noiseMag, float& tilt)) {
    Wavetable wt;
    wt.name = name;
    wt.frameSize = N;
    wt.frameCount = frames;
    wt.data.resize((size_t)N * (size_t)frames);

    rack::dsp::RealFFT fft(N);
    const int nyquistBin = N / 2;
    std::vector<float> mag((size_t)nyquistBin);

    for (int f = 0; f < frames; ++f) {
        float fn = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        uint32_t seed = 0xBADC0FFEu ^ (uint32_t)(f * 1013);

        // Build magnitude bins
        mag.assign((size_t)nyquistBin, 0.0f);
        for (int k = 1; k < nyquistBin; ++k) {
            float harmM = 0.0f, noiseM = 0.0f, tilt = 0.0f;
            fill(k, nyquistBin, fn, harmM, noiseM, tilt);

            // Combine harmonic + noise magnitudes
            float m = std::max(0.0f, harmM + noiseM);

            // Mild de-emphasis near Nyquist to avoid brittle content
            float x = (float)k / (float)nyquistBin;
            float hf = clamp01_local((x - 0.78f) / 0.22f);
            m *= (1.0f - 0.85f * hf);

            mag[(size_t)k] = m;
        }

        float* dst = wt.data.data() + (size_t)f * (size_t)N;
        spectralToFrame(fft, N, mag, seed, dst);
    }
    return wt;
}

static void buildMipmaps(Wavetable& wt) {
#ifdef METAMODULE
    // MetaModule: avoid runtime FFT-based mipmap generation.
    // It is expensive at module-load time and can cause long stalls.
    (void)wt;
    return;
#endif
    // Use 8 levels down to ~8 harmonics for N=2048.
    const int N = wt.frameSize;
    if (wt.frameCount <= 0 || N <= 0) return;
    if ((N % 32) != 0) return; // RealFFT requirement

    constexpr int kMipCount = 8;
    constexpr int kFadeBins = 10;
    const int nyquistBin = N / 2;

    rack::dsp::RealFFT fft(N);
    std::vector<float> in(N);
    std::vector<float> spec(N);
    std::vector<float> out(N);

    wt.mipData.clear();
    wt.mipData.resize(std::max(0, kMipCount - 1));
    for (int l = 1; l < kMipCount; ++l) {
        wt.mipData[l - 1].resize((size_t)wt.frameCount * (size_t)N);
    }

    // Precompute max abs per source frame to normalize mips
    std::vector<float> srcMax((size_t)wt.frameCount, 1.0f);
    for (int f = 0; f < wt.frameCount; ++f) {
        float m = 0.0f;
        const float* src = wt.data.data() + (size_t)f * (size_t)N;
        for (int i = 0; i < N; ++i) m = std::max(m, std::fabs(src[i]));
        srcMax[(size_t)f] = (m > 1e-6f) ? m : 1.0f;
    }

    for (int l = 1; l < kMipCount; ++l) {
        int cutoffBin = nyquistBin >> l;
        if (cutoffBin < 8) cutoffBin = 8;
        if (cutoffBin > nyquistBin) cutoffBin = nyquistBin;

        for (int f = 0; f < wt.frameCount; ++f) {
            const float* src = wt.data.data() + (size_t)f * (size_t)N;
            std::copy(src, src + N, in.begin());

            fft.rfft(in.data(), spec.data());

            // Cosine rolloff near cutoff to reduce ringing
            int fadeStart = std::max(1, cutoffBin - kFadeBins);
            for (int k = fadeStart; k <= cutoffBin && k < nyquistBin; ++k) {
                float t = (float)(k - fadeStart) / (float)std::max(1, cutoffBin - fadeStart);
                // 1 at fadeStart, 0 at cutoff
                float s = 0.5f * (1.0f + std::cos(t * 3.14159265358979323846f));
                spec[2 * k + 0] *= s;
                spec[2 * k + 1] *= s;
            }

            // Hard zero above cutoff
            for (int k = cutoffBin + 1; k < nyquistBin; ++k) {
                spec[2 * k + 0] = 0.0f;
                spec[2 * k + 1] = 0.0f;
            }
            // Nyquist bin
            if (cutoffBin < nyquistBin) spec[1] = 0.0f;

            fft.irfft(spec.data(), out.data());
            fft.scale(out.data());

            // Normalize to match source frame amplitude
            float m = 0.0f;
            for (int i = 0; i < N; ++i) m = std::max(m, std::fabs(out[i]));
            float g = (m > 1e-6f) ? (srcMax[(size_t)f] / m) : 1.0f;
            if (g > 4.0f) g = 4.0f;
            if (g < 0.25f) g = 0.25f;

            float* dst = wt.mipData[l - 1].data() + (size_t)f * (size_t)N;
            for (int i = 0; i < N; ++i) dst[i] = out[i] * g;
        }
    }
}

// ─── Helper: generate a single-frame table from a lambda ────────────
static Wavetable makeTable1(const std::string& name, int size,
                            float (*gen)(float phase01)) {
    Wavetable wt;
    wt.name = name;
    wt.frameSize = size;
    wt.frameCount = 1;
    wt.data.resize(size);
    for (int i = 0; i < size; ++i) {
        float p = (float)i / (float)size;
        wt.data[i] = gen(p);
    }
    return wt;
}

// ─── Helper: generate a multi-frame morph table ─────────────────────
static Wavetable makeMorphTable(const std::string& name, int size, int frames,
                                float (*gen)(float phase01, float frameNorm)) {
    Wavetable wt;
    wt.name = name;
    wt.frameSize = size;
    wt.frameCount = frames;
    wt.data.resize(size * frames);
    for (int f = 0; f < frames; ++f) {
        float fn = (frames > 1) ? (float)f / (float)(frames - 1) : 0.0f;
        for (int i = 0; i < size; ++i) {
            float p = (float)i / (float)size;
            wt.data[f * size + i] = gen(p, fn);
        }
    }
    return wt;
}

// ═══════════════════════════════════════════════════════════════════════
// Built-in wavetable generation
// ═══════════════════════════════════════════════════════════════════════
void WavetableBank::generateBuiltins() {
    tables.clear();
    builtinCount = 0;

    constexpr int N = 2048;
    constexpr int MF = 16; // morph frames for multi-frame tables

    // Curated built-in set (compact indices 0..10):
    // 0 Saw, 1 Formant, 2 Harsh, 3 FMStack, 4 ImpactPulse,
    // 5 FeralMachine, 6 AbyssalAlloy, 7 Substrate, 8 SubmergedMonolith,
    // 9 HarshNoise, 10 ArcadeFX

    // ── 0: Saw (multi-frame so TIMBRE sweep is meaningful) ─────────
    addTable(makeMorphTable("Saw", N, MF, [](float p, float fn) -> float {
        // Additive saw with a frame-dependent harmonic tilt/rolloff.
        // Low frames: bright classic saw; high frames: darker/thicker saw.
        float w = p * 2.0f * (float)M_PI;
        float bright = 1.0f - 0.85f * clamp01_local(fn);
        int maxH = 48;
        float s = 0.0f;
        for (int h = 1; h <= maxH; ++h) {
            float sign = (h % 2 == 0) ? 1.0f : -1.0f;
            float hf = (float)h;

            // Rolloff increases with frame (darker) but keep low harmonics strong.
            float roll = 1.0f / (1.0f + (1.0f - bright) * (hf - 1.0f) * 0.22f);
            float a = (sign / hf) * roll;

            // Slight extra damping of the very high end at darker frames.
            if (h > 20) {
                float hi = (hf - 20.0f) / 28.0f;
                a *= 1.0f - (1.0f - bright) * 0.65f * clamp01_local(hi);
            }

            s += a * sinf(w * hf);
        }
        s *= (2.0f / (float)M_PI);

        // Subtle saturation grows with frame to keep perceived level.
        float drive = 1.05f + 0.85f * powf(clamp01_local(fn), 1.35f);
        return tanhf(s * drive);
    }));
    buildMipmaps(tables.back());

    // ── 1: Formant morph (16 frames) ───────────────────────────────
    addTable(makeMorphTable("Formant", N, MF, [](float p, float fn) -> float {
        float f1 = 1.0f + fn * 4.0f;    // 1..5
        float f2 = 2.0f + fn * 6.0f;    // 2..8
        float f3 = 3.0f + fn * 10.0f;   // 3..13
        float s = sinf(p * 2.0f * (float)M_PI * f1) * 0.5f
                + sinf(p * 2.0f * (float)M_PI * f2) * 0.3f
                + sinf(p * 2.0f * (float)M_PI * f3) * 0.2f;
        return s;
    }));
    buildMipmaps(tables.back());

    // ── 2: Harsh digital (bit-crush-ish, stronger frame divergence) ─
    addTable(makeMorphTable("Harsh", N, MF, [](float p, float fn) -> float {
        float x = clamp01_local(fn);

        // Phase quantization increases with frame (adds obvious stair-stepping).
        float pSteps = 1024.0f - x * 1008.0f; // 1024 .. 16
        if (pSteps < 16.0f) pSteps = 16.0f;
        float pQ = floorf(p * pSteps) / pSteps;

        // Base waveform: saw.
        float saw = pQ * 2.0f - 1.0f;

        // Frame-dependent wavefold before quantization.
        float fold = sinf(saw * (1.0f + x * 7.0f) * (float)M_PI * 0.5f);

        // Value quantization: fewer steps at higher frames.
        float steps = 192.0f - x * 188.0f; // 192 .. 4
        if (steps < 2.0f) steps = 2.0f;
        float q = floorf(fold * steps + 0.5f) / steps;

        // Add a small frame-dependent edge wobble so frames remain distinct in context.
        float wob = (0.04f + 0.10f * x) * sinf(2.0f * (float)M_PI * p * (1.0f + 11.0f * x));
        q = q + wob;

        // Keep bounded.
        return tanhf(q * (1.2f + 1.6f * x));
    }));
    buildMipmaps(tables.back());

    // ── 3: FMStack (PM-optimized low-partial stack, 16 frames) ─────
    addTable(makeMorphTable("FMStack", N, MF, [](float p, float fn) -> float {
        struct BP { float x, n; };
        const BP bp[5] = {
            { 0.00f, 1.0f },
            { 0.25f, 2.0f },
            { 0.50f, 3.0f },
            { 0.75f, 5.0f },
            { 1.00f, 8.0f },
        };
        float x = clamp01_local(fn);
        float n = 1.0f;
        for (int i = 0; i < 4; ++i) {
            if (x >= bp[i].x && x <= bp[i + 1].x) {
                float t = (x - bp[i].x) / std::max(1e-6f, (bp[i + 1].x - bp[i].x));
                n = lerp_local(bp[i].n, bp[i + 1].n, t);
                break;
            }
        }

        int maxH = (int)std::floor(n + 0.5f);
        if (maxH < 1) maxH = 1;
        if (maxH > 12) maxH = 12;

        float w = p * 2.0f * (float)M_PI;
        float s = 0.0f;
        for (int h = 1; h <= maxH; ++h) {
            float a = 1.0f / (1.0f + 0.85f * (float)(h - 1));
            if (h == 1) a *= 1.00f;
            else if (h == 2) a *= 0.75f;
            else a *= 0.55f;
            s += a * sinf(w * (float)h);
        }
        return tanhf(s * 0.90f);
    }));
    buildMipmaps(tables.back());

    // ── 4: ImpactPulse (transient-focused asymmetric pulse, 16 frames) ─
    addTable(makeMorphTable("ImpactPulse", N, MF, [](float p, float fn) -> float {
        float x = clamp01_local(fn);
        float t = p;
        if (t > 0.5f) t -= 1.0f;

        float w = 0.18f - 0.14f * x; // 0.18 .. 0.04
        if (w < 0.02f) w = 0.02f;

        float shift = (x - 0.5f) * 0.35f * w;
        t -= shift;

        float z = t / w;
        float g = expf(-0.5f * z * z);
        float y = (1.0f - z * z) * g;
        y = tanhf(y * (1.4f + 1.8f * x));
        return y;
    }));
    buildMipmaps(tables.back());

    // ── Xenostasis tables (curated subset) ─────────────────────────
    // Note: many are intentionally harsh/chaotic; we still keep mipmaps minimal.
    addTable(makeXenoTable_FeralMachine(N, MF));
    addTable(makeXenoTable_AbyssalAlloy(N, MF));
    addTable(makeXenoTable_Substrate(N, MF));
    buildMipmaps(tables.back());
    addTable(makeXenoTable_SubmergedMonolith(N, MF));
    addTable(makeXenoTable_HarshNoise(N, MF));
    addTable(makeXenoTable_ArcadeFX(N, MF));

    builtinCount = (int)tables.size();
}

// ═══════════════════════════════════════════════════════════════════════
// WAV file loader (minimal, mono, 16/24/32-bit PCM or 32-bit float)
// ═══════════════════════════════════════════════════════════════════════
int WavetableBank::loadFromWav(const std::string& path, int expectedFrameSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;

    // Read RIFF header
    char riff[4]; uint32_t fileSize; char wave[4];
    if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return -1; }
    if (fread(&fileSize, 4, 1, f) != 1) { fclose(f); return -1; }
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return -1; }

    // Find fmt and data chunks
    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRateWav = 0;
    bool fmtFound = false;
    std::vector<uint8_t> rawData;
    uint32_t dataSize = 0;

    while (true) {
        char chunkId[4]; uint32_t chunkSize;
        if (fread(chunkId, 1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) { fclose(f); return -1; }
            fread(&audioFormat, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            fread(&sampleRateWav, 4, 1, f);
            uint32_t byteRate; fread(&byteRate, 4, 1, f);
            uint16_t blockAlign; fread(&blockAlign, 2, 1, f);
            fread(&bitsPerSample, 2, 1, f);
            // Skip extra fmt bytes
            if (chunkSize > 16) fseek(f, chunkSize - 16, SEEK_CUR);
            fmtFound = true;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            rawData.resize(dataSize);
            size_t r = fread(rawData.data(), 1, dataSize, f);
            if (r != dataSize) { rawData.resize(r); dataSize = (uint32_t)r; }
            break; // got data, done
        } else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }
    fclose(f);

    if (!fmtFound || rawData.empty() || numChannels < 1) return -1;
    // Only support PCM (1) or IEEE float (3)
    if (audioFormat != 1 && audioFormat != 3) return -1;

    // Convert raw to float samples (mono, take first channel)
    int bytesPerSample = bitsPerSample / 8;
    int bytesPerFrame = bytesPerSample * numChannels;
    int totalSamples = (int)(dataSize / bytesPerFrame);
    if (totalSamples < 1) return -1;

    std::vector<float> samples(totalSamples);
    for (int i = 0; i < totalSamples; ++i) {
        const uint8_t* ptr = rawData.data() + i * bytesPerFrame;
        if (audioFormat == 3 && bitsPerSample == 32) {
            float v; memcpy(&v, ptr, 4);
            samples[i] = v;
        } else if (audioFormat == 1 && bitsPerSample == 16) {
            int16_t v; memcpy(&v, ptr, 2);
            samples[i] = (float)v / 32768.0f;
        } else if (audioFormat == 1 && bitsPerSample == 24) {
            int32_t v = (int32_t)ptr[0] | ((int32_t)ptr[1] << 8) | ((int32_t)ptr[2] << 16);
            if (v & 0x800000) v |= 0xFF000000; // sign extend
            samples[i] = (float)v / 8388608.0f;
        } else if (audioFormat == 1 && bitsPerSample == 32) {
            int32_t v; memcpy(&v, ptr, 4);
            samples[i] = (float)v / 2147483648.0f;
        } else {
            samples[i] = 0.0f;
        }
    }

    // Build wavetable: auto-detect frames from total size
    int frameSize = expectedFrameSize;
    if (frameSize <= 0) frameSize = 2048;
    int frameCount = std::max(1, totalSamples / frameSize);
    int usedSamples = frameCount * frameSize;

    Wavetable wt;
    // Extract name from filename
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (slash == std::string::npos) slash = 0; else slash++;
    if (dot == std::string::npos || dot <= slash) dot = path.size();
    wt.name = path.substr(slash, dot - slash);

    wt.frameSize = frameSize;
    wt.frameCount = frameCount;
    wt.data.assign(samples.begin(), samples.begin() + usedSamples);

    int idx = addTable(std::move(wt));
    if (idx >= 0) {
        buildMipmaps(tables[(size_t)idx]);
    }
    return idx;
}

} // namespace phaseon
