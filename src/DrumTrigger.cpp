#include "plugin.hpp"
#include "PatternData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#endif

// ============================================================================
// Trigonomicon Module
// ============================================================================
// Generative drum trigger pattern generator with musical mutation.
// Reads probability tables and fires trigger pulses on clock input.
// Each voice has its own step counter and wraps at its own length,
// enabling polymetric patterns and odd time signatures.
//
// Mutation layers (in order):
//   1. Segment DNA Swap — swap pattern segments from a donor pattern
//   2. Pattern Crossfade Blend — probabilistic per-step blend
//   3. Density Injection — ghost triggers on safe voices only
//   4. Micro Timing Variation — tiny probability perturbation
//
// Inputs:  CLOCK, RESET, MUTATE_CV
// Outputs: KICK, SNARE1, SNARE2, CLOSED_HAT, OPEN_HAT
// Params:  PATTERN_SELECT (0-39), MUTATE_AMOUNT (0.0-1.0)
// ============================================================================

// Number of segments per voice for DNA swap
static const int NUM_SEGMENTS = 4;

// Smoothstep helper: cubic Hermite interpolation
static inline float smoothstep(float edge0, float edge1, float x) {
	float t = math::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

// ============================================================================
// Genre lookup — shared by VCV LCD widget and MetaModule text display
// ============================================================================

static const char* getGenreForPattern(int p) {
	if (p <= 7)  return "BREAKBEAT / AMEN";
	if (p == 8)  return "IDM / POLYRHYTHM";
	if (p == 9)  return "BREAKCORE";
	if (p <= 11) return "JUNGLE";
	if (p <= 13) return "6/8 COMPOUND";
	if (p <= 15) return "ODD TIME";
	if (p <= 17) return "POLYMETRIC";
	if (p <= 19) return "EXPERIMENTAL";
	if (p <= 39) return "JUNGLE / BREAKCORE";
	if (p <= 44) return "DRUM & BASS";
	if (p <= 49) return "UK GARAGE";
	if (p <= 54) return "DUB / REGGAE";
	if (p <= 59) return "IDM / DRILLCORE";
	return "UNKNOWN";
}

struct DrumTrigger : Module {

	enum ParamId {
		PATTERN_SELECT_PARAM,
		MUTATE_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		MUTATE_INPUT,
		INPUTS_LEN
	};

	enum OutputId {
		KICK_OUTPUT,
		SNARE1_OUTPUT,
		SNARE2_OUTPUT,
		CLOSED_HAT_OUTPUT,
		OPEN_HAT_OUTPUT,
		OUTPUTS_LEN
	};

	enum LightId {
		DUMMY_LIGHT,
		LIGHTS_LEN
	};

#ifdef METAMODULE
	enum DisplayId {
		GENRE_DISPLAY = LIGHTS_LEN,
		DISPLAY_IDS_LEN
	};
#endif

	// --- Internal state (no allocations in process) ---

	// Schmitt triggers for clock and reset detection
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;

	// Pulse generators for each voice output (short trigger pulses)
	dsp::PulseGenerator pulseGens[NUM_VOICES];

	// Per-voice step counters — each voice wraps at its own length
	int voiceStep[NUM_VOICES] = {0, 0, 0, 0, 0};

	// Previous pattern index for detecting pattern changes
	int lastPattern = -1;

	// --- Mutation state ---

	// Working buffer — segment-swapped pattern (rebuilt on quantized boundaries)
	Pattern mutatedPattern;

	// Donor pattern for segment DNA swap
	int donorPatternIndex = -1;

	// Pre-rolled random thresholds per segment/voice.
	// A segment swaps when segmentAmount > threshold.
	// Re-rolled on hard mutation or pattern change.
	float segmentThresholds[NUM_VOICES][NUM_SEGMENTS] = {};

	// Previous CV value for hard mutation detection
	float prevMutateCV = 0.f;
	bool hardMutArmed = true;
	bool hardMutPending = false;

	// Cached smoothstep curve amounts (updated at control rate)
	float microAmount = 0.f;
	float blendAmount = 0.f;
	float segmentAmount = 0.f;
	float chaosAmount = 0.f;

	// Control rate divider (~every 32 samples)
	int controlRateCounter = 0;
	static const int CONTROL_RATE_DIV = 32;

	// Flag: mutation buffer needs rebuild
	bool mutationDirty = true;
	bool mutationPending = true;
	float lastSegmentAmountBuilt = -1.f;

	// Quantize segment-buffer rebuilds to clock edges (default: every 2 edges)
	int mutateClockDiv = 2;
	int mutateClockCounter = 0;

	// ----------------------------------------------------------------
	// Mutation helper methods (all called from process context)
	// ----------------------------------------------------------------

	// Choose a donor pattern within ±5 of current, never same as self
	void chooseDonorPattern(int currentPattern) {
		int attempts = 0;
		int donor;
		do {
			// Random offset -5..+5, wrap around
			int offset = (int)(random::uniform() * 11.f) - 5;
			donor = currentPattern + offset;
			if (donor < 0) donor += NUM_PATTERNS;
			if (donor >= NUM_PATTERNS) donor -= NUM_PATTERNS;
			attempts++;
		} while (donor == currentPattern && attempts < 20);
		donorPatternIndex = donor;
	}

	// Fill segmentThresholds with fresh random values
	void rollSegmentThresholds() {
		for (int v = 0; v < NUM_VOICES; v++) {
			for (int s = 0; s < NUM_SEGMENTS; s++) {
				segmentThresholds[v][s] = random::uniform();
			}
		}
	}

	// Rebuild mutatedPattern buffer from base + donor via segment swap
	void applySegmentMutation(int currentPattern) {
		const Pattern& base = PATTERNS[currentPattern];
		const Pattern& donor = (donorPatternIndex >= 0 && donorPatternIndex < NUM_PATTERNS)
			? PATTERNS[donorPatternIndex] : base;

		for (int v = 0; v < NUM_VOICES; v++) {
			const VoicePattern& baseVP = base.voices[v];
			const VoicePattern& donorVP = donor.voices[v];

			mutatedPattern.voices[v].numSteps = baseVP.numSteps;

			int segSize = baseVP.numSteps / NUM_SEGMENTS;
			if (segSize < 1) segSize = 1;

			for (int s = 0; s < NUM_SEGMENTS; s++) {
				int segStart = s * segSize;
				int segEnd = (s == NUM_SEGMENTS - 1) ? baseVP.numSteps : (s + 1) * segSize;

				bool usesDonor = (segmentAmount > 0.f && segmentThresholds[v][s] < segmentAmount);

				for (int i = segStart; i < segEnd; i++) {
					if (usesDonor) {
						// Wrap donor step via modulo if donor has different length
						int donorStep = i % donorVP.numSteps;
						float val = donorVP.steps[donorStep];

						// --- Kick protection ---
						// Never alter kick within first 10% of pattern
						if (v == VOICE_KICK && i < baseVP.numSteps / 10) {
							val = baseVP.steps[i];
						}

						// --- Snare anchor protection ---
						// Protect main backbeat zone: midpoint ± numSteps/16
						if (v == VOICE_SNARE1) {
							int mid = baseVP.numSteps / 2;
							int window = baseVP.numSteps / 16;
							if (window < 1) window = 1;
							if (i >= mid - window && i <= mid + window) {
								val = baseVP.steps[i];
							}
						}

						mutatedPattern.voices[v].steps[i] = val;
					} else {
						mutatedPattern.voices[v].steps[i] = baseVP.steps[i];
					}
				}
			}

			// Zero out unused steps
			for (int i = baseVP.numSteps; i < MAX_STEPS; i++) {
				mutatedPattern.voices[v].steps[i] = 0.f;
			}
		}
	}

	// Detect hard mutation (intentional CV spike) with hysteresis.
	// When detected, mutation is scheduled to apply at the next allowed boundary
	// (or immediately if CLOCK is unpatched).
	void detectHardMutation(float cvNorm) {
		// Rearm when CV drops low enough
		if (cvNorm <= 0.25f) {
			hardMutArmed = true;
		}
		// Spike detection (upward jump)
		if (hardMutArmed && (cvNorm - prevMutateCV > 0.65f)) {
			hardMutPending = true;
			mutationPending = true;
			hardMutArmed = false;
		}
		prevMutateCV = cvNorm;
	}

	// Control-rate update: recompute mutation curves and schedule buffer rebuilds
	void updateMutationState(float mutateCV, int currentPattern, bool hasClock) {
		// Combine knob + CV, clamp to [0, 1]
		float cvNorm = math::clamp(mutateCV / 10.f, 0.f, 1.f);
		cvNorm = math::clamp(cvNorm + params[MUTATE_PARAM].getValue(), 0.f, 1.f);

		// Compute layered curve amounts
		microAmount   = smoothstep(0.00f, 0.40f, cvNorm);
		blendAmount   = smoothstep(0.25f, 0.65f, cvNorm);
		segmentAmount = smoothstep(0.50f, 0.85f, cvNorm);
		chaosAmount   = smoothstep(0.80f, 1.00f, cvNorm);

		// Check for hard mutation spike
		detectHardMutation(cvNorm);

		// Only segmentAmount affects the segment mapping buffer.
		const float eps = 0.02f;
		bool segChanged = (lastSegmentAmountBuilt < 0.f) || (std::fabs(segmentAmount - lastSegmentAmountBuilt) > eps);
		if (segChanged || mutationDirty || hardMutPending) {
			mutationPending = true;
		}

		// If CLOCK is unpatched, allow immediate rebuild (control-rate) so Mutate stays responsive.
		if (!hasClock && mutationPending) {
			if (hardMutPending) {
				chooseDonorPattern(currentPattern);
				rollSegmentThresholds();
				hardMutPending = false;
			}
			applySegmentMutation(currentPattern);
			lastSegmentAmountBuilt = segmentAmount;
			mutationDirty = false;
			mutationPending = false;
			mutateClockCounter = 0;
		}
	}

	// ----------------------------------------------------------------
	// Constructor
	// ----------------------------------------------------------------
	DrumTrigger() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Pattern select: integer snapping 0-39
		configParam(PATTERN_SELECT_PARAM, 0.f, 59.f, 0.f, "Pattern Select");
		getParamQuantity(PATTERN_SELECT_PARAM)->snapEnabled = true;

		// Mutate amount: continuous 0.0-1.0
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate Amount", "%", 0.f, 100.f);

		// Label inputs
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(MUTATE_INPUT, "Mutate CV");

		// Label outputs
		configOutput(KICK_OUTPUT, "Kick");
		configOutput(SNARE1_OUTPUT, "Snare 1");
		configOutput(SNARE2_OUTPUT, "Snare 2");
		configOutput(CLOSED_HAT_OUTPUT, "Closed HiHat");
		configOutput(OPEN_HAT_OUTPUT, "Open HiHat");

		// Initialize mutation buffer to silence
		for (int v = 0; v < NUM_VOICES; v++) {
			mutatedPattern.voices[v].numSteps = 1;
			for (int i = 0; i < MAX_STEPS; i++) {
				mutatedPattern.voices[v].steps[i] = 0.f;
			}
		}
	}

	// ----------------------------------------------------------------
	// DSP process (called every sample)
	// ----------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		bool hasClock = inputs[CLOCK_INPUT].isConnected();

		// --- Read pattern select knob ---
		int patternIndex = (int) params[PATTERN_SELECT_PARAM].getValue();
		if (patternIndex < 0) patternIndex = 0;
		if (patternIndex >= NUM_PATTERNS) patternIndex = NUM_PATTERNS - 1;

		// --- Detect pattern change ---
		if (patternIndex != lastPattern) {
			for (int v = 0; v < NUM_VOICES; v++) {
				voiceStep[v] = 0;
				pulseGens[v].reset();
			}
			lastPattern = patternIndex;

			// Pick new donor and reshuffle segments for new pattern
			chooseDonorPattern(patternIndex);
			rollSegmentThresholds();
			mutationDirty = true;
			mutationPending = true;
			// If CLOCK is unpatched, rebuild immediately so mutatedPattern is never stale.
			if (!hasClock) {
				applySegmentMutation(patternIndex);
				lastSegmentAmountBuilt = segmentAmount;
				mutationDirty = false;
				mutationPending = false;
				mutateClockCounter = 0;
			}
		}

		// --- Handle reset input ---
		// Reset playhead and mutation interpolation state.
		// Does NOT change donor pattern or force new mutation (per spec).
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f)) {
			for (int v = 0; v < NUM_VOICES; v++) {
				voiceStep[v] = 0;
				pulseGens[v].reset();
			}
			controlRateCounter = 0;
			mutateClockCounter = 0;
		}

		// --- Control-rate mutation update ---
		controlRateCounter++;
		if (controlRateCounter >= CONTROL_RATE_DIV) {
			controlRateCounter = 0;
			float mutateCV = inputs[MUTATE_INPUT].getVoltage();
			updateMutationState(mutateCV, patternIndex, hasClock);
		}

		// --- Handle clock input ---
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
			// Quantized mutation rebuild window (default: every 2 clock edges)
			mutateClockCounter++;
			if (mutateClockDiv < 1) mutateClockDiv = 1;
			if (mutateClockCounter >= mutateClockDiv) {
				bool needsRebuild = (mutationDirty || mutationPending || hardMutPending);
				if (needsRebuild) {
					if (hardMutPending) {
						chooseDonorPattern(patternIndex);
						rollSegmentThresholds();
						hardMutPending = false;
					}
					applySegmentMutation(patternIndex);
					lastSegmentAmountBuilt = segmentAmount;
					mutationDirty = false;
					mutationPending = false;
				}
				mutateClockCounter = 0;
			}

			const Pattern& basePat = PATTERNS[patternIndex];
			bool closedHatFiredThisTick = false;

			for (int v = 0; v < NUM_VOICES; v++) {
				const VoicePattern& baseVP = basePat.voices[v];
				int step = voiceStep[v];

				// Safety clamp
				if (step >= baseVP.numSteps) {
					step = 0;
					voiceStep[v] = 0;
				}

				// === MUTATION LAYER 1+2: Segment swap + crossfade blend ===
				// Start with base probability
				float probability = baseVP.steps[step];

				// Crossfade blend: probabilistically choose mutated pattern
				if (blendAmount > 0.f) {
					if (random::uniform() < blendAmount) {
						// Use the segment-swapped mutated pattern
						float mutStep = step;
						if (mutStep < mutatedPattern.voices[v].numSteps) {
							probability = mutatedPattern.voices[v].steps[(int)mutStep];
						}
					}
				}

				// === MUTATION LAYER 3: Density injection (safe chaos) ===
				// Only on ghost-safe voices: Snare2, ClosedHat, OpenHat
				// Never on Kick or primary Snare1
				if (chaosAmount > 0.f && probability < 0.01f) {
					if (v == VOICE_SNARE2 || v == VOICE_CHAT || v == VOICE_OHAT) {
						// Inject ghost probability scaled by chaos
						float ghostProb = chaosAmount * 0.3f;
						// Clamp to density limits [0.08, 0.60]
						ghostProb = math::clamp(ghostProb, 0.08f, 0.60f);
						probability = ghostProb;
					}
				}

				// === MUTATION LAYER 4: Micro timing variation ===
				// Tiny probability perturbation — not actual clock delay
				if (microAmount > 0.f && probability > 0.f && probability < 1.f) {
					float offset = (random::uniform() - 0.5f) * microAmount * 0.1f;
					probability = math::clamp(probability + offset, 0.f, 1.f);
				}

				// === Fire decision ===
				bool shouldFire = false;
				if (probability >= 1.f) {
					shouldFire = true;
				} else if (probability > 0.f) {
					shouldFire = (random::uniform() < probability);
				}

				// --- Hi-hat choke ---
				// If the Closed Hat fires, cut any Open Hat gate and prevent same-tick OH.
				if (v == VOICE_OHAT && closedHatFiredThisTick) {
					shouldFire = false;
				}

				if (shouldFire) {
					if (v == VOICE_CHAT) {
						closedHatFiredThisTick = true;
						pulseGens[VOICE_OHAT].reset();
					}
					// Keep trigger compatibility, but make Open Hat a short gate so choke is meaningful.
					const float dur = (v == VOICE_OHAT) ? 0.10f : 5e-3f;
					pulseGens[v].trigger(dur);
				}

				// Advance step counter, wrap at voice's own length
				voiceStep[v]++;
				if (voiceStep[v] >= baseVP.numSteps) {
					voiceStep[v] = 0;
				}
			}
		}

		// --- Update outputs ---
		float dt = args.sampleTime;

		outputs[KICK_OUTPUT].setVoltage(pulseGens[VOICE_KICK].process(dt) ? 10.f : 0.f);
		outputs[SNARE1_OUTPUT].setVoltage(pulseGens[VOICE_SNARE1].process(dt) ? 10.f : 0.f);
		outputs[SNARE2_OUTPUT].setVoltage(pulseGens[VOICE_SNARE2].process(dt) ? 10.f : 0.f);
		outputs[CLOSED_HAT_OUTPUT].setVoltage(pulseGens[VOICE_CHAT].process(dt) ? 10.f : 0.f);
		outputs[OPEN_HAT_OUTPUT].setVoltage(pulseGens[VOICE_OHAT].process(dt) ? 10.f : 0.f);
	}

	// ----------------------------------------------------------------
	// Serialization: save/restore step position, pattern, and mutation
	// ----------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		// Voice steps
		json_t* stepsJ = json_array();
		for (int v = 0; v < NUM_VOICES; v++) {
			json_array_append_new(stepsJ, json_integer(voiceStep[v]));
		}
		json_object_set_new(rootJ, "voiceSteps", stepsJ);

		// Donor pattern
		json_object_set_new(rootJ, "donorPattern", json_integer(donorPatternIndex));

		// Segment thresholds
		json_t* threshJ = json_array();
		for (int v = 0; v < NUM_VOICES; v++) {
			for (int s = 0; s < NUM_SEGMENTS; s++) {
				json_array_append_new(threshJ, json_real(segmentThresholds[v][s]));
			}
		}
		json_object_set_new(rootJ, "segmentThresholds", threshJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Voice steps
		json_t* stepsJ = json_object_get(rootJ, "voiceSteps");
		if (stepsJ && json_is_array(stepsJ)) {
			for (int v = 0; v < NUM_VOICES && v < (int)json_array_size(stepsJ); v++) {
				voiceStep[v] = json_integer_value(json_array_get(stepsJ, v));
				if (voiceStep[v] < 0 || voiceStep[v] >= MAX_STEPS)
					voiceStep[v] = 0;
			}
		}

		// Donor pattern
		json_t* donorJ = json_object_get(rootJ, "donorPattern");
		if (donorJ && json_is_integer(donorJ)) {
			donorPatternIndex = json_integer_value(donorJ);
			if (donorPatternIndex < 0 || donorPatternIndex >= NUM_PATTERNS)
				donorPatternIndex = -1;
		}

		// Segment thresholds
		json_t* threshJ = json_object_get(rootJ, "segmentThresholds");
		if (threshJ && json_is_array(threshJ)) {
			int idx = 0;
			for (int v = 0; v < NUM_VOICES; v++) {
				for (int s = 0; s < NUM_SEGMENTS; s++) {
					if (idx < (int)json_array_size(threshJ)) {
						segmentThresholds[v][s] = (float)json_real_value(json_array_get(threshJ, idx));
					}
					idx++;
				}
			}
		}

		// Trigger mutation buffer rebuild on load
		mutationDirty = true;
	}

	// ----------------------------------------------------------------
	// Genre display text (used by MetaModule DynamicTextDisplay)
	// ----------------------------------------------------------------
	int getGenreDisplayText(char* buf, int bufSize) const {
		int p = lastPattern;
		if (p < 0) p = 0;
		if (p >= NUM_PATTERNS) p = NUM_PATTERNS - 1;
		const char* genre = getGenreForPattern(p);
		return snprintf(buf, bufSize, "%02d: %s", p, genre);
	}

