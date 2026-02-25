// PreenFM2 VCV Port - SynthStateAware shim
#pragma once

#include "SynthState.h"

class SynthStateAware {
public:
    virtual ~SynthStateAware() {}
    virtual void setSynthState(SynthState* sState) {
        this->synthState = sState;
    }
protected:
    SynthState* synthState = nullptr;
};
