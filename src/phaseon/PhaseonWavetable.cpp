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

    // ── 0: Sine ─────────────────────────────────────────────────────
    addTable(makeTable1("Sine", N, [](float p) -> float {
        return sinf(p * 2.0f * (float)M_PI);
    }));
    buildMipmaps(tables.back());

    // ── 1: Saw (additive, ~32 harmonics for mild anti-alias) ────────
    addTable(makeTable1("Saw", N, [](float p) -> float {
        float s = 0.0f;
        for (int h = 1; h <= 32; ++h) {
            float sign = (h % 2 == 0) ? 1.0f : -1.0f;
            s += sign * sinf(p * 2.0f * (float)M_PI * (float)h) / (float)h;
        }
        return s * (2.0f / (float)M_PI);
    }));
    buildMipmaps(tables.back());

    // ── 2: Square (additive, odds only) ─────────────────────────────
    addTable(makeTable1("Square", N, [](float p) -> float {
        float s = 0.0f;
        for (int h = 1; h <= 31; h += 2) {
            s += sinf(p * 2.0f * (float)M_PI * (float)h) / (float)h;
        }
        return s * (4.0f / (float)M_PI);
    }));
    buildMipmaps(tables.back());

    // ── 3: Triangle ─────────────────────────────────────────────────
    addTable(makeTable1("Triangle", N, [](float p) -> float {
        float s = 0.0f;
        for (int h = 1; h <= 15; h += 2) {
            float sign = ((h / 2) % 2 == 0) ? 1.0f : -1.0f;
            s += sign * sinf(p * 2.0f * (float)M_PI * (float)h) / ((float)h * (float)h);
        }
        return s * (8.0f / ((float)M_PI * (float)M_PI));
    }));
    buildMipmaps(tables.back());

    // ── 4: Formant morph (vowel-ish, 16 frames) ────────────────────
    addTable(makeMorphTable("Formant", N, MF, [](float p, float fn) -> float {
        // Simple formant approximation: 3 sine harmonics with moving ratios
        float f1 = 1.0f + fn * 4.0f;    // formant 1 ratio: 1..5
        float f2 = 2.0f + fn * 6.0f;    // formant 2 ratio: 2..8
        float f3 = 3.0f + fn * 10.0f;   // formant 3 ratio: 3..13
        float s = sinf(p * 2.0f * (float)M_PI * f1) * 0.5f
                + sinf(p * 2.0f * (float)M_PI * f2) * 0.3f
                + sinf(p * 2.0f * (float)M_PI * f3) * 0.2f;
        return s;
    }));
    buildMipmaps(tables.back());

    // ── 5: Harsh digital (bit-crush style, 16 frames) ───────────────
    addTable(makeMorphTable("Harsh", N, MF, [](float p, float fn) -> float {
        // Quantize a saw to fewer and fewer steps as frame increases
        float saw = p * 2.0f - 1.0f; // -1..+1
        float steps = 256.0f - fn * 252.0f; // 256 steps down to 4
        if (steps < 2.0f) steps = 2.0f;
        float q = floorf(saw * steps + 0.5f) / steps;
        return q;
    }));
    buildMipmaps(tables.back());

    // ── 6: SineStack (sine-variant, single frame) ───────────────────
    // Replaces the old Noise table (keeps table index stable).
    addTable(makeTable1("SineStack", N, [](float p) -> float {
        float w = p * 2.0f * (float)M_PI;
        float s = 0.0f;
        s += 1.00f * sinf(w);
        s += 0.20f * sinf(w * 2.0f);
        s += 0.12f * sinf(w * 3.0f);
        s += 0.08f * sinf(w * 5.0f);
        s += 0.05f * sinf(w * 7.0f);
        return s * 0.80f;
    }));
    buildMipmaps(tables.back());

    // ── 7: Sub bass (sine + sub-octave blend, 16 frames) ────────────
    addTable(makeMorphTable("SubBass", N, MF, [](float p, float fn) -> float {
        float fund = sinf(p * 2.0f * (float)M_PI);
        float sub  = sinf(p * 1.0f * (float)M_PI); // octave below
        return fund * (1.0f - fn) + sub * fn;
    }));
    buildMipmaps(tables.back());


