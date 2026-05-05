/*
 * Glitch Please - IDUM gate FX port for VCV Rack 2 & MetaModule
 *
 * Based on IDUM Firmware v.99 by Eli Pechman / Mystic Circuits
 * Released under Creative Commons Attribution-ShareAlike 4.0 International
 * https://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 * VCV Rack / MetaModule port by MorphWorx
 *
 * Original IDUM: www.MysticCircuits.com/product/idum
 */

#include "plugin.hpp"

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Breakbeat preset pattern table — identical to IDUM firmware v.99
// [16 patterns][4 channels][16 steps]
// Values: 0=silence, 1=trigger, 2/4=ratchet divisor
// Runs at 2x clock resolution (every half-clock = one step)
// ─────────────────────────────────────────────────────────────────────────────
static const int kBreakBeat[16][4][16] = {
  { { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } },
  { { 1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } },
  { { 1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,1,0,1,0,0,0,1,0,1,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0 } },
  { { 1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 }, { 1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0 } },
  { { 1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0 }, { 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,2 }, { 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 } },
  { { 1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0 }, { 1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0 }, { 1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0 } },
  { { 1,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0 }, { 1,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0 }, { 1,1,1,0,1,0,1,0,1,1,1,0,1,0,2,2 }, { 1,0,0,1,0,0,1,0,1,0,1,0,0,0,0,0 } },
  { { 1,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0 }, { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0 }, { 1,0,1,0,1,1,1,0,4,4,1,0,1,1,1,0 }, { 1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1 } },
  { { 1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0 }, { 1,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0 }, { 1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0 }, { 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0 } },
  { { 1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0 }, { 1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0 }, { 2,2,1,0,1,0,1,0,1,1,1,0,1,0,4,4 }, { 1,0,0,1,0,1,0,0,1,0,1,0,0,0,0,0 } },
  { { 1,0,1,0,1,0,0,0,0,0,0,0,0,0,1,0 }, { 1,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0 }, { 2,2,1,1,1,0,1,0,1,1,1,0,1,0,1,4 }, { 0,0,1,0,0,0,1,0,0,0,1,1,0,0,1,0 } },
  { { 1,0,0,1,0,0,1,0,1,0,0,0,0,0,1,0 }, { 1,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0 }, { 4,0,1,0,1,0,1,0,4,0,1,0,1,0,1,4 }, { 0,0,1,1,0,0,1,1,0,0,4,4,0,0,1,1 } },
  { { 1,0,0,1,0,0,1,0,1,0,0,1,0,0,0,0 }, { 1,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0 }, { 1,1,2,2,4,4,2,2,1,1,2,2,4,4,0,0 }, { 1,0,2,2,1,0,1,0,1,0,1,0,2,2,1,0 } },
  { { 1,1,1,0,0,0,0,0,1,0,0,1,0,0,0,0 }, { 1,0,1,0,1,0,0,0,0,0,0,0,0,0,1,1 }, { 1,1,1,1,1,1,1,1,1,1,2,2,4,4,0,0 }, { 1,0,2,2,1,0,1,0,4,4,1,0,2,2,1,0 } },
  { { 1,1,0,1,1,0,1,0,1,1,0,1,1,0,4,4 }, { 1,1,1,1,1,0,1,0,1,0,1,0,1,0,1,0 }, { 4,4,2,2,1,1,1,0,4,4,2,2,1,1,4,4 }, { 4,4,0,4,4,0,1,1,0,2,2,0,1,1,1,1 } },
  { { 4,4,0,0,0,0,0,0,4,4,2,2,1,1,0,0 }, { 4,4,1,1,1,0,1,0,2,2,2,2,1,0,0,0 }, { 4,4,2,2,1,1,0,0,4,4,2,2,1,0,1,0 }, { 4,4,0,1,0,1,1,0,1,0,0,0,0,0,1,1 } },
};

// Resolution lookup tables (from IDUM firmware, odd/even/powerOf2)
static const int kOdd[16]      = { 8, 7, 6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 8 };
static const int kEven[16]     = { 8, 6, 6, 4, 4, 2, 2, 1, 1, 2, 2, 4, 4, 6, 6, 8 };
static const int kPowerOf2[16] = { 8, 8, 4, 4, 2, 2, 1, 1, 1, 1, 2, 2, 4, 4, 8, 8 };

struct GlitchPlease;

static const char* glitchPleaseModeName(int idx) {
    static const char* kModeNames[] = {
        "Hold", "Burst", "Ratchet", "Ball",
        "Rotate", "Delay", "Break", "Skip"
    };
    if (idx < 0 || idx >= 8)
        return "Unknown";
    return kModeNames[idx];
}

struct GlitchPleaseModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

// ─────────────────────────────────────────────────────────────────────────────
struct GlitchPlease : Module {
    // ── Constants ────────────────────────────────────────────────────────────
    static constexpr int   kNumCh         = 4;
    static constexpr int   kNumModes      = 8;
    static constexpr int   kLoopSteps     = 16;
    static constexpr int   kNumTicks      = 4;  // sub-clock ticks per step recorded
    static constexpr float kGateHigh      = 10.f;
    static constexpr float kGateLow       = 0.f;
    static constexpr float kGateThreshold = 1.f;

    // ── Parameter / I/O Enums ────────────────────────────────────────────────
    // MetaModule row→column order
    enum ParamId {
        // Row 1 — main controls
        MODE_PARAM,
        PROB_PARAM,
        LENGTH_PARAM,
        PARAM_PARAM,
        // Row 2 — buttons & toggles
        CYCLE_PARAM,
        MODE_BTN_PARAM,
        LOOP_BUTTON_PARAM,
        // Row 3 — resolution switches
        PARAM_RESOLUTION_PARAM,
        LENGTH_RESOLUTION_PARAM,
        MERGE_POLICY_PARAM,
        GHOST_TIMING_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        TRIG_1_INPUT,
        TRIG_2_INPUT,
        TRIG_3_INPUT,
        TRIG_4_INPUT,
        MODE_CV_INPUT,
        PROB_CV_INPUT,
        LENGTH_CV_INPUT,
        PARAM_CV_INPUT,
        LOOP_GATE_INPUT,
        MERGE_CV_INPUT,
        GHOST_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CLOCK_OUTPUT,
        TRIG_1_OUTPUT,
        TRIG_2_OUTPUT,
        TRIG_3_OUTPUT,
        TRIG_4_OUTPUT,
        GHOST_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LOOP_LIGHT,
        // 8 RGB mode indicator LEDs: LED i uses channels MODE_LED_LIGHTS + i*3 (R, G, B)
        MODE_LED_LIGHTS,
        LIGHTS_LEN = MODE_LED_LIGHTS + kNumModes * 3
    };

    enum MergePolicy {
        MERGE_REPLACE = 0,
        MERGE_ADD = 1,
        MERGE_CUT = 2,
    };

    enum GhostTimingMode {
        GHOST_FLAM_MODE = 0,
        GHOST_GHOST_MODE = 1,
        GHOST_DRAG_MODE = 2,
    };

    // ── Per-channel state (no dynamic alloc) ─────────────────────────────────
    struct ChState {
        dsp::SchmittTrigger trig;
        uint64_t lastEdgeSample  = 0;   // sample when last rising edge arrived
        uint64_t lastFallSample  = 0;   // sample when last falling edge arrived
        uint64_t lastIntervalSmp = 1;   // samples between last two rising edges
        bool     prevHigh        = false;
        bool     loopPrevHigh    = false;
        bool     stateOut        = false;
        int      modifyLength    = 0;
        int      mode            = 0;
        int      unscaledParam   = 8;   // 0-15, noon = 8
        int      ratchetAmt      = 1;
        int      modifyParam     = 512; // 0-1023 raw
        int      sampledParam    = 512;
        int      sampledUParam   = 8;
        int      sampledRatchet  = 1;
        int      originalLength  = 0;
        int      probModifier    = 0;
        bool     startMod        = false;
        bool     endMod          = false;
        // Ball mode
        int      bounceTime      = 0;
        int      bounceDivide    = 10;
        bool     forceBounce     = false;
        // Delay mode
        int      sampledRatchetDelay = 1;
        // Break mode
        int      breakIndex      = 0;
        // Hold mode per-ch
        bool     holdChoke       = false;
    };

    // ── Module-level state ────────────────────────────────────────────────────
    dsp::SchmittTrigger clockTrig;
    dsp::SchmittTrigger modeBtnTrig;
    dsp::SchmittTrigger loopBtnTrig;
    dsp::SchmittTrigger loopGateTrig;

    uint64_t sampleCount       = 0;
    uint64_t lastClockSample   = 0;
    uint64_t clockIntervalSmp  = 1; // samples between last two clock rising edges
    uint64_t times2ClockSample = 0; // sample of last half-clock
    uint64_t nextTimes2ClockSample = 0;
    bool     clockIn           = false;
    bool     clockEdge         = false;
    bool     clockFallEdge     = false;
    bool     times2ClockEdge   = false;
    bool     clockStateOut     = false;
    bool     clockChoke        = true;

    // Clock-skip mode vars
    bool     clockRatchet      = false;
    bool     clockSkip         = false;
    bool     clockRatchetState = false;
    bool     clockRatchetStateOld = false;
    int      clockRatchetCycleCount = 0;
    int      clockSkipCycleCount    = 0;
    int      skipRatchetAmt    = 1;
    int      skipStepsRemaining = 0; // reused for queued loop-follow clock pulses

    // Global modification / prob state
    int      modifyLength      = 0;   // clock channel mod length
    int      originalModifyLength = 0;
    int      mode              = 0;   // clock channel mode
    bool     modificationActive = false;
    bool     startModification = false;
    uint64_t firstStepSample   = 0;
    int      probabilityModifier = 0;
    int      randomValue       = 0;

    // Global scaled knob values
    int      modeDialPosition    = 0;  // effective mode (always active)
    int      rawModeDialPosition = 0;  // physical knob position 0-7 (may be inactive)
    int      lengthScaled      = 1;
    int      lengthPosition    = 0;
    int      unscaledParam     = 8;
    int      ratchetAmount     = 1;
    int      modifyParam       = 512;
    int      probRead          = 0;    // 0-1023
    int      paramResolution   = 0;    // 0=odd 1=even 2=pow2
    int      lengthResolution  = 0;    // same options
    int      mergePolicySelection = MERGE_REPLACE;
    int      ghostTimingSelection = GHOST_GHOST_MODE;

    // Mode enable flags (user can disable modes via context menu)
    bool     activeModes[kNumModes] = { true,true,true,true,true,true,true,true };

    // Loop
    bool     loopEnable        = false;
    bool     loopInputBehavior = false; // false=toggle, true=momentary
    int      historyIndex      = 0;
    int      originalHistoryIndex = 0;
    int      originalLoopOffset = 0;
    int      tickIndex         = 0;
    int      loopIndex         = 0;
    int      loopIndexOld      = 0;
    int      loopLength        = 1;
    int      loopOffset        = 0;
    int      loopClockSpeed    = 8;
    int      loopRatchetAmount = 1;
    int      loopDivide        = 0;
    int      loopCycleIndex    = 0;
    int      loopCycleCount    = 0;
    bool     loopClockMult     = false;
    bool     loopClockMultOld  = false;
    bool     loopStepPrimed    = false;
    bool     loopClockBurstHigh = false;
    uint64_t loopClockMultSample = 0;
    uint64_t loopClockMultIntervalSmp = 1;
    uint64_t loopClockBurstPhaseSample = 0;

