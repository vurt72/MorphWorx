#include "plugin.hpp"
#include "PatternData.hpp"
#include "Quantizer.hpp"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#else
#include <span>
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
// Outputs: KICK, SNARE1, SNARE2, CLOSED_HAT, OPEN_HAT, RIDE/CRASH
// Params:  PATTERN_SELECT (0-129), MUTATE_AMOUNT (0.0-1.0), WILD_MODE (0/1)
// ============================================================================

// Number of segments per voice for DNA swap
static const int NUM_SEGMENTS = 4;
static const int BASS_CYCLE_CLOCKS = 64;
static const int BASS_WILD_WINDOW_CLOCKS = BASS_CYCLE_CLOCKS / 2;
static const float BASS_GLIDE_SECONDS = 0.012f;
static const float BASS_BASE_VOLTAGE = -1.f;
static const float BASS_TRIGGER_SECONDS = 0.008f;  // 8ms — long enough for Minimalith and other envelope-based synths

// Fast exp approximation (file-local; same formulation as SlideWyrm.cpp)
static inline float fastExp2Approx(float x) {
	union { float f; int32_t i; } u;
	float xf = x + 127.f;
	u.i = (int32_t)(xf * (float)(1 << 23));
	return u.f;
}
static inline float fastExpApprox(float x) {
	return fastExp2Approx(x * 1.44269504089f);
}

struct BassScaleOption {
	const char* name;
	const slidewyrm::Scale* scale;
};

static const BassScaleOption BASS_SCALE_OPTIONS[] = {
	{"3b7+", &slidewyrm::scales[30]},
	{"Minor (Aeolian)", &slidewyrm::scales[2]},
	{"Phrygian", &slidewyrm::scales[7]},
	{"Dorian", &slidewyrm::scales[6]},
	{"Chromatic", &slidewyrm::scales[0]},
	{"Diminished (Half-Whole)", &slidewyrm::scales[14]},
	{"Blues Scale", &slidewyrm::scales[5]},
	{"Minor Pentatonic", &slidewyrm::scales[4]},
	{"Locrian", &slidewyrm::scales[10]},
	{"Harmonic Minor", &slidewyrm::scales[11]}
};
static const int BASS_SCALE_OPTION_COUNT = (int)(sizeof(BASS_SCALE_OPTIONS) / sizeof(BASS_SCALE_OPTIONS[0]));

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
	if (p <= 5)  return "AMEN / BREAKBEAT";
	if (p <= 7)  return "JUNGLE";
	if (p <= 9)  return "BREAKCORE";
	if (p <= 12) return "AMEN / BREAKBEAT";
	if (p <= 15) return "JUNGLE";
	if (p <= 19) return "BREAKCORE";
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

enum GrooveFamily {
	GROOVE_OTHER,
	GROOVE_AMEN,
	GROOVE_JUNGLE,
	GROOVE_BREAKCORE,
};

enum ValidationProfile {
	VALIDATION_PROFILE_BYPASS,
	VALIDATION_PROFILE_BREAKBEAT,
};

static GrooveFamily getGrooveFamilyForPattern(int p) {
	if (p <= 5)  return GROOVE_AMEN;
	if (p <= 7)  return GROOVE_JUNGLE;
	if (p <= 9)  return GROOVE_BREAKCORE;
	if (p <= 12) return GROOVE_AMEN;
	if (p <= 15) return GROOVE_JUNGLE;
	if (p <= 19) return GROOVE_BREAKCORE;
	switch (p) {
		case 20: return GROOVE_AMEN;
		case 21: return GROOVE_JUNGLE;
		case 22: return GROOVE_JUNGLE;
		case 23: return GROOVE_AMEN;
		case 24: return GROOVE_BREAKCORE;
		case 25: return GROOVE_JUNGLE;
		case 26: return GROOVE_AMEN;
		case 27: return GROOVE_BREAKCORE;
		case 28: return GROOVE_JUNGLE;
		case 29: return GROOVE_AMEN;
		case 30: return GROOVE_BREAKCORE;
		case 31: return GROOVE_JUNGLE;
		case 32: return GROOVE_AMEN;
		case 33: return GROOVE_BREAKCORE;
		case 34: return GROOVE_AMEN;
		case 35: return GROOVE_JUNGLE;
		case 36: return GROOVE_JUNGLE;
		case 37: return GROOVE_BREAKCORE;
		case 38: return GROOVE_AMEN;
		case 39: return GROOVE_AMEN;
		default: break;
	}
	if (p <= 44) return GROOVE_JUNGLE;
	return GROOVE_OTHER;
}

static const float AMEN_KICK_MASK[16] = {
	1.00f, 0.48f, 0.10f, 0.16f,
	0.88f, 0.34f, 0.10f, 0.22f,
	0.96f, 0.40f, 0.10f, 0.18f,
	0.82f, 0.28f, 0.16f, 0.24f,
};

static const float JUNGLE_KICK_MASK[16] = {
	1.00f, 0.18f, 0.08f, 0.12f,
	0.22f, 0.08f, 0.30f, 0.12f,
	0.72f, 0.14f, 0.48f, 0.12f,
	0.22f, 0.08f, 0.34f, 0.12f,
};

static const float BREAKCORE_KICK_MASK[16] = {
	0.82f, 0.34f, 0.28f, 0.52f,
	0.40f, 0.26f, 0.54f, 0.24f,
	0.74f, 0.30f, 0.38f, 0.58f,
	0.36f, 0.28f, 0.50f, 0.30f,
};

static const float AMEN_SNARE_MASK[16] = {
	0.02f, 0.05f, 1.00f, 0.08f,
	0.02f, 0.05f, 1.00f, 0.08f,
	0.02f, 0.05f, 1.00f, 0.08f,
	0.02f, 0.05f, 1.00f, 0.08f,
};

static const float JUNGLE_SNARE_MASK[16] = {
	0.02f, 0.05f, 0.08f, 0.10f,
	1.00f, 0.08f, 0.08f, 0.10f,
	0.02f, 0.05f, 0.08f, 0.10f,
	1.00f, 0.08f, 0.08f, 0.10f,
};

static const float BREAKCORE_SNARE_MASK[16] = {
	0.18f, 0.24f, 0.46f, 0.26f,
	0.22f, 0.18f, 0.36f, 0.20f,
	0.20f, 0.28f, 0.42f, 0.26f,
	0.18f, 0.22f, 0.30f, 0.20f,
};

static const float AMEN_GHOST_MASK[16] = {
	0.05f, 0.34f, 0.12f, 0.30f,
	0.08f, 0.16f, 0.34f, 0.18f,
	0.05f, 0.34f, 0.12f, 0.30f,
	0.08f, 0.16f, 0.34f, 0.18f,
};

static const float JUNGLE_GHOST_MASK[16] = {
	0.04f, 0.16f, 0.12f, 0.22f,
	0.12f, 0.20f, 0.10f, 0.16f,
	0.04f, 0.16f, 0.12f, 0.22f,
	0.12f, 0.20f, 0.10f, 0.16f,
};

static const float BREAKCORE_GHOST_MASK[16] = {
	0.16f, 0.28f, 0.24f, 0.34f,
	0.18f, 0.24f, 0.30f, 0.22f,
	0.14f, 0.30f, 0.26f, 0.32f,
	0.18f, 0.26f, 0.28f, 0.20f,
};

static int wrapStepDistance(int a, int b, int mod) {
	if (mod <= 0) return std::abs(a - b);
	int diff = std::abs(a - b);
	return std::min(diff, mod - diff);
}

static int getGrooveMaskStep(int step, int numSteps) {
	if (numSteps <= 1) return 0;
	int maskStep = (step * 16) / numSteps;
	return math::clamp(maskStep, 0, 15);
}

static bool isStrongBeatStep(int step, int numSteps) {
	if (numSteps <= 1) return true;
	int divisions = (numSteps >= 8) ? 4 : 2;
	int spacing = std::max(1, numSteps / divisions);
	int tolerance = (numSteps >= 32) ? 1 : 0;
	for (int i = 0; i < divisions; ++i) {
		if (wrapStepDistance(step, i * spacing, numSteps) <= tolerance) {
			return true;
		}
	}
	return false;
}

static bool isBarAnchorStep(int step, int numSteps) {
	if (numSteps <= 1) return true;
	if (step == 0) return true;
	if ((numSteps % 2) == 0) {
		int tolerance = (numSteps >= 32) ? 1 : 0;
		if (wrapStepDistance(step, numSteps / 2, numSteps) <= tolerance) {
			return true;
		}
	}
	return false;
}

static bool isFamilySnareAnchorStep(GrooveFamily family, int step, int numSteps) {
	if (numSteps <= 3) return false;
	const int* maskTargets = nullptr;
	int targetCount = 0;
	static const int amenTargets[] = {2, 6, 10, 14};
	static const int jungleTargets[] = {4, 12};
	if (family == GROOVE_AMEN) {
		maskTargets = amenTargets;
		targetCount = 4;
	}
	else if (family == GROOVE_JUNGLE) {
		maskTargets = jungleTargets;
		targetCount = 2;
	}
	else {
		return false;
	}
	int tolerance = (numSteps >= 32) ? 1 : 0;
	for (int i = 0; i < targetCount; ++i) {
		int target = (int)std::lround((maskTargets[i] / 16.f) * numSteps) % numSteps;
		if (wrapStepDistance(step, target, numSteps) <= tolerance) {
			return true;
		}
	}
	return false;
}

static bool isProtectedGrooveAnchorStep(GrooveFamily family, int voice, int step, int numSteps) {
	if (voice == VOICE_KICK) {
		return isBarAnchorStep(step, numSteps);
	}
	if (voice == VOICE_SNARE1) {
		return isFamilySnareAnchorStep(family, step, numSteps);
	}
	return false;
}

static const float* getKickMaskForFamily(GrooveFamily family) {
	switch (family) {
		case GROOVE_AMEN: return AMEN_KICK_MASK;
		case GROOVE_JUNGLE: return JUNGLE_KICK_MASK;
		case GROOVE_BREAKCORE: return BREAKCORE_KICK_MASK;
		default: return nullptr;
	}
}

static const float* getSnareMaskForFamily(GrooveFamily family) {
	switch (family) {
		case GROOVE_AMEN: return AMEN_SNARE_MASK;
		case GROOVE_JUNGLE: return JUNGLE_SNARE_MASK;
		case GROOVE_BREAKCORE: return BREAKCORE_SNARE_MASK;
		default: return nullptr;
	}
}

static const float* getGhostMaskForFamily(GrooveFamily family) {
	switch (family) {
		case GROOVE_AMEN: return AMEN_GHOST_MASK;
		case GROOVE_JUNGLE: return JUNGLE_GHOST_MASK;
		case GROOVE_BREAKCORE: return BREAKCORE_GHOST_MASK;
		default: return nullptr;
	}
}

static float getGrooveTargetProbability(GrooveFamily family, int voice, int step, int numSteps) {
	int maskStep = getGrooveMaskStep(step, numSteps);
	const float* kickMask = getKickMaskForFamily(family);
	const float* snareMask = getSnareMaskForFamily(family);
	const float* ghostMask = getGhostMaskForFamily(family);

	switch (voice) {
		case VOICE_KICK:
			return kickMask ? kickMask[maskStep] : 0.18f;
		case VOICE_SNARE1:
			return snareMask ? snareMask[maskStep] : 0.12f;
		case VOICE_SNARE2:
			return ghostMask ? ghostMask[maskStep] : 0.10f;
		case VOICE_CHAT:
			if (family == GROOVE_BREAKCORE) {
				return (step & 1) ? 0.44f : 0.58f;
			}
			if (family == GROOVE_JUNGLE) {
				return (step & 1) ? 0.56f : 0.86f;
			}
			return (step & 1) ? 0.38f : 0.84f;
		case VOICE_OHAT:
			return (family == GROOVE_BREAKCORE) ? 0.18f : 0.10f;
		case VOICE_RIDE:
			if (family == GROOVE_BREAKCORE) {
				return (maskStep % 4 == 0) ? 0.38f : 0.14f;
			}
			return (maskStep % 2 == 0) ? 0.30f : 0.12f;
		default:
			return 0.f;
	}
}

struct Trigonomicon : Module {

	enum ParamId {
		CLOCK_RES_PARAM,
		PATTERN_SELECT_PARAM,
		BASS_SCALE_PARAM,
		MUTATE_PARAM,
		WILD_MODE_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		MUTATE_INPUT,
		FILL_INPUT,
		CLOCK_RES_INPUT,
		BASS_OCT_INPUT,
		PATTERN_SELECT_INPUT,
		INPUTS_LEN
	};

	enum OutputId {
		KICK_OUTPUT,
		SNARE1_OUTPUT,
		SNARE2_OUTPUT,
		CLOSED_HAT_OUTPUT,
		OPEN_HAT_OUTPUT,
		RIDE_CRASH_OUTPUT,
		ACCENT_POLY_OUTPUT,
		BASS_TRIG_OUTPUT,
		BASS_CV_OUTPUT,
		OUTPUTS_LEN
	};

	enum BassSourceRecipe {
		BASS_SOURCE_KICK,
		BASS_SOURCE_KICK_SNARE1,
		BASS_SOURCE_CLOSED_HAT,
		BASS_SOURCE_SNARES,
		BASS_SOURCE_COUNT
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
	dsp::PulseGenerator bassTriggerPulse;
	float accentHoldVoltage[NUM_VOICES] = {};
	float ratchetAccentVoltage[NUM_VOICES] = {};
	bool pendingVoicePulse[NUM_VOICES] = {};
	uint32_t pendingVoicePulseSamples[NUM_VOICES] = {};
	float pendingVoicePulseDur[NUM_VOICES] = {};
	float pendingVoiceAccentVoltage[NUM_VOICES] = {};
	uint32_t phraseTimingDelaySamples[NUM_VOICES] = {};
	uint32_t phraseTimingCycle = 0;

