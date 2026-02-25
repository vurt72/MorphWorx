#pragma once
#include <array>
#include <cmath>

// Lightweight lookup tables for fast sine/exp sampling.
namespace pwmt {

template<int SIZE>
class LookupTable {
public:
    LookupTable() = default;

    float lookup(float index) const {
        while (index < 0.0f) index += SIZE;
        while (index >= SIZE) index -= SIZE;
        int i0 = static_cast<int>(index);
        int i1 = (i0 + 1 < SIZE) ? i0 + 1 : 0;
        float frac = index - i0;
        return data_[i0] + frac * (data_[i1] - data_[i0]);
    }

protected:
    std::array<float, SIZE> data_{};
};

template<int SIZE = 1024>
class SineTable : public LookupTable<SIZE> {
public:
    SineTable() {
        for (int i = 0; i < SIZE; i++) {
            float phase = static_cast<float>(i) / SIZE;
            this->data_[i] = std::sin(2.0f * static_cast<float>(M_PI) * phase);
        }
    }

    float samplePhase(float phase) const { return this->lookup(phase * SIZE); }
};

template<int SIZE = 512>
class ExpTable : public LookupTable<SIZE> {
public:
    ExpTable() {
        for (int i = 0; i < SIZE; i++) {
            float x = -10.0f * static_cast<float>(i) / (SIZE - 1);
            this->data_[i] = std::exp(x);
        }
    }

    float sampleNegative(float x) const {
        if (x < 0.0f) return 1.0f;
        if (x >= 10.0f) return 0.0f;
        float index = (x / 10.0f) * (SIZE - 1);
        return this->lookup(index);
    }
};

extern SineTable<1024> g_sineTable;
extern ExpTable<512> g_expTable;

} // namespace pwmt
