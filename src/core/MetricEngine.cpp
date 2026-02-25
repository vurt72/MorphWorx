#include "MetricEngine.hpp"
#include <algorithm>

namespace pwmt {

MetricGrouping::MetricGrouping() : groups({{3, 2, 2, 0}}), numGroups(3), beatsPerBar(7) {}

MetricGrouping::MetricGrouping(std::initializer_list<int> g)
    : MetricGrouping(7, g) {}

MetricGrouping::MetricGrouping(int beats, std::initializer_list<int> g)
    : groups({{0, 0, 0, 0}}), numGroups(0), beatsPerBar(std::max(1, beats)) {
    int sum = 0;
    for (int v : g) {
        if (numGroups < 4) {
            groups[numGroups++] = std::max(1, v);
            sum += std::max(1, v);
        }
    }
    if (sum != beatsPerBar || numGroups == 0) {
        groups = {{3, 2, 2, 0}};
        numGroups = 3;
        beatsPerBar = 7;
    }
}

int MetricGrouping::getGroupLength(int groupIdx) const {
    if (groupIdx < 0 || groupIdx >= numGroups) return 0;
    return groups[groupIdx];
}

int MetricGrouping::getGroupForBeat(int beatNum) const {
    int accum = 0;
    for (int i = 0; i < numGroups; i++) {
        accum += groups[i];
        if (beatNum < accum) return i;
    }
    return numGroups - 1;
}

float MetricGrouping::getPositionInGroup(float barPhase) const {
    float beatPos = barPhase * static_cast<float>(beatsPerBar);
    int beatIdx = static_cast<int>(beatPos);
    beatIdx = std::max(0, std::min(beatsPerBar - 1, beatIdx));
    int groupIdx = getGroupForBeat(beatIdx);

    int beatStart = 0;
    for (int i = 0; i < groupIdx; i++) beatStart += groups[i];
    int groupLen = groups[groupIdx];
    float withinBeat = beatPos - static_cast<float>(beatIdx);
    float posInGroupBeats = (beatIdx - beatStart) + withinBeat;
    return posInGroupBeats / std::max(1, groupLen);
}

MetricEngine::MetricEngine() = default;

float MetricAccentCurve::sampleAt(float phase) const {
    // Called every sample — avoid modulo (integer division is slow on ARM)
    int res = std::max(1, std::min(resolution, MAX_RESOLUTION));
    float idx = phase * static_cast<float>(res);
    if (idx < 0.0f) idx += static_cast<float>(res);
    if (idx >= static_cast<float>(res)) idx -= static_cast<float>(res);
    int i0 = static_cast<int>(idx);
    if (i0 >= res) i0 = res - 1;
    if (i0 < 0) i0 = 0;
    int i1 = (i0 + 1 < res) ? i0 + 1 : 0;
    float frac = idx - static_cast<float>(i0);
    return accents[i0] + frac * (accents[i1] - accents[i0]);
}

float MetricEngine::calculateAccentStrength(int groupSize, int positionInGroup) {
    if (positionInGroup == 0) return 1.0f;
    if (positionInGroup == groupSize - 1) return 0.5f;
    return 0.3f;
}

MetricAccentCurve MetricEngine::generateAccentCurve(const MetricGrouping& grouping, const MetricSpec& metric) {
    MetricAccentCurve curve{};
    int beats = std::max(1, metric.beatsPerBar);
    int res = std::max(1, std::min(metric.gridStepsPerBar(), MetricAccentCurve::MAX_RESOLUTION));
    curve.resolution = res;
    for (int i = 0; i < res; i++) {
        float phase = static_cast<float>(i) / static_cast<float>(res);
        float beatPos = phase * static_cast<float>(beats);
        int beatIdx = std::min(beats - 1, static_cast<int>(beatPos));
        int groupIdx = grouping.getGroupForBeat(beatIdx);
        int beatStart = 0;
        for (int g = 0; g < groupIdx; g++) beatStart += grouping.groups[g];
        int positionInGroup = beatIdx - beatStart;
        float strength = calculateAccentStrength(grouping.groups[groupIdx], positionInGroup);
        // Slight ramp within beat to emphasize onsets
        float withinBeat = beatPos - beatIdx;
        strength = strength * (0.8f + 0.2f * (1.0f - withinBeat));
        curve.accents[i] = strength;
    }
    return curve;
}

MetricGrouping MetricEngine::selectRandomGrouping(uint32_t seed) {
    RandomGenerator rng(seed);
    int idx = rng.uniformInt(0, NUM_STANDARD_GROUPINGS - 1);
    return STANDARD_GROUPINGS[idx];
}

MetricGrouping MetricEngine::evolveGrouping(const MetricGrouping& current, float chaosAmount, uint32_t seed) {
    RandomGenerator rng(seed);
    float roll = rng.uniform();
    MetricGrouping next = current;

    // Rotate grouping occasionally
    if (roll < 0.3f + 0.4f * chaosAmount) {
        if (next.numGroups > 1) {
            int first = next.groups[0];
            for (int i = 0; i < next.numGroups - 1; i++) {
                next.groups[i] = next.groups[i + 1];
            }
            next.groups[next.numGroups - 1] = first;
        }
    }

    // Small mutation: tweak one group size while keeping sum=beatsPerBar
    if (roll > 0.5f && next.numGroups > 0) {
        int idx = rng.uniformInt(0, next.numGroups - 1);
        int delta = rng.bernoulli(0.5f) ? 1 : -1;
        MetricGrouping mutated = next;
        mutated.groups[idx] = std::max(1, mutated.groups[idx] + delta);
        int sum = 0;
        for (int i = 0; i < mutated.numGroups; i++) sum += mutated.groups[i];
        int diff = sum - next.beatsPerBar;
        if (diff != 0) {
            int adjustIdx = (idx + 1) % mutated.numGroups;
            mutated.groups[adjustIdx] = std::max(1, mutated.groups[adjustIdx] - diff);
        }
        int finalSum = 0;
        for (int i = 0; i < mutated.numGroups; i++) finalSum += mutated.groups[i];
        if (finalSum == next.beatsPerBar) next = mutated;
    }

    return next;
}

} // namespace pwmt