	// Per-voice step counters — each voice wraps at its own length
	int voiceStep[NUM_VOICES] = {};

	// --- Tracker FX State (transient only; do not serialize) ---
	int stutterStepsRemaining = 0;
	bool stutterCache[NUM_VOICES] = {};

	// --- Ratchet state (transient only; do not serialize) ---
	uint32_t samplesSinceLastClock = 4800;
	uint32_t clockIntervalSamples = 4800;
	int clockDivideCounter = 0;
	int pendingClockSubticks = 0;
	uint32_t clockSubtickSamplesRemaining = 0;
	uint32_t clockSubtickIntervalSamples = 0;
	int ratchetsRemaining[NUM_VOICES] = {};
	uint32_t ratchetSamplesRemaining[NUM_VOICES] = {};
	uint32_t ratchetIntervalSamples[NUM_VOICES] = {};

	// Previous pattern index for detecting pattern changes
	int lastPattern = -1;
	int pendingPatternCvIndex = -1;
	int patternStepsSinceChange = 0;

	// --- Mutation state ---

	// Working buffer — segment-swapped pattern (rebuilt on quantized boundaries)
	Pattern mutatedPattern;
	Pattern correctedBasePattern;
	Pattern correctedMutatedPattern;

	// Donor pattern for segment DNA swap
	int donorPatternIndex = -1;

	// Pre-rolled random thresholds per segment/voice.
	// A segment swaps when segmentAmount > threshold.
	// Re-rolled on hard mutation or pattern change.
	float segmentThresholds[NUM_VOICES][NUM_SEGMENTS] = {};

	// Previous CV value for hard mutation detection
	float prevMutateCV = 0.f;
	// Normalized mutate level [0..1] — cached from control-rate read for bass density scaling.
	float currentMutateNorm = 0.f;
	bool hardMutArmed = true;
	bool hardMutPending = false;

	// Cached smoothstep curve amounts (updated at control rate)
	float microAmount = 0.f;
	float blendAmount = 0.f;
	float segmentAmount = 0.f;
	float chaosAmount = 0.f;
	float wildDepth = 0.f;  // 0..1, only > 0 when WILD_MODE_PARAM is ON
	bool wildMode = false;

	// Per-bar snare rest flags — WILD mode occasionally mutes an entire bar
	// of snare to create phrase-level breathing room.
	bool snareRestThisBar[NUM_VOICES] = {};
	int wildKickBarCounter = 0;
	int wildSnareRestBarsRemaining = 0;
	int wildStutterCooldownSteps = 0;
	int wildCutCooldownSteps = 0;

	// Control rate divider (~every 32 samples)
	int controlRateCounter = 0;
	static const int CONTROL_RATE_DIV = 32;
	char debugStepText[64] = "";

	// Flag: mutation buffer needs rebuild
	bool mutationDirty = true;
	bool mutationPending = true;
	float lastSegmentAmountBuilt = -1.f;

	// Quantize segment-buffer rebuilds to clock edges (default: every 2 edges)
	int mutateClockDiv = 2;
	int mutateClockCounter = 0;

	// --- Ratchet cooldown (clock steps remaining before a voice can ratchet again) ---
	int ratchetCooldown[NUM_VOICES] = {};

	// --- Fill CV state ---
	// sparseMode: 0=normal, 1=hats-only, 2=silence, 3=kick-only, 4=kick+hats
	// Driven by FILL_INPUT CV gate+voltage (no longer random).
	int sparseMode = 0;
	bool fillGateHigh = false;   // true while FILL CV is above threshold
	float fillLastVoltage = 0.f; // voltage at time of gate, used for fill intensity

	// --- Bassline S&H state ---
	int bassSourceRecipe = BASS_SOURCE_KICK;
	int bassCycleClock = 0;
	float bassHatThinProbability = 0.60f;
	float bassCurrentCv = 0.f;
	float bassTargetCv = 0.f;
	float bassSlideStartCv = 0.f;
	int bassPitchIndex = 0;
	uint32_t bassSamplesSinceTrigger = 0xffffffffu;
	uint32_t bassMinTriggerSpacingSamples = 1u;
	uint32_t bassSlideSamplesRemaining = 0u;
	uint32_t bassSlideTotalSamples = 0u;
	uint32_t bassOffbeatSamplesRemaining = 0u;  // unused, kept for JSON compat
	bool bassStateInitialized = false;
	bool bassWildDoubleTimeActive = false;
	bool bassOffbeatPending = false;  // unused, kept for JSON compat
	// Wild skip pattern: every bassWildSkipEvery-th candidate note is silenced.
	// 0 = off, 2 = skip every other, 4 = skip every 4th.
	int bassWildSkipEvery = 0;
	int bassWildSkipCounter = 0;
	// Octave transpose from CV input: applied at musical boundaries (every 32 clocks).
	// Both values are integer octave steps (-2..+1); active is what's currently audible.
	float bassOctavePending = 0.f;
	float bassOctaveActive  = 0.f;
	// SlideWyrm-inspired: per-note ±octave decoration and selective exponential portamento.
	bool  bassSlideActive        = false;  // true while exponential portamento is converging
	float bassSlideCoeff         = 1.0f;   // precomputed exp convergence coefficient (1.0 = instant)
	int   bassNoteOctDecoration  = 0;      // per-note octave decoration: -1 / 0 / +1

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

	int getBassScaleIndex() {
		return math::clamp((int)std::lround(params[BASS_SCALE_PARAM].getValue()), 0, BASS_SCALE_OPTION_COUNT - 1);
	}

	const slidewyrm::Scale& getBassScale() {
		return *BASS_SCALE_OPTIONS[getBassScaleIndex()].scale;
	}

	int getBassBaseDegreeCount() {
		return getBassScale().num_notes + 2;
	}

	int getBassMaxDegreeIndex(bool extendedRange) {
		int degreeCount = getBassBaseDegreeCount();
		if (extendedRange) {
			degreeCount += std::max(1, (int)std::floor((float)degreeCount * 0.20f));
		}
		return std::max(0, degreeCount - 1);
	}

	int bassDegreeIndexToSemitone(int index) {
		const slidewyrm::Scale& scale = getBassScale();
		int safeIndex = std::max(0, index);
		int octave = safeIndex / scale.num_notes;
		int degree = safeIndex % scale.num_notes;
		return octave * 12 + scale.notes[degree];
	}

	int clampBassPitchIndex(int index, bool extendedRange) {
		return math::clamp(index, 0, getBassMaxDegreeIndex(extendedRange));
	}

	int nearestBassIndexForSemitone(int targetSemitone, bool extendedRange) {
		int bestIndex = 0;
		int bestDistance = INT_MAX;
		int maxIndex = getBassMaxDegreeIndex(extendedRange);
		for (int index = 0; index <= maxIndex; ++index) {
			int distance = std::abs(bassDegreeIndexToSemitone(index) - targetSemitone);
			if (distance < bestDistance) {
				bestDistance = distance;
				bestIndex = index;
			}
		}
		return bestIndex;
	}

	// CV always snaps to target immediately — trigger and CV are always sample-accurate.
	// (Slide removed: it caused pitch/trigger misalignment on downstream synths.)
	float chooseBassGlideSeconds(float /*sampleRate*/, bool /*doubleTime*/, bool /*isOffbeat*/) const {
		return 0.f;
	}

	// SlideWyrm-inspired: probability of a portamento (slide) on a given bass trigger,
	// weighted by drum source recipe. Hat-driven patterns are more fluid; kick-driven
	// patterns stay punchy and staccato.
	float getBaseSlideProbability() const {
		switch (bassSourceRecipe) {
			case BASS_SOURCE_KICK:        return 0.15f;
			case BASS_SOURCE_KICK_SNARE1: return 0.20f;
			case BASS_SOURCE_SNARES:      return 0.25f;
			case BASS_SOURCE_CLOSED_HAT:  return 0.30f;
			default:                      return 0.20f;
		}
	}

	void beginBassSlide(float sampleRate, bool doubleTime, bool isOffbeat) {
		float glideSeconds = chooseBassGlideSeconds(sampleRate, doubleTime, isOffbeat);
		if (!bassStateInitialized || glideSeconds <= 0.f) {
			bassCurrentCv = bassTargetCv;
			bassSlideStartCv = bassCurrentCv;
			bassSlideSamplesRemaining = 0u;
			bassSlideTotalSamples = 0u;
			return;
		}
		bassSlideStartCv = bassCurrentCv;
		bassSlideTotalSamples = std::max<uint32_t>(1u, (uint32_t)std::lround(glideSeconds * sampleRate));
		bassSlideSamplesRemaining = bassSlideTotalSamples;
	}

	void beginBassWildWindow() {
		// Latch octave shift at this 2-bar boundary — musically the right moment.
		bassOctaveActive = bassOctavePending;
		bassWildDoubleTimeActive = wildMode && (random::uniform() < 0.40f);
		if (bassWildDoubleTimeActive) {
			// Randomly pick a skip pattern:
			//   50% — skip every other note  (play, SKIP, play, SKIP, ...)
			//   50% — skip every 4th note    (play, play, play, SKIP, ...)
			bassWildSkipEvery = (random::uniform() < 0.5f) ? 2 : 4;
			bassWildSkipCounter = 0;
		} else {
			bassWildSkipEvery = 0;
			bassWildSkipCounter = 0;
		}
		// Clear legacy fields.
		bassOffbeatPending = false;
		bassOffbeatSamplesRemaining = 0u;
	}

	int chooseBassSourceRecipe() const {
		float roll = random::uniform();
		if (roll < 0.50f) return BASS_SOURCE_KICK;
		if (roll < 0.70f) return BASS_SOURCE_KICK_SNARE1;
		if (roll < 0.85f) return BASS_SOURCE_CLOSED_HAT;
		return BASS_SOURCE_SNARES;
	}

	void startBassCycle() {
		bassSourceRecipe = chooseBassSourceRecipe();
		bassHatThinProbability = 0.50f + random::uniform() * 0.20f;
		bassHatThinProbability = math::clamp(bassHatThinProbability, 0.50f, 0.70f);
	}

	int chooseBassAnchorIndex(bool extendedRange) {
		float roll = random::uniform();
		switch (bassSourceRecipe) {
			case BASS_SOURCE_KICK:
				if (roll < 0.48f) return nearestBassIndexForSemitone(0, extendedRange);
				if (roll < 0.72f) return nearestBassIndexForSemitone(3, extendedRange);
				if (roll < 0.88f) return nearestBassIndexForSemitone(7, extendedRange);
				return nearestBassIndexForSemitone(10, extendedRange);
			case BASS_SOURCE_KICK_SNARE1:
				if (roll < 0.28f) return nearestBassIndexForSemitone(0, extendedRange);
				if (roll < 0.48f) return nearestBassIndexForSemitone(3, extendedRange);
				if (roll < 0.66f) return nearestBassIndexForSemitone(5, extendedRange);
				if (roll < 0.84f) return nearestBassIndexForSemitone(7, extendedRange);
				return nearestBassIndexForSemitone(12, extendedRange);
			case BASS_SOURCE_CLOSED_HAT:
				if (roll < 0.14f) return nearestBassIndexForSemitone(0, extendedRange);
				if (roll < 0.28f) return nearestBassIndexForSemitone(1, extendedRange);
				if (roll < 0.44f) return nearestBassIndexForSemitone(3, extendedRange);
				if (roll < 0.60f) return nearestBassIndexForSemitone(5, extendedRange);
				if (roll < 0.76f) return nearestBassIndexForSemitone(7, extendedRange);
				if (roll < 0.90f) return nearestBassIndexForSemitone(10, extendedRange);
				return nearestBassIndexForSemitone(15, extendedRange);
			case BASS_SOURCE_SNARES:
			default:
				if (roll < 0.22f) return nearestBassIndexForSemitone(0, extendedRange);
				if (roll < 0.40f) return nearestBassIndexForSemitone(1, extendedRange);
				if (roll < 0.58f) return nearestBassIndexForSemitone(3, extendedRange);
				if (roll < 0.76f) return nearestBassIndexForSemitone(7, extendedRange);
				if (roll < 0.90f) return nearestBassIndexForSemitone(10, extendedRange);
				return nearestBassIndexForSemitone(12, extendedRange);
		}
	}

	int chooseNextBassPitchIndex(bool extendedRange, bool doubleTime) {
		if (!bassStateInitialized) {
			return nearestBassIndexForSemitone(0, extendedRange);
		}

		float repeatChance = 0.40f;
		float neighborChance = 0.32f;
		float anchorChance = 0.22f;
		int maxNeighborSpan = 1;

		switch (bassSourceRecipe) {
			case BASS_SOURCE_KICK:
				repeatChance = 0.52f;
				neighborChance = 0.26f;
				anchorChance = 0.18f;
				maxNeighborSpan = 1;
				break;
			case BASS_SOURCE_KICK_SNARE1:
				repeatChance = 0.34f;
				neighborChance = 0.34f;
				anchorChance = 0.24f;
				maxNeighborSpan = 2;
				break;
			case BASS_SOURCE_CLOSED_HAT:
				repeatChance = 0.24f;
				neighborChance = 0.40f;
				anchorChance = 0.24f;
				maxNeighborSpan = 2;
				break;
			case BASS_SOURCE_SNARES:
			default:
				repeatChance = 0.28f;
				neighborChance = 0.36f;
				anchorChance = 0.24f;
				maxNeighborSpan = 2;
				break;
		}

		if (doubleTime) {
			repeatChance *= 0.82f;
			neighborChance += 0.10f;
			anchorChance += 0.04f;
			maxNeighborSpan += 1;
		}

		float roll = random::uniform();
		if (roll < repeatChance) {
			return clampBassPitchIndex(bassPitchIndex, extendedRange);
		}

		if (roll < repeatChance + neighborChance) {
			int span = (random::uniform() < 0.72f) ? 1 : maxNeighborSpan;
			int delta = (random::uniform() < 0.5f) ? -span : span;
			return clampBassPitchIndex(bassPitchIndex + delta, extendedRange);
		}

		if (roll < repeatChance + neighborChance + anchorChance) {
			return chooseBassAnchorIndex(extendedRange);
		}

		int jump = (random::uniform() < 0.5f) ? -(doubleTime ? 4 : 3) : (doubleTime ? 4 : 3);
		return clampBassPitchIndex(bassPitchIndex + jump, extendedRange);
	}

