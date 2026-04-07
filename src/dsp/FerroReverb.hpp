// FerroReverb.hpp
// Self-contained Griesinger/Dattorro FDN reverb for Ferroklast.
// Adapted from Mutable Instruments Elements by Emilie Gillet (MIT License).
// Re-tuned for 48kHz native operation (all delay lengths scaled x1.5 from 32kHz originals).
// Uses float ring buffer - no integer conversion overhead vs the original STM32 uint16 design.
// No external stmlib dependency. Completely self-contained.

#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

#ifndef METAMODULE

namespace morphworx {

// ---------------------------------------------------------------------------
// CosineOscillator - second-order IIR resonator (Goertzel-style).
// cosf() is called once in Init(), never in the audio loop.
// ---------------------------------------------------------------------------
class CosineOscillator {
public:
    CosineOscillator() : x_(1.f), y_(0.f), coeff_(0.f) {}

    // normalised_freq: cycles per sample (e.g. 0.5f / 48000.f)
    void Init(float normalised_freq) {
        float w = 2.f * 3.14159265358979f * normalised_freq * 32.f;
        coeff_ = 2.f * std::cos(w);
        x_     = std::cos(w);
        y_     = std::cos(2.f * w);
    }

    inline float Next() {
        float next = coeff_ * y_ - x_;
        x_ = y_;
        y_ = next;
        return x_;
    }

    inline float value() const { return x_; }

private:
    float x_, y_, coeff_;
};

// ---------------------------------------------------------------------------
// Reverb - Griesinger/Dattorro topology.
//
// Ring buffer layout (48kHz-scaled, original 32kHz values x1.5):
//   ap1:   base=0,     len=225   (was 150)
//   ap2:   base=226,   len=321   (was 214)
//   ap3:   base=548,   len=479   (was 319)
//   ap4:   base=1028,  len=791   (was 527)
//   dap1a: base=1820,  len=3273  (was 2182)
//   dap1b: base=5094,  len=4035  (was 2690)
//   del1:  base=9130,  len=6752  (was 4501)
//   dap2a: base=15883, len=3788  (was 2525)
//   dap2b: base=19672, len=3296  (was 2197)
//   del2:  base=22969, len=9468  (was 6312)
// Total: 32437 samples, fits in 65536 buffer (power-of-2 for fast masking).
//
// API is identical to elements::Reverb so it can be swapped in directly.
// ---------------------------------------------------------------------------

static constexpr int32_t kRvbBufSize = 65536;
static constexpr int32_t kRvbBufMask = kRvbBufSize - 1;

// Delay line base addresses and lengths
static constexpr int32_t kAp1Base   = 0,     kAp1Len   = 225;
static constexpr int32_t kAp2Base   = 226,   kAp2Len   = 321;
static constexpr int32_t kAp3Base   = 548,   kAp3Len   = 479;
static constexpr int32_t kAp4Base   = 1028,  kAp4Len   = 791;
static constexpr int32_t kDap1aBase = 1820,  kDap1aLen = 3273;
static constexpr int32_t kDap1bBase = 5094,  kDap1bLen = 4035;
static constexpr int32_t kDel1Base  = 9130,  kDel1Len  = 6752;
static constexpr int32_t kDap2aBase = 15883, kDap2aLen = 3788;
static constexpr int32_t kDap2bBase = 19672, kDap2bLen = 3296;
static constexpr int32_t kDel2Base  = 22969, kDel2Len  = 9468;

class Reverb {
public:
    Reverb()
        : amount_(0.f), input_gain_(0.2f), reverb_time_(0.7f),
          // lp_=0.70 keeps the in-loop LP effective: raising this toward 1.0 allows
          // mid-high frequency energy to accumulate 2x faster in the tank, causing
          // clipping at moderate reverb times with broadband input (e.g. drums).
          // Perceived dullness should be addressed at the output mix stage, not here.
          diffusion_(0.625f), lp_(0.70f),
          lp_decay_1_(0.f), lp_decay_2_(0.f),
          write_ptr_(0), buf_(nullptr)
    {
        lfo_val_[0] = lfo_val_[1] = 0.f;
    }

    void Init(float* buffer) {
        buf_ = buffer;
        std::fill(buf_, buf_ + kRvbBufSize, 0.f);
        write_ptr_ = 0;
        lfo_[0].Init(0.5f / 48000.f);
        lfo_[1].Init(0.3f / 48000.f);
        lfo_val_[0] = lfo_val_[1] = 0.f;
    }

