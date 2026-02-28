/*
 * Phaseon — FM + Wavetable monster voice for VCV Rack
 *
 * A preset-first mono synthesizer designed for aggressive modern sounds
 * and CV-driven performance.  6 internal operators (hidden from user),
 * wavetable oscillators, curated FM algorithms, macro controls.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */

#include "plugin.hpp"
#include "phaseon/PhaseonVoice.hpp"
#include "phaseon/PhaseonMacros.hpp"
#include "phaseon/PhaseonWavetable.hpp"
#include "phaseon/PhaseonAlgorithm.hpp"
#include "phaseon/PhaseonPolish.hpp"

#include <array>
#include <algorithm>
#include <utility>
#include <cctype>
#include <cstring>
#include <cmath>

#ifdef METAMODULE
#include <span>
#include "patch/patch_file.hh"
#else
#include <osdialog.h>
#include "ui/PngPanelBackground.hpp"
#endif

using namespace phaseon;

struct Phaseon;

// Custom quantity: show waveform name or semitone offset depending on OP Wav/Freq switch.
struct OpEditParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

// Custom quantity: show "Waveform" or "Freq" instead of 0/1.
struct OpWfModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

// Custom quantity: show "LFO 1" / "LFO 2" / "ENV" for the LFO select switch.
struct LfoSelectParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float v = getValue();
        if (v < 0.5f)
            return "LFO 1";
        if (v < 1.5f)
            return "LFO 2";
        return "ENV";
    }
};

// Custom quantity: show LFO rate as multiplier/divider
struct LfoRateParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

// Custom quantity: show "All Ops" or "Selected Op" for the LFO target switch.
struct LfoTargetParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return (getValue() > 0.5f) ? "Selected Op" : "All Ops";
    }
};

struct Phaseon : Module {
    enum ParamId {
        TIMBRE_PARAM,
        MOTION_PARAM,
        DENSITY_PARAM,
        EDGE_PARAM,
        FM_CHARACTER_PARAM,
        ALGO_PARAM,
        PANIC_PARAM,
        MORPH_PARAM,
        COMPLEX_PARAM,
        ENV_STYLE_PARAM,
        ENV_SPREAD_PARAM,
        SPIKE_PARAM,
        ROLE_SHUFFLE_PARAM,
        TAIL_PARAM,
        DRIFT_PARAM,
        NETWORK_PARAM,
        PRESET_PREV_PARAM,
        PRESET_NEXT_PARAM,
        PRESET_SAVE_PARAM,
        WT_SELECT_PARAM,
        SCRAMBLE_PARAM,
        MOD1_AMT_PARAM,
        MOD2_AMT_PARAM,
        MOD3_AMT_PARAM,
        MOD4_AMT_PARAM,
        MOD5_AMT_PARAM,
        MOD6_AMT_PARAM,
        FORMANT_PARAM,
        MOD_RND_PARAM,
        MOD_MUT_PARAM,
        MOD_AMT_PARAM,
        WARP_MODE_PARAM,
        // Per-operator waveform editing
        EDIT_OP_PARAM,
        OP_WAVE_PARAM,

        TAMING_PARAM,

        // Feedback spice: scales operator self-feedback (and can add a touch of carrier feedback)
        FEEDBACK_BOOST_PARAM,

        // Per-operator frequency editing (stored per op; this param edits selected op)
        OP_FREQ_PARAM,

        // OP Wav/Freq switch
        OP_WF_MODE_PARAM,

        // LFO controls (Surge-style Rate/Phase/Deform/Amp)
        LFO_RATE_PARAM,
        LFO_PHASE_PARAM,
        LFO_DEFORM_PARAM,
        LFO_AMP_PARAM,
        LFO_SELECT_PARAM,      // switch: 0=LFO1, 1=LFO2
        LFO_TARGET_PARAM,      // switch: 0=ALL, 1=OP
        LFO_OP_SELECT_PARAM,   // 1..6 when target=OP

        // Per-operator level trims ("VOLUME" for each operator)
        OP1_LEVEL_TRIM_PARAM,
        OP2_LEVEL_TRIM_PARAM,
        OP3_LEVEL_TRIM_PARAM,
        OP4_LEVEL_TRIM_PARAM,
        OP5_LEVEL_TRIM_PARAM,
        OP6_LEVEL_TRIM_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        GATE_INPUT,
        VOCT_INPUT,
        TIMBRE_CV,
        HARM_DENSITY_CV,
        WT_SELECT_CV,       // was FM_CHAOS_CV  — bipolar ±5V selects WT family ±1 step
        TIMBRE2_CV,         // was TRANSIENT_SPIKE_CV — perf timbre CV (stacks with I/O timbre)
        WT_REGION_BIAS_CV,
        SPECTRAL_TILT_CV,
        MOTION_CV,          // was INSTABILITY_CV — CV adds to motion amount
        FORMANT_CV,         // replaces LOOP_RATE_CV — ±5V formant intensity
        ENV_SPREAD_CV,      // new — 0..10V adds to env spread knob
        EDGE_CV,            // new — 0..10V adds to edge knob
        COMPLEXITY_CV,      // new — 0..10V adds to complexity knob
        MORPH_CV,           // new — ±5V adds to algorithm morph knob
        CLOCK_INPUT,        // clock input -- rising edge sets LFO1 period
        INPUTS_LEN
    };
    enum OutputId {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        GATE_LIGHT,
        LIGHTS_LEN
    };

#ifdef METAMODULE
    enum DisplayId {
        PRESET_DISPLAY = LIGHTS_LEN,
        DISPLAY_IDS_LEN
    };
#endif

    // ── DSP engine ──────────────────────────────────────────────────
    PhaseonVoice voice;
    PhaseonMacroState macros;
    SpectralTilt tiltFilter;
    OutputPolish polish;
    WavetableBank wavetableBank;

    // Display status (temporary). Used to report wavetable load status.
    std::string statusText;
    float statusSecondsLeft = 0.0f;

#ifdef METAMODULE
    static std::string normalizeVolume(std::string vol) {
        // MetaModule volume strings vary (e.g. "ram:/" vs "ram:").
        // Normalize so path concatenation is safe.
        if (vol.empty())
            vol = "sdc:/";
        if (vol == "ram:/" || vol == "ram:")
            vol = "sdc:/";
        if (!vol.empty() && vol.back() != '/') {
            if (vol.back() == ':')
                vol.push_back('/');
            else
                vol.push_back('/');
        }
        return vol;
    }
#endif

    std::string wavetableDir() const {
#ifdef METAMODULE
        std::string vol = normalizeVolume(std::string(MetaModule::Patch::get_volume()));
        return vol + "phaseon/wavetables";
#else
        return asset::user("MorphWorx/phaseon/wavetables");
#endif
    }

#ifndef METAMODULE
    std::string wavetableDirPluginInstall() const {
        // Convenience: allow storing wavs next to the installed plugin folder.
        // Example: .../Rack2/plugins-win-x64/MorphWorx/phaseon/wavetables
        return asset::plugin(pluginInstance, "phaseon/wavetables");
    }
#endif

    static inline std::string toLowerAscii(std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        return s;
    }

    const char* displayTextCstr() const {
        if (statusSecondsLeft > 0.0f && !statusText.empty())
            return statusText.c_str();
        return presetName.c_str();
    }

    void loadUserWavetablesOnce() {
    #ifdef METAMODULE
        // MetaModule: avoid SD-card directory scanning and WAV loading at module-add time.
        // This can stall the UI/audio thread (and has been observed to freeze on load).
        statusText = "WT: builtins";
        statusSecondsLeft = 6.0f;
        return;
    #endif
        // Builtins must already be present.
        std::vector<std::string> dirs;
        dirs.push_back(wavetableDir());
    #ifndef METAMODULE
        // Compatibility: still scan the legacy user folder.
        dirs.push_back(asset::user("Bemushroomed/phaseon/wavetables"));
    #endif
#ifndef METAMODULE
        dirs.push_back(wavetableDirPluginInstall());
#endif

        // Collect .wav files across directories. De-dupe by filename (case-insensitive).
        std::vector<std::string> wavs;
        wavs.reserve(128);

        auto addDir = [&](const std::string& dir) {
            system::createDirectories(dir);
            if (!system::exists(dir) || !system::isDirectory(dir))
                return;

            std::vector<std::string> entries = system::getEntries(dir, 0);
            for (const std::string& p : entries) {
                if (!system::isFile(p))
                    continue;
                std::string ext = toLowerAscii(system::getExtension(p));
                if (ext != ".wav")
                    continue;

                std::string fn = toLowerAscii(system::getFilename(p));
                bool existsAlready = false;
                for (const std::string& q : wavs) {
                    if (toLowerAscii(system::getFilename(q)) == fn) {
                        existsAlready = true;
                        break;
                    }
                }
                if (!existsAlready)
                    wavs.push_back(p);
            }
        };

        for (const std::string& d : dirs)
            addDir(d);

        // Sort by filename to keep stable ordering across OS/filesystems.
        std::sort(wavs.begin(), wavs.end(), [](const std::string& a, const std::string& b) {
            std::string fa = toLowerAscii(system::getFilename(a));
            std::string fb = toLowerAscii(system::getFilename(b));
            if (fa == fb) return a < b;
            return fa < fb;
        });

        // Hard cap to avoid runaway memory usage (especially on MetaModule).
        constexpr int kMaxUserTables = 64;
        int found = (int)wavs.size();
        int loaded = 0;
        int failed = 0;

        for (const std::string& p : wavs) {
            if (loaded >= kMaxUserTables)
                break;
            int idx = wavetableBank.loadFromWav(p, 2048);
            if (idx >= 0)
                loaded++;
            else
                failed++;
        }

        // Show a short message so the user knows immediately.
        char buf[96];
        if (found == 0) {
            snprintf(buf, sizeof(buf), "WT: 0 loaded (add .wav)");
        } else if (failed > 0) {
            snprintf(buf, sizeof(buf), "WT: %d/%d loaded (%d fail)", loaded, found, failed);
        } else {
            snprintf(buf, sizeof(buf), "WT: %d loaded", loaded);
        }
        statusText = buf;
        statusSecondsLeft = 6.0f;
    }

    // ── State ───────────────────────────────────────────────────────
    bool lastGate = false;
    float lastPitch = 0.0f;
    float slewedVoct = 0.0f;    // current smoothed V/Oct (only slews during legato)
    int controlDivider = 0;
    static constexpr int kControlRate = 32; // apply macros every N samples

    std::string presetName = "Init";
    bool engineReady = false;

    // Scene morph determinism
    uint32_t roleSeed = 0xC0FFEE11u;
    bool lastShuffle = false;

    // Shuffle chaos burst (temporary chaos+spike on shuffle press)
    float shuffleBurstTimer = 0.0f;  // seconds remaining; 0 = inactive

    // Clock sync for LFO1
    bool lastClockHigh = false;
    float clockPeriod = 0.0f;       // measured period in seconds (0 = no clock)
    float clockTimer = 0.0f;        // time since last rising edge
    float clockPeriodSmoothed = 0.0f; // slewed measurement for stability

    // BITCRUSH knob coupling: cap bitcrush depth and add a strong high-cut filter.
    float bitcrushKnob01 = 0.0f;          // 0..1 (raw knob)
    float bitcrushFilter01 = 0.0f;        // 0..1 (filter amount derived from knob)
    float bitcrushLpPrevL = 0.0f, bitcrushLpPrevR = 0.0f;

    // Mod matrix randomize/mutate button edges
    bool lastModRnd = false;
    bool lastModMut = false;

    // Preset bank (fixed path, browsed via the screen)
    static constexpr int kPresetSlots = 128;
    struct PresetSlot {
        std::string name;
        uint32_t seed = 0xC0FFEE11u;
        std::array<float, PARAMS_LEN> params{};
        std::string wtName;

        // Per-operator waveform mode (0=WT, 1=Sine, 2=Triangle, 3=Saw, 4=Harmonic Sine, 5=Skewed Sine)
        std::array<uint8_t, 6> opWaveMode{};

        // Per-operator ratio semitone offsets (applied on top of the current ratio set)
        std::array<float, 6> opFreqSemi{};