    bool     loopTrigHistory[kNumCh][kLoopSteps][kNumTicks] = {};
    int      loopModeHistory[kNumCh][kLoopSteps] = {};
    int      loopParamHistory[kNumCh][kLoopSteps] = {};
    int      loopModifyLenHistory[kNumCh][kLoopSteps] = {};
    int      loopOriginalLenHistory[kNumCh][kLoopSteps] = {};
    int      loopSampledParamHistory[kNumCh][kLoopSteps] = {};
    int      loopSampledUParamHistory[kNumCh][kLoopSteps] = {};
    int      loopSampledRatchetHistory[kNumCh][kLoopSteps] = {};
    int      loopSampledRatchetDelayHistory[kNumCh][kLoopSteps] = {};
    int      loopBreakIndexHistory[kNumCh][kLoopSteps] = {};
    int      loopBounceTimeHistory[kNumCh][kLoopSteps] = {};
    int      loopBounceDivideHistory[kNumCh][kLoopSteps] = {};
    int      loopProbModifierHistory[kNumCh][kLoopSteps] = {};
    bool     loopForceBounceHistory[kNumCh][kLoopSteps] = {};
    bool     loopHoldChokeHistory[kNumCh][kLoopSteps] = {};
    int      loopClockModeHistory[kLoopSteps] = {};
    int      loopClockParamHistory[kLoopSteps] = {};
    int      loopClockModifyLenHistory[kLoopSteps] = {};
    int      loopClockOriginalLenHistory[kLoopSteps] = {};
    int      loopClockProbModifierHistory[kLoopSteps] = {};

    int      clockModifyParam  = 512;
    int      clockUnscaledParam = 8;
    int      clockRatchetAmount = 1;
    bool     prevMergedTriggerState[kNumCh] = {};
    bool     ghostOutputHigh = false;
    bool     ghostPulseScheduled = false;
    bool     ghostHasPreviousOnset = false;
    uint64_t lastGhostSourceOnsetSample = 0;
    uint64_t ghostPulseStartSample = 0;
    uint64_t ghostPulseEndSample = 0;

    // Trigger pulse outputs (10ms gate after a modification generates a trigger)
    static constexpr float kTrigPulseWidthMs = 10.f;
    static constexpr float kGhostPulseWidthMs = 4.f;
    uint64_t trigPulseWidthSmp  = 480;
    uint64_t ghostPulseWidthSmp = 192;

    // Ball mode: scale factor so bounceDivide is expressed in samples rather than
    // firmware ticks (IDUM firmware processes gates at ~1 kHz internally).
    // = sampleRate / 1000, defaults to 48 for 48 kHz.
    int ballTimeScale = 48;

    // Channel states
    ChState ch[kNumCh];

    // ── Constructor ───────────────────────────────────────────────────────────
    GlitchPlease() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam<GlitchPleaseModeParamQuantity>(MODE_PARAM, 0.f, 7.f, 0.f, "Mode");
        getParamQuantity(MODE_PARAM)->snapEnabled = true;
        getParamQuantity(MODE_PARAM)->description = "Selects the current IDUM mode slot. Enabled slots report their current mode name; slots disabled with MODE SET show OFF until re-enabled.";

        configParam(PROB_PARAM, 0.f, 1.f, 0.5f, "Chance", "%", 0.f, 100.f);
        getParamQuantity(PROB_PARAM)->description = "Probability that a new mode event will start on each clock step.";

        configParam(LENGTH_PARAM, 1.f, 16.f, 8.f, "Length", " steps");
        getParamQuantity(LENGTH_PARAM)->snapEnabled = true;
        getParamQuantity(LENGTH_PARAM)->description = "Sets how many clock steps the active mode effect lasts. 1 = single step (loop scrub), 2-16 = loop/modification length in clock steps.";

        configParam(PARAM_PARAM, 0.f, 1.f, 0.5f, "Param");
        getParamQuantity(PARAM_PARAM)->description = "Per-mode shaping control. Its exact meaning depends on the selected mode.";

        configButton(CYCLE_PARAM, "Cycle");
        getParamQuantity(CYCLE_PARAM)->description = "Keeps the downstream sequence position aligned when a mode ends early or skips clocks.";

        configButton(MODE_BTN_PARAM, "Mode set");
        getParamQuantity(MODE_BTN_PARAM)->description = "Edits which modes are available to the mode knob. Press to remove the currently selected mode from the rotation. If only one mode remains, pressing again restores all eight modes.";

        configButton(LOOP_BUTTON_PARAM, "Loop");
        getParamQuantity(LOOP_BUTTON_PARAM)->description = "Toggles the looper that records and replays recent gate activity.";

        configSwitch(PARAM_RESOLUTION_PARAM, 0.f, 2.f, 0.f, "Param resolution", {"Odd", "Even", "Power of 2"});
        getParamQuantity(PARAM_RESOLUTION_PARAM)->snapEnabled = true;
        getParamQuantity(PARAM_RESOLUTION_PARAM)->description = "Selects the ratchet/param resolution mapping: Odd, Even, or Power of 2.";

        configSwitch(LENGTH_RESOLUTION_PARAM, 0.f, 2.f, 0.f, "Length resolution", {"Linear", "Even", "Power of 2"});
        getParamQuantity(LENGTH_RESOLUTION_PARAM)->snapEnabled = true;
        getParamQuantity(LENGTH_RESOLUTION_PARAM)->description = "Selects the length resolution mapping: Linear, Even, or Power of 2.";

        configSwitch(MERGE_POLICY_PARAM, 0.f, 2.f, 0.f, "Merge policy", {"Replace", "Add", "Cut"});
        getParamQuantity(MERGE_POLICY_PARAM)->snapEnabled = true;
        getParamQuantity(MERGE_POLICY_PARAM)->description = "Sets how GLITCH PLEASE combines rendered glitches with the incoming trigger pattern. Replace = current behaviour, Add = layer glitches on top, Cut = only keep the overlapping subtractive result.";

        configSwitch(GHOST_TIMING_PARAM, 0.f, 2.f, 1.f, "Ghost timing", {"Flam", "Ghost", "Drag"});
        getParamQuantity(GHOST_TIMING_PARAM)->snapEnabled = true;
        getParamQuantity(GHOST_TIMING_PARAM)->description = "Sets where the derived GHOST output lands inside the gaps between trigger onsets. Flam = late in the gap, Ghost = middle, Drag = early.";

        configInput(CLOCK_INPUT, "Clock input");
        configInput(TRIG_1_INPUT, "TR1 input");
        configInput(TRIG_2_INPUT, "TR2 input");
        configInput(TRIG_3_INPUT, "TR3 input");
        configInput(TRIG_4_INPUT, "TR4 input");
        configInput(MODE_CV_INPUT, "Mode CV input");
        configInput(PROB_CV_INPUT, "Chance CV input");
        configInput(LENGTH_CV_INPUT, "Length CV input");
        configInput(PARAM_CV_INPUT, "Param CV input");
        configInput(LOOP_GATE_INPUT, "Loop gate input");
        configInput(MERGE_CV_INPUT, "Merge CV input");
        configInput(GHOST_CV_INPUT, "Ghost timing CV input");
        inputInfos[CLOCK_INPUT]->description = "Main clock or trigger stream that advances mode timing.";
        inputInfos[TRIG_1_INPUT]->description = "Trigger lane 1 input.";
        inputInfos[TRIG_2_INPUT]->description = "Trigger lane 2 input.";
        inputInfos[TRIG_3_INPUT]->description = "Trigger lane 3 input.";
        inputInfos[TRIG_4_INPUT]->description = "Trigger lane 4 input.";
        inputInfos[MODE_CV_INPUT]->description = "Voltage control for mode selection.";
        inputInfos[PROB_CV_INPUT]->description = "Voltage control for chance / mode-trigger probability.";
        inputInfos[LENGTH_CV_INPUT]->description = "Voltage control for mode-event length.";
        inputInfos[PARAM_CV_INPUT]->description = "Voltage control for the current mode parameter.";
        inputInfos[LOOP_GATE_INPUT]->description = "External gate that toggles or momentarily drives the looper, depending on the loop-gate mode.";
        inputInfos[MERGE_CV_INPUT]->description = "Quantized voltage control for the Merge switch. Approximately 5 V shifts by one state.";
        inputInfos[GHOST_CV_INPUT]->description = "Quantized voltage control for the Ghost timing switch. Approximately 5 V shifts by one state.";

        configOutput(CLOCK_OUTPUT, "Clock output");
        configOutput(TRIG_1_OUTPUT, "TR1 output (Kick)");
        configOutput(TRIG_2_OUTPUT, "TR2 output (Hi-hat)");
        configOutput(TRIG_3_OUTPUT, "TR3 output (Snare)");
        configOutput(TRIG_4_OUTPUT, "TR4 output (Snare 2 / Accent)");
        configOutput(GHOST_OUTPUT, "Ghost output (Ghost note)");
        outputInfos[CLOCK_OUTPUT]->description = "Processed clock output, including skip or ratchet behaviour in Skip mode.";
        outputInfos[TRIG_1_OUTPUT]->description = "Processed trigger lane 1 output. Best suited for the kick lane.";
        outputInfos[TRIG_2_OUTPUT]->description = "Processed trigger lane 2 output. Best suited for hi-hats and other bright, short percussion.";
        outputInfos[TRIG_3_OUTPUT]->description = "Processed trigger lane 3 output. Best suited for the main snare lane.";
        outputInfos[TRIG_4_OUTPUT]->description = "Processed trigger lane 4 output. Best suited for a second snare, clap, or accent lane.";
        outputInfos[GHOST_OUTPUT]->description = "Derived ornament trigger output built from the final trigger onsets. It places a companion pulse early, mid-gap, or late between consecutive hits, and is best suited for ghost notes, grace hits, and supporting percussion.";

