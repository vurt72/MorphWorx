// SlideWyrm - TB-303 Style Acid Pattern Generator
// Ported from TB-3PO Hemisphere applet for Ornament & Crime
// Original code by Logarhythm (MIT License)
// VCV Rack port by Bemushroomed

#include "plugin.hpp"
#include "ui/PngPanelBackground.hpp"
#include "Quantizer.hpp"
#include <cmath>

using namespace slidewyrm;

#define ACID_HALF_STEPS 16
#define ACID_MAX_STEPS 32

// Steps choices for the 3-way switch
static const int STEPS_CHOICES[] = {8, 16, 32};
static constexpr int CONTROL_RATE_SAMPLES = 64;
static constexpr int LIGHT_RATE_SAMPLES = 64;

static inline float fastExp2Approx(float x) {
	int i = (int)std::floor(x);
	float f = x - (float)i;
	float poly = 1.0f + f * (0.69314718f + f * (0.24022651f + f * 0.05550411f));
	return std::ldexp(poly, i);
}

static inline float fastExpApprox(float x) {
	return fastExp2Approx(x * 1.44269504089f);
}

// Custom ParamQuantity for Scale that shows names
struct ScaleParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int idx = (int)getValue();
		if (idx >= 0 && idx < NUM_SCALES) {
			return scale_names[idx];
		}
		return ParamQuantity::getDisplayValueString();
	}
};

struct SlideWyrm : Module {
	enum ParamId {
		DENSITY_PARAM,
		SCALE_PARAM,
		ROOT_PARAM,
		OCTAVE_PARAM,
		SEED_PARAM,
		MANUAL_REGEN_PARAM,
		SLIDE_AMT_PARAM,
		GATE_LEN_PARAM,
		LOCK_SEED_PARAM,
		STEPS_PARAM,
		GATE_MODE_PARAM,
		ACCENT_SHAPE_PARAM,
		ACCENT_MODE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		TRANSPOSE_INPUT,
		DENSITY_CV_INPUT,
		REGEN_INPUT,
		SLIDE_CV_INPUT,
		GATE_LEN_CV_INPUT,
		ROOT_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PITCH_OUTPUT,
		GATE_OUTPUT,
		ACCENT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		SLIDE_LIGHT,
		ACCENT_LIGHT,
		OCTAVE_UP_LIGHT,
		OCTAVE_DOWN_LIGHT,
		LIGHTS_LEN
	};

	// Pattern generation
	Quantizer quantizer;
	// Fast PRNG (replaces std::mt19937: 2.5KB state → 16 bytes)
	uint32_t rngState[4] = {1, 2, 3, 4};
	
	void rngSeed(uint32_t s) {
		// SplitMix32 expansion
		auto sm = [](uint32_t z) -> uint32_t {
			z += 0x9e3779b9; z ^= z >> 16; z *= 0x85ebca6b;
			z ^= z >> 13; z *= 0xc2b2ae35; z ^= z >> 16; return z;
		};
		rngState[0] = sm(s);
		rngState[1] = sm(rngState[0]);
		rngState[2] = sm(rngState[1]);
		rngState[3] = sm(rngState[2]);
		if (rngState[0] == 0 && rngState[1] == 0 && rngState[2] == 0 && rngState[3] == 0)
			rngState[3] = 1;
	}
	
	uint32_t rngNext() {
		uint32_t t = rngState[1] * 5;
		uint32_t result = ((t << 7) | (t >> 25)) * 9;
		t = rngState[1] << 9;
		rngState[2] ^= rngState[0];
		rngState[3] ^= rngState[1];
		rngState[1] ^= rngState[2];
		rngState[0] ^= rngState[3];
		rngState[2] ^= t;
		rngState[3] = (rngState[3] << 11) | (rngState[3] >> 21);
		return result;
	}
	
	int rngInt(int min, int max) {
		uint32_t range = static_cast<uint32_t>(max - min + 1);
		return min + static_cast<int>(rngNext() % range);
	}
	
	// User settings
	uint16_t seed = 0;
	bool lockSeed = false;
	int scale = 15;  // GUNA scale (sounds cool)
	int root = 0;
	int octaveOffset = 0;
	int densityEncoder = 7;  // Center point (0-14 range)
	int numSteps = 16;
	
	// Generated sequence data
	uint32_t gates = 0;
	uint32_t slides = 0;
	uint32_t accents = 0;
	uint32_t octUps = 0;
	uint32_t octDowns = 0;
	uint8_t notes[ACID_MAX_STEPS];
	
	// Playback state
	int step = 0;
	int currentDensity = 7;
	int currentPatternDensity = 7;
	int currentPatternScaleSize = 7;
	
	// Timing
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger regenTrigger;
	dsp::PulseGenerator gatePulse;
	int64_t sampleCounter = 0;
	int64_t gateOffSample = 0;
	int64_t cycleTimeSamples = 0;
	int64_t lastClockSample = 0;
	
	// CV output values
	float currGateCV = 0.f;
	float currPitchCV = 0.f;
	
	// Pitch slide tracking
	float slideStartCV = 0.f;
	float slideEndCV = 0.f;
	