        // Per-LFO settings (2 LFOs, per-operator)
        float lfoRateOp[2][6]        = { {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f} };
        float lfoPhaseOffsetOp[2][6] = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };
        float lfoDeformOp[2][6]      = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };
        float lfoAmpOp[2][6]         = { {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f} };
        int   lfoTargetOp[2]         = {-1, -1};  // -1=ALL, 0..5=operator

        // Per-operator ENV edit shapes (0..1, 0.5 = neutral)
        float envAtkShape[6]         = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
        float envDecShape[6]         = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
        float envSusShape[6]         = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
        float envRelShape[6]         = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
        bool used = false;
    };
    std::array<PresetSlot, kPresetSlots> bank;
    int currentPreset = 0;
    bool bankLoaded = false;
    int bankFileVersionLoaded = 1;
    bool lastPresetPrev = false;
    bool lastPresetNext = false;

    // Per-operator waveform mode (persisted via patch JSON + preset bank)
    // 0=WT, 1=Sine, 2=Triangle, 3=Saw, 4=Harmonic Sine, 5=Skewed Sine
    std::array<uint8_t, 6> opWaveMode{};

    // Per-operator semitone offsets for ratio tuning (persisted via patch JSON + preset bank)
    std::array<float, 6> opFreqSemi{};
    int lastEditOp = 0;
    int lastOpWfMode = -1;

    // Per-LFO settings (2 LFOs, per-operator): Rate / Phase / Deform / Amp / TargetOp
    // For LFO1, values are typically kept identical across operators.
    float lfoRateOp[2][6]        = { {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f} };
    float lfoPhaseOffsetOp[2][6] = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };
    float lfoDeformOp[2][6]      = { {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} };
    float lfoAmpOp[2][6]         = { {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f} };
    int   lfoTargetOp[2]         = {-1, -1};

    // LFO/ENV edit state: 3-way mode (0=LFO1, 1=LFO2, 2=ENV) + last edited operator in ENV/LFO modes
    int   lastLfoMode       = -1;
    int   lastEnvEditOp     = 0;
    int   lastLfoEditOp[2]  = {-1, -1}; // -1 = ALL, 0..5 = OP

    // Per-operator ENV edit shapes (0..1, 0.5 = neutral)
    float envAtkShape[6]    = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float envDecShape[6]    = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float envSusShape[6]    = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float envRelShape[6]    = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    static inline float clamp01(float x) {
        if (x < 0.0f) return 0.0f;
        if (x > 1.0f) return 1.0f;
        return x;
    }

    template <typename T, size_t N>
    static T pickWeighted(const std::array<std::pair<T, float>, N>& items) {
        float total = 0.0f;
        for (const auto& it : items)
            total += std::max(0.0f, it.second);
        if (total <= 0.0f)
            return items[0].first;

        float r = random::uniform() * total;
        for (const auto& it : items) {
            float w = std::max(0.0f, it.second);
            if (r <= w)
                return it.first;
            r -= w;
        }
        return items[N - 1].first;
    }

    static inline bool slotIsActive(const phaseon::ModSlot& s) {
        return s.src != phaseon::ModSource::Off && s.dst != phaseon::ModDest::Off;
    }

    bool hasDuplicateRouting(int slotIdx, phaseon::ModSource src, phaseon::ModDest dst, phaseon::ModOpTarget tgt) const {
        for (int i = 0; i < phaseon::kModSlots; ++i) {
            if (i == slotIdx) continue;
            const phaseon::ModSlot& s = macros.modMatrix.slots[i];
            if (!slotIsActive(s)) continue;
            if (s.src == src && s.dst == dst && s.target == tgt)
                return true;
        }
        return false;
    }

    phaseon::ModOpTarget pickTargetForDest(phaseon::ModDest dst, float amt01) {
        // Global destinations ignore op targeting.
        if (!phaseon::isPerOpDest(dst))
            return phaseon::ModOpTarget::All;

        float hi = clamp01((amt01 - 0.70f) / 0.30f);

        // Occasionally target a single op at high AMT.
        if (random::uniform() < 0.12f * hi) {
            int op = (int)(random::u32() % 6u);
            return (phaseon::ModOpTarget)((int)phaseon::ModOpTarget::Op1 + op);
        }

        if (dst == phaseon::ModDest::FramePos || dst == phaseon::ModDest::WsMix || dst == phaseon::ModDest::WsDrive) {
            const std::array<std::pair<phaseon::ModOpTarget, float>, 3> w = {
                std::make_pair(phaseon::ModOpTarget::CarriersOnly,  1.20f),
                std::make_pair(phaseon::ModOpTarget::All,           0.90f),
                std::make_pair(phaseon::ModOpTarget::ModulatorsOnly,0.35f),
            };
            return pickWeighted(w);
        }

        const std::array<std::pair<phaseon::ModOpTarget, float>, 3> w = {
            std::make_pair(phaseon::ModOpTarget::ModulatorsOnly, 1.10f),
            std::make_pair(phaseon::ModOpTarget::All,            0.90f),
            std::make_pair(phaseon::ModOpTarget::CarriersOnly,   0.40f),
        };
        return pickWeighted(w);
    }

    phaseon::ModSource pickSourceDubstep(float amt01) {
        float hi = clamp01((amt01 - 0.65f) / 0.35f);
        const std::array<std::pair<phaseon::ModSource, float>, 12> w = {
            std::make_pair(phaseon::ModSource::Lfo1,          1.35f),
            std::make_pair(phaseon::ModSource::Lfo2,          1.20f),
            std::make_pair(phaseon::ModSource::AmpEnv,        1.10f),
            std::make_pair(phaseon::ModSource::RandomPerNote, 1.00f),
            std::make_pair(phaseon::ModSource::Velocity,      0.75f),
            std::make_pair(phaseon::ModSource::Keytrack,      0.55f),
            std::make_pair(phaseon::ModSource::CvMorph,       0.35f + 0.25f * hi),
            std::make_pair(phaseon::ModSource::CvTilt,        0.30f + 0.20f * hi),
            std::make_pair(phaseon::ModSource::CvMotion,      0.28f + 0.18f * hi),
            std::make_pair(phaseon::ModSource::CvEdge,        0.22f + 0.18f * hi),
            std::make_pair(phaseon::ModSource::CvTimbre,      0.22f + 0.10f * hi),
            std::make_pair(phaseon::ModSource::CvDensity,     0.18f + 0.12f * hi),
        };
        return pickWeighted(w);
    }

    phaseon::ModDest pickDestDubstep(float amt01) {
        float hi = clamp01((amt01 - 0.70f) / 0.30f);

        // "Spice" becomes more likely at high AMT.
        if (random::uniform() < 0.18f * hi) {
            const std::array<std::pair<phaseon::ModDest, float>, 3> spicy = {
                std::make_pair(phaseon::ModDest::Instability,    1.0f),
                std::make_pair(phaseon::ModDest::BitcrushAmount, 0.8f),
                std::make_pair(phaseon::ModDest::ChaosAmount,    0.6f),
            };
            return pickWeighted(spicy);
        }

        const std::array<std::pair<phaseon::ModDest, float>, 13> w = {
            std::make_pair(phaseon::ModDest::FramePos,        1.45f),
            std::make_pair(phaseon::ModDest::FormantAmount,   1.35f),
            std::make_pair(phaseon::ModDest::WsDrive,         1.20f),
            std::make_pair(phaseon::ModDest::WsMix,           1.05f),
            std::make_pair(phaseon::ModDest::FmDepth,         1.00f),
            std::make_pair(phaseon::ModDest::AlgorithmMorph,  0.75f),
            std::make_pair(phaseon::ModDest::TiltAmount,      0.65f),
            std::make_pair(phaseon::ModDest::EdgeAmount,      0.60f),
            std::make_pair(phaseon::ModDest::MotionAmount,    0.55f),
            std::make_pair(phaseon::ModDest::ComplexAmount,   0.55f),
            std::make_pair(phaseon::ModDest::PdAmount,        0.45f),
            std::make_pair(phaseon::ModDest::Feedback,        0.40f),
            std::make_pair(phaseon::ModDest::NetworkAmount,   0.35f),
        };
        return pickWeighted(w);
    }

    float pickSlewMsDubstep(float amt01, bool mutateStyle) {
        float hi = clamp01((amt01 - 0.60f) / 0.40f);
        // MUT should usually preserve existing slew.
        if (mutateStyle && random::uniform() < 0.70f)
            return -1.0f; // signal "keep"

        const std::array<std::pair<float, float>, 7> w = {
            std::make_pair(0.0f,   0.25f * hi),
            std::make_pair(5.0f,   1.20f),
            std::make_pair(10.0f,  1.00f),
            std::make_pair(20.0f,  0.90f),
            std::make_pair(50.0f,  0.55f),
            std::make_pair(90.0f,  0.35f * hi),
            std::make_pair(150.0f, 0.20f * hi),
        };
        return pickWeighted(w);
    }

    int computeSlotsToChange(float amt01) const {
        int n = (int)std::floor(amt01 * (float)phaseon::kModSlots + 0.5f);
        if (n < 1) n = 1;
        if (n > phaseon::kModSlots) n = phaseon::kModSlots;
        return n;
    }

    void modMatrixRandomize(float amt01) {
        amt01 = clamp01(amt01);
        int n = computeSlotsToChange(amt01);

        std::array<int, phaseon::kModSlots> order{};
        for (int i = 0; i < phaseon::kModSlots; ++i) order[i] = i;
        // Fisher-Yates shuffle
        for (int i = phaseon::kModSlots - 1; i > 0; --i) {
            int j = (int)(random::u32() % (uint32_t)(i + 1));
            std::swap(order[i], order[j]);
        }

        int changed = 0;
        // Pass 0: fill inactive slots; Pass 1: overwrite active slots.
        for (int pass = 0; pass < 2 && changed < n; ++pass) {
            for (int k = 0; k < phaseon::kModSlots && changed < n; ++k) {
                int i = order[k];
                phaseon::ModSlot& s = macros.modMatrix.slots[i];
                bool active = slotIsActive(s);
                if (pass == 0 && active) continue;
                if (pass == 1 && !active) continue;

                for (int tries = 0; tries < 8; ++tries) {
                    phaseon::ModDest dst = pickDestDubstep(amt01);
                    phaseon::ModSource src = pickSourceDubstep(amt01);
                    phaseon::ModOpTarget tgt = pickTargetForDest(dst, amt01);
                    if (hasDuplicateRouting(i, src, dst, tgt))
                        continue;
                    s.src = src;
                    s.dst = dst;
                    s.target = tgt;
                    float slew = pickSlewMsDubstep(amt01, false);
                    if (slew >= 0.0f) s.slewMs = slew;
                    changed++;
                    break;
                }
            }
        }

        statusText = "Matrix: RND";
        statusSecondsLeft = 3.0f;
    }

    void modMatrixMutate(float amt01) {
        amt01 = clamp01(amt01);
        int n = computeSlotsToChange(amt01);

        std::array<int, phaseon::kModSlots> active{};
        int activeCount = 0;
        for (int i = 0; i < phaseon::kModSlots; ++i) {
            if (slotIsActive(macros.modMatrix.slots[i]))
                active[activeCount++] = i;
        }
        if (activeCount == 0) {
            modMatrixRandomize(amt01);
            return;
        }

        // Shuffle active list.
        for (int i = activeCount - 1; i > 0; --i) {
            int j = (int)(random::u32() % (uint32_t)(i + 1));
            std::swap(active[i], active[j]);
        }

        int changed = 0;
        for (int k = 0; k < activeCount && changed < n; ++k) {
            int i = active[k];
            phaseon::ModSlot& s = macros.modMatrix.slots[i];

            bool changeDstOnly = (amt01 < 0.35f);
            bool allowMulti = (amt01 > 0.70f);

            phaseon::ModSource src = s.src;
            phaseon::ModDest dst = s.dst;
            phaseon::ModOpTarget tgt = s.target;
            float slewMs = s.slewMs;

            bool ok = false;
            for (int tries = 0; tries < 8; ++tries) {
                phaseon::ModSource nsrc = src;
                phaseon::ModDest ndst = dst;
                if (changeDstOnly) {
                    ndst = pickDestDubstep(amt01);
                } else {
                    if (random::uniform() < 0.50f)
                        ndst = pickDestDubstep(amt01);
                    else
                        nsrc = pickSourceDubstep(amt01);
                }
                phaseon::ModOpTarget ntgt = pickTargetForDest(ndst, amt01);
                if (hasDuplicateRouting(i, nsrc, ndst, ntgt))
                    continue;
                src = nsrc;
                dst = ndst;
                tgt = ntgt;
                ok = true;
                break;
            }
            if (!ok) continue;

            if (allowMulti && random::uniform() < 0.35f) {
                phaseon::ModOpTarget ntgt = pickTargetForDest(dst, amt01);
                if (!hasDuplicateRouting(i, src, dst, ntgt))
                    tgt = ntgt;
                float slew = pickSlewMsDubstep(amt01, true);
                if (slew >= 0.0f) slewMs = slew;
            }

            s.src = src;
            s.dst = dst;
            s.target = tgt;
            s.slewMs = slewMs;
            changed++;
        }

        statusText = "Matrix: MUT";
        statusSecondsLeft = 3.0f;
    }

    static inline int clampTableIndex(int idx, int numTables) {
        if (numTables <= 0) return 0;
        if (idx < 0) return 0;
        if (idx >= numTables) return numTables - 1;
        return idx;
    }

    int findTableByNameUserFirst(const std::string& name) const {
        if (name.empty()) return -1;
        const int numTables = wavetableBank.count();
        if (numTables <= 0) return -1;

        std::string needle = toLowerAscii(name);
        int builtinCount = (wavetableBank.builtinCount > 0) ? wavetableBank.builtinCount : 11;
        if (builtinCount < 0) builtinCount = 0;
        if (builtinCount > numTables) builtinCount = numTables;

        // Prefer user-loaded tables first to avoid collisions with builtins.
        for (int i = builtinCount; i < numTables; ++i) {
            const Wavetable* wt = wavetableBank.get(i);
            if (!wt) continue;
            if (toLowerAscii(wt->name) == needle)
                return i;
        }
        for (int i = 0; i < builtinCount; ++i) {
            const Wavetable* wt = wavetableBank.get(i);
            if (!wt) continue;
            if (toLowerAscii(wt->name) == needle)
                return i;
        }
        return -1;
    }

    std::string currentSelectedWtName() {
        int idx = (int)std::floor(params[WT_SELECT_PARAM].getValue() + 0.5f);
        idx = clampTableIndex(idx, wavetableBank.count());
        const Wavetable* wt = wavetableBank.get(idx);
        return wt ? wt->name : std::string();
    }

    int legacyEdgeSelectedTableIndex(float edge, int wtFamily) const {
        const int numTables = wavetableBank.count();
        if (numTables <= 0) return 0;
        const int builtinCount = (wavetableBank.builtinCount > 0) ? wavetableBank.builtinCount : 11;

        float e = edge;
        if (e < 0.0f) e = 0.0f;
        if (e > 1.0f) e = 1.0f;
        int fam = wtFamily;
        if (fam < 0) fam = 0;
        if (fam > 2) fam = 2;

        // Built-in table indices (see PhaseonWavetable.cpp generation order)
        // Curated compact set:
        // 0 Saw, 1 Formant, 2 Harsh, 3 FMStack, 4 ImpactPulse,
        // 5 FeralMachine, 6 AbyssalAlloy, 7 Substrate, 8 SubmergedMonolith,
        // 9 HarshNoise, 10 ArcadeFX
        constexpr int WT_SAW       = 0;
        constexpr int WT_FORMANT   = 1;
        constexpr int WT_HARSH     = 2;
        constexpr int WT_FMSTACK   = 3;
        constexpr int WT_IMPACT    = 4;
        constexpr int WT_FERAL     = 5;
        constexpr int WT_SUBSTRATE = 7;

        if (fam == 0) {
            // Old "Classic" family had many basic shapes. Map that intent onto the curated set.
            const int classicBuiltins[] = { WT_SAW, WT_FMSTACK, WT_IMPACT, WT_HARSH };
            constexpr int nClassicBuiltins = (int)(sizeof(classicBuiltins) / sizeof(classicBuiltins[0]));

            int userCount = std::max(0, numTables - builtinCount);
            int nClassic = nClassicBuiltins + userCount;
            if (nClassic < 1) nClassic = 1;

            int cidx = (int)(e * (float)(nClassic - 1) + 0.5f);
            if (cidx < 0) cidx = 0;
            if (cidx >= nClassic) cidx = nClassic - 1;

            int t = 0;
            if (cidx < nClassicBuiltins) {
                t = classicBuiltins[cidx];
            } else {
                int u = cidx - nClassicBuiltins;
                t = builtinCount + u;
            }
            return clampTableIndex(t, numTables);
        }

        if (fam == 1) {
            return clampTableIndex(WT_FORMANT, numTables);
        }

        // Old "Hybrid" family mapped to spectral/noise hybrids.
        // Use a musically useful proxy: FMStack -> Substrate -> FeralMachine as Edge rises.
        int h;
        if (e < 0.33f) h = WT_FMSTACK;
        else if (e < 0.66f) h = WT_SUBSTRATE;
        else h = WT_FERAL;
        return clampTableIndex(h, numTables);
    }

    std::string bankPath() const {
#ifdef METAMODULE
        // Prefer the current patch volume so the bank persists where the user is working.
        // If the patch isn't saved yet, fall back to SD.
        std::string vol = normalizeVolume(std::string(MetaModule::Patch::get_volume()));
        return vol + "phaseon/PhaseonBank.json";
#else
        return asset::user("MorphWorx/PhaseonBank.json");
#endif
    }

    static std::string stripSlotPrefix(const std::string& s) {
        // Strip "03/128 " or "03 " prefixes.
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        if (i < s.size() && s[i] == '/') {
            i++;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        }
        while (i < s.size() && s[i] == ' ') i++;
        return (i > 0) ? s.substr(i) : s;
    }

    void updatePresetDisplayName() {
        int idx = currentPreset;
        if (idx < 0) idx = 0;
        if (idx >= kPresetSlots) idx = kPresetSlots - 1;
        const PresetSlot& s = bank[idx];
        std::string nm = s.used ? s.name : std::string("Empty");
        char buf[48];
        snprintf(buf, sizeof(buf), "%02d/%d %s", idx + 1, kPresetSlots, nm.c_str());
        presetName = buf;
    }

    void bankInitDefault() {
        for (int i = 0; i < kPresetSlots; ++i) {
            bank[i] = PresetSlot{};
        }
        currentPreset = 0;
        updatePresetDisplayName();
    }

    bool bankLoad() {
        json_error_t err;
        std::string primaryPath = bankPath();
        json_t* root = json_load_file(primaryPath.c_str(), 0, &err);
#ifndef METAMODULE
        if (!root) {
            std::string legacyPath = asset::user("Bemushroomed/PhaseonBank.json");
            if (legacyPath != primaryPath)
                root = json_load_file(legacyPath.c_str(), 0, &err);
        }
#endif
        if (!root || !json_is_object(root)) {
            if (root) json_decref(root);
            return false;
        }

        bankFileVersionLoaded = 1;
        json_t* verJ = json_object_get(root, "version");
        if (verJ && json_is_integer(verJ)) {
            bankFileVersionLoaded = (int)json_integer_value(verJ);
            if (bankFileVersionLoaded < 1) bankFileVersionLoaded = 1;
        }

        json_t* slotsJ = json_object_get(root, "slots");
        if (!slotsJ || !json_is_array(slotsJ)) {
            json_decref(root);
            return false;
        }

        bankInitDefault();

        int n = (int)json_array_size(slotsJ);
        int count = std::min(n, kPresetSlots);
        for (int i = 0; i < count; ++i) {
            json_t* slotJ = json_array_get(slotsJ, i);
            if (!slotJ || !json_is_object(slotJ)) continue;

            PresetSlot& s = bank[i];
            s.used = true;

            json_t* nameJ = json_object_get(slotJ, "name");
            if (nameJ && json_is_string(nameJ)) {
                s.name = json_string_value(nameJ);
            } else {
                s.name = "Slot";
            }

            json_t* seedJ = json_object_get(slotJ, "roleSeed");
            if (seedJ && json_is_integer(seedJ)) {
                s.seed = (uint32_t)json_integer_value(seedJ);
            }

            json_t* wtJ = json_object_get(slotJ, "wtName");
            if (wtJ && json_is_string(wtJ)) {
                s.wtName = json_string_value(wtJ);
            }

            json_t* paramsJ = json_object_get(slotJ, "params");
            if (paramsJ && json_is_array(paramsJ)) {
                int pn = (int)json_array_size(paramsJ);
                int pc = std::min(pn, (int)PARAMS_LEN);
                for (int p = 0; p < pc; ++p) {
                    json_t* vJ = json_array_get(paramsJ, p);
                    if (vJ && json_is_number(vJ)) {
                        s.params[p] = (float)json_number_value(vJ);
                    }
                }

                // Migration: older bank versions don't include the new OPx level trim params.
                // If missing, default to 1.0 (neutral), not 0.0 (silence).
                auto setIfMissing = [&](int paramId, float def) {
                    if (paramId >= pn)
                        s.params[paramId] = def;
                };
                setIfMissing(OP1_LEVEL_TRIM_PARAM, 1.0f);
                setIfMissing(OP2_LEVEL_TRIM_PARAM, 1.0f);
                setIfMissing(OP3_LEVEL_TRIM_PARAM, 1.0f);
                setIfMissing(OP4_LEVEL_TRIM_PARAM, 1.0f);
                setIfMissing(OP5_LEVEL_TRIM_PARAM, 1.0f);
                setIfMissing(OP6_LEVEL_TRIM_PARAM, 1.0f);
            }

            // Per-operator waveform mode (introduced in bank version 3)
            json_t* owJ = json_object_get(slotJ, "opWaveMode");
            if (owJ && json_is_array(owJ)) {
                int on = (int)json_array_size(owJ);
                for (int oi = 0; oi < 6 && oi < on; ++oi) {
                    json_t* vJ = json_array_get(owJ, oi);
                    if (!vJ || !json_is_integer(vJ))
                        continue;
                    int v = (int)json_integer_value(vJ);
                    if (v < 0) v = 0;
                    if (v > 8) v = 8;
                    s.opWaveMode[oi] = (uint8_t)v;
                }
            }

            // Per-operator frequency semitone offsets (introduced in bank version 4)
            json_t* ofJ = json_object_get(slotJ, "opFreq");
            if (ofJ && json_is_array(ofJ)) {
                int on = (int)json_array_size(ofJ);
                for (int oi = 0; oi < 6 && oi < on; ++oi) {
                    json_t* vJ = json_array_get(ofJ, oi);
                    if (!vJ || !json_is_number(vJ))
                        continue;
                    float v = (float)json_number_value(vJ);
                    if (v < -24.0f) v = -24.0f;
                    if (v >  24.0f) v =  24.0f;
                    s.opFreqSemi[oi] = v;
                }
            }

            // Per-LFO settings (introduced in bank version 5; per-operator since version 6)
            {
                auto readNested = [&](const char* key, float dst[2][6], float lo, float hi) {
                    json_t* aJ = json_object_get(slotJ, key);
                    if (!aJ || !json_is_array(aJ))
                        return;

                    int an = (int)json_array_size(aJ);
                    for (int li = 0; li < 2 && li < an; ++li) {
                        json_t* vJ = json_array_get(aJ, li);
                        if (!vJ)
                            continue;
                        if (json_is_number(vJ)) {
                            float v = (float)json_number_value(vJ);
                            if (v < lo) v = lo;
                            if (v > hi) v = hi;
                            for (int oi = 0; oi < 6; ++oi)
                                dst[li][oi] = v;
                        }
                        else if (json_is_array(vJ)) {
                            int on = (int)json_array_size(vJ);
                            for (int oi = 0; oi < 6 && oi < on; ++oi) {
                                json_t* eJ = json_array_get(vJ, oi);
                                if (!eJ || !json_is_number(eJ))
                                    continue;
                                float v = (float)json_number_value(eJ);
                                if (v < lo) v = lo;
                                if (v > hi) v = hi;
                                dst[li][oi] = v;
                            }
                        }
                    }
                };

                readNested("lfoRate",   s.lfoRateOp,        0.0f, 1.0f);
                readNested("lfoPhase",  s.lfoPhaseOffsetOp, 0.0f, 1.0f);
                readNested("lfoDeform", s.lfoDeformOp,      0.0f, 1.0f);
                readNested("lfoAmp",    s.lfoAmpOp,         0.0f, 1.0f);

                json_t* ltJ = json_object_get(slotJ, "lfoTarget");
                if (ltJ && json_is_array(ltJ)) {
                    int an2 = (int)json_array_size(ltJ);
                    for (int li = 0; li < 2 && li < an2; ++li) {
                        json_t* eJ = json_array_get(ltJ, li);
                        if (eJ && json_is_integer(eJ)) {
                            int v = (int)json_integer_value(eJ);
                            if (v < -1) v = -1;
                            if (v > 5) v = 5;
                            s.lfoTargetOp[li] = v;
                        }
                    }
                }
            }

            // Per-operator ENV ADSR shapes (optional; available since bank version 7)
            {
                auto readEnv = [&](const char* key, float* dst) {
                    json_t* aJ = json_object_get(slotJ, key);
                    if (!aJ || !json_is_array(aJ))
                        return;
                    int n2 = (int)json_array_size(aJ);
                    for (int oi = 0; oi < 6 && oi < n2; ++oi) {
                        json_t* vJ = json_array_get(aJ, oi);
                        if (!vJ || !json_is_number(vJ))
                            continue;
                        dst[oi] = clamp01((float)json_number_value(vJ));
                    }
                };
                readEnv("envAtkShape", s.envAtkShape);
                readEnv("envDecShape", s.envDecShape);
                readEnv("envSusShape", s.envSusShape);
                readEnv("envRelShape", s.envRelShape);
            }
        }

        json_decref(root);
        updatePresetDisplayName();
        return true;
    }

    bool bankSave() {
        json_t* root = json_object();
        json_object_set_new(root, "format", json_string("MorphWorx.PhaseonBank"));
        json_object_set_new(root, "version", json_integer(8));

        json_t* slotsJ = json_array();
        for (int i = 0; i < kPresetSlots; ++i) {
            const PresetSlot& s = bank[i];
            if (!s.used) {
                json_array_append_new(slotsJ, json_null());
                continue;
            }

            json_t* slotJ = json_object();
            json_object_set_new(slotJ, "name", json_string(s.name.c_str()));
            json_object_set_new(slotJ, "roleSeed", json_integer((json_int_t)s.seed));

            if (!s.wtName.empty()) {
                json_object_set_new(slotJ, "wtName", json_string(s.wtName.c_str()));
            }

            // Per-operator waveform mode
            {
                json_t* owJ = json_array();
                for (int oi = 0; oi < 6; ++oi) {
                    json_array_append_new(owJ, json_integer((int)s.opWaveMode[oi]));
                }
                json_object_set_new(slotJ, "opWaveMode", owJ);
            }

            // Per-operator frequency semitone offsets
            {
                json_t* ofJ = json_array();
                for (int oi = 0; oi < 6; ++oi) {
                    json_array_append_new(ofJ, json_real((double)s.opFreqSemi[oi]));
                }
                json_object_set_new(slotJ, "opFreq", ofJ);
            }

            // Per-LFO settings (per-operator)
            {
                json_t* lrJ = json_array();
                json_t* lpJ = json_array();
                json_t* ldJ = json_array();
                json_t* laJ = json_array();
                json_t* ltJ = json_array();
                for (int li = 0; li < 2; ++li) {
                    json_t* lrOpJ = json_array();
                    json_t* lpOpJ = json_array();
                    json_t* ldOpJ = json_array();
                    json_t* laOpJ = json_array();
                    for (int oi = 0; oi < 6; ++oi) {
                        json_array_append_new(lrOpJ, json_real((double)s.lfoRateOp[li][oi]));
                        json_array_append_new(lpOpJ, json_real((double)s.lfoPhaseOffsetOp[li][oi]));
                        json_array_append_new(ldOpJ, json_real((double)s.lfoDeformOp[li][oi]));
                        json_array_append_new(laOpJ, json_real((double)s.lfoAmpOp[li][oi]));
                    }
                    json_array_append_new(lrJ, lrOpJ);
                    json_array_append_new(lpJ, lpOpJ);
                    json_array_append_new(ldJ, ldOpJ);
                    json_array_append_new(laJ, laOpJ);
                    json_array_append_new(ltJ, json_integer(s.lfoTargetOp[li]));
                }
                json_object_set_new(slotJ, "lfoRate", lrJ);
                json_object_set_new(slotJ, "lfoPhase", lpJ);
                json_object_set_new(slotJ, "lfoDeform", ldJ);
                json_object_set_new(slotJ, "lfoAmp", laJ);
                json_object_set_new(slotJ, "lfoTarget", ltJ);
            }

            // Per-operator ENV ADSR shapes
            {
                json_t* aJ = json_array();
                json_t* dJ = json_array();
                json_t* suJ = json_array();
                json_t* rJ = json_array();
                for (int oi = 0; oi < 6; ++oi) {
                    json_array_append_new(aJ, json_real((double)s.envAtkShape[oi]));
                    json_array_append_new(dJ, json_real((double)s.envDecShape[oi]));
                    json_array_append_new(suJ, json_real((double)s.envSusShape[oi]));
                    json_array_append_new(rJ, json_real((double)s.envRelShape[oi]));
                }
                json_object_set_new(slotJ, "envAtkShape", aJ);
                json_object_set_new(slotJ, "envDecShape", dJ);
                json_object_set_new(slotJ, "envSusShape", suJ);
                json_object_set_new(slotJ, "envRelShape", rJ);
            }

            json_t* paramsJ = json_array();
            for (int p = 0; p < PARAMS_LEN; ++p) {
                float v = s.params[p];
                if (p == PANIC_PARAM || p == ROLE_SHUFFLE_PARAM || p == PRESET_PREV_PARAM || p == PRESET_NEXT_PARAM || p == PRESET_SAVE_PARAM || p == MOD_RND_PARAM || p == MOD_MUT_PARAM)
                    v = 0.0f;
                json_array_append_new(paramsJ, json_real(v));
            }
            json_object_set_new(slotJ, "params", paramsJ);
            json_array_append_new(slotsJ, slotJ);
        }
        json_object_set_new(root, "slots", slotsJ);

        std::string p = bankPath();
        system::createDirectories(system::getDirectory(p));
        int rc = json_dump_file(root, p.c_str(), JSON_INDENT(2));
        json_decref(root);
        return rc == 0;
    }

    void bankEnsureLoaded() {
        if (bankLoaded) return;
        bankLoaded = true;

#ifdef METAMODULE
        // MetaModule: avoid synchronous filesystem I/O during activation.
        // `json_load_file`/`json_dump_file` + directory creation can stall or crash,
        // and MetaModule provides its own async file APIs.
        bankInitDefault();
        statusText = "Bank: default";
        statusSecondsLeft = 6.0f;
#else
        if (!bankLoad()) {
            bankInitDefault();
            bankSave();
        }
#endif
    }

    void bankCaptureCurrentToSlot(int idx) {
        idx = std::max(0, std::min(kPresetSlots - 1, idx));
        PresetSlot& s = bank[idx];
        s.used = true;

        std::string nm = stripSlotPrefix(presetName);
        if (nm.empty() || nm == "Empty") nm = "Saved";
        s.name = nm;
        s.seed = roleSeed;
        for (int i = 0; i < PARAMS_LEN; ++i) {
            float v = params[i].getValue();
            if (i == PANIC_PARAM || i == ROLE_SHUFFLE_PARAM || i == PRESET_PREV_PARAM || i == PRESET_NEXT_PARAM || i == PRESET_SAVE_PARAM || i == MOD_RND_PARAM || i == MOD_MUT_PARAM)
                v = 0.0f;
            s.params[i] = v;
        }

        s.wtName = currentSelectedWtName();

        for (int oi = 0; oi < 6; ++oi) {
            s.opWaveMode[oi] = opWaveMode[oi];
            s.opFreqSemi[oi] = opFreqSemi[oi];
        }

        // Capture per-LFO settings into preset slot
        for (int li = 0; li < 2; ++li) {
            for (int oi = 0; oi < 6; ++oi) {
                s.lfoRateOp[li][oi]        = lfoRateOp[li][oi];
                s.lfoPhaseOffsetOp[li][oi] = lfoPhaseOffsetOp[li][oi];
                s.lfoDeformOp[li][oi]      = lfoDeformOp[li][oi];
                s.lfoAmpOp[li][oi]         = lfoAmpOp[li][oi];
            }
            s.lfoTargetOp[li] = lfoTargetOp[li];
        }

        // Capture per-operator ENV ADSR shapes into preset slot
        for (int oi = 0; oi < 6; ++oi) {
            s.envAtkShape[oi] = envAtkShape[oi];
            s.envDecShape[oi] = envDecShape[oi];
            s.envSusShape[oi] = envSusShape[oi];
            s.envRelShape[oi] = envRelShape[oi];
        }
        updatePresetDisplayName();
    }

    bool renameCurrentPreset(const std::string& newName) {
        bankEnsureLoaded();
        int idx = std::max(0, std::min(kPresetSlots - 1, currentPreset));
        PresetSlot& s = bank[idx];
        // If the slot is currently empty, capture the current patch first so we
        // don't accidentally create a "used" slot with default/zero params.
        if (!s.used) {
            bankCaptureCurrentToSlot(idx);
        }

        std::string nm = newName;
        // Trim whitespace.
        while (!nm.empty() && (nm.front() == ' ' || nm.front() == '\t' || nm.front() == '\n' || nm.front() == '\r'))
            nm.erase(nm.begin());
        while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t' || nm.back() == '\n' || nm.back() == '\r'))
            nm.pop_back();
        if (nm.empty())
            nm = "Saved";

        // Keep names reasonably short for display.
        if (nm.size() > 28)
            nm.resize(28);
        s.name = nm;

        updatePresetDisplayName();

        #ifndef METAMODULE
            // Persist to the PhaseonBank.json so the name survives restarts.
            return bankSave();
        #else
            return true;
        #endif
    }

    void bankApplySlot(int idx, bool applyMidNote) {
        bankEnsureLoaded();
        idx = std::max(0, std::min(kPresetSlots - 1, idx));
        currentPreset = idx;

        const PresetSlot& s = bank[idx];
        if (s.used) {
            roleSeed = s.seed;
            for (int i = 0; i < PARAMS_LEN; ++i) {
                if (i == PANIC_PARAM || i == ROLE_SHUFFLE_PARAM || i == PRESET_PREV_PARAM || i == PRESET_NEXT_PARAM || i == PRESET_SAVE_PARAM || i == MOD_RND_PARAM || i == MOD_MUT_PARAM)
                    continue;
                float v = s.params[i];
                if (i == ALGO_PARAM || i == WT_SELECT_PARAM) {
                    v = floorf(v + 0.5f);
                }
                params[i].setValue(v);
            }

            // Prefer name-based WT resolution for portability.
            // For older bank versions, migrate from legacy Edge-based selection.
            if (!s.wtName.empty()) {
                int found = findTableByNameUserFirst(s.wtName);
                if (found >= 0) {
                    params[WT_SELECT_PARAM].setValue((float)found);
                } else {
                    params[WT_SELECT_PARAM].setValue(0.0f);
                    statusText = std::string("WT missing: ") + s.wtName;
                    statusSecondsLeft = 6.0f;
                }
            } else if (bankFileVersionLoaded < 2) {
                float edge = params[EDGE_PARAM].getValue();
                int fam = 1; // legacy default (Formant)
                int legacyIdx = legacyEdgeSelectedTableIndex(edge, fam);
                params[WT_SELECT_PARAM].setValue((float)legacyIdx);
            }

            // Restore per-operator waveform modes (available since bank version 3)
            for (int oi = 0; oi < 6; ++oi) {
                opWaveMode[oi] = s.opWaveMode[oi];
                opFreqSemi[oi] = s.opFreqSemi[oi];
            }

            // Restore per-LFO settings from preset
            for (int li = 0; li < 2; ++li) {
                for (int oi = 0; oi < 6; ++oi) {
                    lfoRateOp[li][oi]        = s.lfoRateOp[li][oi];
                    lfoPhaseOffsetOp[li][oi] = s.lfoPhaseOffsetOp[li][oi];
                    lfoDeformOp[li][oi]      = s.lfoDeformOp[li][oi];
                    lfoAmpOp[li][oi]         = s.lfoAmpOp[li][oi];
                }
                lfoTargetOp[li] = s.lfoTargetOp[li];
            }

            // Restore per-operator ENV ADSR shapes from preset
            for (int oi = 0; oi < 6; ++oi) {
                envAtkShape[oi] = s.envAtkShape[oi];
                envDecShape[oi] = s.envDecShape[oi];
                envSusShape[oi] = s.envSusShape[oi];
                envRelShape[oi] = s.envRelShape[oi];
            }

            // Force UI resync so OP_WAVE_PARAM reflects the selected operator's stored mode.
            // Without this, the just-loaded OP_WAVE_PARAM value can overwrite opWaveMode.
            lastEditOp   = -1;
            lastLfoMode  = -1;
            lastEnvEditOp = 0;
            lastLfoEditOp[0] = -1;
            lastLfoEditOp[1] = -1;

            params[PANIC_PARAM].setValue(0.0f);
            params[ROLE_SHUFFLE_PARAM].setValue(0.0f);
            params[PRESET_PREV_PARAM].setValue(0.0f);
            params[PRESET_NEXT_PARAM].setValue(0.0f);
            params[PRESET_SAVE_PARAM].setValue(0.0f);
            params[MOD_RND_PARAM].setValue(0.0f);
            params[MOD_MUT_PARAM].setValue(0.0f);
            lastShuffle = false;
        }

        updatePresetDisplayName();

        voice.roleSeed = roleSeed;
        if (applyMidNote && voice.isActive()) {
            voice.applyRoleSeed(roleSeed);
        }

        controlDivider = kControlRate;
    }

    // ════════════════════════════════════════════════════════════════
    // Constructor
    // ════════════════════════════════════════════════════════════════
    Phaseon() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Macro knobs (top half)
        configParam(TIMBRE_PARAM,       0.f, 1.f, 0.28f,  "WT Frame");
        configParam(MOTION_PARAM,       0.f, 1.f, 0.26f,  "Motion");
        configParam(DENSITY_PARAM,      0.f, 1.f, 0.55f,  "Density");
        configParam(EDGE_PARAM,         0.f, 1.f, 1.0f,   "Edge / Digital Harshness");
        configParam(FM_CHARACTER_PARAM,  0.f, 1.f, 0.52f, "FM Character");
        configParam(ALGO_PARAM,         0.f, (float)(kAlgorithmCount - 1), 2.f, "Algorithm");
        paramQuantities[ALGO_PARAM]->snapEnabled = true;

        configParam(MORPH_PARAM,        0.f, 1.f, 0.52f, "Algorithm Morph");
        configParam(COMPLEX_PARAM,      0.f, 1.f, 0.0f,  "Complexity");

        // Normalized knob (0..1) but displayed as 0..4 scenes
        configParam(ENV_STYLE_PARAM,    0.f, 1.f, 0.0f,  "Env Style (Scene Morph)");
        paramQuantities[ENV_STYLE_PARAM]->displayMultiplier = 4.0f;
        configParam(ENV_SPREAD_PARAM,   0.f, 1.f, 1.0f,  "Env Spread");
        // Repurposed: global bitcrusher amount (0 = clean, 1 = heavy crush)
        configParam(SPIKE_PARAM,        0.f, 1.f, 0.0f,  "Bitcrush Amount");
        configButton(ROLE_SHUFFLE_PARAM, "Role Shuffle");

        // Repurposed: macro growl LFO amount
        configParam(TAIL_PARAM,         0.f, 1.f, 0.0f,  "LFO (Growl)");

        // Drift: combined Chaos + Instability (was WT Family)
        configParam(DRIFT_PARAM,        0.f, 1.f, 0.0f,  "Drift (Chaos + Instability)");

        // Cross-operator modulation network (adds coupling on top of the selected algorithm)
        configParam(NETWORK_PARAM,      0.f, 1.f, 0.0f,   "Network (Cross Mod)");

        // Explicit wavetable selector (snapped index into the loaded wavetable bank)
        // Stored by name in the PhaseonBank for VCV ↔ MetaModule portability.
        // Keep the selection range tight so the knob acts like a selector,
        // not a "volume" that clamps to the last table.
        // Current built-in bank indices are 0..10 (see PhaseonWavetable.cpp).
        configParam(WT_SELECT_PARAM,    0.f, 10.f, 0.f,  "WT Select");
        paramQuantities[WT_SELECT_PARAM]->snapEnabled = true;

        // SCRAMBLE: per-operator envelope diversity
        // 0..1 = legacy range, 1..2 = extended "more extreme" range, 2..3 = ultra
        configParam(SCRAMBLE_PARAM,     0.f, 3.f, 0.f,    "Scramble");

        // Warp mode: selects phase remapping function (Serum-style).
        // COMPLEX controls warp depth; this selects the warp shape.
        configParam(WARP_MODE_PARAM,    0.f, 6.f, 0.f,    "Warp Mode");
        paramQuantities[WARP_MODE_PARAM]->snapEnabled = true;

        // Per-operator waveform/frequency editing
        configParam(EDIT_OP_PARAM, 1.f, 6.f, 1.f, "Edit Operator");
        paramQuantities[EDIT_OP_PARAM]->snapEnabled = true;
        // Shared edit knob: normalized 0..1. In WAVE mode this selects
        // discrete waveforms 0..8; in FREQ mode it selects semitone
        // offsets -24..+24. Mapping is handled in OpEditParamQuantity
        // and in the control-rate processing below.
        configParam<OpEditParamQuantity>(OP_WAVE_PARAM, 0.f, 1.f, 0.f, "Operator");
        // Do not use built-in snapping here; we implement our own quantization
        // so that the full 0..1 travel is evenly divided across all modes.
        paramQuantities[OP_WAVE_PARAM]->snapEnabled = false;

        // 2-way switch: 0=WAV, 1=FREQ
        configParam<OpWfModeParamQuantity>(OP_WF_MODE_PARAM, 0.f, 1.f, 0.f, "Op Wav/Freq");
        paramQuantities[OP_WF_MODE_PARAM]->snapEnabled = true;

        // Per-operator frequency editing: semitone offset applied to selected operator ratio.
        configParam(OP_FREQ_PARAM, -24.f, 24.f, 0.f, "Operator Freq");
        paramQuantities[OP_FREQ_PARAM]->snapEnabled = true;

        // TAME: global softener for harshness/clipping
        configParam(TAMING_PARAM, 0.f, 1.f, 0.f, "Tame");

        // Feedback boost: drastic timbre changes with essentially no added CPU.
        // 0..1 = normal range, 1..2 = extra headroom.
        configParam(FEEDBACK_BOOST_PARAM, 0.f, 2.f, 0.f, "Feedback Boost");

        // Mod matrix amount knobs (bipolar; source/dest/target/slew set via right-click menu)
        configParam(MOD1_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 1 Amount");
        configParam(MOD2_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 2 Amount");
        configParam(MOD3_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 3 Amount");
        configParam(MOD4_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 4 Amount");
        configParam(MOD5_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 5 Amount");
        configParam(MOD6_AMT_PARAM,    -1.f, 1.f, 0.f, "Mod 6 Amount");
        configParam(FORMANT_PARAM,      0.f,  1.f, 0.0f, "Formant Amount");

        // Mod matrix routing randomizer (routing only; does not touch depth knobs)
        configButton(MOD_RND_PARAM, "Mod Matrix Randomize");
        configButton(MOD_MUT_PARAM, "Mod Matrix Mutate");
        configParam(MOD_AMT_PARAM,       0.f,  1.f, 0.45f, "Mod Matrix Amount");

        // LFO / ENV controls
        // Shared trimpots: in LFO modes they edit Rate/Phase/Deform/Amp; in ENV mode they edit Attack/Decay/Sustain/Release.
        configParam<LfoRateParamQuantity>(LFO_RATE_PARAM,     0.f, 1.f, 0.5f, "LFO/ENV A");
        configParam(LFO_PHASE_PARAM,    0.f, 1.f, 0.0f, "LFO/ENV D");
        configParam(LFO_DEFORM_PARAM,   0.f, 1.f, 0.5f, "LFO/ENV S");
        configParam(LFO_AMP_PARAM,      0.f, 1.f, 0.5f, "LFO/ENV R");
        configParam<LfoSelectParamQuantity>(LFO_SELECT_PARAM, 0.f, 2.f, 0.f, "LFO / ENV Select");
        paramQuantities[LFO_SELECT_PARAM]->snapEnabled = true;
        configParam<LfoTargetParamQuantity>(LFO_TARGET_PARAM, 0.f, 1.f, 0.f, "LFO/ENV Target");
        paramQuantities[LFO_TARGET_PARAM]->snapEnabled = true;
        configParam(LFO_OP_SELECT_PARAM, 1.f, 6.f, 1.f, "LFO/ENV Op Select");
        paramQuantities[LFO_OP_SELECT_PARAM]->snapEnabled = true;

        // Per-operator level trims (0..1, default 1.0)
        configParam(OP1_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP1 Level");
        configParam(OP2_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP2 Level");
        configParam(OP3_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP3 Level");
        configParam(OP4_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP4 Level");
        configParam(OP5_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP5 Level");
        configParam(OP6_LEVEL_TRIM_PARAM, 0.f, 1.f, 1.f, "OP6 Level");

        // Preset bank browsing (screen buttons)
        configButton(PRESET_PREV_PARAM, "Preset Prev");
        configButton(PRESET_NEXT_PARAM, "Preset Next");
        configButton(PRESET_SAVE_PARAM, "Preset Save");

        configButton(PANIC_PARAM, "Panic / Reset");

        // CV inputs (bottom half)
        configInput(GATE_INPUT,          "Gate");
        configInput(VOCT_INPUT,          "V/Oct");
        configInput(TIMBRE_CV,           "Timbre CV");
        configInput(HARM_DENSITY_CV,     "Density CV");
        configInput(WT_SELECT_CV,        "WT Select CV");
        configInput(TIMBRE2_CV,          "Timbre CV (Perf)");
        configInput(WT_REGION_BIAS_CV,   "WT Region Bias CV");
        configInput(SPECTRAL_TILT_CV,    "Filter (Tilt) CV");
        configInput(MOTION_CV,           "Motion CV");
        configInput(FORMANT_CV,          "Formant CV");
        configInput(ENV_SPREAD_CV,       "Env Spread CV");
        configInput(EDGE_CV,             "Edge CV");
        configInput(COMPLEXITY_CV,       "Complexity CV");
        configInput(MORPH_CV,            "Morph CV");

        configInput(CLOCK_INPUT,         "Clock");

        // Outputs
        configOutput(LEFT_OUTPUT,  "Left");
        configOutput(RIGHT_OUTPUT, "Right");

        configLight(GATE_LIGHT, "Gate indicator");
    }

    // ════════════════════════════════════════════════════════════════
    // Lifecycle
    // ════════════════════════════════════════════════════════════════
    void onAdd() override {
        if (!engineReady) {
            // Generate built-in wavetables
            wavetableBank.generateBuiltins();

            // Append user wavetables (folder-scanned) after builtins.
            // Preset-bank portability VCV ↔ MetaModule depends on having the same
            // wavetable filenames available on both systems.
            loadUserWavetablesOnce();

            voice.reset();
            tiltFilter.reset();
            polish.reset();
            bitcrushKnob01 = 0.0f;
            bitcrushFilter01 = 0.0f;
            bitcrushLpPrevL = bitcrushLpPrevR = 0.0f;
            engineReady = true;
        }

        bankEnsureLoaded();
        bankApplySlot(currentPreset, false);
    }

    void onReset() override {
        voice.reset();
        tiltFilter.reset();
        polish.reset();
        bitcrushKnob01 = 0.0f;
        bitcrushFilter01 = 0.0f;
        bitcrushLpPrevL = bitcrushLpPrevR = 0.0f;
        lastGate = false;
        presetName = "Init";
        roleSeed = 0xC0FFEE11u;
        lastShuffle = false;
        shuffleBurstTimer = 0.0f;
        lastModRnd = false;
        lastModMut = false;
        lastClockHigh = false;
        clockPeriod = 0.0f;
        clockTimer = 0.0f;
        clockPeriodSmoothed = 0.0f;

        for (int oi = 0; oi < 6; ++oi) {
            opWaveMode[oi] = 0;
            opFreqSemi[oi] = 0.0f;
        }

        // Reset LFO settings to defaults
        for (int li = 0; li < 2; ++li) {
            for (int oi = 0; oi < 6; ++oi) {
                lfoRateOp[li][oi]        = 0.5f;
                lfoPhaseOffsetOp[li][oi] = 0.0f;
                lfoDeformOp[li][oi]      = 0.0f;
                lfoAmpOp[li][oi]         = 1.0f;
            }
            lfoTargetOp[li]    = -1;
            lastLfoEditOp[li]  = -1;
        }

        // Reset ENV edit shapes to neutral
        for (int oi = 0; oi < 6; ++oi) {
            envAtkShape[oi] = 0.5f;
            envDecShape[oi] = 0.5f;
            envSusShape[oi] = 0.5f;
            envRelShape[oi] = 0.5f;
        }

        lastLfoMode   = -1;
        lastEnvEditOp = 0;

        bankEnsureLoaded();
        currentPreset = 0;
        bankApplySlot(currentPreset, false);
    }

    // ════════════════════════════════════════════════════════════════
    // Process
    // ════════════════════════════════════════════════════════════════
    void process(const ProcessArgs& args) override {
        if (!engineReady) {
            outputs[LEFT_OUTPUT].setVoltage(0.f);
            outputs[RIGHT_OUTPUT].setVoltage(0.f);
            return;
        }

        // Bank loading is handled in onAdd()/preset actions; avoid any I/O in the audio thread.

        // ── Gate handling (needed for immediate SHUFFLE re-voicing) ─
        bool gate = inputs[GATE_INPUT].getVoltage() > 1.0f;

        // ── Preset bank browse/save (screen buttons) ─────────────
        // Prev/Next browse. Press BOTH together to save to current slot.
        {
            bool prev = params[PRESET_PREV_PARAM].getValue() > 0.5f;
            bool next = params[PRESET_NEXT_PARAM].getValue() > 0.5f;
            bool save = params[PRESET_SAVE_PARAM].getValue() > 0.5f;

            bool both = prev && next;
            bool bothEdge = both && !(lastPresetPrev && lastPresetNext);
            bool saveEdge = save;
            if (saveEdge || bothEdge) {
                bankCaptureCurrentToSlot(currentPreset);
                bankSave();
#ifdef METAMODULE
                MetaModule::Patch::mark_patch_modified();
#endif
            } else {
                if (prev && !lastPresetPrev) {
                    int idx = currentPreset - 1;
                    if (idx < 0) idx = kPresetSlots - 1;
                    bankApplySlot(idx, true);
                }
                if (next && !lastPresetNext) {
                    int idx = currentPreset + 1;
                    if (idx >= kPresetSlots) idx = 0;
                    bankApplySlot(idx, true);
                }
            }

            lastPresetPrev = prev;
            lastPresetNext = next;
        }

        // ── Role shuffle button (applies on next note-on) ───────────
        {
            bool sh = params[ROLE_SHUFFLE_PARAM].getValue() > 0.5f;
            if (sh && !lastShuffle) {
                // Multiple deterministic steps for a larger jump in voicing
                for (int k = 0; k < 4; ++k)
                    roleSeed = roleSeed * 1664525u + 1013904223u;
                roleSeed ^= 0x9E3779B9u;

                // If a note is currently held, apply immediately (no re-trigger)
                if (gate && voice.isActive()) {
                    voice.applyRoleSeed(roleSeed);
                }

                // Shuffle burst: temporary chaos + spike for a dramatic transition
                shuffleBurstTimer = 0.35f; // 350ms
                if (gate && voice.isActive()) {
                    voice.chaosAmount = 0.85f;
                    voice.spike.intensity = 0.40f;
                    voice.spike.trigger();
                }
            }
            lastShuffle = sh;
        }

        // ── Mod matrix randomize/mutate (panel buttons) ───────────
        {
            float amt01 = clamp01(params[MOD_AMT_PARAM].getValue());
            bool rnd = params[MOD_RND_PARAM].getValue() > 0.5f;
            bool mut = params[MOD_MUT_PARAM].getValue() > 0.5f;
            if (rnd && !lastModRnd) {
                modMatrixRandomize(amt01);
#ifdef METAMODULE
                MetaModule::Patch::mark_patch_modified();
#endif
            }
            if (mut && !lastModMut) {
                modMatrixMutate(amt01);
#ifdef METAMODULE
                MetaModule::Patch::mark_patch_modified();
#endif
            }
            lastModRnd = rnd;
            lastModMut = mut;
        }

        // ── Gate handling ───────────────────────────────────────────
        if (gate && !lastGate) {
            // Note on � snap pitch instantly (no glide on attack)
            float voct = inputs[VOCT_INPUT].getVoltage();
            slewedVoct = voct;  // snap to target, no slew
            float freq = 261.63f * powf(2.0f, voct); // C4 = 0V

            // Ensure scene controls are up-to-date for this note-on
            voice.envStyle  = params[ENV_STYLE_PARAM].getValue() * 4.0f;
            voice.envSpread = params[ENV_SPREAD_PARAM].getValue();
            voice.roleBias  = 0.78f;  // hardcoded default (was BIAS knob)
            voice.roleSeed  = roleSeed;

            voice.noteOn(freq, 1.0f);
        } else if (!gate && lastGate) {
            voice.noteOff();
        } else if (gate) {
            // Legato pitch update � slew in V/Oct domain for musical glide
            float voct = inputs[VOCT_INPUT].getVoltage();
            // ~45ms exponential slew (smooth portamento in pitch space)
            const float slewCoeff = 1.0f - expf(-1.0f / (0.045f * args.sampleRate));
            slewedVoct += (voct - slewedVoct) * slewCoeff;
            voice.fundamentalHz = 261.63f * powf(2.0f, slewedVoct);
        }
        lastGate = gate;
        lights[GATE_LIGHT].setBrightness(gate ? 1.0f : 0.0f);

        // -- Clock sync: measure period, override LFO1+LFO2 phase --
        {
            float dt = 1.0f / std::max(1.0f, args.sampleRate);
            bool clockHigh = inputs[CLOCK_INPUT].getVoltage() > 1.0f;
            bool clockConnected = inputs[CLOCK_INPUT].isConnected();
            clockTimer += dt;

            if (clockConnected && clockHigh && !lastClockHigh) {
                // Rising edge: measure period
                if (clockTimer > 0.01f && clockTimer < 10.0f) {
                    // Smooth measurement (1-pole slew, ~4 edges to settle)
                    if (clockPeriodSmoothed < 0.001f)
                        clockPeriodSmoothed = clockTimer;
                    else
                        clockPeriodSmoothed += (clockTimer - clockPeriodSmoothed) * 0.35f;
                    clockPeriod = clockPeriodSmoothed;
                }
                clockTimer = 0.0f;
                // Hard-sync both LFO phases to clock edge (with respective phase offsets)
                voice.lfo1Phase = lfoPhaseOffsetOp[0][0];
                voice.lfo2Phase = lfoPhaseOffsetOp[1][0];
                for (int oi = 0; oi < 6; ++oi) {
                    voice.lfo2PhaseOp[oi] = lfoPhaseOffsetOp[1][oi];
                }
            }
            lastClockHigh = clockHigh;

            // When clock is connected and valid, override both LFO rates.
            // LFO2 rate is ratio-locked to LFO1 based on relative Rate knob settings.
            if (clockConnected && clockPeriod > 0.01f) {
                float clockHz = 1.0f / clockPeriod;
                
                auto getRateMult = [](float v) {
                    if (v >= 0.49f && v <= 0.51f) return 1.0f;
                    if (v > 0.5f) {
                        return 1.0f + (v - 0.5f) * 2.0f * 7.0f;
                    } else {
                        int divs[] = {32, 16, 8, 4, 3, 2};
                        int idx = (int)(v * 2.0f * 5.99f);
                        if (idx < 0) idx = 0;
                        if (idx > 5) idx = 5;
                        return 1.0f / (float)divs[idx];
                    }
                };
                
                // LFO1: clock rate * user rate scaling
                float rate1Scale = getRateMult(lfoRateOp[0][0]);
                voice.lfo1Phase += dt * clockHz * rate1Scale;
                if (voice.lfo1Phase >= 1.0f)
                    voice.lfo1Phase -= 1.0f;
                // LFO2: clock rate * LFO2 rate scaling (independent ratio)
                float rate2Scale = getRateMult(lfoRateOp[1][0]);
                voice.lfo2Phase += dt * clockHz * rate2Scale;
                if (voice.lfo2Phase >= 1.0f)
                    voice.lfo2Phase -= 1.0f;

                // Per-operator LFO2 phases (for per-op rate control)
                for (int oi = 0; oi < 6; ++oi) {
                    float r2 = getRateMult(lfoRateOp[1][oi]);
                    voice.lfo2PhaseOp[oi] += dt * clockHz * r2;
                    if (voice.lfo2PhaseOp[oi] >= 1.0f)
                        voice.lfo2PhaseOp[oi] -= 1.0f;
                }
            }

            // If clock disconnected, clear measurement so free-running resumes
            if (!clockConnected) {
                clockPeriod = 0.0f;
                clockPeriodSmoothed = 0.0f;
            }
        }

        // ── Control rate: update macros ─────────────────────────────
        if (++controlDivider >= kControlRate) {
            controlDivider = 0;

            if (statusSecondsLeft > 0.0f) {
                statusSecondsLeft -= (float)kControlRate / std::max(1.0f, args.sampleRate);
                if (statusSecondsLeft < 0.0f) statusSecondsLeft = 0.0f;
            }

            // Timbre CV is unipolar (0..10V => +0..+1)
            float timbreCv  = std::max(0.0f, inputs[TIMBRE_CV].getVoltage() / 10.0f);
            float timbre2Cv = inputs[TIMBRE2_CV].getVoltage() / 10.0f;  // bipolar, stacks with I/O timbre
            macros.cvTimbre2   = clamp(timbre2Cv, 0.0f, 1.0f);          // keep normalized for mod matrix
            macros.timbre      = clamp(params[TIMBRE_PARAM].getValue() + timbreCv + timbre2Cv, 0.0f, 1.0f);
            macros.motion      = params[MOTION_PARAM].getValue();
            macros.density     = params[DENSITY_PARAM].getValue();
            macros.edge        = params[EDGE_PARAM].getValue();
            macros.fmCharacter = params[FM_CHARACTER_PARAM].getValue();
            macros.algorithmIndex = (int)(params[ALGO_PARAM].getValue() + 0.5f);
            macros.morph       = params[MORPH_PARAM].getValue();
            macros.complex     = params[COMPLEX_PARAM].getValue();

            macros.envStyle    = params[ENV_STYLE_PARAM].getValue() * 4.0f;
            macros.envSpread   = params[ENV_SPREAD_PARAM].getValue();
            // BITCRUSH knob:
            // - actual bitcrush depth is capped (max = previous ~0.4)
            // - simultaneously drives a strong lowpass/high-cut filter to remove harshness
            // Curve: knob=0.5 -> ~80% of max filtering.
            bitcrushKnob01 = clamp01(params[SPIKE_PARAM].getValue());
            macros.spike   = bitcrushKnob01 * 0.30f;
            {
                float k = bitcrushKnob01;
                // 1 - (1-k)^b with b chosen so f(0.5)=0.8
                constexpr float b = 2.3219281f; // log2(5)
                bitcrushFilter01 = 1.0f - powf(1.0f - k, b);
            }
            macros.tail        = params[TAIL_PARAM].getValue();
            macros.drift       = params[DRIFT_PARAM].getValue();
            macros.network     = params[NETWORK_PARAM].getValue();
            macros.wtSelect    = (int)(params[WT_SELECT_PARAM].getValue() + 0.5f);
            macros.scramble    = params[SCRAMBLE_PARAM].getValue();
            macros.warpMode    = (int)(params[WARP_MODE_PARAM].getValue() + 0.5f);

            macros.tame        = params[TAMING_PARAM].getValue();
            macros.feedbackBoost = params[FEEDBACK_BOOST_PARAM].getValue();

            // Per-operator waveform editing sync: EDIT_OP selects which operator the OP_WAVE knob edits.
            int editOp = (int)(params[EDIT_OP_PARAM].getValue() + 0.5f) - 1;
            if (editOp < 0) editOp = 0;
            if (editOp > 5) editOp = 5;

            int wfMode = (params[OP_WF_MODE_PARAM].getValue() > 0.5f) ? 1 : 0; // 0=WAV, 1=FREQ

            if (editOp != lastEditOp || wfMode != lastOpWfMode) {
                // Update knob to reflect stored mode for the newly selected operator.
                if (wfMode == 0) {
                    // Waveform index 0..8 → normalized 0..1
                    int mode = (int)opWaveMode[editOp];
                    if (mode < 0) mode = 0;
                    if (mode > 8) mode = 8;
                    float norm = (float)mode / 8.0f;
                    params[OP_WAVE_PARAM].setValue(norm);
                } else {
                    // Semitone offset -24..+24 → normalized 0..1
                    float semi = opFreqSemi[editOp];
                    if (semi < -24.0f) semi = -24.0f;
                    if (semi >  24.0f) semi =  24.0f;
                    float norm = (semi + 24.0f) / 48.0f;
                    params[OP_WAVE_PARAM].setValue(norm);
                }
                lastEditOp = editOp;
                lastOpWfMode = wfMode;
            }

            float editNorm = params[OP_WAVE_PARAM].getValue();
            if (editNorm < 0.0f) editNorm = 0.0f;
            if (editNorm > 1.0f) editNorm = 1.0f;
            if (wfMode == 0) {
                // WAVE mode: map 0..1 → discrete 0..8 across full knob travel.
                float idxf = editNorm * 8.0f;
                int mode = (int)std::round(idxf);
                if (mode < 0) mode = 0;
                if (mode > 8) mode = 8;
                opWaveMode[editOp] = (uint8_t)mode;
            } else {
                // FREQ mode: map 0..1 → -24..+24 semitone offset.
                float semi = editNorm * 48.0f - 24.0f;
                semi = std::round(semi);
                if (semi < -24.0f) semi = -24.0f;
                if (semi >  24.0f) semi =  24.0f;
                opFreqSemi[editOp] = semi;
            }

            for (int oi = 0; oi < 6; ++oi) {
                macros.opWaveMode[oi] = opWaveMode[oi];
                macros.opFreq[oi] = opFreqSemi[oi];
            }

#ifndef METAMODULE
            // DEBUG: log waveform mode changes
            {
                static uint8_t prevModes[6] = {};
                bool changed = false;
                for (int oi = 0; oi < 6; ++oi) {
                    if (opWaveMode[oi] != prevModes[oi]) { changed = true; break; }
                }
                if (changed) {
                    INFO("Phaseon opWaveMode: [%d,%d,%d,%d,%d,%d] editOp=%d wfMode=%d",
                        opWaveMode[0], opWaveMode[1], opWaveMode[2],
                        opWaveMode[3], opWaveMode[4], opWaveMode[5],
                        editOp, wfMode);
                    for (int oi = 0; oi < 6; ++oi) prevModes[oi] = opWaveMode[oi];
                }
            }
#endif

            // -- LFO / ENV switch-driven sync ----------------------------------
            {
                // 3-way mode: 0=LFO1, 1=LFO2, 2=ENV (per-operator ADSR editor)
                int mode = (int)std::round(params[LFO_SELECT_PARAM].getValue());
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;

                // Current operator selection (1..6 on panel, 0..5 internally)
                int opSel = (int)(params[LFO_OP_SELECT_PARAM].getValue() + 0.5f) - 1;
                if (opSel < 0) opSel = 0;
                if (opSel > 5) opSel = 5;

                // Target selector: 0=ALL, 1=OP. In ENV mode this controls whether
                // ADSR edits apply to all operators or just the selected one.
                int lfoTgt = (params[LFO_TARGET_PARAM].getValue() > 0.5f) ? 1 : 0; // 0=ALL, 1=OP

                auto lfoWriteFromTrimpots = [&](int li, int editOp) {
                    float r = clamp01(params[LFO_RATE_PARAM].getValue());
                    float p = clamp01(params[LFO_PHASE_PARAM].getValue());
                    float d = clamp01(params[LFO_DEFORM_PARAM].getValue());
                    float a = clamp01(params[LFO_AMP_PARAM].getValue());
                    if (editOp < -1) editOp = -1;
                    if (editOp > 5) editOp = 5;
                    if (li < 0) li = 0;
                    if (li > 1) li = 1;
                    if (editOp == -1) {
                        for (int oi = 0; oi < 6; ++oi) {
                            lfoRateOp[li][oi]        = r;
                            lfoPhaseOffsetOp[li][oi] = p;
                            lfoDeformOp[li][oi]      = d;
                            lfoAmpOp[li][oi]         = a;
                        }
                    } else {
                        lfoRateOp[li][editOp]        = r;
                        lfoPhaseOffsetOp[li][editOp] = p;
                        lfoDeformOp[li][editOp]      = d;
                        lfoAmpOp[li][editOp]         = a;
                    }
                };

                auto lfoLoadToTrimpots = [&](int li, int editOp) {
                    if (editOp < -1) editOp = -1;
                    if (editOp > 5) editOp = 5;
                    if (li < 0) li = 0;
                    if (li > 1) li = 1;
                    int srcOp = (editOp == -1) ? 0 : editOp;
                    params[LFO_RATE_PARAM].setValue(lfoRateOp[li][srcOp]);
                    params[LFO_PHASE_PARAM].setValue(lfoPhaseOffsetOp[li][srcOp]);
                    params[LFO_DEFORM_PARAM].setValue(lfoDeformOp[li][srcOp]);
                    params[LFO_AMP_PARAM].setValue(lfoAmpOp[li][srcOp]);
                };

                if (mode == 2) {
                    // ENV edit mode: trimpots edit per-op ADSR shapes under the existing envelopes.

                    // If coming from an LFO mode, persist the last LFO's trimpot values.
                    if (lastLfoMode == 0 || lastLfoMode == 1) {
                        int lastIdx = (lastLfoMode < 0) ? 0 : lastLfoMode;
                        int prevEditOp = lastLfoEditOp[lastIdx];
                        lfoWriteFromTrimpots(lastIdx, prevEditOp);
                    }

                    if (lfoTgt == 0) {
                        // ALL operators: trimpots control a shared ADSR shape applied to all ops.
                        // On first enter to ENV (from LFO), sync knobs from operator 0.
                        if (lastLfoMode != 2) {
                            params[LFO_RATE_PARAM].setValue(envAtkShape[0]);
                            params[LFO_PHASE_PARAM].setValue(envDecShape[0]);
                            params[LFO_DEFORM_PARAM].setValue(envSusShape[0]);
                            params[LFO_AMP_PARAM].setValue(envRelShape[0]);
                        }

                        float a = clamp01(params[LFO_RATE_PARAM].getValue());
                        float d = clamp01(params[LFO_PHASE_PARAM].getValue());
                        float s = clamp01(params[LFO_DEFORM_PARAM].getValue());
                        float r = clamp01(params[LFO_AMP_PARAM].getValue());
                        for (int oi = 0; oi < 6; ++oi) {
                            envAtkShape[oi] = a;
                            envDecShape[oi] = d;
                            envSusShape[oi] = s;
                            envRelShape[oi] = r;
                        }
                    }
                    else {
                        // Single-operator edit: trimpots only affect the selected operator.

                        // If ENV mode just entered or operator changed, sync trimpots to that op's shapes.
                        if (lastLfoMode != 2 || opSel != lastEnvEditOp) {
                            if (lastLfoMode == 2) {
                                // Commit shapes for previously edited operator.
                                int prevOp = lastEnvEditOp;
                                if (prevOp < 0) prevOp = 0;
                                if (prevOp > 5) prevOp = 5;
                                envAtkShape[prevOp] = clamp01(params[LFO_RATE_PARAM].getValue());
                                envDecShape[prevOp] = clamp01(params[LFO_PHASE_PARAM].getValue());
                                envSusShape[prevOp] = clamp01(params[LFO_DEFORM_PARAM].getValue());
                                envRelShape[prevOp] = clamp01(params[LFO_AMP_PARAM].getValue());
                            }

                            // Load current operator's shapes into the trimpots.
                            params[LFO_RATE_PARAM].setValue(envAtkShape[opSel]);
                            params[LFO_PHASE_PARAM].setValue(envDecShape[opSel]);
                            params[LFO_DEFORM_PARAM].setValue(envSusShape[opSel]);
                            params[LFO_AMP_PARAM].setValue(envRelShape[opSel]);
                            lastEnvEditOp = opSel;
                        }

                        // Continuously write trimpots back into the selected operator's shapes.
                        envAtkShape[opSel] = clamp01(params[LFO_RATE_PARAM].getValue());
                        envDecShape[opSel] = clamp01(params[LFO_PHASE_PARAM].getValue());
                        envSusShape[opSel] = clamp01(params[LFO_DEFORM_PARAM].getValue());
                        envRelShape[opSel] = clamp01(params[LFO_AMP_PARAM].getValue());
                    }
                }
                else {
                    // LFO1 / LFO2 edit modes
                    int lfoSel = mode; // 0 or 1
                    if (lfoSel < 0) lfoSel = 0;
                    if (lfoSel > 1) lfoSel = 1;

                    // Current editing target: -1 = ALL, 0..5 = operator
                    int editOp = (lfoTgt == 0) ? -1 : opSel;

                    // If we were already in an LFO mode and the context (LFO or target op) changed,
                    // commit the current trimpots into the previous context before switching.
                    if (lastLfoMode == 0 || lastLfoMode == 1) {
                        int prevLfo = lastLfoMode;
                        int prevEditOp = lastLfoEditOp[prevLfo];
                        if (prevLfo != lfoSel || prevEditOp != editOp) {
                            lfoWriteFromTrimpots(prevLfo, prevEditOp);
                        }
                    }

                    // If switching between LFOs, coming from ENV, or changing target op,
                    // load the new context into the trimpots.
                    if (lastLfoMode != lfoSel || lastLfoMode == 2 || lastLfoEditOp[lfoSel] != editOp) {
                        lfoLoadToTrimpots(lfoSel, editOp);
                    }

                    // Continuously write current trimpot values back to the selected LFO's storage
                    lfoWriteFromTrimpots(lfoSel, editOp);
                    lastLfoEditOp[lfoSel] = editOp;

                    // Target operator: -1=ALL, 0..5=specific
                    if (lfoTgt == 0) {
                        lfoTargetOp[lfoSel] = -1;
                    } else {
                        lfoTargetOp[lfoSel] = opSel;
                    }
                }

                lastLfoMode = mode;

                // Copy all LFO params to macros (ENV shapes are handled separately below)
                for (int li = 0; li < 2; ++li) {
                    macros.lfoTargetOp[li] = lfoTargetOp[li];
                    for (int oi = 0; oi < 6; ++oi) {
                        macros.lfoRate[li][oi]        = lfoRateOp[li][oi];
                        macros.lfoPhaseOffset[li][oi] = lfoPhaseOffsetOp[li][oi];
                        macros.lfoDeform[li][oi]      = lfoDeformOp[li][oi];
                        macros.lfoAmp[li][oi]         = lfoAmpOp[li][oi];
                    }
                }

                // Per-operator ENV ADSR shapes for applyMacros()
                for (int oi = 0; oi < 6; ++oi) {
                    macros.envAtkShape[oi] = envAtkShape[oi];
                    macros.envDecShape[oi] = envDecShape[oi];
                    macros.envSusShape[oi] = envSusShape[oi];
                    macros.envRelShape[oi] = envRelShape[oi];
                }

                // Per-operator level trims (OP1..OP6 volume trimpots)
                for (int oi = 0; oi < 6; ++oi) {
                    macros.opLevelTrim[oi] = clamp01(params[OP1_LEVEL_TRIM_PARAM + oi].getValue());
                }
            }

            // CV inputs (bipolar = /5V, unipolar = /10V)
            macros.cvHarmonicDensity = inputs[HARM_DENSITY_CV].getVoltage() / 5.0f;
            macros.cvWtSelect        = clamp(inputs[WT_SELECT_CV].getVoltage() / 10.0f, 0.0f, 1.0f);
            macros.cvMotion          = clamp(inputs[MOTION_CV].getVoltage() / 10.0f, 0.0f, 1.0f);
            macros.cvWtRegionBias    = inputs[WT_REGION_BIAS_CV].getVoltage() / 5.0f;
            macros.cvSpectralTilt    = inputs[SPECTRAL_TILT_CV].getVoltage() / 5.0f;
            macros.formant           = params[FORMANT_PARAM].getValue();
            macros.cvFormant         = clamp(inputs[FORMANT_CV].getVoltage() / 5.0f, -1.0f, 1.0f);
            macros.cvEnvSpread       = clamp(inputs[ENV_SPREAD_CV].getVoltage() / 10.0f, 0.0f, 1.0f);
            macros.cvEdge            = clamp(inputs[EDGE_CV].getVoltage() / 10.0f, 0.0f, 1.0f);
            macros.cvComplexity      = clamp(inputs[COMPLEXITY_CV].getVoltage() / 10.0f, 0.0f, 1.0f);
            macros.cvMorph           = clamp(inputs[MORPH_CV].getVoltage() / 5.0f, -1.0f, 1.0f);

            // Feed trimpot values into mod matrix slot amounts (the one missing wire)
            for (int i = 0; i < phaseon::kModSlots; ++i)
                macros.modMatrix.slots[i].amount = params[MOD1_AMT_PARAM + i].getValue();

            // Spectral tilt filter
            // Combine Tilt CV with BITCRUSH-driven darkness.
            // Increasing BITCRUSH makes the sound progressively more filtered.
            float dark = bitcrushFilter01;
            tiltFilter.tilt = clamp(macros.cvSpectralTilt - dark, -1.0f, 1.0f);

            // Apply macros → voice parameters
            applyMacros(voice, macros, wavetableBank, args.sampleRate);

#ifndef METAMODULE
            // DEBUG: log tableIndex after applyMacros
            {
                static int prevTable[6] = {-999,-999,-999,-999,-999,-999};
                bool changed = false;
                for (int oi = 0; oi < 6; ++oi) {
                    if (voice.ops[oi].tableIndex != prevTable[oi]) { changed = true; break; }
                }
                if (changed) {
                    INFO("Phaseon tableIndex: [%d,%d,%d,%d,%d,%d]",
                        voice.ops[0].tableIndex, voice.ops[1].tableIndex, voice.ops[2].tableIndex,
                        voice.ops[3].tableIndex, voice.ops[4].tableIndex, voice.ops[5].tableIndex);
                    for (int oi = 0; oi < 6; ++oi) prevTable[oi] = voice.ops[oi].tableIndex;
                }
            }
#endif

            // Shuffle burst: decay chaos/spike overlay over ~200ms
            if (shuffleBurstTimer > 0.0f) {
                float blockDt = (float)kControlRate / std::max(1.0f, args.sampleRate);
                shuffleBurstTimer -= blockDt;
                if (shuffleBurstTimer <= 0.0f) {
                    shuffleBurstTimer = 0.0f;
                } else {
                    float t = shuffleBurstTimer / 0.35f; // 1..0 over 350ms
                    voice.chaosAmount = std::max(voice.chaosAmount, 0.85f * t);
                }
            }
        }

        // Panic button
        if (params[PANIC_PARAM].getValue() > 0.5f) {
            voice.reset();
            tiltFilter.reset();
            bitcrushLpPrevL = bitcrushLpPrevR = 0.0f;
        }

        // ── Audio ───────────────────────────────────────────────────
        voice.tick(wavetableBank, args.sampleRate);

        float outL = voice.outL;
        float outR = voice.outR;

        // Post-processing: spectral tilt
        tiltFilter.process(outL, outR);

        // Additional BITCRUSH-coupled high-cut filter.
        // At max BITCRUSH this should remove essentially all harshness.
        if (bitcrushFilter01 > 0.0005f) {
            float t = clamp01(bitcrushFilter01);
            // One-pole coefficient: 1.0 = bypass, ~0.02 = very strong lowpass
            float coeff = 1.0f - 0.98f * t;
            if (coeff < 0.02f) coeff = 0.02f;
            outL = bitcrushLpPrevL + coeff * (outL - bitcrushLpPrevL);
            outR = bitcrushLpPrevR + coeff * (outR - bitcrushLpPrevR);
            bitcrushLpPrevL = outL;
            bitcrushLpPrevR = outR;
        } else {
            bitcrushLpPrevL = outL;
            bitcrushLpPrevR = outR;
        }

        // Makeup gain to compensate filtering loudness loss.
        // 0..+12 dB as filtering increases to max.
        if (bitcrushFilter01 > 0.0005f) {
            float t = clamp01(bitcrushFilter01);
            float gain = powf(10.0f, (12.0f * t) / 20.0f);
            outL *= gain;
            outR *= gain;
        }

        // Output polish (finished / punchy / slightly dangerous)
        // Prepare is cheap and keeps state stable across sample rate changes.
        polish.prepare(args.sampleRate);
        polish.process(outL, outR, voice.fundamentalHz, macros.edge, args.sampleRate);

        // Scale to Rack levels (±5V nominal)
        outputs[LEFT_OUTPUT].setVoltage(outL * 5.0f);
        outputs[RIGHT_OUTPUT].setVoltage(outR * 5.0f);
    }

    // ════════════════════════════════════════════════════════════════
    // Serialization
    // ════════════════════════════════════════════════════════════════
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "presetName", json_string(presetName.c_str()));
        json_object_set_new(rootJ, "roleSeed", json_integer((json_int_t)roleSeed));
        json_object_set_new(rootJ, "currentPreset", json_integer((json_int_t)currentPreset));

        // Mod matrix slot configs (amounts are Rack params; src/dst/target/slew serialised here)
        json_t* mmJ = json_array();
        for (int i = 0; i < phaseon::kModSlots; ++i) {
            const phaseon::ModSlot& s = macros.modMatrix.slots[i];
            json_t* slotJ = json_object();
            json_object_set_new(slotJ, "src",    json_integer((int)s.src));
            json_object_set_new(slotJ, "dst",    json_integer((int)s.dst));
            json_object_set_new(slotJ, "target", json_integer((int)s.target));
            json_object_set_new(slotJ, "slew",   json_real(s.slewMs));
            json_array_append_new(mmJ, slotJ);
        }
        json_object_set_new(rootJ, "modMatrix", mmJ);

        // Per-operator waveform modes (patch persistence)
        {
            json_t* owJ = json_array();
            for (int oi = 0; oi < 6; ++oi) {
                json_array_append_new(owJ, json_integer((int)opWaveMode[oi]));
            }
            json_object_set_new(rootJ, "opWaveMode", owJ);
        }

        // Per-operator frequency semitone offsets (patch persistence)
        {
            json_t* ofJ = json_array();
            for (int oi = 0; oi < 6; ++oi) {
                json_array_append_new(ofJ, json_real((double)opFreqSemi[oi]));
            }
            json_object_set_new(rootJ, "opFreq", ofJ);
        }

        // Per-LFO settings (patch persistence)
        {
            json_t* lrJ = json_array();
            json_t* lpJ = json_array();
            json_t* ldJ = json_array();
            json_t* laJ = json_array();
            json_t* ltJ = json_array();
            for (int li = 0; li < 2; ++li) {
                json_t* lrOpJ = json_array();
                json_t* lpOpJ = json_array();
                json_t* ldOpJ = json_array();
                json_t* laOpJ = json_array();
                for (int oi = 0; oi < 6; ++oi) {
                    json_array_append_new(lrOpJ, json_real((double)lfoRateOp[li][oi]));
                    json_array_append_new(lpOpJ, json_real((double)lfoPhaseOffsetOp[li][oi]));
                    json_array_append_new(ldOpJ, json_real((double)lfoDeformOp[li][oi]));
                    json_array_append_new(laOpJ, json_real((double)lfoAmpOp[li][oi]));
                }
                json_array_append_new(lrJ, lrOpJ);
                json_array_append_new(lpJ, lpOpJ);
                json_array_append_new(ldJ, ldOpJ);
                json_array_append_new(laJ, laOpJ);
                json_array_append_new(ltJ, json_integer(lfoTargetOp[li]));
            }
            json_object_set_new(rootJ, "lfoRate", lrJ);
            json_object_set_new(rootJ, "lfoPhase", lpJ);
            json_object_set_new(rootJ, "lfoDeform", ldJ);
            json_object_set_new(rootJ, "lfoAmp", laJ);
            json_object_set_new(rootJ, "lfoTarget", ltJ);
        }

        // Per-operator ENV ADSR shapes (patch persistence)
        {
            json_t* aJ = json_array();
            json_t* dJ = json_array();
            json_t* suJ = json_array();
            json_t* rJ = json_array();
            for (int oi = 0; oi < 6; ++oi) {
                json_array_append_new(aJ, json_real((double)envAtkShape[oi]));
                json_array_append_new(dJ, json_real((double)envDecShape[oi]));
                json_array_append_new(suJ, json_real((double)envSusShape[oi]));
                json_array_append_new(rJ, json_real((double)envRelShape[oi]));
            }
            json_object_set_new(rootJ, "envAtkShape", aJ);
            json_object_set_new(rootJ, "envDecShape", dJ);
            json_object_set_new(rootJ, "envSusShape", suJ);
            json_object_set_new(rootJ, "envRelShape", rJ);
        }

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* nameJ = json_object_get(rootJ, "presetName");
        if (nameJ) presetName = json_string_value(nameJ);

        json_t* seedJ = json_object_get(rootJ, "roleSeed");
        if (seedJ) roleSeed = (uint32_t)json_integer_value(seedJ);

        json_t* curJ = json_object_get(rootJ, "currentPreset");
        if (curJ && json_is_integer(curJ)) currentPreset = (int)json_integer_value(curJ);
        if (currentPreset < 0) currentPreset = 0;
        if (currentPreset >= kPresetSlots) currentPreset = kPresetSlots - 1;

        // Mod matrix slot configs
        json_t* mmJ = json_object_get(rootJ, "modMatrix");
        if (mmJ && json_is_array(mmJ)) {
            int n = (int)json_array_size(mmJ);
            for (int i = 0; i < phaseon::kModSlots && i < n; ++i) {
                json_t* sJ = json_array_get(mmJ, i);
                if (!sJ || !json_is_object(sJ)) continue;
                phaseon::ModSlot& s = macros.modMatrix.slots[i];
                json_t* srcJ = json_object_get(sJ, "src");
                if (srcJ && json_is_integer(srcJ)) {
                    int v = (int)json_integer_value(srcJ);
                    if (v < 0) v = 0;
                    if (v >= (int)phaseon::ModSource::kCount) v = 0;
                    s.src = (phaseon::ModSource)v;
                }
                json_t* dstJ = json_object_get(sJ, "dst");
                if (dstJ && json_is_integer(dstJ)) {
                    int v = (int)json_integer_value(dstJ);
                    if (v < 0) v = 0;
                    if (v >= (int)phaseon::ModDest::kCount) v = 0;
                    s.dst = (phaseon::ModDest)v;
                }
                json_t* tgtJ = json_object_get(sJ, "target");
                if (tgtJ && json_is_integer(tgtJ)) {
                    int v = (int)json_integer_value(tgtJ);
                    const int kTargetMax = 8; // All=0 .. Op6=8
                    if (v < 0) v = 0;
                    if (v > kTargetMax) v = 0;
                    s.target = (phaseon::ModOpTarget)v;
                }
                json_t* slewJ = json_object_get(sJ, "slew");
                if (slewJ && json_is_number(slewJ))
                    s.slewMs = std::max(0.0f, std::min(500.0f, (float)json_number_value(slewJ)));
            }
        }

        // Per-operator waveform modes
        json_t* owJ = json_object_get(rootJ, "opWaveMode");
        if (owJ && json_is_array(owJ)) {
            int n = (int)json_array_size(owJ);
            for (int oi = 0; oi < 6 && oi < n; ++oi) {
                json_t* vJ = json_array_get(owJ, oi);
                if (!vJ || !json_is_integer(vJ))
                    continue;
                int v = (int)json_integer_value(vJ);
                if (v < 0) v = 0;
                if (v > 8) v = 8;
                opWaveMode[oi] = (uint8_t)v;
            }
        }

        // Per-operator frequency semitone offsets
        json_t* ofJ = json_object_get(rootJ, "opFreq");
        if (ofJ && json_is_array(ofJ)) {
            int n = (int)json_array_size(ofJ);
            for (int oi = 0; oi < 6 && oi < n; ++oi) {
                json_t* vJ = json_array_get(ofJ, oi);
                if (!vJ || !json_is_number(vJ))
                    continue;
                float v = (float)json_number_value(vJ);
                if (v < -24.0f) v = -24.0f;
                if (v >  24.0f) v =  24.0f;
                opFreqSemi[oi] = v;
            }
        }

        // Force UI resync on next control-rate tick.
        lastEditOp    = -1;
        lastLfoMode   = -1;
        lastEnvEditOp = 0;
        lastLfoEditOp[0] = -1;
        lastLfoEditOp[1] = -1;

        // Per-LFO settings (per-operator). Backward-compatible with legacy scalar arrays.
        auto readLfoNested = [&](const char* key, float dst[2][6], float lo, float hi) {
            json_t* aJ = json_object_get(rootJ, key);
            if (!aJ || !json_is_array(aJ))
                return;
            int n = (int)json_array_size(aJ);
            for (int li = 0; li < 2 && li < n; ++li) {
                json_t* vJ = json_array_get(aJ, li);
                if (!vJ)
                    continue;
                if (json_is_number(vJ)) {
                    float v = (float)json_number_value(vJ);
                    if (v < lo) v = lo;
                    if (v > hi) v = hi;
                    for (int oi = 0; oi < 6; ++oi)
                        dst[li][oi] = v;
                }
                else if (json_is_array(vJ)) {
                    int on = (int)json_array_size(vJ);
                    for (int oi = 0; oi < 6 && oi < on; ++oi) {
                        json_t* eJ = json_array_get(vJ, oi);
                        if (!eJ || !json_is_number(eJ))
                            continue;
                        float v = (float)json_number_value(eJ);
                        if (v < lo) v = lo;
                        if (v > hi) v = hi;
                        dst[li][oi] = v;
                    }
                }
            }
        };
        readLfoNested("lfoRate",   lfoRateOp,        0.0f, 1.0f);
        readLfoNested("lfoPhase",  lfoPhaseOffsetOp, 0.0f, 1.0f);
        readLfoNested("lfoDeform", lfoDeformOp,      0.0f, 1.0f);
        readLfoNested("lfoAmp",    lfoAmpOp,         0.0f, 1.0f);
        {
            json_t* ltJ = json_object_get(rootJ, "lfoTarget");
            if (ltJ && json_is_array(ltJ)) {
                int n = (int)json_array_size(ltJ);
                for (int li = 0; li < 2 && li < n; ++li) {
                    json_t* vJ = json_array_get(ltJ, li);
                    if (vJ && json_is_integer(vJ)) {
                        int v = (int)json_integer_value(vJ);
                        if (v < -1) v = -1;
                        if (v > 5) v = 5;
                        lfoTargetOp[li] = v;
                    }
                }
            }
        }

        // Per-operator ENV ADSR shapes
        {
            auto readEnv = [&](const char* key, float* dst) {
                json_t* aJ = json_object_get(rootJ, key);
                if (!aJ || !json_is_array(aJ))
                    return;
                int n2 = (int)json_array_size(aJ);
                for (int oi = 0; oi < 6 && oi < n2; ++oi) {
                    json_t* vJ = json_array_get(aJ, oi);
                    if (!vJ || !json_is_number(vJ))
                        continue;
                    dst[oi] = clamp01((float)json_number_value(vJ));
                }
            };
            readEnv("envAtkShape", envAtkShape);
            readEnv("envDecShape", envDecShape);
            readEnv("envSusShape", envSusShape);
            readEnv("envRelShape", envRelShape);
        }
    }

#ifdef METAMODULE
    size_t get_display_text(int display_id, std::span<char> text) override {
        if (display_id != PRESET_DISPLAY || text.empty()) return 0;
        const char* s = displayTextCstr();
        size_t len = std::strlen(s);
        size_t n = std::min(len, text.size());
        std::memcpy(text.data(), s, n);
        return n;
    }
#endif
};

std::string OpEditParamQuantity::getDisplayValueString() {
    Phaseon* mod = dynamic_cast<Phaseon*>(module);
    if (!mod) {
        return ParamQuantity::getDisplayValueString();
    }
    int wfMode = (mod->params[Phaseon::OP_WF_MODE_PARAM].getValue() > 0.5f) ? 1 : 0; // 0=WAV, 1=FREQ
    if (wfMode == 0) {
        float v = getValue();
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        float idxf = v * 8.f;
        int idx = (int)std::round(idxf);
        if (idx < 0) idx = 0;
        if (idx > 8) idx = 8;
        static const char* kNames[] = {"WT", "SINE", "TRI", "SAW", "HARM", "SKEW", "SQR", "WARP", "RECT"};
        return kNames[idx];
    }
    // FREQ mode: map normalized 0..1 → -24..+24 semitone offset
    float v = getValue();
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    float semiF = v * 48.f - 24.f;
    int semi = (int)std::round(semiF);
    if (semi < -24) semi = -24;
    if (semi >  24) semi =  24;
    return std::to_string(semi) + " st";
}

std::string OpWfModeParamQuantity::getDisplayValueString() {
    int wfMode = (getValue() > 0.5f) ? 1 : 0;
    return wfMode == 0 ? "Waveform" : "Freq";
}

std::string LfoRateParamQuantity::getDisplayValueString() {
    Phaseon* mod = dynamic_cast<Phaseon*>(module);
    if (!mod) {
        return ParamQuantity::getDisplayValueString();
    }
    
    // If in ENV mode, show 0..1
    int mode = (int)(mod->params[Phaseon::LFO_SELECT_PARAM].getValue() + 0.5f);
    if (mode == 2) {
        return string::f("%.2f", getValue());
    }
    
    float v = getValue();
    if (v >= 0.49f && v <= 0.51f) {
        return "x1";
    } else if (v > 0.5f) {
        // 0.5..1.0 maps to x1..x8
        float mult = 1.0f + (v - 0.5f) * 2.0f * 7.0f;
        return string::f("x%.1f", mult);
    } else {
        // 0.0..0.5 maps to /32, /16, /8, /4, /3, /2
        int divs[] = {32, 16, 8, 4, 3, 2};
        int idx = (int)(v * 2.0f * 5.99f);
        if (idx < 0) idx = 0;
        if (idx > 5) idx = 5;
        return "/" + std::to_string(divs[idx]);
    }
}

// ════════════════════════════════════════════════════════════════════
// Widget
// ════════════════════════════════════════════════════════════════════

#ifndef METAMODULE
// NanoVG label helper (same style as other MorphWorx modules)
struct PhaseonLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool isTitle;

    PhaseonLabel(Vec pos, const char* txt, float fs, NVGcolor col, bool title = false) {
        box.pos = pos;
        box.size = Vec(120, fs + 4);
        this->text = txt;
        this->fontSize = fs;
        this->color = col;
        this->isTitle = title;
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
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 0, fontSize * 0.5f, text.c_str(), NULL);
    }
};

// Centered title label (Cinzel font)
struct PhaseonTitleLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;

    PhaseonTitleLabel(Vec centerPos, Vec size, const char* txt, float fs, NVGcolor col) {
        box.size = size;
        box.pos = centerPos.minus(size.mult(0.5f));
        text = txt ? txt : "";
        fontSize = fs;
        color = col;
    }

    void draw(const DrawArgs& args) override {
        std::string fontPath = asset::plugin(pluginInstance, "res/CinzelDecorative-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        // Thicken headline without changing colors/fonts.
        float x = box.size.x * 0.5f;
        float y = box.size.y * 0.5f;
        nvgText(args.vg, x, y, text.c_str(), NULL);
        nvgText(args.vg, x + 0.7f, y, text.c_str(), NULL);
    }
};

// Subtitle under the headline (Rajdhani font)
struct PhaseonSubtitleLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;

    PhaseonSubtitleLabel(Vec centerPos, Vec size, const char* txt, float fs, NVGcolor col) {
        box.size = size;
        box.pos = centerPos.minus(size.mult(0.5f));
        text = txt ? txt : "";
        fontSize = fs;
        color = col;
    }

    void draw(const DrawArgs& args) override {
        std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

// Preset name display widget
struct PhaseonDisplay : TransparentWidget {
    Phaseon* module = nullptr;
    bool drawBackground = true;

    PhaseonDisplay(Vec pos, Vec size, bool drawBg = true) {
        box.pos = pos;
        box.size = size;
		drawBackground = drawBg;
    }

    void draw(const DrawArgs& args) override {
        if (drawBackground) {
            // Background (matches Minimalith display style)
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.0f);
            nvgFillColor(args.vg, nvgRGB(15, 15, 25));
            nvgFill(args.vg);

            // Border (neon green matching Minimalith)
            nvgStrokeColor(args.vg, nvgRGB(167, 255, 196));
            nvgStrokeWidth(args.vg, 0.8f);
            nvgStroke(args.vg);
        }

        // Text
        std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 12.0f * 1.30f * 1.35f);
		nvgFillColor(args.vg, nvgRGB(0x00, 0xff, 0x00));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        const char* name = "Phaseon";
        if (module) name = module->displayTextCstr();
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f + 0.0f, name, NULL);
    }
};

// Centered label helper for control annotations (ports/knobs/outs)
struct PhaseonCenteredLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;

    PhaseonCenteredLabel(Vec centerPos, Vec size, const char* txt, float fs, NVGcolor col) {
        box.size = size;
        box.pos = centerPos.minus(size.mult(0.5f));
        text = txt ? txt : "";
        fontSize = fs;
        color = col;
    }

    void draw(const DrawArgs& args) override {
        std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x * 0.5f, fontSize * 0.5f, text.c_str(), NULL);
    }
};
#endif

