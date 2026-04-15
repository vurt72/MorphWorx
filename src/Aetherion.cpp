#include "plugin.hpp"
#include "dsp/Predelay.hpp"
#include "dsp/FDN.hpp"
#include "dsp/Diffusion.hpp"

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#endif

namespace {
struct ExpLut {
    static constexpr float kMin = -3.5f;
    static constexpr float kMax = 10.0f;
    static constexpr int kSize = 2048;
    static constexpr float kInvStep = (kSize - 1) / (kMax - kMin);

    float table[kSize] = {};
    bool ready = false;

    void init() {
        if (ready) return;
        float step = (kMax - kMin) / static_cast<float>(kSize - 1);
        for (int i = 0; i < kSize; ++i) {
            table[i] = std::exp(kMin + step * static_cast<float>(i));
        }
        ready = true;
    }

    float eval(float x) const {
        if (x <= kMin) return table[0];
        if (x >= kMax) return table[kSize - 1];
        float idx = (x - kMin) * kInvStep;
        int i = static_cast<int>(idx);
        float frac = idx - static_cast<float>(i);
        return table[i] + frac * (table[i + 1] - table[i]);
    }
};

static ExpLut gExpLut;

static void initExpLut() {
    gExpLut.init();
}

static float fastExpLut(float x) {
    return gExpLut.eval(x);
}
} // namespace

struct Aetherion : Module {
    enum ParamId {
        // Row 1: PREDELAY | DIFFUSION | SIZE
        PREDELAY_PARAM,
        DIFFUSION_PARAM,
        SIZE_PARAM,
        // Row 2: DECAY | MOD | HI DAMP
        DECAY_PARAM,
        MOD_PARAM,
        HI_DAMP_PARAM,
        // Row 3: LO DAMP | TILT | LO-FI
        LO_DAMP_PARAM,
        TILT_PARAM,
        LOFI_PARAM,
        // Row 4: MIX | SHIMMER | MODE
        MIX_PARAM,
        SHIMMER_PARAM,
        MODE_PARAM,
        // SWELL + FREEZE
        FREEZE_BTN_PARAM,
        SWELL_RISE_PARAM,
        SWELL_FALL_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_L_INPUT,
        IN_R_INPUT,
        DECAY_CV_INPUT,
        SIZE_CV_INPUT,
        LOFI_CV_INPUT,
        FREEZE_GATE_INPUT,
        DIFF_CV_INPUT,
        TILT_CV_INPUT,
        MOD_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        SWELL_OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        FREEZE_LIGHT,
        LIGHTS_LEN
    };

    // MODE switch positions
    static constexpr int MODE_CLEAN   = 0;
    static constexpr int MODE_WARM    = 1;
    static constexpr int MODE_DEGRADE = 2;

    // Control-rate divider (matches Ferroklast convention)
    static constexpr int kControlRate = 16;

    // DSP blocks
    morphworx::Predelay   predelay_;
    morphworx::FDN        fdn_;
    morphworx::Diffusion  diffusion_;

    // Smoothed parameter state (one-pole IIR per param)
    float smoothPredelay_   = 0.f;
    float smoothSize_       = 1.f;
    float smoothDecay_      = 2.f;
    float smoothHiDamp_     = 8000.f;
    float smoothLoDamp_     = 100.f;
    float smoothLofi_       = 0.f;
    float smoothMix_        = 0.5f;
    float smoothDiffusion_  = 0.5f;
    float smoothMod_        = 0.1f;
    float smoothTilt_       = 0.f;
    float cachedWetGain_    = 0.7071f;
    float cachedDryGain_    = 0.7071f;

    // TILT EQ state: per-channel one-pole LP state (wet signal only)
    // Shelf formula: lp = lp + coeff*(x - lp); hi = x - lp;
    // output = lp*(1 + loGain) + hi*(1 + hiGain)  where hiGain = -loGain = tilt*0.5
    // tiltLpCoeff_ computed at control-rate from fc=600 Hz.
    float tiltLpState_[2]  = {};   // [0]=L, [1]=R
    float tiltLpCoeff_     = 0.f;  // set in init / onSampleRateChange

    int controlCounter_ = 0;

    // SWELL envelope follower state
    float sampleRate_      = 48000.f;
    float swellEnv_        = 0.f;
    float swellRiseCoeff_  = 0.f;   // set in onSampleRateChange + control-rate
    float swellFallCoeff_  = 0.f;

    // SHIMMER: dual-head granular pitch shifter (octave up, 2× read speed).
    // Buffer 8192 samples → grain period ~85ms at 48kHz. Two heads offset by
    // half a grain with Hann windows guarantee wA+wB=1 (constant amplitude).
    // shimDistA/B = distance (samples) behind the write head; both decrement
    // by 1 per sample (net read advance = 2 vs write advance of 1 = octave up).
    static constexpr int kShimBufSize = 8192;
    static constexpr int kShimBufMask = kShimBufSize - 1;
    float shimBuf_[2][kShimBufSize] = {};  // heap-allocated with Module struct
    int   shimWrite_   = 0;
    float shimDistA_   = float(kShimBufSize) * 0.25f;  // 1/4 grain behind write
    float shimDistB_   = float(kShimBufSize) * 0.75f;  // 3/4 behind; wA+wB=1

    // SHIMMER fifth-interval layer (+7 semitones, ratio 3/2).
    // Grain size = kShimBufSize/2 = 4096 → decrement 0.5/sample gives
    // grain period 4096/0.5 = 8192 samples, matching the octave period.
    // C and D initialised 0.5 grain-phases apart → wC + wD = 1 always.
    static constexpr int   kShimFifthGrain  = kShimBufSize / 2;   // 4096
    static constexpr float kShimFifthGrainF = float(kShimFifthGrain);
    float shimDistC_ = kShimFifthGrainF * 0.25f;  // head C
    float shimDistD_ = kShimFifthGrainF * 0.75f;  // head D

    // SHIMMER stereo spread: ~8 ms offset between L and R reads.
    // Octave shifts R earlier; fifth shifts R later → cross-spread.
    static constexpr float kShimStereoSpread = 384.f;  // ~8 ms at 48 kHz