	// Slide amount (0=none, 1=slight, 2=medium, 3=high)
	float slideAmount = 3.f;
	float cachedSlideCoeff = 1.0f;  // Precomputed at clock edge
	
	// Gate length in seconds
	float gateLengthSec = 0.5f;
	
	// Regen cooldown
	int64_t lastRegenSample = 0;
	static const int64_t REGEN_COOLDOWN_MS = 50;
	
	// Accent envelope
	float accentEnvLevel = 0.f;
	bool accentActive = false;
	int64_t accentTriggerSample = 0;
	int accentShape = 0;   // 0=snap, 1=reverse, 2=medium
	int accentMode = 0;    // 0=every 4th, 1=every 8th, 2=random
	float accentDecayCoeff = 0.99f; // Precomputed exp decay per sample
	float accentAtkSamples = 48.f;  // Attack time in samples
	float accentTotalSamples = 960.f; // Total envelope duration in samples
	bool accentInDecay = false;
	int controlRateDivider = 0;
	int lightRateDivider = 0;

	// Cached step flags for output/lights (updated on clock edge)
	bool currStepIsSlide = false;
	bool currStepIsAccent = false;
	bool currStepIsOctUp = false;
	bool currStepIsOctDown = false;
	
	// Display
	int currStepSemitone = 0;
	int regeneratePhase = 0;
	
	SlideWyrm() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		
		configParam(DENSITY_PARAM, 0.f, 14.f, 7.f, "Density", "", 0.f, 1.f, -7.f);
		configParam<ScaleParamQuantity>(SCALE_PARAM, 0.f, NUM_SCALES - 1.f, 15.f, "Scale");
		getParamQuantity(SCALE_PARAM)->snapEnabled = true;
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root",
			{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		configParam(OCTAVE_PARAM, -3.f, 3.f, 0.f, "Octave");
		getParamQuantity(OCTAVE_PARAM)->snapEnabled = true;
		configSwitch(STEPS_PARAM, 0.f, 2.f, 1.f, "Steps", {"8", "16", "32"});
		configParam(SEED_PARAM, 0.f, 65535.f, 0.f, "Seed");
		configSwitch(LOCK_SEED_PARAM, 0.f, 1.f, 0.f, "Lock Seed", {"Unlocked", "Locked"});
		configButton(MANUAL_REGEN_PARAM, "Regenerate");
		configParam(SLIDE_AMT_PARAM, 0.f, 3.f, 3.f, "Slide Amount", "", 0.f, 1.f, 0.f);
		getParamQuantity(SLIDE_AMT_PARAM)->snapEnabled = true;
			configParam(GATE_LEN_PARAM, 0.f, 1.f, 0.5f, "Gate Length", " ms", 0.f, 93.7f, 6.3f);
		configSwitch(ACCENT_SHAPE_PARAM, 0.f, 2.f, 0.f, "Accent Shape", {"Snap", "Reverse", "Medium"});
		configSwitch(ACCENT_MODE_PARAM, 0.f, 2.f, 0.f, "Accent Mode", {"Every 4", "Every 8", "Random"});
		configSwitch(GATE_MODE_PARAM, 0.f, 2.f, 0.f, "Gate Mode", {"Off", "Cycle", "Slide"});
		
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(TRANSPOSE_INPUT, "Transpose CV");
		configInput(DENSITY_CV_INPUT, "Density CV");
		configInput(REGEN_INPUT, "Regenerate trigger");
		configInput(SLIDE_CV_INPUT, "Slide Amount CV");
		configInput(GATE_LEN_CV_INPUT, "Gate Length CV");
		configInput(ROOT_CV_INPUT, "Root select CV (1V/oct, pitch-class)");
		
		configOutput(PITCH_OUTPUT, "Pitch (1V/oct)");
		configOutput(GATE_OUTPUT, "Gate/Accent");
		configOutput(ACCENT_OUTPUT, "Accent Envelope");
		
		configLight(SLIDE_LIGHT, "Slide");
		configLight(ACCENT_LIGHT, "Accent");
		configLight(OCTAVE_UP_LIGHT, "Octave Up");
		configLight(OCTAVE_DOWN_LIGHT, "Octave Down");
		
		// Initialize
		reseed();
		regenerateAll();
	}
	
	void reseed() {
		// Use a counter-based seed instead of std::random_device
		static uint32_t seedCounter = 12345;
		seed = (++seedCounter) & 0xFFFF;
	}
	
	void regenerateAll() {
		regeneratePhase = 1;
	}
	
