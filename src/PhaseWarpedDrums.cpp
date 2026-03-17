#include "PhaseWarpedDrums.hpp"
#include "plugin.hpp"
#include "ui/PngPanelBackground.hpp"
#include <algorithm>
#include <cmath>

PhaseWarpedDrums::PhaseWarpedDrums() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

    configParam(CHAOS_PARAM, 0.0f, 1.0f, 0.27f, "Chaos", "%", 0.f, 100.f);
    configParam(DENSITY_PARAM, 0.0f, 1.0f, 1.0f, "Density", "%", 0.f, 100.f);
    configParam(EVOLUTION_PARAM, 0.0f, 1.0f, 0.64f, "Evolution", "%", 0.f, 100.f);
    configParam(SWING_PARAM, -1.0f, 1.0f, 0.15f, "Swing", "%", 0.f, 100.f);
    configParam(TENSION_PARAM, 0.0f, 1.0f, 0.51f, "Metric Tension", "%", 0.f, 100.f);
    configButton(GENERATE_BUTTON, "Generate Pattern");
    configSwitch(GROUPING_SELECT, 0.0f, 5.0f, 5.0f, "Metric Grouping", {"3-2-2", "2-2-3", "4-3", "3-4", "2-3-2", "Auto"});
    configSwitch(AUTO_REGEN_PARAM, 0.0f, 2.0f, 2.0f, "Auto Regenerate", {"Manual", "Every Cycle", "Every 4 Cycles"});
    configSwitch(PATTERN_BARS, 0.0f, 2.0f, 2.0f, "Cycle Length", {"1 bar", "2 bars", "4 bars"});
    configSwitch(TIME_MODE_PARAM, 0.0f, 2.0f, 0.0f, "Time Mode", {"7/4", "4/4", "7-over-4"});

    configInput(CHAOS_CV, "Chaos CV");
    configInput(DENSITY_CV, "Density CV");
    configInput(EVOLUTION_CV, "Evolution CV");
    configInput(SWING_CV, "Swing CV");
    configInput(TENSION_CV, "Tension CV");
    configInput(GENERATE_TRIG, "Generate Trigger");
    configInput(RESET_INPUT, "Reset");
    configInput(CLOCK_INPUT, "Clock");

    configOutput(KICK_GATE, "Kick Gate");
    configOutput(KICK_VEL, "Kick Velocity");
    configOutput(SNARE_GATE, "Snare Gate");
    configOutput(SNARE_VEL, "Snare Velocity");
    configOutput(GHOST_GATE, "Ghost Snare Gate");
    configOutput(GHOST_VEL, "Ghost Snare Velocity");
    configOutput(CHAT_GATE, "Closed Hat Gate");
    configOutput(CHAT_VEL, "Closed Hat Velocity");
    configOutput(OHAT_GATE, "Open Hat Gate");
    configOutput(OHAT_VEL, "Open Hat Velocity");
    configOutput(PHASE_OUT, "Bar Phase");
    configOutput(METRIC_ACCENT_OUT, "Metric Accent CV");
    configOutput(GROUP_GATE_1, "Group Gate 1");
    configOutput(GROUP_GATE_2, "Group Gate 2");
    configOutput(GROUP_GATE_3, "Group Gate 3");

    float lambda = 10.0f;
    chaosFilter_.setLambda(lambda);
    densityFilter_.setLambda(lambda);
    evolutionFilter_.setLambda(lambda);
    swingFilter_.setLambda(lambda);
    tensionFilter_.setLambda(lambda);

    // DON'T generate here - wait for first process() call
    // to ensure all systems are initialized

    // All trigger buffers and fields are fixed-size, no heap allocation needed
}

void PhaseWarpedDrums::onReset() {
    barPhase_ = 0.0f;
    kickState_ = {};
    snareState_ = {};
    ghostState_ = {};
    chatState_ = {};
    ohatState_ = {};
    memory_.reset();
    generateNewPattern();
}

json_t* PhaseWarpedDrums::dataToJson() {
    json_t* rootJ = json_object();
    json_object_set_new(rootJ, "tempo", json_real(tempo_));
#ifdef METAMODULE
    json_object_set_new(rootJ, "morphworx_version", json_string(MORPHWORX_VERSION_STRING));
#endif
    return rootJ;
}

void PhaseWarpedDrums::dataFromJson(json_t* rootJ) {
#ifdef METAMODULE
    json_t* verJ = json_object_get(rootJ, "morphworx_version");
    if (verJ && json_is_string(verJ)) {
        const char* saved = json_string_value(verJ);
        if (!saved || std::string(saved) != std::string(MORPHWORX_VERSION_STRING)) {
            return;
        }
    }
#endif
    json_t* tempoJ = json_object_get(rootJ, "tempo");
    if (tempoJ) tempo_ = json_number_value(tempoJ);
}