    // FREEZE: zeros signal injection so the FDN recirculates its current tail.
    bool                 freezeActive_     = false;  // latched by button
    float                freezeBlend_      = 0.f;    // 0=normal, 1=fully frozen (slewed)
    dsp::BooleanTrigger  freezeBtnTrigger_;   // detects momentary button press
    // Gate CV is level-sensitive (no SchmittTrigger needed)

    Aetherion() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // DECAY, HI_DAMP, LO_DAMP use log-space storage so knob travel
        // feels musical. displayBase=e makes the tooltip show natural units.
        // e.g. DECAY: stored = log(t), display = e^stored = t seconds.
        static constexpr float kE = 2.71828182845904523536f;

        configParam(PREDELAY_PARAM,  0.f,   250.f, 20.f,  "Predelay",  " ms");
        configParam(DIFFUSION_PARAM, 0.f,   1.f,   0.5f,  "Diffusion");
        configParam(SIZE_PARAM,      0.5f,  2.0f,  1.0f,  "Size");
        // Decay: log-space 0.05 s … 20 s, default 2 s
        configParam(DECAY_PARAM,    std::log(0.05f), std::log(20.f), std::log(2.f),
                    "Decay", " s", kE);
        configParam(MOD_PARAM,      0.f,   1.f,   0.1f,  "Mod Depth");
        // Hi Damp: log-space 500 Hz … 16 kHz, default 8 kHz
        configParam(HI_DAMP_PARAM,  std::log(500.f), std::log(16000.f), std::log(8000.f),
                    "Hi Damp", " Hz", kE);
        // Lo Damp: log-space 20 Hz … 1 kHz, default 100 Hz
        configParam(LO_DAMP_PARAM,  std::log(20.f), std::log(1000.f), std::log(100.f),
                    "Lo Damp", " Hz", kE);
        configParam(TILT_PARAM,     -1.f,  1.f,   0.f,   "Tilt EQ");
        configParam(LOFI_PARAM,     0.f,   1.f,   0.f,   "Lo-Fi");
        configParam(MIX_PARAM,      0.f,   1.f,   0.5f,  "Mix");
        configSwitch(SHIMMER_PARAM, 0.f,   1.f,   0.f,   "Shimmer", {"Off", "On"});
        configSwitch(MODE_PARAM,    0.f,   2.f,   1.f,   "Mode", {"Clean", "Warm", "Degrade"});
        paramQuantities[MODE_PARAM]->snapEnabled = true;
        configButton(FREEZE_BTN_PARAM, "Freeze");
        // SWELL: log-space ms. Base=e so tooltip shows natural ms value.
        // RISE: 1 – 2000 ms, default 50 ms
        // FALL: 10 – 10000 ms, default 500 ms
        configParam(SWELL_RISE_PARAM, std::log(1.f),    std::log(2000.f),  std::log(50.f),
                    "Swell Rise", " ms", kE);
        configParam(SWELL_FALL_PARAM, std::log(10.f),   std::log(10000.f), std::log(500.f),
                    "Swell Fall", " ms", kE);