	void regeneratePitches() {
		bool bFirstHalf = regeneratePhase < 3;
		
		int pitchChangeDens = getPitchChangeDensity();
		int scaleSize = quantizer.getScaleSize();
		int availablePitches = 0;
		
		if (scaleSize > 0) {
			if (pitchChangeDens > 7) {
				availablePitches = scaleSize - 1;
			} else if (pitchChangeDens < 2) {
				availablePitches = pitchChangeDens;
			} else {
				int rangeFromScale = scaleSize - 3;
				if (rangeFromScale < 4) rangeFromScale = 4;
				availablePitches = 3 + (pitchChangeDens - 3) * rangeFromScale / 4;
				availablePitches = clamp(availablePitches, 1, scaleSize - 1);
			}
		}
		
		if (bFirstHalf) {
			octUps = 0;
			octDowns = 0;
		}
		
		int maxStep = bFirstHalf ? ACID_HALF_STEPS : ACID_MAX_STEPS;
		for (int s = (bFirstHalf ? 0 : ACID_HALF_STEPS); s < maxStep; s++) {
			int forceRepeatNoteProb = 50 - (pitchChangeDens * 6);
			if (s > 0 && randBit(forceRepeatNoteProb)) {
				notes[s] = notes[s - 1];
			} else {
				notes[s] = rngInt(0, availablePitches);
				
				octUps <<= 1;
				octDowns <<= 1;
				
				if (randBit(40)) {
					if (randBit(50)) {
						octUps |= 0x1;
					} else {
						octDowns |= 0x1;
					}
				}
			}
		}
		
		if (scaleSize == 0) scaleSize = 12;
		currentPatternScaleSize = scaleSize;
	}
	
	void applyDensity() {
		int latestSlide = 0;
		int latestAccent = 0;
		
		int onOffDens = getOnOffDensity();
		int densProb = 10 + onOffDens * 14;
		
		bool bFirstHalf = regeneratePhase < 3;
		if (bFirstHalf) {
			gates = 0;
			slides = 0;
			accents = 0;
		}
		
		for (int i = 0; i < ACID_HALF_STEPS; ++i) {
			gates <<= 1;
			gates |= randBit(densProb);
			
			slides <<= 1;
			latestSlide = randBit(latestSlide ? 10 : 18);
			slides |= latestSlide;
			
			accents <<= 1;
			latestAccent = randBit(latestAccent ? 7 : 16);
			accents |= latestAccent;
		}
		
		currentPatternDensity = currentDensity;
	}
	
	void updateRegeneration() {
		if (regeneratePhase == 0) return;
		
		rngSeed(seed + regeneratePhase);
		
		switch (regeneratePhase) {
			case 1: regeneratePitches(); ++regeneratePhase; break;
			case 2: applyDensity(); ++regeneratePhase; break;
			case 3: regeneratePitches(); ++regeneratePhase; break;
			case 4: applyDensity(); regeneratePhase = 0; break;
			default: break;
		}
	}
	
	int randBit(int prob) {
		return (rngInt(1, 100) <= prob) ? 1 : 0;
	}
	
	int getOnOffDensity() {
		int noteDens = currentDensity - 7;
		return abs(noteDens);
	}
	
	int getPitchChangeDensity() {
		return clamp(currentDensity, 0, 8);
	}
	
	bool stepIsGated(int stepNum) {
		return (gates & (0x01 << stepNum));
	}
	
	bool stepIsSlid(int stepNum) {
		return (slides & (0x01 << stepNum));
	}
	
	bool stepIsAccent(int stepNum) {
		return (accents & (0x01 << stepNum));
	}
	
	bool stepIsOctUp(int stepNum) {
		return (octUps & (0x01 << stepNum));
	}
	
	bool stepIsOctDown(int stepNum) {
		return (octDowns & (0x01 << stepNum));
	}
	
	int getNextStep(int stepNum) {
		if (++stepNum >= numSteps) {
			return 0;
		}
		return stepNum;
	}
	
	float getPitchForStep(int stepNum, float transposeCV) {
		const Scale& sc = scales[scale];
		int scaleSize = sc.num_notes;
		if (scaleSize <= 0) scaleSize = 1;

		int degree = notes[stepNum];
		int oct = octaveOffset;

		// Per-step octave decorations
		if (stepIsOctUp(stepNum)) oct++;
		else if (stepIsOctDown(stepNum)) oct--;

		// Wrap degree into valid scale range
		while (degree >= scaleSize) { degree -= scaleSize; oct++; }
		while (degree < 0) { degree += scaleSize; oct--; }

		// Convert to V/oct: root shifts the tonic, scale degree gives semitone offset
		int semitone = sc.notes[degree] + root;
		float voltage = (float)oct + semitone / 12.0f;

		return voltage + transposeCV;
	}

	static int pitchClassFromPitchVoltage(float pitchVoltage) {
		// Match SlideWyrm pitch output convention: 1V/oct, 1/12V per semitone, C = 0.
		// We only care about pitch class (0-11) for ROOT.
		int semitone = (int)std::lround(pitchVoltage * 12.f);
		int pc = semitone % 12;
		if (pc < 0)
			pc += 12;
		return pc;
	}
	
	int getSemitoneForStep(int stepNum) {
		const Scale& sc = scales[scale];
		int scaleSize = sc.num_notes;
		if (scaleSize <= 0) scaleSize = 1;
		int degree = notes[stepNum] % scaleSize;
		if (degree < 0) degree += scaleSize;
		return (sc.notes[degree] + root) % 12;
	}
	