float PhaseWarpedDrums::getEffectiveParam(int paramId, int cvId, float scale) {
    float knobValue = params[paramId].getValue();
    float cvValue = inputs[cvId].isConnected() ? inputs[cvId].getVoltage() / 10.0f * scale : 0.0f;
    return math::clamp(knobValue + cvValue, -1.0f, 1.0f);
}

// Synchronous generation — only used for initial pattern and reset
void PhaseWarpedDrums::generateNewPattern() {
    startGeneration();
    while (genActive_) stepGeneration();
}

// Begin amortized generation (sets flag, actual work in stepGeneration)
void PhaseWarpedDrums::startGeneration() {
    genActive_ = true;
    genStep_ = 0;
}

// Execute one step of generation per process() call
// Step 0: setup params/thresholds/clear buffers
// Steps 1..N: one bar each (generate fields, warp, extract)
// Step N+1: finalize (sort, ratchets, swing, reset states)
void PhaseWarpedDrums::stepGeneration() {
    if (genStep_ == 0) {
        // ===== SETUP =====
        genCtx_.chaos = getEffectiveParam(CHAOS_PARAM, CHAOS_CV);
        genCtx_.density = getEffectiveParam(DENSITY_PARAM, DENSITY_CV);
        float evolution = getEffectiveParam(EVOLUTION_PARAM, EVOLUTION_CV);
        float tension = getEffectiveParam(TENSION_PARAM, TENSION_CV);
        genCtx_.swing = getEffectiveParam(SWING_PARAM, SWING_CV);

        int timeMode = static_cast<int>(params[TIME_MODE_PARAM].getValue());
        if (timeMode < 0) timeMode = 0;
        if (timeMode > 2) timeMode = 2;

        // Base metric always drives generation/transport.
        // Overlay metric drives accent + group gate outputs.
        if (timeMode == 0) {
            baseMetric_.beatsPerBar = 7;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_ = baseMetric_;
        } else if (timeMode == 1) {
            baseMetric_.beatsPerBar = 4;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_ = baseMetric_;
        } else {
            baseMetric_.beatsPerBar = 4;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_.beatsPerBar = 7;
            overlayMetric_.subdivPerBeat = 4;
        }
        genCtx_.metric = baseMetric_;

        static const int barsLookup[] = {1, 2, 4};
        int patIdx = static_cast<int>(params[PATTERN_BARS].getValue());
        if (patIdx < 0) patIdx = 0;
        if (patIdx > 2) patIdx = 2;
        genTotalBars_ = barsLookup[patIdx];

        pwmt::EvolutionConfig evolCfg{genCtx_.chaos, evolution, 0.7f};
        genCtx_.pattern = memory_.generateNextPattern(evolCfg);

        auto selectSeptagonGrouping7 = [&]() -> pwmt::MetricGrouping {
            int groupingSel = static_cast<int>(params[GROUPING_SELECT].getValue());
            if (groupingSel >= 0 && groupingSel < pwmt::NUM_STANDARD_GROUPINGS) {
                return pwmt::STANDARD_GROUPINGS[groupingSel];
            }
            return genCtx_.pattern.grouping;
        };

        if (timeMode == 0) {
            // Classic Septagon: 7/4 for everything
            baseGrouping_ = selectSeptagonGrouping7();
            overlayGrouping_ = baseGrouping_;
        } else {
            // 4/4 base transport: keep base grouping stable and simple
            baseGrouping_ = pwmt::MetricGrouping(4, {2, 2});
            if (timeMode == 2) {
                // Overlay accents/gates follow Septagon grouping
                overlayGrouping_ = selectSeptagonGrouping7();
            } else {
                overlayGrouping_ = baseGrouping_;
            }
        }

        accentCurve_ = metricEngine_.generateAccentCurve(overlayGrouping_, overlayMetric_);
        float tensionScale = 0.4f + 0.6f * tension;
        if (tensionScale < 0.1f) tensionScale = 0.1f;
        if (tensionScale > 1.2f) tensionScale = 1.2f;
        for (int i = 0; i < accentCurve_.resolution; i++) {
            accentCurve_.accents[i] *= tensionScale;
        }

        kickTriggers_.clear();
        snareTriggers_.clear();
        ghostTriggers_.clear();
        chatTriggers_.clear();
        ohatTriggers_.clear();

        genCtx_.enableSnares = (genCtx_.density > 0.05f) && (genCtx_.density + genCtx_.chaos > 0.15f);
        genCtx_.enableGhosts = genCtx_.enableSnares && (genCtx_.chaos > 0.1f) && (genCtx_.density > 0.2f);

        auto scaleThreshold = [&](pwmt::ExtractionConfig cfg) -> pwmt::ExtractionConfig {
            cfg.threshold *= (1.5f - genCtx_.density);
            if (cfg.threshold < 0.1f) cfg.threshold = 0.1f;
            if (cfg.threshold > 0.9f) cfg.threshold = 0.9f;
            cfg.minTimeBetween *= (1.2f - 0.4f * genCtx_.density);
            // Scale grid quantization by inverse chaos: low chaos = tight grid
            cfg.quantizeStrength *= (1.0f - genCtx_.chaos * 0.7f);
            return cfg;
        };

        genCtx_.scaledKick = scaleThreshold(pwmt::ExtractionPresets::kick());
        genCtx_.scaledSnarePri = scaleThreshold(pwmt::ExtractionPresets::snarePrimary());
        genCtx_.scaledSnareGho = scaleThreshold(pwmt::ExtractionPresets::snareGhost());
        genCtx_.scaledHatClosed = scaleThreshold(pwmt::ExtractionPresets::hatClosed());
        genCtx_.scaledHatOpen = scaleThreshold(pwmt::ExtractionPresets::hatOpen());

        genCtx_.barFraction = 1.0f / static_cast<float>(genTotalBars_);
        genStep_ = 1;

    } else if (genStep_ <= genTotalBars_) {
        // ===== PER-BAR (one bar per frame) =====
        int bar = genStep_ - 1;
        float chaos = genCtx_.chaos;
        float density = genCtx_.density;

        uint32_t barSeed = genCtx_.pattern.seed + static_cast<uint32_t>(bar * 1000);
        float barChaos = chaos + (bar > 0 ? (static_cast<float>(bar) * 0.05f) : 0.0f);
        if (barChaos > 1.0f) barChaos = 1.0f;

        pwmt::WarpConfig warpCfg{genCtx_.pattern.warpMacroAmount * barChaos * 0.5f,
                                 genCtx_.pattern.warpMicroAmount * barChaos * 0.5f,
                                 (barChaos - 0.5f) * 0.1f, barSeed};
        warper_.generateWarpFunction(warpFunction_, baseGrouping_, genCtx_.metric, warpCfg);

        fieldGenerator_.generateKickField(scratchKick_, baseGrouping_, genCtx_.metric, barChaos, density, barSeed);
        if (genCtx_.enableSnares) {
            fieldGenerator_.generateSnareFields(scratchSnare_, baseGrouping_, scratchKick_, genCtx_.metric, barChaos, density, barSeed + 1);
        }
        fieldGenerator_.generateHatFields(scratchHat_, genCtx_.metric, density, barChaos,
                                          genCtx_.enableSnares ? scratchSnare_.primary : scratchKick_, barSeed + 2);

        warper_.applyWarpInPlace(scratchKick_, warpFunction_);
        if (genCtx_.enableSnares) {
            warper_.applyWarpInPlace(scratchSnare_.primary, warpFunction_);
            if (genCtx_.enableGhosts) {
                warper_.applyWarpInPlace(scratchSnare_.ghost, warpFunction_);
            }
        }
        warper_.applyWarpInPlace(scratchHat_.closed, warpFunction_);
        warper_.applyWarpInPlace(scratchHat_.open, warpFunction_);

        float barStart = bar * genCtx_.barFraction;

        extractor_.extractInto(scratchTrigBuf_, scratchKick_, genCtx_.scaledKick, genCtx_.metric, &accentCurve_, barSeed + 100);
        for (int i = 0; i < scratchTrigBuf_.count; i++) {
            kickTriggers_.push(barStart + scratchTrigBuf_[i].phase * genCtx_.barFraction, scratchTrigBuf_[i].velocity);
        }

        if (genCtx_.enableSnares) {
            extractor_.extractInto(scratchTrigBuf_, scratchSnare_.primary, genCtx_.scaledSnarePri, genCtx_.metric, &accentCurve_, barSeed + 101);
            for (int i = 0; i < scratchTrigBuf_.count; i++) {
                snareTriggers_.push(barStart + scratchTrigBuf_[i].phase * genCtx_.barFraction, scratchTrigBuf_[i].velocity);
            }
        }
        if (genCtx_.enableGhosts) {
            extractor_.extractInto(scratchTrigBuf_, scratchSnare_.ghost, genCtx_.scaledSnareGho, genCtx_.metric, nullptr, barSeed + 102);
            for (int i = 0; i < scratchTrigBuf_.count; i++) {
                ghostTriggers_.push(barStart + scratchTrigBuf_[i].phase * genCtx_.barFraction, scratchTrigBuf_[i].velocity);
            }
        }

        extractor_.extractInto(scratchTrigBuf_, scratchHat_.closed, genCtx_.scaledHatClosed, genCtx_.metric, &accentCurve_, barSeed + 103);
        for (int i = 0; i < scratchTrigBuf_.count; i++) {
            chatTriggers_.push(barStart + scratchTrigBuf_[i].phase * genCtx_.barFraction, scratchTrigBuf_[i].velocity);
        }

        extractor_.extractInto(scratchTrigBuf_, scratchHat_.open, genCtx_.scaledHatOpen, genCtx_.metric, nullptr, barSeed + 104);
        for (int i = 0; i < scratchTrigBuf_.count; i++) {
            ohatTriggers_.push(barStart + scratchTrigBuf_[i].phase * genCtx_.barFraction, scratchTrigBuf_[i].velocity);
        }

        kickField_.copyFrom(scratchKick_);
        genStep_++;

    } else {
        // ===== FINALIZE =====
        kickTriggers_.sortByPhase();
        snareTriggers_.sortByPhase();
        ghostTriggers_.sortByPhase();
        chatTriggers_.sortByPhase();
        ohatTriggers_.sortByPhase();

        if (genCtx_.chaos > 0.6f) {
            extractor_.addRatchets(snareTriggers_, genCtx_.chaos, genCtx_.density, genCtx_.metric, genCtx_.pattern.seed + 200);
            extractor_.addRatchets(ghostTriggers_, genCtx_.chaos, genCtx_.density, genCtx_.metric, genCtx_.pattern.seed + 201);
            extractor_.addRatchets(chatTriggers_, genCtx_.chaos, genCtx_.density, genCtx_.metric, genCtx_.pattern.seed + 202);
        }

        // Hat choke: remove open hats that coincide with closed hats
        {
            int steps = std::max(1, genCtx_.metric.gridStepsPerBar());
            float halfCycleGridStep = 0.5f / (static_cast<float>(std::max(1, genTotalBars_)) * static_cast<float>(steps));
            pwmt::TriggerExtractor::applyHatChoke(chatTriggers_, ohatTriggers_, halfCycleGridStep);
        }

        if (std::abs(genCtx_.swing) > 0.01f) {
            extractor_.applySwing(kickTriggers_, genCtx_.swing, genCtx_.metric);
            extractor_.applySwing(snareTriggers_, genCtx_.swing, genCtx_.metric);
            extractor_.applySwing(ghostTriggers_, genCtx_.swing, genCtx_.metric);
            extractor_.applySwing(chatTriggers_, genCtx_.swing, genCtx_.metric);
            extractor_.applySwing(ohatTriggers_, genCtx_.swing, genCtx_.metric);
        }

        kickState_ = {};
        snareState_ = {};
        ghostState_ = {};
        chatState_ = {};
        ohatState_ = {};

        genActive_ = false;
    }
}

