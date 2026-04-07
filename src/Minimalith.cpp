/*
 * Minimalith - Minimal PreenFM2 engine for VCV Rack
 *
 * Based on PreenFM2 by Xavier Hosxe (GPL-3.0-or-later)
 * VCV Rack port by Bemushroomed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "plugin.hpp"
#include "ui/PngPanelBackground.hpp"
#include "pfm/PfmEngine.h"
#include "pfm/PfmBankLoader.h"
#include "pfm/waveforms/UserWaveforms.h"
#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#include "filesystem/helpers.hh"
#include "patch/patch_file.hh"
#include <span>
#else
#include <osdialog.h>
#endif
#include <map>
#include <vector>

struct Minimalith : Module {
    enum ParamId {
        // MetaModule-visible controls in panel traversal order.
        LOAD_PARAM,
        PATCH_PARAM,
        VOLUME_PARAM,
        PANIC_PARAM,
        ALGO_PARAM,
        EVO_TRIM_PARAM,
        IM_TRIM_PARAM,
        DRIFT_TRIM_PARAM,
        TEAR_TRIM_PARAM,
        TEAR_FOLD_SWITCH_PARAM,
        OCTAVE_TRIM_PARAM,
        SC_AMOUNT_PARAM,
        SC_ATTACK_PARAM,
        SC_RELEASE_PARAM,
#ifndef METAMODULE
        // VCV-only controls after MM-visible ones.
        MORPH_PARAM,
        PRESET_PREV_PARAM,
        PRESET_NEXT_PARAM,
#endif
        PARAMS_LEN
    };
    enum InputId {
        GATE_INPUT,
        PITCH_INPUT,
        SIDECHAIN_INPUT,
        TEAR_INPUT,
        CV1_INPUT,
        CV2_INPUT,
        EVO_INPUT,
        NEXT_TRIG_INPUT,
        DRIFT_INPUT,
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
    // Display IDs must not overlap with Light IDs.
    enum DisplayId {
        PATCH_DISPLAY = LIGHTS_LEN,
        DISPLAY_IDS_LEN
    };
#endif

    PfmEngine engine;
    PfmBankLoader bankLoader;

    // Stable numeric values from PreenFM2's OscShape enum (src/pfm/SynthState.h).
    // Declared here so both VCV Rack and MetaModule builds can bake trims into patches
    // without including SynthState.h (which conflicts with Rack's ui::MenuItem).
    static constexpr int ML_OSC_SHAPE_SIN = 0;
    static constexpr int ML_OSC_SHAPE_SAW = 1;
    static constexpr int ML_OSC_SHAPE_SQUARE = 2;
    static constexpr int ML_OSC_SHAPE_RAND = 6;
    static constexpr int ML_OSC_SHAPE_OFF = 7;
    static constexpr int ML_FILTER_TEAR_ID = 49;


    int currentPatch = 0;
    // patchNameBase: human-readable preset name without derived suffixes.
    // patchName: rendered on-panel display text (may include derived info).
    std::string patchNameBase = "No Bank";
    std::string patchName = "No Bank";
#ifndef METAMODULE
    // Display-only: small top-right label showing current FX type.
    std::string fxLabel;
#endif
    std::string bankPath;
    bool engineReady = false;
    float lastSampleRate = 0.0f;
    float sidechainEnv = 0.0f;

#ifndef METAMODULE
    OneSynthParams currentParams;
    bool currentParamsValid = false;
    OneSynthParams copiedPreset;
    bool hasCopiedPreset = false;
#endif

    static float mlSmoothstep01(float x) {
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return x * x * (3.0f - 2.0f * x);
    }

#ifdef METAMODULE
    static std::string mmNormalizeVolumeRoot(std::string volume) {
        if (volume.empty() || volume == "ram:/" || volume == "ram:") {
            return std::string();
        }
        if (volume.back() != '/') {
            volume.push_back('/');
        }
        return volume;
    }

    static void mmAppendUniqueString(std::vector<std::string>& values, const std::string& value) {
        if (value.empty()) return;
        for (const std::string& existing : values) {
            if (existing == value) return;
        }
        values.push_back(value);
    }

    static std::vector<std::string> mmVolumeSearchOrder() {
        std::vector<std::string> volumes;
        mmAppendUniqueString(volumes, mmNormalizeVolumeRoot(std::string(MetaModule::Patch::get_volume())));
        mmAppendUniqueString(volumes, std::string("sdc:/"));
        mmAppendUniqueString(volumes, std::string("usb:/"));
        mmAppendUniqueString(volumes, std::string("nor:/"));
        if (volumes.empty()) {
            volumes.push_back("sdc:/");
        }
        return volumes;
    }

    static std::string mmPreferredVolumeRoot() {
        std::vector<std::string> volumes = mmVolumeSearchOrder();
        return volumes.empty() ? std::string("sdc:/") : volumes.front();
    }
#endif

    static float mlExpMap01(float x, float minVal, float maxVal) {
        x = clamp(x, 0.0f, 1.0f);
        if (minVal <= 0.0f || maxVal <= minVal) {
            return minVal;
        }
        return minVal * std::pow(maxVal / minVal, x);
    }

    static inline uint32_t mlXorshift32(uint32_t& state) {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    static inline float mlU01FromU32(uint32_t x) {
        return (float)((x >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
    }

    // Bake Minimalith's trim-driven modulation into actual PreenFM2 patch params.
    // This makes the saved .bnk portable to the original hardware.
    static void mlBakeImScanTrimIntoPatch(OneSynthParams& p, float imTrim01) {
        if (!(imTrim01 == imTrim01)) return;
        imTrim01 = clamp(imTrim01, 0.0f, 1.0f);
        float cv2 = (imTrim01 * 2.0f) - 1.0f;
        float phase = (cv2 + 1.0f) * 0.5f;
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;

        float amt = fabsf(imTrim01 - 0.5f) * 2.0f;
        if (amt < 0.0f) amt = 0.0f;
        if (amt > 1.0f) amt = 1.0f;
        if (amt <= 0.0001f) return;

        const float baseIm[5] = {
            p.engineIm1.modulationIndex1,
            p.engineIm1.modulationIndex2,
            p.engineIm2.modulationIndex3,
            p.engineIm2.modulationIndex4,
            p.engineIm3.modulationIndex5,
        };
        const float baseImV[5] = {
            p.engineIm1.modulationIndexVelo1,
            p.engineIm1.modulationIndexVelo2,
            p.engineIm2.modulationIndexVelo3,
            p.engineIm2.modulationIndexVelo4,
            p.engineIm3.modulationIndexVelo5,
        };

        float outIm[5] = {0};
        float outV[5] = {0};
        for (int i = 0; i < 5; ++i) {
            float center = i * 0.25f;
            float dist = fabsf(phase - center);
            float bump = 1.0f - dist * 4.0f;
            if (bump < 0.0f) bump = 0.0f;
            bump = mlSmoothstep01(bump);
            float imMult = 0.2f + 2.3f * bump;

            float vCenter = center + 0.125f;
            float vDist = fabsf(phase - vCenter);
            float vBump = 1.0f - vDist * 4.0f;
            if (vBump < 0.0f) vBump = 0.0f;
            vBump = mlSmoothstep01(vBump);
            float veloMult = 0.2f + 2.3f * vBump;

            imMult = 1.0f + (imMult - 1.0f) * amt;
            veloMult = 1.0f + (veloMult - 1.0f) * amt;

            outIm[i] = baseIm[i] * imMult;
            outV[i] = baseImV[i] * veloMult;
        }

        p.engineIm1.modulationIndex1 = outIm[0];
        p.engineIm1.modulationIndexVelo1 = outV[0];
        p.engineIm1.modulationIndex2 = outIm[1];
        p.engineIm1.modulationIndexVelo2 = outV[1];
        p.engineIm2.modulationIndex3 = outIm[2];
        p.engineIm2.modulationIndexVelo3 = outV[2];
        p.engineIm2.modulationIndex4 = outIm[3];
        p.engineIm2.modulationIndexVelo4 = outV[3];
        p.engineIm3.modulationIndex5 = outIm[4];
        p.engineIm3.modulationIndexVelo5 = outV[4];
    }

    static void mlBakeEvoTrimIntoPatch(OneSynthParams& p, float evo01) {
        if (!(evo01 == evo01)) return;
        evo01 = clamp(evo01, 0.0f, 1.0f);
        if (evo01 <= 0.0001f) return;

        float shapeF;
        if (evo01 < 0.25f)      shapeF = (float)ML_OSC_SHAPE_SIN;
        else if (evo01 < 0.50f) shapeF = (float)ML_OSC_SHAPE_SAW;
        else if (evo01 < 0.75f) shapeF = (float)ML_OSC_SHAPE_SQUARE;
        else                    shapeF = (float)ML_OSC_SHAPE_RAND;

        struct OscillatorParams* oscs[6] = {
            &p.osc1, &p.osc2, &p.osc3, &p.osc4, &p.osc5, &p.osc6
        };
        const float baseShape[6] = {
            p.osc1.shape, p.osc2.shape, p.osc3.shape, p.osc4.shape, p.osc5.shape, p.osc6.shape
        };
        for (int i = 0; i < 6; ++i) {
            if (baseShape[i] != (float)ML_OSC_SHAPE_OFF) {
                oscs[i]->shape = shapeF;
            }
        }

        const float baseAttack[6] = { p.env1a.attackTime, p.env2a.attackTime, p.env3a.attackTime, p.env4a.attackTime, p.env5a.attackTime, p.env6a.attackTime };
        const float baseDecay[6]  = { p.env1a.decayTime,  p.env2a.decayTime,  p.env3a.decayTime,  p.env4a.decayTime,  p.env5a.decayTime,  p.env6a.decayTime };
        const float baseRelease[6]= { p.env1b.releaseTime,p.env2b.releaseTime,p.env3b.releaseTime,p.env4b.releaseTime,p.env5b.releaseTime,p.env6b.releaseTime };

        float attackMult = expf(-2.99573f * evo01);
        float decayMult = expf(-2.30259f * evo01);
        float releaseMult = expf(1.60944f * evo01);

        struct EnvelopeParamsA* envA[6] = { &p.env1a, &p.env2a, &p.env3a, &p.env4a, &p.env5a, &p.env6a };
        struct EnvelopeParamsB* envB[6] = { &p.env1b, &p.env2b, &p.env3b, &p.env4b, &p.env5b, &p.env6b };
        for (int i = 0; i < 6; ++i) {
            float att = baseAttack[i] * attackMult;
            if (att < 0.01f) att = 0.01f;
            envA[i]->attackTime = att;

            float dec = baseDecay[i] * decayMult;
            if (dec < 0.01f) dec = 0.01f;
            envA[i]->decayTime = dec;

            float rel = baseRelease[i] * releaseMult;
            if (rel > 16.0f) rel = 16.0f;
            envB[i]->releaseTime = rel;
        }
    }

    static void mlBakeDriftTrimIntoPatch(OneSynthParams& p, float driftTrim01) {
        if (!(driftTrim01 == driftTrim01)) return;
        driftTrim01 = clamp(driftTrim01, 0.0f, 1.0f);
        float drift = (driftTrim01 * 2.0f) - 1.0f;

        float absDrift = fabsf(drift);
        float curved = (absDrift > 0.0001f) ? powf(absDrift, 0.75f) : 0.0f;
        drift = (drift < 0.0f) ? -curved : curved;
        if (fabsf(drift) <= 0.0001f) return;

        static const float spreadPattern[6] = { +1.00f, -0.73f, +1.27f, -1.13f, +0.53f, -0.87f };
        const float maxDetune = 6.0f;

        struct OscillatorParams* oscs[6] = { &p.osc1, &p.osc2, &p.osc3, &p.osc4, &p.osc5, &p.osc6 };
        const float baseShape[6] = { p.osc1.shape, p.osc2.shape, p.osc3.shape, p.osc4.shape, p.osc5.shape, p.osc6.shape };
        const float baseDetune[6] = { p.osc1.detune, p.osc2.detune, p.osc3.detune, p.osc4.detune, p.osc5.detune, p.osc6.detune };

        for (int i = 0; i < 6; ++i) {
            if (baseShape[i] != (float)ML_OSC_SHAPE_OFF) {
                oscs[i]->detune = baseDetune[i] + drift * maxDetune * spreadPattern[i];
            }
        }
    }

    static std::string mlFormatMissingUserWaves(uint8_t mask) {
        if (mask == 0) return std::string();
        std::string s = "MISSING ";
        bool first = true;
        for (int i = 0; i < 6; ++i) {
            if (mask & (1u << i)) {
                if (!first) s += "+";
                first = false;
                s += "USR";
                s += std::to_string(i + 1);
            }
        }
        return s;
    }

#ifndef METAMODULE
    // Deprecated: old VCV-only external trim recall mechanism.
    // We keep the file format around for compatibility.
#endif

    // Per-bank, per-slot sidecar values used by both Rack and MetaModule.
    // This keeps UI trims/switches recallable even when they are not native
    // PreenFM2 patch parameters.
    struct TrimState {
        float evo = 0.0f;
        float im = 0.5f;
        float drift = 0.5f;
        float tear = 0.0f;
        float tearFold = 1.0f;
    };
    std::map<int, TrimState> trimBySlot;
#ifndef METAMODULE
    TrimState copiedTrimState;
#endif

    std::string trimsDbPath() const {
#ifdef METAMODULE
        return mmPreferredVolumeRoot() + "minimalith/MinimalithTrims.json";
#else
        return asset::user("MorphWorx/MinimalithTrims.json");
#endif
    }

    std::string trimsDbPathLegacy() const {
#ifdef METAMODULE
        return trimsDbPath();
#else
        return asset::user("Bemushroomed/MinimalithTrims.json");
#endif
    }

    std::string trimsBankKey() const {
        // Normalize path string so trims recall even if the stored bankPath
        // uses different separators/casing (common across Rack patches/versions).
        std::string k = bankPath;
        for (char& c : k) {
            if (c == '/') c = '\\';
        }
#if defined(_WIN32) || defined(ARCH_WIN)
        for (char& c : k) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }

        // Note: keep the rest of the trim DB key logic platform-agnostic.
#endif
        return k;
    }

    void trimsLoadForBank() {
        trimBySlot.clear();
        if (bankPath.empty()) return;

        json_error_t err;
        std::string p = trimsDbPath();
        json_t* root = json_load_file(p.c_str(), 0, &err);
        if (!root) {
            std::string legacy = trimsDbPathLegacy();
            if (legacy != p)
                root = json_load_file(legacy.c_str(), 0, &err);
        }
        if (!root) return;

        json_t* banksJ = json_object_get(root, "banks");
        if (!banksJ || !json_is_object(banksJ)) {
            json_decref(root);
            return;
        }

        // Primary lookup: normalized key.
        std::string bankKey = trimsBankKey();
        json_t* bankJ = json_object_get(banksJ, bankKey.c_str());

        // Fallback lookup: try a key that only differs by slash direction.
        // (Older patches/configs may have preserved one style.)
        if (!bankJ || !json_is_object(bankJ)) {
            std::string alt = bankKey;
            for (char& c : alt) {
                if (c == '\\') c = '/';
            }
            bankJ = json_object_get(banksJ, alt.c_str());
        }

        // Fallback lookup: basename match (bank moved to a different folder).
        if (!bankJ || !json_is_object(bankJ)) {
            std::string base = bankKey;
            size_t pos = base.find_last_of("/\\\\");
            if (pos != std::string::npos) base = base.substr(pos + 1);
            const char* k;
            json_t* v;
            json_object_foreach(banksJ, k, v) {
                if (!json_is_object(v)) continue;
                std::string kk = k ? std::string(k) : std::string();
#if defined(_WIN32) || defined(ARCH_WIN)
                for (char& c : kk) {
                    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                }
#endif
                size_t p2 = kk.find_last_of("/\\\\");
                std::string bb = (p2 != std::string::npos) ? kk.substr(p2 + 1) : kk;
                if (!bb.empty() && bb == base) {
                    bankJ = v;
                    break;
                }
            }
        }

        if (!bankJ || !json_is_object(bankJ)) {
            json_decref(root);
            return;
        }

        const char* slotKey;
        json_t* slotJ;
        json_object_foreach(bankJ, slotKey, slotJ) {
            int slot = atoi(slotKey);
            if (slot < 0 || slot >= 128) continue;
            if (!json_is_object(slotJ)) continue;
            TrimState st;
            json_t* evoJ = json_object_get(slotJ, "evo");
            json_t* imJ = json_object_get(slotJ, "im");
            json_t* driftJ = json_object_get(slotJ, "drift");
            json_t* tearJ = json_object_get(slotJ, "tear");
            json_t* tearFoldJ = json_object_get(slotJ, "tearFold");
            if (evoJ) st.evo = clamp((float)json_number_value(evoJ), 0.0f, 1.0f);
            if (imJ) st.im = clamp((float)json_number_value(imJ), 0.0f, 1.0f);
            if (driftJ) st.drift = clamp((float)json_number_value(driftJ), 0.0f, 1.0f);
            if (tearJ) st.tear = clamp((float)json_number_value(tearJ), 0.0f, 1.0f);
            if (tearFoldJ) st.tearFold = clamp((float)json_number_value(tearFoldJ), 0.0f, 1.0f);
            trimBySlot[slot] = st;
        }

        json_decref(root);

#ifdef METAMODULE
        // Pre-seed every patch slot so that trimsCaptureCurrentToSlot() in
        // the audio thread never inserts a new key (which would heap-alloc).
        if (bankLoader.isLoaded()) {
            int count = bankLoader.getPatchCount();
            for (int i = 0; i < count && i < 128; i++) {
                if (trimBySlot.find(i) == trimBySlot.end())
                    trimBySlot.emplace(i, TrimState{});
            }
        }
#endif
    }

    void trimsSaveForBank() {
        if (bankPath.empty()) return;

        json_t* root = json_object();
        json_t* banksJ = json_object();
        json_object_set_new(root, "banks", banksJ);

        // Load existing file (best effort) so we don't clobber other banks.
        json_error_t err;
        std::string p = trimsDbPath();
        json_t* existing = json_load_file(p.c_str(), 0, &err);
        if (!existing) {
            std::string legacy = trimsDbPathLegacy();
            if (legacy != p)
                existing = json_load_file(legacy.c_str(), 0, &err);
        }
        if (existing && json_is_object(existing)) {
            json_t* existingBanks = json_object_get(existing, "banks");
            if (existingBanks && json_is_object(existingBanks)) {
                const char* k;
                json_t* v;
                json_object_foreach(existingBanks, k, v) {
                    json_object_set(banksJ, k, v);
                }
            }
            json_decref(existing);
        }

        json_t* bankJ = json_object();
        for (const auto& kv : trimBySlot) {
            int slot = kv.first;
            const TrimState& st = kv.second;
            json_t* slotObj = json_object();
            json_object_set_new(slotObj, "evo", json_real(st.evo));
            json_object_set_new(slotObj, "im", json_real(st.im));
            json_object_set_new(slotObj, "drift", json_real(st.drift));
            json_object_set_new(slotObj, "tear", json_real(st.tear));
            json_object_set_new(slotObj, "tearFold", json_real(st.tearFold));
            json_object_set_new(bankJ, std::to_string(slot).c_str(), slotObj);
        }
        json_object_set_new(banksJ, trimsBankKey().c_str(), bankJ);

        // Ensure directory exists
        system::createDirectories(system::getDirectory(p));
        json_dump_file(root, p.c_str(), JSON_INDENT(2));
        json_decref(root);
    }

    TrimState trimsGetForSlot(int slot) const {
        auto it = trimBySlot.find(slot);
        if (it != trimBySlot.end()) return it->second;
        return TrimState{};
    }

    void trimsApplyForSlot(int slot) {
        // If we don't have a saved trim entry for this slot/bank, do NOT force defaults.
        // This preserves Rack patch-saved trimpot values and avoids audible regressions.
        auto it = trimBySlot.find(slot);
        if (it == trimBySlot.end()) return;
        const TrimState& st = it->second;
        params[EVO_TRIM_PARAM].setValue(st.evo);
        params[IM_TRIM_PARAM].setValue(st.im);
        params[DRIFT_TRIM_PARAM].setValue(st.drift);
        params[TEAR_TRIM_PARAM].setValue(st.tear);
        params[TEAR_FOLD_SWITCH_PARAM].setValue(st.tearFold > 0.5f ? 1.0f : 0.0f);
    }

    void trimsCaptureCurrentToSlot(int slot) {
        TrimState st;
        st.evo = clamp(params[EVO_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.im = clamp(params[IM_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.drift = clamp(params[DRIFT_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.tear = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.tearFold = params[TEAR_FOLD_SWITCH_PARAM].getValue() > 0.5f ? 1.0f : 0.0f;
#ifdef METAMODULE
        // On MetaModule, only update keys that already exist in the map to
        // avoid heap allocation in the audio thread. New slots are
        // pre-seeded by trimsLoadForBank(); dataFromJson() handles the rest.
        {
            auto it = trimBySlot.find(slot);
            if (it != trimBySlot.end()) it->second = st;
        }
#else
        trimBySlot[slot] = st;
#endif
    }

    // Edge detectors
    bool lastGate = false;
    float lastNextTrig = 0.0f;
#ifdef METAMODULE
    // Debounce RANDOM trigger on MetaModule: loadPatch() + afterNewParamsLoad() is
    // expensive (~144 ADSR reloads, all LFO reinit). If an envelope is patched into
    // RANDOM it fires on every rising edge; without a cooldown this causes CPU spikes.
    // Allow at most one random-patch switch per ~0.5 s (24 000 samples at 48 kHz).
    static constexpr uint32_t kRandomCooldownSamples = 24000;
    uint32_t mmRandomCooldown = 0;
#endif
    bool lastLoadBtn = false;
    bool lastPanicBtn = false;
#ifndef METAMODULE
    bool lastMorphBtn = false;
    dsp::SchmittTrigger presetPrevButtonTrigger;
    dsp::SchmittTrigger presetNextButtonTrigger;
    static constexpr float kPresetButtonCooldownSeconds = 0.12f;
    uint32_t presetButtonCooldownRemaining = 0;
#endif
    float lastPitch = 0.0f;  // Track pitch for retrigger detection
    bool exportTearLegacyFallback = true;
    static constexpr int kTearDelaySize = 128;
    float tearDelayL[kTearDelaySize] = {};
    float tearDelayR[kTearDelaySize] = {};
    int tearDelayWrite = 0;
    uint32_t tearNoiseState = 0x6D2B79F5u;
    float tearWet = 0.0f;

    void resetTearState() {
        for (int i = 0; i < kTearDelaySize; ++i) {
            tearDelayL[i] = 0.0f;
            tearDelayR[i] = 0.0f;
        }
        tearDelayWrite = 0;
        tearNoiseState = 0x6D2B79F5u;
        tearWet = 0.0f;
    }

    volatile bool loadRequested = false;
#ifdef METAMODULE
    volatile bool pendingBankLoad = false;
    bool loadBrowsing = false;

    // MetaModule: avoid std::string churn in audio thread.
    // We keep a fixed-size current preset name buffer for the on-module text display.
    char mmPresetName[32] = {0};

    // MetaModule: "soft takeover" for physical trimpots.
    // On preset change, we capture the current knob positions as baselines and ignore
    // their values until the user moves a control past a small threshold.
    struct Pickup01 {
        float baseline = 0.0f;
        bool active = false;
    };
    Pickup01 evoPickup;
    Pickup01 imPickup;
    Pickup01 driftPickup;
    Pickup01 tearPickup;
    int algoBaselineQ = 0;
    bool algoPickupActive = false;

    void mmResetPickupBaselines() {
        evoPickup.baseline = clamp(params[EVO_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        evoPickup.active = false;
        imPickup.baseline = clamp(params[IM_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        imPickup.active = false;
        driftPickup.baseline = clamp(params[DRIFT_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        driftPickup.active = false;

        tearPickup.baseline = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        tearPickup.active = false;

        algoBaselineQ = (int)(params[ALGO_PARAM].getValue() + 0.5f);
        if (algoBaselineQ < 0) algoBaselineQ = 0;
        if (algoBaselineQ > 27) algoBaselineQ = 27;
        algoPickupActive = false;
    }

    static bool mmPickup01Active(Pickup01& st, float current01, float threshold01) {
        current01 = clamp(current01, 0.0f, 1.0f);
        if (!st.active && fabsf(current01 - st.baseline) > threshold01) {
            st.active = true;
        }
        return st.active;
    }
#endif

#ifdef METAMODULE
    static void mlCopyTrimPresetNameToBuf(const char* presetName13, char* out, size_t outSize) {
        if (!out || outSize == 0) return;
        out[0] = 0;
        if (!presetName13) return;

        // Copy up to 13 chars, stop at NUL.
        size_t n = 0;
        for (; n + 1 < outSize && n < 13; ++n) {
            char c = presetName13[n];
            if (c == 0) break;
            out[n] = c;
        }
        out[n] = 0;

        // Trim trailing spaces.
        while (n > 0 && out[n - 1] == ' ') {
            out[n - 1] = 0;
            --n;
        }
    }

    static void mlFormatMissingUserWavesToBuf(uint8_t mask, char* out, size_t outSize) {
        if (!out || outSize == 0) return;
        out[0] = 0;
        if (mask == 0) return;

        // Format: "MISSING USR1+USR3"
        size_t used = 0;
        int w = snprintf(out, outSize, "MISSING ");
        if (w < 0) return;
        used = (size_t)w;
        bool first = true;
        for (int i = 0; i < 6; ++i) {
            if ((mask & (1u << i)) == 0) continue;
            const char* sep = first ? "" : "+";
            first = false;
            if (used >= outSize) break;
            w = snprintf(out + used, outSize - used, "%sUSR%d", sep, i + 1);
            if (w < 0) break;
            used += (size_t)w;
        }
    }

    size_t get_display_text(int display_id, std::span<char> text) override {
        if (display_id != PATCH_DISPLAY) return 0;
        if (text.empty()) return 0;

        char buf[128];
        buf[0] = '\0';

        if (bankLoader.isLoaded()) {
            int count = bankLoader.getPatchCount();
            if (count < 1) count = 1;
            // Two-line display: name + patch index
            const char* name = (mmPresetName[0] != 0) ? mmPresetName : "(unnamed)";
            snprintf(buf, sizeof(buf), "%s\n%02d/%02d", name, currentPatch + 1, count);
        } else {
            // No bank loaded yet
            const char* name = (mmPresetName[0] != 0) ? mmPresetName : "No Bank";
            snprintf(buf, sizeof(buf), "%s", name);
        }

        size_t n = std::min(strlen(buf), text.size());
        std::memcpy(text.data(), buf, n);
        return n;
    }

    std::string metaModuleLoadStartDir() const {
        return mmPreferredVolumeRoot() + "minimalith/";
    }

    std::vector<std::string> metaModuleBankLoadCandidates(const std::string& requestedPath) const {
        std::vector<std::string> candidates;
        if (MetaModule::Filesystem::is_local_path(requestedPath)) {
            mmAppendUniqueString(candidates, requestedPath);
        }

        std::string filename = requestedPath;
        auto pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }
        if (!filename.empty()) {
            for (const std::string& volume : mmVolumeSearchOrder()) {
                mmAppendUniqueString(candidates, volume + "minimalith/" + filename);
                mmAppendUniqueString(candidates, volume + "MorphWorx/" + filename);
            }
        }
        return candidates;
    }
#endif

    Minimalith() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(PATCH_PARAM, 0.f, 1.f, 0.f, "Preset Select");
        configParam(ALGO_PARAM, 0.f, 27.f, 0.f, "Algorithm");
        paramQuantities[ALGO_PARAM]->snapEnabled = true;
        configButton(PANIC_PARAM, "Panic / Reset Voice");
        configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume");
        configParam(SC_AMOUNT_PARAM, 0.f, 1.f, 0.f, "Sidechain amount");
        configParam(SC_ATTACK_PARAM, 0.f, 1.f, 0.18f, "Sidechain attack");
        configParam(SC_RELEASE_PARAM, 0.f, 1.f, 0.38f, "Sidechain release");
        configParam(EVO_TRIM_PARAM, 0.f, 1.f, 0.f, "EVO Trim (manual when EVO jack unplugged)");
        configParam(IM_TRIM_PARAM, 0.f, 1.f, 0.5f, "IM Scan Trim (manual when IM SCAN jack unplugged)");
        configParam(DRIFT_TRIM_PARAM, 0.f, 1.f, 0.5f, "Drift Trim (manual when DRIFT jack unplugged)");
        configParam(TEAR_TRIM_PARAM, 0.f, 1.f, 0.f, "Tear");
        configSwitch(TEAR_FOLD_SWITCH_PARAM, 0.f, 1.f, 1.f, "Tear Fold", {"Off", "On"});
        configParam(OCTAVE_TRIM_PARAM, -5.f, 5.f, 0.f, "Octave");
        paramQuantities[OCTAVE_TRIM_PARAM]->snapEnabled = true;
        configButton(LOAD_PARAM, "Load Bank");
#ifndef METAMODULE
        configButton(MORPH_PARAM, "Morph 2 Random Presets");
        configButton(PRESET_PREV_PARAM, "Previous Preset");
        configButton(PRESET_NEXT_PARAM, "Next Preset");
#endif
        configInput(GATE_INPUT, "Gate");
        configInput(PITCH_INPUT, "V/Oct Pitch");
        configInput(SIDECHAIN_INPUT, "Sidechain CV (0-10V envelope)");
        configInput(TEAR_INPUT, "Tear CV (-5V..+5V)");
        configInput(CV1_INPUT, "Algorithm CV (-5V..+5V sweeps Algo 1-28)");
        configInput(CV2_INPUT, "IM Scanner CV (-5V..+5V spotlights IM1-IM5)");
        configInput(EVO_INPUT, "Evolution CV (0-10V: shape + envelope modulation)");
        configInput(NEXT_TRIG_INPUT, "Random Preset Trigger");
        configInput(DRIFT_INPUT, "Drift CV (-5V..+5V: operator detune spread)");
        configOutput(LEFT_OUTPUT, "Left Audio");
        configOutput(RIGHT_OUTPUT, "Right Audio");

    #ifdef METAMODULE
        snprintf(mmPresetName, sizeof(mmPresetName), "%s", "No Bank");
    #endif
        
        // Don't initialize engine in constructor - defer to onAdd when VCV environment is ready
        // This prevents crashes from premature initialization
    }

    void onAdd() override {
        // Initialize engine with a safe default sample rate.
        // We avoid APP->engine here because MetaModule doesn't provide it,
        // and even in VCV Rack APP may not be available in all contexts.
        if (!engineReady) {
            engine.init(44100.0f);
            engineReady = true;
            lastSampleRate = 44100.0f;
        }
        
        // If we have a saved bank path, reload it
        if (!bankPath.empty()) {
#ifdef METAMODULE
            for (const std::string& candidate : metaModuleBankLoadCandidates(bankPath)) {
                loadBankAndPatch(candidate, currentPatch);
                if (bankLoader.isLoaded()) {
                    break;
                }
            }
#else
            loadBankAndPatch(bankPath, currentPatch);
#endif
        }
        else {
            tryLoadFactoryDefaultBank();
        }
    }

    bool tryLoadFactoryDefaultBank() {
        int startupPatch = std::max(0, currentPatch);
#ifdef METAMODULE
        auto tryLoadFactoryPath = [&](const std::string& candidate) {
            if (candidate.empty()) return false;
            loadBankAndPatch(candidate, startupPatch);
            return bankLoader.isLoaded();
        };

        std::vector<std::string> candidates;
        for (const std::string& volume : mmVolumeSearchOrder()) {
            mmAppendUniqueString(candidates, volume + "minimalith/Default.bnk");
            mmAppendUniqueString(candidates, volume + "MorphWorx/Default.bnk");
        }
        for (const std::string& candidate : candidates) {
            if (tryLoadFactoryPath(candidate)) return true;
        }
        return false;
#else
        loadBankAndPatch(asset::plugin(pluginInstance, "def/Default.bnk"), startupPatch);
        return bankLoader.isLoaded();
#endif
    }

    #ifdef METAMODULE
    void loadMetaModuleUserWaveforms(const std::string& path) {
        auto tryDir = [&](const std::string& dirPath) {
            if (!dirPath.empty()) {
                pfm::loadUserWaveformsFromDir(dirPath);
            }
        };

        auto tryVolume = [&](const std::string& volumeRoot) {
            if (volumeRoot.empty()) return;
            tryDir(volumeRoot + "minimalith/userwaveforms");
            tryDir(volumeRoot + "MorphWorx/userwaveforms");
            tryDir(volumeRoot + "morphworx/userwaveforms");
            tryDir(volumeRoot + "Bemushroomed/userwaveforms");
            tryDir(volumeRoot + "bemushroomed/userwaveforms");
            tryDir(volumeRoot + "pfm2/userwaveforms");
            tryDir(volumeRoot + "pfm2/waveform");
            tryDir(volumeRoot + "minimalith/waveform");
        };

        std::string preferredVolume;
        if (MetaModule::Filesystem::is_local_path(path)) {
            auto separator = path.find(':');
            if (separator != std::string::npos) {
                preferredVolume = mmNormalizeVolumeRoot(path.substr(0, separator + 1));
            }
        }
        if (preferredVolume.empty()) {
            preferredVolume = mmNormalizeVolumeRoot(std::string(MetaModule::Patch::get_volume()));
        }

        std::vector<std::string> searchVolumes;
        mmAppendUniqueString(searchVolumes, preferredVolume);
        for (const std::string& volume : mmVolumeSearchOrder()) {
            mmAppendUniqueString(searchVolumes, volume);
        }
        for (const std::string& volume : searchVolumes) {
            tryVolume(volume);
        }
    }
    #endif

    void loadBankAndPatch(const std::string& path, int patchIdx) {
        // (Re)load User1..User6 waveforms when loading a bank.
        // Must never do file I/O in the audio thread.
    #ifdef METAMODULE
        loadMetaModuleUserWaveforms(path);
    #else
        // Prefer plugin-bundled folder so it can ship inside the .vcvplugin.
        // Users can still override by placing files in their Rack user dir.
        pfm::loadUserWaveformsFromDir(asset::plugin(pluginInstance, "userwaveforms"));
        pfm::loadUserWaveformsFromDir(asset::user("MorphWorx/userwaveforms"));
        pfm::loadUserWaveformsFromDir(asset::user("Bemushroomed/userwaveforms"));
        // Legacy fallbacks
        pfm::loadUserWaveformsFromDir(asset::user("pfm2/waveform"));
        pfm::loadUserWaveformsFromDir(asset::user("MorphWorx/pfm2/waveform"));
        pfm::loadUserWaveformsFromDir(asset::user("Bemushroomed/pfm2/waveform"));
    #endif

        if (bankLoader.loadBank(path)) {
            bankPath = path;
            currentPatch = patchIdx;
            trimsLoadForBank();
            // Release any playing note before switching presets to prevent voice crashes  
            if (engineReady) {
                engine.noteOff();
            }
            loadCurrentPatch();
            // Sync preset knob to loaded position
            int count = bankLoader.getPatchCount();
            if (count > 1) {
                params[PATCH_PARAM].setValue((float)patchIdx / (float)(count -1));
            }
        } else {
    #ifdef METAMODULE
            snprintf(mmPresetName, sizeof(mmPresetName), "%s", "Load Error");
    #else
            patchNameBase = "Load Error";
            patchName = patchNameBase;
            currentParamsValid = false;
    #endif
        }
    }

    void loadCurrentPatch() {
        if (!bankLoader.isLoaded()) return;
        // Release any playing note before switching presets to prevent voice hangs
        if (engineReady) {
            engine.noteOff();
        }
        OneSynthParams patchParams;
        if (bankLoader.getPatch(currentPatch, patchParams)) {
#ifndef METAMODULE
            currentParams = patchParams;
            currentParamsValid = true;
#endif
            engine.loadPatch(patchParams);
            // In VCV we can safely sync the virtual knob to the patch.
            // On MetaModule the physical trimpot can't move, so don't fight it.
#ifndef METAMODULE
            params[ALGO_PARAM].setValue((float)engine.getBaseAlgo());
        #endif

            // Apply per-slot sidecar values (trim + tear controls) if available.
            trimsApplyForSlot(currentPatch);

            // If no sidecar exists for this slot, recover TEAR controls from patch FX
            // so hardware-written TEAR presets remain portable.
            auto it = trimBySlot.find(currentPatch);
            if (it == trimBySlot.end()) {
                int effectType = (int)(patchParams.effect.type + 0.5f);
                if (effectType == ML_FILTER_TEAR_ID) {
                    params[TEAR_TRIM_PARAM].setValue(clamp(patchParams.effect.param1, 0.0f, 1.0f));
                    params[TEAR_FOLD_SWITCH_PARAM].setValue(patchParams.effect.param2 > 0.5f ? 1.0f : 0.0f);
                } else {
                    params[TEAR_TRIM_PARAM].setValue(0.0f);
                    params[TEAR_FOLD_SWITCH_PARAM].setValue(1.0f);
                }
            }

    #ifndef METAMODULE
            // If no sidecar slot data exists, keep prior behavior defaults.
            TrimState slotState = trimsGetForSlot(currentPatch);
            it = trimBySlot.find(currentPatch);
            if (it == trimBySlot.end()) {
            params[IM_TRIM_PARAM].setValue(0.5f);
            params[EVO_TRIM_PARAM].setValue(0.0f);
            params[DRIFT_TRIM_PARAM].setValue(0.5f);
            }
    #endif
#ifdef METAMODULE
            // MetaModule: avoid std::string allocations in audio thread.
            mlCopyTrimPresetNameToBuf(patchParams.presetName, mmPresetName, sizeof(mmPresetName));
            if (mmPresetName[0] == 0) {
                snprintf(mmPresetName, sizeof(mmPresetName), "Patch %d", currentPatch + 1);
            }

            // If the patch is hard-muted due to missing USER waves, show explicit info.
            if (engine.isMissingUserWave()) {
                char msg[32];
                mlFormatMissingUserWavesToBuf(engine.getMissingUserWavesMask(), msg, sizeof(msg));
                if (msg[0] != 0) {
                    snprintf(mmPresetName, sizeof(mmPresetName), "%s", msg);
                } else {
                    snprintf(mmPresetName, sizeof(mmPresetName), "%s", "MISSING USR");
                }
            }

            // Reset soft-takeover baselines so the newly loaded preset is not modified
            // by existing physical trimpot positions until the user moves them.
            mmResetPickupBaselines();
#else
            // String operations only on VCV Rack (avoid heap allocations
            // in the audio thread on MetaModule, which can cause crashes)
            std::string name = bankLoader.getPatchName(currentPatch);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            if (name.empty()) {
                patchNameBase = "Patch " + std::to_string(currentPatch + 1);
            } else {
                patchNameBase = name;
            }

            // If the patch is hard-muted due to missing USER waves, show explicit info.
            if (engine.isMissingUserWave()) {
                std::string msg = mlFormatMissingUserWaves(engine.getMissingUserWavesMask());
                if (!msg.empty()) {
                    patchNameBase = msg;
                } else {
                    patchNameBase = "MISSING USR";
                }
            }

            updatePatchNameDisplay();
#endif
        }
    }

    void goRandomPatch() {
        if (!bankLoader.isLoaded()) return;
        int count = bankLoader.getPatchCount();
        if (count <= 1) return;
        trimsCaptureCurrentToSlot(currentPatch);
#ifndef METAMODULE
        // On MetaModule the audio thread must never do disk I/O.
        trimsSaveForBank();
#endif
        // Pick a random patch different from current
        int newPatch;
        do {
            newPatch = random::u32() % count;
        } while (newPatch == currentPatch && count > 1);
        currentPatch = newPatch;
        loadCurrentPatch();
    }

    void bakeTearInteropForAllBankSlots() {
        if (!bankLoader.isLoaded()) return;

        trimsCaptureCurrentToSlot(currentPatch);
        trimsSaveForBank();

        int count = bankLoader.getPatchCount();
        for (int slot = 0; slot < count; ++slot) {
            OneSynthParams patchParams;
            if (!bankLoader.getPatch(slot, patchParams)) continue;

            auto it = trimBySlot.find(slot);
            if (it == trimBySlot.end()) continue;

            const TrimState& st = it->second;
            float tearExport = clamp(st.tear, 0.0f, 1.0f);
            bool foldOn = st.tearFold > 0.5f;

            int existingFxType = (int)(patchParams.effect.type + 0.5f);
            bool keepExistingTear = (existingFxType == ML_FILTER_TEAR_ID);
            if (tearExport > 0.0001f || keepExistingTear) {
                patchParams.effect.type = exportTearLegacyFallback ? 48.0f : (float)ML_FILTER_TEAR_ID;
                patchParams.effect.param1 = tearExport;
                patchParams.effect.param2 = foldOn ? 1.0f : 0.0f;
                if (patchParams.effect.param3 < 0.0f || patchParams.effect.param3 > 2.0f) {
                    patchParams.effect.param3 = 1.0f;
                }
                bankLoader.setPatch(slot, patchParams);
            }
        }
    }

#ifndef METAMODULE
    // Avoid including PreenFM2 SynthState.h here (conflicts with Rack's ui::MenuItem).
    // These are stable enum ranges from src/pfm/SynthState.h:
    // OscShape valid values: 0..13 (OSC_SHAPE_LAST is 14)
    // LfoType valid values:  0..4  (LFO_TYPE_MAX is 5)
    static constexpr int ML_OSC_SHAPE_MAX = 13;
    static constexpr int ML_LFO_TYPE_MAX = 4;
    // Filter/effect types from src/pfm/SynthState.h enum FILTER_TYPE:
    // Valid values are 0..49 (FILTER_LAST is 50).
    static constexpr int ML_FILTER_TYPE_MAX = 49;

    static int mlRoundClampFilterType(float typeF) {
        // Type is stored as float in patches; keep it stable.
        if (!(typeF == typeF)) return 0; // NaN -> FILTER_OFF
        int t = (typeF >= 0.0f) ? (int)(typeF + 0.5f) : (int)(typeF - 0.5f);
        if (t < 0) t = 0;
        if (t > ML_FILTER_TYPE_MAX) t = ML_FILTER_TYPE_MAX;
        return t;
    }

    static const char* mlFilterTypeName(int type) {
        static const char* kNames[ML_FILTER_TYPE_MAX + 1] = {
            /*  0 */ "Off",
            /*  1 */ "Mixer",
            /*  2 */ "LP",
            /*  3 */ "HP",
            /*  4 */ "Bass",
            /*  5 */ "BP",
            /*  6 */ "Crusher",
            /*  7 */ "LP2",
            /*  8 */ "HP2",
            /*  9 */ "BP2",
            /* 10 */ "LP3",
            /* 11 */ "HP3",
            /* 12 */ "BP3",
            /* 13 */ "Peak",
            /* 14 */ "Notch",
            /* 15 */ "Bell",
            /* 16 */ "LowShelf",
            /* 17 */ "HighShelf",
            /* 18 */ "LPHP",
            /* 19 */ "BPds",
            /* 20 */ "LPWS",
            /* 21 */ "Tilt",
            /* 22 */ "Stereo",
            /* 23 */ "Sat",
            /* 24 */ "Sigmoid",
            /* 25 */ "Fold",
            /* 26 */ "Wrap",
            /* 27 */ "Rot",
            /* 28 */ "Texture1",
            /* 29 */ "Texture2",
            /* 30 */ "LPXOR",
            /* 31 */ "LPXOR2",
            /* 32 */ "LPSin",
            /* 33 */ "HPSin",
            /* 34 */ "QuadNotch",
            /* 35 */ "AP4",
            /* 36 */ "AP4B",
            /* 37 */ "AP4D",
            /* 38 */ "Oryx",
            /* 39 */ "Oryx2",
            /* 40 */ "Oryx3",
            /* 41 */ "18dB",
            /* 42 */ "Ladder",
            /* 43 */ "Ladder2",
            /* 44 */ "Diod",
            /* 45 */ "Krmg",
            /* 46 */ "Teebee",
            /* 47 */ "Svflh",
            /* 48 */ "Crush2",
            /* 49 */ "Tear",
        };

        if (type < 0) type = 0;
        if (type > ML_FILTER_TYPE_MAX) type = ML_FILTER_TYPE_MAX;
        return kNames[type];
    }

    void updatePatchNameDisplay() {
        patchName = patchNameBase;
        fxLabel.clear();
        if (!currentParamsValid) return;
        // If output is hard-muted due to missing USER waves, keep the explicit warning text.
        if (engine.isMissingUserWave()) return;
        int t = mlRoundClampFilterType(currentParams.effect.type);
        fxLabel = "FX: ";
        fxLabel += mlFilterTypeName(t);
    }

    static float mlLerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static float mlClamp01(float v) {
        return clamp(v, 0.0f, 1.0f);
    }

    static float mlClampSigned1(float v) {
        return clamp(v, -1.0f, 1.0f);
    }

    static float mlPickDiscrete(float a, float b) {
        return (random::uniform() < 0.5f) ? a : b;
    }

    static float mlPickDiscreteClamped(float a, float b, int minV, int maxV) {
        int v = (int)(mlPickDiscrete(a, b) + 0.5f);
        if (v < minV) v = minV;
        if (v > maxV) v = maxV;
        return (float)v;
    }

    static void mlSetPresetName(OneSynthParams& p, const char* name) {
        // 13 bytes total; ensure null-terminated.
        for (int i = 0; i < 13; i++) {
            p.presetName[i] = 0;
        }
        for (int i = 0; i < 12 && name[i] != 0; i++) {
            p.presetName[i] = name[i];
        }
        p.presetName[12] = 0;
    }

    static void mlSetPresetNameFromDisplay(OneSynthParams& p, const std::string& displayName) {
        // Convert the Rack display name to a 12-char preset name.
        // Keep it simple: strip trailing spaces, then copy printable ASCII.
        std::string name = displayName;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        for (int i = 0; i < 13; i++) {
            p.presetName[i] = 0;
        }
        int outIdx = 0;
        for (char c : name) {
            if (outIdx >= 12) break;
            unsigned char uc = (unsigned char)c;
            if (uc < 32 || uc > 126) continue;
            p.presetName[outIdx++] = c;
        }
        p.presetName[12] = 0;
    }

    static void mlMorphParams(const OneSynthParams& a, const OneSynthParams& b, float alpha, OneSynthParams& out) {
        out = a;

        // --- Engine (discrete + safe continuous) ---
        out.engine1.algo = mlPickDiscreteClamped(a.engine1.algo, b.engine1.algo, 0, 27);
        out.engine1.velocity = mlClamp01(mlLerp(a.engine1.velocity, b.engine1.velocity, alpha));
        out.engine1.glide = clamp(mlLerp(a.engine1.glide, b.engine1.glide, alpha), 0.0f, 12.0f);

        // PreenFM2 engine wrapper forces 1 voice, but keep sane here anyway.
        out.engine1.numberOfVoice = 1.0f;

        // Engine2
        out.engine2.playMode = mlPickDiscrete(a.engine2.playMode, b.engine2.playMode);
        out.engine2.unisonSpread = mlClamp01(mlLerp(a.engine2.unisonSpread, b.engine2.unisonSpread, alpha));
        out.engine2.unisonDetune = mlClamp01(mlLerp(a.engine2.unisonDetune, b.engine2.unisonDetune, alpha));
        if (out.engine2.unisonDetune < 0.0f) out.engine2.unisonDetune = 0.0f;
        out.engine2.pfmVersion = 2.0f;

        // --- IM (continuous, 0..1) ---
        out.engineIm1.modulationIndex1 = mlClamp01(mlLerp(a.engineIm1.modulationIndex1, b.engineIm1.modulationIndex1, alpha));
        out.engineIm1.modulationIndex2 = mlClamp01(mlLerp(a.engineIm1.modulationIndex2, b.engineIm1.modulationIndex2, alpha));
        out.engineIm2.modulationIndex3 = mlClamp01(mlLerp(a.engineIm2.modulationIndex3, b.engineIm2.modulationIndex3, alpha));
        out.engineIm2.modulationIndex4 = mlClamp01(mlLerp(a.engineIm2.modulationIndex4, b.engineIm2.modulationIndex4, alpha));
        out.engineIm3.modulationIndex5 = mlClamp01(mlLerp(a.engineIm3.modulationIndex5, b.engineIm3.modulationIndex5, alpha));
        out.engineIm3.modulationIndex6 = mlClamp01(mlLerp(a.engineIm3.modulationIndex6, b.engineIm3.modulationIndex6, alpha));

        out.engineIm1.modulationIndexVelo1 = mlClamp01(mlLerp(a.engineIm1.modulationIndexVelo1, b.engineIm1.modulationIndexVelo1, alpha));
        out.engineIm1.modulationIndexVelo2 = mlClamp01(mlLerp(a.engineIm1.modulationIndexVelo2, b.engineIm1.modulationIndexVelo2, alpha));
        out.engineIm2.modulationIndexVelo3 = mlClamp01(mlLerp(a.engineIm2.modulationIndexVelo3, b.engineIm2.modulationIndexVelo3, alpha));
        out.engineIm2.modulationIndexVelo4 = mlClamp01(mlLerp(a.engineIm2.modulationIndexVelo4, b.engineIm2.modulationIndexVelo4, alpha));
        out.engineIm3.modulationIndexVelo5 = mlClamp01(mlLerp(a.engineIm3.modulationIndexVelo5, b.engineIm3.modulationIndexVelo5, alpha));
        out.engineIm3.modulationIndexVelo6 = mlClamp01(mlLerp(a.engineIm3.modulationIndexVelo6, b.engineIm3.modulationIndexVelo6, alpha));

        // --- Mixer (continuous, 0..1) ---
        out.engineMix1.mixOsc1 = mlClamp01(mlLerp(a.engineMix1.mixOsc1, b.engineMix1.mixOsc1, alpha));
        out.engineMix1.panOsc1 = mlClamp01(mlLerp(a.engineMix1.panOsc1, b.engineMix1.panOsc1, alpha));
        out.engineMix1.mixOsc2 = mlClamp01(mlLerp(a.engineMix1.mixOsc2, b.engineMix1.mixOsc2, alpha));
        out.engineMix1.panOsc2 = mlClamp01(mlLerp(a.engineMix1.panOsc2, b.engineMix1.panOsc2, alpha));

        out.engineMix2.mixOsc3 = mlClamp01(mlLerp(a.engineMix2.mixOsc3, b.engineMix2.mixOsc3, alpha));
        out.engineMix2.panOsc3 = mlClamp01(mlLerp(a.engineMix2.panOsc3, b.engineMix2.panOsc3, alpha));
        out.engineMix2.mixOsc4 = mlClamp01(mlLerp(a.engineMix2.mixOsc4, b.engineMix2.mixOsc4, alpha));
        out.engineMix2.panOsc4 = mlClamp01(mlLerp(a.engineMix2.panOsc4, b.engineMix2.panOsc4, alpha));

        out.engineMix3.mixOsc5 = mlClamp01(mlLerp(a.engineMix3.mixOsc5, b.engineMix3.mixOsc5, alpha));
        out.engineMix3.panOsc5 = mlClamp01(mlLerp(a.engineMix3.panOsc5, b.engineMix3.panOsc5, alpha));
        out.engineMix3.mixOsc6 = mlClamp01(mlLerp(a.engineMix3.mixOsc6, b.engineMix3.mixOsc6, alpha));
        out.engineMix3.panOsc6 = mlClamp01(mlLerp(a.engineMix3.panOsc6, b.engineMix3.panOsc6, alpha));

        // --- Oscillators ---
        auto morphOsc = [&](const OscillatorParams& oa, const OscillatorParams& ob, OscillatorParams& o) {
            o.shape = mlPickDiscreteClamped(oa.shape, ob.shape, 0, ML_OSC_SHAPE_MAX);
            o.frequencyType = mlPickDiscreteClamped(oa.frequencyType, ob.frequencyType, 0, 2);
            // Keep harmonic ratios stable: pick multiplier rather than lerp.
            o.frequencyMul = clamp(mlPickDiscrete(oa.frequencyMul, ob.frequencyMul), 0.0f, 16.0f);
            o.detune = clamp(mlLerp(oa.detune, ob.detune, alpha), -1.0f, 1.0f);
        };
        morphOsc(a.osc1, b.osc1, out.osc1);
        morphOsc(a.osc2, b.osc2, out.osc2);
        morphOsc(a.osc3, b.osc3, out.osc3);
        morphOsc(a.osc4, b.osc4, out.osc4);
        morphOsc(a.osc5, b.osc5, out.osc5);
        morphOsc(a.osc6, b.osc6, out.osc6);

        // --- Envelopes (0..1) ---
        auto morphEnvA = [&](const EnvelopeParamsA& ea, const EnvelopeParamsA& eb, EnvelopeParamsA& e) {
            e.attackTime = mlClamp01(mlLerp(ea.attackTime, eb.attackTime, alpha));
            e.attackLevel = mlClamp01(mlLerp(ea.attackLevel, eb.attackLevel, alpha));
            e.decayTime = mlClamp01(mlLerp(ea.decayTime, eb.decayTime, alpha));
            e.decayLevel = mlClamp01(mlLerp(ea.decayLevel, eb.decayLevel, alpha));
        };
        auto morphEnvB = [&](const EnvelopeParamsB& ea, const EnvelopeParamsB& eb, EnvelopeParamsB& e) {
            e.sustainTime = mlClamp01(mlLerp(ea.sustainTime, eb.sustainTime, alpha));
            e.sustainLevel = mlClamp01(mlLerp(ea.sustainLevel, eb.sustainLevel, alpha));
            e.releaseTime = mlClamp01(mlLerp(ea.releaseTime, eb.releaseTime, alpha));
            e.releaseLevel = mlClamp01(mlLerp(ea.releaseLevel, eb.releaseLevel, alpha));
        };
        morphEnvA(a.env1a, b.env1a, out.env1a);
        morphEnvB(a.env1b, b.env1b, out.env1b);
        morphEnvA(a.env2a, b.env2a, out.env2a);
        morphEnvB(a.env2b, b.env2b, out.env2b);
        morphEnvA(a.env3a, b.env3a, out.env3a);
        morphEnvB(a.env3b, b.env3b, out.env3b);
        morphEnvA(a.env4a, b.env4a, out.env4a);
        morphEnvB(a.env4b, b.env4b, out.env4b);
        morphEnvA(a.env5a, b.env5a, out.env5a);
        morphEnvB(a.env5b, b.env5b, out.env5b);
        morphEnvA(a.env6a, b.env6a, out.env6a);
        morphEnvB(a.env6b, b.env6b, out.env6b);

        // --- Matrix (source/dest discrete, mul continuous) ---
        auto morphMatrixRow = [&](const MatrixRowParams& ra, const MatrixRowParams& rb, MatrixRowParams& r) {
            r.source = mlPickDiscreteClamped(ra.source, rb.source, 0, (int)MATRIX_SOURCE_MAX - 1);
            r.dest1 = mlPickDiscreteClamped(ra.dest1, rb.dest1, 0, (int)DESTINATION_MAX - 1);
            r.dest2 = mlPickDiscreteClamped(ra.dest2, rb.dest2, 0, (int)DESTINATION_MAX - 1);
            r.mul = mlClampSigned1(mlLerp(ra.mul, rb.mul, alpha));
        };
        morphMatrixRow(a.matrixRowState1, b.matrixRowState1, out.matrixRowState1);
        morphMatrixRow(a.matrixRowState2, b.matrixRowState2, out.matrixRowState2);
        morphMatrixRow(a.matrixRowState3, b.matrixRowState3, out.matrixRowState3);
        morphMatrixRow(a.matrixRowState4, b.matrixRowState4, out.matrixRowState4);
        morphMatrixRow(a.matrixRowState5, b.matrixRowState5, out.matrixRowState5);
        morphMatrixRow(a.matrixRowState6, b.matrixRowState6, out.matrixRowState6);
        morphMatrixRow(a.matrixRowState7, b.matrixRowState7, out.matrixRowState7);
        morphMatrixRow(a.matrixRowState8, b.matrixRowState8, out.matrixRowState8);
        morphMatrixRow(a.matrixRowState9, b.matrixRowState9, out.matrixRowState9);
        morphMatrixRow(a.matrixRowState10, b.matrixRowState10, out.matrixRowState10);
        morphMatrixRow(a.matrixRowState11, b.matrixRowState11, out.matrixRowState11);
        morphMatrixRow(a.matrixRowState12, b.matrixRowState12, out.matrixRowState12);

        // --- LFO Osc (shape discrete, others continuous) ---
        auto morphLfo = [&](const LfoParams& la, const LfoParams& lb, LfoParams& l) {
            l.shape = mlPickDiscreteClamped(la.shape, lb.shape, 0, ML_LFO_TYPE_MAX);
            l.freq = clamp(mlLerp(la.freq, lb.freq, alpha), 0.0f, LFO_FREQ_MAX);
            l.bias = mlClampSigned1(mlLerp(la.bias, lb.bias, alpha));
            l.keybRamp = mlClampSigned1(mlLerp(la.keybRamp, lb.keybRamp, alpha));
        };
        morphLfo(a.lfoOsc1, b.lfoOsc1, out.lfoOsc1);
        morphLfo(a.lfoOsc2, b.lfoOsc2, out.lfoOsc2);
        morphLfo(a.lfoOsc3, b.lfoOsc3, out.lfoOsc3);

        // --- LFO envelopes and step seq (continuous, conservative clamp) ---
        out.lfoEnv1.attack = mlClamp01(mlLerp(a.lfoEnv1.attack, b.lfoEnv1.attack, alpha));
        out.lfoEnv1.decay = mlClamp01(mlLerp(a.lfoEnv1.decay, b.lfoEnv1.decay, alpha));
        out.lfoEnv1.sustain = mlClamp01(mlLerp(a.lfoEnv1.sustain, b.lfoEnv1.sustain, alpha));
        out.lfoEnv1.release = mlClamp01(mlLerp(a.lfoEnv1.release, b.lfoEnv1.release, alpha));

        out.lfoEnv2.silence = mlClamp01(mlLerp(a.lfoEnv2.silence, b.lfoEnv2.silence, alpha));
        out.lfoEnv2.attack = mlClamp01(mlLerp(a.lfoEnv2.attack, b.lfoEnv2.attack, alpha));
        out.lfoEnv2.decay = mlClamp01(mlLerp(a.lfoEnv2.decay, b.lfoEnv2.decay, alpha));
        out.lfoEnv2.loop = mlPickDiscreteClamped(a.lfoEnv2.loop, b.lfoEnv2.loop, 0, 2);

        out.lfoSeq1.bpm = mlClamp01(mlLerp(a.lfoSeq1.bpm, b.lfoSeq1.bpm, alpha));
        out.lfoSeq1.gate = mlClamp01(mlLerp(a.lfoSeq1.gate, b.lfoSeq1.gate, alpha));
        out.lfoSeq2.bpm = mlClamp01(mlLerp(a.lfoSeq2.bpm, b.lfoSeq2.bpm, alpha));
        out.lfoSeq2.gate = mlClamp01(mlLerp(a.lfoSeq2.gate, b.lfoSeq2.gate, alpha));

        for (int s = 0; s < 16; s++) {
            out.lfoSteps1.steps[s] = (random::uniform() < 0.5f) ? a.lfoSteps1.steps[s] : b.lfoSteps1.steps[s];
            out.lfoSteps2.steps[s] = (random::uniform() < 0.5f) ? a.lfoSteps2.steps[s] : b.lfoSteps2.steps[s];
        }

        // --- FX ---
        // Pick discrete type (both parents come from valid bank patches).
        // Filter types in PreenFM2 are 0..48 (see src/pfm/SynthState.h FILTER_TYPE).
        // Clamp to that range so MORPH always produces a valid filter.
        out.effect.type = mlPickDiscreteClamped(a.effect.type, b.effect.type, 0, 48);
        out.effect.param1 = mlClamp01(mlLerp(a.effect.param1, b.effect.param1, alpha));
        out.effect.param2 = mlClamp01(mlLerp(a.effect.param2, b.effect.param2, alpha));
        out.effect.param3 = mlClamp01(mlLerp(a.effect.param3, b.effect.param3, alpha));

        // Name
        mlSetPresetName(out, "MORPH");
    }

    void goMorphRandomPresets() {
        if (!bankLoader.isLoaded()) return;
        int count = bankLoader.getPatchCount();
        if (count <= 1) return;

        int idxA = random::u32() % count;
        int idxB;
        do {
            idxB = random::u32() % count;
        } while (idxB == idxA && count > 1);

        OneSynthParams a;
        OneSynthParams b;
        OneSynthParams out;
        if (!bankLoader.getPatch(idxA, a)) return;
        if (!bankLoader.getPatch(idxB, b)) return;

        // Musical alpha: avoid extremes.
        float alpha = 0.35f + 0.30f * random::uniform();
        mlMorphParams(a, b, alpha, out);

        // Apply patch safely.
        engine.noteOff();
        engine.loadPatch(out);
        params[ALGO_PARAM].setValue((float)engine.getBaseAlgo());

        currentParams = out;
        currentParamsValid = true;

        // Update display name
        patchNameBase = "MORPH " + std::to_string(idxA + 1) + "+" + std::to_string(idxB + 1);
        if (engine.isMissingUserWave()) {
            std::string msg = mlFormatMissingUserWaves(engine.getMissingUserWavesMask());
            if (!msg.empty()) {
                patchNameBase = msg;
            } else {
                patchNameBase = "MISSING USR";
            }
        }

        updatePatchNameDisplay();
    }