	void process(const ProcessArgs& args) override {
		sampleCounter++;
		float transposeCV = 0.f;
		
		// Control-rate parameter reads
		if (++controlRateDivider >= CONTROL_RATE_SAMPLES) {
			controlRateDivider = 0;
			scale = (int)params[SCALE_PARAM].getValue();
			octaveOffset = (int)params[OCTAVE_PARAM].getValue();
			int stepsIdx = (int)params[STEPS_PARAM].getValue();
			numSteps = STEPS_CHOICES[clamp(stepsIdx, 0, 2)];
			lockSeed = params[LOCK_SEED_PARAM].getValue() > 0.5f;
			
			if (!lockSeed) {
				seed = (uint16_t)params[SEED_PARAM].getValue();
			}
			
			quantizer.setScale(scale);
			accentShape = (int)params[ACCENT_SHAPE_PARAM].getValue();
			accentMode = (int)params[ACCENT_MODE_PARAM].getValue();
		}
		
		// Reset/regenerate (button or reset input) — with cooldown
		{
			int64_t regenCooldownSamples = (int64_t)(REGEN_COOLDOWN_MS * 0.001f * args.sampleRate);
			bool canRegen = (sampleCounter - lastRegenSample) > regenCooldownSamples;
			
			if (canRegen && (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f) || 
			    params[MANUAL_REGEN_PARAM].getValue() > 0.5f)) {
				if (!lockSeed) {
					reseed();
				}
				regenerateAll();
				step = 0;
				lastRegenSample = sampleCounter;
			}
			
			// Regen CV input (trigger regenerates without resetting step)
			if (canRegen && regenTrigger.process(inputs[REGEN_INPUT].getVoltage(), 0.1f, 2.f)) {
				if (!lockSeed) {
					reseed();
				}
				regenerateAll();
				lastRegenSample = sampleCounter;
			}
		}
		
		// Clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
			// Density affects pattern regeneration decisions; evaluating at step edges
			// preserves musical intent while reducing per-sample control overhead.
			densityEncoder = (int)params[DENSITY_PARAM].getValue();
			int densityCV = 0;
			if (inputs[DENSITY_CV_INPUT].isConnected()) {
				float cv = inputs[DENSITY_CV_INPUT].getVoltage();
				densityCV = (int)(cv / 10.f * 15.f);
			}
			currentDensity = clamp(densityEncoder + densityCV, 0, 14);

			// Read transpose once per step; this preserves tight clock-edge behavior
			// and removes redundant per-sample input work.
			if (inputs[TRANSPOSE_INPUT].isConnected()) {
				transposeCV = inputs[TRANSPOSE_INPUT].getVoltage();
			}

			// Update effective ROOT at the same cadence as pitch selection.
			// If patched from PITCH_OUTPUT, this makes ROOT follow SlideWyrm pitch (pitch-class).
			if (inputs[ROOT_CV_INPUT].isConnected()) {
				root = pitchClassFromPitchVoltage(inputs[ROOT_CV_INPUT].getVoltage());
			} else {
				root = clamp((int)params[ROOT_PARAM].getValue(), 0, 11);
			}

			cycleTimeSamples = sampleCounter - lastClockSample;
			lastClockSample = sampleCounter;
			
			// Read slide amount param + CV (0-3 snapped: none/slight/medium/high)
			{
				float slideParam = params[SLIDE_AMT_PARAM].getValue();
				if (inputs[SLIDE_CV_INPUT].isConnected()) {
					// 0-10V maps to 0-3
					slideParam += inputs[SLIDE_CV_INPUT].getVoltage() / 10.f * 3.f;
				}
				slideAmount = clamp(slideParam, 0.f, 3.f);
				// Precompute slide coefficient (replaces per-sample exp())
				static const float slideTimes[] = {0.f, 0.015f, 0.040f, 0.080f};
				int slideIdx = clamp((int)(slideAmount + 0.5f), 0, 3);
				float slideTime = slideTimes[slideIdx];
				if (slideTime > 0.001f) {
					cachedSlideCoeff = 1.f - fastExpApprox(-1.f / (slideTime * args.sampleRate));
				} else {
					cachedSlideCoeff = 1.0f; // instant
				}
			}
			
			// Read gate length param + CV (0-1 maps to 6.3ms-100ms to prevent voice crashes)
			{
				float gateParam = params[GATE_LEN_PARAM].getValue();
				if (inputs[GATE_LEN_CV_INPUT].isConnected()) {
					// 0-10V maps to 0-1
					gateParam += inputs[GATE_LEN_CV_INPUT].getVoltage() / 10.f;
				}
				gateParam = clamp(gateParam, 0.f, 1.f);
				gateLengthSec = 0.0063f + gateParam * 0.0937f; // 6.3ms to 100ms
			}
			
			// Check if density or scale changed
			if (currentDensity != currentPatternDensity || 
			    quantizer.getScaleSize() != currentPatternScaleSize) {
				if (regeneratePhase == 0) {
					regeneratePhase = 1;
				}
			}
			
			int stepPv = step;
			step = getNextStep(step);
			bool prevStepSlid = stepIsSlid(stepPv) && slideAmount > 0.f;
			currStepIsSlide = stepIsSlid(step);
			currStepIsAccent = stepIsAccent(step);
			currStepIsOctUp = stepIsOctUp(step);
			currStepIsOctDown = stepIsOctDown(step);
			
