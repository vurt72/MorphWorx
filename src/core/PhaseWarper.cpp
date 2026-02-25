#include "PhaseWarper.hpp"
#include <algorithm>
#include <cmath>

namespace pwmt {

PhaseWarper::PhaseWarper() {}

void PhaseWarper::generateWarpFunction(WarpFunction& out, const MetricGrouping& grouping, const MetricSpec& metric, const WarpConfig& config) {
    // Generate macro warp points directly into the output struct
    // Magnitudes halved from original to reduce wonkiness
    out.numMacroPts = 0;
    float cursor = 0.0f;
    RandomGenerator rng(config.seed);
    for (int g = 0; g < grouping.getNumGroups() && out.numMacroPts < WarpFunction::MAX_WARP_POINTS - 1; g++) {
        int len = grouping.getGroupLength(g);
        float beatsPerBar = static_cast<float>(std::max(1, metric.beatsPerBar));
        float groupLen = static_cast<float>(len) / beatsPerBar;
        float prePhase = wrapPhase(cursor - groupLen * 0.08f);
        if (out.numMacroPts < WarpFunction::MAX_WARP_POINTS) {
            out.macroPts[out.numMacroPts++] = {prePhase, -config.macroAmount * 0.015f};
        }
        if (out.numMacroPts < WarpFunction::MAX_WARP_POINTS) {
            out.macroPts[out.numMacroPts++] = {cursor, -config.macroAmount * 0.025f};
        }
        float postPhase = wrapPhase(cursor + groupLen * 0.35f);
        if (out.numMacroPts < WarpFunction::MAX_WARP_POINTS) {
            out.macroPts[out.numMacroPts++] = {postPhase, config.macroAmount * 0.02f};
        }
        cursor += groupLen;
    }
    if (out.numMacroPts < WarpFunction::MAX_WARP_POINTS && out.numMacroPts > 0) {
        out.macroPts[out.numMacroPts++] = {1.0f, out.macroPts[0].offset};
    }

    // Micro turbulence parameters
    RandomGenerator rng2(config.seed + 77);
    out.microFreq = 6.0f + rng2.uniform(-1.5f, 1.5f);
    out.microPhase = rng2.uniform();
    out.microAmt = config.microAmount;
    out.cubicDrift = config.cubicDrift;
}

void PhaseWarper::applyWarpInPlace(EnergyField& field, const WarpFunction& warp) {
    int res = field.resolution;
    if (res > 48) res = 48;  // Safety clamp
    for (int i = 0; i < res; i++) {
        float phase = static_cast<float>(i) / res;
        float warpedPhase = warp.eval(phase);
        warpScratch_[i] = field.sampleAt(warpedPhase);
    }
    for (int i = 0; i < res; i++) {
        field.energy[i] = warpScratch_[i];
    }
    field.normalize();
}

} // namespace pwmt
