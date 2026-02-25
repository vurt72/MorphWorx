// PreenFM2 VCV Port - CVIn stub
// In our VCV port, CV handling is done directly in PfmEngine
#pragma once

class CVIn {
public:
    void updateValues() {}
    float getFrequency() { return 440.0f; }
    float getCvin1() { return cvin1; }
    float getCvin2() { return cvin2; }
    float getCvin3() { return cvin3; }
    float getCvin4() { return cvin4; }
    int getGate() { return gate; }
    void updateFormula(int a2, int a6) {}

    float cvin1 = 0.0f;
    float cvin2 = 0.0f;
    float cvin3 = 0.0f;
    float cvin4 = 0.0f;
    int gate = 0;
};