			// Handle slide — respect slideAmount
			if (prevStepSlid) {
				slideStartCV = getPitchForStep(stepPv, transposeCV);
				currPitchCV = slideStartCV;
				slideEndCV = getPitchForStep(step, transposeCV);
			} else {
				currPitchCV = getPitchForStep(step, transposeCV);
				slideStartCV = currPitchCV;
				slideEndCV = currPitchCV;
			}
			
			// Open gate — use gateLengthSec
			if (stepIsGated(step) || prevStepSlid) {
				int gateMode = (int)(params[GATE_MODE_PARAM].getValue());
				if (gateMode == 0) {
					currGateCV = currStepIsAccent ? 5.f : 3.f;
					int64_t gateTime = (int64_t)(gateLengthSec * args.sampleRate);
					gateOffSample = sampleCounter + gateTime;
				} else {
					currGateCV = currStepIsAccent ? 5.f : 3.f;
					int64_t gateTime = (int64_t)(gateLengthSec * args.sampleRate);
					static const double kGateMultiplier = 1.5;
					bool extended = false;
					if (gateMode == 1) {
						// CYCLE: emphasize every 4th step (1,5,9,13...) in 16-step.
						if (numSteps >= 4 && (step % 4) == 0) {
							gateTime = (int64_t)((double)gateTime * kGateMultiplier);
							extended = true;
						}
					} else if (gateMode == 2) {
						// SLIDE: extend the step we are sliding INTO (arrival-based).
						if (prevStepSlid) {
							gateTime = (int64_t)((double)gateTime * kGateMultiplier);
							extended = true;
						}
					}

					// Clamp only when extending, so non-extended steps match OFF behavior.
					// Also enforce a minimum gate fraction so the mode is audible even when
					// the Gate Length knob is set short.
					// Clamp to cycleTimeSamples (not -1) so an extended gate can reach the next
					// clock edge without forcing a gap.
					if (extended && cycleTimeSamples > 0) {
						double minFrac = 0.75;
						if (gateMode == 2) {
							// Slide feels better nearer to legato.
							minFrac = 0.90;
						}
						int64_t minGate = (int64_t)((double)cycleTimeSamples * minFrac);
						if (minGate > 0 && gateTime < minGate) {
							gateTime = minGate;
						}
						int64_t maxGate = cycleTimeSamples;
						if (maxGate > 0 && gateTime > maxGate) {
							gateTime = maxGate;
						}
					}
					gateOffSample = sampleCounter + gateTime;
				}
			}
			
			// Accent trigger based on mode
			{
				bool triggerAccent = false;
				switch (accentMode) {
					case 0: triggerAccent = (step % 4 == 0); break;
					case 1: triggerAccent = (step % 8 == 0); break;
					case 2: triggerAccent = currStepIsAccent; break;
				}
				if (triggerAccent) {
					accentActive = true;
					accentInDecay = false;
					accentEnvLevel = 0.f;
					accentTriggerSample = sampleCounter;
					// Precompute envelope params based on shape
					switch (accentShape) {
						case 0: { // Snap: 1ms atk, 20ms decay
							accentAtkSamples = 0.001f * args.sampleRate;
							float dcyTime = 0.020f;
							accentDecayCoeff = fastExpApprox(-1.f / (dcyTime * 0.3f * args.sampleRate));
							accentTotalSamples = accentAtkSamples + dcyTime * 3.f * args.sampleRate;
							break;
						}
						case 1: { // Reverse: 40ms rise, 5ms drop
							accentAtkSamples = 0.040f * args.sampleRate;
							accentDecayCoeff = 0.f; // Not used (linear drop)
							accentTotalSamples = accentAtkSamples + 0.005f * args.sampleRate;
							break;
						}
						case 2: { // Medium: 3ms atk, 60ms decay
							accentAtkSamples = 0.003f * args.sampleRate;
							float dcyTime = 0.060f;
							accentDecayCoeff = fastExpApprox(-1.f / (dcyTime * 0.3f * args.sampleRate));
							accentTotalSamples = accentAtkSamples + dcyTime * 3.f * args.sampleRate;
							break;
						}
					}
				}
			}
			
