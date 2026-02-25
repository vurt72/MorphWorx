#include "PatternMemory.hpp"
#include <algorithm>

namespace pwmt {

PatternMemory::PatternMemory() : historyCount_(0), historyStart_(0), generationCounter_(0) {
    reset();
}

void PatternMemory::reset() {
    historyCount_ = 0;
    historyStart_ = 0;
    generationCounter_ = 0;
    EvolutionConfig cfg{0.3f, 0.2f, 0.7f};
    pushBack(generateNextPattern(cfg));
}

const PatternSnapshot& PatternMemory::getBack() const {
    int idx = (historyStart_ + historyCount_ - 1) % MAX_HISTORY;
    return history_[idx];
}

void PatternMemory::pushBack(const PatternSnapshot& snap) {
    if (historyCount_ < MAX_HISTORY) {
        int idx = (historyStart_ + historyCount_) % MAX_HISTORY;
        history_[idx] = snap;
        historyCount_++;
    } else {
        history_[historyStart_] = snap;
        historyStart_ = (historyStart_ + 1) % MAX_HISTORY;
    }
}

const PatternSnapshot& PatternMemory::getCurrentPattern() const {
    return getBack();
}

void PatternMemory::forceGrouping(const MetricGrouping& grouping) {
    PatternSnapshot snap = getBack();
    snap.grouping = grouping;
    pushBack(snap);
}

void PatternMemory::mutateWells(const GravityWell* inWells, int inCount,
                                 GravityWell* outWells, int& outCount, int maxWells,
                                 float chaosAmount, uint32_t seed) {
    RandomGenerator rng(seed);
    outCount = 0;
    for (int i = 0; i < inCount && outCount < maxWells; i++) {
        outWells[outCount] = inWells[i];
        outWells[outCount].center = wrapPhase(outWells[outCount].center + rng.uniform(-0.02f, 0.02f) * (0.5f + chaosAmount));
        outWells[outCount].strength += rng.uniform(-0.15f, 0.15f);
        if (outWells[outCount].strength < 0.2f) outWells[outCount].strength = 0.2f;
        if (outWells[outCount].strength > 1.0f) outWells[outCount].strength = 1.0f;
        outWells[outCount].width += rng.uniform(-0.01f, 0.01f);
        if (outWells[outCount].width < 0.005f) outWells[outCount].width = 0.005f;
        if (outWells[outCount].width > 0.05f) outWells[outCount].width = 0.05f;
        outCount++;
    }
    // Maybe remove one
    if (rng.bernoulli(0.1f * chaosAmount) && outCount > 1) {
        int removeIdx = rng.uniformInt(0, outCount - 1);
        for (int i = removeIdx; i < outCount - 1; i++) {
            outWells[i] = outWells[i + 1];
        }
        outCount--;
    }
    // Maybe add one
    if (rng.bernoulli(0.12f * chaosAmount) && outCount < maxWells) {
        outWells[outCount].center = rng.uniform();
        outWells[outCount].strength = 0.4f + 0.3f * chaosAmount;
        outWells[outCount].width = 0.015f;
        outCount++;
    }
}

int PatternMemory::generateFreshWells(GravityWell* outWells, int maxWells,
                                       const MetricGrouping& grouping,
                                       float chaosAmount, uint32_t seed) {
    RandomGenerator rng(seed);
    int count = 0;
    float cursor = 0.0f;
    for (int g = 0; g < grouping.getNumGroups() && count < maxWells; g++) {
        int len = grouping.getGroupLength(g);
        float groupLen = static_cast<float>(len) / static_cast<float>(std::max(1, grouping.beatsPerBar));
        outWells[count].center = cursor;
        outWells[count].strength = 0.8f;
        outWells[count].width = 0.02f;
        count++;
        if (len >= 3 && rng.bernoulli(0.35f + 0.35f * chaosAmount) && count < maxWells) {
            outWells[count].center = wrapPhase(cursor + groupLen * 0.5f + rng.uniform(-0.02f, 0.02f));
            outWells[count].strength = 0.6f;
            outWells[count].width = 0.018f;
            count++;
        }
        cursor += groupLen;
    }
    if (rng.bernoulli(0.1f * chaosAmount) && count < maxWells) {
        outWells[count].center = wrapPhase(-0.06f);
        outWells[count].strength = 0.4f;
        outWells[count].width = 0.015f;
        count++;
    }
    return count;
}

bool PatternMemory::shouldBreak(const EvolutionConfig& config, uint32_t seed) {
    RandomGenerator rng(seed);
    float roll = rng.uniform();
    return roll < config.evolutionRate * 0.5f + config.chaosAmount * 0.25f;
}

PatternSnapshot PatternMemory::generateNextPattern(const EvolutionConfig& config) {
    RandomGenerator rng(static_cast<uint32_t>(generationCounter_ * 31 + 17));
    PatternSnapshot next{};

    if (historyCount_ == 0) {
        next.grouping = MetricEngine().selectRandomGrouping(rng.uniformInt(0, 1000));
        next.numKickWells = generateFreshWells(next.kickWells, PatternSnapshot::MAX_WELLS,
                                                next.grouping, config.chaosAmount, rng.uniformInt(0, 10000));
        next.warpMacroAmount = 0.5f;
        next.warpMicroAmount = 0.3f;
    } else {
        const auto& current = getBack();
        bool doBreak = shouldBreak(config, rng.uniformInt(0, 100000));
        if (doBreak) {
            next.grouping = MetricEngine().selectRandomGrouping(rng.uniformInt(0, 1000));
            next.numKickWells = generateFreshWells(next.kickWells, PatternSnapshot::MAX_WELLS,
                                                    next.grouping, config.chaosAmount, rng.uniformInt(0, 10000));
            next.warpMacroAmount = current.warpMacroAmount + rng.uniform(-0.2f, 0.2f);
            if (next.warpMacroAmount < 0.0f) next.warpMacroAmount = 0.0f;
            if (next.warpMacroAmount > 1.0f) next.warpMacroAmount = 1.0f;
            next.warpMicroAmount = current.warpMicroAmount + rng.uniform(-0.2f, 0.2f);
            if (next.warpMicroAmount < 0.0f) next.warpMicroAmount = 0.0f;
            if (next.warpMicroAmount > 1.0f) next.warpMicroAmount = 1.0f;
        } else {
            next.grouping = MetricEngine().evolveGrouping(current.grouping, config.chaosAmount, rng.uniformInt(0, 1000));
            mutateWells(current.kickWells, current.numKickWells,
                        next.kickWells, next.numKickWells, PatternSnapshot::MAX_WELLS,
                        config.chaosAmount, rng.uniformInt(0, 10000));
            next.warpMacroAmount = current.warpMacroAmount + rng.uniform(-0.1f, 0.1f);
            if (next.warpMacroAmount < 0.0f) next.warpMacroAmount = 0.0f;
            if (next.warpMacroAmount > 1.0f) next.warpMacroAmount = 1.0f;
            next.warpMicroAmount = current.warpMicroAmount + rng.uniform(-0.1f, 0.1f);
            if (next.warpMicroAmount < 0.0f) next.warpMicroAmount = 0.0f;
            if (next.warpMicroAmount > 1.0f) next.warpMicroAmount = 1.0f;
        }
    }

    next.seed = rng.uniformInt(0, 0x7fffffff);
    next.generationNumber = generationCounter_++;
    pushBack(next);
    return next;
}

} // namespace pwmt
