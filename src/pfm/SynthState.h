// PreenFM2 VCV Port - Minimal SynthState.h shim
// Provides just enough of SynthState for the engine to compile
#pragma once

#include <cstring>
#include "Common.h"
#include "EncodersListener.h"
#include "SynthParamListener.h"
#include "SynthMenuListener.h"
#include "Menu.h"
#include "Storage.h"

// Ensure CVIN is defined
#ifndef CVIN
#define CVIN
#endif

#define BUTTON_SYNTH  0
#define BUTTON_OSC    1
#define BUTTON_ENV    2
#define BUTTON_MATRIX 3
#define BUTTON_LFO    4
#define BUTTON_BACK         5
#define BUTTON_MENUSELECT   6
#define BUTTON_ENCODER   7

enum {
    ENCODER_ENGINE_ALGO = 0,
    ENCODER_ENGINE_VELOCITY,
    ENCODER_ENGINE_VOICE,
    ENCODER_ENGINE_GLIDE
};

enum {
    ENCODER_ENGINE2_PLAY_MODE = 0,
    ENCODER_ENGINE2_UNISON_SPREAD,
    ENCODER_ENGINE2_UNISON_DETUNE,
    ENCODER_USED_FOR_PFM_VERSION,
};

enum {
    ENCODER_ARPEGGIATOR_CLOCK = 0,
    ENCODER_ARPEGGIATOR_BPM,
    ENCODER_ARPEGGIATOR_DIRECTION,
    ENCODER_ARPEGGIATOR_OCTAVE
};

enum {
    ENCODER_ARPEGGIATOR_PATTERN = 0,
    ENCODER_ARPEGGIATOR_DIVISION,
    ENCODER_ARPEGGIATOR_DURATION,
    ENCODER_ARPEGGIATOR_LATCH
};

enum {
    ENCODER_EFFECT_TYPE = 0,
    ENCODER_EFFECT_PARAM1,
    ENCODER_EFFECT_PARAM2,
    ENCODER_EFFECT_PARAM3
};

enum {
    ENCODER_ENGINE_IM1 = 0,
    ENCODER_ENGINE_IM1_VELOCITY,
    ENCODER_ENGINE_IM2,
    ENCODER_ENGINE_IM2_VELOCITY
};

enum {
    ENCODER_ENGINE_IM3 = 0,
    ENCODER_ENGINE_IM3_VELOCITY,
    ENCODER_ENGINE_IM4,
    ENCODER_ENGINE_IM4_VELOCITY,
};

enum {
    ENCODER_ENGINE_IM5 = 0,
    ENCODER_ENGINE_IM5_VELOCITY
};

enum {
    ENCODER_ENGINE_MIX1 = 0,
    ENCODER_ENGINE_PAN1,
    ENCODER_ENGINE_MIX2,
    ENCODER_ENGINE_PAN2,
};

enum {
    ENCODER_LFO_SHAPE = 0,
    ENCODER_LFO_FREQ,
    ENCODER_LFO_BIAS,
    ENCODER_LFO_KSYNC,
};

enum {
    ENCODER_MATRIX_SOURCE = 0,
    ENCODER_MATRIX_MUL,
    ENCODER_MATRIX_DEST1,
    ENCODER_MATRIX_DEST2,
};

enum Algorithm {
    ALGO1 = 0, ALGO2, ALGO3, ALGO4, ALGO5, ALGO6, ALGO7, ALGO8, ALGO9,
    ALG10, ALG11, ALG12, ALG13, ALG14, ALG15, ALG16, ALG17, ALG18,
    ALG19, ALG20, ALG21, ALG22, ALG23, ALG24, ALG25, ALG26, ALG27, ALG28,
    ALGO_END
};

enum OscShape {
    OSC_SHAPE_SIN = 0,
    OSC_SHAPE_SAW,
    OSC_SHAPE_SQUARE,
    OSC_SHAPE_SIN_SQUARE,
    OSC_SHAPE_SIN_ZERO,
    OSC_SHAPE_SIN_POS,
    OSC_SHAPE_RAND,
    OSC_SHAPE_OFF,
    OSC_SHAPE_USER1,
    OSC_SHAPE_USER2,
    OSC_SHAPE_USER3,
    OSC_SHAPE_USER4,
    OSC_SHAPE_USER5,
    OSC_SHAPE_USER6,
    OSC_SHAPE_LAST
};

enum LfoType {
    LFO_SIN = 0,
    LFO_SAW,
    LFO_TRIANGLE,
    LFO_SQUARE,
    LFO_RANDOM,
    LFO_TYPE_MAX
};

enum MidiNoteCurve {
    MIDI_NOTE_CURVE_FLAT = 0,
    MIDI_NOTE_CURVE_LINEAR,
    MIDI_NOTE_CURVE_LINEAR2,
    MIDI_NOTE_CURVE_EXP,
    MIDI_NOTE_CURVE_M_LINEAR,
    MIDI_NOTE_CURVE_M_LINEAR2,
    MIDI_NOTE_CURVE_M_EXP,
    MIDI_NOTE_CURVE_MAX
};