        configInput(IN_L_INPUT,         "Left");
        configInput(IN_R_INPUT,         "Right (normalled to Left)");
        configInput(DECAY_CV_INPUT,     "Decay CV (0-10V adds up to +18 s)");
        configInput(SIZE_CV_INPUT,      "Size CV (0-10V adds up to +1.5)");
        configInput(LOFI_CV_INPUT,      "Lo-Fi CV (0-10V depth)");
        configInput(FREEZE_GATE_INPUT,  "Freeze gate (level-sensitive: ≥1V holds freeze)");
        configInput(DIFF_CV_INPUT,      "Diffusion CV (0-10V adds 0-1 depth)");
        configInput(TILT_CV_INPUT,      "Tilt CV (-5V to +5V)");
        configInput(MOD_CV_INPUT,       "Mod Depth CV (0-10V adds 0-1 depth)");
        configOutput(OUT_L_OUTPUT,   "Left");
        configOutput(OUT_R_OUTPUT,   "Right");
        configOutput(SWELL_OUT_OUTPUT, "Swell (0–10V envelope follower)");
        configLight(FREEZE_LIGHT, "Freeze active");

        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);

        initExpLut();
        diffusion_.init(48000.f);
        initTiltCoeff();
        updateMixGains();
    }

    void updateMixGains() {
        cachedWetGain_ = pwmt::g_sineTable.samplePhase(smoothMix_ * 0.25f);
        cachedDryGain_ = pwmt::g_sineTable.samplePhase(0.25f - smoothMix_ * 0.25f);
    }

    void updateSwellCoeffs() {
        if (sampleRate_ <= 0.f) return;
        float riseMs = fastExpLut(params[SWELL_RISE_PARAM].getValue());
        float fallMs = fastExpLut(params[SWELL_FALL_PARAM].getValue());
        if (riseMs <= 0.f) riseMs = 1.f;
        if (fallMs <= 0.f) fallMs = 10.f;
        float riseExp = -1.f / (riseMs * 0.001f * sampleRate_);
        float fallExp = -1.f / (fallMs * 0.001f * sampleRate_);
        swellRiseCoeff_ = 1.f - fastExpLut(riseExp);
        swellFallCoeff_ = 1.f - fastExpLut(fallExp);
    }

    void initTiltCoeff() {
        // One-pole LP coefficient for Tilt EQ shelf at fc=600 Hz.
        // Bilinear approximation: a ≈ (2π*fc/fs) / (1 + 2π*fc/fs)
        // avoids std::tan — accurate to <2% for fc << fs/6.
        // Called only from init/onSampleRateChange, never per-sample.
        if (sampleRate_ <= 0.f) return;
        float wc = 2.f * 3.14159265f * 600.f / sampleRate_;
        tiltLpCoeff_ = wc / (1.f + wc);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        predelay_.init(e.sampleRate);
        fdn_.init(e.sampleRate);
        diffusion_.init(e.sampleRate);
        sampleRate_ = e.sampleRate;
        initExpLut();
        initTiltCoeff();
        updateSwellCoeffs();
        // Reset shimmer state on sample rate change.
        shimWrite_ = 0;
        shimDistA_ = float(kShimBufSize) * 0.25f;
        shimDistB_ = float(kShimBufSize) * 0.75f;
        shimDistC_ = kShimFifthGrainF * 0.25f;
        shimDistD_ = kShimFifthGrainF * 0.75f;
        std::memset(shimBuf_, 0, sizeof(shimBuf_));
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        predelay_.reset();
        fdn_.reset();
        diffusion_.reset();
        smoothPredelay_  = 20.f;
        smoothSize_      = 1.f;
        smoothDecay_     = 2.f;
        smoothHiDamp_    = 8000.f;
        smoothLoDamp_    = 100.f;
        smoothLofi_      = 0.f;
        smoothMix_       = 0.5f;
        smoothDiffusion_ = 0.5f;
        smoothMod_       = 0.1f;
        smoothTilt_      = 0.f;
        tiltLpState_[0]  = 0.f;
        tiltLpState_[1]  = 0.f;
        controlCounter_  = 0;
        swellEnv_        = 0.f;
        shimWrite_      = 0;
        shimDistA_      = float(kShimBufSize) * 0.25f;
        shimDistB_      = float(kShimBufSize) * 0.75f;
        shimDistC_      = kShimFifthGrainF * 0.25f;
        shimDistD_      = kShimFifthGrainF * 0.75f;
        std::memset(shimBuf_, 0, sizeof(shimBuf_));
        freezeActive_   = false;
        freezeBlend_    = 0.f;
        freezeBtnTrigger_.reset();
        updateMixGains();
        updateSwellCoeffs();
    }

    void process(const ProcessArgs& args) override {
        // --- Read inputs (right normalled to left) ---
        float inL = inputs[IN_L_INPUT].getVoltage();
        float inR = inputs[IN_R_INPUT].isConnected()
                        ? inputs[IN_R_INPUT].getVoltage()
                        : inL;

        // Scale to [-1, 1] range (VCV audio is ±5V)
        constexpr float kInputScale  = 0.2f;   // 1/5
        constexpr float kOutputScale = 5.0f;
        inL *= kInputScale;
        inR *= kInputScale;

        // --- FREEZE toggle ---
        // Button: momentary press latches freezeActive_.
        // Gate CV: level-sensitive — high (≥1V) holds freeze regardless of latch.
        // effectiveFreeze = latched OR gate-held.
        if (freezeBtnTrigger_.process(params[FREEZE_BTN_PARAM].getValue() > 0.5f))
            freezeActive_ = !freezeActive_;
        bool gateFreeze = inputs[FREEZE_GATE_INPUT].isConnected() &&
                          inputs[FREEZE_GATE_INPUT].getVoltage() >= 1.f;
        bool effectiveFreeze = freezeActive_ || gateFreeze;
        lights[FREEZE_LIGHT].setBrightness(effectiveFreeze ? 1.f : 0.f);

        // Audio-rate FDN freeze (avoids up-to-15-sample lag from control-rate block).
        fdn_.setFreeze(effectiveFreeze);

        // Smooth blend ramp: ~5 ms at 48 kHz (coeff 0.004 → τ ≈ 250 samples).
        // Eliminates click on freeze entry/exit.
        float targetBlend = effectiveFreeze ? 1.f : 0.f;
        freezeBlend_ += (targetBlend - freezeBlend_) * 0.004f;

        // Save dry signal — always live regardless of freeze state.
        float dryL = inL;
        float dryR = inR;

        // Scale reverb injection by (1 - freezeBlend_):
        //   • Fully frozen → new signal contribution drops to zero (no new energy).
        //   • Transition → smooth cross-fade in/out of frozen tail.
        //   • Side effect: during freeze new notes can still layer over the pad
        //     if freezeBlend_ < 1 (natural blend while ramp is in progress).
        inL *= (1.f - freezeBlend_);
        inR *= (1.f - freezeBlend_);

        // --- Control-rate parameter smoothing ---
        if (++controlCounter_ >= kControlRate) {
            controlCounter_ = 0;

            // Smoothing coefficient — per control-rate tick
            constexpr float kAlpha = 0.15f;

            // Square-root curve: knob stores 0–250 linearly; squaring it then
            // scaling by 1/250 gives 0–250 ms actual delay with ~4× finer
            // resolution in the Haas zone (0–40 ms = bottom 40% of knob travel).
            float _preRaw     = params[PREDELAY_PARAM].getValue();
            float pPredelay   = _preRaw * _preRaw / 250.f;
            float pSize       = params[SIZE_PARAM].getValue();
            // Decay, HI/LO damp stored in log-space — apply exp via LUT
            // to avoid transcendentals inside the audio callback.
            float pDecay      = fastExpLut(params[DECAY_PARAM].getValue());
            float pHiDamp     = fastExpLut(params[HI_DAMP_PARAM].getValue());
            float pLoDamp     = fastExpLut(params[LO_DAMP_PARAM].getValue());
            float pLofi       = params[LOFI_PARAM].getValue();
            float pMix        = params[MIX_PARAM].getValue();
            float pDiffusion  = params[DIFFUSION_PARAM].getValue();
            float pMod        = params[MOD_PARAM].getValue();
            float pTilt       = params[TILT_PARAM].getValue();

            smoothPredelay_  += kAlpha * (pPredelay  - smoothPredelay_);
            smoothSize_      += kAlpha * (pSize       - smoothSize_);
            smoothDecay_     += kAlpha * (pDecay      - smoothDecay_);
            smoothHiDamp_    += kAlpha * (pHiDamp     - smoothHiDamp_);
            smoothLoDamp_    += kAlpha * (pLoDamp     - smoothLoDamp_);
            smoothLofi_      += kAlpha * (pLofi       - smoothLofi_);
            smoothMix_       += kAlpha * (pMix        - smoothMix_);
            smoothDiffusion_ += kAlpha * (pDiffusion  - smoothDiffusion_);
            smoothMod_       += kAlpha * (pMod        - smoothMod_);
            smoothTilt_      += kAlpha * (pTilt       - smoothTilt_);
            updateMixGains();

            // Update DSP coefficients at control rate.
            // Apply CV offsets here so they are included in setParams().
            // SIZE: 0-10V adds 0 to +1.5 (full knob span is 0.5-2.0)
            // DECAY: 0-10V adds 0 to +18 s (clamped to 20 s max)
            float cvSize  = inputs[SIZE_CV_INPUT].isConnected()
                                ? inputs[SIZE_CV_INPUT].getVoltage() * 0.15f
                                : 0.f;
            float cvDecay = inputs[DECAY_CV_INPUT].isConnected()
                                ? inputs[DECAY_CV_INPUT].getVoltage() * 1.8f
                                : 0.f;
            // DIFF CV: 0-10V adds 0-1
            float cvDiff  = inputs[DIFF_CV_INPUT].isConnected()
                                ? inputs[DIFF_CV_INPUT].getVoltage() * 0.1f
                                : 0.f;
            // TILT CV: ±5V maps to ±1 (bipolar)
            float cvTilt  = inputs[TILT_CV_INPUT].isConnected()
                                ? inputs[TILT_CV_INPUT].getVoltage() * 0.2f
                                : 0.f;
            // MOD CV: 0-10V adds 0-1
            float cvMod   = inputs[MOD_CV_INPUT].isConnected()
                                ? inputs[MOD_CV_INPUT].getVoltage() * 0.1f
                                : 0.f;

            float finalSize      = clamp(smoothSize_      + cvSize,  0.5f, 2.0f);
            float finalDecay     = clamp(smoothDecay_     + cvDecay, 0.05f, 20.f);
            float finalDiffusion = clamp(smoothDiffusion_ + cvDiff,  0.f, 1.f);
            float finalTilt      = clamp(smoothTilt_      + cvTilt, -1.f, 1.f);
            float finalMod       = clamp(smoothMod_       + cvMod,   0.f, 1.f);

            // Tilt EQ: cache hi/lo gains derived from smoothed tilt at control rate.
            // hiGain in [-0.5, +0.5], loGain is the inverse.
            // These are applied per-sample below as simple gain scalars on shelf bands.
            // Store final values directly in smoothed members for per-sample access.
            smoothTilt_ = finalTilt;   // overwrite with CV-summed+clamped value
            smoothDiffusion_ = finalDiffusion;
            smoothMod_       = finalMod;

            predelay_.setDelayMs(smoothPredelay_);
            fdn_.setParams(finalSize, finalDecay, smoothHiDamp_, smoothLoDamp_);
            // fdn_.setFreeze() is now called at audio-rate above; skip here.

            // SWELL: recompute AR coefficients at control rate.
            updateSwellCoeffs();
        }

        // --- Determine effective LO-FI depth from MODE switch ---
        // LOFI_CV_INPUT adds to the knob (0–10V maps to 0–1 additional depth).
        float cvLofi = inputs[LOFI_CV_INPUT].isConnected()
                           ? clamp(inputs[LOFI_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f)
                           : 0.f;
        float rawLofi = clamp(smoothLofi_ + cvLofi, 0.f, 1.f);

        // WARM:    70% of depth — strong saturation + noticeable pitch sway
        // DEGRADE: 100% of depth — fully extreme at max setting
        int mode = static_cast<int>(params[MODE_PARAM].getValue());
        float lofiDepth;
        switch (mode) {
            case MODE_CLEAN:   lofiDepth = 0.f;              break;
            case MODE_WARM:    lofiDepth = rawLofi * 0.7f;   break;
            case MODE_DEGRADE: lofiDepth = rawLofi;           break;
            default:           lofiDepth = rawLofi * 0.7f;   break;
        }

        // --- SHIMMER inject ---
        // Two layers feed back into inL/inR before predelay+FDN:
        //   Octave (A/B, decrement 1.0/sample → ×2 pitch, +12 st)
        //   Fifth  (C/D, decrement 0.5/sample → ×1.5 pitch, +7 st)
        // Both layers use dual-head Hann crossfade: wA+wB = wC+wD = 1.
        //
        // Stereo spread: octave shifts R earlier (−spread), fifth later
        // (+spread) → intervals sit at different stereo positions.
        //
        // Signal chain: read → blend → one-pole LP → x/(1+|x|) → inject.
        // LP darkens the feedback path per-pass (tape-style); soft sat
        // prevents runaway and adds trace even harmonics.
        bool shimmerOn = params[SHIMMER_PARAM].getValue() > 0.5f;
        float shimL = 0.f, shimR = 0.f;   // computed if shimmerOn, injected after predelay
        if (shimmerOn) {
            const float kShimBufF    = float(kShimBufSize);
            const float kShimOctGain = 0.45f;  // octave feedback
            const float kShimFifGain = 0.28f;  // fifth feedback

            // ── Octave layer (heads A & B) ──────────────────────────────
            float phaseA = shimDistA_ * (1.f / kShimBufF);
            float phaseB = shimDistB_ * (1.f / kShimBufF);
            float sA = pwmt::g_sineTable.samplePhase(phaseA * 0.5f);
            float sB = pwmt::g_sineTable.samplePhase(phaseB * 0.5f);
            float wA = sA * sA, wB = sB * sB;

            // L: nominal; R: shifted earlier by kShimStereoSpread.
            // +2×kShimBufF keeps values positive even when shimWrite_ is small.
            float absRA_L = float(shimWrite_) - shimDistA_ + kShimBufF;
            float absRA_R = float(shimWrite_) - shimDistA_ + 2.f * kShimBufF - kShimStereoSpread;
            float absRB_L = float(shimWrite_) - shimDistB_ + kShimBufF;
            float absRB_R = float(shimWrite_) - shimDistB_ + 2.f * kShimBufF - kShimStereoSpread;

            int   iA0_L = static_cast<int>(absRA_L) & kShimBufMask;
            float fA_L  = absRA_L - float(static_cast<int>(absRA_L));
            int   iA0_R = static_cast<int>(absRA_R) & kShimBufMask;
            float fA_R  = absRA_R - float(static_cast<int>(absRA_R));
            int   iB0_L = static_cast<int>(absRB_L) & kShimBufMask;
            float fB_L  = absRB_L - float(static_cast<int>(absRB_L));
            int   iB0_R = static_cast<int>(absRB_R) & kShimBufMask;
            float fB_R  = absRB_R - float(static_cast<int>(absRB_R));

            float shimL_oct =
                (shimBuf_[0][iA0_L] + fA_L * (shimBuf_[0][(iA0_L+1)&kShimBufMask] - shimBuf_[0][iA0_L])) * wA
              + (shimBuf_[0][iB0_L] + fB_L * (shimBuf_[0][(iB0_L+1)&kShimBufMask] - shimBuf_[0][iB0_L])) * wB;
            float shimR_oct =
                (shimBuf_[1][iA0_R] + fA_R * (shimBuf_[1][(iA0_R+1)&kShimBufMask] - shimBuf_[1][iA0_R])) * wA
              + (shimBuf_[1][iB0_R] + fB_R * (shimBuf_[1][(iB0_R+1)&kShimBufMask] - shimBuf_[1][iB0_R])) * wB;

            // ── Fifth layer (heads C & D) ────────────────────────────────
            // Grain size 4096, decrement 0.5 → grain period 8192 samples.
            // R spread opposite polarity to octave: shifts R later (+spread).
            float phaseC = shimDistC_ * (1.f / kShimFifthGrainF);
            float phaseD = shimDistD_ * (1.f / kShimFifthGrainF);
            float sC = pwmt::g_sineTable.samplePhase(phaseC * 0.5f);
            float sD = pwmt::g_sineTable.samplePhase(phaseD * 0.5f);
            float wC = sC * sC, wD = sD * sD;

            float absRC_L = float(shimWrite_) - shimDistC_ + kShimBufF;
            float absRC_R = absRC_L + kShimStereoSpread;
            float absRD_L = float(shimWrite_) - shimDistD_ + kShimBufF;
            float absRD_R = absRD_L + kShimStereoSpread;

            int   iC0_L = static_cast<int>(absRC_L) & kShimBufMask;
            float fC_L  = absRC_L - float(static_cast<int>(absRC_L));
            int   iC0_R = static_cast<int>(absRC_R) & kShimBufMask;
            float fC_R  = absRC_R - float(static_cast<int>(absRC_R));
            int   iD0_L = static_cast<int>(absRD_L) & kShimBufMask;
            float fD_L  = absRD_L - float(static_cast<int>(absRD_L));
            int   iD0_R = static_cast<int>(absRD_R) & kShimBufMask;
            float fD_R  = absRD_R - float(static_cast<int>(absRD_R));

            float shimL_fif =
                (shimBuf_[0][iC0_L] + fC_L * (shimBuf_[0][(iC0_L+1)&kShimBufMask] - shimBuf_[0][iC0_L])) * wC
              + (shimBuf_[0][iD0_L] + fD_L * (shimBuf_[0][(iD0_L+1)&kShimBufMask] - shimBuf_[0][iD0_L])) * wD;
            float shimR_fif =
                (shimBuf_[1][iC0_R] + fC_R * (shimBuf_[1][(iC0_R+1)&kShimBufMask] - shimBuf_[1][iC0_R])) * wC
              + (shimBuf_[1][iD0_R] + fD_R * (shimBuf_[1][(iD0_R+1)&kShimBufMask] - shimBuf_[1][iD0_R])) * wD;

            // ── Blend, saturate (injection moved to after predelay) ─────────────
            shimL = shimL_oct * kShimOctGain + shimL_fif * kShimFifGain;
            shimR = shimR_oct * kShimOctGain + shimR_fif * kShimFifGain;

            // Note: HF buildup is controlled by the FDN HI_DAMP knob, which
            // already applies a per-pass LP on every feedback loop. Adding a
            // second LP here was double-attenuating the shimmer's bright
            // character and making it inaudible. Removed.

            // Soft saturation x/(1+|x|): transparent at low levels (<5% error
            // for |x|<0.1), hard ceiling prevents feedback runaway.
            shimL = shimL / (1.f + (shimL >= 0.f ? shimL : -shimL));
            shimR = shimR / (1.f + (shimR >= 0.f ? shimR : -shimR));
        }

        // --- Predelay (first — creates a clean direct/reverb onset gap) ---
        float preL, preR;
        predelay_.process(inL, inR, &preL, &preR);

        // --- Shimmer injection (after predelay: round-trip loop excludes the gap) ---
        // Scaled by (1 - freezeBlend_): when freeze is active the shimmer inject
        // fades to zero, preventing runaway feedback through the frozen FDN.
        // (The shimmer write buffer still runs so state is preserved for release.)
        if (shimmerOn) {
            float shimGate = 1.f - freezeBlend_;
            preL += shimL * shimGate;
            preR += shimR * shimGate;
        }

        // --- Diffusion (§3.6 Ebb) ---
        // Applied after predelay: smears the already-delayed signal into the FDN.
        // At amount=0 the chain is bypassed (dry pass-through);
        // at amount=1 it applies the full 4-stage allpass stack.
        diffusion_.process(preL, preR, &preL, &preR, smoothDiffusion_);

        // --- FDN ---
        // lofiDepth drives saturation (at module output), smoothMod_ drives WowFlutter.
        float wetL, wetR;
        fdn_.process(preL, preR, &wetL, &wetR, lofiDepth, smoothMod_);

        // --- Lo-fi output saturation (WARM / DEGRADE) ---
        // boost = 1 + lofiDepth * 2.5 keeps the signal in the tanh soft-knee
        // region for most levels, adding harmonic grit rather than hard-clipping.
        // At lofiDepth=1 and a typical tail of ±0.2: driven to ±0.7, tanh gives
        // 0.604 — noticeable saturation without obliterating the reverb.
        // restore > 1 adds presence/loudness back to the saturated signal.
        if (mode != MODE_CLEAN && lofiDepth > 0.f) {
            float boost   = 1.0f + lofiDepth * 2.5f;
            float restore = 1.0f + lofiDepth * 0.5f;
            // Padé [3/3] tanh — no transcendentals, safe in audio thread
            float xL = wetL * boost;
            if (xL < -3.f) xL = -3.f; else if (xL > 3.f) xL = 3.f;
            wetL = xL * (27.f + xL*xL) / (27.f + 9.f*xL*xL) * restore;
            float xR = wetR * boost;
            if (xR < -3.f) xR = -3.f; else if (xR > 3.f) xR = 3.f;
            wetR = xR * (27.f + xR*xR) / (27.f + 9.f*xR*xR) * restore;
        }

        // --- Tilt EQ (§3.8) — applied to wet signal only ---
        // One-pole LP shelf at fc=600Hz; hi band = signal minus LP.
        // hiGain in [-0.5, +0.5]; loGain = -hiGain.
        // At smoothTilt_=+1: highs boosted 50%, lows cut 50% → brighter tail.
        // At smoothTilt_=-1: highs cut,   lows boosted           → darker tail.
        // tiltLpCoeff_ is precomputed at control-rate / onSampleRateChange.
        if (tiltLpCoeff_ > 0.f && smoothTilt_ != 0.f) {
            float hiGain = smoothTilt_ * 0.5f;
            float loGain = -hiGain;
            // Left
            tiltLpState_[0] += tiltLpCoeff_ * (wetL - tiltLpState_[0]);
            float hiL = wetL - tiltLpState_[0];
            wetL = tiltLpState_[0] * (1.f + loGain) + hiL * (1.f + hiGain);
            // Right
            tiltLpState_[1] += tiltLpCoeff_ * (wetR - tiltLpState_[1]);
            float hiR = wetR - tiltLpState_[1];
            wetR = tiltLpState_[1] * (1.f + loGain) + hiR * (1.f + hiGain);
        } else if (tiltLpCoeff_ > 0.f) {
            // Keep one-pole state tracking even when tilt=0 to avoid DC
            // jump when tilt is swept from 0 to non-zero.
            tiltLpState_[0] += tiltLpCoeff_ * (wetL - tiltLpState_[0]);
            tiltLpState_[1] += tiltLpCoeff_ * (wetR - tiltLpState_[1]);
        }

        // --- SHIMMER write: store wet output into circular buffer; advance heads.
        //     Read heads advance at 2 samples per write-head advance, net rate = 1
        //     sample closer per sample → octave up. When a head's distance reaches
        //     zero it jumps back by kShimBufSize (starts a new grain). Hann windows
        //     above suppress the click: gain is ~0 at both wrap boundaries.
        if (shimmerOn) {
            shimBuf_[0][shimWrite_ & kShimBufMask] = wetL;
            shimBuf_[1][shimWrite_ & kShimBufMask] = wetR;
            shimWrite_++;
            // Octave heads: decrement 1.0 → pitch ×2 (+12 st)
            shimDistA_ -= 1.f;
            shimDistB_ -= 1.f;
            if (shimDistA_ <= 0.f) shimDistA_ += float(kShimBufSize);
            if (shimDistB_ <= 0.f) shimDistB_ += float(kShimBufSize);
#ifndef METAMODULE
            // Fifth heads: decrement 0.5 → pitch ×1.5 (+7 st)
            shimDistC_ -= 0.5f;
            shimDistD_ -= 0.5f;
            if (shimDistC_ <= 0.f) shimDistC_ += kShimFifthGrainF;
            if (shimDistD_ <= 0.f) shimDistD_ += kShimFifthGrainF;
#endif
        }

        // --- Equal-power dry/wet mix ---
        // cos/sin crossfade: dry = cos(mix * π/2), wet = sin(mix * π/2)
        // Use LUT for both to avoid std::sin per sample.
        // Phase 0.0 = sin(0) = 0, phase 0.25 = sin(π/2) = 1.
        float outL = (cachedDryGain_ * dryL + cachedWetGain_ * wetL) * kOutputScale;
        float outR = (cachedDryGain_ * dryR + cachedWetGain_ * wetR) * kOutputScale;

        outputs[OUT_L_OUTPUT].setVoltage(outL);
        outputs[OUT_R_OUTPUT].setVoltage(outR);

        // --- SWELL envelope follower ---
        // Always tracks the live signal (dryL/dryR), not the frozen zero,
        // so SWELL OUT still reflects dynamics even during freeze.
        float monoIn   = (dryL + dryR) * 0.5f;
        float absIn    = monoIn >= 0.f ? monoIn : -monoIn;
        float coeff    = absIn > swellEnv_ ? swellRiseCoeff_ : swellFallCoeff_;
        swellEnv_     += coeff * (absIn - swellEnv_);
        // Scale: ±1 normalised peak → 10V (×10 then clamp)
        outputs[SWELL_OUT_OUTPUT].setVoltage(clamp(swellEnv_ * 10.f, 0.f, 10.f));
    }
};

