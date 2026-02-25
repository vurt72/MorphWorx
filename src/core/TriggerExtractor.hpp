#pragma once
#include "EnergyField.hpp"
#include "MetricEngine.hpp"
#include "MetricSpec.hpp"
#include "../dsp/RandomGenerator.hpp"

namespace pwmt {

struct Trigger {
    float phase;
    float velocity;
    Trigger() : phase(0.0f), velocity(0.0f) {}
    Trigger(float p, float v) : phase(p), velocity(v) {}
};

struct ExtractionConfig {
    float threshold;
    float velocityFloor;
    float velocityCeiling;
    float minTimeBetween;
    bool useMetricAccents;
    float humanization;
    float quantizeStrength;  // 0=free, 1=fully grid-locked to 28-step grid
};

struct ExtractionPresets {
    static ExtractionConfig kick() {
        return {0.5f, 0.6f, 1.0f, 0.10f, true, 0.08f, 0.95f};
    }
    static ExtractionConfig snarePrimary() {
        return {0.6f, 0.5f, 1.0f, 0.14f, true, 0.12f, 0.9f};
    }
    static ExtractionConfig snareGhost() {
        return {0.45f, 0.2f, 0.5f, 0.08f, false, 0.2f, 0.7f};
    }
    static ExtractionConfig hatClosed() {
        return {0.25f, 0.4f, 1.0f, 0.03f, true, 0.15f, 0.85f};
    }
    static ExtractionConfig hatOpen() {
        return {0.4f, 0.6f, 1.0f, 0.06f, false, 0.10f, 0.8f};
    }
};

// Fixed-size trigger buffer to avoid heap allocation
struct TriggerBuffer {
    static constexpr int MAX_TRIGGERS = 64;
    Trigger triggers[MAX_TRIGGERS];
    int count = 0;

    void clear() { count = 0; }
    void push(float phase, float vel) {
        if (count < MAX_TRIGGERS) {
            triggers[count++] = {phase, vel};
        }
    }
    void sortByPhase();
    int size() const { return count; }
    const Trigger& operator[](int i) const { return triggers[i]; }
    Trigger& operator[](int i) { return triggers[i]; }
};

class TriggerExtractor {
public:
    TriggerExtractor();

    // Extract into a pre-allocated TriggerBuffer (zero heap allocation)
    void extractInto(TriggerBuffer& out, const EnergyField& field,
                     const ExtractionConfig& config,
                     const MetricSpec& metric,
                     const MetricAccentCurve* accentCurve = nullptr,
                     uint32_t seed = 0);

    void applySwing(TriggerBuffer& triggers, float swingAmount, const MetricSpec& metric);
    void addRatchets(TriggerBuffer& triggers, float chaos, float density, const MetricSpec& metric, uint32_t seed);
    static void applyHatChoke(TriggerBuffer& closedHat, TriggerBuffer& openHat, float chokeDist);
    static void applyHatChoke(TriggerBuffer& closedHat, TriggerBuffer& openHat) {
        applyHatChoke(closedHat, openHat, 0.004f);
    }

private:
    float calculateVelocity(float energy, float metricAccent,
                            const ExtractionConfig& config, float randomValue,
                            float phase, const MetricSpec& metric);
    bool canPlaceTrigger(float phase, const TriggerBuffer& existing, float minTimeBetween);
};

} // namespace pwmt
