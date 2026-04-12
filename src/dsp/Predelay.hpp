#pragma once
#include <cstring>

namespace morphworx {

// Simple stereo delay line with fractional-sample read for reverb predelay.
//
// Max 250 ms at up to 192 kHz = 48000 samples.
// Buffer is 65536 (power-of-2) for bitmask wrapping.
// Linear interpolation on read for smooth predelay time changes.
//
// NOTE: bufL_ + bufR_ = 2 × 65536 × 4 bytes = 512KB. Predelay must be
// heap-allocated (new/unique_ptr) or a direct rack::Module member — never
// a stack-local variable.
//
// onSampleRateChange (via init()) clears both buffers. Any in-flight
// predelay tail is wiped. This is expected behaviour.

class Predelay {
public:
    static constexpr int kBufSize = 65536;          // power-of-2
    static constexpr int kBufMask = kBufSize - 1;

    Predelay() = default;

    void init(float sampleRate) {
        if (sampleRate <= 0.f) return;
        sampleRate_ = sampleRate;
        writePos_   = 0;
        smoothedDelay_ = 0.f;
        // ~20ms smoothing time at any sample rate.
        // std::exp at init only — never per-sample.
        smoothCoeff_ = 1.f - std::exp(-1.f / (sampleRate * 0.020f));
        std::memset(bufL_, 0, sizeof(bufL_));
        std::memset(bufR_, 0, sizeof(bufR_));
    }

    // Set predelay time in milliseconds [0, 250].
    // Called at control rate — smoothing happens internally.
    void setDelayMs(float ms) {
        if (ms < 0.f) ms = 0.f;
        if (ms > 250.f) ms = 250.f;
        targetDelay_ = ms * 0.001f * sampleRate_; // ms → samples
    }

    // Per-sample stereo processing.
    void process(float inL, float inR, float* outL, float* outR) {
        // Write input
        bufL_[writePos_] = inL;
        bufR_[writePos_] = inR;

        // Smooth delay length to avoid clicks on knob turns
        smoothedDelay_ += smoothCoeff_ * (targetDelay_ - smoothedDelay_);

        // Fractional read position
        float readPosF = static_cast<float>(writePos_) - smoothedDelay_;
        // One add always sufficient: max smoothedDelay_ = 48000 (250ms @ 192kHz)
        // and kBufSize = 65536, so worst case readPosF = -48000;
        // after one add: -48000 + 65536 = 17536 > 0. QED.
        if (readPosF < 0.f) readPosF += static_cast<float>(kBufSize);

        int   readPos0 = static_cast<int>(readPosF);
        float frac     = readPosF - static_cast<float>(readPos0);
        int   idx0     = readPos0 & kBufMask;
        int   idx1     = (readPos0 + 1) & kBufMask;

        // Linear interpolation
        *outL = bufL_[idx0] + frac * (bufL_[idx1] - bufL_[idx0]);
        *outR = bufR_[idx0] + frac * (bufR_[idx1] - bufR_[idx0]);

        // Advance write pointer
        writePos_ = (writePos_ + 1) & kBufMask;
    }

    void reset() {
        writePos_      = 0;
        smoothedDelay_ = 0.f;
        targetDelay_   = 0.f;
        std::memset(bufL_, 0, sizeof(bufL_));
        std::memset(bufR_, 0, sizeof(bufR_));
    }

    // NOTE: clears buffers — any in-flight predelay tail is wiped.
    void onSampleRateChange(float sampleRate) {
        init(sampleRate);
    }

private:
    float bufL_[kBufSize] = {};
    float bufR_[kBufSize] = {};
    float sampleRate_    = 48000.f;
    float targetDelay_   = 0.f;
    float smoothedDelay_ = 0.f;
    float smoothCoeff_   = 0.001f;  // precomputed in init()
    int   writePos_      = 0;
};

} // namespace morphworx