std::pair<float, float> PhaseWarpedDrums::processTriggerState(TriggerState& state,
                                                              const pwmt::TriggerBuffer& triggers,
                                                              float currentPhase,
                                                              float sampleTime) {
    float gate = 0.0f;
    float vel = state.lastVelocityV;

    if (state.gateTimer > 0.0f) {
        state.gateTimer -= sampleTime;
        if (state.gateTimer > 0.0f) {
            gate = 10.0f;
        }
    }

    if (state.nextTriggerIdx < triggers.count) {
        const auto& t = triggers[state.nextTriggerIdx];
        if (currentPhase >= t.phase) {
            state.gateTimer = TriggerState::GATE_DURATION;
            gate = 10.0f;
            state.lastVelocityV = t.velocity * 10.0f;
            vel = state.lastVelocityV;
            state.nextTriggerIdx++;
        }
    }

    return {gate, vel};
}

void PhaseWarpedDrums::processTriggers(float sampleTime) {
    auto kv = processTriggerState(kickState_, kickTriggers_, barPhase_, sampleTime);
    kickGateV_ = kv.first; kickVelV_ = kv.second;
    auto sv = processTriggerState(snareState_, snareTriggers_, barPhase_, sampleTime);
    snareGateV_ = sv.first; snareVelV_ = sv.second;
    auto gv = processTriggerState(ghostState_, ghostTriggers_, barPhase_, sampleTime);
    ghostGateV_ = gv.first; ghostVelV_ = gv.second;
    auto cv = processTriggerState(chatState_, chatTriggers_, barPhase_, sampleTime);
    chatGateV_ = cv.first; chatVelV_ = cv.second;
    auto ov = processTriggerState(ohatState_, ohatTriggers_, barPhase_, sampleTime);
    ohatGateV_ = ov.first; ohatVelV_ = ov.second;
}

