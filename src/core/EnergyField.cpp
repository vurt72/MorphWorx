#include "EnergyField.hpp"
#include <algorithm>
#include <cmath>

namespace pwmt {

float EnergyField::sampleAt(float phase) const {
    phase = wrapPhase(phase);
    float idx = phase * static_cast<float>(resolution);
    while (idx < 0.0f) idx += resolution;
    while (idx >= resolution) idx -= resolution;
    int i0 = static_cast<int>(idx);
    int i1 = (i0 + 1) % resolution;
    float frac = idx - i0;
    return energy[i0] + frac * (energy[i1] - energy[i0]);
}

void EnergyField::add(const EnergyField& other, float scale) {
    int n = std::min(resolution, other.resolution);
    for (int i = 0; i < n; i++) {
        energy[i] += other.energy[i] * scale;
    }
}

void EnergyField::scale(float scalar) {
    for (int i = 0; i < resolution; i++) energy[i] *= scalar;
}

void EnergyField::multiply(const EnergyField& other) {
    int n = std::min(resolution, other.resolution);
    for (int i = 0; i < n; i++) {
        energy[i] *= other.energy[i];
    }
}

void EnergyField::applyThreshold(float threshold) {
    for (int i = 0; i < resolution; i++) {
        if (energy[i] < threshold) energy[i] = 0.0f;
    }
}

void EnergyField::normalize() {
    float maxVal = 0.0f;
    for (int i = 0; i < resolution; i++) maxVal = std::max(maxVal, energy[i]);
    if (maxVal <= 0.0001f) return;
    float inv = 1.0f / maxVal;
    for (int i = 0; i < resolution; i++) energy[i] *= inv;
}

int EnergyField::findLocalMaxima(int* outIndices, int maxOut, float minHeight) const {
    int count = 0;
    for (int i = 0; i < resolution && count < maxOut; i++) {
        float prev = energy[(i - 1 + resolution) % resolution];
        float curr = energy[i];
        float next = energy[(i + 1) % resolution];
        if (curr >= prev && curr >= next && curr >= minHeight) {
            outIndices[count++] = i;
        }
    }
    return count;
}

float GravityWell::energyAt(float phase) const {
    float dist = std::abs(circularDistance(center, phase));
    float sigma = width * 0.6f;
    float z = dist / std::max(0.0001f, sigma);
    float shape = dist < width ? g_expTable.sampleNegative(z * z) : 0.0f;
    return strength * shape;
}

EnergyFieldGenerator::EnergyFieldGenerator() : numWells_(0) {}

int EnergyFieldGenerator::generateKickWells(GravityWell* outWells, int maxWells, const MetricGrouping& grouping, float chaos, float density, uint32_t seed) {
    RandomGenerator rng(seed);
    int count = 0;
    float phaseCursor = 0.0f;

    for (int g = 0; g < grouping.getNumGroups() && count < maxWells; g++) {
        int len = grouping.getGroupLength(g);
        float groupPhaseLen = static_cast<float>(len) / static_cast<float>(std::max(1, grouping.beatsPerBar));

        bool placeWell = false;
        if (g == 0) {
            placeWell = true;
        } else {
            float baseProbability = 0.15f + 0.65f * density;
            float lengthBonus = (len >= 3) ? 0.1f : 0.0f;
            float chaosBonus = chaos * 0.1f;
            placeWell = rng.bernoulli(std::min(1.0f, baseProbability + lengthBonus + chaosBonus));
        }

        if (placeWell && count < maxWells) {
            outWells[count].center = phaseCursor;
            outWells[count].strength = (g == 0) ? 1.0f : rng.uniform(0.5f, 0.8f);
            outWells[count].width = 0.015f + chaos * 0.025f;
            count++;
        }

        if (len >= 3 && density > 0.6f && chaos > 0.4f && rng.bernoulli(0.15f + 0.35f * density) && count < maxWells) {
            outWells[count].center = wrapPhase(phaseCursor + groupPhaseLen * 0.5f);
            outWells[count].strength = 0.3f + chaos * 0.3f;
            outWells[count].width = 0.02f + chaos * 0.02f;
            count++;
        }

        phaseCursor += groupPhaseLen;
    }

    return count;
}

void EnergyFieldGenerator::wellsToField(EnergyField& out, const GravityWell* wells, int numWells, int resolution) {
    out.resolution = resolution;
    out.reset();
    for (int i = 0; i < resolution; i++) {
        float phase = static_cast<float>(i) / resolution;
        float e = 0.0f;
        for (int w = 0; w < numWells; w++) e += wells[w].energyAt(phase);
        out.energy[i] = e;
    }
    out.normalize();
}

void EnergyFieldGenerator::generateKickField(EnergyField& out, const MetricGrouping& grouping, const MetricSpec& metric,
                                            float chaos, float density, uint32_t seed) {
    int beats = std::max(1, metric.beatsPerBar);
    int subdiv = std::max(1, metric.subdivPerBeat);
    int steps = std::max(1, std::min(metric.gridStepsPerBar(), EnergyField::MAX_RESOLUTION));

    // Grid-aware kick: place energy directly on metric grid positions
    out.resolution = steps;
    out.reset();
    RandomGenerator rng(seed);

    // Always place strong kick on beat 1 (grid index 0)
    out.energy[0] = 1.0f;

    // Place kicks on each group downbeat
    float phaseCursor = 0.0f;
    for (int g = 1; g < grouping.getNumGroups(); g++) {
        int prevLen = grouping.getGroupLength(g - 1);
        phaseCursor += static_cast<float>(prevLen) / static_cast<float>(beats);
        int gridIdx = static_cast<int>(phaseCursor * static_cast<float>(steps) + 0.5f) % steps;

        float prob = 0.4f + 0.5f * density;
        if (g == 0 || rng.bernoulli(prob)) {
            out.energy[gridIdx] = 0.7f + 0.2f * rng.uniform();
        }
    }

    // Syncopated subdivision kicks: "and" positions (only meaningful when subdiv is even)
    int andOffset = (subdiv >= 2 && (subdiv % 2 == 0)) ? (subdiv / 2) : 0;
    for (int beat = 0; beat < beats; beat++) {
        int andIdx = beat * subdiv + andOffset;
        if (andIdx >= steps) break;
        float syncoProb = 0.05f + 0.25f * density + 0.2f * chaos;
        if (rng.bernoulli(syncoProb)) {
            float str = 0.35f + 0.3f * density * rng.uniform();
            out.energy[andIdx] += str;
        }
        // Ghost kick on the early subdivision (keeps original feel when subdiv=4)
        if (chaos > 0.4f && density > 0.5f && subdiv >= 2) {
            int eIdx = beat * subdiv + 1;
            if (eIdx < steps && rng.bernoulli(0.08f + 0.15f * chaos)) {
                out.energy[eIdx] += 0.25f + 0.15f * chaos;
            }
        }
    }

    out.normalize();
}

void EnergyFieldGenerator::generateSnareFields(SnareFields& fields, const MetricGrouping& grouping,
                                               const EnergyField& kickField,
                                               const MetricSpec& metric,
                                               float chaos, float density, uint32_t seed) {
    RandomGenerator rng(seed);
    int beats = std::max(1, metric.beatsPerBar);
    int subdiv = std::max(1, metric.subdivPerBeat);
    int steps = std::max(1, std::min(metric.gridStepsPerBar(), EnergyField::MAX_RESOLUTION));

    fields.primary.resolution = steps;
    fields.primary.reset();
    fields.ghost.resolution = steps;
    fields.ghost.reset();

    // --- Primary snare: amen/breakcore style ---
    // Not every group gets a snare. Guarantee at least 1 per bar,
    // then probabilistically add more based on density.
    // Placement varies: backbeat, syncopated, or call-response after kick.

    int numGroups = grouping.getNumGroups();

    // Pick which groups get a snare (guarantee at least one)
    bool groupGetsSnare[4] = {false, false, false, false};
    int guaranteedGroup = 1;  // Default: 2nd group gets the "main" snare
    if (numGroups <= 1) guaranteedGroup = 0;
    else if (numGroups >= 3) {
        // Prefer group 1 or 2 (the "middle" of the bar) for the main backbeat
        guaranteedGroup = rng.bernoulli(0.6f) ? 1 : (rng.bernoulli(0.5f) ? 2 : 0);
        if (guaranteedGroup >= numGroups) guaranteedGroup = 1;
    }
    groupGetsSnare[guaranteedGroup] = true;

    // Additional snares: probability scales with density, but kept sparse
    for (int g = 0; g < numGroups; g++) {
        if (g == guaranteedGroup) continue;
        float prob = 0.15f + 0.35f * density;
        // Longer groups more likely to get a snare
        if (grouping.getGroupLength(g) >= 3) prob += 0.1f;
        if (rng.bernoulli(prob)) {
            groupGetsSnare[g] = true;
        }
    }

    // Place snares for selected groups
    float phaseCursor = 0.0f;
    for (int g = 0; g < numGroups; g++) {
        int len = grouping.getGroupLength(g);
        float groupPhaseLen = static_cast<float>(len) / static_cast<float>(beats);

        if (groupGetsSnare[g]) {
            // Choose placement strategy (varies per group for unpredictability)
            float stratRoll = rng.uniform();
            int snareGridIdx;

            if (stratRoll < 0.45f) {
                // Strategy A (45%): Classic backbeat — middle of group
                int backbeatOffset = (len >= 3) ? (len / 2) : 1;
                float snarePhase = phaseCursor + static_cast<float>(backbeatOffset) / static_cast<float>(beats);
                snareGridIdx = static_cast<int>(snarePhase * static_cast<float>(steps) + 0.5f) % steps;
            } else if (stratRoll < 0.70f) {
                // Strategy B (25%): Syncopated — "and" of the first beat in group
                float snarePhase = phaseCursor + 0.5f / static_cast<float>(beats);  // half a beat in
                snareGridIdx = static_cast<int>(snarePhase * static_cast<float>(steps) + 0.5f) % steps;
            } else if (stratRoll < 0.85f) {
                // Strategy C (15%): Call-response — 1 subdivision after a kick
                // Find nearest kick in this group's range and place snare after it
                int groupStartIdx = static_cast<int>(phaseCursor * static_cast<float>(steps) + 0.5f) % steps;
                int groupEndIdx = static_cast<int>((phaseCursor + groupPhaseLen) * static_cast<float>(steps) + 0.5f) % steps;
                int bestKickIdx = -1;
                float bestKickE = 0.0f;
                for (int k = 0; k < steps; k++) {
                    // Check if k is within this group's range
                    bool inRange = (groupEndIdx > groupStartIdx)
                        ? (k >= groupStartIdx && k < groupEndIdx)
                        : (k >= groupStartIdx || k < groupEndIdx);
                    if (inRange && kickField.energy[k] > bestKickE) {
                        bestKickE = kickField.energy[k];
                        bestKickIdx = k;
                    }
                }
                if (bestKickIdx >= 0) {
                    snareGridIdx = (bestKickIdx + 1) % steps;  // 1 subdivision after kick
                } else {
                    // Fallback to backbeat
                    int backbeatOffset = (len >= 3) ? (len / 2) : 1;
                    float snarePhase = phaseCursor + static_cast<float>(backbeatOffset) / static_cast<float>(beats);
                    snareGridIdx = static_cast<int>(snarePhase * static_cast<float>(steps) + 0.5f) % steps;
                }
            } else {
                // Strategy D (15%): Pushed — one 16th before the next group downbeat
                float snarePhase = phaseCursor + groupPhaseLen - (1.0f / static_cast<float>(steps));
                snareGridIdx = static_cast<int>(snarePhase * static_cast<float>(steps) + 0.5f) % steps;
            }

            // Anti-coincidence: don't place snare exactly on a strong kick
            if (kickField.energy[snareGridIdx] > 0.6f) {
                // Shift snare by 1 subdivision forward
                snareGridIdx = (snareGridIdx + 1) % steps;
            }

            // Vary velocity: guaranteed snare is louder, extras are softer
            float strength;
            if (g == guaranteedGroup) {
                strength = 0.85f + 0.15f * rng.uniform();  // strong
            } else {
                strength = 0.45f + 0.30f * rng.uniform();  // softer, more varied
            }
            fields.primary.energy[snareGridIdx] += strength;
        }

        phaseCursor += groupPhaseLen;
    }
    fields.primary.normalize();

    // Ghost snares: adjacent to primary snares (1 subdivision before/after)
    for (int i = 0; i < fields.primary.resolution; i++) {
        if (fields.primary.energy[i] > 0.3f) {
            // Try placing ghost 1 step before
            int before = (i - 1 + steps) % steps;
            if (rng.bernoulli(0.25f + 0.35f * chaos * density)) {
                fields.ghost.energy[before] += 0.3f + 0.1f * rng.uniform();
            }
            // Try placing ghost 1 step after
            int after = (i + 1) % steps;
            if (rng.bernoulli(0.15f + 0.25f * chaos * density)) {
                fields.ghost.energy[after] += 0.25f + 0.1f * rng.uniform();
            }
        }
    }
    // Additional sparse ghost fills at random grid positions (chaos-dependent)
    if (chaos > 0.3f) {
        for (int beat = 0; beat < beats; beat++) {
            // Fill positions near the beat (keeps original feel when subdiv=4)
            for (int sub = 1; sub <= subdiv - 1; sub += 2) {
                int idx = beat * subdiv + sub;
                if (idx < steps && fields.primary.energy[idx] < 0.1f
                    && fields.ghost.energy[idx] < 0.1f
                    && rng.bernoulli(0.04f + 0.12f * chaos * density)) {
                    fields.ghost.energy[idx] += 0.2f + 0.15f * rng.uniform();
                }
            }
        }
    }
    fields.ghost.normalize();
}

void EnergyFieldGenerator::generateHatFields(HatFields& fields, const MetricSpec& metric,
                                             float density, float chaos,
                                             const EnergyField& snareField, uint32_t seed) {
    RandomGenerator rng(seed);

    int beats = std::max(1, metric.beatsPerBar);
    int subdiv = std::max(1, metric.subdivPerBeat);
    int steps = std::max(1, std::min(metric.gridStepsPerBar(), EnergyField::MAX_RESOLUTION));
    int andOffset = (subdiv >= 2 && (subdiv % 2 == 0)) ? (subdiv / 2) : 0;

    // Closed hat: grid-based 8th or 16th note pattern on 28-step grid
    fields.closed.resolution = steps;
    fields.closed.reset();

    for (int beat = 0; beat < beats; beat++) {
        // Always place on 8th-note positions (beat downbeat + "and")
        int downIdx = beat * subdiv;
        int andIdx = beat * subdiv + andOffset;

        if (downIdx < steps) fields.closed.energy[downIdx] = 0.7f + 0.2f * rng.uniform();
        if (andIdx < steps) fields.closed.energy[andIdx] = 0.5f + 0.3f * rng.uniform();

        // At higher density, add 16th-note hits ("e" and "a")
        if (density > 0.3f) {
            int eIdx = beat * subdiv + 1;
            int aIdx = beat * subdiv + (subdiv - 1);
            if (eIdx < steps && rng.bernoulli(0.2f + 0.6f * density)) {
                fields.closed.energy[eIdx] = 0.3f + 0.3f * density * rng.uniform();
            }
            if (aIdx < steps && rng.bernoulli(0.15f + 0.5f * density)) {
                fields.closed.energy[aIdx] = 0.25f + 0.25f * density * rng.uniform();
            }
        }
    }

    // Remove closed-hat hits that coincide with snare (let snare breathe)
    for (int i = 0; i < fields.closed.resolution; i++) {
        float sn = snareField.sampleAt(static_cast<float>(i) / fields.closed.resolution);
        if (sn > 0.5f) {
            fields.closed.energy[i] *= (1.0f - sn * 0.7f);
        }
    }
    fields.closed.normalize();

    // Open hat: musical placement on positions NOT occupied by closed hat.
    // Open hats replace closed hats at their position (can't have both).
    fields.open.resolution = steps;
    fields.open.reset();

    // Last-beat downbeat — classic open hat before cycle repeats
    int lastBeatDownIdx = (beats - 1) * subdiv;
    if (lastBeatDownIdx >= 0 && lastBeatDownIdx < steps) {
        fields.open.energy[lastBeatDownIdx] = 0.7f + 0.2f * density;
        fields.closed.energy[lastBeatDownIdx] = 0.0f;
    }

    // At moderate+ density, add open hat at the halfway downbeat (when beats even)
    if ((beats % 2 == 0) && density > 0.3f && rng.bernoulli(0.4f + 0.4f * density)) {
        int halfIdx = (beats / 2) * subdiv;
        if (halfIdx >= 0 && halfIdx < steps) {
            fields.open.energy[halfIdx] = 0.5f + 0.2f * density;
            fields.closed.energy[halfIdx] = 0.0f;
        }
    }

    // Chaos-driven: random open hat on a beat downbeat
    if (chaos > 0.3f) {
        // Pick from beat downbeats (excluding half + last if present)
        int candidateBeats[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        int numCand = std::min(beats, 8);
        int pickBeat = candidateBeats[rng.uniformInt(0, std::max(0, numCand - 1))];
        int pick = pickBeat * subdiv;
        if (rng.bernoulli(0.2f + 0.35f * chaos)) {
            if (pick >= 0 && pick < steps) {
                fields.open.energy[pick] += 0.35f + 0.25f * chaos;
                fields.closed.energy[pick] = 0.0f;
            }
        }
    }

    // Additional density-based open hat on "and" positions (odd 8th notes)
    if (density > 0.6f) {
        for (int beat = 0; beat < beats; beat++) {
            int andIdx = beat * subdiv + andOffset;
            if (andIdx < steps && fields.open.energy[andIdx] < 0.01f
                && rng.bernoulli(0.06f + 0.14f * density * chaos)) {
                fields.open.energy[andIdx] = 0.3f + 0.2f * density;
                fields.closed.energy[andIdx] = 0.0f;
            }
        }
    }

    // Re-normalize closed hat after removing positions
    fields.closed.normalize();
    fields.open.normalize();
}

void EnergyFieldGenerator::generateInterferencePattern(EnergyField& out, const Oscillator* oscillators, int numOsc, int resolution) {
    out.resolution = resolution;
    out.reset();
    for (int i = 0; i < resolution; i++) {
        float phase = static_cast<float>(i) / resolution;
        float sum = 0.0f;
        for (int o = 0; o < numOsc; o++) {
            float tablePhase = wrapPhase(oscillators[o].frequency * phase + oscillators[o].phase);
            sum += g_sineTable.samplePhase(tablePhase) * oscillators[o].amplitude;
        }
        out.energy[i] = std::max(0.0f, sum * 0.5f + 0.5f);
    }
    out.normalize();
}

} // namespace pwmt