struct PhaseonWidget : ModuleWidget {
    PhaseonWidget(Phaseon* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Phaseon.svg")));

#ifndef METAMODULE
    // PNG faceplate for VCV Rack UI. Keep the SVG panel for correct sizing.
    auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Phaseon.png"));
    panelBg->box.pos = Vec(0, 0);
    panelBg->box.size = box.size;
    addChild(panelBg);
#endif

#ifndef METAMODULE
    struct PhaseonPortWidget : MVXPort {
            PhaseonPortWidget() {
                imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_dark.png");
                imageHandle = -1;
            }
        };
#else
    using PhaseonPortWidget = MVXPort;
#endif

        // ── Panel layout: 20 HP = 101.6mm ───────────────────────────
        // Minimalith neon green labels: nvgRGB(167, 255, 196)
        const NVGcolor labelColor  = nvgRGB(167, 255, 196);

        // Move all text down by 4px
        // Global label text shift (user request: move all knob text down 3px)
        const Vec textShiftPx = Vec(0.0f, 7.0f);

        const float panelW = 101.6f;
        const float centerX = panelW * 0.5f;   // 50.8

        // ── Label colours (row-based, matches panel "sections") ──
        // Row 1 (WT SEL .. DENSITY): GREEN
        // Row 2 (FM CHAR .. PUNCH): RED
        // Row 3 (MOTION .. DRIFT):  BLUE
        // Row 4 (ENV .. SCRAMBLE):  YELLOW
        // Row 5 (EDIT OP .. TAME):  CYAN
        const NVGcolor cRowGreen  = nvgRGB(180, 255, 140);
        const NVGcolor cRowRed    = nvgRGB(255,  60,  60);
        const NVGcolor cRowBlue   = nvgRGB(120, 180, 255);
        const NVGcolor cRowYellow = nvgRGB(255, 210,  60);
        const NVGcolor cRowCyan   = nvgRGB( 80, 255, 255);

        const NVGcolor cTilt    = nvgRGB(200, 180, 255);  // lavender — TILT CV
        const NVGcolor cModKnob = nvgRGB(255, 255, 255);  // white — M1–M6 + MTRX AMT labels

        // Black macro knobs: tight left block, 11 mm pitch, 4 columns
        const float mcol1    =  9.0f;
        const float mcol2    = 20.0f;
        const float mcol3    = 31.0f;
        const float mcolTail = 42.0f;

        // ── TOP: Title + Display ────────────────────────────────────
#ifdef METAMODULE
        // MetaModule preset name text display (rendered above the faceplate).
        {
            struct PhaseonMMDisplay : MetaModule::VCVTextDisplay {};
            auto* mmDisplay = new PhaseonMMDisplay();
            // Faceplate screen window moved up; shift text up accordingly.
            mmDisplay->box.pos = mm2px(Vec(10.0f, 16.0f)).plus(Vec(0.0f, -15.0f));
            mmDisplay->box.size = mm2px(Vec(panelW - 20.0f, 12.0f));
            mmDisplay->font = "Default_14";
            mmDisplay->color = Colors565::Green;
            mmDisplay->firstLightId = Phaseon::PRESET_DISPLAY;
            addChild(mmDisplay);
        }
#endif
#ifndef METAMODULE
    // Preset name text: draw over the faceplate (screen art is on the PNG).
    {
        // Faceplate screen window moved up; shift text up accordingly.
        Vec dispPos = mm2px(Vec(6.0f, 17.0f)).plus(textShiftPx).plus(Vec(0.0f, -15.0f));
        Vec dispSize = mm2px(Vec(panelW - 12.0f, 8.0f));
        // Shrink horizontally by 30%, keep centered.
        float fullW = dispSize.x;
        float newW = fullW * 0.70f;
        dispPos.x += (fullW - newW) * 0.5f;
        dispSize.x = newW;

        PhaseonDisplay* disp = new PhaseonDisplay(dispPos, dispSize, /*drawBg*/ false);
        disp->module = module;
        addChild(disp);
    }

        // Preset browse buttons (Prev/Next). Press both together to SAVE to slot.
        {
            // Centered row under the screen, with a small 5px drop so nothing touches the border.
            const float btnY = 28.0f;
            const float btnDx = 10.0f;
            // Move buttons up by 15px (5px drop - 15px = -10px).
            const Vec presetBtnShiftPx = Vec(0.0f, -11.0f);

            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX - btnDx, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_PREV_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_SAVE_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX + btnDx, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_NEXT_PARAM));