// --- Widget -----------------------------------------------------------

#ifndef METAMODULE

// Panel label widget — follows MorphWorx convention (Rajdhani-Bold for labels,
// CinzelDecorative-Bold for titles, NanoVG text rendered on top of PNG panel).
struct AePanelLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool isTitle;

    AePanelLabel(Vec pos, const std::string& text, float fontSize, NVGcolor color, bool isTitle = false)
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

static AePanelLabel* aeLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
    return new AePanelLabel(mm2px(mmPos), text, fontSize, color, isTitle);
}

struct AetherionWidget : ModuleWidget {
    AetherionWidget(Aetherion* module) {
        setModule(module);

        // 12 HP panel = 60.96 mm wide
        box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);
        {
            auto* bg = new bem::PngPanelBackground(
                asset::plugin(pluginInstance, "res/Aetherion.png"));
            bg->box.pos  = Vec(0, 0);
            bg->box.size = box.size;
            addChild(bg);
        }

        // ── Colours ──
        NVGcolor labelCol = nvgRGB(0xff, 0xff, 0xff);  // white
        NVGcolor portCol  = nvgRGB(0xff, 0xff, 0xff);  // white
        NVGcolor modeCol  = nvgRGB(0xff, 0xff, 0xff);  // white

        // ── Layout constants (mm) ──
        // 12 HP = 60.96mm.  Three-column layout.
        constexpr float kPanelW  = 60.96f;
        constexpr float kCol1    = 13.0f;                // left column
        constexpr float kCentre  = kPanelW * 0.5f;       // 30.48  (centre column)
        constexpr float kCol3    = kPanelW - 13.0f;      // right column (47.96)