void PhaseWarpedDrums::updateGroupGates() {
    groupGateV_[0] = groupGateV_[1] = groupGateV_[2] = 0.0f;

    // Determine how many bars per cycle
    static const int barsLookup[] = {1, 2, 4};
    int patIdx = static_cast<int>(params[PATTERN_BARS].getValue());
    if (patIdx < 0) patIdx = 0;
    if (patIdx > 2) patIdx = 2;
    int numBars = barsLookup[patIdx];
    float barFraction = 1.0f / static_cast<float>(numBars);

    // Find which bar we're in and our position within that bar
    // Fast fmod replacement: barPhase_ is always [0,1), barFraction is 1/1, 1/2, or 1/4
    float localPhase = barPhase_ / barFraction;
    localPhase -= static_cast<float>(static_cast<int>(localPhase));
    if (localPhase < 0.0f) localPhase += 1.0f;

    float cursor = 0.0f;
    for (int g = 0; g < std::min(3, overlayGrouping_.getNumGroups()); g++) {
        float start = cursor;
        float end = pwmt::wrapPhase(start + 0.02f);
        bool active = false;
        if (end > start) {
            active = (localPhase >= start && localPhase < end);
        } else {
            active = (localPhase >= start || localPhase < end);
        }
        groupGateV_[g] = active ? 10.0f : 0.0f;
        cursor += static_cast<float>(overlayGrouping_.getGroupLength(g)) / static_cast<float>(std::max(1, overlayMetric_.beatsPerBar));
    }
}