        configLight(LOOP_LIGHT, "Loop active");
        for (int i = 0; i < kNumModes; i++) {
            configLight(MODE_LED_LIGHTS + i * 3, string::f("Mode %d", i + 1));
        }
    }

    // ── Sample-rate change ────────────────────────────────────────────────────
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        trigPulseWidthSmp = (uint64_t)(e.sampleRate * kTrigPulseWidthMs * 0.001f);
        ghostPulseWidthSmp = std::max<uint64_t>(1, (uint64_t)(e.sampleRate * kGhostPulseWidthMs * 0.001f));
        ballTimeScale = std::max(1, (int)(e.sampleRate * 0.001f + 0.5f));
        // Preserve sensible default clock interval
        if (clockIntervalSmp < 2) clockIntervalSmp = (uint64_t)(e.sampleRate * 0.5f);
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].lastIntervalSmp < 2) ch[i].lastIntervalSmp = (uint64_t)(e.sampleRate * 0.5f);
        }
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        nextTimes2ClockSample = 0;
        for (int i = 0; i < kNumModes; i++) activeModes[i] = true;
        loopEnable = false;
        modifyLength = 0;
        originalModifyLength = 0;
        historyIndex = 0;
        originalHistoryIndex = 0;
        originalLoopOffset = 0;
        tickIndex = 0;
        loopIndex = 0;
        loopIndexOld = 0;
        loopLength = 1;
        loopOffset = 0;
        loopClockSpeed = 8;
        loopRatchetAmount = 1;
        loopDivide = 0;
        loopCycleIndex = 0;
        loopCycleCount = 0;
        loopClockMult = false;
        loopClockMultOld = false;
        loopStepPrimed = false;
        loopClockBurstHigh = false;
        loopClockBurstPhaseSample = 0;
        clockModifyParam = 512;
        clockUnscaledParam = 8;
        clockRatchetAmount = 1;
        skipStepsRemaining = 0;
        mergePolicySelection = MERGE_REPLACE;
        ghostTimingSelection = GHOST_GHOST_MODE;
        ghostOutputHigh = false;
        ghostPulseScheduled = false;
        ghostHasPreviousOnset = false;
        lastGhostSourceOnsetSample = 0;
        ghostPulseStartSample = 0;
        ghostPulseEndSample = 0;
        for (int i = 0; i < kNumCh; i++) {
            prevMergedTriggerState[i] = false;
        }
        for (int i = 0; i < kNumCh; i++) ch[i] = ChState{};
        for (int i = 0; i < kNumCh; i++) {
            for (int step = 0; step < kLoopSteps; step++) {
                loopModeHistory[i][step] = 0;
                loopParamHistory[i][step] = 512;
                loopModifyLenHistory[i][step] = 0;
                loopOriginalLenHistory[i][step] = 0;
                loopSampledParamHistory[i][step] = 512;
                loopSampledUParamHistory[i][step] = 8;
                loopSampledRatchetHistory[i][step] = 1;
                loopSampledRatchetDelayHistory[i][step] = 1;
                loopBreakIndexHistory[i][step] = 0;
                loopBounceTimeHistory[i][step] = 0;
                loopBounceDivideHistory[i][step] = 10;
                loopProbModifierHistory[i][step] = 0;
                loopForceBounceHistory[i][step] = false;
                loopHoldChokeHistory[i][step] = false;
                for (int tick = 0; tick < kNumTicks; tick++) {
                    loopTrigHistory[i][step][tick] = false;
                }
            }
        }
        for (int step = 0; step < kLoopSteps; step++) {
            loopClockModeHistory[step] = 0;
            loopClockParamHistory[step] = 512;
            loopClockModifyLenHistory[step] = 0;
            loopClockOriginalLenHistory[step] = 0;
            loopClockProbModifierHistory[step] = 0;
        }
    }

    // ── JSON Serialization ────────────────────────────────────────────────────
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* modes = json_array();
        for (int i = 0; i < kNumModes; i++) json_array_append_new(modes, json_boolean(activeModes[i]));
        json_object_set_new(root, "activeModes", modes);
        json_object_set_new(root, "paramResolution",  json_integer(paramResolution));
        json_object_set_new(root, "lengthResolution", json_integer(lengthResolution));
        json_object_set_new(root, "loopInputBehavior",json_boolean(loopInputBehavior));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* modes = json_object_get(root, "activeModes");
        if (modes) {
            for (int i = 0; i < kNumModes; i++) {
                json_t* v = json_array_get(modes, i);
                if (v) activeModes[i] = json_is_true(v);
            }
        }
        json_t* pr = json_object_get(root, "paramResolution");
        if (pr) {
            paramResolution = clamp((int)json_integer_value(pr), 0, 2);
            params[PARAM_RESOLUTION_PARAM].setValue((float)paramResolution);
        }
        json_t* lr = json_object_get(root, "lengthResolution");
        if (lr) {
            lengthResolution = clamp((int)json_integer_value(lr), 0, 2);
            params[LENGTH_RESOLUTION_PARAM].setValue((float)lengthResolution);
        }
        json_t* lib = json_object_get(root, "loopInputBehavior");
        if (lib) loopInputBehavior = json_is_true(lib);
    }

    // ═════════════════════════════════════════════════════════════════════════
    // process()
    // ═════════════════════════════════════════════════════════════════════════
    void process(const ProcessArgs& args) override {
        sampleCount++;

        // ── Read & scale inputs ───────────────────────────────────────────────
        readInputs(args);
        scaleInputs(args);

        // ── Edge detect: clock ────────────────────────────────────────────────
        detectClockEdges(args);

        // ── Edge detect: channels 1-4 ─────────────────────────────────────────
        for (int i = 0; i < kNumCh; i++) detectChEdge(i);

        // ── Mode selection ─────────────────────────────────────────────────────
        chooseMode();

        // ── Looper ────────────────────────────────────────────────────────────
        handleLooper();

        // ── Probability / modification test ───────────────────────────────────
        if (!loopEnable) modifyTest();

        // ── Mode buttons ──────────────────────────────────────────────────────
        handleModeButton();

        // ── Calculate output states ───────────────────────────────────────────
        calculateOutputStates();

        // ── Write outputs ─────────────────────────────────────────────────────
        writeOutputs();

        // ── Lights ────────────────────────────────────────────────────────────
        updateLights();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // readInputs — read raw knob + CV values (0-1023 range internally)
    // ─────────────────────────────────────────────────────────────────────────
    int modeRaw  = 0;
    int probRaw  = 0;
    int lengRaw  = 0;
    int paramRaw = 0;
    bool cycleIn = false;
    bool loopGateIn = false;
    bool loopGateInOld = false;

    int quantizeThreeWaySelection(float switchValue, float cvVoltage) const {
        float value = switchValue + (cvVoltage * 0.2f);
        if (value < 0.5f) {
            return 0;
        }
        if (value < 1.5f) {
            return 1;
        }
        return 2;
    }

    void readInputs(const ProcessArgs& /*args*/) {
        // Knob (0..1) + CV (0..10V) → 0..1023
        float modeKnob  = params[MODE_PARAM].getValue();        // 0..7 direct
        float modeCV    = inputs[MODE_CV_INPUT].getVoltage() * 0.1f; // 0..1
        float probKnob  = params[PROB_PARAM].getValue();        // 0..1
        float probCV    = inputs[PROB_CV_INPUT].getVoltage() * 0.1f;
        float lengKnob  = params[LENGTH_PARAM].getValue();      // 1..8 (snapped)
        float lengCV    = inputs[LENGTH_CV_INPUT].getVoltage() * 0.1f;
        float paramKnob = params[PARAM_PARAM].getValue();       // 0..1
        float paramCV   = inputs[PARAM_CV_INPUT].getVoltage() * 0.1f;

        paramResolution  = (int)params[PARAM_RESOLUTION_PARAM].getValue();
        lengthResolution = (int)params[LENGTH_RESOLUTION_PARAM].getValue();
        mergePolicySelection = quantizeThreeWaySelection(params[MERGE_POLICY_PARAM].getValue(), inputs[MERGE_CV_INPUT].getVoltage());
        ghostTimingSelection = quantizeThreeWaySelection(params[GHOST_TIMING_PARAM].getValue(), inputs[GHOST_CV_INPUT].getVoltage());

        // modeRaw: 0..1023 proportional to 0..7 modes
        modeRaw  = (int)clamp((modeKnob / 7.f + modeCV) * 1023.f, 0.f, 1023.f);
        probRaw  = (int)clamp((probKnob + probCV) * 1023.f, 0.f, 1023.f);
        // Normalize snapped 1-16 back to 0-1 before CV addition and 0-1023 scaling
        lengRaw  = (int)clamp(((lengKnob - 1.f) / 15.f + lengCV) * 1023.f, 0.f, 1023.f);
        paramRaw = (int)clamp((paramKnob + paramCV) * 1023.f, 0.f, 1023.f);

        probRead   = probRaw;
        modifyParam = paramRaw;

        cycleIn = params[CYCLE_PARAM].getValue() > 0.5f;

        loopGateInOld = loopGateIn;
        loopGateIn = inputs[LOOP_GATE_INPUT].getVoltage() > kGateThreshold;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // scaleInputs — map raw ADC values to discrete position vars
    // ─────────────────────────────────────────────────────────────────────────
    void scaleInputs(const ProcessArgs& /*args*/) {
        // ── Length position (0-7) with hysteresis ─────────────────────────────
        static const int kLengBounds[8] = { 50, 120, 300, 520, 680, 880, 1000, 1024 };
        for (int i = 0; i < 8; i++) {
            int lo = (i == 0) ? 0 : kLengBounds[i-1];
            if (lengRaw >= lo && lengRaw < kLengBounds[i]) {
                lengthPosition = i;
                break;
            }
        }
        switch (lengthResolution) {
            case 1:  lengthScaled = kEven[lengthPosition + 8]; break;
            case 2:  lengthScaled = kPowerOf2[lengthPosition + 8]; break;
            default: lengthScaled = lengthPosition + 1; break;
        }

        // ── Param → unscaledParam (0-15) with hysteresis ─────────────────────
        unscaledParam = rawToUnscaledParam(paramRaw);

        // ── Ratchet amount from param resolution ──────────────────────────────
        ratchetAmount = unscaledToRatchetAmount(unscaledParam);
        clockModifyParam = modifyParam;
        clockUnscaledParam = unscaledParam;
        clockRatchetAmount = ratchetAmount;

        // ── Copy global into per-channel (no split mode) ──────────────────────
        if (!loopEnable) {
            for (int i = 0; i < kNumCh; i++) {
                ch[i].modifyParam   = modifyParam;
                ch[i].unscaledParam = unscaledParam;
                ch[i].ratchetAmt    = ratchetAmount;
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // detectClockEdges
    // ─────────────────────────────────────────────────────────────────────────
    bool prevClockHigh = false;
    void detectClockEdges(const ProcessArgs& /*args*/) {
        float cv = inputs[CLOCK_INPUT].getVoltage();
        bool high = (cv > kGateThreshold);
        clockEdge     = high && !prevClockHigh;
        clockFallEdge = !high && prevClockHigh;
        times2ClockEdge = false;

        if (clockEdge) {
            clockIntervalSmp = sampleCount - lastClockSample;
            if (clockIntervalSmp < 1) clockIntervalSmp = 1;
            lastClockSample = sampleCount;
            times2ClockEdge = true;
            times2ClockSample = sampleCount;
            nextTimes2ClockSample = sampleCount + std::max<uint64_t>(1, clockIntervalSmp / 2);
        } else if (nextTimes2ClockSample != 0 && sampleCount >= nextTimes2ClockSample) {
            times2ClockEdge = true;
            times2ClockSample = nextTimes2ClockSample;
            nextTimes2ClockSample = 0;
        }

        clockIn = high;
        prevClockHigh = high;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // detectChEdge — rising/falling for gate channels
    // ─────────────────────────────────────────────────────────────────────────
    bool liveChEdge[kNumCh]     = {};
    bool liveChFallEdge[kNumCh] = {};
    bool liveChIn[kNumCh]       = {};
    bool chEdge[kNumCh]         = {};
    bool chFallEdge[kNumCh]     = {};
    bool chIn[kNumCh]           = {};

    int wrapLoopStep(int value) const {
        int wrapped = value % kLoopSteps;
        if (wrapped < 0) wrapped += kLoopSteps;
        return wrapped;
    }

    int rawToUnscaledParam(int raw) const {
        return clamp(raw / 64, 0, 15);
    }

    int unscaledToRatchetAmount(int value) const {
        switch (paramResolution) {
            case 1:  return kEven[value];
            case 2:  return kPowerOf2[value];
            default: return kOdd[value];
        }
    }

    uint64_t loopPulseWidth(uint64_t intervalSmp) const {
        if (intervalSmp < 1) intervalSmp = 1;
        uint64_t halfInterval = std::max<uint64_t>(1, intervalSmp / 2);
        return std::min<uint64_t>(trigPulseWidthSmp, halfInterval);
    }

    int computeTickFromInterval(uint64_t startSample, uint64_t intervalSmp) const {
        if (intervalSmp < 1) return 0;
        uint64_t elapsed = sampleCount - startSample;
        int tick = (int)((elapsed * kNumTicks) / intervalSmp);
        return clamp(tick, 0, kNumTicks - 1);
    }

    void clearLoopClockBurst() {
        skipStepsRemaining = 0;
        loopClockBurstHigh = false;
        loopClockBurstPhaseSample = 0;
    }

    void queueLoopClockSteps(int numSteps) {
        if (numSteps <= 0) {
            return;
        }

        skipStepsRemaining += numSteps;
        if (!loopClockBurstHigh && loopClockBurstPhaseSample == 0 && skipStepsRemaining > 0) {
            loopClockBurstHigh = true;
            loopClockBurstPhaseSample = sampleCount;
            skipStepsRemaining--;
        }
    }

    int computeLoopFollowSteps(int fromIndex, int toIndex) const {
        int fromWrapped = wrapLoopStep(fromIndex);
        int toWrapped = wrapLoopStep(toIndex);
        if (toWrapped >= fromWrapped) {
            return toWrapped - fromWrapped;
        }
        return kLoopSteps - (fromWrapped - toWrapped);
    }

    int computeLoopExitRecoverySteps() const {
        int recoverySteps = computeLoopFollowSteps(loopIndex, wrapLoopStep(loopCycleCount));
        if (clockIn && recoverySteps > 0) {
            recoverySteps--;
        }
        return recoverySteps;
    }

    bool advanceLoopClockBurst() {
        if (!loopClockBurstHigh && skipStepsRemaining == 0 && loopClockBurstPhaseSample == 0) {
            return false;
        }

        uint64_t phaseLength = loopPulseWidth(std::max<uint64_t>(1, clockIntervalSmp));
        if (phaseLength < 1) {
            phaseLength = 1;
        }

        if (loopClockBurstPhaseSample == 0 && skipStepsRemaining > 0) {
            loopClockBurstHigh = true;
            loopClockBurstPhaseSample = sampleCount;
            skipStepsRemaining--;
        }

        if ((sampleCount - loopClockBurstPhaseSample) >= phaseLength) {
            if (loopClockBurstHigh) {
                loopClockBurstHigh = false;
                loopClockBurstPhaseSample = sampleCount;
            } else if (skipStepsRemaining > 0) {
                loopClockBurstHigh = true;
                loopClockBurstPhaseSample = sampleCount;
                skipStepsRemaining--;
            } else {
                loopClockBurstPhaseSample = 0;
            }
        }

        return loopClockBurstHigh;
    }

    void syncEffectiveInputsToLive() {
        for (int i = 0; i < kNumCh; i++) {
            chIn[i] = liveChIn[i];
            chEdge[i] = liveChEdge[i];
            chFallEdge[i] = liveChFallEdge[i];
        }
    }

    void clearLoopStep(int step) {
        for (int i = 0; i < kNumCh; i++) {
            for (int tick = 0; tick < kNumTicks; tick++) {
                loopTrigHistory[i][step][tick] = false;
            }
        }
    }

    void captureLoopStepState(int step) {
        loopClockModeHistory[step] = mode;
        loopClockParamHistory[step] = clockModifyParam;
        loopClockModifyLenHistory[step] = modifyLength;
        loopClockOriginalLenHistory[step] = originalModifyLength;
        loopClockProbModifierHistory[step] = probabilityModifier;

        for (int i = 0; i < kNumCh; i++) {
            loopModeHistory[i][step] = ch[i].mode;
            loopParamHistory[i][step] = ch[i].modifyParam;
            loopModifyLenHistory[i][step] = ch[i].modifyLength;
            loopOriginalLenHistory[i][step] = ch[i].originalLength;
            loopSampledParamHistory[i][step] = ch[i].sampledParam;
            loopSampledUParamHistory[i][step] = ch[i].sampledUParam;
            loopSampledRatchetHistory[i][step] = ch[i].sampledRatchet;
            loopSampledRatchetDelayHistory[i][step] = ch[i].sampledRatchetDelay;
            loopBreakIndexHistory[i][step] = ch[i].breakIndex;
            loopBounceTimeHistory[i][step] = ch[i].bounceTime;
            loopBounceDivideHistory[i][step] = ch[i].bounceDivide;
            loopProbModifierHistory[i][step] = ch[i].probModifier;
            loopForceBounceHistory[i][step] = ch[i].forceBounce;
            loopHoldChokeHistory[i][step] = ch[i].holdChoke;
        }
    }

    void syncModificationActiveFromState() {
        modificationActive = (modifyLength > 0);
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength > 0) {
                modificationActive = true;
                break;
            }
        }
    }

    void restoreLoopStepSnapshot(int step) {
        mode = loopClockModeHistory[step];
        clockModifyParam = loopClockParamHistory[step];
        clockUnscaledParam = rawToUnscaledParam(clockModifyParam);
        clockRatchetAmount = unscaledToRatchetAmount(clockUnscaledParam);
        probabilityModifier = loopClockProbModifierHistory[step];

        modifyLength = 0;
        originalModifyLength = 0;

        clockRatchet = false;
        clockSkip = false;
        clockRatchetStateOld = false;

        for (int i = 0; i < kNumCh; i++) {
            ch[i].mode = loopModeHistory[i][step];
            ch[i].modifyParam = loopParamHistory[i][step];
            ch[i].unscaledParam = rawToUnscaledParam(ch[i].modifyParam);
            ch[i].ratchetAmt = unscaledToRatchetAmount(ch[i].unscaledParam);
            ch[i].sampledParam = loopSampledParamHistory[i][step];
            ch[i].sampledUParam = loopSampledUParamHistory[i][step];
            ch[i].sampledRatchet = loopSampledRatchetHistory[i][step];
            ch[i].sampledRatchetDelay = loopSampledRatchetDelayHistory[i][step];
            ch[i].breakIndex = loopBreakIndexHistory[i][step];
            ch[i].bounceTime = loopBounceTimeHistory[i][step];
            ch[i].bounceDivide = loopBounceDivideHistory[i][step];
            ch[i].probModifier = loopProbModifierHistory[i][step];
            ch[i].forceBounce = loopForceBounceHistory[i][step];
            ch[i].holdChoke = loopHoldChokeHistory[i][step];
            ch[i].modifyLength = 0;
            ch[i].originalLength = 0;
        }

        syncModificationActiveFromState();
    }

    bool stepHasSavedLoopModification(int step) const {
        if (loopClockModifyLenHistory[step] > 0) {
            return true;
        }
        for (int i = 0; i < kNumCh; i++) {
            if (loopModifyLenHistory[i][step] > 0) {
                return true;
            }
        }
        return false;
    }

    void applyLoopStepModificationChance(int step) {
        static const int kLoopProbLower = 15;

        modifyLength = 0;
        originalModifyLength = 0;
        for (int i = 0; i < kNumCh; i++) {
            ch[i].modifyLength = 0;
            ch[i].originalLength = 0;
        }

        if (!stepHasSavedLoopModification(step) || probRead <= kLoopProbLower) {
            syncModificationActiveFromState();
            return;
        }

        const int loopRandom = (int)(rack::random::u32() % 950u);
        if (loopRandom >= probRead) {
            syncModificationActiveFromState();
            return;
        }

        firstStepSample = sampleCount;
        if (loopClockModifyLenHistory[step] > 0) {
            modifyLength = 1;
            originalModifyLength = 1;
        }

        for (int i = 0; i < kNumCh; i++) {
            if (loopModifyLenHistory[i][step] > 0) {
                ch[i].modifyLength = 1;
                ch[i].originalLength = 1;
            }
        }

        syncModificationActiveFromState();
    }

    void detectChEdge(int i) {
        static const InputId inputMap[kNumCh] = { TRIG_1_INPUT, TRIG_2_INPUT, TRIG_3_INPUT, TRIG_4_INPUT };
        float cv  = inputs[inputMap[i]].getVoltage();
        bool  high = (cv > kGateThreshold);
        liveChEdge[i]     = high && !ch[i].prevHigh;
        liveChFallEdge[i] = !high && ch[i].prevHigh;
        if (liveChEdge[i] && !loopEnable) {
            ch[i].lastIntervalSmp = sampleCount - ch[i].lastEdgeSample;
            if (ch[i].lastIntervalSmp < 1) ch[i].lastIntervalSmp = 1;
            ch[i].lastEdgeSample = sampleCount;
        }
        if (liveChFallEdge[i] && !loopEnable) ch[i].lastFallSample = sampleCount;
        liveChIn[i]     = high;
        ch[i].prevHigh  = high;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // chooseMode — map modeRaw to active mode slot (IDUM boundary logic)
    // ─────────────────────────────────────────────────────────────────────────
    void chooseMode() {
        // Build list of active modes
        int activeModeList[kNumModes];
        int activeCount = 0;
        for (int i = 0; i < kNumModes; i++) {
            if (activeModes[i]) activeModeList[activeCount++] = i;
        }
        if (activeCount == 0) { activeModeList[0] = 0; activeCount = 1; }

        // Divide 0-1023 evenly among active modes
        int segment = 1024 / activeCount;
        int slot = clamp(modeRaw / (segment > 0 ? segment : 1), 0, activeCount - 1);
        modeDialPosition = activeModeList[slot];

        // Raw 0-7 position regardless of which modes are enabled — used by LED and mode button.
        rawModeDialPosition = clamp(modeRaw * kNumModes / 1024, 0, kNumModes - 1);

        // Match IDUM firmware: the selected mode can move, but the active mode
        // does not change while a modification or loop playback is running.
        if (!modificationActive && !loopEnable) {
            mode = modeDialPosition;
            for (int i = 0; i < kNumCh; i++) ch[i].mode = modeDialPosition;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // handleLooper — record and replay recent trigger/modification history
    // ─────────────────────────────────────────────────────────────────────────
    bool prevLoopBtn     = false;

    void handleLooper() {
        bool loopBtnHigh = params[LOOP_BUTTON_PARAM].getValue() > 0.5f;
        bool loopBtnEdge = loopBtnHigh && !prevLoopBtn;
        prevLoopBtn = loopBtnHigh;

        bool loopGateEdge = loopGateIn && !loopGateInOld;
        bool loopGateFall = !loopGateIn && loopGateInOld;

        bool toggled = false;
        if (loopBtnEdge) {
            loopEnable = !loopEnable;
            toggled = true;
        }
        if (loopInputBehavior) {
            if (loopGateEdge || loopGateFall) {
                loopEnable = !loopEnable;
                toggled = true;
            }
        } else {
            if (loopGateEdge) {
                loopEnable = !loopEnable;
                toggled = true;
            }
        }

        if (toggled && loopEnable) {
            originalLoopOffset = modeDialPosition;

            // Capture loop length first so we can compute the correct base index.
            int capturedLength = std::max(1, lengthScaled);

            // historyIndex always points to the step being written right now.
            // If the clock is currently high we haven't finished writing it yet,
            // so step back one more slot.
            int baseIndex = historyIndex;
            if (clockIn) {
                baseIndex = wrapLoopStep(baseIndex - 1);
            }
            // Shift back so that loopIndex=0 plays the OLDEST captured step
            // and loopIndex=capturedLength-1 plays the MOST RECENT one.
            originalHistoryIndex = wrapLoopStep(baseIndex - (capturedLength - 1));

            tickIndex = 0;
            loopIndex = 0;
            loopIndexOld = 0;
            loopLength = capturedLength;
            loopOffset = 0;
            loopClockSpeed = 8;
            loopRatchetAmount = 1;
            loopDivide = 0;
            loopCycleIndex = 0;
            loopCycleCount = 0;
            loopClockMult = false;
            loopClockMultOld = false;
            loopStepPrimed = false;
            loopClockMultSample = sampleCount;
            loopClockMultIntervalSmp = std::max<uint64_t>(1, clockIntervalSmp);
            clearLoopClockBurst();

            modifyLength = 0;
            originalModifyLength = 0;
            clockRatchet = false;
            clockSkip = false;
            clockRatchetStateOld = false;
            clockRatchetCycleCount = 0;
            clockSkipCycleCount = 0;
            for (int i = 0; i < kNumCh; i++) {
                ch[i].modifyLength = 0;
                ch[i].originalLength = 0;
                ch[i].loopPrevHigh = false;
            }
            syncModificationActiveFromState();
        } else if (toggled && !loopEnable) {
            if (cycleIn) {
                queueLoopClockSteps(computeLoopExitRecoverySteps());
            }

            loopCycleCount = 0;
            modifyLength = 0;
            originalModifyLength = 0;
            clockRatchet = false;
            clockSkip = false;
            clockRatchetStateOld = false;
            for (int i = 0; i < kNumCh; i++) {
                ch[i].modifyLength = 0;
                ch[i].originalLength = 0;
                ch[i].loopPrevHigh = false;
            }
            syncModificationActiveFromState();
        }

        if (clockEdge) {
            historyIndex = (historyIndex + 1) % kLoopSteps;
        }

        if (loopEnable) {
            playLoop();
        } else {
            recordLoop();
            syncEffectiveInputsToLive();
        }
    }

    void recordLoop() {
        if (clockEdge) {
            clearLoopStep(historyIndex);
            captureLoopStepState(historyIndex);
        }

        tickIndex = computeTickFromInterval(lastClockSample, std::max<uint64_t>(1, clockIntervalSmp));
        for (int i = 0; i < kNumCh; i++) {
            // Use |= so a trigger that fires and goes low within a tick quarter is
            // still captured.  clearLoopStep() resets all ticks at the next clock edge.
            loopTrigHistory[i][historyIndex][tickIndex] |= liveChIn[i];
        }
    }

    void playLoop() {
        loopLength = std::max(1, lengthScaled);
        loopOffset = wrapLoopStep(modeDialPosition - originalLoopOffset);
        loopRatchetAmount = std::max(1, ratchetAmount);
        loopClockSpeed = unscaledParam;

        if (clockEdge) {
            loopDivide = (loopDivide + 1) % loopRatchetAmount;
            loopCycleCount++;
        }

        uint64_t elapsedClock = sampleCount - lastClockSample;
        if (loopClockSpeed > 9) {
            uint64_t subdiv = std::max<uint64_t>(1, clockIntervalSmp / (uint64_t)loopRatchetAmount);
            loopClockMult = (elapsedClock % subdiv) < loopPulseWidth(subdiv);
        } else if (loopClockSpeed >= 7 && loopClockSpeed <= 9) {
            loopClockMult = clockIn;
            tickIndex = computeTickFromInterval(lastClockSample, std::max<uint64_t>(1, clockIntervalSmp));
        } else {
            loopClockMult = (loopDivide == 0) && (elapsedClock < loopPulseWidth(clockIntervalSmp));
        }

        if ((loopLength == 1) && (loopClockSpeed == 8)) {
            loopClockMult = false;
            tickIndex = 0;
        }

        bool loopClockEdge = loopClockMult && !loopClockMultOld;
        if (loopClockEdge) {
            loopCycleIndex = (loopCycleIndex + 1) % loopLength;
            loopClockMultIntervalSmp = sampleCount - loopClockMultSample;
            if (loopClockMultIntervalSmp < 1) loopClockMultIntervalSmp = 1;
            loopClockMultSample = sampleCount;
        }

        if ((loopClockSpeed < 7) || (loopClockSpeed > 9)) {
            tickIndex = computeTickFromInterval(loopClockMultSample, std::max<uint64_t>(1, loopClockMultIntervalSmp));
        }
        tickIndex = clamp(tickIndex, 0, kNumTicks - 1);

        if (loopLength == 1) {
            loopIndex = loopOffset;
        } else {
            loopIndex = (loopOffset + loopCycleIndex) % kLoopSteps;
        }

        int historyReadIndex = wrapLoopStep(originalHistoryIndex + loopIndex);
        bool loopIndexChanged = loopStepPrimed && (loopIndex != loopIndexOld);
        bool loopStepTriggered = !loopStepPrimed || loopClockEdge || loopIndexChanged;
        if (loopStepTriggered) {
            if (loopIndexChanged) {
                queueLoopClockSteps(computeLoopFollowSteps(loopIndexOld, loopIndex));
            }
            restoreLoopStepSnapshot(historyReadIndex);
            applyLoopStepModificationChance(historyReadIndex);
            loopIndexOld = loopIndex;
            loopStepPrimed = true;
        }

        for (int i = 0; i < kNumCh; i++) {
            bool high = loopTrigHistory[i][historyReadIndex][tickIndex];
            bool edge = high && !ch[i].loopPrevHigh;
            bool fall = !high && ch[i].loopPrevHigh;

            chIn[i] = high;
            chEdge[i] = edge;
            chFallEdge[i] = fall;

            if (edge) {
                ch[i].lastIntervalSmp = sampleCount - ch[i].lastEdgeSample;
                if (ch[i].lastIntervalSmp < 1) ch[i].lastIntervalSmp = 1;
                ch[i].lastEdgeSample = sampleCount;
            }
            if (fall) {
                ch[i].lastFallSample = sampleCount;
            }

            ch[i].loopPrevHigh = high;
        }

        if (times2ClockEdge) {
            for (int i = 0; i < kNumCh; i++) {
                ch[i].breakIndex = (ch[i].breakIndex + 1) % 16;
            }
        }

        loopClockMultOld = loopClockMult;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // modifyTest — probability engine: decide if a modification starts/continues
    // ─────────────────────────────────────────────────────────────────────────
    void modifyTest() {
        if (!clockEdge) return;

        static const int kProbLower = 15;
        static const int kProbUpper = 1000;

        // Decay probability modifier
        if (probabilityModifier > 0) probabilityModifier--;

        // Count down active modifications
        if (modifyLength > 0) {
            if (modifyLength == 1) probabilityModifier = originalModifyLength - 1;
            modifyLength--;
        }
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength > 0) ch[i].modifyLength--;
        }

        // Roll random value with modifier
        uint32_t rv = rack::random::u32() % kProbUpper;
        randomValue = (int)std::min((uint32_t)kProbUpper, rv + (uint32_t)(probabilityModifier * 90));

        // Update per-channel random (shared in non-split mode)
        for (int i = 0; i < kNumCh; i++) {
            // Copy global random — no split mode
        }

        modificationActive = false;
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength > 0) { modificationActive = true; break; }
        }
        if (modifyLength > 0) modificationActive = true;

        // Start a new modification?
        if (modifyLength == 0 && randomValue < probRead && probRead > kProbLower) {
            // Activate modification
            originalModifyLength = lengthScaled;
            modifyLength         = lengthScaled;
            firstStepSample      = sampleCount;
            probabilityModifier  = originalModifyLength - 1;
            modificationActive   = true;
            // Apply to all channels
            for (int i = 0; i < kNumCh; i++) {
                if (ch[i].modifyLength == 0) {
                    ch[i].originalLength  = lengthScaled;
                    ch[i].modifyLength    = lengthScaled;
                    ch[i].mode            = modeDialPosition;
                    ch[i].bounceTime      = 0;
                    ch[i].breakIndex      = 0;
                    ch[i].forceBounce     = true;
                    // Reset edge timestamp so Burst/Ratchet don't fire on a stale
                    // lastEdgeSample from before this modification started.
                    ch[i].lastEdgeSample  = sampleCount;
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // handleModeButton — toggle active modes, cancel active modification
    // ─────────────────────────────────────────────────────────────────────────
    bool prevModeBtn = false;
    void handleModeButton() {
        bool high = params[MODE_BTN_PARAM].getValue() > 0.5f;
        bool btnFallingEdge = !high && prevModeBtn;
        prevModeBtn = high;

        if (btnFallingEdge && !loopEnable) {
            // Always cancel any running modification first.
            if (modificationActive) {
                modifyLength = 0;
                for (int i = 0; i < kNumCh; i++) ch[i].modifyLength = 0;
                modificationActive = false;
            }

            // Then always toggle the mode at the current knob position.
            int activeCount = 0;
            for (int i = 0; i < kNumModes; i++) {
                if (activeModes[i])
                    activeCount++;
            }

            // When the last remaining mode would be removed, restore the full set.
            if (activeCount <= 1 && activeModes[rawModeDialPosition]) {
                for (int i = 0; i < kNumModes; i++) {
                    activeModes[i] = true;
                }
                modeDialPosition = 0;
                mode = 0;
                for (int i = 0; i < kNumCh; i++) {
                    ch[i].mode = 0;
                }
            } else {
                activeModes[rawModeDialPosition] = !activeModes[rawModeDialPosition];

                bool any = false;
                for (int i = 0; i < kNumModes; i++) {
                    if (activeModes[i]) { any = true; break; }
                }
                if (!any)
                    activeModes[rawModeDialPosition] = true;
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // calculateOutputStates — dispatch to each mode's logic
    // ─────────────────────────────────────────────────────────────────────────
    void calculateOutputStates() {
        // Default: pass through
        clockStateOut = clockIn;
        clockChoke    = true;

        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0) ch[i].stateOut = chIn[i];
        }

        if (!modificationActive) return;

        // Clock channel output for most modes: one trigger at start of modification
        if (modifyLength > 0) {
            bool isClockPulse = (sampleCount - firstStepSample) < trigPulseWidthSmp;
            clockStateOut = isClockPulse;
        }

        // Dispatch per mode
        for (int mi = 0; mi < kNumModes; mi++) {
            bool anyUses = false;
            for (int i = 0; i < kNumCh; i++) {
                if (ch[i].modifyLength > 0 && ch[i].mode == mi) anyUses = true;
            }
            if (mode == mi && modifyLength > 0) anyUses = true;
            if (!anyUses) continue;

            switch (mi) {
                case 0: modeHold();     break;
                case 1: modeBurst();    break;
                case 2: modeRatchet();  break;
                case 3: modeBall();     break;
                case 4: modeRotate();   break;
                case 5: modeDelay();    break;
                case 6: modeBreak();    break;
                case 7: modeSkip();     break;
                default: break;
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 0 — HOLD (gate silence / random skip / gate extend)
    // ─────────────────────────────────────────────────────────────────────────
    void modeHold() {
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 0) continue;
            int sp  = ch[i].sampledParam;
            int sup = ch[i].sampledUParam;

            if (chEdge[i]) {
                ch[i].sampledParam  = ch[i].modifyParam;
                ch[i].sampledUParam = ch[i].unscaledParam;
                sp  = ch[i].sampledParam;
                sup = ch[i].sampledUParam;
            }

            if (sp < 20) {
                ch[i].stateOut = false;
            } else if (sup < 8) {
                if (chEdge[i]) ch[i].holdChoke = ((rack::random::u32() % 450) < (uint32_t)sp);
                ch[i].stateOut = chIn[i] && ch[i].holdChoke;
            } else if (sup == 8) {
                ch[i].stateOut = chIn[i];
            } else {
                // Gate extend: hold gate open past the falling edge
                uint64_t elapsed = sampleCount - ch[i].lastFallSample;
                uint64_t extLen  = (uint64_t)((float)clockIntervalSmp *
                    ((float)((ch[i].originalLength + 1) * (sp - 512)) / 512.f));
                ch[i].stateOut = chIn[i] || (elapsed < extLen);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 1 — BURST (ratchet at clock-rate subdivisions)
    // ─────────────────────────────────────────────────────────────────────────
    void modeBurst() {
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 1) continue;
            int ra = ch[i].ratchetAmt;
            if (ra < 1) ra = 1;
            uint64_t scaledInterval;
            if (ch[i].unscaledParam > 7) {
                scaledInterval = clockIntervalSmp / (uint64_t)ra;
            } else {
                scaledInterval = clockIntervalSmp * (uint64_t)ra;
            }
            if (scaledInterval < 1) scaledInterval = 1;
            uint64_t elapsed = sampleCount - ch[i].lastEdgeSample;
            ch[i].stateOut = (elapsed % scaledInterval) < trigPulseWidthSmp;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 2 — RATCHET (ratchet at trigger-rate subdivisions)
    // ─────────────────────────────────────────────────────────────────────────
    void modeRatchet() {
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 2) continue;
            int ra = ch[i].ratchetAmt;
            if (ra < 1) ra = 1;
            uint64_t scaledInterval;
            if (ch[i].unscaledParam > 7) {
                scaledInterval = ch[i].lastIntervalSmp / (uint64_t)ra;
            } else {
                scaledInterval = ch[i].lastIntervalSmp * (uint64_t)ra;
            }
            if (scaledInterval < 1) scaledInterval = 1;
            uint64_t elapsed = sampleCount - ch[i].lastEdgeSample;
            ch[i].stateOut = (elapsed % scaledInterval) < trigPulseWidthSmp;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 3 — BALL (bouncing ball expanding/contracting)
    // ─────────────────────────────────────────────────────────────────────────
    void modeBall() {
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 3) continue;
            int sp  = ch[i].modifyParam;
            int sup = ch[i].unscaledParam;

            // ballTimeScale normalises raw firmware tick counts (recorded at ~1 kHz)
            // to the current host sample rate, keeping timing identical across rates.
            const int bts = ballTimeScale;

            if (sup > 8) {
                // Expanding: starts fast, gradually slows (right of noon)
                if (chEdge[i] || ch[i].forceBounce) {
                    ch[i].bounceTime   = 0;
                    ch[i].bounceDivide = (10 + (512 - (sp / 2))) * bts;
                    ch[i].forceBounce  = false;
                }
                ch[i].stateOut = (ch[i].bounceTime < 10 * bts) && (ch[i].bounceDivide < 1000 * bts);
                if (ch[i].bounceTime > ch[i].bounceDivide) {
                    ch[i].bounceTime   = 0;
                    int growth = (1 + (((1025 - sp) * ch[i].originalLength) / 8)) * bts;
                    ch[i].bounceDivide += growth;
                }
            } else if (sup == 8) {
                // Noon: pass triggers through unmodified (manual shows '0' at center)
                ch[i].stateOut = chIn[i];
            } else {
                // Contracting: starts slow, gradually speeds up (left of noon)
                if (chEdge[i] || ch[i].forceBounce) {
                    ch[i].bounceTime   = 0;
                    ch[i].bounceDivide = (100 + (sp / 2)) * bts;
                    ch[i].forceBounce  = false;
                }
                ch[i].stateOut = (ch[i].bounceTime < 10 * bts) && (ch[i].bounceDivide > 10 * bts);
                if (ch[i].bounceTime > ch[i].bounceDivide) {
                    ch[i].bounceTime   = 0;
                    float ratio = 0.5f + 0.4f * ((float)ch[i].originalLength / 8.f);
                    ch[i].bounceDivide = (int)((float)ch[i].bounceDivide * ratio);
                }
            }
            ch[i].bounceTime++;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 4 — ROTATE (cross-route channels)
    // Rotation table matches IDUM firmware v.99 exactly
    // ─────────────────────────────────────────────────────────────────────────
    void modeRotate() {
        // Rotation routing table per original IDUM firmware
        // ch0 → source, ch1 → source, ch2 → source, ch3 → source
        static const int kRotSrc[8][4] = {
            { 0, 2, 3, 1 }, // rot=0
            { 2, 3, 1, 0 }, // rot=1
            { 3, 2, 0, 1 }, // rot=2
            { 1, 0, 3, 2 }, // rot=3
            { 0, 1, 2, 3 }, // rot=4 (identity)
            { 3, 1, 0, 2 }, // rot=5
            { 2, 0, 1, 3 }, // rot=6
            { 1, 3, 2, 0 }, // rot=7
        };
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 4) continue;
            int rot = ch[i].unscaledParam / 2;
            int src = kRotSrc[rot % 8][i];
            ch[i].stateOut = chIn[src];
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 5 — DELAY (delay each gate by a ratio of its own interval)
    // ─────────────────────────────────────────────────────────────────────────
    void modeDelay() {
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 5) continue;
            if (chEdge[i]) ch[i].sampledRatchetDelay = ch[i].ratchetAmt;
            int ra = ch[i].sampledRatchetDelay;
            if (ra < 1) ra = 1;
            uint64_t elapsed    = sampleCount - ch[i].lastEdgeSample;
            uint64_t intervalSmp = ch[i].lastIntervalSmp;
            // Different divisors per channel (matching IDUM: ch0=8or16, ch1=12or14, ch2=14or12, ch3=16or8)
            static const int kDivA[4] = { 8, 12, 14, 16 };
            static const int kDivB[4] = { 16, 14, 12, 8  };
            int div = (ch[i].unscaledParam <= 7) ? kDivA[i] : kDivB[i];
            if (div < 1) div = 1;
            uint64_t delayStart = intervalSmp * (uint64_t)ra / (uint64_t)div;
            uint64_t delayEnd   = delayStart + trigPulseWidthSmp;
            ch[i].stateOut = (elapsed > delayStart) && (elapsed < delayEnd);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 6 — BREAK (preset rhythms from kBreakBeat table)
    // ─────────────────────────────────────────────────────────────────────────
    void modeBreak() {
        // Advance break indices on half-clock.
        // Guard with !loopEnable: playLoop() already advances breakIndex on times2ClockEdge
        // when the looper is running, so advancing here too would double the speed.
        if (times2ClockEdge && !loopEnable) {
            for (int i = 0; i < kNumCh; i++) {
                ch[i].breakIndex = (ch[i].breakIndex + 1) % 16;
            }
        }
        for (int i = 0; i < kNumCh; i++) {
            if (chEdge[i]) ch[i].breakIndex = 0;
        }

        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength == 0 || ch[i].mode != 6) continue;
            int pat = clamp(ch[i].unscaledParam, 0, 15);
            int idx = ch[i].breakIndex;
            int val = kBreakBeat[pat][i][idx];
            if (val < 2) {
                ch[i].stateOut = (val == 1) &&
                    ((sampleCount - times2ClockSample) < trigPulseWidthSmp);
            } else {
                // Ratchet: val is the divisor
                if (val < 1) val = 1;
                uint64_t rDiv = clockIntervalSmp / (uint64_t)val;
                if (rDiv < 1) rDiv = 1;
                uint64_t elapsed = (sampleCount - lastClockSample) % rDiv;
                ch[i].stateOut = elapsed < trigPulseWidthSmp;
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MODE 7 — SKIP (clock-only: ratchet or skip N steps)
    // ─────────────────────────────────────────────────────────────────────────
    void modeSkip() {
        if (mode != 7 || modifyLength == 0) return;

        // Set skip/ratchet mode on clock edges
        if (clockEdge) {
            skipRatchetAmt = clockRatchetAmount;
            if (clockUnscaledParam == 7 || clockUnscaledParam == 8) {
                clockRatchet = false;
                clockSkip    = false;
            } else if (clockUnscaledParam > 8) {
                clockRatchet = true;
                clockSkip    = false;
            } else {
                clockRatchet = false;
                clockSkip    = true;
            }
        }

        if (!clockRatchet && !clockSkip) {
            clockStateOut = clockIn;
            clockChoke    = true;
            if (clockEdge) clockRatchetCycleCount++;
        }

        if (clockRatchet) {
            clockChoke = true;
            int ra = skipRatchetAmt;
            if (ra < 1) ra = 1;
            uint64_t rInterval = clockIntervalSmp / (uint64_t)ra;
            if (rInterval < 1) rInterval = 1;
            bool rState = ((sampleCount - lastClockSample) % rInterval) < (rInterval / 2);
            if (rState && !clockRatchetStateOld) clockRatchetCycleCount++;
            clockStateOut        = rState;
            clockRatchetStateOld = rState;
        }

        if (clockSkip) {
            if (clockEdge) {
                clockSkipCycleCount += skipRatchetAmt;
                queueLoopClockSteps(skipRatchetAmt);
                clockChoke    = false;
                clockStateOut = false;
                clockRatchetStateOld = false;
            }
        }

        // Trigger channels pass through in skip mode
        for (int i = 0; i < kNumCh; i++) {
            if (ch[i].modifyLength > 0 && ch[i].mode == 7) ch[i].stateOut = chIn[i];
        }
    }

    bool mergeTriggerState(bool sourceState, bool glitchState) {
        switch (mergePolicySelection) {
            case MERGE_ADD:
                return sourceState || glitchState;
            case MERGE_CUT:
                return sourceState && glitchState;
            case MERGE_REPLACE:
            default:
                return glitchState;
        }
    }

    uint64_t computeGhostOffsetSamples(uint64_t intervalSmp) const {
        if (intervalSmp < 1) {
            intervalSmp = 1;
        }

        switch (ghostTimingSelection) {
            case GHOST_FLAM_MODE:
                return std::max<uint64_t>(1, (intervalSmp * 3) / 4);
            case GHOST_DRAG_MODE:
                return std::max<uint64_t>(1, intervalSmp / 4);
            case GHOST_GHOST_MODE:
            default:
                return std::max<uint64_t>(1, intervalSmp / 2);
        }
    }

    void updateGhostTriggerState(bool anySourceOnset) {
        if (anySourceOnset) {
            ghostPulseScheduled = false;
            ghostOutputHigh = false;

            if (ghostHasPreviousOnset) {
                uint64_t intervalSmp = sampleCount - lastGhostSourceOnsetSample;
                uint64_t minGap = (ghostPulseWidthSmp * 2) + 1;

                if (intervalSmp > minGap) {
                    uint64_t offsetSmp = computeGhostOffsetSamples(intervalSmp);
                    if (offsetSmp + ghostPulseWidthSmp >= intervalSmp) {
                        if (intervalSmp > ghostPulseWidthSmp + 1) {
                            offsetSmp = intervalSmp - ghostPulseWidthSmp - 1;
                        } else {
                            offsetSmp = 0;
                        }
                    }

                    if (offsetSmp > 0) {
                        ghostPulseStartSample = sampleCount + offsetSmp;
                        ghostPulseEndSample = ghostPulseStartSample + ghostPulseWidthSmp;
                        ghostPulseScheduled = true;
                    }
                }
            }

            lastGhostSourceOnsetSample = sampleCount;
            ghostHasPreviousOnset = true;
        }

        if (ghostPulseScheduled) {
            if (sampleCount >= ghostPulseStartSample && sampleCount < ghostPulseEndSample) {
                ghostOutputHigh = true;
            } else if (sampleCount >= ghostPulseEndSample) {
                ghostOutputHigh = false;
                ghostPulseScheduled = false;
            } else {
                ghostOutputHigh = false;
            }
        } else {
            ghostOutputHigh = false;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // writeOutputs
    // ─────────────────────────────────────────────────────────────────────────
    void writeOutputs() {
        bool loopClockBurstActive = loopClockBurstHigh || skipStepsRemaining > 0 || loopClockBurstPhaseSample != 0;
        bool loopClockBurstVoltage = advanceLoopClockBurst();

        if (loopClockBurstActive) {
            outputs[CLOCK_OUTPUT].setVoltage(loopClockBurstVoltage ? kGateHigh : kGateLow);
        } else if (loopEnable) {
            outputs[CLOCK_OUTPUT].setVoltage(kGateLow);
        } else {
            outputs[CLOCK_OUTPUT].setVoltage(clockStateOut && clockChoke ? kGateHigh : kGateLow);
        }

        static const OutputId outMap[kNumCh] = { TRIG_1_OUTPUT, TRIG_2_OUTPUT, TRIG_3_OUTPUT, TRIG_4_OUTPUT };
        bool anySourceOnset = false;
        for (int i = 0; i < kNumCh; i++) {
            bool mergedState = mergeTriggerState(chIn[i], ch[i].stateOut);
            anySourceOnset |= mergedState && !prevMergedTriggerState[i];
            outputs[outMap[i]].setVoltage(mergedState ? kGateHigh : kGateLow);
            prevMergedTriggerState[i] = mergedState;
        }

        updateGhostTriggerState(anySourceOnset);
        outputs[GHOST_OUTPUT].setVoltage(ghostOutputHigh ? kGateHigh : kGateLow);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // updateLights
    // ─────────────────────────────────────────────────────────────────────────
    void updateLights() {
        lights[LOOP_LIGHT].setBrightness(loopEnable ? 1.f : 0.f);

        // RGB LED colours per mode — all 8 must be visually distinct and vivid
        // mode: 0=Hold    1=Burst  2=Ratchet  3=Ball   4=Rotate  5=Delay   6=Break  7=Skip
        //       hot-pink   red      orange     yellow   green     cyan      blue     purple
        static const float kModeR[kNumModes] = { 1.f,  1.f,  1.f,   1.f,   0.f,   0.f,   0.f,  0.6f };
        static const float kModeG[kNumModes] = { 0.f,  0.f,  0.45f, 0.85f, 0.75f, 0.35f, 0.f,  0.f  };
        static const float kModeB[kNumModes] = { 0.5f, 0.f,  0.f,   0.f,   0.1f,  1.f,   1.f,  1.f  };

        // 8 arc LEDs: only the LED at rawModeDialPosition lights up in that mode's colour.
        // Disabled mode at knob position → black (same as unselected).
        // Active modification → full brightness; idle → 70%.
        for (int i = 0; i < kNumModes; i++) {
            int base = MODE_LED_LIGHTS + i * 3;
            if (i == rawModeDialPosition && activeModes[i]) {
                float scale = modificationActive ? 1.f : 0.7f;
                lights[base + 0].setBrightness(kModeR[i] * scale);
                lights[base + 1].setBrightness(kModeG[i] * scale);
                lights[base + 2].setBrightness(kModeB[i] * scale);
            } else {
                lights[base + 0].setBrightness(0.f);
                lights[base + 1].setBrightness(0.f);
                lights[base + 2].setBrightness(0.f);
            }
        }
    }
};

std::string GlitchPleaseModeParamQuantity::getDisplayValueString() {
    int idx = (int)std::lround(getValue());
    if (idx < 0)
        idx = 0;
    if (idx >= GlitchPlease::kNumModes)
        idx = GlitchPlease::kNumModes - 1;

    GlitchPlease* glitchPlease = dynamic_cast<GlitchPlease*>(module);
    if (glitchPlease && !glitchPlease->activeModes[idx])
        return "OFF";

    return string::f("Mode %d: %s", idx + 1, glitchPleaseModeName(idx));
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────
#ifndef METAMODULE
struct GpPanelLabel : TransparentWidget {
    std::string text;
    float fontSize;
    NVGcolor color;
    int align;
    static constexpr float kLabelWidth = 180.f;

    GpPanelLabel(Vec pos, const char* labelText, float labelFontSize, NVGcolor labelColor, int labelAlign)
        : text(labelText), fontSize(labelFontSize), color(labelColor), align(labelAlign) {
        box.pos = pos;
        box.size = Vec(kLabelWidth, fontSize + 4.f);
    }

    void draw(const DrawArgs& args) override {
        std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf"));
        if (!font)
            return;

        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);

        float x = 0.f;
        if (align == NVG_ALIGN_CENTER)
            x = box.size.x * 0.5f;
        else if (align == NVG_ALIGN_RIGHT)
            x = box.size.x;

        nvgText(args.vg, x, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

static GpPanelLabel* gpLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, int align = NVG_ALIGN_LEFT) {
    Vec pxPos = mm2px(mmPos);
    if (align == NVG_ALIGN_CENTER)
        pxPos.x -= GpPanelLabel::kLabelWidth * 0.5f;
    else if (align == NVG_ALIGN_RIGHT)
        pxPos.x -= GpPanelLabel::kLabelWidth;
    return new GpPanelLabel(pxPos, text, fontSize, color, align);
}

// Mode arc LEDs — 4.1 mm diameter
template <typename TBase>
struct GpModeLed : TBase {
    GpModeLed() {
        this->box.size = rack::window::mm2px(rack::math::Vec(4.1f, 4.1f));
    }
};

struct GlitchPleaseWidget : ModuleWidget {
    GlitchPleaseWidget(GlitchPlease* module) {
        setModule(module);

        box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);
        // ── PNG faceplate ─────────────────────────────────────────────────────
        box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);
        {
            auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/GP.png"));
            panelBg->box.pos  = Vec(0, 0);
            panelBg->box.size = box.size;
            addChild(panelBg);
        }

        NVGcolor labelCol = nvgRGB(0x00, 0x00, 0x00);

        constexpr float kPanelW = 60.96f;
        constexpr float kCenterX = kPanelW * 0.5f;
        constexpr float kTopRightX = 52.0f;
        constexpr float kTopRowY = 16.0f;
        constexpr float kModeKnobY = 34.0f;
        constexpr float kChanceSliderX = 10.0f;
        constexpr float kLengthSliderX = 50.96f;
        constexpr float kSliderY = 53.0f;
        constexpr float kLoopButtonY = 52.76f;
        constexpr float kParamKnobY = 68.0f;
        constexpr float kPxToMm = 25.4f / 75.0f;
        constexpr float kCvRowY = 84.0f;
        constexpr float kTrigInRowY = 98.0f - (12.0f * kPxToMm);
        constexpr float kTrigOutRowY = 116.0f + (15.0f * kPxToMm);
        constexpr float kPortLabelDy = 5.9f + (3.0f * kPxToMm);
        constexpr float kSmallLabelSize = 4.6f * 1.25f * 1.25f;
        constexpr float kTinyLabelSize = 4.0f * 1.25f;
        constexpr float kSwitchLabelSize = kTinyLabelSize * 1.15f;
        constexpr float kMidLabelSize = 5.8f * 1.25f;
        constexpr float kPortSpacing = 11.84f - (3.0f * kPxToMm);
        constexpr float kPortXs[5] = {
            kCenterX - (2.0f * kPortSpacing),
            kCenterX - kPortSpacing,
            kCenterX,
            kCenterX + kPortSpacing,
            kCenterX + (2.0f * kPortSpacing)
        };
        constexpr float kLoopLabelSize = 6.2f * 1.25f;
        constexpr float kParamLabelSize = 7.0f * 1.25f;
        constexpr float kInlineCvLabelInset = 4.0f;
        constexpr float kInlineCvLabelSize = kTinyLabelSize * 0.95f;



        auto addAboveLabel = [&](Vec mmPos, const char* text, float fontSize) {
            addChild(gpLabel(Vec(mmPos.x, mmPos.y - kPortLabelDy), text, fontSize, labelCol, NVG_ALIGN_CENTER));
        };

        auto addPortInput = [&](float x, float y, int inputId, const char* text) {
            addInput(createInputCentered<MVXport_s1>(mm2px(Vec(x, y)), module, inputId));
            addAboveLabel(Vec(x, y), text, kSmallLabelSize);
        };

        auto addPortOutput = [&](float x, float y, int outputId, const char* text) {
            addOutput(createOutputCentered<MVXport_s1_purple>(mm2px(Vec(x, y)), module, outputId));
            addAboveLabel(Vec(x, y), text, kSmallLabelSize);
        };

        // ── Top controls close to original IDUM silhouette ───────────────────
        addParam(createParamCentered<VCVButton>(mm2px(Vec(kTopRightX, kTopRowY)), module, GlitchPlease::MODE_BTN_PARAM));
        addChild(gpLabel(Vec(kTopRightX, kTopRowY - 5.0f), "MODE SET", kTinyLabelSize, labelCol, NVG_ALIGN_CENTER));

        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(kCenterX + 7.0f, kLoopButtonY)), module, GlitchPlease::LOOP_LIGHT));

        // ── Main mode control ────────────────────────────────────────────────
        addParam(createParamCentered<MVXKnob_pr>(mm2px(Vec(kCenterX, kModeKnobY)), module, GlitchPlease::MODE_PARAM));

        // ── 8 mode indicator LEDs in arc around the mode knob ─────────────────
        // Arc spans the knob's 270° sweep (-135° to +135° from 12 o'clock).
        // LED i is at the angular position the knob pointer occupies for mode i.
        {
            constexpr float kArcPi  = 3.14159265358979323846f;
            constexpr float kArcR   = 12.0f;  // radius in mm
            for (int i = 0; i < GlitchPlease::kNumModes; i++) {
                float angleDeg = -135.f + i * (270.f / 7.f);
                float angleRad = angleDeg * kArcPi / 180.f;
                float lx = kCenterX  + kArcR * std::sin(angleRad);
                float ly = kModeKnobY - kArcR * std::cos(angleRad);
                addChild(createLightCentered<GpModeLed<RedGreenBlueLight>>(
                    mm2px(Vec(lx, ly)), module, GlitchPlease::MODE_LED_LIGHTS + i * 3));
            }
        }

        // ── Side sliders: Chance and Length ──────────────────────────────────
        addParam(createParamCentered<VCVSlider>(mm2px(Vec(kChanceSliderX, kSliderY)), module, GlitchPlease::PROB_PARAM));
        addChild(gpLabel(Vec(kChanceSliderX, kSliderY + 14.0f), "CHANCE", kMidLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<VCVSlider>(mm2px(Vec(kLengthSliderX, kSliderY)), module, GlitchPlease::LENGTH_PARAM));
        addChild(gpLabel(Vec(kLengthSliderX, kSliderY + 14.0f), "LENGTH", kMidLabelSize, labelCol, NVG_ALIGN_CENTER));

        // ── Loop and param section ───────────────────────────────────────────
        addParam(createParamCentered<VCVButton>(mm2px(Vec(kCenterX, kLoopButtonY)), module, GlitchPlease::LOOP_BUTTON_PARAM));
        addChild(gpLabel(Vec(kCenterX, kLoopButtonY - 6.0f), "LOOP", kLoopLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(kCenterX, kParamKnobY)), module, GlitchPlease::PARAM_PARAM));
        addChild(gpLabel(Vec(kCenterX, kParamKnobY - 10.0f + (6.0f * kPxToMm)), "PARAM", kParamLabelSize, labelCol, NVG_ALIGN_CENTER));

        // ── Global switch strip + CVs: two rows above the outputs ───────────
        constexpr float kGlobalSwitchYOffset = 5.66f; // ~110 px total on the 2500 px faceplate art
        constexpr float kGlobalSwitchY = 101.0f + kGlobalSwitchYOffset;
        constexpr float kGlobalSwitchLabelGap = 7.85f; // ~3 px closer to the switches
        constexpr float kGlobalCvY = 109.5f;
        constexpr float kMergeCvX = 6.8f;
        constexpr float kMergeSwitchX = 14.8f;
        constexpr float kParamResSwitchX = 23.2f;
        constexpr float kCycleSwitchX = 30.48f;
        constexpr float kLengthResSwitchX = 37.76f;
        constexpr float kGhostSwitchX = 46.2f;
        constexpr float kGhostCvX = 54.2f;

        addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kMergeCvX, kGlobalCvY)), module, GlitchPlease::MERGE_CV_INPUT));
        addChild(gpLabel(Vec(kMergeCvX + kInlineCvLabelInset, kGlobalCvY), "MERGE CV", kInlineCvLabelSize, labelCol, NVG_ALIGN_LEFT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kMergeSwitchX, kGlobalSwitchY)), module, GlitchPlease::MERGE_POLICY_PARAM));
        addChild(gpLabel(Vec(kMergeSwitchX, kGlobalSwitchY - kGlobalSwitchLabelGap), "MERGE", kSwitchLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kParamResSwitchX, kGlobalSwitchY)), module, GlitchPlease::PARAM_RESOLUTION_PARAM));
        addChild(gpLabel(Vec(kParamResSwitchX, kGlobalSwitchY - kGlobalSwitchLabelGap), "P RES", kSwitchLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<CKSS>(mm2px(Vec(kCycleSwitchX, kGlobalSwitchY)), module, GlitchPlease::CYCLE_PARAM));
        addChild(gpLabel(Vec(kCycleSwitchX, kGlobalSwitchY - kGlobalSwitchLabelGap), "CYCLE", kSwitchLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kLengthResSwitchX, kGlobalSwitchY)), module, GlitchPlease::LENGTH_RESOLUTION_PARAM));
        addChild(gpLabel(Vec(kLengthResSwitchX, kGlobalSwitchY - kGlobalSwitchLabelGap), "L RES", kSwitchLabelSize, labelCol, NVG_ALIGN_CENTER));

        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kGhostSwitchX, kGlobalSwitchY)), module, GlitchPlease::GHOST_TIMING_PARAM));
        addChild(gpLabel(Vec(kGhostSwitchX, kGlobalSwitchY - kGlobalSwitchLabelGap), "GHOST", kSwitchLabelSize, labelCol, NVG_ALIGN_CENTER));
        addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kGhostCvX, kGlobalCvY)), module, GlitchPlease::GHOST_CV_INPUT));
        addChild(gpLabel(Vec(kGhostCvX - kInlineCvLabelInset, kGlobalCvY), "GHOST CV", kInlineCvLabelSize, labelCol, NVG_ALIGN_RIGHT));

        // ── Control CV row: CHANCE MODE PARAM LOOP LENGTH ───────────────────
        addPortInput(kPortXs[0], kCvRowY, GlitchPlease::PROB_CV_INPUT,    "CHANCE");
        addPortInput(kPortXs[1], kCvRowY, GlitchPlease::MODE_CV_INPUT,    "MODE");
        addPortInput(kPortXs[2], kCvRowY, GlitchPlease::PARAM_CV_INPUT,   "PARAM");
        addPortInput(kPortXs[3], kCvRowY, GlitchPlease::LOOP_GATE_INPUT,  "LOOP");
        addPortInput(kPortXs[4], kCvRowY, GlitchPlease::LENGTH_CV_INPUT,  "LENGTH");

        // ── Trigger input row: CLOCK TR1 TR2 TR3 TR4 ────────────────────────
        addPortInput(kPortXs[0], kTrigInRowY,  GlitchPlease::CLOCK_INPUT,  "CLOCK");
        addPortInput(kPortXs[1], kTrigInRowY,  GlitchPlease::TRIG_1_INPUT, "TR1");
        addPortInput(kPortXs[2], kTrigInRowY,  GlitchPlease::TRIG_2_INPUT, "TR2");
        addPortInput(kPortXs[3], kTrigInRowY,  GlitchPlease::TRIG_3_INPUT, "TR3");
        addPortInput(kPortXs[4], kTrigInRowY,  GlitchPlease::TRIG_4_INPUT, "TR4");

        // ── Output row: CLOCK TR1 TR2 TR3 TR4 GHOST ─────────────────────────
        constexpr float kOutputSpacing = kPanelW / 7.0f;
        constexpr float kOutputXs[6] = {
            kOutputSpacing,
            kOutputSpacing * 2.0f,
            kOutputSpacing * 3.0f,
            kOutputSpacing * 4.0f,
            kOutputSpacing * 5.0f,
            kOutputSpacing * 6.0f
        };
        addPortOutput(kOutputXs[0], kTrigOutRowY, GlitchPlease::CLOCK_OUTPUT,  "CLOCK");
        addPortOutput(kOutputXs[1], kTrigOutRowY, GlitchPlease::TRIG_1_OUTPUT, "TR1");
        addPortOutput(kOutputXs[2], kTrigOutRowY, GlitchPlease::TRIG_2_OUTPUT, "TR2");
        addPortOutput(kOutputXs[3], kTrigOutRowY, GlitchPlease::TRIG_3_OUTPUT, "TR3");
        addPortOutput(kOutputXs[4], kTrigOutRowY, GlitchPlease::TRIG_4_OUTPUT, "TR4");
        addOutput(createOutputCentered<MVXport_s1_purple>(mm2px(Vec(kOutputXs[5], kTrigOutRowY)), module, GlitchPlease::GHOST_OUTPUT));
        addChild(gpLabel(Vec(kOutputXs[5], kTrigOutRowY - kPortLabelDy), "GHOST", kSmallLabelSize, labelCol, NVG_ALIGN_CENTER));
    }

    void appendContextMenu(Menu* menu) override {
        GlitchPlease* m = dynamic_cast<GlitchPlease*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Active Modes"));

        for (int i = 0; i < GlitchPlease::kNumModes; i++) {
            menu->addChild(createBoolPtrMenuItem(string::f("Mode %d: %s", i + 1, glitchPleaseModeName(i)), "", &m->activeModes[i]));
        }

        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("Loop Gate: Momentary", "", &m->loopInputBehavior));
    }
};