    // CALLING CONTRACT: *left and *right must be seeded with the dry send bus
    // signal before calling Process(). This is an in-place function:
    //   float revL = sendBus, revR = sendBus;
    //   reverb_.Process(&revL, &revR, 1);
    // Passing zero-initialised pointers results in a silent reverb.
    void Process(float* left, float* right, size_t size) {
        const float kap    = diffusion_;
        const float klp    = lp_;
        const float krt    = reverb_time_;
        const float amount = amount_;
        const float gain   = input_gain_;

        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        while (size--) {
            // Advance write pointer (newest sample at write_ptr_)
            if (--write_ptr_ < 0) write_ptr_ += kRvbBufSize;

            // LFO removed: modulation causes audible flutter (pitch chorusing) on
            // percussive material. Fixed delay lines are clean for drums; metallic
            // ringing risk is negligible given broadband transient content.

            float acc, w, prev;

            // -------------------------------------------------------
            // AP1 smear block removed: was corrupting ap1 tail with a modulated
            // sample before the diffusion chain ran, causing excessive input smear
            // on percussive transients. Tank APs (dap1a, dap2a/b) handle decorrelation.
            // -------------------------------------------------------
            // Input: sum L+R, apply gain.
            // 1e-18f DC injection: prevents lp_1, lp_2, and all FDN delay lines
            // from decaying into IEEE 754 subnormal range as the tail fades.
            // Without this, the CPU switches to microcode for denormal arithmetic
            // and the audio thread can stall by up to 100x during silence.
            // -------------------------------------------------------
            acc = (*left + *right) * gain;
            acc += 1e-18f;

            // -------------------------------------------------------
            // 4-stage input allpass diffusion
            // -------------------------------------------------------
            // ap1
            prev = buf_[(write_ptr_ + kAp1Base + kAp1Len - 1) & kRvbBufMask];
            acc += prev * kap;
            w = acc;
            buf_[(write_ptr_ + kAp1Base) & kRvbBufMask] = w;
            acc = w * (-kap) + prev;

            // ap2
            prev = buf_[(write_ptr_ + kAp2Base + kAp2Len - 1) & kRvbBufMask];
            acc += prev * kap;
            w = acc;
            buf_[(write_ptr_ + kAp2Base) & kRvbBufMask] = w;
            acc = w * (-kap) + prev;

            // ap3 and ap4 disabled for percussive clarity — 4 stages sum to ~38ms
            // of pre-diffusion smear which blurs drum transients before reaching the tank.
            // Buffer regions for ap3/ap4 remain allocated but unused; re-enable for
            // hall/plate use where pre-diffusion smear is desirable.

            float apout = acc;

            // -------------------------------------------------------
            // Main loop LEFT: dap1a, dap1b -> del1
            // -------------------------------------------------------
            acc = apout;

            // Fixed read from del2 tail (98.4% depth, ~194ms at 48kHz).
            // No LFO modulation — avoids flutter on percussive material.
            acc += buf_[(write_ptr_ + kDel2Base + (int32_t)(6211.f * 1.5f)) & kRvbBufMask] * krt;

            // LP filter (lp_1)
            lp_1 += klp * (acc - lp_1);
            acc = lp_1;

            // dap1a allpass — fixed read at tail, no LFO modulation.
            prev = buf_[(write_ptr_ + kDap1aBase + kDap1aLen - 1) & kRvbBufMask];
            acc += prev * (-kap);
            w = acc;
            buf_[(write_ptr_ + kDap1aBase) & kRvbBufMask] = w;
            acc = w * kap + prev;

            // dap1b allpass
            prev = buf_[(write_ptr_ + kDap1bBase + kDap1bLen - 1) & kRvbBufMask];
            acc += prev * kap;
            w = acc;
            buf_[(write_ptr_ + kDap1bBase) & kRvbBufMask] = w;
            acc = w * (-kap) + prev;

            // Store unscaled acc in del1 (feedback path reads this with krt).
            // Wet output = acc * 2.0 — the x2 boosts output level, NOT the feedback gain.
            // Storing acc*2 here (the original bug) doubled loop gain and caused runaway.
            buf_[(write_ptr_ + kDel1Base) & kRvbBufMask] = acc;

            // Output left: wet = acc * 2.0
            *left += (acc * 2.f - *left) * amount;

            // -------------------------------------------------------
            // Main loop RIGHT: dap2a, dap2b -> del2
            // -------------------------------------------------------
            acc = apout;

            // Read tail of del1
            acc += buf_[(write_ptr_ + kDel1Base + kDel1Len - 1) & kRvbBufMask] * krt;

            // LP filter (lp_2)
            lp_2 += klp * (acc - lp_2);
            acc = lp_2;

            // dap2a allpass
            prev = buf_[(write_ptr_ + kDap2aBase + kDap2aLen - 1) & kRvbBufMask];
            acc += prev * kap;
            w = acc;
            buf_[(write_ptr_ + kDap2aBase) & kRvbBufMask] = w;
            acc = w * (-kap) + prev;

            // dap2b allpass
            prev = buf_[(write_ptr_ + kDap2bBase + kDap2bLen - 1) & kRvbBufMask];
            acc += prev * (-kap);
            w = acc;
            buf_[(write_ptr_ + kDap2bBase) & kRvbBufMask] = w;
            acc = w * kap + prev;

            // Store unscaled acc in del2 (same reasoning as del1 above).
            buf_[(write_ptr_ + kDel2Base) & kRvbBufMask] = acc;

            // Output right: wet = acc * 2.0
            *right += (acc * 2.f - *right) * amount;

            ++left;
            ++right;
        }

        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
    }

    inline void set_amount(float amount)         { amount_      = amount; }
    inline void set_input_gain(float input_gain) { input_gain_  = input_gain; }
    inline void set_time(float reverb_time)      { reverb_time_ = reverb_time; }
    inline void set_diffusion(float diffusion)   { diffusion_   = diffusion; }
    inline void set_lp(float lp)                 { lp_          = lp; }

private:
    float amount_;
    float input_gain_;
    float reverb_time_;
    float diffusion_;
    float lp_;
    float lp_decay_1_;
    float lp_decay_2_;

    int32_t          write_ptr_;
    float*           buf_;
    CosineOscillator lfo_[2];
    float            lfo_val_[2];

    Reverb(const Reverb&)            = delete;
    Reverb& operator=(const Reverb&) = delete;
};

} // namespace morphworx

#endif // METAMODULE