            const Vec bLabelSize = Vec(28.0f, 10.0f);
            // Keep labeling consistent with the rest of the panel: text above the control.
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX - btnDx, btnY - 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx).plus(Vec(0.f, 6.f)), bLabelSize, "<", 10.0f, labelColor));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX + btnDx, btnY - 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx).plus(Vec(0.f, 6.f)), bLabelSize, ">", 10.0f, labelColor));

            const Vec saveLabelSize = Vec(60.0f, 10.0f);
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX, btnY - 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx).plus(Vec(0.f, 6.f)), saveLabelSize, "SAVE", 7.0f, labelColor));
        }

        // Headline/subtitle are now part of the faceplate art (Phaseon.png), so we don't draw them here.
#endif

        // ── MACRO KNOBS (3 rows × 3 columns) ───────────────────────
        // Move the macro knobs down + right (closer to mod matrix block)
        const Vec macroShiftPx = Vec(25.0f, 47.0f);
        float macroY = 30.0f;
        // Slightly more vertical spacing to avoid label overlap
        // Layout uses mm units (converted by mm2px). Add +3px per row for more breathing room.
        // 1 HP = 5.08mm = 15px => mmPerPx = 5.08/15.
        float macroSpacing = 10.3f + (4.0f * 5.08f / 15.0f);

        // Row 1 (WT group): WT SEL / WT FRAME / EDGE / DENSITY
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::WT_SELECT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::TIMBRE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::EDGE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::DENSITY_PARAM));
#ifndef METAMODULE
        const Vec labelSize = Vec(80.0f, 10.0f);
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT SEL",  7.0f, cRowGreen));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT FRAME", 6.5f, cRowGreen));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "EDGE",    7.0f, cRowGreen));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DENSITY", 7.0f, cRowGreen));
#endif

        // Row 2 (FM group): FM CHARACTER / ALGO / NET / BITCRUSH
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::FM_CHARACTER_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::ALGO_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::NETWORK_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::SPIKE_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FM CHAR", 7.0f, cRowRed));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ALGO",    7.0f, cRowRed));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "NET",     7.0f, cRowRed));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "BITCRUSH",7.0f, cRowRed));
#endif

        // Row 3: MOTION / MORPH / COMPLEX / DRIFT
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::MOTION_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::MORPH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::COMPLEX_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::DRIFT_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MOTION",  7.0f, cRowBlue));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MORPH",   7.0f, cRowBlue));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "COMPLEX", 7.0f, cRowBlue));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DRIFT",   7.0f, cRowBlue));
#endif

        // Row 4: ENV / SPREAD / LFO / SCRAMBLE
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::ENV_STYLE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::ENV_SPREAD_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::TAIL_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::SCRAMBLE_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ENV",      7.0f, cRowYellow));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SPREAD",   7.0f, cRowYellow));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "LFO",      7.0f, cRowYellow));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SCRAMBLE", 6.5f, cRowYellow));
#endif

        // Row 5: OPERATOR / EDIT OP / TAME
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::OP_WAVE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::EDIT_OP_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::TAMING_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::FEEDBACK_BOOST_PARAM));

        // OP Wav/Freq switch (same row as OPERATOR, to the left)
        // Dropped further down for better visual alignment with the OPERATOR knob.
        const float opSwitchY = macroY - 2.0f;
        addParam(createParamCentered<CKSS>(mm2px(Vec(mcol1 - 7.0f, opSwitchY)).plus(macroShiftPx), module, Phaseon::OP_WF_MODE_PARAM));
    #ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "OPERATOR", 6.2f, cRowCyan));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "EDIT OP",  6.5f, cRowCyan));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "TAME",     7.0f, cRowCyan));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FDBK",     7.0f, cRowCyan));
    #endif

        // Row 6: SHUFFLE / RND / MUTATE (moved from mod matrix Row E)
        macroY += macroSpacing;
        // Move buttons up ~6px total (~2mm) without moving labels
        const float shuffleBtnY = macroY - 2.0f;
        addParam(createParamCentered<TL1105>(mm2px(Vec(mcol1, shuffleBtnY)).plus(macroShiftPx), module, Phaseon::ROLE_SHUFFLE_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(mcol2, shuffleBtnY)).plus(macroShiftPx), module, Phaseon::MOD_RND_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(mcol3, shuffleBtnY)).plus(macroShiftPx), module, Phaseon::MOD_MUT_PARAM));
    #ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SHUFFLE", 6.5f, cRowCyan));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "RND",     6.5f, cRowCyan));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MUTATE",  5.5f, cRowCyan));
    #endif

        // ── WHITE MOD MATRIX KNOBS (right block) + SHUFFLE ──────────
        // 3 columns × 2 rows on the right half (55–89 mm), 17 mm pitch.
        // Davies1900hWhiteKnob visually distinct from the black macro knobs.
        // Labels 7 mm above each knob — same convention as for black knobs.
        // -- MOD MATRIX KNOBS (right block, 3�4 grid) ----------------
        // 3 columns � 4 rows: M1-M3, M4-M6, MTRX AMT/FORM/WARP, SHUFFLE/RND/MUT
        {
            // 3 columns, 10 mm pitch, centred in the right block
            const float wmcol1 = 66.0f;
            const float wmcol2 = 76.0f;
            const float wmcol3 = 86.0f;
            // 4 rows, same spacing as macro rows
            const float wRowA = 30.0f;
            const float wRowB = 30.0f + macroSpacing * 1.0f;
            const float wRowC = 30.0f + macroSpacing * 2.0f;
            const float wRowD = 30.0f + macroSpacing * 3.0f;
            const float wRowE = 30.0f + macroSpacing * 4.0f;
            // Mod matrix block uses its own shift (15px left of macro block)
            const Vec modShiftPx = Vec(macroShiftPx.x - 21.0f, macroShiftPx.y);

            // Row A: M1, M2, M3
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol1, wRowA)).plus(modShiftPx), module, Phaseon::MOD1_AMT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol2, wRowA)).plus(modShiftPx), module, Phaseon::MOD2_AMT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol3, wRowA)).plus(modShiftPx), module, Phaseon::MOD3_AMT_PARAM));
            // Row B: M4, M5, M6
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol1, wRowB)).plus(modShiftPx), module, Phaseon::MOD4_AMT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol2, wRowB)).plus(modShiftPx), module, Phaseon::MOD5_AMT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol3, wRowB)).plus(modShiftPx), module, Phaseon::MOD6_AMT_PARAM));
            // Row C: MTRX AMT, FORMANT, WARP
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol1, wRowC)).plus(modShiftPx), module, Phaseon::MOD_AMT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol2, wRowC)).plus(modShiftPx), module, Phaseon::FORMANT_PARAM));
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(wmcol3, wRowC)).plus(modShiftPx), module, Phaseon::WARP_MODE_PARAM));

            // Row D: LFO / ENV mode + target + OP SEL trimpot
            addParam(createParamCentered<CKSSThree>(mm2px(Vec(wmcol1, wRowD)).plus(modShiftPx), module, Phaseon::LFO_SELECT_PARAM));
            addParam(createParamCentered<CKSS>(mm2px(Vec(wmcol2, wRowD)).plus(modShiftPx), module, Phaseon::LFO_TARGET_PARAM));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(wmcol3, wRowD)).plus(modShiftPx), module, Phaseon::LFO_OP_SELECT_PARAM));

            // Row E: LFO trimpots (OP SEL section: RATE / PHASE / DEFORM / AMP)
            // Use tighter 7mm spacing and small trimpots so they sit closer together.
            const float lfoCol0 = wmcol1;          // RATE
            const float lfoCol1 = wmcol1 + 7.0f;   // PHASE
            const float lfoCol2 = wmcol1 + 14.0f;  // DEFORM
            const float lfoCol3 = wmcol1 + 21.0f;  // AMP
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(lfoCol0, wRowE)).plus(modShiftPx), module, Phaseon::LFO_RATE_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(lfoCol1, wRowE)).plus(modShiftPx), module, Phaseon::LFO_PHASE_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(lfoCol2, wRowE)).plus(modShiftPx), module, Phaseon::LFO_DEFORM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(lfoCol3, wRowE)).plus(modShiftPx), module, Phaseon::LFO_AMP_PARAM));

            // Per-operator VOLUME trims (OP1..OP6) — compact single-row layout using the same trimpot size as the LFO trims.
            const float opVolRow = wRowE + 7.0f;
            const float opVolDx  = 6.0f; // tighter spacing
            const float opVolStart = wmcol1 - opVolDx;
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 0.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP1_LEVEL_TRIM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 1.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP2_LEVEL_TRIM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 2.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP3_LEVEL_TRIM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 3.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP4_LEVEL_TRIM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 4.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP5_LEVEL_TRIM_PARAM));
            addParam(createParamCentered<SmallTrimpot>(mm2px(Vec(opVolStart + opVolDx * 5.0f, opVolRow)).plus(modShiftPx), module, Phaseon::OP6_LEVEL_TRIM_PARAM));

