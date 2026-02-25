#pragma once
#include <array>
#include <initializer_list>
#include "../dsp/RandomGenerator.hpp"
#include "MetricSpec.hpp"

namespace pwmt {

struct MetricGrouping {
    // Fixed-size array: max 4 groups, with count
    // Eliminates std::vector heap allocation
    std::array<int, 4> groups;
    int numGroups;
    int beatsPerBar;

    MetricGrouping();
    MetricGrouping(std::initializer_list<int> g);
    MetricGrouping(int beats, std::initializer_list<int> g);

    int getNumGroups() const { return numGroups; }
    int getGroupLength(int groupIdx) const;
    int getGroupForBeat(int beatNum) const;
    float getPositionInGroup(float barPhase) const;
};

static const MetricGrouping STANDARD_GROUPINGS[] = {
    {3, 2, 2},
    {2, 2, 3},
    {4, 3},
    {3, 4},
    {2, 3, 2},
};
static constexpr int NUM_STANDARD_GROUPINGS = 5;

struct MetricAccentCurve {
    static constexpr int MAX_RESOLUTION = 48;
    std::array<float, MAX_RESOLUTION> accents{};
    int resolution = 28;
    float sampleAt(float phase) const;
};

class MetricEngine {
public:
    MetricEngine();
    MetricAccentCurve generateAccentCurve(const MetricGrouping& grouping, const MetricSpec& metric);
    MetricGrouping selectRandomGrouping(uint32_t seed);
    MetricGrouping evolveGrouping(const MetricGrouping& current, float chaosAmount, uint32_t seed);

private:
    float calculateAccentStrength(int groupSize, int positionInGroup);
};

} // namespace pwmt
