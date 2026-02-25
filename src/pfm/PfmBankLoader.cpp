/*
 * PfmBankLoader.cpp - Load PreenFM2 .bnk bank files
 *
 * Based on PreenFM2 by Xavier Hosxe (GPL-3.0-or-later)
 * VCV Rack port by Bemushroomed
 */

#include "PfmBankLoader.h"
#include <cstdio>
#include <cstring>

PfmBankLoader::PfmBankLoader()
    : patchCount(0)
    , bankLoaded(false)
{
}

PfmBankLoader::~PfmBankLoader() {
}

bool PfmBankLoader::loadBank(const std::string& path) {
    bankLoaded = false;
    patchCount = 0;
    bankData.clear();
    bankPath = path;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size != PFM_BANK_SIZE) {
        // Not a valid PreenFM2 bank file
        fclose(f);
        return false;
    }

    fseek(f, 0, SEEK_SET);
    bankData.resize(PFM_BANK_SIZE);
    size_t bytesRead = fread(bankData.data(), 1, PFM_BANK_SIZE, f);
    fclose(f);

    if (bytesRead != PFM_BANK_SIZE) {
        bankData.clear();
        return false;
    }

    // Count valid patches (non-empty name)
    patchCount = PFM_PATCHES_PER_BANK;
    bankLoaded = true;
    return true;
}

