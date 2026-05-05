// XORnado — 4-channel bytebeat / digital noise generator
// Ported from O+C Phazerville "Viznutcracker, sweet!" applet
// Original equations: Tim Churches, Mutable Instruments, Microbe Modular, BitWiz
// VCV Rack 2 + 4ms MetaModule port: MorphWorx
//
// License: MIT (original bytebeat equations) / GPL-3.0-or-later (VCV Rack integration)
// SPDX-License-Identifier: MIT AND GPL-3.0-or-later

#include "plugin.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Bytebeat equation evaluator
// All arithmetic is unsigned 32-bit integer — zero transcendentals, ARM-safe.
// Division-by-zero guards applied to every variable-divisor operation.
// last_sample is the feedback register (previous output), used by eqns 9,10,13,15.
// ─────────────────────────────────────────────────────────────────────────────

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"

static inline uint8_t evaluateBytebeat(
    uint8_t eq, uint32_t t,
    uint8_t p0, uint8_t p1, uint8_t p2,
    uint8_t pitch, uint16_t last_sample)
{
    // Safe-divisor helpers — prevent UB from division by zero
    auto sd  = [](uint32_t v) -> uint32_t { return v == 0 ? 1u : v; };
    auto sd8 = [](uint8_t  v) -> uint8_t  { return v == 0 ? 1u : v; };
    (void)sd8;

    const uint32_t tp = (uint32_t)t * pitch;   // t * pitch (used in most eqns)
    uint32_t s = 0;

    switch (eq) {
        case 0: // hope — atmospheric, hopeful
            s = (((tp*3) & (t>>10)) | ((tp*p0) & (t>>10)) | ((t*10) & ((t>>8)*p1) & p2));
            break;
        case 1: // love — stephth
            s = ((((tp*p0) & (t>>4)) | ((t*p2) & (t>>7)) | ((t*p1) & (t>>10))));
            break;
        case 2: // life — xifeng
            s = (((((((tp>>p0)|(tp))|((tp)>>p0))*p2)&((5*tp)|((tp)>>p2)))|((tp)^(t%sd(p1)))));
            break;
        case 3: // age — Arp rotator (pitch disabled, uses raw t)
            s = ((t>>(p2>>4))&((t)<<3)/sd((t)*p1*((t)>>11)%sd(3+(((t)>>(16-(p0>>4)))%22))));
            break;
        case 4: { // Morph — XOR dual oscillator + sawtooth floor
            // p0|3 and (p1>>1)|5 are floored so factor never zeros either osc
            // +c is an always-present sawtooth baseline — structurally never silent
            uint32_t a = (tp * (uint32_t)(p0 | 3u)) >> 5;
            uint32_t b = (tp * (uint32_t)((p1 >> 1) | 5u)) >> 5;
            uint32_t c = (tp * (uint32_t)(p2 | 1u)) >> 8;
            s = ((a ^ b) + c) & 255u;
            break;
        }
        case 5: // monk — Vocaliser
            s = (((tp%sd(p0))>>2)&p1)*(t>>sd(p2>>5));
            break;
        case 6: // NERV — Chewie
            s = (p0-(((p2+1)/sd(tp))^p0|(tp)^922+p0))*(p2+1)/sd(p0)*(((tp)+p1)>>sd(p1%19));
            break;
        case 7: // Trurl — Tinbot
            s = ((tp)/sd(40+p0)*((tp)+(tp)|4-(p1+20)))+((tp)*(p2>>5));
            break;
        case 8: // Pirx — My Loud Friend
            s = (((tp>>((p0>>12)%12))%sd(t>>((p1%12)+1))-(t>>((t>>(p2%10))%12)))/sd((t>>((p0>>2)%15))%15))<<4;
            break;
        case 9: // Snaut — GGT2 (uses feedback)
            s = ((tp)+(uint32_t)last_sample+p1/sd(p0))%sd(p0|(tp)+p2);
            break;
        case 10: // Hari — The Signs (uses feedback)
            s = ((0&(251&((tp)/sd(100+p0))))|(((uint32_t)last_sample/sd(tp)|((tp)/sd(100*(p1+1))))*((tp)|p2)));
            break;
        case 11: // Kris — Light Reactor
            s = (((tp)>>3)*(p0-643|(325%sd(t)|p1)&t)-((t>>6)*35/sd(p2)%sd(t)))>>6;
            break;
        case 12: // Tide — parametric Viznut melodic cascade
            // Parametric t*(t>>5|t>>8) — the most melodic bytebeat formula
            // | 1u guarantees factor >= 1: impossible to silence
            s = tp * ((tp >> (2u + (p0 >> 5))) | (tp >> (5u + (p1 >> 6))) | 1u) >> (1u + (p2 >> 5));
            break;
        case 13: // Bregg — Hooks (uses feedback)
            s = ((tp)&(p0+2))-(t/sd(p1))/sd((uint32_t)last_sample)/sd(p2);
            break;
        case 14: // Avon — Widerange
            s = (((p0^((tp)>>(p1>>3)))-(t>>(p2>>2))-t%sd(t&p1)));
            break;
        case 15: // Orac — Abducted (uses feedback)
            s = (p0+(tp)>>p1%12)|((((uint32_t)last_sample%sd(p0+(tp)>>p0%4))+11+p2)^t)>>(p2>>12);
            break;
        case 16: { // Mantis — cross-modulated XOR self-modulation
            // XOR of t with t*k creates dense inharmonic difference tones
            // Multiplying two such terms: extremely complex non-linear harmonics
            uint32_t x = tp ^ (tp * (uint32_t)(p0 | 3u));
            uint32_t y = (tp ^ (tp * (uint32_t)(p1 | 5u))) >> 4;
            s = (x * (y | 1u)) >> (3u + (p2 >> 5));
            break;
        }
        case 17: // Kernal — SID pulse wave with PWM (p0=pitch, p1=duty cycle)
            s = (((tp * (p0 + 1u)) >> 8) & 255u) < (uint32_t)p1 ? 255u : 0u;
            break;
        case 18: { // Warp — slow LFO AM multiplication + XOR colour
            // t>>(8+shift) is a slow bit-counter → creates evolving AM texture
            // | p1 | 3u floors lfo >= 3: structurally never silent
            uint32_t lfo    = (t >> (8u + (p0 >> 5))) | (uint32_t)p1 | 3u;
            uint32_t xorcol = tp >> (p2 >> 5);
            s = ((tp * lfo) ^ xorcol) * 7u >> 4;
            break;
        }
        case 19: { // Phase — self-product with slow harmonic cycling
            // shift (1-8) derived from upper bits of t → 8 distinct tonal characters evolve
            // tp * ((tp>>shift)|1u): odd floor guarantees non-zero once t>0
            // XOR terms add two independent frequency layers
            uint32_t shift = ((t >> (7u + (p0 >> 5))) & 7u) + 1u;
            uint32_t mix   = tp * ((tp >> shift) | 1u);
            s = (mix ^ (tp >> (p1 >> 4)) ^ (t >> (p2 >> 4))) >> 4;
            break;
        }
        case 20: { // Ritual — dual non-power-of-2 modulo sawtooth beating
            // Modulo with odd-floored periods: complex quasi-periodic beating
            // p0|7 >= 7, p1|11 >= 11: never zero — safe without sd()
            // v1*v2: sum + difference tones; XOR tail adds high-frequency detail
            uint32_t v1 = tp % (uint32_t)(p0 | 7u);
            uint32_t v2 = tp % (uint32_t)(p1 | 11u);
            s = (v1 * v2 ^ (tp >> (p2 >> 4))) & 255u;
            break;
        }
        case 21: { // Vortex — XOR oscillator pair with OR baseline floor
            // XOR of two sawtooths: rich sidebands when p0 != p1
            // | (tp>>shift): sawtooth floor survives even when XOR cancels
            uint32_t xr = (tp * (uint32_t)(p0 + 1u)) ^ (tp * (uint32_t)(p1 + 1u));
            s = (xr | (tp >> (p2 >> 4))) >> 2;
            break;
        }
        case 22: { // Helix — two sawtooth oscillators XOR'd (p0=osc1 freq, p1=osc2 freq, p2=bit mask)
            uint32_t o1 = (tp * (uint32_t)(p0 + 1u)) >> 8;
            uint32_t o2 = (tp * (uint32_t)(p1 + 1u)) >> 8;
            s = (o1 ^ o2) & (uint32_t)p2;
            break;
        }
        case 23: // Grieg — parametric chiptune (p0=upper shift, p1=mid shift, p2=low shift)
            s = ((tp >> (p0 >> 4) | tp | tp >> (3u + (p1 >> 5))) * 10u + 4u * (tp & (tp >> 13) | tp >> (p2 >> 4))) & 255u;
            break;
        default:
            s = 0;
            break;
    }
    return (uint8_t)(s & 0xFF);
}

