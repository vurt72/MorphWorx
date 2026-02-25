#pragma once
#include "MetricEngine.hpp"
#include "EnergyField.hpp"
#include "../dsp/RandomGenerator.hpp"

namespace pwmt {

struct PatternSnapshot {
    MetricGrouping grouping;
    // Fixed-size well storage (no vector)
    static constexpr int MAX_WELLS = 8;
    GravityWell kickWells[MAX_WELLS];
    int numKickWells;
    float warpMacroAmount;
    float warpMicroAmount;
    uint32_t seed;
    int generationNumber;
};

struct EvolutionConfig {
    float chaosAmount;
    float evolutionRate;
    float memoryDepth;
};

class PatternMemory {
public:
    PatternMemory();
    PatternSnapshot generateNextPattern(const EvolutionConfig& config);
    const PatternSnapshot& getCurrentPattern() const;
    void forceGrouping(const MetricGrouping& grouping);
    void reset();

private:
    static constexpr int MAX_HISTORY = 8;
    PatternSnapshot history_[MAX_HISTORY];
    int historyCount_;
    int historyStart_;  // ring buffer index
    int generationCounter_;

    const PatternSnapshot& getBack() const;
    void pushBack(const PatternSnapshot& snap);

    void mutateWells(const GravityWell* inWells, int inCount,
                     GravityWell* outWells, int& outCount, int maxWells,
                     float chaosAmount, uint32_t seed);
    int generateFreshWells(GravityWell* outWells, int maxWells,
                           const MetricGrouping& grouping, float chaosAmount, uint32_t seed);
    bool shouldBreak(const EvolutionConfig& config, uint32_t seed);
};

} // namespace pwmt