#ifdef METAMODULE
    // MetaModule: avoid FFT-based spectral tables at startup.
    // Provide lighter-weight time-domain approximations with the same names
    // (keeps preset portability via name lookup).

    // ── 8: Formant vowels (cheap approximation) ────────────────────
    addTable(makeMorphTable("FormantVowels", N, MF, [](float p, float fn) -> float {
        // Interpolate 5 vowel-ish formant ratios.
        struct V { float f1, f2, f3; };
        const V v[5] = {
            { 1.6f, 3.2f, 6.0f },
            { 2.2f, 4.5f, 7.0f },
            { 2.9f, 5.8f, 8.5f },
            { 1.8f, 3.7f, 6.5f },
            { 1.4f, 2.8f, 5.6f },
        };
        float y = clamp01_local(fn) * 4.0f;
        int i = (int)y;
        float frac = y - (float)i;
        if (i < 0) { i = 0; frac = 0.0f; }
        if (i >= 4) { i = 4; frac = 0.0f; }
        const V& a = v[i];
        const V& b = v[std::min(4, i + 1)];
        float f1 = lerp_local(a.f1, b.f1, frac);
        float f2 = lerp_local(a.f2, b.f2, frac);
        float f3 = lerp_local(a.f3, b.f3, frac);
        float w = p * 2.0f * (float)M_PI;
        float s = 0.55f * sinf(w * f1) + 0.30f * sinf(w * f2) + 0.20f * sinf(w * f3);
        return s;
    }));
    buildMipmaps(tables.back());

    // ── 9: Formant shift (cheap approximation) ─────────────────────
    addTable(makeMorphTable("FormantShift", N, MF, [](float p, float fn) -> float {
        float shift = (fn - 0.5f) * 0.9f; // -0.45..+0.45
        float f1 = 1.8f + shift * 0.6f;
        float f2 = 3.8f + shift * 1.0f;
        float f3 = 7.2f + shift * 1.4f;
        float w = p * 2.0f * (float)M_PI;
        return 0.60f * sinf(w * f1) + 0.28f * sinf(w * f2) + 0.18f * sinf(w * f3);
    }));
    buildMipmaps(tables.back());

    // ── 10: HarmNoise (cheap approximation) ────────────────────────
    addTable(makeMorphTable("HarmNoise", N, MF, [](float p, float fn) -> float {
        // Harmonics blend to deterministic pseudo-noise across frames.
        float nmix = powf(clamp01_local(fn), 1.6f);
        float hmix = 1.0f - nmix;

        float w = p * 2.0f * (float)M_PI;
        float harm = 0.0f;
        for (int h = 1; h <= 16; ++h) {
            float sign = (h % 2 == 0) ? 1.0f : -1.0f;
            harm += sign * sinf(w * (float)h) / (float)h;
        }
        harm *= (2.0f / (float)M_PI);

        // Deterministic hash noise in -1..1, slightly lowpassed via sin()
        uint32_t hp = (uint32_t)(p * 16777216.0f);
        float n = u01_local(hash32_local(hp ^ 0xA53A9E37u)) * 2.0f - 1.0f;
        float noise = sinf(n * 3.14159265358979323846f);

        return harm * hmix + noise * (0.60f * nmix);
    }));
    buildMipmaps(tables.back());
