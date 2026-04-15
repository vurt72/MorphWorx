#pragma once
#include <cstring>

namespace morphworx {

// 4-stage series Schroeder allpass diffusion chain for early-reflection
// smearing. Applies the classic allpass structure:
//
//   y[n] = -g * x[n] + x[n-D] + g * y[n-D]
//
// where g = kG (fixed coefficient), D = per-stage delay in samples
// (prime-ish, scaled to sample rate).
//
// Two independent mono chains (L and R) with the same delay lengths but
// independent state → fully stereo, no cross-leakage between channels.
//
// Usage:
//   init(sampleRate)                               once on setup / SR change
//   process(inL, inR, &outL, &outR, amount)        per sample in audio thread
//   reset()                                        on module reset
//
// 'amount' in [0,1]: linear crossfade between dry input (0) and allpass
// chain output (1). The chain is always running (avoids click on blend change).
//
// RT-safe: fixed-size stack buffers, no dynamic allocation, no transcendentals.

class Diffusion {
public:
    static constexpr int   kStages   = 4;
    // Max buffer per stage: 512 samples (covers 389 * srRatio up to ~2x)
    static constexpr int   kBufSize  = 512;
    static constexpr int   kBufMask  = kBufSize - 1;
    // Fixed allpass coefficient. 0.6 gives dense reflections without
    // metallic ringing; safe guard: never reach ±1.
    static constexpr float kG        = 0.6f;

    Diffusion() {
        std::memset(bufL_, 0, sizeof(bufL_));
        std::memset(bufR_, 0, sizeof(bufR_));
        std::memset(writePos_, 0, sizeof(writePos_));
        std::memset(delayLen_, 0, sizeof(delayLen_));
    }

    // Call on construction and onSampleRateChange. Safe from audio thread
    // only if called before first process(), or from non-audio thread.
    void init(float sampleRate) {
        if (sampleRate <= 0.f) return;
        float ratio = sampleRate / 48000.f;

        // Prime-ish delay lengths at 48 kHz: 113, 173, 259, 389 samples.
        // Chosen for mutual coprimality — avoids constructive resonance.
        constexpr float kBaseLengths[kStages] = { 113.f, 173.f, 259.f, 389.f };
        for (int s = 0; s < kStages; s++) {
            int len = static_cast<int>(kBaseLengths[s] * ratio + 0.5f);
            if (len < 1)          len = 1;
            if (len >= kBufSize)  len = kBufSize - 1;
            delayLen_[s] = len;
        }
        reset();
    }

    // Reset all state without recomputing coefficients.
    void reset() {
        std::memset(bufL_,    0, sizeof(bufL_));
        std::memset(bufR_,    0, sizeof(bufR_));
        std::memset(writePos_, 0, sizeof(writePos_));
    }

    // Per-sample processing. amount in [0,1].
    // amount=0 → outputs are the raw inputs (bypass).
    // amount=1 → outputs are the fully-diffused allpass chain output.
    void process(float inL, float inR, float* outL, float* outR, float amount) {
        float dL = inL;
        float dR = inR;

        for (int s = 0; s < kStages; s++) {
            int D     = delayLen_[s];
            int wp    = writePos_[s];
            int rp    = (wp - D) & kBufMask;

            float delayedL = bufL_[s][rp];
            float delayedR = bufR_[s][rp];

            // Allpass output:   -g*x + x[n-D] + g*y[n-D]
            // Which rearranges to write: x - g*delayed, read: delayed + g*write
            float wL = dL - kG * delayedL;
            float wR = dR - kG * delayedR;
            bufL_[s][wp] = wL;
            bufR_[s][wp] = wR;

            dL = delayedL + kG * wL;
            dR = delayedR + kG * wR;

            writePos_[s] = (wp + 1) & kBufMask;
        }

        // Blend dry ↔ diffused
        *outL = inL + amount * (dL - inL);
        *outR = inR + amount * (dR - inR);
    }

private:
    float bufL_[kStages][kBufSize] = {};
    float bufR_[kStages][kBufSize] = {};
    int   writePos_[kStages]       = {};
    int   delayLen_[kStages]       = {};
};

} // namespace morphworx