#ifdef METAMODULE
	size_t get_display_text(int display_id, std::span<char> text) override {
		if (display_id != GENRE_DISPLAY) return 0;
		if (text.empty()) return 0;
		char buf[64];
		buf[0] = '\0';
		int n = getGenreDisplayText(buf, (int)sizeof(buf));
		if (n <= 0) return 0;
		size_t outN = std::min((size_t)n, text.size());
		std::memcpy(text.data(), buf, outN);
		return outN;
	}
#endif
};

// ============================================================================
// Panel Label Widget — draws text via NanoVG (SVG text is unsupported)
// VCV Rack only — NanoVG is not available on MetaModule.
// ============================================================================

#ifndef METAMODULE

struct PanelLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;
	NVGalign align;
	bool isTitle;

	PanelLabel(Vec pos, const char* text, float fontSize, NVGcolor color, NVGalign align = NVG_ALIGN_CENTER, bool isTitle = false) {
		box.pos = pos;
		box.size = Vec(60, fontSize + 4);
		this->text = text;
		this->fontSize = fontSize;
		this->color = color;
		this->align = align;
		this->isTitle = isTitle;
	}

	void draw(const DrawArgs& args) override {
		std::string fontPath = isTitle
			? asset::plugin(pluginInstance, "res/CinzelDecorative-Bold.ttf")
			: asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
		float y = fontSize / 2.f;
		nvgText(args.vg, 0, y, text.c_str(), NULL);
		if (isTitle) {
			nvgText(args.vg, 0.5f, y, text.c_str(), NULL);
			nvgText(args.vg, -0.5f, y, text.c_str(), NULL);
			nvgText(args.vg, 0, y + 0.4f, text.c_str(), NULL);
			nvgText(args.vg, 0, y - 0.4f, text.c_str(), NULL);
		}
	}
};

