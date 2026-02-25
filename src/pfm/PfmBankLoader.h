/*
 * PfmBankLoader.h - Load PreenFM2 .bnk bank files
 *
 * Based on PreenFM2 by Xavier Hosxe (GPL-3.0-or-later)
 * VCV Rack port by Bemushroomed
 */
#pragma once

#include "synth/Common.h"
#include <string>
#include <vector>

// PreenFM2 bank format: 128 patches * 1024 bytes = 131072 bytes per .bnk file
#define PFM_BANK_SIZE 131072
#define PFM_PATCHES_PER_BANK 128
#define PFM_ALIGNED_PATCH_SIZE 1024

// Storage format of a patch in bank file
struct FlashSynthParams {
    struct Engine1Params engine1;
    // FlashEngineIm uses a different layout than OneSynthParams
    struct { float modulationIndex1, modulationIndex2, modulationIndex3, modulationIndex4; } flashEngineIm1;
    struct { float modulationIndex5, modulationIndex6, notUsed1, notUsed2; } flashEngineIm2;
    struct EngineMix1 engineMix1;
    struct EngineMix2 engineMix2;
    struct EngineMix3 engineMix3;
    struct OscillatorParams osc1, osc2, osc3, osc4, osc5, osc6;
    struct EnvelopeParamsA env1a;
    struct EnvelopeParamsB env1b;
    struct EnvelopeParamsA env2a;
    struct EnvelopeParamsB env2b;
    struct EnvelopeParamsA env3a;
    struct EnvelopeParamsB env3b;
    struct EnvelopeParamsA env4a;
    struct EnvelopeParamsB env4b;
    struct EnvelopeParamsA env5a;
    struct EnvelopeParamsB env5b;
    struct EnvelopeParamsA env6a;
    struct EnvelopeParamsB env6b;
    struct MatrixRowParams matrixRowState1, matrixRowState2, matrixRowState3, matrixRowState4;
    struct MatrixRowParams matrixRowState5, matrixRowState6, matrixRowState7, matrixRowState8;
    struct MatrixRowParams matrixRowState9, matrixRowState10, matrixRowState11, matrixRowState12;
    struct LfoParams lfoOsc1, lfoOsc2, lfoOsc3;
    struct EnvelopeParams lfoEnv1;
    struct Envelope2Params lfoEnv2;
    struct StepSequencerParams lfoSeq1, lfoSeq2;
    struct StepSequencerSteps lfoSteps1, lfoSteps2;
    char presetName[13];
    struct EngineArp1 engineArp1;
    struct EngineArp2 engineArp2;
    struct { float modulationIndexVelo1, modulationIndexVelo2, modulationIndexVelo3, modulationIndexVelo4; } flashEngineVeloIm1;
    struct { float modulationIndexVelo5, modulationIndexVelo6, notUsed1, notUsed2; } flashEngineVeloIm2;
    struct EffectRowParams effect;
    struct EngineArpUserPatterns engineArpUserPatterns;
    struct LfoPhaseRowParams lfoPhases;
    struct MidiNoteCurveRowParams midiNote1Curve;
    struct MidiNoteCurveRowParams midiNote2Curve;
    struct Engine2Params engine2;
};

class PfmBankLoader {
public:
    PfmBankLoader();
    ~PfmBankLoader();

    // Load a .bnk file. Returns true on success.
    bool loadBank(const std::string& path);

    // Get the number of valid patches in the loaded bank
    int getPatchCount() const { return patchCount; }

    // Get a patch by index (0-127). Returns true on success.
    bool getPatch(int index, struct OneSynthParams& outParams) const;

    // Get patch name by index
    std::string getPatchName(int index) const;

    // Modify the currently loaded bank in memory (no disk write).
    bool setPatch(int index, const struct OneSynthParams& params);
    // Save the current in-memory bank data back to the original file.
    bool saveBankInPlace() const;

    // Save the current in-memory bank data to an arbitrary file path.
    // Does not modify the currently loaded bank contents.
    bool saveBankToPath(const std::string& path) const;

    // Update the internal bank path used by saveBankInPlace().
    // Intended for "Save bank as..." flows.
    void setBankPath(const std::string& path);

    // Navigate patches
    int nextPatch(int current) const;
    int prevPatch(int current) const;

    // Is a bank loaded?
    bool isLoaded() const { return bankLoaded; }

    // Get the bank file path
    const std::string& getBankPath() const { return bankPath; }

private:
    void convertMemoryToParams(const FlashSynthParams* memory, OneSynthParams* params) const;
    void convertParamsToMemory(const OneSynthParams* params, FlashSynthParams* memory) const;
    void copyFloat(const float* src, float* dst, int count) const;

    std::vector<uint8_t> bankData;
    std::string bankPath;
    int patchCount;
    bool bankLoaded;
};