#endif

#ifndef METAMODULE
    bool renameCurrentPreset(const std::string& newName) {
        if (!bankLoader.isLoaded()) return false;
        if (bankPath.empty()) return false;

        OneSynthParams patchParams;
        if (!bankLoader.getPatch(currentPatch, patchParams)) return false;

        // Write the new name into the patch (12 chars max, null-terminated at [12]).
        mlSetPresetNameFromDisplay(patchParams, newName);

        // Update in-memory bank and save to disk.
        if (!bankLoader.setPatch(currentPatch, patchParams)) return false;
        if (!bankLoader.saveBankInPlace()) return false;

        // Also update the live engine params copy.
        if (currentParamsValid) {
            mlSetPresetNameFromDisplay(currentParams, newName);
        }

        // Refresh display name.
        std::string trimmed = newName;
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        if (trimmed.empty()) {
            patchNameBase = "Patch " + std::to_string(currentPatch + 1);
        } else {
            patchNameBase = trimmed;
        }
        updatePatchNameDisplay();
        return true;
    }

    TrimState getCurrentTrimState() {
        TrimState st;
        st.evo = clamp(params[EVO_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.im = clamp(params[IM_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.drift = clamp(params[DRIFT_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.tear = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        st.tearFold = params[TEAR_FOLD_SWITCH_PARAM].getValue() > 0.5f ? 1.0f : 0.0f;
        return st;
    }

    bool buildCurrentPresetSnapshot(OneSynthParams& out) {
        if (!bankLoader.isLoaded()) return false;

        OneSynthParams toSave;
        if (!currentParamsValid) return false;
        toSave = currentParams;

        if (!inputs[CV1_INPUT].isConnected()) {
            int algoFromKnob = (int)(params[ALGO_PARAM].getValue() + 0.5f);
            algoFromKnob = clamp(algoFromKnob, 0, 27);
            toSave.engine1.algo = (float)algoFromKnob;
        }

        if (!inputs[CV2_INPUT].isConnected()) {
            mlBakeImScanTrimIntoPatch(toSave, params[IM_TRIM_PARAM].getValue());
        }
        if (!inputs[EVO_INPUT].isConnected()) {
            mlBakeEvoTrimIntoPatch(toSave, params[EVO_TRIM_PARAM].getValue());
        }
        if (!inputs[DRIFT_INPUT].isConnected()) {
            mlBakeDriftTrimIntoPatch(toSave, params[DRIFT_TRIM_PARAM].getValue());
        }

        float tearExport = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        bool foldOn = params[TEAR_FOLD_SWITCH_PARAM].getValue() > 0.5f;
        bool canBakeTear = !inputs[TEAR_INPUT].isConnected();
        int existingFxType = (int)(toSave.effect.type + 0.5f);
        bool keepExistingTear = (existingFxType == ML_FILTER_TEAR_ID);
        if (canBakeTear && (tearExport > 0.0001f || keepExistingTear)) {
            toSave.effect.type = exportTearLegacyFallback ? 48.0f : (float)ML_FILTER_TEAR_ID;
            toSave.effect.param1 = tearExport;
            toSave.effect.param2 = foldOn ? 1.0f : 0.0f;
            if (toSave.effect.param3 < 0.0f || toSave.effect.param3 > 2.0f) {
                toSave.effect.param3 = 1.0f;
            }
        }

        mlSetPresetNameFromDisplay(toSave, patchNameBase);
        out = toSave;
        return true;
    }

    bool copyCurrentPresetToClipboard() {
        OneSynthParams snapshot;
        if (!buildCurrentPresetSnapshot(snapshot)) return false;
        copiedPreset = snapshot;
        copiedTrimState = getCurrentTrimState();
        hasCopiedPreset = true;
        return true;
    }

    bool pasteClipboardPresetToCurrentSlot() {
        if (!hasCopiedPreset) return false;
        if (!bankLoader.isLoaded()) return false;
        if (bankPath.empty()) return false;

        if (!bankLoader.setPatch(currentPatch, copiedPreset)) return false;
        if (!bankLoader.saveBankInPlace()) return false;

        trimBySlot[currentPatch] = copiedTrimState;
        trimsSaveForBank();

        loadCurrentPatch();
        return true;
    }
#endif

        bool saveCurrentToBankSlot() {
        if (!bankLoader.isLoaded()) return false;
        if (bankPath.empty()) return false;

        OneSynthParams toSave;
    #ifndef METAMODULE
        if (!buildCurrentPresetSnapshot(toSave)) return false;
    #else
        if (!bankLoader.getPatch(currentPatch, toSave)) return false;
    #endif

#ifdef METAMODULE
        // --- Bake in algorithm override if applicable ---
        // If CV1 is connected, algorithm is being modulated externally and we can't
        // meaningfully bake a "current" value; leave the patch algo untouched.
        if (!inputs[CV1_INPUT].isConnected()) {
            int algoFromKnob = (int)(params[ALGO_PARAM].getValue() + 0.5f);
            algoFromKnob = clamp(algoFromKnob, 0, 27);
            if (algoPickupActive) {
                toSave.engine1.algo = (float)algoFromKnob;
            }
        }

        // --- Bake trim-driven modulation into patch values ---
        if (!inputs[CV2_INPUT].isConnected() && imPickup.active) {
            mlBakeImScanTrimIntoPatch(toSave, params[IM_TRIM_PARAM].getValue());
        }
        if (!inputs[EVO_INPUT].isConnected() && evoPickup.active) {
            mlBakeEvoTrimIntoPatch(toSave, params[EVO_TRIM_PARAM].getValue());
        }
        if (!inputs[DRIFT_INPUT].isConnected() && driftPickup.active) {
            mlBakeDriftTrimIntoPatch(toSave, params[DRIFT_TRIM_PARAM].getValue());
        }

        // --- Optional TEAR export for PreenFM2 firmware interop ---
        // Encodes TEAR into effect.type/param1/param2 when TEAR is active.
        // This keeps old banks unchanged unless TEAR is explicitly used.
        float tearExport = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        bool foldOn = params[TEAR_FOLD_SWITCH_PARAM].getValue() > 0.5f;
        bool canBakeTear = !inputs[TEAR_INPUT].isConnected() && tearPickup.active;
        int existingFxType = (int)(toSave.effect.type + 0.5f);
        bool keepExistingTear = (existingFxType == ML_FILTER_TEAR_ID);
        if (canBakeTear && (tearExport > 0.0001f || keepExistingTear)) {
            toSave.effect.type = exportTearLegacyFallback ? 48.0f : (float)ML_FILTER_TEAR_ID;
            toSave.effect.param1 = tearExport;
            toSave.effect.param2 = foldOn ? 1.0f : 0.0f;
            if (toSave.effect.param3 < 0.0f || toSave.effect.param3 > 2.0f) {
                toSave.effect.param3 = 1.0f;
            }
        }
#endif

        if (!bankLoader.setPatch(currentPatch, toSave)) return false;
        if (!bankLoader.saveBankInPlace()) return false;

    #ifdef METAMODULE
        snprintf(mmPresetName, sizeof(mmPresetName), "SAVED %02d", currentPatch + 1);
        MetaModule::Patch::mark_patch_modified();
    #else
        patchNameBase = "SAVED " + std::to_string(currentPatch + 1);
        updatePatchNameDisplay();
    #endif
        return true;
        }

    void process(const ProcessArgs& args) override {
        // Keep engine sample rate synced without relying on APP.
        // This also handles MetaModule (no APP) and any host sample rate changes.
        if (engineReady) {
            if (args.sampleRate > 0.0f && args.sampleRate != lastSampleRate) {
                engine.setSampleRate(args.sampleRate);
                lastSampleRate = args.sampleRate;
            }
        }

        if (!engineReady) {
            outputs[LEFT_OUTPUT].setVoltage(0.f);
            outputs[RIGHT_OUTPUT].setVoltage(0.f);
            sidechainEnv = 0.0f;
            return;
        }

        // --- Load bank button (rising edge) ---
        {
            bool loadBtn = params[LOAD_PARAM].getValue() > 0.5f;
            if (loadBtn && !lastLoadBtn) {
#ifdef METAMODULE
                loadRequested = true;
#else
                loadRequested = true;
#endif
            }
            lastLoadBtn = loadBtn;
        }

#ifdef METAMODULE
        // --- Handle pending bank load from file browser callback ---
        if (pendingBankLoad) {
            pendingBankLoad = false;
            loadCurrentPatch();
            int count = bankLoader.isLoaded() ? bankLoader.getPatchCount() : 128;
            if (count > 1) {
                params[PATCH_PARAM].setValue((float)currentPatch / (float)(count - 1));
            }
        }
#endif

        // --- Random patch trigger (rising edge) ---
        float nextTrig = inputs[NEXT_TRIG_INPUT].getVoltage();
#ifdef METAMODULE
        if (mmRandomCooldown > 0) --mmRandomCooldown;
#endif
        if (nextTrig >= 1.0f && lastNextTrig < 1.0f) {
#ifdef METAMODULE
            // Only allow one patch switch per cooldown window to prevent
            // a continuous envelope output from spamming loadPatch().
            if (mmRandomCooldown == 0) {
                goRandomPatch();
                mmRandomCooldown = kRandomCooldownSamples;
            }
#else
            goRandomPatch();
#endif
            // Sync patch knob to the random selection
            int count = bankLoader.isLoaded() ? bankLoader.getPatchCount() : 128;
            if (count > 1) {
                params[PATCH_PARAM].setValue((float)currentPatch / (float)(count - 1));
            }
        }
        lastNextTrig = nextTrig;

#ifndef METAMODULE
        // --- MORPH button (rising edge) ---
        {
            bool morphBtn = params[MORPH_PARAM].getValue() > 0.5f;
            if (morphBtn && !lastMorphBtn) {
                goMorphRandomPresets();
            }
            lastMorphBtn = morphBtn;
        }

        // --- Preset Prev/Next buttons (rising edge) ---
        if (presetButtonCooldownRemaining > 0) {
            presetButtonCooldownRemaining--;
        }
        if (bankLoader.isLoaded()) {
            bool prevPressed = presetPrevButtonTrigger.process(params[PRESET_PREV_PARAM].getValue());
            bool nextPressed = presetNextButtonTrigger.process(params[PRESET_NEXT_PARAM].getValue());
            uint32_t presetCooldownSamples = std::max<uint32_t>(1u, (uint32_t)std::lround(args.sampleRate * kPresetButtonCooldownSeconds));

            if (prevPressed && presetButtonCooldownRemaining == 0) {
                trimsCaptureCurrentToSlot(currentPatch);
                trimsSaveForBank();
                currentPatch = bankLoader.prevPatch(currentPatch);
                loadCurrentPatch();
                int count = bankLoader.getPatchCount();
                if (count > 1) {
                    params[PATCH_PARAM].setValue((float)currentPatch / (float)(count - 1));
                }
                presetButtonCooldownRemaining = presetCooldownSamples;
            }
            if (nextPressed && presetButtonCooldownRemaining == 0) {
                trimsCaptureCurrentToSlot(currentPatch);
                trimsSaveForBank();
                currentPatch = bankLoader.nextPatch(currentPatch);
                loadCurrentPatch();
                int count = bankLoader.getPatchCount();
                if (count > 1) {
                    params[PATCH_PARAM].setValue((float)currentPatch / (float)(count - 1));
                }
                presetButtonCooldownRemaining = presetCooldownSamples;
            }
        }
#endif

        // --- Patch selector knob ---
        if (bankLoader.isLoaded()) {
            float patchKnob = params[PATCH_PARAM].getValue();
            int count = bankLoader.getPatchCount();
            int patchIdx = (int)(patchKnob * (count - 1) + 0.5f);
            if (patchIdx < 0) patchIdx = 0;
            if (patchIdx >= count) patchIdx = count - 1;
            if (patchIdx != currentPatch) {
                trimsCaptureCurrentToSlot(currentPatch);
#ifndef METAMODULE
                // File I/O in the audio thread — suppress on MetaModule.
                // Persistence happens via dataToJson() on MetaModule save.
                trimsSaveForBank();
#endif
                currentPatch = patchIdx;
                loadCurrentPatch();
            }
        }

        // --- Algorithm knob ---
        {
            int algoFromKnob = (int)(params[ALGO_PARAM].getValue() + 0.5f);
            if (algoFromKnob < 0) algoFromKnob = 0;
            if (algoFromKnob > 27) algoFromKnob = 27;

#ifdef METAMODULE
            if (!algoPickupActive && algoFromKnob != algoBaselineQ) {
                algoPickupActive = true;
            }
            engine.setManualAlgo(algoPickupActive ? algoFromKnob : -1);
#else
            engine.setManualAlgo(algoFromKnob);
#endif
        }

        // --- PANIC button - force reset voice state ---
        bool panicBtn = params[PANIC_PARAM].getValue() > 0.5f;
        if (panicBtn && !lastPanicBtn) {
            // Kill any stuck notes and reset voice
            engine.panic();
            lastGate = false;  // Reset gate tracking
            lastPitch = 0.0f;  // Reset pitch tracking
        }
        lastPanicBtn = panicBtn;

        // --- Gate + Pitch tracking ---
        bool gate = inputs[GATE_INPUT].getVoltage() >= 1.0f;
        float voct = inputs[PITCH_INPUT].getVoltage();
        // Apply octave offset trim
        voct += (float)(int)(params[OCTAVE_TRIM_PARAM].getValue() + (params[OCTAVE_TRIM_PARAM].getValue() >= 0.f ? 0.5f : -0.5f));

        // Detect significant pitch change for retrigger (more than 1 semitone)
        float pitchChange = std::abs(voct - lastPitch);
        bool pitchChanged = pitchChange > 0.08f; // ~1 semitone threshold

        if (gate && !lastGate) {
            // Gate just went high — attack starts at target pitch.
            float freq = 261.6256f * std::exp2(voct); // C4 = 0V
            engine.noteOn(freq, 127);
            lastPitch = voct;
        } else if (gate && lastGate) {
            // Gate held: update target pitch; engine handles musical glide in log-frequency.
            float freq = 261.6256f * std::exp2(voct);
            engine.updatePitch(freq);
            if (pitchChanged) {
                lastPitch = voct;  // Update pitch tracking
            }
        } else if (!gate && lastGate) {
            // Gate just went low - note off
            engine.noteOff();
        }
        lastGate = gate;
        lights[GATE_LIGHT].setBrightness(gate ? 1.0f : 0.0f);

        // --- CV modulation ---
        // CV1 controls algorithm selection, CV2 scans IM1-IM5
        bool cv1Conn = inputs[CV1_INPUT].isConnected();
        bool cv2Conn = inputs[CV2_INPUT].isConnected();
        engine.setCv1Connected(cv1Conn);

        // IM SCAN trim: when unplugged, trim both selects scan position and
        // sets amount so center = neutral (no change), edges = extreme.
        float cv2Amount = 1.0f;
        float cv2;
        if (cv2Conn) {
            cv2 = inputs[CV2_INPUT].getVoltage() / 5.0f;
            engine.setCv2Connected(true);
            engine.setCvin2Amount(1.0f);
        } else {
#ifdef METAMODULE
            const float tNow = clamp(params[IM_TRIM_PARAM].getValue(), 0.0f, 1.0f);
            const bool imActive = mmPickup01Active(imPickup, tNow, 0.02f);
            if (imActive) {
                float t = tNow;                                   // 0..1
                cv2 = (t * 2.0f) - 1.0f;                           // -1..+1
                cv2Amount = clamp(fabsf(t - 0.5f) * 2.0f, 0.0f, 1.0f); // 0 at center
                engine.setCv2Connected(true);
                engine.setCvin2Amount(cv2Amount);
            } else {
                // Not touched since preset load: do not modify the preset.
                cv2 = 0.0f;
                engine.setCv2Connected(false);
                engine.setCvin2Amount(0.0f);
            }
#else
            float t = params[IM_TRIM_PARAM].getValue();          // 0..1
            cv2 = (t * 2.0f) - 1.0f;                             // -1..+1
            cv2Amount = clamp(fabsf(t - 0.5f) * 2.0f, 0.0f, 1.0f); // 0 at center
            engine.setCv2Connected(true); // always on; amount controls neutrality when unplugged
            engine.setCvin2Amount(cv2Amount);
#endif
        }

        float cv1 = cv1Conn ? (inputs[CV1_INPUT].getVoltage() / 5.0f) : 0.0f;
        engine.setCvin1(clamp(cv1, -1.0f, 1.0f));
        engine.setCvin2(clamp(cv2, -1.0f, 1.0f));

        // EVO controls shape + envelope evolution (0-10V unipolar)
        bool evoConn = inputs[EVO_INPUT].isConnected();
        float evo;
        if (evoConn) {
            evo = inputs[EVO_INPUT].getVoltage() / 10.0f;
            engine.setEvoConnected(true);
            engine.setCvinEvo(clamp(evo, 0.0f, 1.0f));
        } else {
#ifdef METAMODULE
            const float evoNow = clamp(params[EVO_TRIM_PARAM].getValue(), 0.0f, 1.0f);
            const bool evoActive = mmPickup01Active(evoPickup, evoNow, 0.02f);
            if (evoActive) {
                evo = evoNow;
                engine.setEvoConnected(evo > 0.0001f);
                engine.setCvinEvo(clamp(evo, 0.0f, 1.0f));
            } else {
                // Not touched since preset load: do not modify the preset.
                engine.setEvoConnected(false);
                engine.setCvinEvo(0.0f);
            }
#else
            evo = params[EVO_TRIM_PARAM].getValue(); // 0..1
            bool evoActive = evo > 0.0001f;
            engine.setEvoConnected(evoActive);
            engine.setCvinEvo(clamp(evo, 0.0f, 1.0f));
#endif
        }

        // DRIFT controls operator detune spread (-5V..+5V bipolar)
        bool driftConn = inputs[DRIFT_INPUT].isConnected();
        float drift;
        if (driftConn) {
            drift = inputs[DRIFT_INPUT].getVoltage() / 5.0f;
            engine.setDriftConnected(true);
            engine.setCvinDrift(clamp(drift, -1.0f, 1.0f));
        } else {
#ifdef METAMODULE
            const float driftNow01 = clamp(params[DRIFT_TRIM_PARAM].getValue(), 0.0f, 1.0f);
            const bool driftActive = mmPickup01Active(driftPickup, driftNow01, 0.02f);
            if (driftActive) {
                drift = (driftNow01 * 2.0f) - 1.0f;
                engine.setDriftConnected(true);
                engine.setCvinDrift(clamp(drift, -1.0f, 1.0f));
            } else {
                // Not touched since preset load: do not modify the preset.
                engine.setDriftConnected(false);
                engine.setCvinDrift(0.0f);
            }
#else
            drift = (params[DRIFT_TRIM_PARAM].getValue() * 2.0f) - 1.0f;
            engine.setDriftConnected(true);
            engine.setCvinDrift(clamp(drift, -1.0f, 1.0f));
#endif
        }

        // --- Audio ---
        float outL, outR;
        engine.process(outL, outR);

        // TEAR (lightweight): post-engine fold/noise/comb coloration.
        // Hard bypass at 0 keeps CPU overhead negligible when disabled.
        bool tearCvConn = inputs[TEAR_INPUT].isConnected();
        float tearBase;
    #ifdef METAMODULE
        const float tearNow = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
        const bool tearActive = mmPickup01Active(tearPickup, tearNow, 0.02f);
        tearBase = tearActive ? tearNow : 0.0f;
    #else
        tearBase = clamp(params[TEAR_TRIM_PARAM].getValue(), 0.0f, 1.0f);
    #endif
        float tearTarget = tearCvConn
            ? clamp(tearBase + inputs[TEAR_INPUT].getVoltage() * 0.2f, 0.0f, 1.0f)
            : tearBase;
        if (tearTarget <= 0.0001f) {
            if (tearWet > 0.0f || tearDelayWrite != 0) {
                resetTearState();
            }
        } else {
            bool tearFoldEnabled = params[TEAR_FOLD_SWITCH_PARAM].getValue() > 0.5f;

            // Light smoothing only while active.
            tearWet += (tearTarget - tearWet) * 0.18f;
            if (tearWet < 0.0001f) {
                tearWet = 0.0f;
            }

            int tearDelayLen = 8 + (int)(tearWet * (float)(kTearDelaySize - 9));
            if (tearDelayLen < 1) tearDelayLen = 1;
            if (tearDelayLen >= kTearDelaySize) tearDelayLen = kTearDelaySize - 1;

            float drive = 1.0f + 5.5f * tearWet;
            float foldedL = tearFoldEnabled ? std::sin(outL * drive) : outL;
            float foldedR = tearFoldEnabled ? std::sin(outR * drive) : outR;

            float nL = mlU01FromU32(mlXorshift32(tearNoiseState)) * 2.0f - 1.0f;
            float nR = mlU01FromU32(mlXorshift32(tearNoiseState)) * 2.0f - 1.0f;
            float noiseAmt = 0.004f + 0.028f * tearWet;

            float preL = foldedL + nL * noiseAmt;
            float preR = foldedR + nR * noiseAmt;

            int read = (tearDelayWrite - tearDelayLen) & (kTearDelaySize - 1);
            float dL = tearDelayL[read];
            float dR = tearDelayR[read];
            float fb = 0.08f + 0.55f * tearWet;
            float combMix = 0.12f + 0.42f * tearWet;

            tearDelayL[tearDelayWrite] = preL + dL * fb;
            tearDelayR[tearDelayWrite] = preR + dR * fb;
            tearDelayWrite = (tearDelayWrite + 1) & (kTearDelaySize - 1);

            float wetL = std::tanh((preL + dL * combMix) * (1.0f + 1.2f * tearWet));
            float wetR = std::tanh((preR + dR * combMix) * (1.0f + 1.2f * tearWet));
            outL = outL + (wetL - outL) * tearWet;
            outR = outR + (wetR - outR) * tearWet;
        }

        float sidechainInput = 0.0f;
        bool sidechainConnected = inputs[SIDECHAIN_INPUT].isConnected();
        if (sidechainConnected) {
            sidechainInput = clamp(inputs[SIDECHAIN_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f);
        }
        float sidechainAmount = clamp(params[SC_AMOUNT_PARAM].getValue(), 0.0f, 1.0f);
        float attackMs = mlExpMap01(params[SC_ATTACK_PARAM].getValue(), 0.5f, 50.0f);
        float releaseMs = mlExpMap01(params[SC_RELEASE_PARAM].getValue(), 20.0f, 500.0f);
        float attackCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, attackMs * 0.001f * args.sampleRate));
        float releaseCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, releaseMs * 0.001f * args.sampleRate));
        if (sidechainInput > sidechainEnv) {
            sidechainEnv += attackCoeff * (sidechainInput - sidechainEnv);
        } else {
            sidechainEnv += releaseCoeff * (sidechainInput - sidechainEnv);
        }
        sidechainEnv = clamp(sidechainEnv, 0.0f, 1.0f);

        float vol = params[VOLUME_PARAM].getValue(); // 0..2
        float sidechainShaped = sidechainEnv * sidechainEnv;
        float sidechainMinGain = 1.0f - sidechainAmount;
        float sidechainGain = 1.0f - sidechainShaped * sidechainAmount;
        if (sidechainGain < sidechainMinGain) {
            sidechainGain = sidechainMinGain;
        }

        float leftVoltage = outL * vol * sidechainGain;
        float rightVoltage = outR * vol * sidechainGain;
        if (sidechainConnected && sidechainAmount > 0.0f) {
            leftVoltage = clamp(leftVoltage, -10.0f, 10.0f);
            rightVoltage = clamp(rightVoltage, -10.0f, 10.0f);
        }

        outputs[LEFT_OUTPUT].setVoltage(leftVoltage);
        outputs[RIGHT_OUTPUT].setVoltage(rightVoltage);

    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();

        // Persist current sidecar trims for the active slot.
        if (bankLoader.isLoaded()) {
            // Ensure slot key exists before capture (safe here — save/UI thread).
            trimBySlot.emplace(currentPatch, TrimState{});
            trimsCaptureCurrentToSlot(currentPatch);
            trimsSaveForBank();
        }

        json_object_set_new(rootJ, "bankPath", json_string(bankPath.c_str()));
        json_object_set_new(rootJ, "currentPatch", json_integer(currentPatch));
        json_object_set_new(rootJ, "tearLegacyExport", json_boolean(exportTearLegacyFallback));

#ifdef METAMODULE
        // Persist the MorphWorx plugin version alongside module state so
        // MetaModule firmware can reject patches from mismatched builds.
        json_object_set_new(rootJ, "morphworx_version", json_string(MORPHWORX_VERSION_STRING));
#endif

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* bankPathJ = json_object_get(rootJ, "bankPath");
        if (bankPathJ) {
            bankPath = json_string_value(bankPathJ);
        }
        json_t* patchJ = json_object_get(rootJ, "currentPatch");
        if (patchJ) {
            currentPatch = json_integer_value(patchJ);
        }
        json_t* tearLegacyJ = json_object_get(rootJ, "tearLegacyExport");
        if (tearLegacyJ) {
            exportTearLegacyFallback = json_boolean_value(tearLegacyJ) != 0;
        }

#ifdef METAMODULE
        // On MetaModule, enforce a strict version handshake: if the saved
        // MorphWorx version does not match the running firmware, treat the
        // patch as incompatible and leave state at safe defaults.
        json_t* verJ = json_object_get(rootJ, "morphworx_version");
        if (verJ && json_is_string(verJ)) {
            const char* saved = json_string_value(verJ);
            if (!saved || std::string(saved) != std::string(MORPHWORX_VERSION_STRING)) {
                return;
            }
        }
#endif
        // Bank will be loaded in onAdd()
    }
};

// ============================================================================
// NanoVG Label Widget (same style as SlideWyrm)
// ============================================================================
#ifndef METAMODULE
struct MLPanelLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool isTitle;

    MLPanelLabel(Vec pos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
        box.pos = pos;
        box.size = Vec(120, fontSize + 4);
        this->text = text;
        this->fontSize = fontSize;
        this->color = color;
        this->isTitle = isTitle;
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

static MLPanelLabel* mlCreateLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
    Vec pxPos = mm2px(mmPos);
    return new MLPanelLabel(pxPos, text, fontSize, color, isTitle);
}

// ============================================================================
// Patch Name Display Widget
// ============================================================================
struct MinimalithDisplay : TransparentWidget {
    Minimalith* module;

    void draw(const DrawArgs& args) override {
        // Text-only overlay. The screen art is baked into the faceplate PNG.
        std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
        if (!font) return;

        nvgFontFaceId(args.vg, font->handle);

        // Main patch name (center)
        nvgFontSize(args.vg, 16.25f);
        nvgFillColor(args.vg, nvgRGB(0x00, 0xff, 0x00));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        const char* displayText = "No Bank Loaded";
        if (module) {
            displayText = module->patchName.c_str();
        }
        nvgText(args.vg, box.size.x / 2.f, box.size.y * 0.38f + 5.0f - 3.0f, displayText, NULL);

        // FX label (top-right, small, orange/brown)
        if (module && !module->fxLabel.empty()) {
            nvgFontSize(args.vg, 10.75f);
            nvgFillColor(args.vg, nvgRGB(0xd4, 0x95, 0x45));
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgText(args.vg, box.size.x - 4.0f, 2.0f, module->fxLabel.c_str(), NULL);
        }

        // Patch number
        char numBuf[16];
        if (module && module->bankLoader.isLoaded()) {
            snprintf(numBuf, sizeof(numBuf), "%d / %d", module->currentPatch + 1, module->bankLoader.getPatchCount());
        } else {
            snprintf(numBuf, sizeof(numBuf), "- / -");
        }
        nvgFontSize(args.vg, 12.5f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0x99));
        // Ensure the number stays centered under the patch name.
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2.f, box.size.y * 0.78f - 3.0f, numBuf, NULL);
    }
};
#endif

