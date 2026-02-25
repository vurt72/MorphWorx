// PreenFM2 VCV Port - Minimal EncodersListener stub
#pragma once

class EncodersListener {
public:
    virtual ~EncodersListener() {}
    virtual void encoderTurned(int encoder, int ticks) {}
    virtual void buttonPressed(int button) {}
    virtual void twoButtonsPressed(int button1, int button2) {}
    virtual void encoderTurnedWhileButtonPressed(int encoder, int ticks, int button) {}
};