void PhaseWarpedDrums::updateOutputs() {
    outputs[KICK_GATE].setVoltage(kickGateV_);
    outputs[KICK_VEL].setVoltage(kickVelV_);
    outputs[SNARE_GATE].setVoltage(snareGateV_);
    outputs[SNARE_VEL].setVoltage(snareVelV_);
    outputs[GHOST_GATE].setVoltage(ghostGateV_);
    outputs[GHOST_VEL].setVoltage(ghostVelV_);
    outputs[CHAT_GATE].setVoltage(chatGateV_);
    outputs[CHAT_VEL].setVoltage(chatVelV_);
    outputs[OHAT_GATE].setVoltage(ohatGateV_);
    outputs[OHAT_VEL].setVoltage(ohatVelV_);

    outputs[PHASE_OUT].setVoltage(barPhase_ * 10.0f);
    outputs[METRIC_ACCENT_OUT].setVoltage(accentCurve_.sampleAt(barPhase_) * 10.0f);
    outputs[GROUP_GATE_1].setVoltage(groupGateV_[0]);
    outputs[GROUP_GATE_2].setVoltage(groupGateV_[1]);
    outputs[GROUP_GATE_3].setVoltage(groupGateV_[2]);
}

void PhaseWarpedDrums::updateLights(float sampleTime) {
    lights[KICK_LIGHT].setBrightnessSmooth(kickGateV_ > 5.0f ? 1.0f : 0.0f, sampleTime);
    lights[SNARE_LIGHT].setBrightnessSmooth(snareGateV_ > 5.0f ? 1.0f : 0.0f, sampleTime);
    lights[GHOST_LIGHT].setBrightnessSmooth(ghostGateV_ > 5.0f ? 1.0f : 0.0f, sampleTime);
    lights[CHAT_LIGHT].setBrightnessSmooth(chatGateV_ > 5.0f ? 1.0f : 0.0f, sampleTime);
    lights[OHAT_LIGHT].setBrightnessSmooth(ohatGateV_ > 5.0f ? 1.0f : 0.0f, sampleTime);
    lights[GENERATE_LIGHT].setBrightnessSmooth(params[GENERATE_BUTTON].getValue() > 0.5f ? 1.0f : 0.0f, sampleTime);
    lights[PHASE_LIGHT].setBrightness(barPhase_);
}

void PhaseWarpedDrums::updatePhase(float sampleTime) {
    // Pattern bars multiplier: 0=1bar, 1=2bars, 2=4bars
    static const int barsLookup[] = {1, 2, 4};
    int patIdx = static_cast<int>(params[PATTERN_BARS].getValue());
    if (patIdx < 0) patIdx = 0;
    if (patIdx > 2) patIdx = 2;
    int totalBeats = std::max(1, baseMetric_.beatsPerBar) * barsLookup[patIdx];

    useExternalClock_ = inputs[CLOCK_INPUT].isConnected();
    if (useExternalClock_) {
        if (clockTrigger_.process(inputs[CLOCK_INPUT].getVoltage())) {
            barPhase_ += 1.0f / static_cast<float>(totalBeats);
        }
    }
    // No clock connected = no phase advance (module waits for external clock)
    if (barPhase_ >= 1.0f) barPhase_ -= 1.0f;
}

