#include "TriggerExtractor.hpp"
#include <algorithm>
#include <cmath>

namespace pwmt {

namespace {

static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

} // namespace

void TriggerBuffer::sortByPhase() {
    // Simple insertion sort for small arrays (max 64 elements)
    for (int i = 1; i < count; i++) {
        Trigger key = triggers[i];
        int j = i - 1;
        while (j >= 0 && triggers[j].phase > key.phase) {
            triggers[j + 1] = triggers[j];
            j--;
        }
        triggers[j + 1] = key;
    }
}

TriggerExtractor::TriggerExtractor() = default;

float TriggerExtractor::calculateVelocity(float energy,
                                          float metricAccent,
                                          const ExtractionConfig& config,
                                          float randomValue,
                                          float phase,
                                          const MetricSpec& metric) {
    float range = config.velocityCeiling - config.velocityFloor;
    float vel = config.velocityFloor + energy * range;

    // Amen-style subdivision accent: downbeats loud, upbeats medium, 16ths slightly softer
    int steps = std::max(1, metric.gridStepsPerBar());
    int subdiv = std::max(1, metric.subdivPerBeat);
    int gridIdx = static_cast<int>(phase * static_cast<float>(steps) + 0.5f) % steps;
    int subPos = gridIdx % subdiv;
    float subdivAccent = 1.0f;
    if ((subdiv % 2 == 0) && subPos == subdiv / 2) subdivAccent = 0.9f;
    else if (subdiv >= 4 && (subPos == 1 || subPos == subdiv - 1)) subdivAccent = 0.75f;
    vel *= subdivAccent;

    if (config.useMetricAccents) {
        vel *= (0.7f + 0.3f * metricAccent);
    }
    float jitter = (randomValue - 0.5f) * 2.0f * config.humanization * range;
    vel += jitter;
    if (vel < config.velocityFloor) vel = config.velocityFloor;
    if (vel > config.velocityCeiling) vel = config.velocityCeiling;
    return vel;
}

bool TriggerExtractor::canPlaceTrigger(float phase,
                                       const TriggerBuffer& existing,
                                       float minTimeBetween) {
    for (int i = 0; i < existing.count; i++) {
        float dist = circularDistanceAbs(existing.triggers[i].phase, phase);
        if (dist < minTimeBetween) return false;
    }
    return true;
}

void TriggerExtractor::extractInto(TriggerBuffer& out, const EnergyField& field,
                                    const ExtractionConfig& config,
                                    const MetricSpec& metric,
                                    const MetricAccentCurve* accentCurve,
                                    uint32_t seed) {
    RandomGenerator rng(seed);
    out.clear();
    int maximaIndices[EnergyField::MAX_RESOLUTION];
    int numMaxima = field.findLocalMaxima(maximaIndices, EnergyField::MAX_RESOLUTION, config.threshold);
    int steps = std::max(1, metric.gridStepsPerBar());
    float invSteps = 1.0f / static_cast<float>(steps);
    for (int m = 0; m < numMaxima; m++) {
        int idx = maximaIndices[m];
        float phase = static_cast<float>(idx) / field.resolution;

        // Grid quantization: snap to nearest grid position
        float snapped = std::floor(phase * static_cast<float>(steps) + 0.5f) * invSteps;
        if (snapped >= 1.0f) snapped -= 1.0f;
        phase = phase + (snapped - phase) * config.quantizeStrength;

        float energy = field.energy[idx];
        float metricAccent = accentCurve ? accentCurve->sampleAt(phase) : 1.0f;
        float vel = calculateVelocity(energy, metricAccent, config, rng.uniform(), phase, metric);
        if (canPlaceTrigger(phase, out, config.minTimeBetween)) {
            out.push(phase, vel);
        }
    }
    out.sortByPhase();
}

void TriggerExtractor::applySwing(TriggerBuffer& triggers, float swingAmount, const MetricSpec& metric) {
    if (triggers.count == 0) return;
    float swing = swingAmount;
    if (swing > 1.0f) swing = 1.0f;
    if (swing < -1.0f) swing = -1.0f;
    int steps = std::max(1, metric.gridStepsPerBar());
    // Max swing displacement = half a grid step
    float maxShift = 0.5f / static_cast<float>(steps);
    float shift = swing * maxShift;
    for (int i = 0; i < triggers.count; i++) {
        // Operate on the grid: odd grid positions get shifted
        int gridPos = static_cast<int>(std::floor(triggers[i].phase * static_cast<float>(steps) + 0.5f));
        if (gridPos % 2 == 1) {
            triggers[i].phase = wrapPhase(triggers[i].phase + shift);
        }
    }
    triggers.sortByPhase();
}

void TriggerExtractor::addRatchets(TriggerBuffer& triggers, float chaos, float density, const MetricSpec& metric,
                                   uint32_t seed, const RatchetConfig& config, TriggerBuffer* fillMarkers) {
    if (chaos < config.chaosThreshold) return;

    RandomGenerator rng(seed);
    float chaosNorm = (chaos - config.chaosThreshold) / std::max(0.001f, 1.0f - config.chaosThreshold);
    chaosNorm = clamp01(chaosNorm);
    float effectiveDensity = clamp01(density * config.densityScale);
    float ratchetProb = chaosNorm * 0.85f * config.probabilityScale;
    ratchetProb *= (0.3f + 0.7f * effectiveDensity);
    ratchetProb = clamp01(ratchetProb);

    int steps = std::max(1, metric.gridStepsPerBar());
    // Grid-aware ratchet spacing: straight 16th/32nd, optional triplet and reverse fills.
    float gridStep = 1.0f / static_cast<float>(steps);
    float gridHalfStep = 0.5f / static_cast<float>(steps);
    float gridTripletStep = gridStep / 3.0f;

    int originalCount = triggers.count;
    for (int i = 0; i < originalCount; i++) {
        if (!rng.bernoulli(ratchetProb)) continue;

        float basePhase = triggers[i].phase;
        float baseVel = triggers[i].velocity;
        enum class RatchetMode {
            Straight,
            Triplet,
            Reverse,
        };
        RatchetMode mode = RatchetMode::Straight;
        float reverseProb = config.allowReverse ? (0.08f + 0.22f * chaosNorm) : 0.0f;
        float tripletProb = config.allowTriplet ? (0.18f + 0.28f * chaosNorm) : 0.0f;
        float modePick = rng.uniform();
        if (config.allowReverse && modePick < reverseProb) {
            mode = RatchetMode::Reverse;
        }
        else if (config.allowTriplet && modePick < reverseProb + tripletProb) {
            mode = RatchetMode::Triplet;
        }

        int extras = rng.uniformInt(config.minExtras, config.maxExtras);
        float spacing = gridHalfStep;
        switch (mode) {
            case RatchetMode::Triplet:
                extras = std::max(2, extras);
                spacing = gridTripletStep;
                break;
            case RatchetMode::Reverse:
                spacing = rng.bernoulli(0.6f) ? gridHalfStep : gridTripletStep;
                break;
            case RatchetMode::Straight:
            default:
                spacing = rng.bernoulli(0.45f + 0.25f * chaosNorm) ? gridHalfStep : gridStep;
                break;
        }

        float minSpacing = std::max(spacing * 0.75f, gridTripletStep * 0.8f);
        bool inserted = false;

        for (int r = 1; r <= extras; r++) {
            float direction = (mode == RatchetMode::Reverse) ? -1.0f : 1.0f;
            float ratchetPhase = wrapPhase(basePhase + direction * spacing * static_cast<float>(r));
            float velScale = (mode == RatchetMode::Reverse)
                ? (0.48f + 0.12f * static_cast<float>(r))
                : (0.55f + rng.uniform(0.0f, 0.25f));
            float ratchetVel = baseVel * velScale;
            if (canPlaceTrigger(ratchetPhase, triggers, minSpacing)) {
                triggers.push(ratchetPhase, ratchetVel);
                inserted = true;
            }
        }

        if (inserted && fillMarkers && canPlaceTrigger(basePhase, *fillMarkers, std::max(0.002f, spacing * 1.5f))) {
            fillMarkers->push(basePhase, 1.0f);
        }
    }
    triggers.sortByPhase();
}

void TriggerExtractor::applyFlams(TriggerBuffer& triggers, float chaos, const FlamConfig& config,
                                  uint32_t seed, TriggerBuffer* fillMarkers) {
    if (chaos < config.minChaos || config.phaseOffset <= 0.0f) return;

    RandomGenerator rng(seed);
    float chaosNorm = (chaos - config.minChaos) / std::max(0.001f, 1.0f - config.minChaos);
    chaosNorm = clamp01(chaosNorm);
    float flamProb = clamp01(chaosNorm * config.maxProbability);

    int originalCount = triggers.count;
    float markerSpacing = std::max(0.002f, config.phaseOffset * 2.0f);
    for (int i = 0; i < originalCount; i++) {
        if (!rng.bernoulli(flamProb)) continue;

        float gracePhase = wrapPhase(triggers[i].phase - config.phaseOffset);
        float graceVel = triggers[i].velocity * config.velocityScale;
        if (canPlaceTrigger(gracePhase, triggers, config.phaseOffset * 0.6f)) {
            triggers.push(gracePhase, graceVel);
            if (fillMarkers && canPlaceTrigger(triggers[i].phase, *fillMarkers, markerSpacing)) {
                fillMarkers->push(triggers[i].phase, 1.0f);
            }
        }
    }

    triggers.sortByPhase();
}

void TriggerExtractor::applyHatChoke(TriggerBuffer& closedHat, TriggerBuffer& openHat, float chokeDist) {
    // Remove open hat triggers only when they're very close to a closed hat.
    // In multi-bar patterns, phases are compressed (e.g. 4 bars: one grid step
    // = 1/112 of the cycle). Use a small absolute distance so we only choke
    // near-coincident hits, not hits on adjacent beats.
    TriggerBuffer cleaned;
    cleaned.clear();
    for (int i = 0; i < openHat.count; i++) {
        bool choked = false;
        for (int j = 0; j < closedHat.count; j++) {
            float dist = circularDistanceAbs(openHat[i].phase, closedHat[j].phase);
            if (dist < chokeDist) {
                choked = true;
                break;
            }
        }
        if (!choked) {
            cleaned.push(openHat[i].phase, openHat[i].velocity);
        }
    }
    // Copy back
    openHat.count = cleaned.count;
    for (int i = 0; i < cleaned.count; i++) {
        openHat[i] = cleaned[i];
    }
}

} // namespace pwmt