        // Label sits above knob centre by this offset (mm)
        constexpr float kLabelAbove = 7.5f;

        // Knob rows (mm from top) — 15 mm spacing, 3-column layout
        // kKnobShift: 20px down (20 * 25.4 / 75 ≈ 6.773 mm)
        constexpr float kKnobShift = 20.0f * 25.4f / 75.0f;
        constexpr float kRow1 = 20.0f + kKnobShift;    // PREDELAY | DIFFUSION | SIZE
        constexpr float kRow2 = 35.0f + kKnobShift;    // DECAY | MOD | HI DAMP
        constexpr float kRow3 = 50.0f + kKnobShift;    // LO DAMP | TILT | LO-FI
        constexpr float kRow4 = 65.0f + kKnobShift;    // MIX | SHIMMER | MODE

        // Port label sits above port centre
        constexpr float kPortLabelAbove = 5.84f;

        // ── Row 1: PREDELAY | DIFFUSION | SIZE ──
        addChild(aeLabel(Vec(kCol1,   kRow1 - kLabelAbove), "PREDELAY",  7.5f, labelCol));
        addChild(aeLabel(Vec(kCentre, kRow1 - kLabelAbove), "DIFFUSION", 7.5f, labelCol));
        addChild(aeLabel(Vec(kCol3,   kRow1 - kLabelAbove), "SIZE",      7.5f, labelCol));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol1,   kRow1)), module, Aetherion::PREDELAY_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCentre, kRow1)), module, Aetherion::DIFFUSION_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol3,   kRow1)), module, Aetherion::SIZE_PARAM));

        // ── Row 2: DECAY | MOD | HI DAMP ──
        addChild(aeLabel(Vec(kCol1,   kRow2 - kLabelAbove), "DECAY",   7.5f, labelCol));
        addChild(aeLabel(Vec(kCentre, kRow2 - kLabelAbove), "MOD",     7.5f, labelCol));
        addChild(aeLabel(Vec(kCol3,   kRow2 - kLabelAbove), "HI DAMP", 7.5f, labelCol));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol1,   kRow2)), module, Aetherion::DECAY_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCentre, kRow2)), module, Aetherion::MOD_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol3,   kRow2)), module, Aetherion::HI_DAMP_PARAM));

        // ── Row 3: LO DAMP | TILT | LO-FI ──
        addChild(aeLabel(Vec(kCol1,   kRow3 - kLabelAbove), "LO DAMP", 7.5f, labelCol));
        addChild(aeLabel(Vec(kCentre, kRow3 - kLabelAbove), "TILT",    7.5f, labelCol));
        addChild(aeLabel(Vec(kCol3,   kRow3 - kLabelAbove), "LO-FI",   7.5f, labelCol));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol1,   kRow3)), module, Aetherion::LO_DAMP_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCentre, kRow3)), module, Aetherion::TILT_PARAM));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol3,   kRow3)), module, Aetherion::LOFI_PARAM));

        // ── Row 4: MIX | SHIMMER toggle | MODE switch ──
        addChild(aeLabel(Vec(kCol1,   kRow4 - kLabelAbove),        "MIX",     7.5f, labelCol));
        addChild(aeLabel(Vec(kCentre, kRow4 - 5.34f),              "SHIMMER", 5.5f, labelCol));
        addChild(aeLabel(Vec(kCol3,   kRow4 - kLabelAbove - 0.66f),"MODE",    7.5f, modeCol));
        addParam(createParamCentered<MVXKnob_wh>(
            mm2px(Vec(kCol1,   kRow4)), module, Aetherion::MIX_PARAM));
        addParam(createParamCentered<CKSS>(
            mm2px(Vec(kCentre, kRow4)), module, Aetherion::SHIMMER_PARAM));
        addParam(createParamCentered<CKSSThree>(
            mm2px(Vec(kCol3,   kRow4)), module, Aetherion::MODE_PARAM));

        // ── Ports ──
        // 4 evenly-spaced across 12 HP: spacing = kPanelW / 5
        constexpr float kPortSpacing = kPanelW / 5.f;  // ~12.19 mm
        constexpr float kPortRow     = 121.0f;
        constexpr float kPort1 = kPortSpacing;          // ~12.19
        constexpr float kPort2 = kPortSpacing * 2.f;    // ~24.38
        constexpr float kPort3 = kPortSpacing * 3.f;    // ~36.58
        constexpr float kPort4 = kPortSpacing * 4.f;    // ~48.77

        addChild(aeLabel(Vec(kPort1, kPortRow - kPortLabelAbove), "IN L",  5.5f, portCol));
        addChild(aeLabel(Vec(kPort2, kPortRow - kPortLabelAbove), "IN R",  5.5f, portCol));
        addChild(aeLabel(Vec(kPort3, kPortRow - kPortLabelAbove), "OUT L", 5.5f, portCol));
        addChild(aeLabel(Vec(kPort4, kPortRow - kPortLabelAbove), "OUT R", 5.5f, portCol));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kPort1, kPortRow)), module, Aetherion::IN_L_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kPort2, kPortRow)), module, Aetherion::IN_R_INPUT));
        addOutput(createOutputCentered<MVXport_s1_red>(
            mm2px(Vec(kPort3, kPortRow)), module, Aetherion::OUT_L_OUTPUT));
        addOutput(createOutputCentered<MVXport_s1_red>(
            mm2px(Vec(kPort4, kPortRow)), module, Aetherion::OUT_R_OUTPUT));

        // ── SWELL section ──
        constexpr float kSwellRow        = 79.0f + kKnobShift;
        constexpr float kSwellLabel      = 72.0f + kKnobShift;
        constexpr float kSwellLabelAbove = 5.34f;
        NVGcolor swellCol = nvgRGB(0xff, 0xff, 0xff);
        addChild(aeLabel(Vec(kCentre, kSwellLabel), "SWELL", 6.5f, swellCol, false));

        addChild(aeLabel(Vec(kPort1, kSwellRow - kSwellLabelAbove), "FREEZE", 5.5f, swellCol));
        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(kPort1, kSwellRow)), module, Aetherion::FREEZE_BTN_PARAM));
        addChild(createLightCentered<TinyLight<GreenLight>>(
            mm2px(Vec(kPort1 + 3.5f, kSwellRow - 3.5f)), module, Aetherion::FREEZE_LIGHT));

        addChild(aeLabel(Vec(kPort2, kSwellRow - kSwellLabelAbove), "RISE", 5.5f, swellCol));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(kPort2, kSwellRow)), module, Aetherion::SWELL_RISE_PARAM));

        addChild(aeLabel(Vec(kPort3, kSwellRow - kSwellLabelAbove), "OUT", 5.5f, swellCol));
        addOutput(createOutputCentered<MVXport_s1_red>(
            mm2px(Vec(kPort3, kSwellRow)), module, Aetherion::SWELL_OUT_OUTPUT));

        addChild(aeLabel(Vec(kPort4, kSwellRow - kSwellLabelAbove), "FALL", 5.5f, swellCol));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(kPort4, kSwellRow)), module, Aetherion::SWELL_FALL_PARAM));

        // ── CV row A (7 ports: FRZ|DCY|SZ|LOFI|DIFF|TILT|MOD) ──
        // 7 ports in 60.96mm using kPanelW/8 spacing (~7.62mm each).
        constexpr float kCvSpacing = kPanelW / 8.f;  // ~7.62 mm
        constexpr float kCvRow     = 109.0f;
        constexpr float kCvLabelAbove = 5.34f;
        constexpr float kCv1 = kCvSpacing;           // ~7.62
        constexpr float kCv2 = kCvSpacing * 2.f;     // ~15.24
        constexpr float kCv3 = kCvSpacing * 3.f;     // ~22.86
        constexpr float kCv4 = kCvSpacing * 4.f;     // ~30.48
        constexpr float kCv5 = kCvSpacing * 5.f;     // ~38.1
        constexpr float kCv6 = kCvSpacing * 6.f;     // ~45.72
        constexpr float kCv7 = kCvSpacing * 7.f;     // ~53.34

        addChild(aeLabel(Vec(kCv1, kCvRow - kCvLabelAbove), "FRZ",  4.5f, portCol));
        addChild(aeLabel(Vec(kCv2, kCvRow - kCvLabelAbove), "DCY",  4.5f, portCol));
        addChild(aeLabel(Vec(kCv3, kCvRow - kCvLabelAbove), "SZ",   4.5f, portCol));
        addChild(aeLabel(Vec(kCv4, kCvRow - kCvLabelAbove), "LFI",  4.5f, portCol));
        addChild(aeLabel(Vec(kCv5, kCvRow - kCvLabelAbove), "DIF",  4.5f, portCol));
        addChild(aeLabel(Vec(kCv6, kCvRow - kCvLabelAbove), "TLT",  4.5f, portCol));
        addChild(aeLabel(Vec(kCv7, kCvRow - kCvLabelAbove), "MOD",  4.5f, portCol));

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv1, kCvRow)), module, Aetherion::FREEZE_GATE_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv2, kCvRow)), module, Aetherion::DECAY_CV_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv3, kCvRow)), module, Aetherion::SIZE_CV_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv4, kCvRow)), module, Aetherion::LOFI_CV_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv5, kCvRow)), module, Aetherion::DIFF_CV_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv6, kCvRow)), module, Aetherion::TILT_CV_INPUT));
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCv7, kCvRow)), module, Aetherion::MOD_CV_INPUT));
    }
};