// ============================================================================
// Widget
// ============================================================================
struct MinimalithWidget : ModuleWidget {
    MinimalithWidget(Minimalith* module) {
        setModule(module);
#ifdef METAMODULE
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Minimalith.png")));
#else
        // VCV Rack: no SVG; hardcode 16HP and use PNG directly.
        box.size = Vec(RACK_GRID_WIDTH * 16, RACK_GRID_HEIGHT);
        {
            auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Minimalith.png"));
            panelBg->box.pos = Vec(0, 0);
            panelBg->box.size = box.size;
            addChild(panelBg);
        }
#endif

#ifndef METAMODULE
        struct MinimalithPort : MVXPort {
            MinimalithPort() {
                imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
                imageHandle = -1;
            }
        };
#else
        using MinimalithPort = MVXPort;
#endif

#ifndef METAMODULE
        NVGcolor neonGreen = nvgRGB(0xa7, 0xff, 0xc4);
        NVGcolor dimGreen  = nvgRGB(0x7d, 0xbf, 0x93);
#endif

        // Module is 16HP, center = 40.64mm
        float centerX = 40.64f;

        // 5-column grid across the panel (used for button row and CV inputs)
        float gridLeft = 10.0f;
        float gridRight = 71.28f;
        float gridStep = (gridRight - gridLeft) / 4.0f;
        float x0 = gridLeft + gridStep * 0.0f;
        float x1 = gridLeft + gridStep * 1.0f;
        float x2 = gridLeft + gridStep * 2.0f;
        float x3 = gridLeft + gridStep * 3.0f;
        float x4 = gridLeft + gridStep * 4.0f;

        // 4-column grid (used for FM editor trims)
        float trimLeft = 16.0f;
        float trimRight = 65.28f;
        float trimStep = (trimRight - trimLeft) / 3.0f;
        float t0 = trimLeft + trimStep * 0.0f;
        float t1 = trimLeft + trimStep * 1.0f;
        float t2 = trimLeft + trimStep * 2.0f;
        float t3 = trimLeft + trimStep * 3.0f;

#ifndef METAMODULE
    // Headline is baked into the faceplate PNG; keep the subtitle and all other text.
    addChild(mlCreateLabel(Vec(centerX, 13.22f), "PreenFM2 Engine by Xavier Hosxe", 9.75f, dimGreen));
#endif

        // --- Patch Display (center, below title) ---
#ifndef METAMODULE
        {
            MinimalithDisplay* display = new MinimalithDisplay();
            display->module = module;
        // Full-width display (matches mock)
            // Move down ~6px to make room for larger headline/subtitle
        // Shrink ~30% horizontally to better match text scale
        display->box.pos = mm2px(Vec(16.39f, 18.04f));
        display->box.size = mm2px(Vec(48.50f, 14.f));
            addChild(display);
        }
#endif

#ifdef METAMODULE
        // MetaModule dynamic text display for preset name + number.
        struct MinimalithMMDisplay : MetaModule::VCVTextDisplay {};
        auto* mmDisplay = new MinimalithMMDisplay();
    // Expand the text display area ~20% to avoid clipping (keep same center).
    mmDisplay->box.pos = mm2px(Vec(11.54f, 16.64f));
    mmDisplay->box.size = mm2px(Vec(58.20f, 16.80f));
        // Use a built-in MetaModule font by name (see MetaModule SDK fonts.hh).
        mmDisplay->font = "Default_14";
    mmDisplay->color = Colors565::Green;
        mmDisplay->firstLightId = Minimalith::PATCH_DISPLAY;
        addChild(mmDisplay);
#endif

#ifndef METAMODULE
    // --- Section title: PRESET ---
    // Place ~6px below the screen
    addChild(mlCreateLabel(Vec(centerX, 34.08f), "P R E S E T", 13.2f, neonGreen));
#endif

    // --- Top row: buttons + preset/volume knobs ---
    // Move down ~10px total
    float topRowY = 44.04f;
    float topLabelY = topRowY + 3.5f;
    float loadRowY = topRowY;
    float loadLabelY = topLabelY;
#ifndef METAMODULE
    // VCV: move LOAD straight up so it is centered relative to the screen area.
    // Screen is at y=18.04mm with height=14mm, so center is 25.04mm.
    loadRowY = 25.04f;
    loadLabelY = loadRowY + 3.5f;
#endif
    addParam(createParamCentered<TL1105>(mm2px(Vec(x0, loadRowY)), module, Minimalith::LOAD_PARAM));
#ifndef METAMODULE
    addChild(mlCreateLabel(Vec(x0, loadLabelY), "LOAD", 7.5f, neonGreen));

    // VOLUME: mirrored opposite LOAD (right side of screen), centered vertically.
    addParam(createParamCentered<Trimpot>(mm2px(Vec(x4, loadRowY)), module, Minimalith::VOLUME_PARAM));
    addChild(mlCreateLabel(Vec(x4, loadLabelY), "VOLUME", 7.5f, neonGreen));
#endif

    // MORPH is VCV-only; keep spacing even by leaving the spot empty on MetaModule.
#ifndef METAMODULE
    // Spread top-row buttons away from the center preset buttons.
    const float sideBtnDx = 18.0f;
    const float morphX = x2 - sideBtnDx;
    const float panicX = x2 + sideBtnDx;
    addParam(createParamCentered<TL1105>(mm2px(Vec(morphX, topRowY)), module, Minimalith::MORPH_PARAM));
    addChild(mlCreateLabel(Vec(morphX, topLabelY), "MORPH", 7.5f, neonGreen));
#endif

    // PRESET (= preset select)
#ifdef METAMODULE
    addParam(createParamCentered<Trimpot>(mm2px(Vec(x2, topRowY)), module, Minimalith::PATCH_PARAM));
#else
    // VCV Rack: replace the preset knob with Prev/Next buttons to avoid skipping.
    const float presetBtnDx = 4.2f;
    addParam(createParamCentered<TL1105>(mm2px(Vec(x2 - presetBtnDx, topRowY)), module, Minimalith::PRESET_PREV_PARAM));
    addParam(createParamCentered<TL1105>(mm2px(Vec(x2 + presetBtnDx, topRowY)), module, Minimalith::PRESET_NEXT_PARAM));
    // Arrows should sit just above the buttons.
    // Raise by ~4px (≈1.4mm) vs previous placement.
    // Then nudge down ~2px (≈0.7mm) to match the panel.
    addChild(mlCreateLabel(Vec(x2 - presetBtnDx, topRowY - 4.9f), "<", 9.5f, neonGreen));
    addChild(mlCreateLabel(Vec(x2 + presetBtnDx, topRowY - 4.9f), ">", 9.5f, neonGreen));
#endif

    // VOLUME
    // MetaModule keeps VOLUME in the top row (faceplate text matches).
    // VCV Rack moves VOLUME next to the V/OCT input (below) to keep the top row clean.
#ifdef METAMODULE
    addParam(createParamCentered<Trimpot>(mm2px(Vec(x3, topRowY)), module, Minimalith::VOLUME_PARAM));
#endif

    // PANIC
    addParam(createParamCentered<TL1105>(mm2px(Vec(
#ifdef METAMODULE
        x4
#else
        panicX
#endif
        , topRowY)), module, Minimalith::PANIC_PARAM));
#ifndef METAMODULE
    addChild(mlCreateLabel(Vec(panicX, topLabelY), "PANIC", 7.5f, neonGreen));
#endif

#ifndef METAMODULE
    // --- Section title: FM EDITOR ---
    // Move up ~2px and increase size ~20%
    // Move up another ~2px
    // Move up additional ~5px
    addChild(mlCreateLabel(Vec(centerX, 54.24f), "F M  E D I T O R", 13.2f, neonGreen));
#endif

    // --- FM EDITOR trims (ALGO / EVO / IM SCAN / DRIFT / TEAR) ---
    struct MinimalithTearSwitch : CKSS {
        MinimalithTearSwitch() {
            box.size = box.size.mult(0.75f);
        }
    };

    float fmTrimY = 64.60f;
    float fmTrimLeft = 12.00f;
    float fmTrimRight = 69.28f;
    float fmTrimStep = (fmTrimRight - fmTrimLeft) / 4.0f;
    float ft0 = fmTrimLeft + fmTrimStep * 0.0f;
    float ft1 = fmTrimLeft + fmTrimStep * 1.0f;
    float ft2 = fmTrimLeft + fmTrimStep * 2.0f;
    float ft3 = fmTrimLeft + fmTrimStep * 3.0f;
    float ft4 = fmTrimLeft + fmTrimStep * 4.0f;
    addParam(createParamCentered<Trimpot>(mm2px(Vec(ft0, fmTrimY)), module, Minimalith::ALGO_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(ft1, fmTrimY)), module, Minimalith::EVO_TRIM_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(ft2, fmTrimY)), module, Minimalith::IM_TRIM_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(ft3, fmTrimY)), module, Minimalith::DRIFT_TRIM_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(ft4, fmTrimY)), module, Minimalith::TEAR_TRIM_PARAM));
    addParam(createParamCentered<MinimalithTearSwitch>(mm2px(Vec(ft4 + 6.0f, fmTrimY)), module, Minimalith::TEAR_FOLD_SWITCH_PARAM));