			currStepSemitone = getSemitoneForStep(step);
		}
		
		// Gate off timing
		if (currGateCV > 0.f && gateOffSample > 0 && sampleCounter >= gateOffSample) {
			gateOffSample = 0;
			if (!(currStepIsSlide && slideAmount > 0.f)) {
				currGateCV = 0.f;
			}
		}
		
		// Update slide — uses precomputed coefficient (no exp per sample)
		if (currPitchCV != slideEndCV) {
			if (slideAmount <= 0.f) {
				currPitchCV = slideEndCV;
			} else {
				currPitchCV += (slideEndCV - currPitchCV) * cachedSlideCoeff;
				if (std::abs(slideEndCV - currPitchCV) < 0.0001f) {
					currPitchCV = slideEndCV;
				}
			}
		}
		
		// Outputs
		outputs[PITCH_OUTPUT].setVoltage(currPitchCV);
		outputs[GATE_OUTPUT].setVoltage(currGateCV);
		
		// --- Accent envelope (recursive, no per-sample exp) ---
		if (accentActive) {
			float elapsed = (float)(sampleCounter - accentTriggerSample);
			
			if (elapsed >= accentTotalSamples) {
				accentActive = false;
				accentEnvLevel = 0.f;
			} else if (accentShape == 1) {
				// Reverse: linear ramp up, linear drop
				if (elapsed < accentAtkSamples) {
					accentEnvLevel = elapsed / accentAtkSamples;
				} else {
					float dropElapsed = elapsed - accentAtkSamples;
					float dropSamples = accentTotalSamples - accentAtkSamples;
					accentEnvLevel = 1.f - (dropSamples > 0.f ? clamp(dropElapsed / dropSamples, 0.f, 1.f) : 1.f);
				}
			} else {
				// Snap / Medium: linear attack, then recursive exponential decay
				if (elapsed < accentAtkSamples) {
					accentEnvLevel = elapsed / accentAtkSamples;
					accentInDecay = false;
				} else {
					if (!accentInDecay) {
						accentInDecay = true;
						accentEnvLevel = 1.f; // Start decay from peak
					}
					accentEnvLevel *= accentDecayCoeff;
				}
			}
			accentEnvLevel = clamp(accentEnvLevel, 0.f, 1.f);
		} else {
			accentEnvLevel = 0.f;
		}
		outputs[ACCENT_OUTPUT].setVoltage(accentEnvLevel * 8.f);
		
		// Lights (control-rate to save CPU on MetaModule)
		if (++lightRateDivider >= LIGHT_RATE_SAMPLES) {
			lightRateDivider = 0;
			lights[SLIDE_LIGHT].setBrightness(currStepIsSlide ? 1.f : 0.f);
			lights[ACCENT_LIGHT].setBrightness(currStepIsAccent ? 1.f : 0.f);
			lights[OCTAVE_UP_LIGHT].setBrightness(currStepIsOctUp ? 1.f : 0.f);
			lights[OCTAVE_DOWN_LIGHT].setBrightness(currStepIsOctDown ? 1.f : 0.f);
		}
		
		// Update regeneration (amortized)
		updateRegeneration();
	}
	
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "seed", json_integer(seed));
		json_object_set_new(rootJ, "lockSeed", json_boolean(lockSeed));
		json_object_set_new(rootJ, "step", json_integer(step));

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
		json_t* seedJ = json_object_get(rootJ, "seed");
		if (seedJ) seed = json_integer_value(seedJ);
		
		json_t* lockSeedJ = json_object_get(rootJ, "lockSeed");
		if (lockSeedJ) lockSeed = json_boolean_value(lockSeedJ);
		
		json_t* stepJ = json_object_get(rootJ, "step");
		if (stepJ) step = json_integer_value(stepJ);
		
		regenerateAll();
	}
};

// ============================================================================
// NanoVG Label Widget (same style as Trigonomicon)
// ============================================================================
#ifndef METAMODULE
struct SWPanelLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;
	bool isTitle;

	SWPanelLabel(Vec pos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
		box.pos = pos;
		box.size = Vec(80, fontSize + 4);
		this->text = text;
		this->fontSize = fontSize;
		this->color = color;
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
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
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

static SWPanelLabel* swCreateLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color, bool isTitle = false) {
	Vec pxPos = mm2px(mmPos);
	return new SWPanelLabel(pxPos, text, fontSize, color, isTitle);
}

// ============================================================================
// Display Widget - Shows Scale/Root/Octave
// ============================================================================
struct SlideWyrmDisplay : TransparentWidget {
	SlideWyrm* module;

	void draw(const DrawArgs& args) override {
		// Text-only overlay. The screen art is baked into the faceplate PNG.
		std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) return;

		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 13.f);
		nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		char displayBuf[64];
		if (module) {
			int scaleIdx = (int)module->params[SlideWyrm::SCALE_PARAM].getValue();
			int rootIdx = 0;
			if (module->inputs[SlideWyrm::ROOT_CV_INPUT].isConnected()) {
				rootIdx = SlideWyrm::pitchClassFromPitchVoltage(module->inputs[SlideWyrm::ROOT_CV_INPUT].getVoltage());
			} else {
				rootIdx = (int)module->params[SlideWyrm::ROOT_PARAM].getValue();
			}
			int octave = (int)module->params[SlideWyrm::OCTAVE_PARAM].getValue();
			
			const char* scaleName = scale_names[clamp(scaleIdx, 0, NUM_SCALES - 1)];
			const char* rootName = note_names[clamp(rootIdx, 0, 11)];
			
			snprintf(displayBuf, sizeof(displayBuf), "%s %s Oct:%+d", scaleName, rootName, octave);
		} else {
			snprintf(displayBuf, sizeof(displayBuf), "GUNA C Oct:0");
		}

		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, displayBuf, NULL);
	}
};
#endif