#pragma GCC diagnostic pop

// ─────────────────────────────────────────────────────────────────────────────
// XORnado Module
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int kNumChannels = 1;
static constexpr int kNumEquations = 24;

struct XORnado : Module {

    // ── Enums ─────────────────────────────────────────────────────────────────
    enum ParamId {
        EQ_1,     // Equation (0–23, snap)
        SPEED_1,  // Speed (0–255)
        PITCH_1,  // Pitch multiplier (1–8, snap)
        LOOP_1,   // Loop length (snap: Free/16/32/.../4096)
        P0_1,     // P0 (0–255)
        P1_1,     // P1 (0–255)
        P2_1,     // P2 (0–255)
        VOL_1,       // Volume (0.0–2.0)
        PM_DEPTH_1,      // Phase/FM mutation depth (0–255)
        MUTATION_MODE_1, // Mutation mode switch (0=PM, 1=FM)
        PARAMS_LEN
    };

    enum InputId {
        TRIG_1,
        EQ_CV_1,
        SPEED_CV_1,
        PITCH_CV_1,
        LOOP_CV_1,
        P0_CV_1,
        P1_CV_1,
        P2_CV_1,
        VOL_CV_1,
        PM_CV_1,        // PM modulation source (self-feedback when unconnected)
        PM_DEPTH_CV_1,  // CV scales PM depth knob
        INPUTS_LEN
    };