	float bassPitchVoltageForIndex(int index) {
		return BASS_BASE_VOLTAGE + (float)bassDegreeIndexToSemitone(index) / 12.f;
	}

	void triggerBassSampleHold(float sampleRate, bool extendedRange, bool isDoubleTime) {
		bassPitchIndex = chooseNextBassPitchIndex(extendedRange, isDoubleTime);
		bassTargetCv = bassPitchVoltageForIndex(bassPitchIndex);

		// Per-note ±octave decoration (TB-303 style spontaneous octave leap).
		// 12.5% chance up, 12.5% down, 75% unchanged.
		{
			float decorRoll = random::uniform();
			if (decorRoll < 0.125f)       bassNoteOctDecoration = -1;
			else if (decorRoll < 0.25f)   bassNoteOctDecoration = +1;
			else                          bassNoteOctDecoration =  0;
			bassTargetCv += (float)bassNoteOctDecoration;
		}

		// Selective portamento: some triggers glide (TB-303 slide), most snap instantly.
		// Probability is source-recipe weighted; on slide notes the gate fires while
		// CV is still at the previous pitch and converges exponentially (50ms).
		{
			float slideRoll = random::uniform();
			bassSlideActive = bassStateInitialized && (slideRoll < getBaseSlideProbability());
			if (bassSlideActive) {
				// CV stays at current value — portamento starts from the previous pitch.
				static const float BASS_PORTAMENTO_SECONDS = 0.050f;
				bassSlideCoeff = 1.f - fastExpApprox(-1.f / (BASS_PORTAMENTO_SECONDS * sampleRate));
			} else {
				// Instant snap — downstream synths always open on the exact target pitch.
				bassCurrentCv = bassTargetCv;
				bassSlideCoeff = 1.0f;
			}
		}

		bassSlideStartCv = bassCurrentCv;
		bassStateInitialized = true;
		// Gate length: 50% of one clock step, clamped to at least 8ms so
		// envelope-based synths (e.g. Minimalith) reliably respond.
		float clockSeconds = sampleRate > 0.f ? (float)clockIntervalSamples / sampleRate : BASS_TRIGGER_SECONDS;
		float gateSeconds = std::max(BASS_TRIGGER_SECONDS, clockSeconds * 0.5f);
		bassTriggerPulse.trigger(gateSeconds);
		beginBassSlide(sampleRate, isDoubleTime, false);
		bassSamplesSinceTrigger = 0;
	}

