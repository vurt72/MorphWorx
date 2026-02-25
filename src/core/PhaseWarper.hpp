#pragma once
#include "MetricEngine.hpp"
#include "MetricSpec.hpp"
#include "../dsp/RandomGenerator.hpp"
#include "../dsp/CircularMath.hpp"
#include "../dsp/LookupTables.hpp"
#include "EnergyField.hpp"

namespace pwmt {

// Plain struct warp function - no std::function, no heap allocation
struct WarpFunction {
    // Stored macro warp points (fixed-size, max 16 points)
    struct WarpPoint {
        float phase;
        float offset;
    };
    static constexpr int MAX_WARP_POINTS = 16;
    WarpPoint macroPts[MAX_WARP_POINTS];
    int numMacroPts = 0;

    float microAmt = 0.0f;
    float cubicDrift = 0.0f;
    float microFreq = 6.0f;
    float microPhase = 0.0f;

    float eval(float phase) const {
        float p = wrapPhase(phase);
        // Macro interpolation
        float macroOffset = 0.0f;
        if (numMacroPts > 0) {
            WarpPoint p0 = macroPts[numMacroPts - 1];
            for (int i = 0; i < numMacroPts; i++) {
                if (macroPts[i].phase >= p) {
                    WarpPoint p1 = macroPts[i];
                    float span = wrapPhase(p1.phase - p0.phase);
                    float t = 0.0f;
                    if (span > 0.0001f) {
                        float diff = wrapPhase(p - p0.phase);
                        t = diff / span;
                    }
                    macroOffset = p0.offset + (p1.offset - p0.offset) * t;
                    break;
                }
                p0 = macroPts[i];
            }
        }
        float microPhaseVal = wrapPhase(microFreq * p + microPhase);
        float micro = g_sineTable.samplePhase(microPhaseVal) * microAmt * 0.005f;
        float cubic = cubicDrift * (p * p * p - p * 0.5f) * 0.02f;
        return wrapPhase(p + macroOffset + micro + cubic);
    }
};

struct WarpConfig {
    float macroAmount;
    float microAmount;
    float cubicDrift;
    uint32_t seed;
};

class PhaseWarper {
public:
    PhaseWarper();
    void generateWarpFunction(WarpFunction& out, const MetricGrouping& grouping, const MetricSpec& metric, const WarpConfig& config);
    void applyWarpInPlace(EnergyField& field, const WarpFunction& warp);

private:
    float warpScratch_[48];  // Fixed-size scratch buffer (max resolution 48)
};

} // namespace pwmt