#ifndef METAMODULE
    // Match the primary label styling (LOAD/MORPH/etc)
    addChild(mlCreateLabel(Vec(ft0, fmTrimY + 4.5f), "ALGO", 7.5f, neonGreen));
    addChild(mlCreateLabel(Vec(ft1, fmTrimY + 4.5f), "EVO", 7.5f, neonGreen));
    addChild(mlCreateLabel(Vec(ft2, fmTrimY + 4.5f), "IM SCAN", 7.5f, neonGreen));
    addChild(mlCreateLabel(Vec(ft3, fmTrimY + 4.5f), "DRIFT", 7.5f, neonGreen));
    addChild(mlCreateLabel(Vec(ft4, fmTrimY + 4.5f), "TEAR", 7.5f, neonGreen));
    addChild(mlCreateLabel(Vec(ft4 + 6.0f, fmTrimY + 4.5f), "FOLD", 6.5f, neonGreen));
#endif


    #ifndef METAMODULE
        // --- Section title: CV INPUTS ---
        // Move up ~3px and increase size ~20%
        // Move up to total ~4px
        // Move up additional ~4px
        addChild(mlCreateLabel(Vec(centerX, 75.86f), "C V  I N P U T S", 13.2f, neonGreen));
    #endif

        // --- CV input jacks row (TEAR / ALGO / EVO / IM SCAN / DRIFT / RANDOM) ---
        // Keep ports closer together so 6 jacks fit this row.
        float cvPortStep = 13.5f;
        float cvPortLeft = centerX - cvPortStep * 2.5f;
        float cx0 = cvPortLeft + cvPortStep * 0.0f;
        float cx1 = cvPortLeft + cvPortStep * 1.0f;
        float cx2 = cvPortLeft + cvPortStep * 2.0f;
        float cx3 = cvPortLeft + cvPortStep * 3.0f;
        float cx4 = cvPortLeft + cvPortStep * 4.0f;
        float cx5 = cvPortLeft + cvPortStep * 5.0f;
        float cvY = 88.60f;
        float cvLabelY = cvY - 4.f - 0.68f - 1.02f - 0.68f;
    #ifndef METAMODULE
        addChild(mlCreateLabel(Vec(cx0, cvLabelY), "TEAR", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(cx1, cvLabelY), "ALGO", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(cx2, cvLabelY), "EVO", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(cx3, cvLabelY), "IM SCAN", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(cx4, cvLabelY), "DRIFT", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(cx5, cvLabelY), "RANDOM", 7.5f, neonGreen));
    #endif
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx0, cvY)), module, Minimalith::TEAR_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx1, cvY)), module, Minimalith::CV1_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx2, cvY)), module, Minimalith::EVO_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx3, cvY)), module, Minimalith::CV2_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx4, cvY)), module, Minimalith::DRIFT_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(cx5, cvY)), module, Minimalith::NEXT_TRIG_INPUT));

    #ifndef METAMODULE
        // --- Section title: GATE/PITCH ---
        // Move down ~5px and match requested spacing
        addChild(mlCreateLabel(Vec(centerX, 101.94f), "G A T E   /   V / O C T", 13.2f, neonGreen));
    #endif

        // --- Gate + Pitch ---
        // Move GATE/V-OCT up ~4px
        // Move GATE/V-OCT up ~3px
        float gateY = 103.36f;
        float gateLabelY = gateY - 4.f - 1.02f - 0.68f - 0.34f;
    #ifndef METAMODULE
        addChild(mlCreateLabel(Vec(x0, gateLabelY), "GATE", 8.f, neonGreen));
        addChild(mlCreateLabel(Vec(x4, gateLabelY), "V/OCT", 8.f, neonGreen));
    #endif
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(x0, gateY)), module, Minimalith::GATE_INPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(x4, gateY)), module, Minimalith::PITCH_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(x4 - 9.0f, gateY)), module, Minimalith::OCTAVE_TRIM_PARAM));
    #ifndef METAMODULE
        addChild(mlCreateLabel(Vec(x4 - 9.0f, gateLabelY), "OCT", 8.f, neonGreen));
    #endif
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x0, gateY + 6.f)), module, Minimalith::GATE_LIGHT));

        // --- Outputs ---
        // Raise outputs ~4px
        float outY = 112.17f;
        float outLabelY = outY - 4.f + 4.08f;
        float outPortY = outY + 1.02f + 1.36f + 4.08f;
        float scCtrlY = outPortY;
        float scCtrlLabelY = scCtrlY - 4.4f - 3.f * (25.4f / 75.f);
        float scTrimSpacing = 20.f * (25.4f / 75.f);
        float scAmtX = x2 - 6.f;
        float scAtkX = scAmtX + scTrimSpacing;
        float scRelX = scAtkX + scTrimSpacing;
    #ifndef METAMODULE
        addChild(mlCreateLabel(Vec(x0, outLabelY), "LEFT", 8.5f, neonGreen));
        addChild(mlCreateLabel(Vec(x1, outLabelY), "SC IN", 7.5f, neonGreen));
        addChild(mlCreateLabel(Vec(x4, outLabelY), "RIGHT", 8.5f, neonGreen));
        addChild(mlCreateLabel(Vec(scAmtX, scCtrlLabelY), "AMT", 6.8f, neonGreen));
        addChild(mlCreateLabel(Vec(scAtkX, scCtrlLabelY), "ATK", 6.8f, neonGreen));
        addChild(mlCreateLabel(Vec(scRelX, scCtrlLabelY), "REL", 6.8f, neonGreen));
    #endif
        addParam(createParamCentered<Trimpot>(mm2px(Vec(scAmtX, scCtrlY)), module, Minimalith::SC_AMOUNT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(scAtkX, scCtrlY)), module, Minimalith::SC_ATTACK_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(scRelX, scCtrlY)), module, Minimalith::SC_RELEASE_PARAM));
        addOutput(createOutputCentered<MinimalithPort>(mm2px(Vec(x0, outPortY)), module, Minimalith::LEFT_OUTPUT));
        addInput(createInputCentered<MinimalithPort>(mm2px(Vec(x1, outPortY)), module, Minimalith::SIDECHAIN_INPUT));
        addOutput(createOutputCentered<MinimalithPort>(mm2px(Vec(x4, outPortY)), module, Minimalith::RIGHT_OUTPUT));
    }

    void step() override {
        ModuleWidget::step();
        Minimalith* mod = dynamic_cast<Minimalith*>(this->module);
        if (mod && mod->loadRequested) {
            mod->loadRequested = false;
#ifdef METAMODULE
            if (!mod->loadBrowsing) {
                mod->loadBrowsing = true;
                std::string startDir = mod->metaModuleLoadStartDir();
                async_open_file(startDir.c_str(), "bnk,BNK", "Load PreenFM2 Bank",
                    [mod](char *path) {
                        if (path) {
                            if (mod->bankLoader.loadBank(path)) {
                                mod->bankPath = path;
                                mod->currentPatch = 0;
                                mod->trimsLoadForBank();
                                mod->pendingBankLoad = true;
                            }
                            free(path);
                            MetaModule::Patch::mark_patch_modified();
                        }
                        mod->loadBrowsing = false;
                    }
                );
            }
#else
            char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL,
                osdialog_filters_parse("PreenFM2 Bank:bnk"));
            if (path) {
                mod->loadBankAndPatch(std::string(path), 0);
                free(path);
            }
#endif
        }
    }

    void appendContextMenu(Menu* menu) override {
        Minimalith* module = dynamic_cast<Minimalith*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator);

        // Load bank menu item
        menu->addChild(createMenuItem("Load PreenFM2 Bank (.bnk)", "",
            [=]() {
#ifdef METAMODULE
                // MetaModule: async file browser (runs in GUI context)
                async_open_file(module->metaModuleLoadStartDir().c_str(), "bnk,BNK", "Load PreenFM2 Bank",
                    [module](char *path) {
                        if (path) {
                            if (module->bankLoader.loadBank(path)) {
                                module->bankPath = path;
                                module->currentPatch = 0;
                                module->trimsLoadForBank();
                                module->pendingBankLoad = true;
                            }
                            free(path);
                            MetaModule::Patch::mark_patch_modified();
                        }
                    }
                );
#else
                char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL,
                    osdialog_filters_parse("PreenFM2 Bank:bnk"));
                if (path) {
                    module->loadBankAndPatch(std::string(path), 0);
                    free(path);
                }
#endif
            }
        ));

        menu->addChild(createMenuItem("Save current preset to bank slot", "",
            [=]() {
                if (!module->saveCurrentToBankSlot()) {
                    // Keep silent on failure (Rack has no standard toast here).
                    // The display name remains unchanged.
                }
            }
        ));