static PanelLabel* createLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
	Vec pxPos = mm2px(mmPos);
	return new PanelLabel(pxPos, text, fontSize, color, NVG_ALIGN_CENTER, isTitle);
}

// ============================================================================
// Genre LCD Display Widget — VCV Rack only
// ============================================================================

struct GenreLCDDisplay : TransparentWidget {
	DrumTrigger* module;

	static const char* getGenreForPatternLCD(int p) {
		return getGenreForPattern(p);
	}

	void draw(const DrawArgs& args) override {
		// Determine genre
		const char* genre = "BREAKBEAT / AMEN";
		char displayBuf[64];
		if (module) {
			int p = (int)module->params[DrumTrigger::PATTERN_SELECT_PARAM].getValue();
			if (p < 0) p = 0;
			if (p >= NUM_PATTERNS) p = NUM_PATTERNS - 1;
			genre = getGenreForPattern(p);
			snprintf(displayBuf, sizeof(displayBuf), "%02d: %s", p, genre);
		} else {
			snprintf(displayBuf, sizeof(displayBuf), "%s", genre);
		}

		// LCD green text
		std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) return;

		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 11.f * 1.25f);
		nvgFillColor(args.vg, nvgRGB(0xfe, 0x28, 0x16));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 2.f, displayBuf, NULL);
	}
};
#endif // !METAMODULE
// ============================================================================
// Module Widget (Panel + Components) — 10HP
// VCV Rack version with full NanoVG rendering.
// On MetaModule, text labels are baked into the faceplate PNG.
// ============================================================================

