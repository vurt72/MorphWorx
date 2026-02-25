// PreenFM2 VCV Port - Minimal SynthParamListener stub
#pragma once

#include <cstdint>

enum ParameterDisplayType {
    DISPLAY_TYPE_FLOAT = 0,
    DISPLAY_TYPE_INT,
    DISPLAY_TYPE_STRINGS,
    DISPLAY_TYPE_STEP_SEQ_BPM,
    DISPLAY_TYPE_STEP_SEQ1,
    DISPLAY_TYPE_STEP_SEQ2,
    DISPLAY_TYPE_ARP_PATTERN,
    DISPLAY_TYPE_NONE
};

struct ParameterDisplay {
    float minValue;
    float maxValue;
    float numberOfValues;
    ParameterDisplayType displayType;
    const char** valueName;
    const unsigned char* valueNameOrder;
    const unsigned char* valueNameOrderReversed;
    float incValue;
};

class SynthParamListener {
public:
    virtual ~SynthParamListener() {}
    virtual void newParamValue(int timbre, int currentrow, int encoder, ParameterDisplay* param, float oldValue, float newValue) {}
    virtual void newParamValueFromExternal(int timbre, int currentrow, int encoder, ParameterDisplay* param, float oldValue, float newValue) {}
    virtual void newTimbre(int timbre) {}
    virtual void newcurrentRow(int timbre, int newcurrentRow) {}
    virtual void beforeNewParamsLoad(int timbre) {}
    virtual void afterNewParamsLoad(int timbre) {}
    virtual void afterNewComboLoad() {}
    virtual void playNote(int timbreNumber, char note, char velocity) {}
    virtual void stopNote(int timbreNumber, char note) {}
    virtual void newPresetName(int timbre) {}
    virtual void showAlgo() {}
    virtual void showIMInformation() {}
    SynthParamListener* nextListener = nullptr;
};

class SynthParamChecker {
public:
    virtual ~SynthParamChecker() {}
    virtual void checkNewParamValue(int timbre, int currentRow, int encoder, float oldValue, float *newValue) {}
    SynthParamChecker* nextChecker = nullptr;
};