#ifndef METAMODULE
        menu->addChild(createMenuItem("Copy preset", "",
            [=]() {
                module->copyCurrentPresetToClipboard();
            }
        ));

        menu->addChild(createMenuItem("Paste preset to current slot", "",
            [=]() {
                module->pasteClipboardPresetToCurrentSlot();
            }
        ));
#endif

        menu->addChild(createCheckMenuItem(
            "TEAR export fallback (legacy firmware)", "",
            [module]() { return module->exportTearLegacyFallback; },
            [module]() { module->exportTearLegacyFallback = !module->exportTearLegacyFallback; }
        ));

#ifndef METAMODULE
        if (module->bankLoader.isLoaded() && !module->bankPath.empty()) {
            menu->addChild(new MenuSeparator);

            menu->addChild(createMenuItem("Save bank", "",
                [=]() {
                    module->bakeTearInteropForAllBankSlots();
                    module->bankLoader.saveBankInPlace();
                }
            ));

            menu->addChild(createMenuItem("Save bank as...", "",
                [=]() {
                    std::string dir = module->bankPath.empty() ? asset::user("MorphWorx") : system::getDirectory(module->bankPath);
                    system::createDirectories(dir);
                    char* path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), NULL,
                        osdialog_filters_parse("PreenFM2 Bank:bnk"));
                    if (!path) return;
                    std::string p = path;
                    free(path);
                    if (system::getExtension(p).empty()) {
                        p += ".bnk";
                    }
                    system::createDirectories(system::getDirectory(p));
                    module->bakeTearInteropForAllBankSlots();
                    if (module->bankLoader.saveBankToPath(p)) {
                        module->bankPath = p;
                        module->bankLoader.setBankPath(p);
                    }
                }
            ));
        }
