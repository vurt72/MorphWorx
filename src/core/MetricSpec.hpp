#pragma once

#include <algorithm>
#include <cmath>

namespace pwmt {

// Describes the base rhythmic grid used across generation/warp/extraction.
// Must remain heap-free and cheap to copy (MetaModule-safe).
struct MetricSpec {
    int beatsPerBar = 7;
    int subdivPerBeat = 4;

    // Derived: total grid steps in one bar.
    int gridStepsPerBar() const {
        return std::max(1, beatsPerBar * subdivPerBeat);
    }

    // Phase step size within one bar, in normalized phase [0..1).
    float gridStepPhase() const {
        return 1.0f / static_cast<float>(gridStepsPerBar());
    }

    // Converts a grid step index [0..gridStepsPerBar) to a normalized phase.
    float stepToPhase(int step) const {
        int steps = gridStepsPerBar();
        int s = step % steps;
        if (s < 0) s += steps;
        return static_cast<float>(s) / static_cast<float>(steps);
    }

    // Nearest grid step for a given phase.
    int phaseToNearestStep(float phase) const {
        float wrapped = phase - std::floor(phase);
        int steps = gridStepsPerBar();
        int idx = static_cast<int>(std::floor(wrapped * static_cast<float>(steps) + 0.5f));
        if (idx >= steps) idx -= steps;
        if (idx < 0) idx += steps;
        return idx;
    }
};

} // namespace pwmt