	void resetBasslineState() {
		bassCycleClock = 0;
		bassPitchIndex = nearestBassIndexForSemitone(0, false);
		bassCurrentCv = bassPitchVoltageForIndex(0);
		bassTargetCv = bassCurrentCv;
		bassSlideStartCv = bassCurrentCv;
		bassSamplesSinceTrigger = 0xffffffffu;
		bassMinTriggerSpacingSamples = 1u;
		bassSlideSamplesRemaining = 0u;
		bassSlideTotalSamples = 0u;
		bassOffbeatPending = false;
		bassOffbeatSamplesRemaining = 0u;
		bassWildDoubleTimeActive = false;
		bassWildSkipEvery = 0;
		bassWildSkipCounter = 0;
		bassOctavePending = 0.f;
		bassOctaveActive  = 0.f;
		bassSlideActive       = false;
		bassSlideCoeff        = 1.0f;
		bassNoteOctDecoration = 0;
		bassStateInitialized = false;
		startBassCycle();
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

	void clearScheduledVoicePulses() {
		for (int v = 0; v < NUM_VOICES; ++v) {
			pendingVoicePulse[v] = false;
			pendingVoicePulseSamples[v] = 0u;
			pendingVoicePulseDur[v] = 0.f;
			pendingVoiceAccentVoltage[v] = accentHoldVoltage[v];
		}
	}

	void clearWildPhraseState() {
		wildKickBarCounter = 0;
		wildSnareRestBarsRemaining = 0;
		wildStutterCooldownSteps = 0;
		wildCutCooldownSteps = 0;
		phraseTimingCycle = 0;
		for (int v = 0; v < NUM_VOICES; ++v) {
			phraseTimingDelaySamples[v] = 0u;
		}
		for (int v = 0; v < NUM_VOICES; v++) {
			snareRestThisBar[v] = false;
		}
	}

	static uint32_t hash32(uint32_t x) {
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	static float hash01(uint32_t seed) {
		return (hash32(seed) & 0x00ffffffu) * (1.f / 16777216.f);
	}

	float phraseTimingBaseMs(int voice, GrooveFamily family) const {
		switch (voice) {
			case VOICE_KICK:
				return (family == GROOVE_BREAKCORE) ? 0.10f : 0.18f;
			case VOICE_SNARE1:
				return (family == GROOVE_BREAKCORE) ? 0.18f : 0.42f;
			case VOICE_SNARE2:
				return (family == GROOVE_BREAKCORE) ? 0.26f : 0.70f;
			case VOICE_CHAT:
				return (family == GROOVE_BREAKCORE) ? 0.35f : 0.95f;
			case VOICE_OHAT:
				return (family == GROOVE_BREAKCORE) ? 0.40f : 1.10f;
			case VOICE_RIDE:
				return (family == GROOVE_BREAKCORE) ? 0.28f : 0.90f;
			default:
				return 0.25f;
		}
	}

	void updatePhraseTimingMap(int patternIndex, GrooveFamily family, int kickStep, int kickNumSteps, float sampleRate) {
		if (kickNumSteps <= 0 || kickStep != 0) return;
		phraseTimingCycle++;
		for (int v = 0; v < NUM_VOICES; ++v) {
			float baseMs = phraseTimingBaseMs(v, family);
			float phraseJitter = 0.75f + hash01((uint32_t)patternIndex * 131u + phraseTimingCycle * 17u + (uint32_t)v * 23u) * 0.55f;
			float delayMs = baseMs * phraseJitter;
			phraseTimingDelaySamples[v] = (uint32_t)std::lround((delayMs * 1e-3f) * sampleRate);
		}
	}

	uint32_t getRoleTimingDelaySamples(int voice, bool protectedAnchor, bool strongBeat, bool replaying) const {
		float scale = 1.f;
		switch (voice) {
			case VOICE_KICK:
				scale = strongBeat ? 0.10f : 0.35f;
				break;
			case VOICE_SNARE1:
				scale = protectedAnchor ? 0.30f : 0.70f;
				break;
			case VOICE_SNARE2:
				scale = 0.95f;
				break;
			case VOICE_CHAT:
				scale = strongBeat ? 0.55f : 1.00f;
				break;
			case VOICE_OHAT:
				scale = strongBeat ? 0.70f : 1.05f;
				break;
			case VOICE_RIDE:
				scale = protectedAnchor ? 0.65f : 0.95f;
				break;
			default:
				break;
		}
		if (replaying) scale *= 0.70f;
		return (uint32_t)std::lround((float)phraseTimingDelaySamples[voice] * scale);
	}

	void updateWildSnareRestState(int kickStep, int kickNumSteps) {
		if (kickNumSteps <= 0 || kickStep != 0) return;

		if (wildSnareRestBarsRemaining > 0) {
			wildSnareRestBarsRemaining--;
			if (wildSnareRestBarsRemaining <= 0) {
				for (int v = 0; v < NUM_VOICES; v++) {
					snareRestThisBar[v] = false;
				}
			}
		}

		wildKickBarCounter++;

		if (!wildMode || wildDepth < 0.45f || wildSnareRestBarsRemaining > 0) {
			return;
		}

		// Phrase-scoped, not per-bar: only consider snare rests every 4 kick bars.
		if ((wildKickBarCounter % 4) != 0) {
			return;
		}

		float phraseRestProb = (wildDepth - 0.45f) * 0.18f;
		if (random::uniform() >= phraseRestProb) {
			return;
		}

		for (int v = 0; v < NUM_VOICES; v++) {
			snareRestThisBar[v] = false;
		}

		// Prefer Snare2 so the main backbeat survives. Only allow Snare1 rests rarely
		// at extreme wild depth.
		int restVoice = VOICE_SNARE2;
		if (wildDepth > 0.88f && random::uniform() < 0.20f) {
			restVoice = VOICE_SNARE1;
		}

		snareRestThisBar[restVoice] = true;
		wildSnareRestBarsRemaining = 1;
	}

	static int normalizedHitLimit(int numSteps, float hitsPer32) {
		if (numSteps <= 0) return 0;
		return std::max(1, (int)std::lround((hitsPer32 * (float)numSteps) / 32.f));
	}

	static bool isStrongPatternHit(float value, float threshold) {
		return value >= threshold;
	}

	static void sanitizePatternBuffer(Pattern& pattern) {
		for (int v = 0; v < NUM_VOICES; ++v) {
			int numSteps = math::clamp(pattern.voices[v].numSteps, 0, MAX_STEPS);
			pattern.voices[v].numSteps = numSteps;
			for (int step = 0; step < numSteps; ++step) {
				pattern.voices[v].steps[step] = math::clamp(pattern.voices[v].steps[step], 0.f, 1.f);
			}
			for (int step = numSteps; step < MAX_STEPS; ++step) {
				pattern.voices[v].steps[step] = 0.f;
			}
		}
	}

	void setDebugStepText(int step, const bool fired[NUM_VOICES]) {
		std::snprintf(
			debugStepText,
			sizeof(debugStepText),
			"S%02d %c%c%c%c%c%c",
			step + 1,
			fired[VOICE_KICK] ? 'K' : '-',
			fired[VOICE_SNARE1] ? '1' : '-',
			fired[VOICE_SNARE2] ? '2' : '-',
			fired[VOICE_CHAT] ? 'H' : '-',
			fired[VOICE_OHAT] ? 'O' : '-',
			fired[VOICE_RIDE] ? 'R' : '-');
	}

	void clearDebugStepText() {
		debugStepText[0] = '\0';
	}

	float deriveAccentVoltage(int voice, float baseProbability, float resolvedProbability,
			bool protectedAnchor, bool strongBeat, bool replaying) const {
		float probabilityHint = math::clamp(baseProbability, 0.f, 1.f);
		float resolvedHint = math::clamp(resolvedProbability, 0.f, 1.f);
		float accentNorm = probabilityHint;
		switch (voice) {
			case VOICE_KICK:
				accentNorm = strongBeat ? (0.42f + probabilityHint * 0.28f)
					: (0.16f + probabilityHint * 0.38f);
				break;
			case VOICE_SNARE1:
				accentNorm = protectedAnchor ? (0.48f + probabilityHint * 0.24f)
					: (0.16f + probabilityHint * 0.42f);
				break;
			case VOICE_SNARE2:
				accentNorm = 0.06f + probabilityHint * 0.44f;
				break;
			case VOICE_CHAT:
				accentNorm = 0.10f + probabilityHint * 0.46f;
				break;
			case VOICE_OHAT:
				accentNorm = 0.14f + probabilityHint * 0.48f;
				break;
			case VOICE_RIDE:
				accentNorm = 0.18f + probabilityHint * 0.46f;
				break;
			default:
				break;
		}
		accentNorm += resolvedHint * 0.06f;
		if (protectedAnchor || strongBeat) {
			accentNorm += 0.10f;
		}
		if (replaying) {
			accentNorm *= 0.86f;
		}
		return math::clamp(accentNorm, 0.f, 1.f) * 10.f;
	}

	int getClockResolutionMode() {
		int mode = math::clamp((int)std::lround(params[CLOCK_RES_PARAM].getValue()), 0, 2);
		if (inputs[CLOCK_RES_INPUT].isConnected()) {
			float cv = math::clamp(inputs[CLOCK_RES_INPUT].getVoltage(), 0.f, 10.f);
			mode = math::clamp((int)std::lround(cv / 5.f), 0, 2);
		}
		return mode;
	}

	int getClockStepMultiplier() {
		int mode = getClockResolutionMode();
		switch (mode) {
			case 2: return 2;
			default: return 1;
		}
	}

	bool shouldAdvanceOnExternalClock() {
		if (getClockResolutionMode() != 1) {
			return true;
		}
		clockDivideCounter = (clockDivideCounter + 1) % 2;
		return clockDivideCounter == 1;
	}

	void clearClockSubticks() {
		clockDivideCounter = 0;
		pendingClockSubticks = 0;
		clockSubtickSamplesRemaining = 0;
		clockSubtickIntervalSamples = 0;
	}

	float getSubtickSwingFraction() const {
		int patternIndex = (lastPattern >= 0) ? lastPattern : 0;
		GrooveFamily family = getGrooveFamilyForPattern(patternIndex);
		float baseSwing = 0.f;
		switch (family) {
			case GROOVE_AMEN:      baseSwing = 0.08f; break;
			case GROOVE_JUNGLE:    baseSwing = 0.11f; break;
			case GROOVE_BREAKCORE: baseSwing = 0.01f; break;
			default:               baseSwing = 0.04f; break;
		}
		baseSwing *= 1.f + microAmount * 0.35f;
		return math::clamp(baseSwing, 0.f, 0.16f);
	}

	void scheduleClockSubticks() {
		int stepMultiplier = getClockStepMultiplier();
		pendingClockSubticks = stepMultiplier - 1;
		if (pendingClockSubticks <= 0) {
			clockSubtickSamplesRemaining = 0;
			clockSubtickIntervalSamples = 0;
			return;
		}
		float swingRatio = 1.f / (float)stepMultiplier;
		if (stepMultiplier == 2) {
			swingRatio = 0.5f + getSubtickSwingFraction();
		}
		clockSubtickIntervalSamples = std::max<uint32_t>(1u,
			(uint32_t)std::lround(clockIntervalSamples * swingRatio));
		clockSubtickSamplesRemaining = clockSubtickIntervalSamples;
	}

	void advanceDiagnosticPatternTick() {
		const Pattern& diagPattern = PATTERNS[1];
		bool fired[NUM_VOICES] = {};
		int debugStep = voiceStep[VOICE_KICK];
		if (debugStep >= diagPattern.voices[VOICE_KICK].numSteps) {
			debugStep = 0;
		}
		clearRatchetState();
		clearTrackerFxState();
		for (int v = 0; v < NUM_VOICES; ++v) {
			const VoicePattern& voicePattern = diagPattern.voices[v];
			if (voicePattern.numSteps <= 0) {
				continue;
			}
			int step = voiceStep[v];
			if (step >= voicePattern.numSteps) {
				step = 0;
				voiceStep[v] = 0;
			}
			if (voicePattern.steps[step] >= 0.5f) {
				float duration = (v == VOICE_OHAT) ? 0.10f : 5e-3f;
				triggerVoicePulse(v, duration);
				fired[v] = true;
			}
			voiceStep[v] = (voiceStep[v] + 1) % voicePattern.numSteps;
		}
		setDebugStepText(debugStep, fired);
	}

	void advanceSequencerTick(const ProcessArgs& args, int patternIndex) {
		bool isReplayingStutter = (stutterStepsRemaining > 0);
		bool startNewStutter = false;
		bool hardCutThisTick = false;
		if (wildStutterCooldownSteps > 0) wildStutterCooldownSteps--;
		if (wildCutCooldownSteps > 0) wildCutCooldownSteps--;
		bool kickPlaysTick  = (sparseMode == 0 || sparseMode == 3 || sparseMode == 4);
		bool snarePlaysTick = (sparseMode == 0);
		bool hatPlaysTick   = (sparseMode == 0 || sparseMode == 1 || sparseMode == 4);
		bool bassFiredVoices[NUM_VOICES] = {};
		if (bassCycleClock == 0) {
			startBassCycle();
		}
		if (isReplayingStutter) {
			stutterStepsRemaining--;
		}
		else if (chaosAmount > 0.7f) {
			float extremeDepth = (chaosAmount - 0.7f) * (1.f / 0.3f);
			float fxRoll = random::uniform();
			bool allowWildChaosAccent = true;
			if (wildMode) {
				const Pattern& chaosPattern = correctedBasePattern;
				GrooveFamily chaosGrooveFamily = getGrooveFamilyForPattern(patternIndex);
				const VoicePattern& kickPat = chaosPattern.voices[VOICE_KICK];
				int kickTick = voiceStep[VOICE_KICK];
				if (kickTick >= kickPat.numSteps) kickTick = 0;
				bool kickAnchor = isProtectedGrooveAnchorStep(chaosGrooveFamily, VOICE_KICK, kickTick, kickPat.numSteps);
				bool kickStrongBeat = isStrongBeatStep(kickTick, kickPat.numSteps);
				allowWildChaosAccent = !kickAnchor && !kickStrongBeat;
			}
			float stutterProb = wildMode ? (extremeDepth * 0.07f) : (extremeDepth * 0.12f);
			float cutProb = wildMode ? (extremeDepth * 0.025f) : (extremeDepth * 0.08f);
			if (fxRoll < stutterProb && (!wildMode || (allowWildChaosAccent && wildStutterCooldownSteps == 0))) {
				clearTrackerFxState();
				for (int v = 0; v < NUM_VOICES; v++) {
					if (isStutterVoice(v)) {
						clearRatchetForVoice(v);
					}
				}
				stutterStepsRemaining = (int)(random::uniform() * 3.f) + 1;
				if (wildMode) {
					wildStutterCooldownSteps = 6 + (int)std::lround(wildDepth * 6.f);
				}
				if (wildMode && wildDepth > 0.7f) {
					stutterStepsRemaining += (int)(wildDepth * 3.f);
				}
				startNewStutter = true;

				if (wildMode && wildDepth > 0.40f) {
					const int maxDrift = (wildDepth > 0.90f) ? 2 : 1;
					int drifted = 0;
					for (int v = 0; v < NUM_VOICES && drifted < maxDrift; v++) {
						if (v == VOICE_KICK) continue;
						if (v == VOICE_RIDE) continue;
						if (random::uniform() > wildDepth) continue;
						const Pattern& basePat2 = PATTERNS[patternIndex];
						int n2 = basePat2.voices[v].numSteps;
						if (n2 < 4) continue;
						const float offsets[] = {1.f/3.f, 1.f/4.f, 2.f/3.f};
						int oi = (int)(random::uniform() * 3.f);
						if (oi > 2) oi = 2;
						int drift = (int)(n2 * offsets[oi]);
						voiceStep[v] = (voiceStep[v] + drift) % n2;
						drifted++;
					}
				}
			}
			else if (fxRoll > (1.f - cutProb) && (!wildMode || (allowWildChaosAccent && wildCutCooldownSteps == 0))) {
				hardCutThisTick = true;
				if (wildMode) {
					wildCutCooldownSteps = 10 + (int)std::lround(wildDepth * 8.f);
				} else {
					clearRatchetState();
				}
			}
		}

		const Pattern& basePat = (patternIndex == 0 && !wildMode)
			? PATTERNS[patternIndex]
			: correctedBasePattern;
		GrooveFamily grooveFamily = getGrooveFamilyForPattern(patternIndex);
		bool closedHatFiredThisTick = false;
		int tickStep[NUM_VOICES] = {};
		float tickBaseProbability[NUM_VOICES] = {};
		for (int v = 0; v < NUM_VOICES; v++) {
			const VoicePattern& tickVP = basePat.voices[v];
			int tick = voiceStep[v];
			if (tick >= tickVP.numSteps) tick = 0;
			tickStep[v] = tick;
			tickBaseProbability[v] = tickVP.steps[tick];
		}
		updateWildSnareRestState(tickStep[VOICE_KICK], basePat.voices[VOICE_KICK].numSteps);
		updatePhraseTimingMap(patternIndex, grooveFamily, tickStep[VOICE_KICK], basePat.voices[VOICE_KICK].numSteps, args.sampleRate);

		for (int v = 0; v < NUM_VOICES; v++) {
			const VoicePattern& baseVP = basePat.voices[v];
			int step = tickStep[v];
			bool voiceIsStutterable = isStutterVoice(v);
			bool voiceIsReplaying = isReplayingStutter && voiceIsStutterable;
			bool accentProtectedAnchor = isProtectedGrooveAnchorStep(grooveFamily, v, step, baseVP.numSteps);
			bool accentStrongBeat = isStrongBeatStep(step, baseVP.numSteps);
			float accentProbability = baseVP.steps[step];

			if (step >= baseVP.numSteps) {
				step = 0;
				voiceStep[v] = 0;
			}

			bool shouldFire = false;
			bool chaosCutVoice = hardCutThisTick && (!wildMode || v == VOICE_SNARE2 || v == VOICE_CHAT || v == VOICE_OHAT || v == VOICE_RIDE);
			if (chaosCutVoice) {
				clearRatchetForVoice(v);
				shouldFire = false;
			}
			else if (voiceIsReplaying) {
				clearRatchetForVoice(v);
				shouldFire = stutterCache[v];
			}
			else {
				float probability = baseVP.steps[step];
				float grooveTarget = getGrooveTargetProbability(grooveFamily, v, step, baseVP.numSteps);
				bool protectedAnchor = accentProtectedAnchor;
				if (blendAmount > 0.f && random::uniform() < blendAmount) {
					float mutStep = step;
					if (mutStep < correctedMutatedPattern.voices[v].numSteps) {
						probability = correctedMutatedPattern.voices[v].steps[(int)mutStep];
					}
				}

				float grooveBiasStrength = smoothstep(0.15f, 0.55f, currentMutateNorm)
					* (1.f - smoothstep(0.80f, 0.98f, currentMutateNorm));
				grooveBiasStrength *= (grooveFamily == GROOVE_BREAKCORE) ? 0.20f : 0.34f;
				if (grooveBiasStrength > 0.f) {
					probability += (grooveTarget - probability) * grooveBiasStrength;
				}

				if (protectedAnchor && currentMutateNorm > 0.18f && currentMutateNorm < 0.85f) {
					float anchorFloor = (v == VOICE_SNARE1)
						? ((grooveFamily == GROOVE_JUNGLE) ? 0.74f : 0.80f)
						: 0.68f;
					probability = std::max(probability, anchorFloor);
				}

				bool snare1AnchorNow = isProtectedGrooveAnchorStep(
					grooveFamily,
					VOICE_SNARE1,
					tickStep[VOICE_SNARE1],
					basePat.voices[VOICE_SNARE1].numSteps);
				bool kickAnchorNow = isProtectedGrooveAnchorStep(
					grooveFamily,
					VOICE_KICK,
					tickStep[VOICE_KICK],
					basePat.voices[VOICE_KICK].numSteps);
				bool snareExpected = snare1AnchorNow || tickBaseProbability[VOICE_SNARE1] >= 0.55f;
				bool kickExpected = kickAnchorNow || tickBaseProbability[VOICE_KICK] >= 0.65f;

				if (v == VOICE_KICK && (snareExpected || tickBaseProbability[VOICE_SNARE2] >= 0.35f)) {
					float kickDuck = (grooveFamily == GROOVE_BREAKCORE) ? 0.75f : 0.48f;
					probability *= kickDuck;
				}
				if ((v == VOICE_CHAT || v == VOICE_RIDE) && !kickExpected && !snareExpected && currentMutateNorm > 0.f) {
					float fillFloor = (v == VOICE_CHAT) ? 0.18f : 0.10f;
					fillFloor += blendAmount * ((v == VOICE_CHAT) ? 0.10f : 0.06f);
					fillFloor *= (grooveFamily == GROOVE_BREAKCORE) ? 0.85f : 1.f;
					probability = std::max(probability, std::min(fillFloor, grooveTarget));
				}

				if (chaosAmount > 0.f && voiceIsStutterable) {
					float maskedGhost = getGrooveTargetProbability(grooveFamily, VOICE_SNARE2, step, baseVP.numSteps);
					if (v == VOICE_SNARE2 && maskedGhost > 0.10f) {
						float ghostProb = maskedGhost * (0.12f + chaosAmount * 0.22f);
						if (wildMode) {
							ghostProb = math::clamp(ghostProb, 0.06f, 0.22f);
							if (random::uniform() < 0.35f) ghostProb *= 0.5f;
						} else {
							ghostProb = math::clamp(ghostProb, 0.04f, 0.18f);
						}
						probability = std::max(probability, ghostProb);
					}
					else if ((v == VOICE_CHAT || v == VOICE_OHAT) && grooveTarget > 0.12f) {
						float hatLift = grooveTarget * (0.12f + chaosAmount * 0.26f);
						probability = std::max(probability, math::clamp(hatLift, 0.08f, 0.52f));
					}
				}

				// Cap mutated snare density so mutate adds phrasing, not constant chatter.
				if (v == VOICE_SNARE1) {
					float snareCap = std::max(baseVP.steps[step], grooveTarget);
					snareCap += protectedAnchor ? (0.08f + currentMutateNorm * 0.08f)
						: (0.04f + currentMutateNorm * 0.06f);
					snareCap = protectedAnchor ? std::min(snareCap, 0.82f) : std::min(snareCap, 0.42f);
					snareCap = std::max(snareCap, baseVP.steps[step]);
					probability = std::min(probability, snareCap);
				}
				else if (v == VOICE_SNARE2) {
					float ghostCap = std::max(baseVP.steps[step], grooveTarget * 0.85f);
					ghostCap += 0.03f + chaosAmount * 0.08f + currentMutateNorm * 0.04f;
					if (grooveTarget < 0.10f && baseVP.steps[step] < 0.18f) {
						ghostCap = std::min(ghostCap, 0.14f);
					} else {
						ghostCap = std::min(ghostCap, 0.28f);
					}
					ghostCap = std::max(ghostCap, baseVP.steps[step]);
					probability = std::min(probability, ghostCap);
				}

				if (probability >= 1.f) {
					shouldFire = true;
				}
				else if (probability > 0.f) {
					shouldFire = (random::uniform() < probability);
				}
				accentProbability = probability;
			}

			if (wildMode && snareRestThisBar[v] &&
			    (v == VOICE_SNARE1 || v == VOICE_SNARE2)) {
				shouldFire = false;
			}

			if (v == VOICE_OHAT && closedHatFiredThisTick) {
				shouldFire = false;
			}

			if (wildMode && wildDepth > 0.20f && v != VOICE_KICK && !voiceIsReplaying && !hardCutThisTick) {
				bool protectedAnchor = isProtectedGrooveAnchorStep(grooveFamily, v, step, baseVP.numSteps);
				bool strongBeat = isStrongBeatStep(step, baseVP.numSteps);
				if (!protectedAnchor && !strongBeat) {
					float flipProb = (wildDepth - 0.20f) * 0.875f;
					if (v == VOICE_SNARE1 || v == VOICE_SNARE2)
						flipProb *= 0.45f;
					if (random::uniform() < flipProb) {
						shouldFire = !shouldFire;
					}
				}
			}

			if (startNewStutter && voiceIsStutterable) {
				stutterCache[v] = shouldFire;
			}

			if (shouldFire) {
				clearRatchetForVoice(v);
				uint32_t newRatchetIntervalSamples = 0;
				int newRatchetHitsRemaining = 0;
				float accentVoltage = deriveAccentVoltage(v, baseVP.steps[step], accentProbability,
					accentProtectedAnchor, accentStrongBeat, voiceIsReplaying);

				if (ratchetCooldown[v] > 0) ratchetCooldown[v]--;

				bool voiceCanRatchet = isRatchetVoice(v) || (v == VOICE_KICK);
				if (wildMode && wildDepth > 0.60f && v == VOICE_KICK) voiceCanRatchet = true;

				float ratchetThreshold = wildMode ? 0.70f : 0.90f;
				if (!voiceIsReplaying && chaosAmount > ratchetThreshold && voiceCanRatchet
					&& ratchetCooldown[v] == 0) {
					float ratchetDepth = (chaosAmount - ratchetThreshold) / (1.0f - ratchetThreshold);
					bool isClosedHatVoice = (v == VOICE_CHAT);
					bool isOpenHatVoice = (v == VOICE_OHAT);
					bool isRideVoice = (v == VOICE_RIDE);
					bool isHatVoice = isClosedHatVoice || isOpenHatVoice || isRideVoice;
					float maxProb;
					if (v == VOICE_SNARE2)      maxProb = wildMode ? 0.05f  : 0.015f;
					else if (v == VOICE_SNARE1) maxProb = wildMode ? 0.015f : 0.01f;
					else if (v == VOICE_KICK)   maxProb = wildMode ? 0.14f  : 0.04f;
					else if (isClosedHatVoice)  maxProb = wildMode ? 0.18f  : 0.08f;
					else if (isOpenHatVoice)    maxProb = wildMode ? 0.06f  : 0.02f;
					else if (isRideVoice)       maxProb = wildMode ? 0.03f  : 0.01f;
					else                        maxProb = wildMode ? 0.10f  : 0.04f;
					float ratchetRollProb = ratchetDepth * maxProb;

					bool cascadeBurst = wildMode && wildDepth > 0.85f
						&& isClosedHatVoice
						&& (random::uniform() < (wildDepth - 0.85f) * 2.f);

					if (cascadeBurst || random::uniform() < ratchetRollProb) {
						float roll = random::uniform();
						int maxHits = wildMode ? 16 : 8;
						int kickMaxHits = wildMode ? 3 : 2;
						if (isClosedHatVoice) {
							maxHits = wildMode ? 6 : 4;
						} else if (isOpenHatVoice) {
							maxHits = 3;
						} else if (isRideVoice) {
							maxHits = 2;
						}
						if (cascadeBurst) {
							int burstHits = (wildDepth > 0.92f) ? 32 : 16;
							if (isClosedHatVoice) {
								burstHits = (wildDepth > 0.92f) ? 8 : 6;
							}
							newRatchetIntervalSamples = std::max<uint32_t>(1u, clockIntervalSamples / (uint32_t)burstHits);
							newRatchetHitsRemaining = burstHits - 1;
							if (stutterStepsRemaining == 0) stutterStepsRemaining = 1;
						} else {
							int totalHits = 2;
							float intervalScale = 0.5f;
							if (v == VOICE_KICK) {
								totalHits = (roll < 0.6f) ? 2 : kickMaxHits;
								intervalScale = std::max(0.333333f, 1.f / (float)totalHits);
							} else if (v == VOICE_SNARE2) {
								totalHits = (roll < 0.75f) ? 2 : 3;
								intervalScale = std::max(0.333333f, 1.f / (float)totalHits);
							} else if (v == VOICE_SNARE1) {
								if (roll < 0.50f) {
									totalHits = 2;
									intervalScale = 0.5f;
								} else {
									totalHits = 3;
									intervalScale = 0.333333f;
								}
							} else if (isOpenHatVoice) {
								if (roll < 0.85f) {
									totalHits = 2;
									intervalScale = 0.5f;
								} else {
									totalHits = 3;
									intervalScale = 0.333333f;
								}
							} else if (isRideVoice) {
								totalHits = 2;
								intervalScale = (roll < 0.5f) ? 0.75f : 0.5f;
							} else {
								if (roll < 0.08f) {
									totalHits = 2;
									intervalScale = 1.0f;
								} else if (roll < 0.22f) {
									totalHits = 2;
									intervalScale = 0.75f;
								} else if (roll < 0.42f) {
									totalHits = 2;
									intervalScale = 0.5f;
								} else if (roll < 0.56f) {
									totalHits = 3;
									intervalScale = 0.5f;
								} else if (roll < 0.70f) {
									totalHits = 3;
									intervalScale = 0.333333f;
								} else if (roll < 0.82f) {
									totalHits = 4;
									intervalScale = 0.25f;
								} else if (roll < 0.92f) {
									totalHits = wildMode ? 8 : 4;
									intervalScale = 1.f / (float)totalHits;
								} else {
									totalHits = maxHits;
									intervalScale = 1.f / (float)totalHits;
								}
								if (isClosedHatVoice) {
									intervalScale = std::max(0.166667f, intervalScale);
								} else if (isOpenHatVoice || isRideVoice) {
									intervalScale = std::max(0.25f, intervalScale);
								}
							}
							newRatchetIntervalSamples = std::max<uint32_t>(1u, (uint32_t)std::lround(clockIntervalSamples * intervalScale));
							newRatchetHitsRemaining = totalHits - 1;
						}
						int coolSteps;
						if (v == VOICE_KICK) {
							coolSteps = 12 + (int)(random::uniform() * 4.f);
						} else if (v == VOICE_SNARE2) {
							coolSteps = 28 + (int)(random::uniform() * 12.f);
						} else if (v == VOICE_SNARE1) {
							if (wildMode && wildDepth > 0.50f)
								coolSteps = 36 + (int)(random::uniform() * 18.f);
							else
								coolSteps = 24 + (int)(random::uniform() * 12.f);
						} else if (isClosedHatVoice) {
							coolSteps = wildMode ? 6 + (int)(random::uniform() * 4.f)
								: 8 + (int)(random::uniform() * 4.f);
						} else if (isOpenHatVoice) {
							coolSteps = 14 + (int)(random::uniform() * 6.f);
						} else if (isRideVoice) {
							coolSteps = 18 + (int)(random::uniform() * 8.f);
						} else {
							coolSteps = 6 + (int)(random::uniform() * 4.f);
						}
						if (wildMode && isClosedHatVoice)
							coolSteps = (int)(coolSteps * 0.6f);
						ratchetCooldown[v] = coolSteps;
					}
				}
				ratchetAccentVoltage[v] = math::clamp(
					accentVoltage * ((v == VOICE_KICK || v == VOICE_SNARE2) ? 0.78f : 0.72f),
					0.f,
					10.f);

				float dur = (v == VOICE_OHAT) ? 0.10f : 5e-3f;
				if (v == VOICE_KICK && wildMode) dur = 8e-3f;
				if (newRatchetHitsRemaining > 0) {
					dur = ratchetPulseDuration(newRatchetIntervalSamples, args.sampleTime, dur);
				}

				if (wildMode && wildDepth > 0.10f) {
					float fuzzRange = 0.4f + wildDepth * 2.6f;
					float fuzz = 0.4f + random::uniform() * (fuzzRange - 0.4f);
					dur *= fuzz;
					if (dur < 1e-4f) dur = 1e-4f;
					if (dur > 0.25f) dur = 0.25f;
				}

				if (v == VOICE_CHAT) {
					closedHatFiredThisTick = true;
				}
				bool voiceFeedsBass = false;
				if (v == VOICE_KICK) {
					voiceFeedsBass = kickPlaysTick;
				} else if (v == VOICE_SNARE1 || v == VOICE_SNARE2) {
					voiceFeedsBass = snarePlaysTick;
				} else {
					voiceFeedsBass = hatPlaysTick;
				}
				if (voiceFeedsBass) {
					bassFiredVoices[v] = true;
				}
				uint32_t phraseDelaySamples = getRoleTimingDelaySamples(v, accentProtectedAnchor, accentStrongBeat, voiceIsReplaying);
				if (phraseDelaySamples > 0u) {
					pendingVoicePulse[v] = true;
					pendingVoicePulseSamples[v] = phraseDelaySamples;
					pendingVoicePulseDur[v] = dur;
					pendingVoiceAccentVoltage[v] = accentVoltage;
				} else {
					accentHoldVoltage[v] = accentVoltage;
					triggerVoicePulse(v, dur);
				}
				if (newRatchetHitsRemaining > 0) {
					ratchetsRemaining[v] = newRatchetHitsRemaining;
					ratchetIntervalSamples[v] = newRatchetIntervalSamples;
					ratchetSamplesRemaining[v] = newRatchetIntervalSamples;
				}
			}

			if (!voiceIsReplaying) {
				voiceStep[v]++;
				if (voiceStep[v] >= baseVP.numSteps) {
					voiceStep[v] = 0;
				}
			}
		}

		bool bassCandidate = false;
		{
			const float m = currentMutateNorm;
			bool kickFired  = bassFiredVoices[VOICE_KICK];
			bool sn1Fired   = bassFiredVoices[VOICE_SNARE1];
			bool sn2Fired   = bassFiredVoices[VOICE_SNARE2];
			bool chatFired  = bassFiredVoices[VOICE_CHAT];
			bool ohatFired  = bassFiredVoices[VOICE_OHAT];

			if (m >= 0.95f) {
				bassCandidate = kickFired || sn1Fired || sn2Fired || chatFired || ohatFired;
			} else {
				switch (bassSourceRecipe) {
					case BASS_SOURCE_KICK:
						bassCandidate = kickFired;
						break;
					case BASS_SOURCE_KICK_SNARE1:
						bassCandidate = kickFired || sn1Fired;
						break;
					case BASS_SOURCE_CLOSED_HAT:
						bassCandidate = chatFired && (random::uniform() < bassHatThinProbability);
						break;
					case BASS_SOURCE_SNARES:
						bassCandidate = (sn1Fired || sn2Fired) && (random::uniform() < 0.80f);
						break;
					default:
						break;
				}
				if (!bassCandidate && chatFired) {
					float hatProb = smoothstep(0.30f, 0.90f, m);
					if (random::uniform() < hatProb) bassCandidate = true;
				}
				if (!bassCandidate && m >= 0.70f && (sn2Fired || ohatFired)) {
					float extraProb = smoothstep(0.70f, 0.95f, m);
					if (random::uniform() < extraProb) bassCandidate = true;
				}
			}
		}

		if (bassCandidate && bassSamplesSinceTrigger >= bassMinTriggerSpacingSamples) {
			bool doFire = true;
			if (bassWildDoubleTimeActive && bassWildSkipEvery > 0) {
				bassWildSkipCounter++;
				if (bassWildSkipCounter % bassWildSkipEvery == 0) {
					doFire = false;
				}
			}
			if (doFire) {
				triggerBassSampleHold(args.sampleRate, bassWildDoubleTimeActive, false);
			}
		}
		bassCycleClock++;
		patternStepsSinceChange++;
		if (bassCycleClock >= BASS_CYCLE_CLOCKS) {
			bassCycleClock = 0;
		}
	}

	ValidationProfile getValidationProfile(int patternIndex, const Pattern& pattern) const {
		if (patternIndex == 0) {
			return VALIDATION_PROFILE_BYPASS;
		}

		GrooveFamily family = getGrooveFamilyForPattern(patternIndex);
		if (!(family == GROOVE_AMEN || family == GROOVE_JUNGLE || family == GROOVE_BREAKCORE)) {
			return VALIDATION_PROFILE_BYPASS;
		}

		int kickSteps = pattern.voices[VOICE_KICK].numSteps;
		if (kickSteps <= 0 || (kickSteps % 8) != 0) {
			return VALIDATION_PROFILE_BYPASS;
		}

		for (int v = 0; v < NUM_VOICES; ++v) {
			int numSteps = pattern.voices[v].numSteps;
			if (numSteps <= 0) continue;
			if (numSteps != kickSteps || (numSteps % 8) != 0) {
				return VALIDATION_PROFILE_BYPASS;
			}
		}

		return VALIDATION_PROFILE_BREAKBEAT;
	}

	int getPrimarySnareAnchorTargetStep(GrooveFamily family, int numSteps) const {
		if (numSteps <= 0) return 0;
		static const int amenTargets[] = {2, 6, 10, 14};
		static const int jungleTargets[] = {4, 12};
		const int* targets = nullptr;
		int targetCount = 0;
		if (family == GROOVE_AMEN) {
			targets = amenTargets;
			targetCount = 4;
		}
		else if (family == GROOVE_JUNGLE) {
			targets = jungleTargets;
			targetCount = 2;
		}
		else {
			return numSteps / 2;
		}

		int midpoint = numSteps / 2;
		int bestStep = midpoint;
		int bestDistance = INT_MAX;
		for (int i = 0; i < targetCount; ++i) {
			int mapped = (int)std::lround((targets[i] / 16.f) * numSteps) % numSteps;
			int distance = wrapStepDistance(mapped, midpoint, numSteps);
			if (distance < bestDistance) {
				bestDistance = distance;
				bestStep = mapped;
			}
		}
		return bestStep;
	}

	bool hasPrimarySnareAround(const VoicePattern& voicePattern, int targetStep, int window) const {
		int numSteps = voicePattern.numSteps;
		if (numSteps <= 0) return false;
		for (int step = 0; step < numSteps; ++step) {
			if (!isStrongPatternHit(voicePattern.steps[step], 0.55f)) continue;
			if (wrapStepDistance(step, targetStep, numSteps) <= window) {
				return true;
			}
		}
		return false;
	}

	int findNearestSnareCandidate(const VoicePattern& voicePattern, int targetStep, float threshold) const {
		int numSteps = voicePattern.numSteps;
		int bestStep = -1;
		int bestDistance = INT_MAX;
		float bestValue = -1.f;
		for (int step = 0; step < numSteps; ++step) {
			float value = voicePattern.steps[step];
			if (value < threshold) continue;
			int distance = wrapStepDistance(step, targetStep, numSteps);
			if (distance < bestDistance || (distance == bestDistance && value > bestValue)) {
				bestDistance = distance;
				bestValue = value;
				bestStep = step;
			}
		}
		return bestStep;
	}

	int countHitsAboveThreshold(const VoicePattern& voicePattern, float threshold) const {
		int count = 0;
		for (int step = 0; step < voicePattern.numSteps; ++step) {
			if (voicePattern.steps[step] >= threshold) {
				count++;
			}
		}
		return count;
	}

	bool hasNearbyPrimarySnare(const Pattern& pattern, int step, int radius) const {
		const VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		for (int i = 0; i < snare.numSteps; ++i) {
			if (!isStrongPatternHit(snare.steps[i], 0.55f)) continue;
			if (wrapStepDistance(i, step, snare.numSteps) <= radius) {
				return true;
			}
		}
		return false;
	}

	bool isValidGhostZone(const Pattern& pattern, GrooveFamily family, int step, int numSteps) const {
		if (numSteps <= 0) return false;
		if (isStrongBeatStep(step, numSteps)) return false;
		if (hasNearbyPrimarySnare(pattern, step, std::max(1, numSteps / 16))) return true;
		if (getGrooveTargetProbability(family, VOICE_SNARE2, step, numSteps) >= 0.14f) return true;
		int maskStep = getGrooveMaskStep(step, numSteps);
		return (maskStep % 2) == 1;
	}

	void enforcePrimarySnareAnchor(Pattern& pattern, GrooveFamily family) {
		VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		if (snare.numSteps <= 0) return;

		int targetStep = getPrimarySnareAnchorTargetStep(family, snare.numSteps);
		int window = std::max(1, snare.numSteps / 16);
		if (hasPrimarySnareAround(snare, targetStep, window)) {
			snare.steps[targetStep] = std::max(snare.steps[targetStep], 0.88f);
			return;
		}

		int sourceStep = findNearestSnareCandidate(snare, targetStep, 0.30f);
		if (sourceStep >= 0 && sourceStep != targetStep) {
			float movedValue = std::max(snare.steps[sourceStep], 0.88f);
			snare.steps[sourceStep] = 0.f;
			snare.steps[targetStep] = std::max(snare.steps[targetStep], movedValue);
		}
		else {
			snare.steps[targetStep] = std::max(snare.steps[targetStep], 1.f);
		}
	}

	bool convertPrimarySnareToGhost(Pattern& pattern, GrooveFamily family, int step) {
		VoicePattern& ghost = pattern.voices[VOICE_SNARE2];
		if (ghost.numSteps <= 0) return false;
		if (countHitsAboveThreshold(ghost, 0.18f) >= normalizedHitLimit(ghost.numSteps, 4.f)) {
			return false;
		}

		static const int searchOffsets[] = {0, -1, 1, -2, 2};
		for (int offset : searchOffsets) {
			int candidate = (step + offset + ghost.numSteps) % ghost.numSteps;
			if (!isValidGhostZone(pattern, family, candidate, ghost.numSteps)) continue;
			if (pattern.voices[VOICE_SNARE1].steps[candidate] >= 0.55f) continue;
			ghost.steps[candidate] = std::max(ghost.steps[candidate], 0.32f);
			return true;
		}

		return false;
	}

	void limitPrimarySnareDensity(Pattern& pattern, GrooveFamily family) {
		VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		if (snare.numSteps <= 0) return;

		int targetStep = getPrimarySnareAnchorTargetStep(family, snare.numSteps);
		int protectWindow = std::max(1, snare.numSteps / 16);
		int maxSnareHits = normalizedHitLimit(snare.numSteps, 5.f);

		while (countHitsAboveThreshold(snare, 0.55f) > maxSnareHits) {
			int removeStep = -1;
			float weakestScore = FLT_MAX;

			for (int step = 0; step < snare.numSteps; ++step) {
				float value = snare.steps[step];
				if (value < 0.55f) continue;
				if (wrapStepDistance(step, targetStep, snare.numSteps) <= protectWindow) continue;

				float keepScore = value;
				keepScore += getGrooveTargetProbability(family, VOICE_SNARE1, step, snare.numSteps) * 0.75f;
				keepScore += isStrongBeatStep(step, snare.numSteps) ? 0.15f : 0.f;
				keepScore += (1.f - ((float)wrapStepDistance(step, targetStep, snare.numSteps) / (float)snare.numSteps));

				if (keepScore < weakestScore) {
					weakestScore = keepScore;
					removeStep = step;
				}
			}

			if (removeStep < 0) break;
			convertPrimarySnareToGhost(pattern, family, removeStep);
			snare.steps[removeStep] = 0.f;
		}
	}

	void limitGhostSnareDensity(Pattern& pattern, GrooveFamily family) {
		VoicePattern& ghost = pattern.voices[VOICE_SNARE2];
		if (ghost.numSteps <= 0) return;

		for (int step = 0; step < ghost.numSteps; ++step) {
			if (ghost.steps[step] < 0.18f) continue;
			if (!isValidGhostZone(pattern, family, step, ghost.numSteps)) {
				ghost.steps[step] = 0.f;
			}
		}

		int maxGhostHits = normalizedHitLimit(ghost.numSteps, 4.f);
		while (countHitsAboveThreshold(ghost, 0.18f) > maxGhostHits) {
			int removeStep = -1;
			float weakestClusterScore = FLT_MAX;
			for (int step = 0; step < ghost.numSteps; ++step) {
				if (ghost.steps[step] < 0.18f) continue;
				float clusterScore = ghost.steps[step];
				clusterScore += hasNearbyPrimarySnare(pattern, step, std::max(1, ghost.numSteps / 16)) ? 0.6f : 0.f;
				clusterScore += isValidGhostZone(pattern, family, step, ghost.numSteps) ? 0.2f : 0.f;
				if (step > 0 && ghost.steps[(step - 1 + ghost.numSteps) % ghost.numSteps] >= 0.18f) clusterScore += 0.2f;
				if (ghost.steps[(step + 1) % ghost.numSteps] >= 0.18f) clusterScore += 0.2f;

				if (clusterScore < weakestClusterScore) {
					weakestClusterScore = clusterScore;
					removeStep = step;
				}
			}

			if (removeStep < 0) break;
			ghost.steps[removeStep] = 0.f;
		}
	}

	void enforceKickSnareRelationship(Pattern& pattern, GrooveFamily family) {
		VoicePattern& kick = pattern.voices[VOICE_KICK];
		VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		int numSteps = std::min(kick.numSteps, snare.numSteps);
		if (numSteps <= 0) return;

		for (int step = 0; step < numSteps; ++step) {
			if (!isStrongPatternHit(snare.steps[step], 0.55f)) continue;
			kick.steps[step] = std::min(kick.steps[step], 0.12f);

			int leadStep = (step - 1 + numSteps) % numSteps;
			int followStep = (step + 1) % numSteps;
			bool leadExists = kick.steps[leadStep] >= 0.35f;
			bool followExists = kick.steps[followStep] >= 0.35f;
			if (!leadExists && !followExists) {
				int preferred = !isStrongBeatStep(leadStep, numSteps) ? leadStep : followStep;
				kick.steps[preferred] = std::max(kick.steps[preferred], std::min(0.68f,
					getGrooveTargetProbability(family, VOICE_KICK, preferred, numSteps) + 0.22f));
			}
		}
	}

	bool hasRepeatingMotif(const Pattern& pattern, int motifLen) const {
		const VoicePattern& kick = pattern.voices[VOICE_KICK];
		const VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		int numSteps = std::min(kick.numSteps, snare.numSteps);
		if (numSteps < motifLen * 2 || motifLen <= 0) return true;

		for (int startA = 0; startA + motifLen <= numSteps; startA += motifLen) {
			for (int startB = startA + motifLen; startB + motifLen <= numSteps; startB += motifLen) {
				int matches = 0;
				int total = motifLen * 2;
				for (int i = 0; i < motifLen; ++i) {
					bool kickA = kick.steps[startA + i] >= 0.35f;
					bool kickB = kick.steps[startB + i] >= 0.35f;
					bool snareA = snare.steps[startA + i] >= 0.55f;
					bool snareB = snare.steps[startB + i] >= 0.55f;
					matches += (kickA == kickB) ? 1 : 0;
					matches += (snareA == snareB) ? 1 : 0;
				}
				if ((float)matches / (float)total >= 0.75f) {
					return true;
				}
			}
		}

		return false;
	}

	void enforceMotifRepetition(Pattern& pattern) {
		const VoicePattern& kick = pattern.voices[VOICE_KICK];
		int numSteps = kick.numSteps;
		if (numSteps < 16) return;
		if (hasRepeatingMotif(pattern, 8) || hasRepeatingMotif(pattern, 16)) return;

		int motifLen = (numSteps >= 32) ? 8 : 4;
		int copyStart = 0;
		int copyDest = numSteps / 2;
		for (int i = 0; i < motifLen && (copyDest + i) < numSteps; ++i) {
			int src = copyStart + i;
			int dst = copyDest + i;
			pattern.voices[VOICE_KICK].steps[dst] = std::max(pattern.voices[VOICE_KICK].steps[dst], pattern.voices[VOICE_KICK].steps[src] * 0.92f);
			pattern.voices[VOICE_CHAT].steps[dst] = std::max(pattern.voices[VOICE_CHAT].steps[dst], pattern.voices[VOICE_CHAT].steps[src] * 0.85f);
			if (pattern.voices[VOICE_SNARE1].steps[dst] < 0.55f) {
				pattern.voices[VOICE_SNARE2].steps[dst] = std::max(pattern.voices[VOICE_SNARE2].steps[dst], pattern.voices[VOICE_SNARE2].steps[src] * 0.85f);
			}
		}
	}

	void enforceSpacingAndSaturation(Pattern& pattern, GrooveFamily family) {
		VoicePattern& kick = pattern.voices[VOICE_KICK];
		VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		int numSteps = std::min(kick.numSteps, snare.numSteps);
		if (numSteps <= 0) return;

		for (int step = 0; step < numSteps; ++step) {
			if (kick.steps[step] < 0.55f) continue;
			int next = (step + 1) % numSteps;
			if (kick.steps[next] >= 0.55f) {
				if (kick.steps[step] >= kick.steps[next]) kick.steps[next] = 0.f;
				else kick.steps[step] = 0.f;
			}
		}

		int strongHitCount = countHitsAboveThreshold(kick, 0.55f) + countHitsAboveThreshold(snare, 0.55f);
		int maxStrongHits = std::max(4, numSteps / 2);
		while (strongHitCount > maxStrongHits) {
			int removeStep = -1;
			float weakestKick = FLT_MAX;
			for (int step = 0; step < kick.numSteps; ++step) {
				if (kick.steps[step] < 0.55f) continue;
				float keepScore = kick.steps[step] + getGrooveTargetProbability(family, VOICE_KICK, step, kick.numSteps);
				if (keepScore < weakestKick) {
					weakestKick = keepScore;
					removeStep = step;
				}
			}
			if (removeStep < 0) break;
			kick.steps[removeStep] = 0.f;
			strongHitCount = countHitsAboveThreshold(kick, 0.55f) + countHitsAboveThreshold(snare, 0.55f);
		}
	}

	void fillHatGaps(Pattern& pattern) {
		VoicePattern& kick = pattern.voices[VOICE_KICK];
		VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		VoicePattern& hat = pattern.voices[VOICE_CHAT];
		int numSteps = std::min(std::min(kick.numSteps, snare.numSteps), hat.numSteps);
		if (numSteps <= 0) return;

		int lastEvent = -1;
		int gapThreshold = std::max(4, numSteps / 8);
		for (int step = 0; step < numSteps * 2; ++step) {
			int wrappedStep = step % numSteps;
			bool eventHere = kick.steps[wrappedStep] >= 0.55f || snare.steps[wrappedStep] >= 0.55f;
			if (!eventHere) continue;
			if (lastEvent >= 0) {
				int gap = step - lastEvent;
				if (gap >= gapThreshold) {
					int fillStep = (lastEvent + gap / 2) % numSteps;
					if (!isStrongBeatStep(fillStep, numSteps) && hat.steps[fillStep] < 0.30f) {
						hat.steps[fillStep] = std::max(hat.steps[fillStep], 0.28f);
					}
				}
			}
			lastEvent = step;
			if (step >= numSteps && wrappedStep == 0) break;
		}
	}

	float computeGrooveScore(const Pattern& pattern, GrooveFamily family) const {
		const VoicePattern& snare = pattern.voices[VOICE_SNARE1];
		const VoicePattern& ghost = pattern.voices[VOICE_SNARE2];
		const VoicePattern& kick = pattern.voices[VOICE_KICK];
		if (snare.numSteps <= 0) return 1.f;

		float score = 0.f;
		int targetStep = getPrimarySnareAnchorTargetStep(family, snare.numSteps);
		int window = std::max(1, snare.numSteps / 16);
		if (hasPrimarySnareAround(snare, targetStep, window)) score += 1.f;
		if (countHitsAboveThreshold(snare, 0.55f) <= normalizedHitLimit(snare.numSteps, 5.f)) score += 1.f;
		if (countHitsAboveThreshold(ghost, 0.18f) <= normalizedHitLimit(ghost.numSteps, 4.f)) score += 1.f;
		if (hasRepeatingMotif(pattern, 8) || hasRepeatingMotif(pattern, 16)) score += 1.f;

		int collisions = 0;
		int numSteps = std::min(kick.numSteps, snare.numSteps);
		for (int step = 0; step < numSteps; ++step) {
			if (kick.steps[step] >= 0.55f && snare.steps[step] >= 0.55f) collisions++;
		}
		if (collisions == 0) score += 1.f;

		int strongHitCount = countHitsAboveThreshold(kick, 0.55f) + countHitsAboveThreshold(snare, 0.55f);
		if (strongHitCount <= std::max(4, numSteps / 2)) score += 1.f;

		return score / 6.f;
	}

	void rebuildCorrectedPatternBuffer(Pattern& target, const Pattern& source, int patternIndex, bool applyStructural = true) {
		target = source;
		sanitizePatternBuffer(target);

		if (!applyStructural) {
			return;
		}

		ValidationProfile profile = getValidationProfile(patternIndex, target);
		if (profile != VALIDATION_PROFILE_BREAKBEAT) {
			return;
		}

		GrooveFamily family = getGrooveFamilyForPattern(patternIndex);
		for (int iteration = 0; iteration < 3; ++iteration) {
			enforcePrimarySnareAnchor(target, family);
			limitPrimarySnareDensity(target, family);
			limitGhostSnareDensity(target, family);
			enforceKickSnareRelationship(target, family);
			if (computeGrooveScore(target, family) < 0.80f) {
				enforceMotifRepetition(target);
			}
			enforceSpacingAndSaturation(target, family);
			fillHatGaps(target);
			limitGhostSnareDensity(target, family);
			if (computeGrooveScore(target, family) >= 0.84f) {
				break;
			}
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
		return voice == VOICE_SNARE1 || voice == VOICE_SNARE2
			|| voice == VOICE_CHAT || voice == VOICE_OHAT || voice == VOICE_RIDE;
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
			int baseSteps = baseVP.numSteps;
			if (baseSteps < 0) baseSteps = 0;
			if (baseSteps > MAX_STEPS) baseSteps = MAX_STEPS;
			int donorSteps = donorVP.numSteps;
			if (donorSteps < 0) donorSteps = 0;
			if (donorSteps > MAX_STEPS) donorSteps = MAX_STEPS;

			mutatedPattern.voices[v].numSteps = baseSteps;

			if (baseSteps == 0) {
				for (int i = 0; i < MAX_STEPS; i++) {
					mutatedPattern.voices[v].steps[i] = 0.f;
				}
				continue;
			}

			int segSize = baseSteps / NUM_SEGMENTS;
			if (segSize < 1) segSize = 1;

			for (int s = 0; s < NUM_SEGMENTS; s++) {
				int segStart = s * segSize;
				int segEnd = (s == NUM_SEGMENTS - 1) ? baseSteps : (s + 1) * segSize;

				bool usesDonor = (segmentAmount > 0.f && donorSteps > 0 && segmentThresholds[v][s] < segmentAmount);

				for (int i = segStart; i < segEnd; i++) {
					if (usesDonor) {
						// Wrap donor step via modulo if donor has different length
						int donorStep = i % donorSteps;
						float val = donorVP.steps[donorStep];

						// Keep donor snare imports conservative so mutation adds phrasing
						// instead of flooding the groove with extra snares.
						if (v == VOICE_SNARE1) {
							float scale = wildMode ? 0.75f : 0.45f;
							val = baseVP.steps[i] + (val - baseVP.steps[i]) * scale;
						}
						else if (v == VOICE_SNARE2) {
							float scale = wildMode ? 0.85f : 0.60f;
							val = baseVP.steps[i] + (val - baseVP.steps[i]) * scale;
						}

						// --- Kick protection ---
						// Never alter kick within first 10% of pattern
						if (v == VOICE_KICK && i < baseSteps / 10) {
							val = baseVP.steps[i];
						}

						// --- Snare anchor protection ---
						// Protect main backbeat zone: midpoint ± numSteps/16
						if (v == VOICE_SNARE1) {
							int mid = baseSteps / 2;
							int window = baseSteps / 16;
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
			for (int i = baseSteps; i < MAX_STEPS; i++) {
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
				// Snares rarely get Euclidean rewrites — only at extreme depth
				// and with low probability, to avoid mechanical grid patterns.
				if ((v == VOICE_SNARE1 || v == VOICE_SNARE2) &&
				    (wildDepth < 0.92f || random::uniform() > 0.30f)) continue;
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

		rebuildCorrectedPatternBuffer(correctedMutatedPattern, mutatedPattern, currentPattern, true);
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
		currentMutateNorm = cvNorm;

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
	Trigonomicon() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(CLOCK_RES_PARAM, 0.f, 2.f, 0.f, "Clock resolution", {"1", "/2", "x2"});

		// Pattern select: integer snapping 0-99
		configParam(PATTERN_SELECT_PARAM, 0.f, 129.f, 0.f, "Pattern Select");
		getParamQuantity(PATTERN_SELECT_PARAM)->snapEnabled = true;

		configSwitch(BASS_SCALE_PARAM, 0.f, (float)(BASS_SCALE_OPTION_COUNT - 1), 1.f, "Bass scale", {
			"3b7+",
			"Minor (Aeolian)",
			"Phrygian",
			"Dorian",
			"Chromatic",
			"Diminished (Half-Whole)",
			"Blues Scale",
			"Minor Pentatonic",
			"Locrian",
			"Harmonic Minor"
		});
		getParamQuantity(BASS_SCALE_PARAM)->snapEnabled = true;

		// Mutate amount: continuous 0.0-1.0
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate Amount", "%", 0.f, 100.f);

		// Wild mode toggle
		configSwitch(WILD_MODE_PARAM, 0.f, 1.f, 0.f, "Wild Mutate", {"Off", "On"});

		// Label inputs
		configInput(CLOCK_INPUT, "Clock");
		configInput(FILL_INPUT, "Fill (gate+voltage)");
		configInput(RESET_INPUT, "Reset");
		configInput(MUTATE_INPUT, "Mutate CV");
		configInput(CLOCK_RES_INPUT, "Clock resolution CV (0V=1, 5V=/2, 10V=x2)");

		configInput(BASS_OCT_INPUT, "Bass octave CV (-1V to +1V, 1V/oct)");
		configInput(PATTERN_SELECT_INPUT, "Pattern select CV (0-10V)");

		// Label outputs
		configOutput(KICK_OUTPUT, "Kick");
		configOutput(SNARE1_OUTPUT, "Snare 1");
		configOutput(SNARE2_OUTPUT, "Snare 2");
		configOutput(CLOSED_HAT_OUTPUT, "Closed HiHat");
		configOutput(OPEN_HAT_OUTPUT, "Open HiHat");
		configOutput(RIDE_CRASH_OUTPUT, "Ride/Crash");
		configOutput(ACCENT_POLY_OUTPUT, "Accent bus (polyphonic)");
		configOutput(BASS_TRIG_OUTPUT, "Bassline trigger");
		configOutput(BASS_CV_OUTPUT, "Bassline CV");

		// Initialize mutation buffer to silence
		for (int v = 0; v < NUM_VOICES; v++) {
			mutatedPattern.voices[v].numSteps = 1;
			for (int i = 0; i < MAX_STEPS; i++) {
				mutatedPattern.voices[v].steps[i] = 0.f;
			}
		}

		resetBasslineState();
	}

	void applyPatternSelection(int patternIndex, bool hasClock) {
		if (patternIndex < 0) patternIndex = 0;
		if (patternIndex >= NUM_PATTERNS) patternIndex = NUM_PATTERNS - 1;
		if (patternIndex == lastPattern) {
			return;
		}

		rebuildCorrectedPatternBuffer(correctedBasePattern, PATTERNS[patternIndex], patternIndex, false);
		correctedMutatedPattern = correctedBasePattern;
		for (int v = 0; v < NUM_VOICES; v++) {
			voiceStep[v] = 0;
			pulseGens[v].reset();
		}
		lastPattern = patternIndex;
		pendingPatternCvIndex = -1;
		patternStepsSinceChange = 0;
		clearTrackerFxState();
		clearRatchetState();
		clearScheduledVoicePulses();
		clearWildPhraseState();
		clearClockSubticks();

		chooseDonorPattern(patternIndex);
		rollSegmentThresholds();
		mutationDirty = true;
		mutationPending = true;
		resetBasslineState();
		if (!hasClock) {
			applySegmentMutation(patternIndex);
			lastSegmentAmountBuilt = segmentAmount;
			mutationDirty = false;
			mutationPending = false;
			mutateClockCounter = 0;
		}
	}

	// ----------------------------------------------------------------
	// DSP process (called every sample)
	// ----------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		bool hasClock = inputs[CLOCK_INPUT].isConnected();

		// --- Read pattern select knob ---
		int requestedPatternIndex = (int) params[PATTERN_SELECT_PARAM].getValue();
		if (inputs[PATTERN_SELECT_INPUT].isConnected()) {
			float patternCv = math::clamp(inputs[PATTERN_SELECT_INPUT].getVoltage(), 0.f, 10.f);
			requestedPatternIndex = (int)std::lround((patternCv / 10.f) * (NUM_PATTERNS - 1));
		}
		if (requestedPatternIndex < 0) requestedPatternIndex = 0;
		if (requestedPatternIndex >= NUM_PATTERNS) requestedPatternIndex = NUM_PATTERNS - 1;

		bool cvPatternControlActive = inputs[PATTERN_SELECT_INPUT].isConnected() && hasClock;
		if (cvPatternControlActive) {
			if (lastPattern < 0) {
				applyPatternSelection(requestedPatternIndex, hasClock);
			}
			if (requestedPatternIndex != lastPattern) {
				pendingPatternCvIndex = requestedPatternIndex;
			} else {
				pendingPatternCvIndex = -1;
			}
		} else {
			applyPatternSelection(requestedPatternIndex, hasClock);
			pendingPatternCvIndex = -1;
		}

		int patternIndex = (lastPattern >= 0) ? lastPattern : requestedPatternIndex;

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
			clearScheduledVoicePulses();
			clearWildPhraseState();
			clearClockSubticks();
			pendingPatternCvIndex = -1;
			patternStepsSinceChange = 0;
			resetBasslineState();
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
			// Read bass octave CV — snap to nearest integer octave, clamp -2..+1.
			if (inputs[BASS_OCT_INPUT].isConnected()) {
				bassOctavePending = (float)math::clamp((int)std::lround(inputs[BASS_OCT_INPUT].getVoltage()), -2, 1);
			} else {
				bassOctavePending = 0.f;
			}
		}

		// --- Handle clock input ---
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
			if (pendingPatternCvIndex >= 0 && patternStepsSinceChange >= 4) {
				applyPatternSelection(pendingPatternCvIndex, hasClock);
			}
			patternIndex = (lastPattern >= 0) ? lastPattern : requestedPatternIndex;

			uint32_t currentClockDeltaSamples = samplesSinceLastClock;
			samplesSinceLastClock = 0;
			uint32_t minClockSamples = (uint32_t)std::lround(args.sampleRate * 0.015f);
			uint32_t maxClockSamples = (uint32_t)std::lround(args.sampleRate * 2.0f);
			if (currentClockDeltaSamples >= minClockSamples && currentClockDeltaSamples <= maxClockSamples) {
				clockIntervalSamples = currentClockDeltaSamples;
			}
			bool advanceThisClock = shouldAdvanceOnExternalClock();
			if (advanceThisClock) {
				scheduleClockSubticks();
			} else {
				pendingClockSubticks = 0;
				clockSubtickSamplesRemaining = 0;
				clockSubtickIntervalSamples = 0;
			}
			if (!wildMode) {
				bassWildDoubleTimeActive = false;
				bassWildSkipEvery = 0;
				bassWildSkipCounter = 0;
				bassOffbeatPending = false;
				bassOffbeatSamplesRemaining = 0u;
			}
			if (bassCycleClock % BASS_WILD_WINDOW_CLOCKS == 0) {
				beginBassWildWindow();
			}
			bassMinTriggerSpacingSamples = 1u;

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
								clearRatchetForVoice(VOICE_RIDE);
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

			if (advanceThisClock) {
				advanceSequencerTick(args, patternIndex);
			}
		}
		else if (pendingClockSubticks > 0) {
			if (clockSubtickSamplesRemaining > 0) {
				clockSubtickSamplesRemaining--;
			}
			if (clockSubtickSamplesRemaining == 0) {
				advanceSequencerTick(args, patternIndex);
				pendingClockSubticks--;
				clockSubtickSamplesRemaining = (pendingClockSubticks > 0) ? clockSubtickIntervalSamples : 0;
			}
		}

		// --- Update outputs ---
		float dt = args.sampleTime;
		uint32_t maxClockSamples = (uint32_t)std::lround(args.sampleRate * 10.f);
		if (samplesSinceLastClock < maxClockSamples) {
			samplesSinceLastClock++;
		}
		if (bassSamplesSinceTrigger < maxClockSamples) {
			bassSamplesSinceTrigger++;
		}
		// Wild skip pattern: autonomous timer removed; skipping is handled
		// at the candidate-fire site above (drum-grid-locked, no timer needed).

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
			if (pendingVoicePulse[v]) {
				if (pendingVoicePulseSamples[v] > 0u) {
					pendingVoicePulseSamples[v]--;
				}
				if (pendingVoicePulseSamples[v] == 0u) {
					accentHoldVoltage[v] = pendingVoiceAccentVoltage[v];
					triggerVoicePulse(v, pendingVoicePulseDur[v]);
					pendingVoicePulse[v] = false;
				}
			}
			if (ratchetsRemaining[v] <= 0) continue;
			if (ratchetSamplesRemaining[v] > 0) {
				ratchetSamplesRemaining[v]--;
			}
			if (ratchetSamplesRemaining[v] == 0) {
				float trigDur = ratchetPulseDuration(ratchetIntervalSamples[v], args.sampleTime, 5e-3f);
				accentHoldVoltage[v] = ratchetAccentVoltage[v];
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
		bool ridePlays  = hatPlays;
		if (bassStateInitialized) {
			// Exponential portamento convergence (SlideWyrm-style).
			// On non-slide triggers bassSlideActive is false and bassCurrentCv == bassTargetCv,
			// so this branch exits immediately with no audible effect.
			if (bassSlideActive) {
				bassCurrentCv += (bassTargetCv - bassCurrentCv) * bassSlideCoeff;
				if (std::abs(bassTargetCv - bassCurrentCv) < 0.0001f) {
					bassCurrentCv = bassTargetCv;
					bassSlideActive = false;
				}
			} else {
				bassCurrentCv = bassTargetCv;
			}
		} else {
			bassCurrentCv = bassTargetCv;
		}
		outputs[KICK_OUTPUT].setVoltage(kickPlays && pulseGens[VOICE_KICK].process(dt) ? 10.f : 0.f);
		outputs[SNARE1_OUTPUT].setVoltage(snarePlays && pulseGens[VOICE_SNARE1].process(dt) ? 10.f : 0.f);
		outputs[SNARE2_OUTPUT].setVoltage(snarePlays && pulseGens[VOICE_SNARE2].process(dt) ? 10.f : 0.f);
		outputs[CLOSED_HAT_OUTPUT].setVoltage(hatPlays && pulseGens[VOICE_CHAT].process(dt) ? 10.f : 0.f);
		outputs[OPEN_HAT_OUTPUT].setVoltage(hatPlays && pulseGens[VOICE_OHAT].process(dt) ? 10.f : 0.f);
		outputs[RIDE_CRASH_OUTPUT].setVoltage(ridePlays && pulseGens[VOICE_RIDE].process(dt) ? 10.f : 0.f);
		outputs[ACCENT_POLY_OUTPUT].setChannels(NUM_VOICES);
		for (int v = 0; v < NUM_VOICES; ++v) {
			outputs[ACCENT_POLY_OUTPUT].setVoltage(accentHoldVoltage[v], v);
		}
		outputs[BASS_TRIG_OUTPUT].setVoltage(bassTriggerPulse.process(dt) ? 10.f : 0.f);
		outputs[BASS_CV_OUTPUT].setVoltage(bassCurrentCv + bassOctaveActive);
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
		json_object_set_new(rootJ, "bassCycleClock", json_integer(bassCycleClock));
		json_object_set_new(rootJ, "bassSourceRecipe", json_integer(bassSourceRecipe));
		json_object_set_new(rootJ, "bassPitchIndex", json_integer(bassPitchIndex));
		json_object_set_new(rootJ, "bassCurrentCv", json_real(bassCurrentCv));
		json_object_set_new(rootJ, "bassTargetCv", json_real(bassTargetCv));
		json_object_set_new(rootJ, "bassSlideStartCv", json_real(bassSlideStartCv));
		json_object_set_new(rootJ, "bassHatThinProbability", json_real(bassHatThinProbability));

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

		json_t* bassCycleJ = json_object_get(rootJ, "bassCycleClock");
		if (bassCycleJ && json_is_integer(bassCycleJ)) {
			bassCycleClock = math::clamp((int)json_integer_value(bassCycleJ), 0, BASS_CYCLE_CLOCKS - 1);
		}

		json_t* bassRecipeJ = json_object_get(rootJ, "bassSourceRecipe");
		if (bassRecipeJ && json_is_integer(bassRecipeJ)) {
			bassSourceRecipe = math::clamp((int)json_integer_value(bassRecipeJ), 0, BASS_SOURCE_COUNT - 1);
		}

		json_t* bassPitchJ = json_object_get(rootJ, "bassPitchIndex");
		if (bassPitchJ && json_is_integer(bassPitchJ)) {
			bassPitchIndex = clampBassPitchIndex((int)json_integer_value(bassPitchJ), false);
		}

		json_t* bassCurrentJ = json_object_get(rootJ, "bassCurrentCv");
		if (bassCurrentJ && json_is_number(bassCurrentJ)) {
			bassCurrentCv = (float)json_number_value(bassCurrentJ);
		}

		json_t* bassTargetJ = json_object_get(rootJ, "bassTargetCv");
		if (bassTargetJ && json_is_number(bassTargetJ)) {
			bassTargetCv = (float)json_number_value(bassTargetJ);
		}

		json_t* bassSlideStartJ = json_object_get(rootJ, "bassSlideStartCv");
		if (bassSlideStartJ && json_is_number(bassSlideStartJ)) {
			bassSlideStartCv = (float)json_number_value(bassSlideStartJ);
		}
		else {
			bassSlideStartCv = bassCurrentCv;
		}

		json_t* bassThinJ = json_object_get(rootJ, "bassHatThinProbability");
		if (bassThinJ && json_is_number(bassThinJ)) {
			bassHatThinProbability = math::clamp((float)json_number_value(bassThinJ), 0.50f, 0.70f);
		}

		bassStateInitialized = true;
		bassSamplesSinceTrigger = 0xffffffffu;
		bassSlideSamplesRemaining = 0u;
		bassSlideTotalSamples = 0u;
		bassOffbeatPending = false;
		bassOffbeatSamplesRemaining = 0u;
		bassWildDoubleTimeActive = false;

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
	Trigonomicon* module;

	static const char* getGenreForPatternLCD(int p) {
		return getGenreForPattern(p);
	}

	void draw(const DrawArgs& args) override {
		// Determine genre
		const char* genre = "BREAKBEAT / AMEN";
		char displayBuf[64];
		if (module) {
			int p = module->lastPattern;
			if (p < 0) {
				p = (int)module->params[Trigonomicon::PATTERN_SELECT_PARAM].getValue();
			}
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

struct TrigonomiconWidget : ModuleWidget {
	TrigonomiconWidget(Trigonomicon* module) {
		setModule(module);

		// Load the panel from packaged res/
		setPanel(createPanel(asset::plugin(pluginInstance, "res/trigonomicon.svg")));

#ifndef METAMODULE
		// PNG faceplate from res/ (canonical location). Keep SVG for correct sizing.
		std::string panelPath = asset::plugin(pluginInstance, "res/trigonomicon.png");
		auto* panelBg = new bem::PngPanelBackground(panelPath);
		panelBg->box.pos = Vec(0, 0);
		panelBg->box.size = box.size;
		addChild(panelBg);
#endif

		// Layout (10HP = 50.8mm wide). No labels; all text/screen art is on the faceplate PNG.
		const float centerX = 25.4f;
		const float knobLeftX = 14.12f;
		const float knobCenterX = 25.4f;
		const float knobRightX = 36.68f;
		const float controlShiftUpMm = 25.0f / 3.7795275591f;
		const float knobY = 52.0f - controlShiftUpMm;
		const float ioRowY = 73.0f - controlShiftUpMm;
		const float outStartY = 82.0f - controlShiftUpMm;
		const float outSpacingY = 9.5f;
		const Vec portShiftPx = Vec(0.f, -10.f);
		const Vec topInputShiftPx = Vec(0.f, -20.f);
		const Vec knobRowShiftPx = Vec(-6.f, -8.f);
		const Vec drumOutShiftPx = Vec(0.f, -20.f);
		const Vec bassRowShiftPx = Vec(0.f, -22.f);

		#ifndef METAMODULE
			struct TrigonomiconPort : MVXPort {
				TrigonomiconPort() {
					imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
					imageHandle = -1;
				}
			};
			struct TrigonomiconKnob : MVXKnob {
				TrigonomiconKnob() {
					box.size = box.size.mult(0.96f);
				}
			};
		#else
			using TrigonomiconPort = MVXPort;
			// MetaModule expects SliderKnob-derived params for addParam().
			using TrigonomiconKnob = app::SliderKnob;
		#endif

		// Restore only the pattern display text on the screen.
		#ifndef METAMODULE
		{
			constexpr float screenX = 2.9f;
			constexpr float screenY = 26.0f;
			constexpr float screenW = 45.0f;
			constexpr float screenH = 6.0f * 1.2f;
			GenreLCDDisplay* lcd = new GenreLCDDisplay();
			lcd->box.pos = mm2px(Vec(screenX, screenY));
			lcd->box.size = mm2px(Vec(screenW, screenH));
			lcd->module = module;
			addChild(lcd);
		}
		#else
		{
			struct TrigonomiconMMDisplay : MetaModule::VCVTextDisplay {};
			auto* mmDisplay = new TrigonomiconMMDisplay();
			mmDisplay->box.pos = mm2px(Vec(2.9f, 30.0f)).minus(Vec(0.f, 2.f));
			mmDisplay->box.size = mm2px(Vec(45.0f, 8.0f));
			mmDisplay->font = "Default_14";
			mmDisplay->color = RGB565{(uint8_t)57, 255, 20};
			mmDisplay->firstLightId = Trigonomicon::GENRE_DISPLAY;
			addChild(mmDisplay);
		}
		#endif

		// Knobs (as-is)
		const float clockResSwitchX = knobLeftX - 8.5f;
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(clockResSwitchX, knobY)).plus(knobRowShiftPx), module, Trigonomicon::CLOCK_RES_PARAM));
		addParam(createParamCentered<TrigonomiconKnob>(mm2px(Vec(knobLeftX, knobY)).plus(knobRowShiftPx), module, Trigonomicon::PATTERN_SELECT_PARAM));
		addParam(createParamCentered<TrigonomiconKnob>(mm2px(Vec(knobCenterX, knobY)).plus(knobRowShiftPx), module, Trigonomicon::MUTATE_PARAM));
		addParam(createParamCentered<TrigonomiconKnob>(mm2px(Vec(knobRightX, knobY)).plus(knobRowShiftPx), module, Trigonomicon::BASS_SCALE_PARAM));

		// WILD toggle switch — to the right of the MUTATE knob, same row
		const float wildSwitchX = knobRightX + 8.5f;
		const float wildSwitchY = knobY;
		addParam(createParamCentered<CKSS>(mm2px(Vec(wildSwitchX, wildSwitchY)).plus(knobRowShiftPx), module, Trigonomicon::WILD_MODE_PARAM));

		// Inputs row (CLK / FILL / RST / MCV) — 4 ports, tighter spacing
		const float inRowLeft = 8.5f;
		const float inSpacing = 11.0f;
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft, ioRowY)).plus(portShiftPx).plus(topInputShiftPx), module, Trigonomicon::CLOCK_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing, ioRowY)).plus(portShiftPx).plus(topInputShiftPx), module, Trigonomicon::FILL_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing * 2.f, ioRowY)).plus(portShiftPx).plus(topInputShiftPx), module, Trigonomicon::RESET_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(mm2px(Vec(inRowLeft + inSpacing * 3.f, ioRowY)).plus(portShiftPx).plus(topInputShiftPx), module, Trigonomicon::MUTATE_INPUT));

		int outIds[] = {
			Trigonomicon::KICK_OUTPUT, Trigonomicon::SNARE1_OUTPUT,
			Trigonomicon::SNARE2_OUTPUT, Trigonomicon::CLOSED_HAT_OUTPUT,
			Trigonomicon::OPEN_HAT_OUTPUT, Trigonomicon::RIDE_CRASH_OUTPUT
		};

		// Drum outputs (center column)
		for (int i = 0; i < 6; i++) {
			float y = outStartY + i * outSpacingY;
			addOutput(createOutputCentered<TrigonomiconPort>(mm2px(Vec(centerX, y)).plus(portShiftPx).plus(drumOutShiftPx), module, outIds[i]));
		}

		const float bassOutY = outStartY + 6.f * outSpacingY;
		float bassRowYPx = mm2px(Vec(0.f, bassOutY)).y + portShiftPx.y + bassRowShiftPx.y;
		const float bassRowLeftXPx = 18.f;
		const float bassRowRightXPx = box.size.x - 18.f;
		const float bassRowSpacingPx = (bassRowRightXPx - bassRowLeftXPx) / 4.f;
		const float accentOutX = box.size.x - 18.f;
		const float accentOutY = mm2px(Vec(0.f, outStartY + outSpacingY * 2.3f)).y + portShiftPx.y + drumOutShiftPx.y;
		addInput(createInputCentered<TrigonomiconPort>(Vec(bassRowLeftXPx, bassRowYPx), module, Trigonomicon::CLOCK_RES_INPUT));
		addOutput(createOutputCentered<TrigonomiconPort>(Vec(bassRowLeftXPx + bassRowSpacingPx, bassRowYPx), module, Trigonomicon::BASS_TRIG_OUTPUT));
		addOutput(createOutputCentered<TrigonomiconPort>(Vec(bassRowLeftXPx + bassRowSpacingPx * 2.f, bassRowYPx), module, Trigonomicon::BASS_CV_OUTPUT));
		addInput(createInputCentered<TrigonomiconPort>(Vec(bassRowLeftXPx + bassRowSpacingPx * 3.f, bassRowYPx), module, Trigonomicon::BASS_OCT_INPUT));
		addInput(createInputCentered<TrigonomiconPort>(Vec(bassRowLeftXPx + bassRowSpacingPx * 4.f, bassRowYPx), module, Trigonomicon::PATTERN_SELECT_INPUT));
		addOutput(createOutputCentered<TrigonomiconPort>(Vec(accentOutX, accentOutY), module, Trigonomicon::ACCENT_POLY_OUTPUT));

	}
};

// ============================================================================
// Register the module
// ============================================================================

Model* modelTrigonomicon = createModel<Trigonomicon, TrigonomiconWidget>("Trigonomicon");