#endif

#ifndef METAMODULE
        // --- Rename current preset (inline text field) ---
        if (module->bankLoader.isLoaded() && !module->bankPath.empty()) {
            menu->addChild(new MenuSeparator);
            menu->addChild(createMenuLabel("Rename preset (Enter to confirm):"));

            struct MLRenameField : ui::TextField {
                Minimalith* module = nullptr;

                MLRenameField() {
                    box.size.x = 220.f;
                }

                void onSelectKey(const SelectKeyEvent& e) override {
                    if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
                        if (module) {
                            std::string name = getText();
                            module->renameCurrentPreset(name);
                        }
                        // Close the menu after confirming.
                        MenuOverlay* overlay = getAncestorOfType<MenuOverlay>();
                        if (overlay) overlay->requestDelete();
                        e.consume(this);
                        return;
                    }
                    ui::TextField::onSelectKey(e);
                }
            };

            auto* field = new MLRenameField;
            field->module = module;
            field->setText(module->patchNameBase);
            field->selectAll();
            menu->addChild(field);
        }
#endif

        // Show current bank path
        if (!module->bankPath.empty()) {
            menu->addChild(createMenuLabel("Bank: " + module->bankPath));
        }
    }
};

Model* modelMinimalith = createModel<Minimalith, MinimalithWidget>("Minimalith");
