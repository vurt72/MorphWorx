// PreenFM2 VCV Port - Stub for LiquidCrystal LCD
#pragma once

class LiquidCrystal {
public:
    void setRealTimeAction(bool) {}
    void clearActions() {}
    void clear() {}
    void setCursor(int, int) {}
    void print(const char*) {}
    void print(char) {}
    void print(int) {}
    void print(float) {}
};
