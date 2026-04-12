#pragma once
#include "WowFlutter.hpp"
#include <cmath>
#include <cstring>

namespace morphworx {

// 8-line Feedback Delay Network with Hadamard mixing for stereo hall reverb.
//
// Architecture per sample:
//   read (linear-interp fractional delay)
//   → WowFlutter offset (pitch modulation)
//   → multiply by feedback gain
//   → HI_DAMP (one-pole LP per line)
//   → LO_DAMP (one-pole HP per line)
//   → Hadamard H8 mix (in-place Walsh-Hadamard transform)
//   → write back to delay lines
//
// Lo-fi saturation is applied at MODULE OUTPUT, not inside this loop.
// Applying saturation here with normalised gain caused the reverb to
// disappear: the gain-normalised tanh attenuates quiet tail signals.
//
// Stereo I/O: even lines (0,2,4,6) ← inL, odd (1,3,5,7) ← inR.
//             sum even → outL, sum odd → outR.
//
// Output gain: each channel sums 4 post-Hadamard lines (each normalised
// by 1/√8 ≈ 0.354), then scales by kOutputGain. Net per-channel gain at
// unity feedback is ~0.354 × 4 × kOutputGain. This is intentional —
// the module's MIX crossfade compensates at the output stage.
//
// NOTE: buffer_ is ~1MB. FDN must be heap-allocated (new/unique_ptr) or a
// direct member of a rack::Module struct — never a stack-local variable.
//
// onSampleRateChange calls init() which clears all buffers. Any running
// reverb tail is wiped on sample rate change. This is expected behaviour.

class FDN {
public:
    static constexpr int   kNumLines    = 8;
    static constexpr int   kBufSize     = 32768;          // power-of-2
    static constexpr int   kBufMask     = kBufSize - 1;
    static constexpr int   kControlRate = 16;              // matches Ferroklast
    static constexpr float kPi          = 3.14159265358979323846f;
    static constexpr float kOutputGain  = 0.25f;           // see gain note above

    FDN() {
        std::memset(buffer_, 0, sizeof(buffer_));
    }

    // Call once at module construction or onSampleRateChange.
    void init(float sampleRate) {
        if (sampleRate <= 0.f) return;
        sampleRate_ = sampleRate;
        srRatio_    = sampleRate / 48000.f;
        writePos_   = 0;
        controlCounter_ = 0;
        smoothedSize_ = 1.0f;

        // Precompute SIZE smoothing coefficient (~20ms at control rate).
        // std::exp at init only — never per sample.
        float ctrlRate = sampleRate / static_cast<float>(kControlRate);
        smoothCoeff_ = 1.f - std::exp(-1.f / (ctrlRate * 0.020f));

        std::memset(buffer_,       0, sizeof(buffer_));
        std::memset(hiDampState_,  0, sizeof(hiDampState_));
        std::memset(loDampState_,  0, sizeof(loDampState_));

        for (int i = 0; i < kNumLines; i++) {
            wf_[i].init(i, 0x4AE7u + static_cast<uint32_t>(i) * 0x1337u);
            wf_[i].setSampleRate(sampleRate);
        }

        // Initialize delay lengths and gains with defaults
        updateDelayLengths(1.0f);
        updateFeedbackGains(2.0f); // 2 second default RT60
        updateHiDampCoeffs(8000.f);
        updateLoDampCoeffs(100.f);
    }

    // Control-rate parameter update. Call from the module's control-rate block.
    // size:    [0.5, 2.0] scales delay line lengths
    // decay:   RT60 in seconds (0 = no feedback)
    // hiDamp:  LP cutoff Hz for high-frequency damping
    // loDamp:  HP cutoff Hz for low-frequency damping
    void setParams(float size, float decay, float hiDamp, float loDamp) {
        // SIZE is one-pole smoothed to avoid discontinuities when
        // recomputing delay lengths. smoothCoeff_ is precomputed from
        // sample rate in init() for consistent ~20ms time constant.
        smoothedSize_ += smoothCoeff_ * (size - smoothedSize_);
        updateDelayLengths(smoothedSize_);
        updateFeedbackGains(decay);
        updateHiDampCoeffs(hiDamp);
        updateLoDampCoeffs(loDamp);
    }

    // Toggle infinite-sustain freeze mode. When active:
    //   • feedback gain is forced to 1.0 (no decay)
    //   • hi/lo damping filters are bypassed (tail spectrum locked)
    // The parent module is responsible for zeroing input injection.
    void setFreeze(bool active) { frozen_ = active; }