#ifndef METAMODULE
            const Vec wLabelSz = Vec(60.0f, 10.0f);
            // Row A labels
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol1, wRowA - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M1", 6.5f, cModKnob));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowA - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M2", 6.5f, cModKnob));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowA - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M3", 6.5f, cModKnob));
            // Row B labels
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol1, wRowB - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M4", 6.5f, cModKnob));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowB - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M5", 6.5f, cModKnob));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowB - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "M6", 6.5f, cModKnob));
            // Row C labels
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol1, wRowC - 7.f)).plus(modShiftPx).plus(textShiftPx), Vec(80.0f, 10.0f), "MTRX AMT", 6.0f, cModKnob));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowC - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "FORM", 6.5f, cRowGreen));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowC - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "WARP", 6.5f, cRowGreen));
            // Row D labels (LFO / ENV switches)
            const NVGcolor cLfo = nvgRGB(255, 180, 60); // orange for LFO controls
            // "ENV/LFO" and "ALL/OP" slightly above row center; "OP SEL" 1px lower than before
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol1, wRowD - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "ENV/LFO", 5.5f, cLfo));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowD - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "ALL/OP", 5.5f, cLfo));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowD - 5.5f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP SEL", 5.5f, cLfo));
            // Row E labels (LFO trimpots) — shifted slightly down
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol0, wRowE - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "RATE", 5.5f, cLfo));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol1, wRowE - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "PHASE", 5.5f, cLfo));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol2, wRowE - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "DEFORM", 5.0f, cLfo));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol3, wRowE - 6.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "AMP", 5.5f, cLfo));

            // Secondary ADSR markers under the LFO labels (cyan), aligned with the same columns
            const NVGcolor cAdsr = cRowCyan;
            const float adsrDyPx = -1.5f; // a touch closer to the knobs than the main labels
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol0, wRowE - 4.f)).plus(modShiftPx).plus(textShiftPx).plus(Vec(0.f, adsrDyPx)), wLabelSz, "A", 5.0f, cAdsr));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol1, wRowE - 4.f)).plus(modShiftPx).plus(textShiftPx).plus(Vec(0.f, adsrDyPx)), wLabelSz, "D", 5.0f, cAdsr));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol2, wRowE - 4.f)).plus(modShiftPx).plus(textShiftPx).plus(Vec(0.f, adsrDyPx)), wLabelSz, "S", 5.0f, cAdsr));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(lfoCol3, wRowE - 4.f)).plus(modShiftPx).plus(textShiftPx).plus(Vec(0.f, adsrDyPx)), wLabelSz, "R", 5.0f, cAdsr));

                // OP volume trim labels, aligned with single-row trims
                const float opLblDy = 4.0f;
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 0.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP1", 5.2f, cLfo));
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 1.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP2", 5.2f, cLfo));
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 2.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP3", 5.2f, cLfo));
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 3.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP4", 5.2f, cLfo));
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 4.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP5", 5.2f, cLfo));
                addChild(new PhaseonCenteredLabel(mm2px(Vec(opVolStart + opVolDx * 5.0f, opVolRow - opLblDy)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "OP6", 5.2f, cLfo));
#endif
        }

        // ── PORTS (16 total) — tidy 8×2 grid, equal spacing ──
        {
            const float xL = 10.0f;
            const float xR = panelW - 10.0f;
            const float dx = (xR - xL) / 7.0f;
            const float row1Y = 92.0f;
            const float row2Y = 116.0f;
            // Panel layout tweak:
            // - Upper row down slightly (tighten spacing above ports)
            // - Lower row down by 15px
            // Additional 2px downward shift applied to both rows.
            const Vec row1ShiftPx = Vec(0.f, 61.f);
			const Vec row2ShiftPx = Vec(0.f, 17.f);
            const float portLabelDyPx = 17.f;

            auto xAt = [&](int i) { return xL + dx * (float)i; };
			auto portPx = [&](int col, float y, Vec shiftPx) {
				return mm2px(Vec(xAt(col), y)).plus(shiftPx);
			};

            // Row 1 (8 CV inputs)
            addInput(createInputCentered<PhaseonPortWidget>(portPx(0, row1Y, row1ShiftPx), module, Phaseon::HARM_DENSITY_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(1, row1Y, row1ShiftPx), module, Phaseon::ENV_SPREAD_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(2, row1Y, row1ShiftPx), module, Phaseon::WT_SELECT_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(3, row1Y, row1ShiftPx), module, Phaseon::EDGE_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(4, row1Y, row1ShiftPx), module, Phaseon::TIMBRE2_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(5, row1Y, row1ShiftPx), module, Phaseon::WT_REGION_BIAS_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(6, row1Y, row1ShiftPx), module, Phaseon::COMPLEXITY_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(7, row1Y, row1ShiftPx), module, Phaseon::SPECTRAL_TILT_CV));

            // Row 2 (2 CV inputs + 4 bottom IO + 2 outputs on lower-right)
            addInput(createInputCentered<PhaseonPortWidget>(portPx(0, row2Y, row2ShiftPx), module, Phaseon::MORPH_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(1, row2Y, row2ShiftPx), module, Phaseon::MOTION_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(2, row2Y, row2ShiftPx), module, Phaseon::FORMANT_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(3, row2Y, row2ShiftPx), module, Phaseon::CLOCK_INPUT));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(4, row2Y, row2ShiftPx), module, Phaseon::GATE_INPUT));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(5, row2Y, row2ShiftPx), module, Phaseon::VOCT_INPUT));
            addOutput(createOutputCentered<PhaseonPortWidget>(portPx(6, row2Y, row2ShiftPx), module, Phaseon::LEFT_OUTPUT));
            addOutput(createOutputCentered<PhaseonPortWidget>(portPx(7, row2Y, row2ShiftPx), module, Phaseon::RIGHT_OUTPUT));

            // Gate light near GATE jack
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xAt(4) + 5.f, row2Y - 5.f)).plus(row2ShiftPx), module, Phaseon::GATE_LIGHT));

    #ifndef METAMODULE
            const Vec cvLabelSize = Vec(70.0f, 10.0f);
            // Row 1 labels
            addChild(new PhaseonCenteredLabel(portPx(0, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "DENS",     5.2f, cRowGreen));
            addChild(new PhaseonCenteredLabel(portPx(1, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "ENV.SPD",  5.2f, cRowYellow));
            addChild(new PhaseonCenteredLabel(portPx(2, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "WT.SEL",   5.2f, cRowGreen));
            addChild(new PhaseonCenteredLabel(portPx(3, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "EDGE",     5.2f, cRowGreen));
            addChild(new PhaseonCenteredLabel(portPx(4, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "WT FRAME", 4.8f, cRowGreen));
            addChild(new PhaseonCenteredLabel(portPx(5, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "WT.RGN",   5.2f, cRowGreen));
            addChild(new PhaseonCenteredLabel(portPx(6, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "CMPLX",    5.2f, cRowBlue));
            addChild(new PhaseonCenteredLabel(portPx(7, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "FILTER",   4.8f, cTilt));

            const Vec ioLabelSize = Vec(60.0f, 10.0f);
            // Row 2 labels
                addChild(new PhaseonCenteredLabel(portPx(0, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "MORPH",  5.2f, cRowBlue));
                addChild(new PhaseonCenteredLabel(portPx(1, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), cvLabelSize, "MOTION", 5.2f, cRowBlue));
                addChild(new PhaseonCenteredLabel(portPx(2, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "FORM",  5.6f, cRowGreen));
                const NVGcolor cIoGrey = nvgRGB(160, 160, 160);
                addChild(new PhaseonCenteredLabel(portPx(3, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "CLK",   5.6f, cIoGrey));
                addChild(new PhaseonCenteredLabel(portPx(4, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "GATE",  5.6f, cIoGrey));
                addChild(new PhaseonCenteredLabel(portPx(5, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "V/OCT", 5.6f, cIoGrey));
                addChild(new PhaseonCenteredLabel(portPx(6, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "L",     5.6f, cIoGrey));
                addChild(new PhaseonCenteredLabel(portPx(7, row2Y, row2ShiftPx).plus(Vec(0.f, -portLabelDyPx + 4.f)), ioLabelSize, "R",     5.6f, cIoGrey));
    #endif
        }
    }

    // Context menu
    void appendContextMenu(Menu* menu) override {
        Phaseon* module = dynamic_cast<Phaseon*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator);

        // Algorithm display
        int algo = (int)(module->params[Phaseon::ALGO_PARAM].getValue() + 0.5f);
        const Algorithm& a = getAlgorithm(algo);
        menu->addChild(createMenuLabel(std::string("Algorithm: ") + a.name));

        // FM Character display
        float fc = module->params[Phaseon::FM_CHARACTER_PARAM].getValue();
        int ri = fmCharacterToRatioIndex(fc);
        const RatioSet& rs = getRatioSet(ri);
        menu->addChild(createMenuLabel(std::string("Ratio Set: ") + rs.name));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Mod Matrix (right-click amount knobs to set source/dest)"));

        // One submenu per slot showing current state + sub-items to pick src/dst/target/slew
        for (int si = 0; si < phaseon::kModSlots; ++si) {
            phaseon::ModSlot& slot = module->macros.modMatrix.slots[si];

            // Build label: "M1: Velocity → FmDepth [All]"
            char slotLabel[80];
            snprintf(slotLabel, sizeof(slotLabel), "M%d: %s → %s [%s]",
                     si + 1,
                     phaseon::modSourceName(slot.src),
                     phaseon::modDestName(slot.dst),
                     phaseon::modOpTargetName(slot.target));

            // Source submenu
            menu->addChild(createSubmenuItem(slotLabel, "", [si, module](Menu* subMenu) {
                subMenu->addChild(createMenuLabel("Source"));
                for (int src = 0; src < (int)phaseon::ModSource::kCount; ++src) {
                    subMenu->addChild(createCheckMenuItem(
                        phaseon::modSourceName((phaseon::ModSource)src), "",
                        [si, src, module]() { return module->macros.modMatrix.slots[si].src == (phaseon::ModSource)src; },
                        [si, src, module]() { module->macros.modMatrix.slots[si].src = (phaseon::ModSource)src; }));
                }

                subMenu->addChild(new MenuSeparator);
                subMenu->addChild(createMenuLabel("Destination"));
                for (int dst = 0; dst < (int)phaseon::ModDest::kCount; ++dst) {
                    subMenu->addChild(createCheckMenuItem(
                        phaseon::modDestName((phaseon::ModDest)dst), "",
                        [si, dst, module]() { return module->macros.modMatrix.slots[si].dst == (phaseon::ModDest)dst; },
                        [si, dst, module]() { module->macros.modMatrix.slots[si].dst = (phaseon::ModDest)dst; }));
                }

                subMenu->addChild(new MenuSeparator);
                subMenu->addChild(createMenuLabel("Op Target"));
                const phaseon::ModOpTarget targets[] = {
                    phaseon::ModOpTarget::All,
                    phaseon::ModOpTarget::CarriersOnly,
                    phaseon::ModOpTarget::ModulatorsOnly,
                    phaseon::ModOpTarget::Op1, phaseon::ModOpTarget::Op2,
                    phaseon::ModOpTarget::Op3, phaseon::ModOpTarget::Op4,
                    phaseon::ModOpTarget::Op5, phaseon::ModOpTarget::Op6,
                };
                for (auto tgt : targets) {
                    subMenu->addChild(createCheckMenuItem(
                        phaseon::modOpTargetName(tgt), "",
                        [si, tgt, module]() { return module->macros.modMatrix.slots[si].target == tgt; },
                        [si, tgt, module]() { module->macros.modMatrix.slots[si].target = tgt; }));
                }

                subMenu->addChild(new MenuSeparator);
                char slewBuf[32];
                snprintf(slewBuf, sizeof(slewBuf), "Slew: %d ms", (int)module->macros.modMatrix.slots[si].slewMs);
                subMenu->addChild(createMenuLabel(slewBuf));
                const float slewOptions[] = {0.0f, 1.0f, 5.0f, 20.0f, 50.0f};
                for (float ms : slewOptions) {
                    char buf[24];
                    snprintf(buf, sizeof(buf), "%.0f ms", ms);
                    subMenu->addChild(createCheckMenuItem(
                        buf, "",
                        [si, ms, module]() { return std::fabs(module->macros.modMatrix.slots[si].slewMs - ms) < 0.5f; },
                        [si, ms, module]() { module->macros.modMatrix.slots[si].slewMs = ms; }));
                }
            }));
        }

#ifndef METAMODULE
        // --- Rename current preset (inline text field) ---
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Rename preset (Enter to confirm):"));
        struct PhaseonRenameField : ui::TextField {
            Phaseon* module = nullptr;
            PhaseonRenameField() {
                box.size.x = 220.f;
            }
            void onSelectKey(const SelectKeyEvent& e) override {
                if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
                    if (module) {
                        module->renameCurrentPreset(getText());
                    }
                    MenuOverlay* overlay = getAncestorOfType<MenuOverlay>();
                    if (overlay) overlay->requestDelete();
                    e.consume(this);
                    return;
                }
                ui::TextField::onSelectKey(e);
            }
        };
        auto* field = new PhaseonRenameField;
        field->module = module;
        // Pre-fill with the current slot name (without the numeric prefix).
        field->setText(Phaseon::stripSlotPrefix(module->presetName));
        field->selectAll();
        menu->addChild(field);
#endif
    }
};

Model* modelPhaseon = createModel<Phaseon, PhaseonWidget>("Phaseon");