    enum OutputId {
        OUT_1,      // mono crossfade blend
        OUT_L_1,    // left  = equation A (floor)
        OUT_R_1,    // right = equation B (ceiling)
        OUTPUTS_LEN
    };

    enum LightId {
        STEP_LIGHT_1,
        LIGHTS_LEN
    };

    // ── Per-channel state (fixed-size, no heap allocation) ───────────────────
    struct Channel {
        uint32_t t          = 0;      // phase accumulator (the "t" in bytebeat equations)
        uint32_t phase_acc  = 0;      // sub-sample accumulator for speed scaling
        uint16_t last_sample = 13;    // feedback register for equations 9,10,13,15
        bool     step_mode = false;  // true = clock steps t; false = clock resets t
        bool     bipolar   = false;  // output roughly ±10V instead of 0–10V
        dsp::SchmittTrigger trigIn;
        dsp::PulseGenerator stepLight;
    };

    Channel channels[kNumChannels];

    // Speed scaling: phase_acc threshold such that at speed=255, t advances
    // at ~16,666 Hz. Recomputed on sample rate change.
    float phaseThreshold = 1.0f;   // computed in onSampleRateChange()

    // ── Constructor ───────────────────────────────────────────────────────────
    XORnado() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        for (int ch = 0; ch < kNumChannels; ch++) {
            std::string n = std::to_string(ch + 1);

            configParam(EQ_1 + ch, 0.f, 23.f, 0.f, "Equation Morph");
            configParam(SPEED_1 + ch, 0.f, 255.f, 128.f, "Speed");
            configParam(PITCH_1 + ch, 1.f, 8.f,   1.f,   "Pitch", "×");
            configSwitch(LOOP_1 + ch, 0.f, 9.f,   0.f,   "Loop Length", {
                "Free", "16", "32", "64", "128", "256", "512", "1024", "2048", "4096"
            });
            configParam(P0_1    + ch, 0.f, 255.f, 127.f, "P0");
            configParam(P1_1    + ch, 0.f, 255.f, 127.f, "P1");
            configParam(P2_1    + ch, 0.f, 255.f, 127.f, "P2");
            configParam(VOL_1      + ch, 0.f,   2.f, 1.f, "Volume");
            configParam(PM_DEPTH_1 + ch, 0.f, 255.f, 0.f, "PM Depth");
            configSwitch(MUTATION_MODE_1 + ch, 0.f, 1.f, 0.f, "Mutation Mode", {"Phase Mutation (PM)", "Frequency Warp (FM)"});

            configInput(TRIG_1         + ch, "Trigger/Clock");
            configInput(EQ_CV_1     + ch, "Equation CV");
            configInput(SPEED_CV_1  + ch, "Speed CV");
            configInput(PITCH_CV_1  + ch, "Pitch CV");
            configInput(LOOP_CV_1   + ch, "Loop CV");
            configInput(P0_CV_1     + ch, "P0 CV");
            configInput(P1_CV_1     + ch, "P1 CV");
            configInput(P2_CV_1     + ch, "P2 CV");
            configInput(VOL_CV_1       + ch, "Volume CV");
            configInput(PM_CV_1        + ch, "PM modulation source");
            configInput(PM_DEPTH_CV_1  + ch, "PM Depth CV");

            configOutput(OUT_1   + ch, "Out (mono blend)");
            configOutput(OUT_L_1 + ch, "Out L (equation A)");
            configOutput(OUT_R_1 + ch, "Out R (equation B)");
            configLight(STEP_LIGHT_1 + ch, "Step " + n);
        }