#else

struct GlitchPleaseWidget : ModuleWidget {
    GlitchPleaseWidget(GlitchPlease* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GlitchPlease.png")));

        constexpr float kPanelW = 60.96f;
        constexpr float kCenterX = kPanelW * 0.5f;
        constexpr float kTopRightX = 52.0f;
        constexpr float kTopRowY = 16.0f;
        constexpr float kModeKnobY = 34.0f;
        constexpr float kChanceSliderX = 10.0f;
        constexpr float kLengthSliderX = 50.96f;
        constexpr float kSliderY = 53.0f;
        constexpr float kLoopButtonY = 52.76f;
        constexpr float kParamKnobY = 68.0f;
        constexpr float kPxToMm = 25.4f / 75.0f;
        constexpr float kCvRowY = 84.0f;
        constexpr float kTrigInRowY = 98.0f - (12.0f * kPxToMm);
        constexpr float kTrigOutRowY = 116.0f + (15.0f * kPxToMm);
        constexpr float kPortSpacing = 11.84f - (3.0f * kPxToMm);
        constexpr float kPortXs[5] = {
            kCenterX - (2.0f * kPortSpacing),
            kCenterX - kPortSpacing,
            kCenterX,
            kCenterX + kPortSpacing,
            kCenterX + (2.0f * kPortSpacing)
        };

        addParam(createParamCentered<VCVButton>(mm2px(Vec(kTopRightX, kTopRowY)), module, GlitchPlease::MODE_BTN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kCenterX, kModeKnobY)), module, GlitchPlease::MODE_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(kChanceSliderX, kSliderY)), module, GlitchPlease::PROB_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(kLengthSliderX, kSliderY)), module, GlitchPlease::LENGTH_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(kCenterX, kLoopButtonY)), module, GlitchPlease::LOOP_BUTTON_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kCenterX, kParamKnobY)), module, GlitchPlease::PARAM_PARAM));

        constexpr float kGlobalSwitchYOffset = 5.66f;
        constexpr float kGlobalSwitchY = 101.0f + kGlobalSwitchYOffset;
        constexpr float kGlobalCvY = 109.5f;
        constexpr float kMergeCvX = 6.8f;
        constexpr float kMergeSwitchX = 14.8f;
        constexpr float kParamResSwitchX = 23.2f;
        constexpr float kCycleSwitchX = 30.48f;
        constexpr float kLengthResSwitchX = 37.76f;
        constexpr float kGhostSwitchX = 46.2f;
        constexpr float kGhostCvX = 54.2f;

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kMergeCvX, kGlobalCvY)), module, GlitchPlease::MERGE_CV_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kMergeSwitchX, kGlobalSwitchY)), module, GlitchPlease::MERGE_POLICY_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kParamResSwitchX, kGlobalSwitchY)), module, GlitchPlease::PARAM_RESOLUTION_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(kCycleSwitchX, kGlobalSwitchY)), module, GlitchPlease::CYCLE_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kLengthResSwitchX, kGlobalSwitchY)), module, GlitchPlease::LENGTH_RESOLUTION_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(kGhostSwitchX, kGlobalSwitchY)), module, GlitchPlease::GHOST_TIMING_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kGhostCvX, kGlobalCvY)), module, GlitchPlease::GHOST_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[0], kCvRowY)), module, GlitchPlease::PROB_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[1], kCvRowY)), module, GlitchPlease::MODE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[2], kCvRowY)), module, GlitchPlease::PARAM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[3], kCvRowY)), module, GlitchPlease::LOOP_GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[4], kCvRowY)), module, GlitchPlease::LENGTH_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[0], kTrigInRowY)), module, GlitchPlease::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[1], kTrigInRowY)), module, GlitchPlease::TRIG_1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[2], kTrigInRowY)), module, GlitchPlease::TRIG_2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[3], kTrigInRowY)), module, GlitchPlease::TRIG_3_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kPortXs[4], kTrigInRowY)), module, GlitchPlease::TRIG_4_INPUT));

        constexpr float kOutputSpacing = kPanelW / 7.0f;
        constexpr float kOutputXs[6] = {
            kOutputSpacing,
            kOutputSpacing * 2.0f,
            kOutputSpacing * 3.0f,
            kOutputSpacing * 4.0f,
            kOutputSpacing * 5.0f,
            kOutputSpacing * 6.0f
        };
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[0], kTrigOutRowY)), module, GlitchPlease::CLOCK_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[1], kTrigOutRowY)), module, GlitchPlease::TRIG_1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[2], kTrigOutRowY)), module, GlitchPlease::TRIG_2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[3], kTrigOutRowY)), module, GlitchPlease::TRIG_3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[4], kTrigOutRowY)), module, GlitchPlease::TRIG_4_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kOutputXs[5], kTrigOutRowY)), module, GlitchPlease::GHOST_OUTPUT));
    }
};

#endif // !METAMODULE

Model* modelGlitchPlease = createModel<GlitchPlease, GlitchPleaseWidget>("GlitchPlease");