void PhaseWarpedDrums::process(const ProcessArgs& args) {
    int timeMode = static_cast<int>(params[TIME_MODE_PARAM].getValue());
    if (timeMode < 0) timeMode = 0;
    if (timeMode > 2) timeMode = 2;

    auto applyTimeMode = [&](int mode) {
        if (mode == 0) {
            baseMetric_.beatsPerBar = 7;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_ = baseMetric_;
        } else if (mode == 1) {
            baseMetric_.beatsPerBar = 4;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_ = baseMetric_;
        } else {
            baseMetric_.beatsPerBar = 4;
            baseMetric_.subdivPerBeat = 4;
            overlayMetric_.beatsPerBar = 7;
            overlayMetric_.subdivPerBeat = 4;
        }
    };

    // Generate initial pattern on first process() call (synchronous, OK once)
    if (!hasGeneratedInitialPattern_) {
        applyTimeMode(timeMode);
        lastTimeMode_ = timeMode;
        generateNewPattern();
        hasGeneratedInitialPattern_ = true;
    }

    // Time mode change triggers immediate regen (no cooldown gate)
    if (timeMode != lastTimeMode_) {
        applyTimeMode(timeMode);
        lastTimeMode_ = timeMode;

        barPhase_ = 0.0f;
        barCounter_ = 0;
        kickState_ = {};
        snareState_ = {};
        ghostState_ = {};
        chatState_ = {};
        ohatState_ = {};

        // Provide sane interim groupings until the new generation finishes.
        if (timeMode == 0) {
            baseGrouping_ = pwmt::MetricGrouping();
            overlayGrouping_ = baseGrouping_;
        } else {
            baseGrouping_ = pwmt::MetricGrouping(4, {2, 2});
            overlayGrouping_ = (timeMode == 2) ? pwmt::MetricGrouping() : baseGrouping_;
        }

        startGeneration();
        genCooldownTimer_ = GEN_COOLDOWN_SECS;
    }

    // Tick cooldown timer
    if (genCooldownTimer_ > 0.0f) {
        genCooldownTimer_ -= args.sampleTime;
    }

    // Track previous phase for bar wrap detection
    float prevPhase = barPhase_;

    // Manual triggers (with cooldown) — start amortized generation
    if (generateTrigger_.process(params[GENERATE_BUTTON].getValue() + inputs[GENERATE_TRIG].getVoltage())) {
        if (genCooldownTimer_ <= 0.0f) {
            startGeneration();
            genCooldownTimer_ = GEN_COOLDOWN_SECS;
        }
    }

    if (resetTrigger_.process(inputs[RESET_INPUT].getVoltage())) {
        barPhase_ = 0.0f;
        kickState_ = {};
        snareState_ = {};
        ghostState_ = {};
        chatState_ = {};
        ohatState_ = {};
    }

    updatePhase(args.sampleTime);

    // Detect bar wrap
    if (barPhase_ < prevPhase) {
        // Reset trigger indices on bar wrap but PRESERVE velocity S&H
        kickState_.nextTriggerIdx = 0;
        kickState_.gateTimer = 0.0f;
        snareState_.nextTriggerIdx = 0;
        snareState_.gateTimer = 0.0f;
        ghostState_.nextTriggerIdx = 0;
        ghostState_.gateTimer = 0.0f;
        chatState_.nextTriggerIdx = 0;
        chatState_.gateTimer = 0.0f;
        ohatState_.nextTriggerIdx = 0;
        ohatState_.gateTimer = 0.0f;

        barCounter_++;
        int regenMode = static_cast<int>(params[AUTO_REGEN_PARAM].getValue());
        if (genCooldownTimer_ <= 0.0f) {
            if (regenMode == 1) {
                startGeneration();
                genCooldownTimer_ = GEN_COOLDOWN_SECS;
            } else if (regenMode == 2 && (barCounter_ % 4 == 0)) {
                startGeneration();
                genCooldownTimer_ = GEN_COOLDOWN_SECS;
            }
        }
    }

    // Advance amortized generation — one step per sample, no spikes
    if (genActive_) {
        stepGeneration();
    }

    processTriggers(args.sampleTime);
    updateGroupGates();
    updateOutputs();
    updateLights(args.sampleTime);
}

// -----------------------------------------------------------------------------
// Widget
// -----------------------------------------------------------------------------

#ifndef METAMODULE
struct PWLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    bool isTitle;
    PWLabel(Vec pos, const char* t, float s, NVGcolor c, bool title = false) {
        box.pos = pos;
        box.size = Vec(90, s + 4);
        text = t; fontSize = s; color = c; isTitle = title;
    }
    void draw(const DrawArgs& args) override {
        std::string fontPath = isTitle ? asset::plugin(pluginInstance, "res/CinzelDecorative-Bold.ttf")
                                       : asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
        auto font = APP->window->loadFont(fontPath);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        float x = 0.0f;
        float y = fontSize / 2.f;
        nvgText(args.vg, x, y, text.c_str(), NULL);
        if (isTitle) {
            // Thicken headline without changing colors/fonts.
            nvgText(args.vg, x + 0.7f, y, text.c_str(), NULL);
        }
    }
};

static PWLabel* pwCreateLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, bool title = false) {
    return new PWLabel(mm2px(mmPos), text, fontSize, color, title);
}
#endif