struct SlideWyrmWidget : ModuleWidget {
	SlideWyrmWidget(SlideWyrm* module) {
		setModule(module);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SlideWyrm.png")));
	#else
		// VCV Rack: no SVG; hardcode 12HP and use PNG directly.
		box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);
		{
			auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/SlideWyrm.png"));
			panelBg->box.pos = Vec(0, 0);
			panelBg->box.size = box.size;
			addChild(panelBg);
		}
	#endif

#ifndef METAMODULE
		struct SlideWyrmPort : MVXPort {
			SlideWyrmPort() {
				imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_dark.png");
				imageHandle = -1;
			}
		};

		struct SlideWyrmMainKnob : MVXKnob {
			SlideWyrmMainKnob() {
				box.size = box.size.mult(0.8f);
			}
		};
		using SlideWyrmMainControl = SlideWyrmMainKnob;
#else
		using SlideWyrmPort = MVXPort;
		using SlideWyrmMainControl = Trimpot;
#endif

#ifndef METAMODULE
		NVGcolor neonGreen = nvgRGB(0x00, 0x00, 0x00);
		NVGcolor dimGreen  = nvgRGB(0x00, 0x00, 0x00);
#endif

		// 12HP: center = 30.48mm
		float centerX = 30.48f;
		float leftX   = 13.0f;
		float rightX  = 48.0f;
		const float pxPerMmY = mm2px(Vec(0.f, 1.f)).y;
		const float row3bShiftMmY = (pxPerMmY > 0.f) ? (9.f / pxPerMmY) : 0.f;

#ifndef METAMODULE
		const float topLabelShiftMmY = (pxPerMmY > 0.f) ? (-2.f / pxPerMmY) : 0.f;
#endif

		// === ROW 1: Density + Scale ===
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(leftX, 17.5f + topLabelShiftMmY), "DENSITY", 8.5f, neonGreen));
#endif
		addParam(createParamCentered<SlideWyrmMainControl>(
			mm2px(Vec(leftX, 23.5f)), module, SlideWyrm::DENSITY_PARAM));

#ifndef METAMODULE
		addChild(swCreateLabel(Vec(rightX, 17.5f + topLabelShiftMmY), "SCALE", 8.5f, neonGreen));
#endif
		addParam(createParamCentered<SlideWyrmMainControl>(
			mm2px(Vec(rightX, 23.5f)), module, SlideWyrm::SCALE_PARAM));

		// === ROW 2: Root + Octave ===
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(leftX, 28.5f + topLabelShiftMmY), "ROOT", 8.5f, neonGreen));
#endif
		addParam(createParamCentered<SlideWyrmMainControl>(
			mm2px(Vec(leftX, 34.5f)), module, SlideWyrm::ROOT_PARAM));

#ifndef METAMODULE
		addChild(swCreateLabel(Vec(rightX, 28.5f + topLabelShiftMmY), "OCTAVE", 8.5f, neonGreen));
#endif
		addParam(createParamCentered<SlideWyrmMainControl>(
			mm2px(Vec(rightX, 34.5f)), module, SlideWyrm::OCTAVE_PARAM));

		// === ROW 3: Seed + Regen ===
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(leftX, 40.5f + topLabelShiftMmY), "SEED", 8.5f, neonGreen));
		addChild(swCreateLabel(Vec(rightX, 40.5f), "REGEN", 8.5f, neonGreen));
#endif
		addParam(createParamCentered<SlideWyrmMainControl>(
			mm2px(Vec(leftX, 46.5f)), module, SlideWyrm::SEED_PARAM));
		addParam(createParamCentered<TL1105>(
			mm2px(Vec(rightX, 46.5f)), module, SlideWyrm::MANUAL_REGEN_PARAM));

		// === ROW 3b: Slide Amount (trimpot+CV) + Gate Length (trimpot+CV) ===
		float row3bY = 64.f;
		// Center these two sub-sections around the module center, preserving their spacing.
		const float row3bCenter = (8.f + 44.f) * 0.5f;
		const float row3bShiftMmX = centerX - row3bCenter;
		const float row3bSlideKnobX = 17.f + row3bShiftMmX;
		const float row3bSlidePortX = 8.f + row3bShiftMmX;
		const float row3bGateKnobX = 35.f + row3bShiftMmX;
		const float row3bGatePortX = 44.f + row3bShiftMmX;

#ifndef METAMODULE
		addChild(swCreateLabel(Vec(12.f + row3bShiftMmX, 56.f + row3bShiftMmY), "SLIDE AMOUNT", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(38.f + row3bShiftMmX, 56.f + row3bShiftMmY), "GATE LENGTH", 7.5f, neonGreen));
