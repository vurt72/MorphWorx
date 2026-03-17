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
// Wild mode (WILD switch ON) adds layer remapping + 6 new behaviors:
//   W1. Euclidean Voice Rewrite — Bjorklund groove on 1–2 voices at rebuild
//   W2. Negative Groove Flip   — invert shouldFire (counter-groove)
//   W3. Voice Phase Drift      — teleport step counter to musical offset
//   W4. Hyper-Ratchet          — extend to kick, 16×, cascade burst
//   W5. Full Chaos Reroll      — fully random donor + instant reroll
//   W6. Gate Length Fuzz       — randomize trigger pulse duration
//
// Inputs:  CLOCK, RESET, MUTATE_CV
// Outputs: KICK, SNARE1, SNARE2, CLOSED_HAT, OPEN_HAT
// Params:  PATTERN_SELECT (0-119), MUTATE_AMOUNT (0.0-1.0), WILD_MODE (0/1)
// ============================================================================

// Number of segments per voice for DNA swap
static const int NUM_SEGMENTS = 4;

// Smoothstep helper: cubic Hermite interpolation
static inline float smoothstep(float edge0, float edge1, float x) {
	float t = math::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

// ============================================================================
// Bjorklund (Euclidean rhythm) fill
// Writes k evenly-spaced hits into a VoicePattern of length n.
// O(n) — only called at mutation rebuild time, never per sample.
// ============================================================================
static void euclideanFill(VoicePattern& vp, int k, int n) {
	if (n <= 0) return;
	k = math::clamp(k, 1, n);
	for (int i = 0; i < n; i++) {
		// Bresenham-style even distribution
		vp.steps[i] = (((i * k) % n) < k) ? 1.f : 0.f;
	}
	vp.numSteps = n;
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
	if (p <= 79) return "EBM";
	if (p <= 89) return "ELECTRO";
	if (p <= 99) return "TECHNO";
	if (p <= 119) return "GQOM";
	if (p <= 129) return "TRAP";
	return "UNKNOWN";
}

struct DrumTrigger : Module {

	enum ParamId {
		PATTERN_SELECT_PARAM,
		MUTATE_PARAM,
		WILD_MODE_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		MUTATE_INPUT,
		FILL_INPUT,
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

	// Schmitt triggers for clock, reset, and fill detection
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger fillGateOn;   // detect rising edge (gate open)
	dsp::SchmittTrigger fillGateOff;  // detect falling edge (gate close)

	// Pulse generators for each voice output (short trigger pulses)
	dsp::PulseGenerator pulseGens[NUM_VOICES];

	// Per-voice step counters — each voice wraps at its own length
	int voiceStep[NUM_VOICES] = {0, 0, 0, 0, 0};

	// --- Tracker FX State (transient only; do not serialize) ---
	int stutterStepsRemaining = 0;
	bool stutterCache[NUM_VOICES] = {false, false, false, false, false};

	// --- Ratchet state (transient only; do not serialize) ---
	uint32_t samplesSinceLastClock = 4800;
	uint32_t clockIntervalSamples = 4800;
	int ratchetsRemaining[NUM_VOICES] = {0, 0, 0, 0, 0};
	uint32_t ratchetSamplesRemaining[NUM_VOICES] = {0, 0, 0, 0, 0};
	uint32_t ratchetIntervalSamples[NUM_VOICES] = {0, 0, 0, 0, 0};

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
	float wildDepth = 0.f;  // 0..1, only > 0 when WILD_MODE_PARAM is ON
	bool wildMode = false;

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

	// --- Ratchet cooldown (clock steps remaining before a voice can ratchet again) ---
	int ratchetCooldown[NUM_VOICES] = {0, 0, 0, 0, 0};

	// --- Fill CV state ---
	// sparseMode: 0=normal, 1=hats-only, 2=silence, 3=kick-only, 4=kick+hats
	// Driven by FILL_INPUT CV gate+voltage (no longer random).
	int sparseMode = 0;
	bool fillGateHigh = false;   // true while FILL CV is above threshold
	float fillLastVoltage = 0.f; // voltage at time of gate, used for fill intensity

	// --- Generative fill helper ---
	// Schedules a short multi-voice ratchet burst as a transition fill.
	// Called once when a sparse moment expires.
	// --- Generative fill helper ---
	// intensity: 0.0 = gentle (kick doublet), 1.0 = explosive (all voices, dense).
	// Fill spans 2 clock periods for musical weight at moderate tempos.
	void scheduleTransitionFill(float intensity) {
		uint32_t fillWindow = clockIntervalSamples * 2u;
		if (fillWindow < 960u) fillWindow = 960u; // safety floor

		for (int v = 0; v < NUM_VOICES; v++) {
			float roll = random::uniform();
			int hits = 0;
			uint32_t interval = 0;

			// Voice participation and density scale with intensity
			if (v == VOICE_KICK) {
				// Kick always participates — 2 hits at low intensity, up to 6 at max
				hits = 2 + (int)(intensity * 4.f);
				interval = fillWindow / (uint32_t)hits;
			} else if (v == VOICE_SNARE1) {
				// Snare1: needs intensity > 0.2 to participate
				float snareChance = 0.3f + intensity * 0.6f;
				if (roll < snareChance) {
					hits = 2 + (int)(intensity * 3.f);
					interval = fillWindow / (uint32_t)(hits + 1);
				}
			} else if (v == VOICE_SNARE2) {
				// Snare2/clap: needs intensity > 0.4
				float clapChance = intensity * 0.5f;
				if (roll < clapChance) {
					hits = 1 + (int)(intensity * 2.f);
					interval = fillWindow / (uint32_t)(hits + 2);
				}
			} else if (v == VOICE_CHAT) {
				// Closed hat: almost always present — 3 at low, up to 12 at max
				float hatChance = 0.5f + intensity * 0.4f;
				if (roll < hatChance) {
					hits = 3 + (int)(intensity * 9.f);
					interval = fillWindow / (uint32_t)hits;
				}
			} else if (v == VOICE_OHAT) {
				// Open hat: accent splash — more likely at high intensity
				float ohChance = 0.2f + intensity * 0.4f;
				if (roll < ohChance) {
					hits = 1 + (int)(intensity * 2.f);
					interval = fillWindow / (uint32_t)(hits + 1);
				}
			}

			if (hits > 0 && interval > 0) {
				float dur = (v == VOICE_OHAT) ? 0.10f : 5e-3f;
				triggerVoicePulse(v, dur);
				if (hits > 1) {
					ratchetsRemaining[v] = hits - 1;
					ratchetIntervalSamples[v] = interval;
					ratchetSamplesRemaining[v] = interval;
				}
			}
		}
	}

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

	void clearTrackerFxState() {
		stutterStepsRemaining = 0;
		for (int v = 0; v < NUM_VOICES; v++) {
			stutterCache[v] = false;
		}
	}

	static inline bool isStutterVoice(int voice) {
		return voice == VOICE_SNARE2 || voice == VOICE_CHAT || voice == VOICE_OHAT;
	}

	void clearRatchetState() {
		for (int v = 0; v < NUM_VOICES; v++) {
			ratchetsRemaining[v] = 0;
			ratchetSamplesRemaining[v] = 0;
			ratchetIntervalSamples[v] = 0;
		}
	}

	void clearRatchetForVoice(int voice) {
		ratchetsRemaining[voice] = 0;
		ratchetSamplesRemaining[voice] = 0;
		ratchetIntervalSamples[voice] = 0;
	}

	static inline bool isRatchetVoice(int voice) {
		return voice == VOICE_SNARE1 || voice == VOICE_SNARE2 || voice == VOICE_CHAT;
	}

	inline float ratchetPulseDuration(uint32_t intervalSamples, float sampleTime, float baseDur) const {
		if (intervalSamples == 0) return baseDur;
		float intervalSec = intervalSamples * sampleTime;
		float shortDur = std::max(5e-4f, intervalSec * 0.45f);
		return std::min(baseDur, shortDur);
	}

	inline void triggerVoicePulse(int voice, float dur) {
		if (voice == VOICE_CHAT) {
			pulseGens[VOICE_OHAT].reset();
		}
		pulseGens[voice].trigger(dur);
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

		// === WILD W1: Euclidean Voice Rewrite ===
		// Overwrite 1-2 non-kick voices with a fresh Euclidean rhythm.
		// Only runs during mutation rebuilds, so zero audio-rate cost.
		if (wildMode && wildDepth > 0.f) {
			// How many voices to rewrite: 1 normally, all 5 at extreme depth
			int maxRewrite = (wildDepth > 0.90f) ? NUM_VOICES : 2;
			// Musical k values: Euclidean spreads that always sound groove-intentional
			const int kValues[] = {3, 3, 5, 5, 7, 7, 9, 11};
			const int kCount = 8;
			int rewritten = 0;
			for (int v = 0; v < NUM_VOICES && rewritten < maxRewrite; v++) {
				// Skip kick unless extreme wildDepth; Euclidean kick can sound great
				if (v == VOICE_KICK && wildDepth < 0.90f) continue;
				// Probability of rewriting this voice scales with wildDepth
				if (random::uniform() > wildDepth) continue;
				int n = mutatedPattern.voices[v].numSteps;
				if (n < 2) continue;
				int ki = (int)(random::uniform() * kCount);
				int k = kValues[ki];
				// Clamp k so it's always less than n (meaningful Euclidean)
				if (k >= n) k = n / 2;
				if (k < 1) k = 1;
				euclideanFill(mutatedPattern.voices[v], k, n);
				rewritten++;
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

		// Read WILD switch
		wildMode = params[WILD_MODE_PARAM].getValue() > 0.5f;

		// Compute layered curve amounts — tighter in Wild mode (chaos starts sooner)
		if (wildMode) {
			microAmount   = smoothstep(0.00f, 0.30f, cvNorm);
			blendAmount   = smoothstep(0.15f, 0.50f, cvNorm);
			segmentAmount = smoothstep(0.35f, 0.70f, cvNorm);
			chaosAmount   = smoothstep(0.55f, 0.85f, cvNorm);
			wildDepth     = smoothstep(0.70f, 1.00f, cvNorm);
		} else {
			microAmount   = smoothstep(0.00f, 0.40f, cvNorm);
			blendAmount   = smoothstep(0.25f, 0.65f, cvNorm);
			segmentAmount = smoothstep(0.50f, 0.85f, cvNorm);
			chaosAmount   = smoothstep(0.80f, 1.00f, cvNorm);
			wildDepth     = 0.f;
		}

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
				// === WILD W5: Full Chaos Reroll ===
				// When Wild mode, sometimes pick a fully random donor instead of ±5.
				if (wildMode && wildDepth > 0.80f) {
					float rerollChance = 0.10f + (wildDepth - 0.80f) * 1.0f; // 10% at 80%, ~30% at 100%
					if (random::uniform() < rerollChance) {
						donorPatternIndex = (int)(random::uniform() * NUM_PATTERNS);
						if (donorPatternIndex >= NUM_PATTERNS) donorPatternIndex = NUM_PATTERNS - 1;
						rollSegmentThresholds();
					} else {
						chooseDonorPattern(currentPattern);
						rollSegmentThresholds();
					}
				} else {
					chooseDonorPattern(currentPattern);
					rollSegmentThresholds();
				}
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

		// Pattern select: integer snapping 0-99
		configParam(PATTERN_SELECT_PARAM, 0.f, 129.f, 0.f, "Pattern Select");
		getParamQuantity(PATTERN_SELECT_PARAM)->snapEnabled = true;

		// Mutate amount: continuous 0.0-1.0
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate Amount", "%", 0.f, 100.f);

		// Wild mode toggle
		configSwitch(WILD_MODE_PARAM, 0.f, 1.f, 0.f, "Wild Mutate", {"Off", "On"});

		// Label inputs
		configInput(CLOCK_INPUT, "Clock");
		configInput(FILL_INPUT, "Fill (gate+voltage)");
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
			clearTrackerFxState();
			clearRatchetState();

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
			clearTrackerFxState();
			clearRatchetState();
			// Exit any fill breakdown on reset
			sparseMode = 0;
			fillGateHigh = false;
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
			uint32_t currentClockDeltaSamples = samplesSinceLastClock;
			samplesSinceLastClock = 0;
			uint32_t minClockSamples = (uint32_t)std::lround(args.sampleRate * 0.015f);
			uint32_t maxClockSamples = (uint32_t)std::lround(args.sampleRate * 2.0f);
			if (currentClockDeltaSamples >= minClockSamples && currentClockDeltaSamples <= maxClockSamples) {
				clockIntervalSamples = currentClockDeltaSamples;
			}

				// --- Fill CV processing (replaces random sparse moments) ---
			// Runs on clock edge so zone changes are quantized to the grid.
			if (inputs[FILL_INPUT].isConnected()) {
				float fillV = inputs[FILL_INPUT].getVoltage();
				bool gateHigh = (fillV > 1.0f);

				if (gateHigh && !fillGateHigh) {
					// Rising edge — enter breakdown
					fillGateHigh = true;
					fillLastVoltage = fillV;
				}

				if (fillGateHigh) {
					// Continuously update zone from voltage
					fillLastVoltage = fillV;
					int newMode;
					if (fillV >= 7.f)      newMode = 2; // full silence
					else if (fillV >= 5.f) newMode = 1; // hats only
					else if (fillV >= 3.f) newMode = 3; // kick only
					else                   newMode = 4; // kick + hats (lightest)

					if (newMode != sparseMode) {
						sparseMode = newMode;
						// Clear ratchets for newly muted voices
						if (sparseMode == 2) clearRatchetState();
						else {
							if (sparseMode == 1 || sparseMode == 3) {
								clearRatchetForVoice(VOICE_SNARE1);
								clearRatchetForVoice(VOICE_SNARE2);
							}
							if (sparseMode == 1) clearRatchetForVoice(VOICE_KICK);
							if (sparseMode == 3) {
								clearRatchetForVoice(VOICE_CHAT);
								clearRatchetForVoice(VOICE_OHAT);
							}
							if (sparseMode == 4) {
								clearRatchetForVoice(VOICE_SNARE1);
								clearRatchetForVoice(VOICE_SNARE2);
							}
						}
					}
				}

				if (!gateHigh && fillGateHigh) {
					// Falling edge — exit breakdown, fire fill
					fillGateHigh = false;
					sparseMode = 0;
					// Intensity 0..1 from the voltage that was held
					float intensity = clamp(fillLastVoltage / 10.f, 0.f, 1.f);
					scheduleTransitionFill(intensity);
				}
			} else {
				// FILL not connected — if we were in a breakdown, exit cleanly
				if (fillGateHigh || sparseMode != 0) {
					fillGateHigh = false;
					sparseMode = 0;
				}
			}

			// Quantized mutation rebuild window (default: every 2 clock edges)
			mutateClockCounter++;
			if (mutateClockDiv < 1) mutateClockDiv = 1;
			if (mutateClockCounter >= mutateClockDiv) {
				bool needsRebuild = (mutationDirty || mutationPending || hardMutPending);
				if (needsRebuild) {
					if (hardMutPending) {
						// === WILD W5: Full Chaos Reroll (clock-edge path) ===
						if (wildMode && wildDepth > 0.80f) {
							float rerollChance = 0.10f + (wildDepth - 0.80f) * 1.0f;
							if (random::uniform() < rerollChance) {
								donorPatternIndex = (int)(random::uniform() * NUM_PATTERNS);
								if (donorPatternIndex >= NUM_PATTERNS) donorPatternIndex = NUM_PATTERNS - 1;
								rollSegmentThresholds();
							} else {
								chooseDonorPattern(patternIndex);
								rollSegmentThresholds();
							}
						} else {
							chooseDonorPattern(patternIndex);
							rollSegmentThresholds();
						}
						hardMutPending = false;
					}
					applySegmentMutation(patternIndex);
					lastSegmentAmountBuilt = segmentAmount;
					mutationDirty = false;
					mutationPending = false;
				}
				mutateClockCounter = 0;
			}

			bool isReplayingStutter = (stutterStepsRemaining > 0);
			bool startNewStutter = false;
			bool hardCutThisTick = false;
			if (isReplayingStutter) {
				stutterStepsRemaining--;
			}
			else if (chaosAmount > 0.7f) {
				float extremeDepth = (chaosAmount - 0.7f) * (1.f / 0.3f);
				float fxRoll = random::uniform();
				if (fxRoll < (extremeDepth * 0.12f)) {
					clearTrackerFxState();
					for (int v = 0; v < NUM_VOICES; v++) {
						if (isStutterVoice(v)) {
							clearRatchetForVoice(v);
						}
					}
					stutterStepsRemaining = (int)(random::uniform() * 3.f) + 1;
					// Wild extends stutter length at high depth
					if (wildMode && wildDepth > 0.7f) {
						stutterStepsRemaining += (int)(wildDepth * 3.f);
					}
					startNewStutter = true;

					// === WILD W3: Voice Phase Drift ===
					// Teleport 1-2 non-kick voices to a musical step offset so they
					// sound "lost" in a different meter pocket, then re-sync at boundary.
					if (wildMode && wildDepth > 0.40f) {
						const int maxDrift = (wildDepth > 0.90f) ? 2 : 1;
						int drifted = 0;
						for (int v = 0; v < NUM_VOICES && drifted < maxDrift; v++) {
							if (v == VOICE_KICK) continue; // kick anchors always
							if (random::uniform() > wildDepth) continue;
							const Pattern& basePat2 = PATTERNS[patternIndex];
							int n2 = basePat2.voices[v].numSteps;
							if (n2 < 4) continue;
							// Choose rational musical offset: n/3, n/4, or 2n/3
							const float offsets[] = {1.f/3.f, 1.f/4.f, 2.f/3.f};
							int oi = (int)(random::uniform() * 3.f);
							if (oi > 2) oi = 2;
							int drift = (int)(n2 * offsets[oi]);
							voiceStep[v] = (voiceStep[v] + drift) % n2;
							drifted++;
						}
					}
				}
				else if (fxRoll > (1.f - (extremeDepth * 0.08f))) {
					hardCutThisTick = true;
					clearRatchetState();
				}
			}

			const Pattern& basePat = PATTERNS[patternIndex];
			bool closedHatFiredThisTick = false;

			for (int v = 0; v < NUM_VOICES; v++) {
				const VoicePattern& baseVP = basePat.voices[v];
				int step = voiceStep[v];
				bool voiceIsStutterable = isStutterVoice(v);
				bool voiceIsReplaying = isReplayingStutter && voiceIsStutterable;

				// Safety clamp
				if (step >= baseVP.numSteps) {
					step = 0;
					voiceStep[v] = 0;
				}

				bool shouldFire = false;
				if (hardCutThisTick) {
					clearRatchetForVoice(v);
					shouldFire = false;
				}
				else if (voiceIsReplaying) {
					clearRatchetForVoice(v);
					shouldFire = stutterCache[v];
				}
				else {
					// === MUTATION LAYER 1+2: Segment swap + crossfade blend ===
					float probability = baseVP.steps[step];
					if (blendAmount > 0.f && random::uniform() < blendAmount) {
						float mutStep = step;
						if (mutStep < mutatedPattern.voices[v].numSteps) {
							probability = mutatedPattern.voices[v].steps[(int)mutStep];
						}
					}

					// === MUTATION LAYER 3: Density injection (safe chaos) ===
					if (chaosAmount > 0.f && probability < 0.01f && voiceIsStutterable) {
						float ghostProb = chaosAmount * 0.3f;
						ghostProb = math::clamp(ghostProb, 0.08f, 0.60f);
						probability = ghostProb;
					}

					// === MUTATION LAYER 4: Micro timing variation ===
					if (microAmount > 0.f && probability > 0.f && probability < 1.f) {
						float offset = (random::uniform() - 0.5f) * microAmount * 0.1f;
						probability = math::clamp(probability + offset, 0.f, 1.f);
					}

					if (probability >= 1.f) {
						shouldFire = true;
					}
					else if (probability > 0.f) {
						shouldFire = (random::uniform() < probability);
					}
				}

				// --- Hi-hat choke ---
				// If the Closed Hat fires, cut any Open Hat gate and prevent same-tick OH.
				if (v == VOICE_OHAT && closedHatFiredThisTick) {
					shouldFire = false;
				}

				// === WILD W2: Negative Groove Flip ===
				// After all mutation layers, invert shouldFire with rising probability.
				// Creates counter-groove / off-beat IDM feel. Kick excluded.
				if (wildMode && wildDepth > 0.20f && v != VOICE_KICK && !voiceIsReplaying && !hardCutThisTick) {
					float flipProb = (wildDepth - 0.20f) * 0.875f; // 0% at 20%, peaks ~70% at 100%
					if (random::uniform() < flipProb) {
						shouldFire = !shouldFire;
					}
				}

				if (startNewStutter && voiceIsStutterable) {
					stutterCache[v] = shouldFire;
				}

				if (shouldFire) {
					clearRatchetForVoice(v);
					uint32_t newRatchetIntervalSamples = 0;
					int newRatchetHitsRemaining = 0;

					// Tick down per-voice ratchet cooldown (counts clock steps, not samples)
					if (ratchetCooldown[v] > 0) ratchetCooldown[v]--;

					// --- Ratchet eligibility ---
					// Normal: snare1/snare2/chat can ratchet; kick can too but less often.
					// Wild W4: kick also eligible, higher max hits, cascade burst.
					bool voiceCanRatchet = isRatchetVoice(v) || (v == VOICE_KICK);
					if (wildMode && wildDepth > 0.60f && v == VOICE_KICK) voiceCanRatchet = true;

					// Ratchet roll probability — per-voice caps.
					// Snares are kept very rare (roughly once every 8-10 s at 142 BPM)
					// so ratchets feel like intentional accents, not constant machinegun.
					float ratchetThreshold = wildMode ? 0.70f : 0.90f;
					if (!voiceIsReplaying && chaosAmount > ratchetThreshold && voiceCanRatchet
						&& ratchetCooldown[v] == 0) {
						float ratchetDepth = (chaosAmount - ratchetThreshold) / (1.0f - ratchetThreshold);
						float maxProb;
						if (v == VOICE_SNARE2)      maxProb = wildMode ? 0.012f : 0.004f;
						else if (v == VOICE_SNARE1) maxProb = wildMode ? 0.03f  : 0.01f;
						else if (v == VOICE_KICK)   maxProb = wildMode ? 0.14f  : 0.04f;
						else                        maxProb = wildMode ? 0.30f  : 0.10f;
						float ratchetRollProb = ratchetDepth * maxProb;

						// Wild W4: cascade burst — all voices fire together at extreme depth
						// Snares excluded from cascade to keep them sparse.
						bool cascadeBurst = wildMode && wildDepth > 0.85f
							&& v != VOICE_SNARE1 && v != VOICE_SNARE2
							&& (random::uniform() < (wildDepth - 0.85f) * 2.f);

						if (cascadeBurst || random::uniform() < ratchetRollProb) {
							float roll = random::uniform();
							int maxHits = wildMode ? 16 : 8;
							int kickMaxHits = wildMode ? 4 : 2;
							if (cascadeBurst) {
								int burstHits = (wildDepth > 0.92f) ? 32 : 16;
								newRatchetIntervalSamples = std::max<uint32_t>(1u, clockIntervalSamples / (uint32_t)burstHits);
								newRatchetHitsRemaining = burstHits - 1;
								if (stutterStepsRemaining == 0) stutterStepsRemaining = 1;
							} else {
								int totalHits = 2;
								float intervalScale = 0.5f;
								if (v == VOICE_KICK) {
									totalHits = (roll < 0.6f) ? 2 : kickMaxHits;
									intervalScale = 1.f / (float)totalHits;
								} else {
									// Burst variety: mix of shorter/slower bursts plus
									// occasional longer ones. No faster than existing max.
									if (roll < 0.08f) {
										// Slowest double: hits at full clock speed
										totalHits = 2;
										intervalScale = 1.0f;
									} else if (roll < 0.22f) {
										// Slow double: 3/4 clock speed
										totalHits = 2;
										intervalScale = 0.75f;
									} else if (roll < 0.42f) {
										// Standard doublet
										totalHits = 2;
										intervalScale = 0.5f;
									} else if (roll < 0.56f) {
										// Slow triplet: half clock speed per hit
										totalHits = 3;
										intervalScale = 0.5f;
									} else if (roll < 0.70f) {
										// Standard triplet
										totalHits = 3;
										intervalScale = 0.333333f;
									} else if (roll < 0.82f) {
										// Quad
										totalHits = 4;
										intervalScale = 0.25f;
									} else if (roll < 0.92f) {
										// Medium burst
										totalHits = wildMode ? 8 : 4;
										intervalScale = 1.f / (float)totalHits;
									} else {
										// Full burst (rare)
										totalHits = maxHits;
										intervalScale = 1.f / (float)totalHits;
									}
								}
								newRatchetIntervalSamples = std::max<uint32_t>(1u, (uint32_t)std::lround(clockIntervalSamples * intervalScale));
								newRatchetHitsRemaining = totalHits - 1;
							}
							// Cooldown: snares wait 24-36 steps (~2.5-3.8s at 142 BPM)
							// Combined with low probability, yields ~8-10s avg between snare ratchets.
							int coolSteps;
							if (v == VOICE_KICK) {
								coolSteps = 12 + (int)(random::uniform() * 4.f);
							} else if (v == VOICE_SNARE1 || v == VOICE_SNARE2) {
								coolSteps = 24 + (int)(random::uniform() * 12.f);
							} else {
								coolSteps = 6 + (int)(random::uniform() * 4.f);
							}
							// Wild shortens hat/kick cooldown but keeps snare cooldown long
							if (wildMode && v != VOICE_SNARE1 && v != VOICE_SNARE2)
								coolSteps = (int)(coolSteps * 0.6f);
							ratchetCooldown[v] = coolSteps;
						}
					}

					float dur = (v == VOICE_OHAT) ? 0.10f : 5e-3f;
					// Kick is slower by design even in wild mode
					if (v == VOICE_KICK && wildMode) dur = 8e-3f;
					if (newRatchetHitsRemaining > 0) {
						dur = ratchetPulseDuration(newRatchetIntervalSamples, args.sampleTime, dur);
					}

					// === WILD W6: Gate Length Fuzz ===
					// Randomize pulse duration for ghost hits vs accent blasts.
					// Applied before trigger, so stutter replay copies the fuzzed duration.
					if (wildMode && wildDepth > 0.10f) {
						float fuzzRange = 0.4f + wildDepth * 2.6f; // 0.4x–3.0x at max
						float fuzz = 0.4f + random::uniform() * (fuzzRange - 0.4f);
						dur *= fuzz;
						if (dur < 1e-4f) dur = 1e-4f; // floor: never fully silent
						if (dur > 0.25f) dur = 0.25f;  // ceil: no held gates longer than a 16th
					}

					if (v == VOICE_CHAT) {
						closedHatFiredThisTick = true;
					}
					triggerVoicePulse(v, dur);
					if (newRatchetHitsRemaining > 0) {
						ratchetsRemaining[v] = newRatchetHitsRemaining;
						ratchetIntervalSamples[v] = newRatchetIntervalSamples;
						ratchetSamplesRemaining[v] = newRatchetIntervalSamples;
					}
				}

				// Hard cuts drop a step, but only replaying stutter voices freeze in place.
				if (!voiceIsReplaying) {
					voiceStep[v]++;
					if (voiceStep[v] >= baseVP.numSteps) {
						voiceStep[v] = 0;
					}
				}
			}
		}

		// --- Update outputs ---
		float dt = args.sampleTime;
		uint32_t maxClockSamples = (uint32_t)std::lround(args.sampleRate * 10.f);
		if (samplesSinceLastClock < maxClockSamples) {
			samplesSinceLastClock++;
		}

		// Fill CV: also process falling edge per-sample (not just on clock)
		// so fill fires promptly when the gate drops.
		if (inputs[FILL_INPUT].isConnected()) {
			float fillV = inputs[FILL_INPUT].getVoltage();
			bool gateHigh = (fillV > 1.0f);
			if (!gateHigh && fillGateHigh) {
				fillGateHigh = false;
				sparseMode = 0;
				float intensity = clamp(fillLastVoltage / 10.f, 0.f, 1.f);
				scheduleTransitionFill(intensity);
			}
		}

		for (int v = 0; v < NUM_VOICES; v++) {
			if (ratchetsRemaining[v] <= 0) continue;
			if (ratchetSamplesRemaining[v] > 0) {
				ratchetSamplesRemaining[v]--;
			}
			if (ratchetSamplesRemaining[v] == 0) {
				float trigDur = ratchetPulseDuration(ratchetIntervalSamples[v], args.sampleTime, 5e-3f);
				triggerVoicePulse(v, trigDur);
				ratchetsRemaining[v]--;
				if (ratchetsRemaining[v] > 0) {
					ratchetSamplesRemaining[v] = ratchetIntervalSamples[v];
				}
				else {
					clearRatchetForVoice(v);
				}
			}
		}

		// Sparse mode voice gating: 0=all, 1=hats-only, 2=silence, 3=kick-only, 4=kick+hats
		bool kickPlays  = (sparseMode == 0 || sparseMode == 3 || sparseMode == 4);
		bool snarePlays = (sparseMode == 0);
		bool hatPlays   = (sparseMode == 0 || sparseMode == 1 || sparseMode == 4);
		outputs[KICK_OUTPUT].setVoltage(kickPlays && pulseGens[VOICE_KICK].process(dt) ? 10.f : 0.f);
		outputs[SNARE1_OUTPUT].setVoltage(snarePlays && pulseGens[VOICE_SNARE1].process(dt) ? 10.f : 0.f);
		outputs[SNARE2_OUTPUT].setVoltage(snarePlays && pulseGens[VOICE_SNARE2].process(dt) ? 10.f : 0.f);
		outputs[CLOSED_HAT_OUTPUT].setVoltage(hatPlays && pulseGens[VOICE_CHAT].process(dt) ? 10.f : 0.f);
		outputs[OPEN_HAT_OUTPUT].setVoltage(hatPlays && pulseGens[VOICE_OHAT].process(dt) ? 10.f : 0.f);
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

#ifdef METAMODULE
		json_object_set_new(rootJ, "morphworx_version", json_string(MORPHWORX_VERSION_STRING));
#endif

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {

#ifdef METAMODULE
		json_t* verJ = json_object_get(rootJ, "morphworx_version");
		if (verJ && json_is_string(verJ)) {
			const char* saved = json_string_value(verJ);
			if (!saved || std::string(saved) != std::string(MORPHWORX_VERSION_STRING)) {
				return;
			}
		}
#endif
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

		// WILD toggle switch — to the right of the MUTATE knob, same row
		const float wildSwitchX = knobRightX + 8.5f;
		const float wildSwitchY = knobY;
		addParam(createParamCentered<CKSS>(mm2px(Vec(wildSwitchX, wildSwitchY)), module, DrumTrigger::WILD_MODE_PARAM));
		#ifndef METAMODULE
		addChild(createLabel(Vec(wildSwitchX, wildSwitchY - 4.5f), "WILD", 7.5f, nvgRGB(0xfe, 0x28, 0x16)));
		#endif

		// Inputs row (CLK / FILL / RST / MCV) — 4 ports, tighter spacing
		const float inRowLeft = 8.5f;
		const float inSpacing = 11.0f;
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft, ioRowY)).plus(portShiftPx), module, DrumTrigger::CLOCK_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing, ioRowY)).plus(portShiftPx), module, DrumTrigger::FILL_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing * 2.f, ioRowY)).plus(portShiftPx), module, DrumTrigger::RESET_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing * 3.f, ioRowY)).plus(portShiftPx), module, DrumTrigger::MUTATE_INPUT));
		#ifndef METAMODULE
		addChild(createLabel(Vec(inRowLeft + inSpacing, ioRowY - 5.0f), "FILL", 7.0f, nvgRGB(0xfe, 0x28, 0x16)));
		#endif

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