enum OscFrequencyType {
    OSC_FT_KEYBOARD = 0,
    OSC_FT_FIXE,
    OSC_FT_KEYHZ
};

enum OscEnv2Loop {
    LFO_ENV2_NOLOOP = 0,
    LFO_ENV2_LOOP_SILENCE,
    LFO_ENV2_LOOP_ATTACK
};

enum FILTER_TYPE {
    FILTER_OFF = 0,
    FILTER_MIXER,
    FILTER_LP,
    FILTER_HP,
    FILTER_BASS,
    FILTER_BP,
    FILTER_CRUSHER,
    FILTER_LP2,
    FILTER_HP2,
    FILTER_BP2,
    FILTER_LP3,
    FILTER_HP3,
    FILTER_BP3,
    FILTER_PEAK,
    FILTER_NOTCH,
    FILTER_BELL,
    FILTER_LOWSHELF,
    FILTER_HIGHSHELF,
    FILTER_LPHP,
    FILTER_BPds,
    FILTER_LPWS,
    FILTER_TILT,
    FILTER_STEREO,
    FILTER_SAT,
    FILTER_SIGMOID,
    FILTER_FOLD,
    FILTER_WRAP,
    FILTER_ROT,
    FILTER_TEXTURE1,
    FILTER_TEXTURE2,
    FILTER_LPXOR,
    FILTER_LPXOR2,
    FILTER_LPSIN,
    FILTER_HPSIN,
    FILTER_QUADNOTCH,
    FILTER_AP4,
    FILTER_AP4B,
    FILTER_AP4D,
    FILTER_ORYX,
    FILTER_ORYX2,
    FILTER_ORYX3,
    FILTER_18DB,
    FILTER_LADDER,
    FILTER_LADDER2,
    FILTER_DIOD,
    FILTER_KRMG,
    FILTER_TEEBEE,
    FILTER_SVFLH,
    FILTER_CRUSH2,
    FILTER_LAST
};

// ParameterRowDisplay for allParameterRows
struct ParameterRowDisplay {
    const char* rowName;
    const char* paramName[4];
    struct ParameterDisplay params[4];
};

struct AllParameterRowsDisplay {
    struct ParameterRowDisplay* row[NUMBER_OF_ROWS];
};

class Hexter;

// Minimal SynthState class - provides just what the engine needs
class SynthState : public EncodersListener {
public:
    SynthState() {
        // Zero-initialize everything first for bare-metal ARM safety
        memset(&fullState, 0, sizeof(fullState));
        memset(&backupParams, 0, sizeof(backupParams));
        fullState.globalTuning = 440.0f;
        fullState.synthMode = SYNTH_MODE_EDIT;
        fullState.currentMenuItem = nullptr;
        for (int i = 0; i < MIDICONFIG_SIZE + 1; i++) {
            fullState.midiConfigValue[i] = 0;
        }
        params = nullptr;
        currentTimbre = 0;
        stepSelect[0] = 0;
        stepSelect[1] = 0;
    }

    void setStorage(Storage* storage) {}
    void setHexter(Hexter* hexter) {}

    void setParamsAndTimbre(struct OneSynthParams *newParams, int newCurrentTimbre) {
        this->params = newParams;
        this->currentTimbre = newCurrentTimbre;
    }

    void propagateNewParamValue(int timbre, int currentRow, int encoder, ParameterDisplay* param, float oldValue, float newValue) {}
    void propagateNewParamValueFromExternal(int timbre, int currentRow, int encoder, ParameterDisplay* param, float oldValue, float newValue) {}
    void propagateBeforeNewParamsLoad(int timbre) {}
    void propagateAfterNewParamsLoad(int timbre) {}
    void propagateAfterNewComboLoad() {}
    void propagateNewTimbre(int timbre) {}
    void propagateNoteOff() {}
    void propagateNoteOn(int shift) {}
    void propagateShowAlgo() {}
    void propagateShowIMInformation() {}
    bool canPlayNote() { return true; }

    void setScalaEnable(bool enable) {}
    void setScalaScale(int scaleNumber) {}
    void setCurrentInstrument(int value) { currentTimbre = value; }

    void loadPreenFMPatchFromMidi(int timbre, int bank, int bankLSB, int patchNumber, struct OneSynthParams* params) {}

    int currentTimbre;
    struct OneSynthParams *params;
    struct OneSynthParams backupParams;
    struct FullState fullState;
    char stepSelect[2];
    char patternSelect = 0;
};

// Global allParameterRows stub - needed by Env.h for algo info lookup
// The actual param rows are not used in our minimal port
extern struct AllParameterRowsDisplay allParameterRows;

// Frequency table - 128 MIDI notes to Hz
extern float frequency[];
