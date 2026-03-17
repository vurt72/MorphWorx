/*
 * PhaseonPolish.hpp — Output polish chain (DC block, saturation, transient control, HF smoothing)
 *
 * Designed to make the init preset sound finished, wide, punchy, and exciting
 * without becoming sterile. Character > technical purity.
 */
#pragma once
#include <cmath>
#include <algorithm>

namespace phaseon {

struct DcBlocker {
    // 1st-order highpass: y[n] = x[n] - x[n-1] + r*y[n-1]
    float r = 0.995f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    void setCutoff(float cutoffHz, float sampleRate) {
        cutoffHz = std::max(1.0f, cutoffHz);
        float x = -2.0f * 3.14159265358979323846f * cutoffHz / std::max(1.0f, sampleRate);
        r = std::exp(x);
        r = std::max(0.90f, std::min(0.9999f, r));
    }

    float process(float x) {
        float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() { x1 = y1 = 0.0f; }
};

inline float softClipAsym(float x, float drive, float asym) {
    // Mild asymmetric pre-warp + smooth saturation.
    // drive: 0..1
    float g = 1.0f + drive * 6.0f;
    float pre = x * g;
    // asymmetry bends positive and negative slightly differently
    pre += asym * pre * pre * (pre >= 0.0f ? 1.0f : -1.0f);
    // tanh-like rational approximation (stable + cheap)
    float a = std::fabs(pre);
    float y = pre / (1.0f + 0.6f * a + 0.2f * a * a);
    // makeup to keep perceived loudness when drive is low
    float makeup = 1.0f / (1.0f + drive * 0.6f);
    return y * makeup;
}

struct TransientClipper {
    // Very light envelope-controlled soft limiter.
    // Tracks absolute amplitude and applies a gentle gain reduction above threshold.
    float envL = 0.0f;
    float envR = 0.0f;

    float thresh = 0.92f;   // where gain reduction begins
    float strength = 0.35f; // 0..1 (higher = more clamp)

    float aCoeff = 0.0f;
    float rCoeff = 0.0f;

    void setBallistics(float attackMs, float releaseMs, float sampleRate) {
        attackMs = std::max(0.1f, attackMs);
        releaseMs = std::max(1.0f, releaseMs);
        float a = std::exp(-1.0f / (0.001f * attackMs * std::max(1.0f, sampleRate)));
        float r = std::exp(-1.0f / (0.001f * releaseMs * std::max(1.0f, sampleRate)));
        aCoeff = a;
        rCoeff = r;
    }

    inline float follow(float x, float& env) {
        float ax = std::fabs(x);
        if (ax > env)
            env = aCoeff * env + (1.0f - aCoeff) * ax;
        else
            env = rCoeff * env + (1.0f - rCoeff) * ax;
        return env;
    }

    inline float gainFromEnv(float env) const {
        if (env <= thresh) return 1.0f;
        float over = (env - thresh) / std::max(1e-6f, (1.0f - thresh));
        over = std::max(0.0f, std::min(1.0f, over));
        // soft reduction
        float g = 1.0f - strength * over * over;
        return std::max(0.6f, g);
    }

    void process(float& l, float& r) {
        float eL = follow(l, envL);
        float eR = follow(r, envR);
        float g = std::min(gainFromEnv(eL), gainFromEnv(eR));
        l *= g;
        r *= g;
    }

    void reset() { envL = envR = 0.0f; }
};

struct OnePoleLP {
    float zL = 0.0f;
    float zR = 0.0f;
    float a = 1.0f; // smoothing coefficient

    void setCutoff(float cutoffHz, float sampleRate) {
        cutoffHz = std::max(20.0f, cutoffHz);
        float x = -2.0f * 3.14159265358979323846f * cutoffHz / std::max(1.0f, sampleRate);
        // a = exp(-2pi fc / sr)
        a = std::exp(x);
        a = std::max(0.0f, std::min(0.9999f, a));
    }

    void process(float& l, float& r) {
        zL = a * zL + (1.0f - a) * l;
        zR = a * zR + (1.0f - a) * r;
        l = zL;
        r = zR;
    }

    void reset() { zL = zR = 0.0f; }
};

struct OutputPolish {
    DcBlocker dcL, dcR;
    TransientClipper clip;
    OnePoleLP hf;
    bool prepared = false;

    void reset() {
        dcL.reset(); dcR.reset();
        clip.reset();
        hf.reset();
        prepared = false;
    }

    void prepare(float sampleRate) {
        if (prepared) return;
        prepared = true;
        // Fixed DC cut
        dcL.setCutoff(12.0f, sampleRate);
        dcR.setCutoff(12.0f, sampleRate);
        // Light transient clamp ballistics
        clip.setBallistics(0.35f, 35.0f, sampleRate);
    }

    void process(float& l, float& r, float fundamentalHz, float edge01, float sampleRate) {
        // DC block first
        l = dcL.process(l);
        r = dcR.process(r);

#ifdef METAMODULE
        // MetaModule: skip saturation + transient clipper to save CPU
        // Just do HF smoothing (already precomputed coefficient)
        hf.process(l, r);
#else
        // Edge-driven asymmetric saturation (character wins)
        float drive = std::max(0.0f, std::min(1.0f, edge01));
        float asym = 0.10f + 0.10f * drive;
        l = softClipAsym(l, drive, asym);
        r = softClipAsym(r, drive, asym);

        // Transient control (very light)
        clip.strength = 0.20f + 0.35f * drive;
        clip.thresh = 0.94f - 0.06f * drive;
        clip.process(l, r);

        // Dynamic HF smoothing: more smoothing at higher pitch + higher drive
        // Coefficient is precomputed via updateHfCutoff() at control rate
        hf.process(l, r);
#endif
    }

    // Call this at control rate (~32 samples), not per-sample
    void updateHfCutoff(float fundamentalHz, float edge01, float sampleRate) {
        float f = std::max(20.0f, fundamentalHz);
        float drive = std::max(0.0f, std::min(1.0f, edge01));
        float pitchNorm = std::min(1.0f, f / 2000.0f);
        float cutoff = 18000.0f;
        cutoff *= (1.0f - 0.55f * drive);
        cutoff *= (1.0f - 0.35f * pitchNorm);
        cutoff = std::max(3500.0f, std::min(18000.0f, cutoff));
        hf.setCutoff(cutoff, sampleRate);
    }
};

} // namespace phaseon
