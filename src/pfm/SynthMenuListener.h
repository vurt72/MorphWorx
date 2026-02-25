// PreenFM2 VCV Port - Minimal SynthMenuListener stub
#pragma once

struct FullState;

class SynthMenuListener {
public:
    virtual ~SynthMenuListener() {}
    virtual void newSynthMode(FullState* fullState) {}
    virtual void menuBack(int oldMenuState, FullState* fullState) {}
    virtual void newMenuState(FullState* fullState) {}
    virtual void newMenuSelect(FullState* fullState) {}
    SynthMenuListener* nextListener = nullptr;
};