    // Per-sample processing.
    // lofiDepth in [0,1]: controls WowFlutter pitch modulation depth.
    void process(float inL, float inR, float* outL, float* outR,
                 float lofiDepth) {

        // --- Control-rate housekeeping ---
        if (++controlCounter_ >= kControlRate) {
            controlCounter_ = 0;
            for (int i = 0; i < kNumLines; i++) {
                hiDampState_[i] += 1e-25f;
                loDampState_[i] += 1e-25f;
                wf_[i].flushDenormals();
            }
        }

        // --- Read from delay lines with fractional interpolation ---
        float lineOut[kNumLines];

        for (int i = 0; i < kNumLines; i++) {
            float wfOffset = wf_[i].process(lofiDepth);

            // Fractional read position.
            // Double-add wrap: wow amplitude (up to 350 samples) can push
            // readPosF below -kBufSize at very high sample rates where
            // delayLengths_[i] is clamped near kBufSize-8. Two adds of
            // kBufSize always suffice since max deficit < 2 × kBufSize.
            float readPosF = static_cast<float>(writePos_) - delayLengths_[i] + wfOffset;
            if (readPosF < 0.f) readPosF += static_cast<float>(kBufSize);
            if (readPosF < 0.f) readPosF += static_cast<float>(kBufSize);

            int readPos0   = static_cast<int>(readPosF);
            float frac     = readPosF - static_cast<float>(readPos0);
            int idx0       = readPos0 & kBufMask;
            int idx1       = (readPos0 + 1) & kBufMask;

            // Linear interpolation
            float sample = buffer_[i][idx0] + frac * (buffer_[i][idx1] - buffer_[i][idx0]);

            // --- Feedback gain ---
            // Freeze: bypass decay (gain = 1.0) AND bypass damping filters
            // so the captured tail circulates indefinitely at fixed spectrum.
            if (frozen_) {
                lineOut[i] = sample;   // gain 1.0, no damping
                continue;
            }
            sample *= feedbackGains_[i];

            // --- HI_DAMP: one-pole lowpass ---
            hiDampState_[i] += hiDampCoeffs_[i] * (sample - hiDampState_[i]);
            sample = hiDampState_[i];

            // --- LO_DAMP: one-pole highpass (signal minus lowpassed signal) ---
            loDampState_[i] += loDampCoeffs_[i] * (sample - loDampState_[i]);
            sample = sample - loDampState_[i];

            // --- TapeSaturator removed: applied at module output instead ---

            lineOut[i] = sample;
        }

        // --- Hadamard H8 mix (in-place Walsh-Hadamard transform) ---
        // 3 butterfly stages, then normalise by 1/sqrt(8).
        hadamard8(lineOut);

        // --- Inject input and write back ---
        for (int i = 0; i < kNumLines; i++) {
            float input = (i & 1) ? inR : inL; // even=L, odd=R
            buffer_[i][writePos_] = lineOut[i] + input;
        }

        // --- Output: sum even lines → L, odd lines → R ---
        float sumL = 0.f;
        float sumR = 0.f;
        for (int i = 0; i < kNumLines; i++) {
            if (i & 1)
                sumR += lineOut[i];
            else
                sumL += lineOut[i];
        }

        // Stereo widening via cross-subtraction.
        // After Hadamard mixing, sumL and sumR are moderately correlated
        // (both contain energy from both input channels). Subtracting a
        // fraction of the opposite channel creates anti-correlation which
        // is perceptually very wide.  kCross = 0.5 gives strong widening
        // while remaining mono-compatible (mono sum = 0.5*(sumL+sumR),
        // unchanged).  Gain normalisation by 1/(1+kCross) keeps output level
        // stable across the full correlation range.
        constexpr float kCross    = 0.5f;
        constexpr float kCrossNorm = kOutputGain / (1.0f + kCross);  // ~0.167
        *outL = (sumL - kCross * sumR) * kCrossNorm;
        *outR = (sumR - kCross * sumL) * kCrossNorm;

        // --- Advance write pointer ---
        writePos_ = (writePos_ + 1) & kBufMask;
    }

    void reset() {
        std::memset(buffer_,       0, sizeof(buffer_));
        std::memset(hiDampState_,  0, sizeof(hiDampState_));
        std::memset(loDampState_,  0, sizeof(loDampState_));
        writePos_       = 0;
        controlCounter_ = 0;
        smoothedSize_   = 1.0f;
        frozen_         = false;
        for (int i = 0; i < kNumLines; i++) {
            wf_[i].reset();
        }
    }

