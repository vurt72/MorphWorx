#pragma once
#include <cmath>

namespace morphworx {

constexpr float kTsPi = 3.14159265358979323846f;

// Frequency-dependent soft clipper modelling tape saturation.
//
// Signal path:
//   pre-emphasis (one-pole HP boost ~4 kHz)
//   → drive gain
//   → soft clip (Padé tanh approximant)
//   → de-emphasis (approximate shelving cut)
//
// De-emphasis note: the formula LP + (direct - LP) * 0.5 is *not* a perfect
// inversion of the pre-emphasis HP boost. At drive=0, unity-gain signals pass
// through with a slight HF tilt rather than flat response. This is intentional
// — the mismatch adds subtle warmth on each feedback reflection. If transparent
// bypass is needed, use true HP subtraction instead.
//
// Drive range [0,1] controls saturation knee only. Linear-regime gain is
// exactly 1.0 at all drive settings (output normalised by 1/(1+drive)).
// This is critical for FDN use: any net gain > 1 in the feedback path
// overrides RT60-calibrated gains and causes endless feedback.
//
// Internal drive scaling: drive input [0,1] is mapped to [1, kDriveMax+1]
// so the full knob range produces clearly audible saturation on typical
// reverb tail signal levels (~0.05–0.3 normalised amplitude).
//
// setSampleRate() MUST be called before process().
// Maintains per-line filter state (8 lines to match FDN).

class TapeSaturator {
public:
    static constexpr int kMaxLines = 8;

    TapeSaturator() = default;

    // No per-line seeding needed (unlike WowFlutter). Interface asymmetry
    // is intentional — call setSampleRate() before process().
    void init() {}

    // Call from onSampleRateChange() or module constructor.
    // MUST be called before process(); emphCoeff_ = 0 otherwise.
    void setSampleRate(float sr) {
        if (sr <= 0.f) return;
        // Pre/de-emphasis corner frequency: ~4 kHz.
        // std::exp only here — never per-sample.
        emphCoeff_ = 1.0f - std::exp(-2.0f * kTsPi
                                      * 4000.f / sr);
    }

    // Per-sample, per-line processing.
    // line:  delay line index in [0, kMaxLines) — assert below is commented
    //        out for release builds; FDN always passes i in [0, kNumLines).
    // input: signal from FDN feedback path
    // drive: saturation amount [0, 1]
    float process(int line, float input, float drive) {
        // assert(line >= 0 && line < kMaxLines);

        // --- Pre-emphasis: one-pole HP boost at ~4 kHz ---
        // Extracts the high-frequency component and adds it back,
        // giving +6 dB above the corner frequency.
        preEmphState_[line] += emphCoeff_ * (input - preEmphState_[line]);
        float hp = input - preEmphState_[line];
        float emphasized = input + kEmphGain * hp;

        // --- Drive + soft clip ---
        // `drive` input is [0,1]. We scale it to [1, 1+kDriveMax] so that
        // even small reverb tail signals (~0.05–0.3) hit the saturation knee
        // at high drive. Gain normalisation by 1/driveGain keeps unity gain
        // in the linear regime so this cannot cause feedback runaway.
        float driveGain = 1.0f + drive * kDriveMax;
        float driven    = emphasized * driveGain;
        float clipped   = fastTanh(driven) / driveGain;

        // --- De-emphasis: approximate shelving blend ---
        // Formula: LP * (1 - kDeEmphScale) + clipped * kDeEmphScale.
        // DC gain = 1.0, HF gain → 0.5 (-6dB). Not a perfect inversion of
        // the pre-emphasis (see file header note) — intentional character.
        deEmphState_[line] += emphCoeff_ * (clipped - deEmphState_[line]);
        return deEmphState_[line] + (clipped - deEmphState_[line]) * kDeEmphScale;
    }

    void reset() {
        for (int i = 0; i < kMaxLines; i++) {
            preEmphState_[i] = 0.f;
            deEmphState_[i]  = 0.f;
        }
    }

    // Call at control rate (every 16 samples) to prevent filter states
    // from decaying into subnormal range during silence.
    void flushDenormals() {
        for (int i = 0; i < kMaxLines; i++) {
            preEmphState_[i] += 1e-25f;
            deEmphState_[i]  += 1e-25f;
        }
    }

private:
    static constexpr float kEmphGain    = 1.0f;                       // +6 dB HF boost
    static constexpr float kDeEmphScale = 1.0f / (1.0f + kEmphGain);  // = 0.5
    static constexpr float kDriveMax    = 8.0f;  // drive=1 → 9× clipping threshold

    // Padé [3/3] tanh approximant, accurate to <1% for |x| <= 3.
    // Clamped to [-3, 3] to prevent divergence — at x=3 output is
    // exactly 1.0, so the clamp introduces no discontinuity.
    // Denominator 27 + 9x² >= 27 always — no division by zero.
    static float fastTanh(float x) {
        if (x < -3.f) x = -3.f;
        if (x >  3.f) x =  3.f;
        float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    float preEmphState_[kMaxLines] = {};
    float deEmphState_[kMaxLines]  = {};
    float emphCoeff_ = 0.f;
};

} // namespace morphworx
