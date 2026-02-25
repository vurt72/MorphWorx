/*
 * Phaseon â€” FM + Wavetable monster voice for VCV Rack
 *
 * A preset-first mono synthesizer designed for aggressive modern sounds
 * and CV-driven performance.  6 internal operators (hidden from user),
 * wavetable oscillators, curated FM algorithms, macro controls.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary â€” not licensed under GPL or any open-source license.
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
        PARAMS_LEN
    };
    enum InputId {
        GATE_INPUT,
        VOCT_INPUT,
        TIMBRE_CV,
        HARM_DENSITY_CV,
        WT_SELECT_CV,       // was FM_CHAOS_CV  â€” bipolar Â±5V selects WT family Â±1 step
        TIMBRE2_CV,         // was TRANSIENT_SPIKE_CV â€” perf timbre CV (stacks with I/O timbre)
        WT_REGION_BIAS_CV,
        SPECTRAL_TILT_CV,
        MOTION_CV,          // was INSTABILITY_CV â€” CV adds to motion amount
        FORMANT_CV,         // replaces LOOP_RATE_CV â€” Â±5V formant intensity
        ENV_SPREAD_CV,      // new â€” 0..10V adds to env spread knob
        EDGE_CV,            // new â€” 0..10V adds to edge knob
        COMPLEXITY_CV,      // new â€” 0..10V adds to complexity knob
        MORPH_CV,           // new â€” Â±5V adds to algorithm morph knob
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

    // â”€â”€ DSP engine â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    // â”€â”€ State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
        bool used = false;
    };
    std::array<PresetSlot, kPresetSlots> bank;
    int currentPreset = 0;
    bool bankLoaded = false;
    int bankFileVersionLoaded = 1;
    bool lastPresetPrev = false;
    bool lastPresetNext = false;

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
                std::make_pair(phaseon::ModDest::SpikeIntensity, 0.8f),
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
        constexpr int WT_SINE     = 0;
        constexpr int WT_SAW      = 1;
        constexpr int WT_SQUARE   = 2;
        constexpr int WT_TRI      = 3;
        constexpr int WT_FORMANT  = 4;
        constexpr int WT_HARSH    = 5;
        constexpr int WT_NOISE    = 6;
        constexpr int WT_SUBBASS  = 7;
        constexpr int WT_VOWELS   = 8;
        constexpr int WT_SHIFT    = 9;
        constexpr int WT_HYBRID   = 10;

        if (fam == 0) {
            const int classicBuiltins[7] = { WT_SINE, WT_TRI, WT_SAW, WT_SQUARE, WT_SUBBASS, WT_HARSH, WT_NOISE };
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
            bool hasNew = (builtinCount > WT_SHIFT);
            int vowels = hasNew ? WT_VOWELS : WT_FORMANT;
            return clampTableIndex(vowels, numTables);
        }

        bool hasNew = (builtinCount > WT_HYBRID);
        int h = hasNew ? WT_HYBRID : WT_NOISE;
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
            }
        }

        json_decref(root);
        updatePresetDisplayName();
        return true;
    }

    bool bankSave() {
        json_t* root = json_object();
        json_object_set_new(root, "format", json_string("MorphWorx.PhaseonBank"));
        json_object_set_new(root, "version", json_integer(2));

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

    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    // Constructor
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
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
        configParam(SPIKE_PARAM,        0.f, 1.f, 0.0f,  "Spike (Transient Punch)");
        configButton(ROLE_SHUFFLE_PARAM, "Role Shuffle");

        configParam(TAIL_PARAM,         0.f, 1.f, 0.73f, "Tail (Release Length)");

        // Drift: combined Chaos + Instability (was WT Family)
        configParam(DRIFT_PARAM,        0.f, 1.f, 0.0f,  "Drift (Chaos + Instability)");

        // Cross-operator modulation network (adds coupling on top of the selected algorithm)
        configParam(NETWORK_PARAM,      0.f, 1.f, 0.0f,   "Network (Cross Mod)");

        // Explicit wavetable selector (snapped index into the loaded wavetable bank)
        // Stored by name in the PhaseonBank for VCV â†” MetaModule portability.
        configParam(WT_SELECT_PARAM,    0.f, 127.f, 0.f,  "WT Select");
        paramQuantities[WT_SELECT_PARAM]->snapEnabled = true;

        // SCRAMBLE: per-operator envelope diversity
        // 0 = uniform (current behavior), 1 = wildly different per-operator envelopes
        configParam(SCRAMBLE_PARAM,     0.f, 1.f, 0.f,    "Scramble");

        // Warp mode: selects phase remapping function (Serum-style).
        // COMPLEX controls warp depth; this selects the warp shape.
        configParam(WARP_MODE_PARAM,    0.f, 5.f, 0.f,    "Warp Mode");
        paramQuantities[WARP_MODE_PARAM]->snapEnabled = true;

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

        // Preset bank browsing (screen buttons)
        configButton(PRESET_PREV_PARAM, "Preset Prev");
        configButton(PRESET_NEXT_PARAM, "Preset Next");
        configButton(PRESET_SAVE_PARAM, "Preset Save");

        configButton(PANIC_PARAM, "Panic / Reset");

        // CV inputs (bottom half)
        configInput(GATE_INPUT,          "Gate");
        configInput(VOCT_INPUT,          "V/Oct");
        configInput(TIMBRE_CV,           "Timbre CV");
        configInput(HARM_DENSITY_CV,     "Harmonic Density CV");
        configInput(WT_SELECT_CV,        "WT Select CV");
        configInput(TIMBRE2_CV,          "Timbre CV (Perf)");
        configInput(WT_REGION_BIAS_CV,   "WT Region Bias CV");
        configInput(SPECTRAL_TILT_CV,    "Spectral Tilt CV");
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

    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    // Lifecycle
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    void onAdd() override {
        if (!engineReady) {
            // Generate built-in wavetables
            wavetableBank.generateBuiltins();

            // Append user wavetables (folder-scanned) after builtins.
            // Preset-bank portability VCV â†” MetaModule depends on having the same
            // wavetable filenames available on both systems.
            loadUserWavetablesOnce();

            voice.reset();
            tiltFilter.reset();
            polish.reset();
            engineReady = true;
        }

        bankEnsureLoaded();
        bankApplySlot(currentPreset, false);
    }

    void onReset() override {
        voice.reset();
        tiltFilter.reset();
        polish.reset();
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

        bankEnsureLoaded();
        currentPreset = 0;
        bankApplySlot(currentPreset, false);
    }

    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    // Process
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    void process(const ProcessArgs& args) override {
        if (!engineReady) {
            outputs[LEFT_OUTPUT].setVoltage(0.f);
            outputs[RIGHT_OUTPUT].setVoltage(0.f);
            return;
        }

        // Bank loading is handled in onAdd()/preset actions; avoid any I/O in the audio thread.

        // â”€â”€ Gate handling (needed for immediate SHUFFLE re-voicing) â”€
        bool gate = inputs[GATE_INPUT].getVoltage() > 1.0f;

        // â”€â”€ Preset bank browse/save (screen buttons) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

        // â”€â”€ Role shuffle button (applies on next note-on) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
                shuffleBurstTimer = 0.20f; // 200ms
                if (gate && voice.isActive()) {
                    voice.chaosAmount = 0.60f;
                    voice.spike.intensity = 0.40f;
                    voice.spike.trigger();
                }
            }
            lastShuffle = sh;
        }

        // â”€â”€ Mod matrix randomize/mutate (panel buttons) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

        // â”€â”€ Gate handling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (gate && !lastGate) {
            // Note on — snap pitch instantly (no glide on attack)
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
            // Legato pitch update — slew in V/Oct domain for musical glide
            float voct = inputs[VOCT_INPUT].getVoltage();
            // ~45ms exponential slew (smooth portamento in pitch space)
            const float slewCoeff = 1.0f - expf(-1.0f / (0.045f * args.sampleRate));
            slewedVoct += (voct - slewedVoct) * slewCoeff;
            voice.fundamentalHz = 261.63f * powf(2.0f, slewedVoct);
        }
        lastGate = gate;
        lights[GATE_LIGHT].setBrightness(gate ? 1.0f : 0.0f);

        // -- Clock sync: measure period, override LFO1 phase --
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
                // Hard-sync LFO1 phase to clock edge (reset to 0)
                voice.lfo1Phase = 0.0f;
            }
            lastClockHigh = clockHigh;

            // When clock is connected and valid, override LFO1 rate.
            // Phase is advanced here at 1/period Hz; tick() also advances
            // slightly causing drift, but hard sync on each clock edge corrects it.
            if (clockConnected && clockPeriod > 0.01f) {
                float clockHz = 1.0f / clockPeriod;
                voice.lfo1Phase += dt * clockHz;
                if (voice.lfo1Phase >= 1.0f)
                    voice.lfo1Phase -= 1.0f;
            }

            // If clock disconnected, clear measurement so free-running resumes
            if (!clockConnected) {
                clockPeriod = 0.0f;
                clockPeriodSmoothed = 0.0f;
            }
        }

        // â”€â”€ Control rate: update macros â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
            macros.spike       = params[SPIKE_PARAM].getValue();
            macros.tail        = params[TAIL_PARAM].getValue();
            macros.drift       = params[DRIFT_PARAM].getValue();
            macros.network     = params[NETWORK_PARAM].getValue();
            macros.wtSelect    = (int)(params[WT_SELECT_PARAM].getValue() + 0.5f);
            macros.scramble    = params[SCRAMBLE_PARAM].getValue();
            macros.warpMode    = (int)(params[WARP_MODE_PARAM].getValue() + 0.5f);

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
            tiltFilter.tilt = clamp(macros.cvSpectralTilt, -1.0f, 1.0f);

            // Apply macros â†’ voice parameters
            applyMacros(voice, macros, wavetableBank, args.sampleRate);

            // Shuffle burst: decay chaos/spike overlay over ~200ms
            if (shuffleBurstTimer > 0.0f) {
                float blockDt = (float)kControlRate / std::max(1.0f, args.sampleRate);
                shuffleBurstTimer -= blockDt;
                if (shuffleBurstTimer <= 0.0f) {
                    shuffleBurstTimer = 0.0f;
                } else {
                    float t = shuffleBurstTimer / 0.20f; // 1..0 over 200ms
                    voice.chaosAmount = std::max(voice.chaosAmount, 0.60f * t);
                }
            }
        }

        // Panic button
        if (params[PANIC_PARAM].getValue() > 0.5f) {
            voice.reset();
            tiltFilter.reset();
        }

        // â”€â”€ Audio â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        voice.tick(wavetableBank, args.sampleRate);

        float outL = voice.outL;
        float outR = voice.outR;

        // Post-processing: spectral tilt
        tiltFilter.process(outL, outR);

        // Output polish (finished / punchy / slightly dangerous)
        // Prepare is cheap and keeps state stable across sample rate changes.
        polish.prepare(args.sampleRate);
        polish.process(outL, outR, voice.fundamentalHz, macros.edge, args.sampleRate);

        // Scale to Rack levels (Â±5V nominal)
        outputs[LEFT_OUTPUT].setVoltage(outL * 5.0f);
        outputs[RIGHT_OUTPUT].setVoltage(outR * 5.0f);
    }

    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    // Serialization
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
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

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// Widget
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

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

        // â”€â”€ Panel layout: 20 HP = 101.6mm â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Minimalith neon green labels: nvgRGB(167, 255, 196)
        const NVGcolor labelColor  = nvgRGB(167, 255, 196);
        const NVGcolor accentColor = nvgRGB(255, 140, 100);

        // Move all text down by 4px
        // Global label text shift (user request: move all knob text down 3px)
        const Vec textShiftPx = Vec(0.0f, 7.0f);
        // CV label text should be slightly higher than knob labels
        const Vec cvTextShiftPx = textShiftPx.plus(Vec(0.0f, -2.0f));

        const float panelW = 101.6f;
        const float centerX = panelW * 0.5f;   // 50.8

        // Global panel columns (used by CV + I/O sections)
        const float col1 = 16.0f;
        const float col2 = centerX;
        const float col3 = panelW - 16.0f;     // 85.6

        // â”€â”€ Semantic colour palette: knob label colour == its paired CV label colour â”€â”€
        const NVGcolor cTimbre  = nvgRGB(255, 210,  60);  // amber  â€” TIMBRE
        const NVGcolor cDensity = nvgRGB( 80, 220, 200);  // teal   â€” DENSITY / H.DENS
        const NVGcolor cEdge    = nvgRGB(255,  60,  60);  // red    â€” EDGE
        const NVGcolor cMotion  = nvgRGB(120, 180, 255);  // blue   â€” MOTION
        const NVGcolor cMorph   = nvgRGB(200, 130, 255);  // violet â€” MORPH
        const NVGcolor cComplex = nvgRGB(255, 160, 200);  // pink   â€” COMPLEX / CMPLX
        const NVGcolor cSpread  = nvgRGB(140, 255, 220);  // mint   â€” SPREAD / ENV.SPD
        const NVGcolor cWt      = nvgRGB(180, 255, 140);  // green  â€” WT knobs + WT CVs
        const NVGcolor cTilt    = nvgRGB(200, 180, 255);  // lavend â€” TILT CV
        const NVGcolor cModKnob = nvgRGB(255, 255, 255);  // white  â€” M1â€“M6 labels

        // Black macro knobs: tight left block, 11 mm pitch, 4 columns
        const float mcol1    =  9.0f;
        const float mcol2    = 20.0f;
        const float mcol3    = 31.0f;
        const float mcolTail = 42.0f;

        // â”€â”€ TOP: Title + Display â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#ifdef METAMODULE
        // MetaModule preset name text display (rendered above the faceplate).
        {
            struct PhaseonMMDisplay : MetaModule::VCVTextDisplay {};
            auto* mmDisplay = new PhaseonMMDisplay();
            mmDisplay->box.pos = mm2px(Vec(10.0f, 16.0f));
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
        Vec dispPos = mm2px(Vec(6.0f, 17.0f)).plus(textShiftPx);
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
            const Vec presetBtnShiftPx = Vec(0.0f, 5.0f);

            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX - btnDx, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_PREV_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_SAVE_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(centerX + btnDx, btnY)).plus(textShiftPx).plus(presetBtnShiftPx), module, Phaseon::PRESET_NEXT_PARAM));

            const Vec bLabelSize = Vec(28.0f, 10.0f);
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX - btnDx, btnY + 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx), bLabelSize, "<", 10.0f, labelColor));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX + btnDx, btnY + 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx), bLabelSize, ">", 10.0f, labelColor));

            const Vec saveLabelSize = Vec(60.0f, 10.0f);
            addChild(new PhaseonCenteredLabel(mm2px(Vec(centerX, btnY + 5.0f)).plus(textShiftPx).plus(presetBtnShiftPx), saveLabelSize, "SAVE", 7.0f, labelColor));
        }

        // Headline/subtitle are now part of the faceplate art (Phaseon.png), so we don't draw them here.
#endif

        // â”€â”€ MACRO KNOBS (3 rows Ã— 3 columns) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Move the macro knobs down + right (closer to mod matrix block)
        const Vec macroShiftPx = Vec(25.0f, 54.0f);
        float macroY = 30.0f;
        // Slightly more vertical spacing to avoid label overlap
        float macroSpacing = 10.3f;

        // Row 1: TIMBRE / DENSITY / EDGE / TAIL
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::TIMBRE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::DENSITY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::EDGE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::TAIL_PARAM));
#ifndef METAMODULE
        const Vec labelSize = Vec(80.0f, 10.0f);
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT FRAME", 6.5f, cTimbre));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DENSITY", 7.0f, cDensity));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "EDGE",    7.0f, cEdge));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "TAIL",    7.0f, labelColor));
#endif

        // Row 2: FM CHARACTER / MOTION / ALGO / WT SET
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::FM_CHARACTER_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::MOTION_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::ALGO_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::DRIFT_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FM CHAR", 7.0f, accentColor));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MOTION",  7.0f, cMotion));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ALGO",    7.0f, labelColor));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DRIFT",   7.0f, accentColor));
#endif

        // Row 3: MORPH / COMPLEX / SPREAD / WT SEL
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::MORPH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::COMPLEX_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::ENV_SPREAD_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::WT_SELECT_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MORPH",   7.0f, cMorph));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "COMPLEX", 7.0f, cComplex));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SPREAD",  7.0f, cSpread));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT SEL",  7.0f, cWt));
#endif

        // Row 4: ENV / BIAS / SCRAMBLE / NET
        macroY += macroSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol1,    macroY)).plus(macroShiftPx), module, Phaseon::ENV_STYLE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol2,    macroY)).plus(macroShiftPx), module, Phaseon::SPIKE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcol3,    macroY)).plus(macroShiftPx), module, Phaseon::SCRAMBLE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(mcolTail, macroY)).plus(macroShiftPx), module, Phaseon::NETWORK_PARAM));
#ifndef METAMODULE
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol1,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ENV",      7.0f, labelColor));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol2,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SPIKE",    7.0f, labelColor));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcol3,    macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SCRAMBLE", 6.5f, accentColor));
        addChild(new PhaseonCenteredLabel(mm2px(Vec(mcolTail, macroY - 7.f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "NET",      7.0f, labelColor));
#endif

        // â”€â”€ WHITE MOD MATRIX KNOBS (right block) + SHUFFLE â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // 3 columns Ã— 2 rows on the right half (55â€“89 mm), 17 mm pitch.
        // Davies1900hWhiteKnob visually distinct from the black macro knobs.
        // Labels 7 mm above each knob â€” same convention as for black knobs.
        // ── MOD MATRIX KNOBS (right block, 3×4 grid) ────────────────
        // 3 columns × 4 rows: M1-M3, M4-M6, MTRX AMT/FORM/WARP, SHUFFLE/RND/MUT
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
            const NVGcolor cFormant = nvgRGB(255, 200, 100);      // warm gold
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
            // Row D: SHUFFLE, RND, MUT (buttons, evenly spaced)
            addParam(createParamCentered<TL1105>(mm2px(Vec(wmcol1, wRowD + 2.f)).plus(modShiftPx), module, Phaseon::ROLE_SHUFFLE_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(wmcol2, wRowD + 2.f)).plus(modShiftPx), module, Phaseon::MOD_RND_PARAM));
            addParam(createParamCentered<TL1105>(mm2px(Vec(wmcol3, wRowD + 2.f)).plus(modShiftPx), module, Phaseon::MOD_MUT_PARAM));

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
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowC - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "FORM", 6.5f, cFormant));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowC - 7.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "WARP", 6.5f, cComplex));
            // Row D labels
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol1, wRowD - 5.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "SHUFFLE", 6.5f, accentColor));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol2, wRowD - 5.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "RND", 6.5f, accentColor));
            addChild(new PhaseonCenteredLabel(mm2px(Vec(wmcol3, wRowD - 5.f)).plus(modShiftPx).plus(textShiftPx), wLabelSz, "MUT", 6.5f, accentColor));
#endif
        }

        // â”€â”€ PORTS (16 total) â€” tidy 8Ã—2 grid, equal spacing â”€â”€
        {
            const float xL = 10.0f;
            const float xR = panelW - 10.0f;
            const float dx = (xR - xL) / 7.0f;
            const float row1Y = 92.0f;
            const float row2Y = 116.0f;
			const Vec row1ShiftPx = Vec(0.f, 16.f);
            const float portLabelDyPx = 19.f;

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
            addInput(createInputCentered<PhaseonPortWidget>(portPx(0, row2Y, Vec()), module, Phaseon::MORPH_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(1, row2Y, Vec()), module, Phaseon::MOTION_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(2, row2Y, Vec()), module, Phaseon::FORMANT_CV));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(3, row2Y, Vec()), module, Phaseon::CLOCK_INPUT));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(4, row2Y, Vec()), module, Phaseon::GATE_INPUT));
            addInput(createInputCentered<PhaseonPortWidget>(portPx(5, row2Y, Vec()), module, Phaseon::VOCT_INPUT));
            addOutput(createOutputCentered<PhaseonPortWidget>(portPx(6, row2Y, Vec()), module, Phaseon::LEFT_OUTPUT));
            addOutput(createOutputCentered<PhaseonPortWidget>(portPx(7, row2Y, Vec()), module, Phaseon::RIGHT_OUTPUT));

            // Gate light near GATE jack
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xAt(4) + 5.f, row2Y - 5.f)), module, Phaseon::GATE_LIGHT));

    #ifndef METAMODULE
            const Vec cvLabelSize = Vec(70.0f, 10.0f);
            // Row 1 labels
            addChild(new PhaseonCenteredLabel(portPx(0, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "H.DENS",   6.5f, cDensity));
            addChild(new PhaseonCenteredLabel(portPx(1, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "ENV.SPD",  6.5f, cSpread));
            addChild(new PhaseonCenteredLabel(portPx(2, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "WT.SEL",   6.5f, cWt));
            addChild(new PhaseonCenteredLabel(portPx(3, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "EDGE",     6.5f, cEdge));
            addChild(new PhaseonCenteredLabel(portPx(4, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "WT FRAME", 6.0f, cTimbre));
            addChild(new PhaseonCenteredLabel(portPx(5, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "WT.RGN",   6.5f, cWt));
            addChild(new PhaseonCenteredLabel(portPx(6, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "CMPLX",    6.5f, cComplex));
            addChild(new PhaseonCenteredLabel(portPx(7, row1Y, row1ShiftPx).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "TILT",     6.5f, cTilt));

            const Vec ioLabelSize = Vec(60.0f, 10.0f);
            // Row 2 labels
                addChild(new PhaseonCenteredLabel(portPx(0, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "MORPH",  6.5f, cMorph));
                addChild(new PhaseonCenteredLabel(portPx(1, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), cvLabelSize, "MOTION", 6.5f, cMotion));
                addChild(new PhaseonCenteredLabel(portPx(2, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "FORM",  7.0f, nvgRGB(255, 200, 100)));
                addChild(new PhaseonCenteredLabel(portPx(3, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "CLK",   7.0f, nvgRGB(200, 200, 255)));
                addChild(new PhaseonCenteredLabel(portPx(4, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "GATE",  7.0f, nvgRGB(100, 255, 100)));
                addChild(new PhaseonCenteredLabel(portPx(5, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "V/OCT", 7.0f, labelColor));
                addChild(new PhaseonCenteredLabel(portPx(6, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "L",     7.0f, labelColor));
                addChild(new PhaseonCenteredLabel(portPx(7, row2Y, Vec()).plus(Vec(0.f, -portLabelDyPx)), ioLabelSize, "R",     7.0f, labelColor));
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

            // Build label: "M1: Velocity â†’ FmDepth [All]"
            char slotLabel[80];
            snprintf(slotLabel, sizeof(slotLabel), "M%d: %s â†’ %s [%s]",
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