    // NOTE: init() clears buffers — any running reverb tail is wiped.
    void onSampleRateChange(float sampleRate) {
        init(sampleRate);
    }

private:
    // In-place 8-point Walsh-Hadamard transform, normalised by 1/sqrt(8).
    // 3 butterfly stages = 24 add/sub operations. No matrix storage.
    static void hadamard8(float* v) {
        // Stage 1: pairs (0,1) (2,3) (4,5) (6,7)
        float a, b;
        a = v[0]; b = v[1]; v[0] = a + b; v[1] = a - b;
        a = v[2]; b = v[3]; v[2] = a + b; v[3] = a - b;
        a = v[4]; b = v[5]; v[4] = a + b; v[5] = a - b;
        a = v[6]; b = v[7]; v[6] = a + b; v[7] = a - b;

        // Stage 2: pairs (0,2) (1,3) (4,6) (5,7)
        a = v[0]; b = v[2]; v[0] = a + b; v[2] = a - b;
        a = v[1]; b = v[3]; v[1] = a + b; v[3] = a - b;
        a = v[4]; b = v[6]; v[4] = a + b; v[6] = a - b;
        a = v[5]; b = v[7]; v[5] = a + b; v[7] = a - b;

        // Stage 3: pairs (0,4) (1,5) (2,6) (3,7)
        a = v[0]; b = v[4]; v[0] = a + b; v[4] = a - b;
        a = v[1]; b = v[5]; v[1] = a + b; v[5] = a - b;
        a = v[2]; b = v[6]; v[2] = a + b; v[6] = a - b;
        a = v[3]; b = v[7]; v[3] = a + b; v[7] = a - b;

        // Normalise: 1/sqrt(8) ≈ 0.35355339f
        constexpr float kNorm = 0.35355339f;
        for (int i = 0; i < 8; i++)
            v[i] *= kNorm;
    }

    void updateDelayLengths(float size) {
        // Base delay lengths in samples at 48 kHz (prime-ish, mutually coprime).
        // Local constexpr avoids static class member ODR issues on MinGW/GCC.
        constexpr float kBase[kNumLines] = {
            1483.f, 1699.f, 1867.f, 2053.f, 2251.f, 2399.f, 2617.f, 2803.f
        };
        for (int i = 0; i < kNumLines; i++) {
            float len = kBase[i] * srRatio_ * size;
            // Clamp: minimum 1 sample, maximum leaves 8-sample margin for
            // WowFlutter offset (max 3.5 samples) plus interpolation (1 sample).
            if (len < 1.f) len = 1.f;
            float maxLen = static_cast<float>(kBufSize - 8);
            if (len > maxLen) len = maxLen;
            delayLengths_[i] = len;
        }
    }

    void updateFeedbackGains(float rt60) {
        constexpr float kBase[kNumLines] = {
            1483.f, 1699.f, 1867.f, 2053.f, 2251.f, 2399.f, 2617.f, 2803.f
        };
        // gain = 10^(-3 * refDelayTime / rt60)  where refDelayTime uses
        // SIZE=1.0 (kBase at current sample rate only).
        //
        // IMPORTANT: we intentionally do NOT use delayLengths_[i] here.
        // If we did, SIZE=0.5 would give gain ~0.948 (near-unity for short
        // delays) which rings indefinitely, while SIZE=1.77 gives gain ~0.83
        // (decays quickly) — the exact opposite of user intuition.
        //
        // By anchoring gains to the size=1.0 reference, DECAY controls RT60
        // independently of SIZE. SIZE purely affects room character:
        // diffusion density, early-reflection spacing, and stereo spread.
        for (int i = 0; i < kNumLines; i++) {
            if (rt60 <= 0.f) {
                feedbackGains_[i] = 0.f;
            } else {
                // Reference delay at SIZE=1.0 (not current size — see above)
                float refDelayTimeSec = kBase[i] * srRatio_ / sampleRate_;
                // std::pow at control rate only — never per-sample.
                feedbackGains_[i] = std::pow(0.001f, refDelayTimeSec / rt60);
            }
        }
    }

    void updateHiDampCoeffs(float cutoffHz) {
        // One-pole LP: coeff = 1 - exp(-2π * fc / sr)
        // std::exp at control rate only.
        if (sampleRate_ <= 0.f) return;
        float c = 1.0f - std::exp(-2.0f * kPi * cutoffHz / sampleRate_);
        for (int i = 0; i < kNumLines; i++)
            hiDampCoeffs_[i] = c;
    }

    void updateLoDampCoeffs(float cutoffHz) {
        // One-pole LP (used to derive HP): coeff = 1 - exp(-2π * fc / sr)
        if (sampleRate_ <= 0.f) return;
        float c = 1.0f - std::exp(-2.0f * kPi * cutoffHz / sampleRate_);
        for (int i = 0; i < kNumLines; i++)
            loDampCoeffs_[i] = c;
    }

    // --- Delay line storage ---
    float buffer_[kNumLines][kBufSize] = {};

    // --- Per-line filter states ---
    float hiDampState_[kNumLines]  = {};
    float loDampState_[kNumLines]  = {};

    // --- Per-line parameters (precomputed at control rate) ---
    float delayLengths_[kNumLines]  = {};
    float feedbackGains_[kNumLines] = {};
    float hiDampCoeffs_[kNumLines]  = {};
    float loDampCoeffs_[kNumLines]  = {};

    // --- Wow/Flutter modulators (one per line) ---
    WowFlutter wf_[kNumLines];

    // --- Shared state ---
    float sampleRate_   = 48000.f;
    float srRatio_      = 1.f;
    float smoothedSize_ = 1.f;
    float smoothCoeff_  = 0.02f;  // precomputed in init()
    int   writePos_     = 0;
    int   controlCounter_ = 0;
    bool  frozen_         = false;
};

} // namespace morphworx