#else
    // ── 8: Formant vowels (A/E/I/O/U morph, 16 frames) ─────────────
    // Wide, musical spectral bumps for "expensive" glue. Vocal sharpness is mainly provided by the FormantShaper.
    addTable(makeSpectralMorphTable("FormantVowels", N, MF,
        [](int bin, int nyq, float fn, float& harmMag, float& noiseMag, float& tilt) {
            float x = (float)bin / (float)nyq; // 0..1
            // 5 vowel targets in normalized freq. (not strict phonetics, tuned for musicality)
            // Interpolate between adjacent vowels across frames.
            struct V { float c1, c2, c3; };
            const V v[5] = {
                { 0.065f, 0.120f, 0.300f }, // A
                { 0.050f, 0.175f, 0.280f }, // E
                { 0.035f, 0.230f, 0.330f }, // I
                { 0.045f, 0.110f, 0.260f }, // O
                { 0.040f, 0.085f, 0.240f }, // U
            };
            float y = clamp01_local(fn) * 4.0f;
            int i = (int)y;
            float frac = y - (float)i;
            if (i < 0) { i = 0; frac = 0.0f; }
            if (i >= 4) { i = 4; frac = 0.0f; }
            const V& a = v[i];
            const V& b = v[std::min(4, i + 1)];
            float c1 = lerp_local(a.c1, b.c1, frac);
            float c2 = lerp_local(a.c2, b.c2, frac);
            float c3 = lerp_local(a.c3, b.c3, frac);

            float env = formantEnv(x, c1, c2, c3,
                                   0.030f, 0.040f, 0.055f,
                                   1.00f, 0.75f, 0.55f,
                                   -0.10f);

            // Harmonic structure: only at integer harmonics (bins divisible by 1 here, but we bias low bins)
            // Use a gentle 1/sqrt(k) falloff.
            float hk = 1.0f / sqrtf((float)bin);
            harmMag = env * hk * 0.85f;
            noiseMag = 0.0f;
            tilt = -0.10f;
        }
    ));
    buildMipmaps(tables.back());

    // ── 9: Formant shift evolution (centers slide across frames, 16 frames) ─────
    addTable(makeSpectralMorphTable("FormantShift", N, MF,
        [](int bin, int nyq, float fn, float& harmMag, float& noiseMag, float& tilt) {
            float x = (float)bin / (float)nyq;
            // Slide the formant bumps upward with frames
            float shift = (fn - 0.5f) * 0.10f; // -0.05..+0.05
            float c1 = 0.050f + shift * 0.6f;
            float c2 = 0.140f + shift * 0.9f;
            float c3 = 0.290f + shift * 1.2f;
            float env = formantEnv(x, c1, c2, c3,
                                   0.035f, 0.045f, 0.060f,
                                   1.00f, 0.80f, 0.60f,
                                   -0.05f);
            float hk = 1.0f / sqrtf((float)bin);
            harmMag = env * hk * 0.90f;
            noiseMag = 0.0f;
            tilt = -0.05f;
        }
    ));
    buildMipmaps(tables.back());

    // ── 10: Harmonic + shaped-noise hybrid (harmonic → noise across frames) ─────
    // Noise is spectrally shaped (formant/tilt envelope), not white.
    addTable(makeSpectralMorphTable("HarmNoise", N, MF,
        [](int bin, int nyq, float fn, float& harmMag, float& noiseMag, float& tilt) {
            float x = (float)bin / (float)nyq;
            float nmix = powf(clamp01_local(fn), 1.6f); // 0..1
            float hmix = 1.0f - nmix;

            // Envelope: broad formant-ish bumps + slight tilt
            float c1 = 0.040f;
            float c2 = 0.130f + 0.020f * fn;
            float c3 = 0.300f + 0.030f * fn;
            float env = formantEnv(x, c1, c2, c3,
                                   0.040f, 0.060f, 0.090f,
                                   0.95f, 0.80f, 0.60f,
                                   (fn - 0.5f) * 0.25f);

            // Harmonic magnitude at all bins with harmonic-ish falloff
            float hk = 1.0f / (0.65f + powf((float)bin, 0.75f));
            float harm = env * hk;

            // Shaped noise magnitude: envelope-weighted, slightly flatter than harmonics
            float noise = env * (0.020f + 0.085f * powf(x, 0.35f));

            harmMag = harm * hmix;
            noiseMag = noise * nmix;
            tilt = (fn - 0.5f) * 0.25f;
        }
    ));
    buildMipmaps(tables.back());
#endif

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
