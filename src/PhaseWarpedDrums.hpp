#pragma once
#include <rack.hpp>
#include "core/MetricEngine.hpp"
#include "core/EnergyField.hpp"
#include "core/PhaseWarper.hpp"
#include "core/TriggerExtractor.hpp"
#include "core/PatternMemory.hpp"
#include "core/MetricSpec.hpp"

using namespace rack;

struct PhaseWarpedDrums : Module {
    enum ParamIds {
        CHAOS_PARAM,
        DENSITY_PARAM,
        SWING_PARAM,
        TENSION_PARAM,
        EVOLUTION_PARAM,
        GROUPING_SELECT,
        GENERATE_BUTTON,
        TIME_MODE_PARAM,
        PATTERN_BARS,
        AUTO_REGEN_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        CHAOS_CV,
        DENSITY_CV,
        EVOLUTION_CV,
        SWING_CV,
        TENSION_CV,
        GENERATE_TRIG,
        RESET_INPUT,
        CLOCK_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {
        KICK_GATE,
        KICK_VEL,
        SNARE_GATE,
        SNARE_VEL,
        GHOST_GATE,
        GHOST_VEL,
        CHAT_GATE,
        CHAT_VEL,
        OHAT_GATE,
        OHAT_VEL,
        PHASE_OUT,
        METRIC_ACCENT_OUT,
        GROUP_GATE_1,
        GROUP_GATE_2,
        GROUP_GATE_3,
        FILL_GATE,
        NUM_OUTPUTS
    };

    enum LightIds {
        KICK_LIGHT,
        SNARE_LIGHT,
        GHOST_LIGHT,
        CHAT_LIGHT,
        OHAT_LIGHT,
        GENERATE_LIGHT,
        PHASE_LIGHT,
        NUM_LIGHTS
    };

    PhaseWarpedDrums();

    void process(const ProcessArgs& args) override;
    void generateNewPattern();  // Synchronous (used for initial/reset only)
    json_t* dataToJson() override;
    void dataFromJson(json_t* rootJ) override;
    void onReset() override;

private:
    // Amortized generation — spreads work across multiple process() calls
    // to prevent CPU spikes on MetaModule
    void startGeneration();
    void stepGeneration();

    // Generation state machine
    bool genActive_ = false;
    int genStep_ = 0;       // 0=setup, 1..N=per-bar, N+1=finalize
    int genTotalBars_ = 1;
    struct GenContext {
        float chaos, density, swing;
        bool enableSnares, enableGhosts;
        pwmt::MetricSpec metric;
        pwmt::PatternSnapshot pattern;
        pwmt::ExtractionConfig scaledKick, scaledSnarePri, scaledSnareGho;
        pwmt::ExtractionConfig scaledHatClosed, scaledHatOpen;
        float barFraction;
    } genCtx_;

private:
    pwmt::MetricEngine metricEngine_;
    pwmt::EnergyFieldGenerator fieldGenerator_;
    pwmt::PhaseWarper warper_;
    pwmt::TriggerExtractor extractor_;
    pwmt::PatternMemory memory_;

    // Base = generation/warp/transport grouping; Overlay = accent + group gates (for 7-over-4)
    pwmt::MetricGrouping baseGrouping_;
    pwmt::MetricGrouping overlayGrouping_;
    pwmt::MetricAccentCurve accentCurve_;
    pwmt::WarpFunction warpFunction_;

    // Base metric for transport/grid (defaults to Septagon identity: 7/4 at 16ths)
    pwmt::MetricSpec baseMetric_;
    // Overlay metric for accents/group gates (defaults to base)
    pwmt::MetricSpec overlayMetric_;

    int lastTimeMode_ = 0;

    pwmt::EnergyField kickField_;  // Kept for accent/phase output in process()

    // Pre-allocated trigger buffers (fixed-size, zero heap allocation)
    pwmt::TriggerBuffer kickTriggers_;
    pwmt::TriggerBuffer snareTriggers_;
    pwmt::TriggerBuffer ghostTriggers_;
    pwmt::TriggerBuffer chatTriggers_;
    pwmt::TriggerBuffer ohatTriggers_;
    pwmt::TriggerBuffer fillTriggers_;

    // Pre-allocated scratch EnergyFields for per-bar generation
    pwmt::EnergyField scratchKick_;
    pwmt::EnergyFieldGenerator::SnareFields scratchSnare_;
    pwmt::EnergyFieldGenerator::HatFields scratchHat_;
    // Scratch trigger buffers for per-bar extraction
    pwmt::TriggerBuffer scratchTrigBuf_;

    float barPhase_ = 0.0f;
    bool useExternalClock_ = false;
    bool hasGeneratedInitialPattern_ = false;
    int barCounter_ = 0;
    float genCooldownTimer_ = 0.0f;  // Cooldown to prevent rapid-fire generation
    static constexpr float GEN_COOLDOWN_SECS = 0.15f;  // 150ms minimum between generations

    struct TriggerState {
        int nextTriggerIdx = 0;
        float gateTimer = 0.0f;       // seconds remaining for gate pulse
        float lastVelocityV = 0.0f;   // sample-and-hold velocity (persists)
        static constexpr float GATE_DURATION = 0.003f;  // 3ms trigger pulse
    };

    TriggerState kickState_;
    TriggerState snareState_;
    TriggerState ghostState_;
    TriggerState chatState_;
    TriggerState ohatState_;
    TriggerState fillState_;

    static constexpr float FILL_GATE_DURATION = 0.08f;

    float kickGateV_ = 0.0f, kickVelV_ = 0.0f;
    float snareGateV_ = 0.0f, snareVelV_ = 0.0f;
    float ghostGateV_ = 0.0f, ghostVelV_ = 0.0f;
    float chatGateV_ = 0.0f, chatVelV_ = 0.0f;
    float ohatGateV_ = 0.0f, ohatVelV_ = 0.0f;
    float fillGateV_ = 0.0f;
    float groupGateV_[3] = {0.0f, 0.0f, 0.0f};

    dsp::ExponentialFilter chaosFilter_;
    dsp::ExponentialFilter densityFilter_;
    dsp::ExponentialFilter evolutionFilter_;
    dsp::ExponentialFilter swingFilter_;
    dsp::ExponentialFilter tensionFilter_;

    dsp::SchmittTrigger generateTrigger_;
    dsp::SchmittTrigger resetTrigger_;
    dsp::SchmittTrigger clockTrigger_;

    void updatePhase(float sampleTime);
    void processTriggers(float sampleTime, float prevPhase, float phaseAdvance, bool wrapped);
    void updateOutputs();
    void updateLights(float sampleTime);
    float getEffectiveParam(int paramId, int cvId, float scale = 1.0f);
    std::pair<float, float> processTriggerState(TriggerState& state,
                                                const pwmt::TriggerBuffer& triggers,
                                                float prevPhase,
                                                float currentPhase,
                                                float phaseAdvance,
                                                bool wrapped,
                                                float sampleTime,
                                                float gateDuration = TriggerState::GATE_DURATION);
    void updateGroupGates(float phaseAdvance);
};

extern Model* modelSeptagon;
