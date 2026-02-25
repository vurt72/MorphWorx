#pragma once
#include <cmath>
#include <algorithm>

// Circular math helpers for phase values (0.0 - 1.0)
// Handles wrap-around correctly for distance/interpolation checks.
namespace pwmt {

inline float circularDistance(float p1, float p2) {
    float diff = p2 - p1;
    while (diff > 0.5f) diff -= 1.0f;
    while (diff < -0.5f) diff += 1.0f;
    return diff;
}

inline float circularDistanceAbs(float p1, float p2) {
    return std::abs(circularDistance(p1, p2));
}

inline float circularLerp(float p1, float p2, float t) {
    float dist = circularDistance(p1, p2);
    float result = p1 + dist * t;
    if (result < 0.0f) result += 1.0f;
    if (result >= 1.0f) result -= 1.0f;
    return result;
}

inline bool isPhaseInRange(float phase, float rangeStart, float rangeEnd) {
    float distFromStart = circularDistance(rangeStart, phase);
    float rangeSize = circularDistance(rangeStart, rangeEnd);
    return distFromStart >= 0.0f && distFromStart <= rangeSize;
}

// Wrap phase to [0.0, 1.0) — branchless, no fmod
inline float wrapPhase(float phase) {
    // Fast: subtract integer part. Works for phase in roughly [-1e6, 1e6]
    phase -= static_cast<float>(static_cast<int>(phase));
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

} // namespace pwmt
