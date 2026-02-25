#pragma once
#include <array>
#include <algorithm>
#include "MetricEngine.hpp"
#include "MetricSpec.hpp"
#include "../dsp/CircularMath.hpp"
#include "../dsp/LookupTables.hpp"
#include "../dsp/RandomGenerator.hpp"

namespace pwmt {

struct EnergyField {
    // Fixed-size array: max resolution 48 (covers 28 for drums, 35 for hats)
    static constexpr int MAX_RESOLUTION = 48;
    float energy[MAX_RESOLUTION];
    int resolution;

    explicit EnergyField(int res = 28) : resolution(res) { reset(); }

    float sampleAt(float phase) const;
    void add(const EnergyField& other, float scale = 1.0f);
    void scale(float scalar);
    void multiply(const EnergyField& other);
    void applyThreshold(float threshold);
    void normalize();

    // Fixed-size output: writes indices into provided array, returns count
    int findLocalMaxima(int* outIndices, int maxOut, float minHeight = 0.3f) const;

    void reset() {
        for (int i = 0; i < MAX_RESOLUTION; i++) energy[i] = 0.0f;
    }

    void copyFrom(const EnergyField& other) {
        resolution = other.resolution;
        for (int i = 0; i < resolution; i++) energy[i] = other.energy[i];
    }
};

struct GravityWell {
    float center;
    float strength;
    float width;
    float energyAt(float phase) const;
};

class EnergyFieldGenerator {
public:
    EnergyFieldGenerator();

    // In-place generation: writes into provided fields (zero heap allocation)
    void generateKickField(EnergyField& out, const MetricGrouping& grouping, const MetricSpec& metric,
                           float chaos, float density, uint32_t seed);

    struct SnareFields {
        EnergyField primary;
        EnergyField ghost;
    };
    void generateSnareFields(SnareFields& out, const MetricGrouping& grouping,
                             const EnergyField& kickField,
                             const MetricSpec& metric,
                             float chaos, float density, uint32_t seed);

    struct HatFields {
        EnergyField closed;
        EnergyField open;
    };
    void generateHatFields(HatFields& out, const MetricSpec& metric,
                           float density, float chaos, const EnergyField& snareField, uint32_t seed);

private:
    // Fixed-size well storage (max 8 wells)
    static constexpr int MAX_WELLS = 8;
    GravityWell wells_[MAX_WELLS];
    int numWells_;

    int generateKickWells(GravityWell* outWells, int maxWells, const MetricGrouping& grouping, float chaos, float density, uint32_t seed);
    void wellsToField(EnergyField& out, const GravityWell* wells, int numWells, int resolution);

    struct Oscillator {
        float frequency;
        float phase;
        float amplitude;
    };

    void generateInterferencePattern(EnergyField& out, const Oscillator* oscillators, int numOsc, int resolution);
};

} // namespace pwmt