struct DrumTriggerWidget : ModuleWidget {
	DrumTriggerWidget(DrumTrigger* module) {
		setModule(module);

		// Load the panel from packaged res/
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Trigonomicon.svg")));

#ifndef METAMODULE
		// Optional PNG faceplate (when provided). Keep SVG for correct sizing.
		auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Trigonomicon.png"));
		panelBg->box.pos = Vec(0, 0);
		panelBg->box.size = box.size;
		addChild(panelBg);
#endif

		// Layout (10HP = 50.8mm wide). No labels; all text/screen art is on the faceplate PNG.
		const float centerX = 25.4f;
		const float knobLeftX = 14.12f;
		const float knobRightX = 36.68f;
		const float knobY = 52.0f;
		const float ioRowY = 73.0f;
		const float outStartY = 82.0f;
		const float outSpacingY = 9.5f;
		const Vec portShiftPx = Vec(0.f, -10.f);

		#ifndef METAMODULE
			struct TrigonomiconPort : MVXPort {
				TrigonomiconPort() {
					imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
					imageHandle = -1;
				}
			};
			struct TrigonomiconKnob : MVXKnob {
				TrigonomiconKnob() {
					box.size = box.size.mult(1.2f);
				}
			};
		#else
			using TrigonomiconPort = MVXPort;
			// MetaModule expects SliderKnob-derived params for addParam().
			using TrigonomiconKnob = app::SliderKnob;
		#endif

		// Screen text overlay (drawn over the faceplate)
		#ifndef METAMODULE
		{
			constexpr float screenX = 2.9f;
			constexpr float screenY = 26.0f;
			constexpr float screenW = 45.0f;
			constexpr float screenH = 6.0f * 1.2f; // ~20% taller
			GenreLCDDisplay* lcd = new GenreLCDDisplay();
			lcd->box.pos = mm2px(Vec(screenX, screenY));
			lcd->box.size = mm2px(Vec(screenW, screenH));
			lcd->module = module;
			addChild(lcd);
		}
		#else
		{
			// MetaModule: render text via VCVTextDisplay (drawn above the faceplate).
			struct TrigonomiconMMDisplay : MetaModule::VCVTextDisplay {};
			auto* mmDisplay = new TrigonomiconMMDisplay();
			mmDisplay->box.pos = mm2px(Vec(2.9f, 31.0f));
			mmDisplay->box.size = mm2px(Vec(45.0f, 6.0f));
			mmDisplay->font = "Default_10";
			mmDisplay->color = Colors565::Red;
			mmDisplay->firstLightId = DrumTrigger::GENRE_DISPLAY;
			addChild(mmDisplay);
		}
		#endif

		// Knobs (as-is)
		addParam(createParamCentered<TrigonomiconKnob>(mm2px(Vec(knobLeftX, knobY)), module, DrumTrigger::PATTERN_SELECT_PARAM));
		addParam(createParamCentered<TrigonomiconKnob>(mm2px(Vec(knobRightX, knobY)), module, DrumTrigger::MUTATE_PARAM));

		// Inputs row (CLK / RST / MUTATE CV)
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(knobLeftX, ioRowY)).plus(portShiftPx), module, DrumTrigger::CLOCK_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(centerX, ioRowY)).plus(portShiftPx), module, DrumTrigger::RESET_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(knobRightX, ioRowY)).plus(portShiftPx), module, DrumTrigger::MUTATE_INPUT));

		const char* outLabels[] = {"KCK", "SN1", "SN2", "CHH", "OHH"};
		int outIds[] = {
			DrumTrigger::KICK_OUTPUT, DrumTrigger::SNARE1_OUTPUT,
			DrumTrigger::SNARE2_OUTPUT, DrumTrigger::CLOSED_HAT_OUTPUT,
			DrumTrigger::OPEN_HAT_OUTPUT
		};

		// Drum outputs (center column)
		for (int i = 0; i < 5; i++) {
			float y = outStartY + i * outSpacingY;
			addOutput(createOutputCentered<TrigonomiconPort>(mm2px(Vec(centerX, y)).plus(portShiftPx), module, outIds[i]));
		}
	}
};

// ============================================================================
// Register the module
// ============================================================================

Model* modelTrigonomicon = createModel<DrumTrigger, DrumTriggerWidget>("Trigonomicon");