#else // METAMODULE

struct AetherionWidget : ModuleWidget {
    AetherionWidget(Aetherion* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Aetherion.png")));

        // Matches kKnobShift in the VCV build: 20 px down = 20 × 25.4/75 mm.
        // MetaModule uses widget positions for hardware knob/CV-input mapping.
        constexpr float kKnobShift = 20.f * 25.4f / 75.f;

        // Row 1: PREDELAY | DIFFUSION | SIZE
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(10.16f, 20.f + kKnobShift)), module, Aetherion::PREDELAY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(25.4f,  20.f + kKnobShift)), module, Aetherion::DIFFUSION_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(40.64f, 20.f + kKnobShift)), module, Aetherion::SIZE_PARAM));
        // Row 2: DECAY | MOD | HI DAMP
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(10.16f, 35.f + kKnobShift)), module, Aetherion::DECAY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(25.4f,  35.f + kKnobShift)), module, Aetherion::MOD_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(40.64f, 35.f + kKnobShift)), module, Aetherion::HI_DAMP_PARAM));
        // Row 3: LO DAMP | TILT | LO-FI
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(10.16f, 50.f + kKnobShift)), module, Aetherion::LO_DAMP_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(25.4f,  50.f + kKnobShift)), module, Aetherion::TILT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(40.64f, 50.f + kKnobShift)), module, Aetherion::LOFI_PARAM));
        // Row 4: MIX | SHIMMER | MODE
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(10.16f, 65.f + kKnobShift)), module, Aetherion::MIX_PARAM));
        addParam(createParamCentered<CKSS>(
            mm2px(Vec(25.4f,  65.f + kKnobShift)), module, Aetherion::SHIMMER_PARAM));
        addParam(createParamCentered<CKSSThree>(
            mm2px(Vec(40.64f, 65.f + kKnobShift)), module, Aetherion::MODE_PARAM));
        // SWELL
        addParam(createParamCentered<VCVButton>(
            mm2px(Vec(10.16f, 79.f + kKnobShift)), module, Aetherion::FREEZE_BTN_PARAM));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(22.86f, 79.f + kKnobShift)), module, Aetherion::SWELL_RISE_PARAM));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(40.64f, 79.f + kKnobShift)), module, Aetherion::SWELL_FALL_PARAM));

        // CV row (unchanged — not shifted)
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(7.62f,  109.f)), module, Aetherion::FREEZE_GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(15.24f, 109.f)), module, Aetherion::DECAY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(22.86f, 109.f)), module, Aetherion::SIZE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(30.48f, 109.f)), module, Aetherion::LOFI_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(38.1f,  109.f)), module, Aetherion::DIFF_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(45.72f, 109.f)), module, Aetherion::TILT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(53.34f, 109.f)), module, Aetherion::MOD_CV_INPUT));
        // Outputs
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(30.48f, 79.f + kKnobShift)), module, Aetherion::SWELL_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(36.58f, 121.f)), module, Aetherion::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(48.77f, 121.f)), module, Aetherion::OUT_R_OUTPUT));
        // Audio inputs (unchanged)
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(12.19f, 121.f)), module, Aetherion::IN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(24.38f, 121.f)), module, Aetherion::IN_R_INPUT));
    }
};

#endif // METAMODULE

Model* modelAetherion = createModel<Aetherion, AetherionWidget>("Aetherion");