        onSampleRateChange();
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        // speed=255 should advance t at ~16,666 Hz.
        // We use a quadratic accumulator: phase_acc += speed*speed each sample.
        // phase_acc wraps at phaseThreshold → t++.
        // At speed=255: increments/sec = sr * 255*255 / phaseThreshold = 16666
        // → phaseThreshold = sr * 255 * 255 / 16666
        phaseThreshold = sr * (255.f * 255.f) / 16666.f;
    }

    // ── State serialization ───────────────────────────────────────────────────
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* chArr = json_array();
        for (int ch = 0; ch < kNumChannels; ch++) {
            json_t* obj = json_object();
            json_object_set_new(obj, "step_mode", json_boolean(channels[ch].step_mode));
            json_object_set_new(obj, "bipolar",   json_boolean(channels[ch].bipolar));
            json_array_append_new(chArr, obj);
        }
        json_object_set_new(root, "channels", chArr);
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* chArr = json_object_get(root, "channels");
        if (!chArr) return;
        for (int ch = 0; ch < kNumChannels; ch++) {
            json_t* obj = json_array_get(chArr, ch);
            if (!obj) continue;
            json_t* sm = json_object_get(obj, "step_mode");
            if (sm) channels[ch].step_mode = json_boolean_value(sm);
            json_t* bp = json_object_get(obj, "bipolar");
            if (bp) channels[ch].bipolar = json_boolean_value(bp);
        }
    }

    // ── process() ─────────────────────────────────────────────────────────────
    void process(const ProcessArgs& args) override {
        for (int ch = 0; ch < kNumChannels; ch++) {
            Channel& C = channels[ch];

            // ── Trigger input ─────────────────────────────────────────────────
            if (C.trigIn.process(inputs[TRIG_1 + ch].getVoltage(), 0.1f, 2.f)) {
                if (C.step_mode) {
                    C.t++;
                    C.stepLight.trigger(1e-3f);
                } else {
                    C.t = 0;
                    C.phase_acc = 0;
                }
            }

            // ── PM/FM depth and signal (computed before speed acc for FM) ─────────
            float pmDepth = params[PM_DEPTH_1 + ch].getValue();
            if (inputs[PM_DEPTH_CV_1 + ch].isConnected())
                pmDepth = rack::math::clamp(pmDepth + inputs[PM_DEPTH_CV_1 + ch].getVoltage() * 25.5f, 0.f, 255.f);
            float pmSignal = 0.f;
            if (pmDepth > 0.f) {
                if (inputs[PM_CV_1 + ch].isConnected())
                    pmSignal = inputs[PM_CV_1 + ch].getVoltage() * (127.5f / 5.0f);
                else
                    pmSignal = (float)C.last_sample - 128.f;  // self-feedback: -128..+127
            }

            // ── Speed accumulator (only advances t in free-run mode) ──────────
            if (!C.step_mode) {
                float speedKnob = params[SPEED_1 + ch].getValue();
                if (inputs[SPEED_CV_1 + ch].isConnected()) {
                    speedKnob = rack::math::clamp(
                        speedKnob + inputs[SPEED_CV_1 + ch].getVoltage() * 25.5f,
                        0.f, 255.f);
                }
                float effectiveSpeedF = speedKnob;
                if (pmDepth > 0.f && params[MUTATION_MODE_1 + ch].getValue() >= 0.5f) {  // FM: modulate speed accumulator
                    float fmOffset = pmSignal * (pmDepth * (1.f / 255.f));
                    effectiveSpeedF = rack::math::clamp(speedKnob + fmOffset, 0.f, 255.f);
                }
                uint32_t speed = (uint32_t)effectiveSpeedF;
                C.phase_acc += speed * speed;
                if (C.phase_acc >= (uint32_t)phaseThreshold) {
                    C.phase_acc -= (uint32_t)phaseThreshold;
                    C.t++;
                    C.stepLight.trigger(1e-3f);
                }
            }

            // ── Loop length wrap ──────────────────────────────────────────────
            // loopIdx 0 = Free; 1–9 → 16<<(idx-1) = 16,32,64,128,256,512,1024,2048,4096
            // All loop lengths are powers of 2 → bitwise AND for zero-cost modulo
            // Only active when a clock is patched: clock is the sole looping authority.
            // Without a clock, t runs free — no automatic wrap.
            if (inputs[TRIG_1 + ch].isConnected()) {
                float loopF = params[LOOP_1].getValue();
                if (inputs[LOOP_CV_1].isConnected())
                    loopF = rack::math::clamp(loopF + inputs[LOOP_CV_1].getVoltage() * 0.9f, 0.f, 9.f);
                uint32_t loopIdx = (uint32_t)rack::math::clamp(loopF, 0.f, 9.f);
                if (loopIdx > 0u) {
                    uint32_t loopLen = 16u << (loopIdx - 1u);
                    if (C.t >= loopLen)
                        C.t &= (loopLen - 1u);
                }
            }

            // ── Parameter reads (knob + CV) ───────────────────────────────────
            auto readParam = [&](ParamId base, InputId cvBase) -> uint8_t {
                float v = params[base + ch].getValue();
                if (inputs[cvBase + ch].isConnected())
                    v = rack::math::clamp(v + inputs[cvBase + ch].getVoltage() * 25.5f, 0.f, 255.f);
                return (uint8_t)rack::math::clamp(v, 0.f, 255.f);
            };

            // Equation morph: fractional EQ value blends between two adjacent equations
            float eqF = rack::math::clamp(params[EQ_1 + ch].getValue(), 0.f, 23.f);
            if (inputs[EQ_CV_1 + ch].isConnected())
                eqF = rack::math::clamp(eqF + inputs[EQ_CV_1 + ch].getVoltage() * 2.4f, 0.f, 23.f);
            uint8_t eqA    = (uint8_t)eqF;
            uint8_t eqB    = eqA < 23u ? eqA + 1u : 23u;
            float   eqBlend = eqF - (float)eqA;

            uint8_t p0 = readParam(P0_1, P0_CV_1);
            uint8_t p1 = readParam(P1_1, P1_CV_1);
            uint8_t p2 = readParam(P2_1, P2_CV_1);

            // Pitch multiplier — live knob + CV, range 1–8
            // Changes tp = t * pitch, which alters harmonic density across all equations
            float pitchF = params[PITCH_1 + ch].getValue();
            if (inputs[PITCH_CV_1 + ch].isConnected())
                pitchF = rack::math::clamp(pitchF + inputs[PITCH_CV_1 + ch].getVoltage() * 0.7f, 1.f, 8.f);
            uint8_t pitch = (uint8_t)rack::math::clamp(pitchF, 1.f, 8.f);

            // ── Evaluate equation ─────────────────────────────────────────────
            // PM: offset t by depth-scaled pmSignal; uint32 wrap = intentional aliasing
            uint32_t t_eval = C.t;
            if (pmDepth > 0.f && params[MUTATION_MODE_1 + ch].getValue() < 0.5f) {
                int32_t offset = (int32_t)(pmSignal * (float)pmDepth) >> 3;
                t_eval = C.t + (uint32_t)offset;
            }
            uint8_t resultA = evaluateBytebeat(eqA, t_eval, p0, p1, p2, pitch, C.last_sample);
            uint8_t resultB;
            uint8_t result;
            if (eqBlend < 0.001f) {
                resultB = resultA;   // at integer EQ, L == R == mono
                result  = resultA;
            } else {
                resultB = evaluateBytebeat(eqB, t_eval, p0, p1, p2, pitch, C.last_sample);
                result  = (uint8_t)((1.0f - eqBlend) * (float)resultA + eqBlend * (float)resultB + 0.5f);
            }
            C.last_sample = result;

            // ── Output ────────────────────────────────────────────────────────
            float vol = params[VOL_1 + ch].getValue();
            if (inputs[VOL_CV_1 + ch].isConnected())
                vol = rack::math::clamp(vol + inputs[VOL_CV_1 + ch].getVoltage() * 0.2f, 0.f, 2.f);

            float out, outL, outR;
            if (C.bipolar) {
                out  = ((float)result  - 128.f) * (10.f / 127.f);
                outL = ((float)resultA - 128.f) * (10.f / 127.f);
                outR = ((float)resultB - 128.f) * (10.f / 127.f);
            } else {
                out  = (float)result  * (10.f / 255.f);
                outL = (float)resultA * (10.f / 255.f);
                outR = (float)resultB * (10.f / 255.f);
            }
            outputs[OUT_1   + ch].setVoltage(out  * vol);
            outputs[OUT_L_1 + ch].setVoltage(outL * vol);
            outputs[OUT_R_1 + ch].setVoltage(outR * vol);

            // ── Step LED ─────────────────────────────────────────────────────
            lights[STEP_LIGHT_1 + ch].setBrightness(C.stepLight.process(args.sampleTime) ? 1.f : 0.f);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

#ifndef METAMODULE

struct XORnadoWidget : ModuleWidget {
    XORnadoWidget(XORnado* module) {
        setModule(module);

        // 6HP grey panel — drawn programmatically, no SVG needed
        box.size = Vec(RACK_GRID_WIDTH * 6, RACK_GRID_HEIGHT);

        // ── Layout constants (all mm) ──────────────────────────────────────
        // 6HP = 30.48mm wide
        constexpr float kW       = 30.48f;
        constexpr float kCX      = kW * 0.5f;   // 15.24mm — panel center
        constexpr float kColL    =  7.5f;        // left knob column
        constexpr float kColR    = 22.98f;       // right knob column

        // Knob rows — 2 cols × 4 rows, shifted 15px (5.08mm) higher
        constexpr float kRow1Y   = 18.0f;
        constexpr float kRow2Y   = 31.0f;
        constexpr float kRow3Y   = 44.0f;
        constexpr float kRow4Y   = 57.0f;
        constexpr float kLedY    = 57.0f;   // centred between LOOP (kColL) and VOL (kColR)
        constexpr float kSwitchY = 64.36f;   // PM/FM toggle, just below LED

        // Port sub-columns centered on panel
        constexpr float kSubL    = -8.0f;
        constexpr float kSubM    =  0.0f;
        constexpr float kSubR    =  8.0f;

        // Port rows (shifted 15px/5.08mm lower; new kPRow0 above CLK row)
        constexpr float kPRow0   = 78.0f;   // PM▲  | DPT▲  | DEPTH knob
        constexpr float kPRow1   = 89.0f;   // CLK  | EQ▲   | SPD▲
        constexpr float kPRow2   = 100.0f;  // VOL▲ | P0▲   | P1▲
        constexpr float kPRow3   = 111.0f;  // LOOP▲| P2▲   | PITCH▲
        constexpr float kPRow4   = 122.0f;  // OUT  |       |

        NVGcolor labelCol = nvgRGB(0x11, 0x11, 0x11);  // near-black text

        // Label ABOVE a knob (3px higher than before to clear the knob body)
        auto knobLabel = [&](float x, float y, const char* text) {
            auto* lbl = new rack::ui::Label;
            lbl->box.pos    = mm2px(Vec(x - 7.f, y - 9.5f));
            lbl->box.size.x = mm2px(Vec(14.f, 0)).x;
            lbl->alignment  = rack::ui::Label::CENTER_ALIGNMENT;
            lbl->fontSize   = 8.5f;
            lbl->color      = labelCol;
            lbl->text       = text;
            addChild(lbl);
        };

        // Label ABOVE a port — offset -9mm clears the jack body with ~10px gap
        auto portLabel = [&](float x, float y, const char* text) {
            auto* lbl = new rack::ui::Label;
            lbl->box.pos    = mm2px(Vec(x - 5.f, y - 9.0f));
            lbl->box.size.x = mm2px(Vec(12.f, 0)).x;  // 12mm: wide enough for "LOOP" without wrapping
            lbl->alignment  = rack::ui::Label::CENTER_ALIGNMENT;
            lbl->fontSize   = 7.5f;
            lbl->color      = labelCol;
            lbl->text       = text;
            addChild(lbl);
        };

        // Module title — full panel width so CENTER_ALIGNMENT centres correctly
        {
            auto* lbl = new rack::ui::Label;
            lbl->box.pos    = mm2px(Vec(1.36f, 1.1f));
            lbl->box.size.x = mm2px(Vec(kW, 0)).x;
            lbl->alignment  = rack::ui::Label::CENTER_ALIGNMENT;
            lbl->fontSize   = 10.f;
            lbl->color      = labelCol;
            lbl->text       = "X O R n a d o";
            addChild(lbl);
        }

        // ── Left column: EQ | SPEED | PITCH | LOOP ────────────────────────
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColL, kRow1Y)), module, XORnado::EQ_1));
        knobLabel(kColL, kRow1Y, "EQ");

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColL, kRow2Y)), module, XORnado::SPEED_1));
        knobLabel(kColL + 1.1f, kRow2Y, "SPEED");

        addParam(createParamCentered<RoundBlackSnapKnob>(
            mm2px(Vec(kColL, kRow3Y)), module, XORnado::PITCH_1));
        knobLabel(kColL + 1.1f, kRow3Y, "PITCH");

        addParam(createParamCentered<RoundBlackSnapKnob>(
            mm2px(Vec(kColL, kRow4Y)), module, XORnado::LOOP_1));
        knobLabel(kColL + 1.78f, kRow4Y, "LOOP");

        // ── Right column: P0 | P1 | P2 | VOL ─────────────────────────────
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColR, kRow1Y)), module, XORnado::P0_1));
        knobLabel(kColR, kRow1Y, "P0");

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColR, kRow2Y)), module, XORnado::P1_1));
        knobLabel(kColR, kRow2Y, "P1");

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColR, kRow3Y)), module, XORnado::P2_1));
        knobLabel(kColR, kRow3Y, "P2");

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(kColR, kRow4Y)), module, XORnado::VOL_1));
        knobLabel(kColR + 1.1f, kRow4Y, "VOL");

        // Step LED (centred between LOOP and VOL)
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(kCX, kLedY)), module, XORnado::STEP_LIGHT_1));

        // PM/FM switch (CKSS — vertical 2-position toggle), just below LED
        {
            auto* lbl = new rack::ui::Label;
            lbl->box.pos    = mm2px(Vec(kCX - 11.41f, kSwitchY - 3.48f));
            lbl->box.size.x = mm2px(Vec(14.f, 0)).x;
            lbl->alignment  = rack::ui::Label::CENTER_ALIGNMENT;
            lbl->fontSize   = 7.5f;
            lbl->color      = labelCol;
            lbl->text       = "PM/FM";
            addChild(lbl);
        }
        addParam(createParamCentered<CKSS>(
            mm2px(Vec(kCX, kSwitchY)), module, XORnado::MUTATION_MODE_1));

        // ── Port row 0: PM▲ | DPT▲ | DEPTH knob ────────────────────────────────
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubL, kPRow0)), module, XORnado::PM_CV_1));
        portLabel(kCX + kSubL, kPRow0, "PM");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubM, kPRow0)), module, XORnado::PM_DEPTH_CV_1));
        portLabel(kCX + kSubM, kPRow0, "DPT");

        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(kCX + kSubR, kPRow0)), module, XORnado::PM_DEPTH_1));
        knobLabel(kCX + kSubR + 2.02f, kPRow0 + 0.68f, "DEPTH");

        // ── Port row 1: CLK | EQ▲ | SPD▲ ────────────────────────────────
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubL, kPRow1)), module, XORnado::TRIG_1));
        portLabel(kCX + kSubL, kPRow1, "CLK");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubM, kPRow1)), module, XORnado::EQ_CV_1));
        portLabel(kCX + kSubM, kPRow1, "EQ");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubR, kPRow1)), module, XORnado::SPEED_CV_1));
        portLabel(kCX + kSubR, kPRow1, "SPD");

        // ── Port row 2: VOL▲ | P0▲ | P1▲ ────────────────────────────────
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubL, kPRow2)), module, XORnado::VOL_CV_1));
        portLabel(kCX + kSubL, kPRow2, "VOL");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubM, kPRow2)), module, XORnado::P0_CV_1));
        portLabel(kCX + kSubM, kPRow2, "P0");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubR, kPRow2)), module, XORnado::P1_CV_1));
        portLabel(kCX + kSubR, kPRow2, "P1");

        // ── Port row 3: LOOP▲ | P2▲ | PITCH▲ ────────────────────────────
        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubL, kPRow3)), module, XORnado::LOOP_CV_1));
        portLabel(kCX + kSubL, kPRow3, "LOOP");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubM, kPRow3)), module, XORnado::P2_CV_1));
        portLabel(kCX + kSubM, kPRow3, "P2");

        addInput(createInputCentered<MVXport_s1>(
            mm2px(Vec(kCX + kSubR, kPRow3)), module, XORnado::PITCH_CV_1));
        portLabel(kCX + kSubR, kPRow3, "PTH");

        // ── Port row 4: L | OUT | R ───────────────────────────────────────
        addOutput(createOutputCentered<MVXport_s1_purple>(
            mm2px(Vec(kCX + kSubL, kPRow4)), module, XORnado::OUT_L_1));
        portLabel(kCX + kSubL, kPRow4, "L");

        addOutput(createOutputCentered<MVXport_s1_purple>(
            mm2px(Vec(kCX + kSubM, kPRow4)), module, XORnado::OUT_1));
        portLabel(kCX + kSubM, kPRow4, "OUT");

        addOutput(createOutputCentered<MVXport_s1_purple>(
            mm2px(Vec(kCX + kSubR, kPRow4)), module, XORnado::OUT_R_1));
        portLabel(kCX + kSubR, kPRow4, "R");
    }

    // Draw grey background
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(0xB0, 0xB0, 0xB0));
        nvgFill(args.vg);

        ModuleWidget::draw(args);
    }

    // ── Context menu ─────────────────────────────────────────────────────
    void appendContextMenu(Menu* menu) override {
        XORnado* m = dynamic_cast<XORnado*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createCheckMenuItem(
            "Step Mode (clock = step t)", "",
            [m]() { return m->channels[0].step_mode; },
            [m]() { m->channels[0].step_mode = !m->channels[0].step_mode; }
        ));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Output Range"));
        menu->addChild(createCheckMenuItem(
            "Bipolar ±10V", "",
            [m]() { return m->channels[0].bipolar; },
            [m]() { m->channels[0].bipolar = !m->channels[0].bipolar; }
        ));
    }
};