struct PhaseWarpedDrumsWidget : ModuleWidget {
    PhaseWarpedDrumsWidget(PhaseWarpedDrums* module) {
        setModule(module);
        auto* panel = createPanel(asset::plugin(pluginInstance,
#ifdef METAMODULE
            // MetaModule: use a Septagon-named panel so faceplate lookup can resolve Septagon.png.
            "res/Septagon.svg"
#else
            "res/PhaseWarpedDrums.svg"
#endif
        ));
        setPanel(panel);
        // Ensure panel width is actually 24HP (Rack can cache SVG sizes).
        box.size = Vec(RACK_GRID_WIDTH * 24, RACK_GRID_HEIGHT);
        if (panel) {
            panel->box.size = box.size;
            // Faceplate art is now PNG; keep SVG only for sizing.
            panel->visible = false;
        }

#ifndef METAMODULE
        // PNG faceplate background (drawn on top of the hidden SVG panel).
        auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Septagon.png"));
        panelBg->box.pos = Vec(0, 0);
        panelBg->box.size = box.size;
        addChild(panelBg);

        struct SeptagonPort : MVXPort {
            SeptagonPort() {
                imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
                imageHandle = -1;
            }
        };
#else
        using SeptagonPort = MVXPort;
#endif

        // Used to nudge some VCV-only UI elements down.
        // Must exist in MetaModule builds as well (as a no-op) since some placements
        // are shared.
        Vec topShiftPx = Vec(0.f, 0.f);

#ifndef METAMODULE
    NVGcolor neon = nvgRGB(0xff, 0xff, 0xff);
    NVGcolor dim = nvgRGB(0xff, 0xff, 0xff);
        topShiftPx = Vec(0.f, 20.f);
        // Use a separate header offset to avoid ambiguity.
        const Vec headerShiftPx = Vec(0.f, 60.f);
        
        // Section headers (2x larger)
    {
        auto* patternLbl = pwCreateLabel(Vec(30.5f, 18.f), "PATTERN", 16.f, dim);
        patternLbl->box.pos = patternLbl->box.pos.plus(headerShiftPx);
        addChild(patternLbl);
        auto* rhythmLbl = pwCreateLabel(Vec(91.5f, 18.f), "RHYTHM", 16.f, dim);
        rhythmLbl->box.pos = rhythmLbl->box.pos.plus(headerShiftPx);
        addChild(rhythmLbl);
    }
        
        // Knob labels (moved up 2px)
        {
            auto* l = pwCreateLabel(Vec(15.f, 28.f), "CHAOS", 8.5f, neon);
            l->box.pos = l->box.pos.plus(topShiftPx);
            addChild(l);
        }
        {
            auto* l = pwCreateLabel(Vec(46.f, 28.f), "DENSITY", 8.5f, neon);
            l->box.pos = l->box.pos.plus(topShiftPx);
            addChild(l);
        }
        {
            auto* l = pwCreateLabel(Vec(76.f, 28.f), "SWING", 8.5f, neon);
            l->box.pos = l->box.pos.plus(topShiftPx);
            addChild(l);
        }
        {
            auto* l = pwCreateLabel(Vec(107.f, 28.f), "TENSION", 8.5f, neon);
            l->box.pos = l->box.pos.plus(topShiftPx);
            addChild(l);
        }
        
        addChild(pwCreateLabel(Vec(15.f, 54.f), "EVOLVE", 8.5f, neon));
        addChild(pwCreateLabel(Vec(46.f, 54.f), "GROUPING", 8.5f, neon));
        addChild(pwCreateLabel(Vec(60.96f, 57.f), "GEN", 7.5f, dim));
        
        addChild(pwCreateLabel(Vec(76.f, 71.f), "PATTERN LENGTH", 7.5f, dim));
        addChild(pwCreateLabel(Vec(107.f, 71.f), "REGEN", 7.5f, dim));
        addChild(pwCreateLabel(Vec(15.f, 71.f), "TIME MODE", 7.5f, dim));
        
        // CV section header is baked into the faceplate PNG.
        addChild(pwCreateLabel(Vec(15.f, 88.f), "CHAOS", 6.5f, dim));
        addChild(pwCreateLabel(Vec(30.5f, 88.f), "DENS", 6.5f, dim));
        addChild(pwCreateLabel(Vec(46.f, 88.f), "EVOL", 6.5f, dim));
        addChild(pwCreateLabel(Vec(61.5f, 88.f), "SWING", 6.5f, dim));
        addChild(pwCreateLabel(Vec(76.f, 88.f), "TENS", 6.5f, dim));
        addChild(pwCreateLabel(Vec(91.5f, 88.f), "GEN", 6.5f, dim));
        addChild(pwCreateLabel(Vec(107.f, 88.f), "RESET", 6.5f, dim));
        
        // Clock (moved up 1px)
        addChild(pwCreateLabel(Vec(60.96f, 100.f), "CLOCK", 7.f, dim));
        
        // Outputs section header is baked into the faceplate PNG.
        addChild(pwCreateLabel(Vec(15.f, 115.f), "KICK", 7.f, neon));
        addChild(pwCreateLabel(Vec(36.5f, 115.f), "SNARE", 7.f, neon));
        addChild(pwCreateLabel(Vec(58.f, 115.f), "GHOST", 7.f, neon));
        addChild(pwCreateLabel(Vec(79.5f, 115.f), "C.HAT", 7.f, neon));
        addChild(pwCreateLabel(Vec(101.f, 115.f), "O.HAT", 7.f, neon));
        
        // Utility outputs
        addChild(pwCreateLabel(Vec(60.96f, 138.f), "UTILITY", 7.f, dim));
        addChild(pwCreateLabel(Vec(15.f, 147.f), "GRP1", 6.f, dim));
        addChild(pwCreateLabel(Vec(30.5f, 147.f), "GRP2", 6.f, dim));
        addChild(pwCreateLabel(Vec(46.f, 147.f), "GRP3", 6.f, dim));
        addChild(pwCreateLabel(Vec(76.f, 147.f), "PHASE", 6.f, dim));
        addChild(pwCreateLabel(Vec(107.f, 147.f), "ACCENT", 6.f, dim));
#endif

        // Pattern parameter knobs (top row)
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15, 38)).plus(topShiftPx), module, PhaseWarpedDrums::CHAOS_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(46, 38)).plus(topShiftPx), module, PhaseWarpedDrums::DENSITY_PARAM));
        
        // Rhythm controls (top row right side)
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(76, 38)).plus(topShiftPx), module, PhaseWarpedDrums::SWING_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(107, 38)).plus(topShiftPx), module, PhaseWarpedDrums::TENSION_PARAM));
        
        // Second row
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15, 64)), module, PhaseWarpedDrums::EVOLUTION_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(46, 64)), module, PhaseWarpedDrums::GROUPING_SELECT));
        
        // Generate button (center)
        addParam(createParamCentered<TL1105>(mm2px(Vec(60.96, 64)), module, PhaseWarpedDrums::GENERATE_BUTTON));
        
        // Switches
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(15, 78)), module, PhaseWarpedDrums::TIME_MODE_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(76, 78)), module, PhaseWarpedDrums::PATTERN_BARS));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(107, 78)), module, PhaseWarpedDrums::AUTO_REGEN_PARAM));

        // CV Inputs row
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(15, 95)), module, PhaseWarpedDrums::CHAOS_CV));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(30.5, 95)), module, PhaseWarpedDrums::DENSITY_CV));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(46, 95)), module, PhaseWarpedDrums::EVOLUTION_CV));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(61.5, 95)), module, PhaseWarpedDrums::SWING_CV));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(76, 95)), module, PhaseWarpedDrums::TENSION_CV));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(91.5, 95)), module, PhaseWarpedDrums::GENERATE_TRIG));
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(107, 95)), module, PhaseWarpedDrums::RESET_INPUT));

        // Clock input (center)
        addInput(createInputCentered<SeptagonPort>(mm2px(Vec(60.96, 106)), module, PhaseWarpedDrums::CLOCK_INPUT));

        // Drum outputs (gate + velocity pairs)
        float outY1 = 122;
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(11, outY1)), module, PhaseWarpedDrums::KICK_GATE));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(19, outY1)), module, PhaseWarpedDrums::KICK_VEL));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(15, outY1 - 3.5)), module, PhaseWarpedDrums::KICK_LIGHT));
        
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(33, outY1)), module, PhaseWarpedDrums::SNARE_GATE));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(40, outY1)), module, PhaseWarpedDrums::SNARE_VEL));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(36.5, outY1 - 3.5)), module, PhaseWarpedDrums::SNARE_LIGHT));
        
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(54.5, outY1)), module, PhaseWarpedDrums::GHOST_GATE));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(61.5, outY1)), module, PhaseWarpedDrums::GHOST_VEL));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(58, outY1 - 3.5)), module, PhaseWarpedDrums::GHOST_LIGHT));
        
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(76, outY1)), module, PhaseWarpedDrums::CHAT_GATE));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(83, outY1)), module, PhaseWarpedDrums::CHAT_VEL));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(79.5, outY1 - 3.5)), module, PhaseWarpedDrums::CHAT_LIGHT));
        
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(97.5, outY1)), module, PhaseWarpedDrums::OHAT_GATE));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(104.5, outY1)), module, PhaseWarpedDrums::OHAT_VEL));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(101, outY1 - 3.5)), module, PhaseWarpedDrums::OHAT_LIGHT));

        // Utility outputs at bottom
        float outY2 = 152;
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(15, outY2)), module, PhaseWarpedDrums::GROUP_GATE_1));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(30.5, outY2)), module, PhaseWarpedDrums::GROUP_GATE_2));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(46, outY2)), module, PhaseWarpedDrums::GROUP_GATE_3));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(76, outY2)), module, PhaseWarpedDrums::PHASE_OUT));
        addOutput(createOutputCentered<SeptagonPort>(mm2px(Vec(107, outY2)), module, PhaseWarpedDrums::METRIC_ACCENT_OUT));
    }
};

Model* modelSeptagon = createModel<PhaseWarpedDrums, PhaseWarpedDrumsWidget>("Septagon");