#endif
		addParam(createParamCentered<Trimpot>(
			mm2px(Vec(row3bSlideKnobX, row3bY + row3bShiftMmY)), module, SlideWyrm::SLIDE_AMT_PARAM));
		addInput(createInputCentered<SlideWyrmPort>(
			mm2px(Vec(row3bSlidePortX, row3bY + row3bShiftMmY)), module, SlideWyrm::SLIDE_CV_INPUT));
		addParam(createParamCentered<Trimpot>(
			mm2px(Vec(row3bGateKnobX, row3bY + row3bShiftMmY)), module, SlideWyrm::GATE_LEN_PARAM));
		addInput(createInputCentered<SlideWyrmPort>(
			mm2px(Vec(row3bGatePortX, row3bY + row3bShiftMmY)), module, SlideWyrm::GATE_LEN_CV_INPUT));

		// === ROW 4: Switches row — LOCK, STEPS, GATE, ACC, Acc. Shape ===
		float switchY = 80.f;
		float sw1X = 9.f, sw2X = 22.f, swGateX = 30.5f, sw3X = 39.f, sw4X = 52.f;
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(sw1X, 73.5f), "LOCK", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(sw2X, 73.5f), "STEPS", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(swGateX, 73.5f), "GATE", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(sw3X, 73.5f), "ACC", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(sw4X, 73.5f), "ACC.STEP", 7.5f, neonGreen));
#endif
		addParam(createParamCentered<CKSS>(
			mm2px(Vec(sw1X, switchY)), module, SlideWyrm::LOCK_SEED_PARAM));
		addParam(createParamCentered<CKSSThree>(
			mm2px(Vec(sw2X, 81.f)), module, SlideWyrm::STEPS_PARAM));
		addParam(createParamCentered<CKSSThree>(
			mm2px(Vec(swGateX, 81.f)), module, SlideWyrm::GATE_MODE_PARAM));
		addParam(createParamCentered<CKSSThree>(
			mm2px(Vec(sw3X, 81.f)), module, SlideWyrm::ACCENT_SHAPE_PARAM));
		addParam(createParamCentered<CKSSThree>(
			mm2px(Vec(sw4X, 81.f)), module, SlideWyrm::ACCENT_MODE_PARAM));

		// === Indicator lights ===
		float lightY = 92.f;
		float l1 = 10.f, l2 = 21.f, l3 = 40.f, l4 = 51.f;
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(l1, lightY - 3.5f), "SLD", 7.5f, dimGreen));
		addChild(swCreateLabel(Vec(l2, lightY - 3.5f), "ACC", 7.5f, dimGreen));
		addChild(swCreateLabel(Vec(l3, lightY - 3.5f), "UP", 7.5f, dimGreen));
		addChild(swCreateLabel(Vec(l4, lightY - 3.5f), "DN", 7.5f, dimGreen));
#endif
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(l1, lightY)), module, SlideWyrm::SLIDE_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(l2, lightY)), module, SlideWyrm::ACCENT_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(l3, lightY)), module, SlideWyrm::OCTAVE_UP_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(l4, lightY)), module, SlideWyrm::OCTAVE_DOWN_LIGHT));

		// === Inputs row ===
		float inY = 105.f;
#ifndef METAMODULE
		float inLabelY = 98.f;
		float in1 = 7.5f, in2 = 16.5f, in3 = 25.5f, in4 = 34.5f, in5 = 43.5f, in6 = 52.5f;
		addChild(swCreateLabel(Vec(in1, inLabelY), "CLK", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(in2, inLabelY), "RST", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(in3, inLabelY), "TRN", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(in4, inLabelY), "ROT", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(in5, inLabelY), "DNS", 7.5f, neonGreen));
		addChild(swCreateLabel(Vec(in6, inLabelY), "REG", 7.5f, neonGreen));
#endif
		float inSpacing = 9.f;
		float inStartX = 7.5f;
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX, inY)), module, SlideWyrm::CLOCK_INPUT));
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX + inSpacing, inY)), module, SlideWyrm::RESET_INPUT));
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX + inSpacing*2, inY)), module, SlideWyrm::TRANSPOSE_INPUT));
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX + inSpacing*3, inY)), module, SlideWyrm::ROOT_CV_INPUT));
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX + inSpacing*4, inY)), module, SlideWyrm::DENSITY_CV_INPUT));
		addInput(createInputCentered<SlideWyrmPort>(mm2px(Vec(inStartX + inSpacing*5, inY)), module, SlideWyrm::REGEN_INPUT));

		// === Outputs row: PITCH + GATE + ACCENT ===
		float outY = 115.f;
		float outPitchX = 11.f;
		float outGateX  = 30.48f;
		float outAccX   = 50.f;
		addOutput(createOutputCentered<SlideWyrmPort>(mm2px(Vec(outPitchX, outY)), module, SlideWyrm::PITCH_OUTPUT));
		addOutput(createOutputCentered<SlideWyrmPort>(mm2px(Vec(outGateX, outY)), module, SlideWyrm::GATE_OUTPUT));
		addOutput(createOutputCentered<SlideWyrmPort>(mm2px(Vec(outAccX, outY)), module, SlideWyrm::ACCENT_OUTPUT));
#ifndef METAMODULE
		addChild(swCreateLabel(Vec(outPitchX, outY + 5.f), "PITCH", 9.5f, neonGreen));
		addChild(swCreateLabel(Vec(outGateX, outY + 5.f), "GATE", 9.5f, neonGreen));
		addChild(swCreateLabel(Vec(outAccX, outY + 5.f), "ACC", 9.5f, neonGreen));
#endif
	}
};

Model* modelSlideWyrm = createModel<SlideWyrm, SlideWyrmWidget>("SlideWyrm");