void PfmBankLoader::copyFloat(const float* src, float* dst, int count) const {
    for (int i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

void PfmBankLoader::convertMemoryToParams(const FlashSynthParams* memory, OneSynthParams* params) const {
    // Adapted from PreenFMFileType::convertMemoryToParams
    copyFloat((float*)&memory->engine1, (float*)&params->engine1, 4);
    copyFloat((float*)&memory->engine2, (float*)&params->engine2, 4);

    // Version compatibility
    if (params->engine2.pfmVersion == 0.0f) {
        params->engine2.playMode = 1.0f;
        params->engine2.unisonDetune = .12f;
        params->engine2.unisonSpread = .5f;
    } else if (params->engine2.pfmVersion == 1.0f) {
        if (params->engine2.playMode == 0.0f) {
            params->engine1.numberOfVoice = 1;
            params->engine2.playMode = 1.0f;
        } else {
            params->engine1.numberOfVoice = 3;
            params->engine2.playMode = params->engine1.numberOfVoice;
        }
    }
    params->engine2.pfmVersion = 2.0f;

    // Arpeggiator (load but won't use)
    copyFloat((float*)&memory->engineArp1, (float*)&params->engineArp1, 4 * 2);
    params->engineArpUserPatterns = memory->engineArpUserPatterns;

    // Modulation indices (different layout in flash vs runtime)
    params->engineIm1.modulationIndex1 = memory->flashEngineIm1.modulationIndex1;
    params->engineIm1.modulationIndex2 = memory->flashEngineIm1.modulationIndex2;
    params->engineIm2.modulationIndex3 = memory->flashEngineIm1.modulationIndex3;
    params->engineIm2.modulationIndex4 = memory->flashEngineIm1.modulationIndex4;
    params->engineIm3.modulationIndex5 = memory->flashEngineIm2.modulationIndex5;
    params->engineIm3.modulationIndex6 = memory->flashEngineIm2.modulationIndex6;
    params->engineIm1.modulationIndexVelo1 = memory->flashEngineVeloIm1.modulationIndexVelo1;
    params->engineIm1.modulationIndexVelo2 = memory->flashEngineVeloIm1.modulationIndexVelo2;
    params->engineIm2.modulationIndexVelo3 = memory->flashEngineVeloIm1.modulationIndexVelo3;
    params->engineIm2.modulationIndexVelo4 = memory->flashEngineVeloIm1.modulationIndexVelo4;
    params->engineIm3.modulationIndexVelo5 = memory->flashEngineVeloIm2.modulationIndexVelo5;
    params->engineIm3.modulationIndexVelo6 = memory->flashEngineVeloIm2.modulationIndexVelo6;

    // Mix, effect, oscillators, envelopes
    copyFloat((float*)&memory->engineMix1, (float*)&params->engineMix1, 4 * 3);
    copyFloat((float*)&memory->effect, (float*)&params->effect, 4);
    copyFloat((float*)&memory->osc1, (float*)&params->osc1, 4 * 6);
    copyFloat((float*)&memory->env1a, (float*)&params->env1a, 4 * 6 * 2);

    // Matrix rows
    copyFloat((float*)&memory->matrixRowState1, (float*)&params->matrixRowState1, 4 * 12);

    // LFOs
    copyFloat((float*)&memory->lfoOsc1, (float*)&params->lfoOsc1, 4 * 3);
    copyFloat((float*)&memory->lfoEnv1, (float*)&params->lfoEnv1, 4);
    copyFloat((float*)&memory->lfoEnv2, (float*)&params->lfoEnv2, 4);
    copyFloat((float*)&memory->lfoSeq1, (float*)&params->lfoSeq1, 4 * 2);

    // Phases and MIDI note curves
    copyFloat((float*)&memory->lfoPhases, (float*)&params->lfoPhases, 4);
    copyFloat((float*)&memory->midiNote1Curve, (float*)&params->midiNote1Curve, 4);
    copyFloat((float*)&memory->midiNote2Curve, (float*)&params->midiNote2Curve, 4);

    // Step sequencer steps
    for (int s = 0; s < 16; s++) {
        params->lfoSteps1.steps[s] = memory->lfoSteps1.steps[s];
        params->lfoSteps2.steps[s] = memory->lfoSteps2.steps[s];
    }

    // Preset name
    for (int s = 0; s < 13; s++) {
        params->presetName[s] = memory->presetName[s];
    }

    // Performance (reset)
    params->performance1.perf1 = 0.0f;
    params->performance1.perf2 = 0.0f;
    params->performance1.perf3 = 0.0f;
    params->performance1.perf4 = 0.0f;

    // Default effect params if all zero
    if (params->effect.type == 0.0f && params->effect.param1 == 0.0f &&
        params->effect.param2 == 0.0f && params->effect.param3 == 0.0f) {
        params->effect.param1 = 0.5f;
        params->effect.param2 = 0.5f;
        params->effect.param3 = 1.0f;
    }

    // Default MIDI note curve compatibility
    if (params->midiNote1Curve.breakNote == 0.0f && params->midiNote1Curve.curveAfter == 0.0f &&
        params->midiNote1Curve.curveBefore == 0.0f) {
        params->midiNote1Curve.curveAfter = 1;
    }
    if (params->midiNote2Curve.breakNote == 0.0f && params->midiNote2Curve.curveAfter == 0.0f &&
        params->midiNote2Curve.curveBefore == 0.0f) {
        params->midiNote2Curve.curveBefore = 4;
        params->midiNote2Curve.curveAfter = 1;
        params->midiNote2Curve.breakNote = 60;
    }
}

void PfmBankLoader::convertParamsToMemory(const OneSynthParams* params, FlashSynthParams* memory) const {
    // Reverse of convertMemoryToParams().
    memset(memory, 0, sizeof(FlashSynthParams));

    // Engine
    copyFloat((const float*)&params->engine1, (float*)&memory->engine1, 4);
    copyFloat((const float*)&params->engine2, (float*)&memory->engine2, 4);

    // Arpeggiator
    copyFloat((const float*)&params->engineArp1, (float*)&memory->engineArp1, 4);
    copyFloat((const float*)&params->engineArp2, (float*)&memory->engineArp2, 4);
    memory->engineArpUserPatterns = params->engineArpUserPatterns;

    // IM indices: runtime layout differs from flash layout
    memory->flashEngineIm1.modulationIndex1 = params->engineIm1.modulationIndex1;
    memory->flashEngineIm1.modulationIndex2 = params->engineIm1.modulationIndex2;
    memory->flashEngineIm1.modulationIndex3 = params->engineIm2.modulationIndex3;
    memory->flashEngineIm1.modulationIndex4 = params->engineIm2.modulationIndex4;
    memory->flashEngineIm2.modulationIndex5 = params->engineIm3.modulationIndex5;
    memory->flashEngineIm2.modulationIndex6 = params->engineIm3.modulationIndex6;

    memory->flashEngineVeloIm1.modulationIndexVelo1 = params->engineIm1.modulationIndexVelo1;
    memory->flashEngineVeloIm1.modulationIndexVelo2 = params->engineIm1.modulationIndexVelo2;
    memory->flashEngineVeloIm1.modulationIndexVelo3 = params->engineIm2.modulationIndexVelo3;
    memory->flashEngineVeloIm1.modulationIndexVelo4 = params->engineIm2.modulationIndexVelo4;
    memory->flashEngineVeloIm2.modulationIndexVelo5 = params->engineIm3.modulationIndexVelo5;
    memory->flashEngineVeloIm2.modulationIndexVelo6 = params->engineIm3.modulationIndexVelo6;

    // Mix, effect, oscillators, envelopes
    copyFloat((const float*)&params->engineMix1, (float*)&memory->engineMix1, 4 * 3);
    copyFloat((const float*)&params->effect, (float*)&memory->effect, 4);
    copyFloat((const float*)&params->osc1, (float*)&memory->osc1, 4 * 6);
    copyFloat((const float*)&params->env1a, (float*)&memory->env1a, 4 * 6 * 2);

    // Matrix
    copyFloat((const float*)&params->matrixRowState1, (float*)&memory->matrixRowState1, 4 * 12);

    // LFOs and step sequencer
    copyFloat((const float*)&params->lfoOsc1, (float*)&memory->lfoOsc1, 4 * 3);
    copyFloat((const float*)&params->lfoEnv1, (float*)&memory->lfoEnv1, 4);
    copyFloat((const float*)&params->lfoEnv2, (float*)&memory->lfoEnv2, 4);
    copyFloat((const float*)&params->lfoSeq1, (float*)&memory->lfoSeq1, 4 * 2);

    // Phases and MIDI note curves
    copyFloat((const float*)&params->lfoPhases, (float*)&memory->lfoPhases, 4);
    copyFloat((const float*)&params->midiNote1Curve, (float*)&memory->midiNote1Curve, 4);
    copyFloat((const float*)&params->midiNote2Curve, (float*)&memory->midiNote2Curve, 4);

    // Step sequencer steps
    for (int s = 0; s < 16; s++) {
        memory->lfoSteps1.steps[s] = params->lfoSteps1.steps[s];
        memory->lfoSteps2.steps[s] = params->lfoSteps2.steps[s];
    }

    // Preset name
    for (int s = 0; s < 13; s++) {
        memory->presetName[s] = params->presetName[s];
    }
}

bool PfmBankLoader::setPatch(int index, const struct OneSynthParams& params) {
    if (!bankLoaded) return false;
    if (index < 0 || index >= PFM_PATCHES_PER_BANK) return false;
    if ((int)bankData.size() != PFM_BANK_SIZE) return false;

    FlashSynthParams flash;
    convertParamsToMemory(&params, &flash);

    const size_t offset = (size_t)index * (size_t)PFM_ALIGNED_PATCH_SIZE;
    if (offset + sizeof(FlashSynthParams) > bankData.size()) return false;
    memcpy(bankData.data() + offset, &flash, sizeof(FlashSynthParams));
    return true;
}

bool PfmBankLoader::saveBankInPlace() const {
    if (!bankLoaded) return false;
    if (bankPath.empty()) return false;
    if ((int)bankData.size() != PFM_BANK_SIZE) return false;

    FILE* f = fopen(bankPath.c_str(), "wb");
    if (!f) return false;
    size_t bytesWritten = fwrite(bankData.data(), 1, PFM_BANK_SIZE, f);
    fclose(f);
    return bytesWritten == PFM_BANK_SIZE;
}

bool PfmBankLoader::saveBankToPath(const std::string& path) const {
    if (!bankLoaded) return false;
    if (path.empty()) return false;
    if ((int)bankData.size() != PFM_BANK_SIZE) return false;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t bytesWritten = fwrite(bankData.data(), 1, PFM_BANK_SIZE, f);
    fclose(f);
    return bytesWritten == PFM_BANK_SIZE;
}

void PfmBankLoader::setBankPath(const std::string& path) {
    bankPath = path;
}

bool PfmBankLoader::getPatch(int index, struct OneSynthParams& outParams) const {
    if (!bankLoaded || index < 0 || index >= PFM_PATCHES_PER_BANK) {
        return false;
    }

    const uint8_t* patchData = bankData.data() + (index * PFM_ALIGNED_PATCH_SIZE);
    const FlashSynthParams* flashParams = reinterpret_cast<const FlashSynthParams*>(patchData);

    // Zero out the output first
    memset(&outParams, 0, sizeof(OneSynthParams));

    // Convert from flash format to runtime format
    convertMemoryToParams(flashParams, &outParams);

    return true;
}

std::string PfmBankLoader::getPatchName(int index) const {
    if (!bankLoaded || index < 0 || index >= PFM_PATCHES_PER_BANK) {
        return "";
    }

    const uint8_t* patchData = bankData.data() + (index * PFM_ALIGNED_PATCH_SIZE);
    const FlashSynthParams* flashParams = reinterpret_cast<const FlashSynthParams*>(patchData);

    char name[14] = {0};
    for (int i = 0; i < 13; i++) {
        name[i] = flashParams->presetName[i];
        if (name[i] == 0) break;
    }
    return std::string(name);
}

int PfmBankLoader::nextPatch(int current) const {
    if (!bankLoaded) return 0;
    int next = current + 1;
    if (next >= PFM_PATCHES_PER_BANK) next = 0;
    return next;
}

int PfmBankLoader::prevPatch(int current) const {
    if (!bankLoaded) return 0;
    int prev = current - 1;
    if (prev < 0) prev = PFM_PATCHES_PER_BANK - 1;
    return prev;
}