#else // METAMODULE

struct XORnadoWidget : ModuleWidget {
    XORnadoWidget(XORnado* module) {
        setModule(module);
        box.size = Vec(RACK_GRID_WIDTH * 6, RACK_GRID_HEIGHT);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/XORnado.png")));

        constexpr float kW       = 30.48f;
        constexpr float kCX      = kW * 0.5f;
        constexpr float kColL    = 7.5f;
        constexpr float kColR    = 22.98f;
        constexpr float kRow1Y   = 18.0f;
        constexpr float kRow2Y   = 31.0f;
        constexpr float kRow3Y   = 44.0f;
        constexpr float kRow4Y   = 57.0f;
        constexpr float kSwitchY = 64.36f;
        constexpr float kSubL    = -8.0f;
        constexpr float kSubM    = 0.0f;
        constexpr float kSubR    = 8.0f;
        constexpr float kPRow0   = 78.0f;
        constexpr float kPRow1   = 89.0f;
        constexpr float kPRow2   = 100.0f;
        constexpr float kPRow3   = 111.0f;
        constexpr float kPRow4   = 122.0f;

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColL, kRow1Y)), module, XORnado::EQ_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColL, kRow2Y)), module, XORnado::SPEED_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColL, kRow3Y)), module, XORnado::PITCH_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColL, kRow4Y)), module, XORnado::LOOP_1));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColR, kRow1Y)), module, XORnado::P0_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColR, kRow2Y)), module, XORnado::P1_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColR, kRow3Y)), module, XORnado::P2_1));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kColR, kRow4Y)), module, XORnado::VOL_1));

        addParam(createParamCentered<CKSS>(mm2px(Vec(kCX, kSwitchY)), module, XORnado::MUTATION_MODE_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubL, kPRow0)), module, XORnado::PM_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubM, kPRow0)), module, XORnado::PM_DEPTH_CV_1));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(kCX + kSubR, kPRow0)), module, XORnado::PM_DEPTH_1));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubL, kPRow1)), module, XORnado::TRIG_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubM, kPRow1)), module, XORnado::EQ_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubR, kPRow1)), module, XORnado::SPEED_CV_1));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubL, kPRow2)), module, XORnado::VOL_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubM, kPRow2)), module, XORnado::P0_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubR, kPRow2)), module, XORnado::P1_CV_1));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubL, kPRow3)), module, XORnado::LOOP_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubM, kPRow3)), module, XORnado::P2_CV_1));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubR, kPRow3)), module, XORnado::PITCH_CV_1));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubL, kPRow4)), module, XORnado::OUT_L_1));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubM, kPRow4)), module, XORnado::OUT_1));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kCX + kSubR, kPRow4)), module, XORnado::OUT_R_1));
    }
};

#endif // METAMODULE

Model* modelXORnado = createModel<XORnado, XORnadoWidget>("XORnado");
