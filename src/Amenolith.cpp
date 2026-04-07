#include "plugin.hpp"
#include "sampler/DrumKits.h"

#include <cmath>

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bdkit;

namespace {

struct KitParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int k = (int)std::lround(getValue());
		if (k < 0) k = 0;
		if (k >= NUM_KITS) k = NUM_KITS - 1;
		return std::to_string(k + 1);
	}
};

struct SemitoneParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int s = (int)std::lround(getValue());
		return std::to_string(s);
	}
};

#ifndef METAMODULE

struct BCPanelLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;
	int align;
	bool isTitle;

	BCPanelLabel(Vec pxPos, const char* t, float fs, NVGcolor c, int a, bool title) {
		box.pos = pxPos;
		box.size = Vec(120, fs + 4);
		text = t;
		fontSize = fs;
		color = c;
		align = a;
		isTitle = title;
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
		nvgTextAlign(args.vg, align);
		nvgText(args.vg, 0.f, 0.f, text.c_str(), NULL);
		if (isTitle) {
			// Subtle outline/emboss like other module titles
			nvgText(args.vg, 0.5f, 0.f, text.c_str(), NULL);
			nvgText(args.vg, -0.5f, 0.f, text.c_str(), NULL);
			nvgText(args.vg, 0.f, 0.4f, text.c_str(), NULL);
			nvgText(args.vg, 0.f, -0.4f, text.c_str(), NULL);
		}
	}
};

static inline BCPanelLabel* bcCreateLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color,
	int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, bool isTitle = false) {
	return new BCPanelLabel(mm2px(mmPos), text, fontSize, color, align, isTitle);
}

#endif // !METAMODULE

static inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

static inline float softClip(float x, float drive01) {
	drive01 = clampf(drive01, 0.f, 1.f);

	// Base drive: gentle up to ~90%
	float k = 1.f + drive01 * 6.f;
	float k2 = 1.f;

	// Top-end ramp (90–100%): get aggressively distorted.
	if (drive01 > 0.90f) {
		float t = (drive01 - 0.90f) / 0.10f; // 0..1
		// Very steep curve near the top.
		float t2 = t * t;
		float t4 = t2 * t2;
		k += 50.f * t4;
		k2 = 1.f + 10.f * t;
	}

	auto shape = [&](float in) {
		float y = in * k;
		float ay = std::fabs(y);
		float out = y / (1.f + ay);
		// Extra stage only at the very top for harsher grit.
		if (drive01 > 0.90f) {
			float z = out * k2;
			float az = std::fabs(z);
			out = z / (1.f + az);
		}
		return out;
	};

	float out = shape(x);

	// Loudness management: keep the perceived level from rising too much with Drive.
	// Normalize against a mid-level reference amplitude and blend by drive amount.
	{
		constexpr float ref = 0.35f;
		float refOut = std::fabs(shape(ref));
		float targetRef = ref * (1.f + 0.15f * drive01);
		float rawScale = (refOut > 1e-6f) ? (targetRef / refOut) : 1.f;
		float scale = 1.f + drive01 * (rawScale - 1.f);
		out *= scale;
	}

	return out;
}

struct Voice {
	bool active = false;

	uint8_t currentKit = 0;
	uint8_t pendingKit = 0;

	const int16_t* data = nullptr;
	uint32_t frames = 0;
	uint32_t endFrame = 0;
	uint32_t fadeStartFrame = 0;
	uint32_t sampleRate = 48000;

	// Q16.16 phase: frame index in high 16 bits
	uint32_t phase = 0;
	uint32_t phaseInc = (1u << 16);

	float gain = 0.f;
	float lengthFadeInv = 0.f;
	float panL = 0.7071f;
	float panR = 0.7071f;

	uint32_t fadeIn = 0;
	uint32_t fadeOut = 0;
	bool choking = false;
};

static inline bool isHatInst(int inst) {
	return inst == CH || inst == OH;
}

} // namespace

struct Amenolith : Module {
	enum ParamIds {
		KIT_PARAM,
			SCRAMBLE_PARAM,
		ACCENT_PARAM,
			DRIVE_PARAM,
			HUMANIZE_PARAM,
			LAYER_SELECT_PARAM,
			GEN_PITCH_PARAM,

			// Per-voice MM controls in column->row order: RR, PAN, LVL, SEMI, LENGTH.
			RR_MODE_PARAM_BASE,
			PAN_PARAM_BASE = RR_MODE_PARAM_BASE + NUM_INST,
			LEVEL_PARAM_BASE = PAN_PARAM_BASE + NUM_INST,
			TUNE_PARAM_BASE = LEVEL_PARAM_BASE + NUM_INST,
			LENGTH_PARAM_BASE = TUNE_PARAM_BASE + NUM_INST,

			// Legacy global RR (kept for backward compatibility, no longer used in UI)
			RR_MODE_PARAM = LENGTH_PARAM_BASE + NUM_INST,
			PARAMS_LEN
	};
	enum InputIds {
		KIT_CV_INPUT,
		ACCENT_CV_INPUT,

		TRIG_INPUT_BASE,
		VEL_INPUT_BASE = TRIG_INPUT_BASE + NUM_INST,
		TUNE_CV_INPUT_BASE = VEL_INPUT_BASE + NUM_INST,

		// Global drive CV input (appended to preserve existing input indices)
		DRIVE_CV_INPUT = TUNE_CV_INPUT_BASE + NUM_INST,

		INPUTS_LEN = DRIVE_CV_INPUT + 1
	};
	enum OutputIds {
		MIX_L_OUTPUT,
		MIX_R_OUTPUT,
		INST_OUT_BASE,
		ENV_OUTPUT = INST_OUT_BASE + NUM_INST,
		OUTPUTS_LEN = ENV_OUTPUT + 1
	};
	enum LightIds {
		LIGHTS_LEN
	};

	dsp::SchmittTrigger trig[NUM_INST];
	dsp::SchmittTrigger scrambleTrig;
	Voice v[NUM_INST];

	// Per-instrument kit map for scramble. -1 = use main kit knob.
	int scrambleKitMap_[NUM_INST] = {-1, -1, -1, -1, -1, -1};
	bool scrambleActive_ = false;

	int ctrlDiv = 0;
	uint32_t sampleCounter = 0;

	// Humanize scheduling (per-voice) to preserve transients.
	bool pendingTrig[NUM_INST] = {};
	int pendingDelay[NUM_INST] = {};
	float pendingGainMult[NUM_INST] = {};
	float nextGainMult[NUM_INST] = {};
	float humanizeDrift[NUM_INST] = {};
	float hatToneState_[NUM_INST] = {};
	float hatAirColor_[NUM_INST] = {};
	float hatAirEnv_[NUM_INST] = {};
	uint32_t accentRoleLastTriggerSample_[NUM_INST] = {};
	bool accentRoleInitialized_[NUM_INST] = {};

	uint32_t rng = 0x12345678u;

	// ---------------------------------------------------------------------
	// Generative pitch modulation (deterministic, trigger-driven)
	// ---------------------------------------------------------------------
	static constexpr int PITCH_TABLE_SIZE = 7;
	static constexpr int8_t PITCH_TABLE[PITCH_TABLE_SIZE] = {-5, -3, -2, 0, 2, 3, 5};
	static constexpr int NEUTRAL_PITCH_INDEX = 3; // 0 semitones
	static constexpr int UP2_PITCH_INDEX = 4;     // +2 semitones

	struct GenPitchState {
		uint32_t lastTrigSample = 0;
		uint32_t avgIoiSamples = 0;
		uint32_t phrase = 1;
		uint32_t rng = 0x9E3779B9u;
		int8_t tableIndex = NEUTRAL_PITCH_INDEX;
		uint8_t repeatCount = 0;
	};

	GenPitchState genPitch_[NUM_INST] = {};
	float pendingPitchSemis_[NUM_INST] = {};
	float nextPitchSemis_[NUM_INST] = {};
	int lastQuantizedKit_ = -1;

	static inline uint32_t hash32(uint32_t x) {
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	static inline float genRand01(uint32_t& s) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (s & 0xFFFFFFu) / (float)0x1000000u;
	}

	inline void resetGenPitchPhrase(int inst) {
		GenPitchState& st = genPitch_[inst];
		st.phrase++;
		st.repeatCount = 0;
		st.tableIndex = NEUTRAL_PITCH_INDEX;
		uint32_t seed = 0xA511E9B3u ^ (uint32_t)inst * 0x85EBCA6Bu ^ st.phrase * 0xC2B2AE35u;
		st.rng = hash32(seed);
	}

	static inline bool genPitchAppliesToInst(int inst) {
		return inst != BD && inst != OH && inst != RC;
	}

	inline float computeGenPitchSemis(int inst, float engineSampleRate, float velNorm, bool accent) {
		GenPitchState& st = genPitch_[inst];
		const uint32_t now = sampleCounter;
		const uint32_t prev = st.lastTrigSample;
		st.lastTrigSample = now;

		// First hit: neutral
		if (prev == 0u) {
			resetGenPitchPhrase(inst);
			return (float)PITCH_TABLE[st.tableIndex];
		}

		uint32_t dt = now - prev;
		// Update running IOI estimate (ignore pathological extremes)
		const uint32_t dtMin = (uint32_t)std::lround(engineSampleRate * 0.002f);
		const uint32_t dtMax = (uint32_t)std::lround(engineSampleRate * 4.0f);
		if (dt >= dtMin && dt <= dtMax) {
			if (st.avgIoiSamples == 0u) {
				st.avgIoiSamples = dt;
			}
			else {
				// Exponential smoothing, alpha=0.12
				st.avgIoiSamples = (uint32_t)std::lround((float)st.avgIoiSamples * 0.88f + (float)dt * 0.12f);
			}
		}

		uint32_t baseIoi = (st.avgIoiSamples > 0u) ? st.avgIoiSamples : (uint32_t)std::lround(engineSampleRate * 0.18f);
		if (baseIoi < 32u) baseIoi = 32u;

		// Thresholds scaled by trigger history (tempo-adaptive)
		uint32_t repeatWindow = (uint32_t)std::lround((float)baseIoi * 0.85f);
		uint32_t resetThresh = (uint32_t)std::lround((float)baseIoi * 3.25f);
		if (repeatWindow < 32u) repeatWindow = 32u;
		uint32_t resetFloor = (uint32_t)std::lround(engineSampleRate * 0.45f);
		if (resetThresh < resetFloor) resetThresh = resetFloor;

		// Phrase reset on long gaps
		if (dt > resetThresh) {
			resetGenPitchPhrase(inst);
			return (float)PITCH_TABLE[st.tableIndex];
		}

		// Not repeating: snap back to neutral
		if (dt > repeatWindow) {
			st.repeatCount = 0;
			st.tableIndex = NEUTRAL_PITCH_INDEX;
			return (float)PITCH_TABLE[st.tableIndex];
		}

		// Repeating hit: descent with controlled variation
		st.repeatCount++;
		const float continueDescendProb = 0.82f; // 70–90% target
		const float u = genRand01(st.rng);
		int nextIdx = st.tableIndex;
		if (u < continueDescendProb) {
			nextIdx = std::max(0, (int)st.tableIndex - 1);
		}
		else {
			float v = genRand01(st.rng);
			int dir = (v < 0.5f) ? 1 : -1;
			nextIdx = std::min(PITCH_TABLE_SIZE - 1, std::max(0, (int)st.tableIndex + dir));
		}

		// Accent interaction: bias toward 0 or +2 semitones
		if (accent) {
			float w = genRand01(st.rng);
			nextIdx = (w < 0.60f) ? NEUTRAL_PITCH_INDEX : UP2_PITCH_INDEX;
		}

		st.tableIndex = (int8_t)nextIdx;
		(void)velNorm; // reserved for future velocity-dependent shaping
		return (float)PITCH_TABLE[st.tableIndex];
	}

	struct RRState {
		uint32_t lastFrame = 0;
		uint32_t hit = 0;
	};
	RRState rr[NUM_INST] = {};

	static constexpr int TUNE_RANGE_SEMIS = 3;
	static constexpr int LUT_SIZE = TUNE_RANGE_SEMIS * 2 + 1;
	float semitoneRatio[LUT_SIZE] = {};
	float mixEnv_ = 0.f;

	// Cached envelope follower coefficients (recomputed on sample rate change, not per sample).
	float envAttackCoeff_  = 0.f;
	float envReleaseCoeff_ = 0.f;

	void updateEnvCoeffs(float sr) {
		envAttackCoeff_  = std::exp(-1.f / (0.002f * sr));
		envReleaseCoeff_ = std::exp(-1.f / (0.08f  * sr));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		updateEnvCoeffs(e.sampleRate);
	}

	Amenolith() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam<KitParamQuantity>(KIT_PARAM, 0.f, (float)(NUM_KITS - 1), 0.f, "Kit");
		getParamQuantity(KIT_PARAM)->snapEnabled = true;
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive");
		configParam(HUMANIZE_PARAM, 0.f, 1.f, 0.f, "Humanize");
		configSwitch(LAYER_SELECT_PARAM, 0.f, 2.f, 0.f, "Layer", {"1", "2", "3"});
		// Legacy global RR (no longer used)
		configSwitch(RR_MODE_PARAM, 0.f, 2.f, 0.f, "Round robin (legacy)", {"OFF", "EUC", "DENS"});
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.7f, "Accent amount / poly sensitivity");
		configSwitch(GEN_PITCH_PARAM, 0.f, 1.f, 0.f, "Generative pitch", {"Off", "On"});
		configParam(SCRAMBLE_PARAM, 0.f, 1.f, 0.f, "Scramble kits");

		for (int i = 0; i < NUM_INST; ++i) {
			configSwitch(RR_MODE_PARAM_BASE + i, 0.f, 2.f, 0.f,
				std::string(instrumentName(i)) + " RR mode", {"OFF", "EUC", "DENS"});
		}

		for (int i = 0; i < NUM_INST; ++i) {
			float defaultLevel = 0.8f;
			switch (i) {
				case BD:  defaultLevel = 1.0f; break;
				case SN:  defaultLevel = 1.0f; break;
				case GSN: defaultLevel = 0.5f; break;
				case CH:  defaultLevel = 0.5f; break;
				case OH:  defaultLevel = 0.5f; break;
				case RC:  defaultLevel = 0.35f; break;
				default: break;
			}
			configParam(LEVEL_PARAM_BASE + i, 0.f, 1.f, defaultLevel, std::string(instrumentName(i)) + " level");
			float defaultPan = 0.f;
			switch (i) {
				case BD: defaultPan = 0.0f; break;
				case SN: defaultPan = -0.2f; break;
				case GSN: defaultPan = 0.2f; break;
				case CH: defaultPan = 0.35f; break;
				case OH: defaultPan = -0.35f; break;
				case RC: defaultPan = 0.45f; break;
				default: defaultPan = 0.f; break;
			}
			configParam(PAN_PARAM_BASE + i, -1.f, 1.f, defaultPan, std::string(instrumentName(i)) + " pan");
			configParam<SemitoneParamQuantity>(TUNE_PARAM_BASE + i, -3.f, 3.f, 0.f, std::string(instrumentName(i)) + " tune (semitones)");
			getParamQuantity(TUNE_PARAM_BASE + i)->snapEnabled = true;
			float defaultLength = 1.f;
			switch (i) {
				case CH: defaultLength = 0.4f; break;
				case OH: defaultLength = 0.4f; break;
				case RC: defaultLength = 0.4f; break;
				default: break;
			}
			configParam(LENGTH_PARAM_BASE + i, 0.f, 1.f, defaultLength, std::string(instrumentName(i)) + " length");
		}

		configInput(KIT_CV_INPUT, "Kit CV");
		configInput(ACCENT_CV_INPUT, "Accent CV (mono global or poly per-voice dynamic)");
		configInput(DRIVE_CV_INPUT, "Drive CV");

		for (int i = 0; i < NUM_INST; ++i) {
			configInput(TRIG_INPUT_BASE + i, std::string(instrumentName(i)) + " trig");
			configInput(VEL_INPUT_BASE + i, std::string(instrumentName(i)) + " velocity");
			configInput(TUNE_CV_INPUT_BASE + i, std::string(instrumentName(i)) + " tune CV (1V/oct)");
			configOutput(INST_OUT_BASE + i, std::string(instrumentName(i)) + " out");
		}

		configOutput(MIX_L_OUTPUT, "Mix L");
		configOutput(MIX_R_OUTPUT, "Mix R");
		configOutput(ENV_OUTPUT, "ENV OUT");

		for (int s = -TUNE_RANGE_SEMIS; s <= TUNE_RANGE_SEMIS; ++s) {
			semitoneRatio[s + TUNE_RANGE_SEMIS] = std::exp2f((float)s / 12.f);
		}

		// Initial pan state from params (updated at control rate in process())
		for (int i = 0; i < NUM_INST; ++i) {
			setPan(i, params[PAN_PARAM_BASE + i].getValue());
		}

		// Cache envelope follower coefficients at default sample rate.
		updateEnvCoeffs(48000.f);

		initKits();
	}

	void onReset() override {
		mixEnv_ = 0.f;
		for (int i = 0; i < NUM_INST; ++i) {
			pendingTrig[i] = false;
			pendingDelay[i] = 0;
			pendingGainMult[i] = 1.f;
			nextGainMult[i] = 1.f;
			humanizeDrift[i] = 0.f;
			hatToneState_[i] = 0.f;
			hatAirColor_[i] = 0.f;
			hatAirEnv_[i] = 0.f;
			accentRoleLastTriggerSample_[i] = 0u;
			accentRoleInitialized_[i] = false;
			genPitch_[i] = GenPitchState{};
			pendingPitchSemis_[i] = 0.f;
			nextPitchSemis_[i] = 0.f;
			resetGenPitchPhrase(i);
		}
		lastQuantizedKit_ = -1;
		scrambleActive_ = false;
		for (int i = 0; i < NUM_INST; ++i) scrambleKitMap_[i] = -1;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "scrambleActive", json_boolean(scrambleActive_));
		json_t* arr = json_array();
		for (int i = 0; i < NUM_INST; ++i)
			json_array_append_new(arr, json_integer(scrambleKitMap_[i]));
		json_object_set_new(root, "scrambleKitMap", arr);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* sa = json_object_get(root, "scrambleActive");
		if (sa) scrambleActive_ = json_is_true(sa);
		json_t* arr = json_object_get(root, "scrambleKitMap");
		if (arr && json_is_array(arr)) {
			for (int i = 0; i < NUM_INST && i < (int)json_array_size(arr); ++i) {
				json_t* v = json_array_get(arr, i);
				if (v) scrambleKitMap_[i] = (int)json_integer_value(v);
			}
		}
	}

	void setPan(int inst, float pan) {
		// pan -1..+1, constant-power via sqrt — avoids cos/sin on ARM.
		// Identical law: panL = cos(angle), panR = sin(angle) with angle = t*pi/2
		// approximated as panL = sqrt(1-t), panR = sqrt(t), which is exact at
		// the endpoints and perceptually identical in the useful range.
		float t = pan * 0.5f + 0.5f;  // remap -1..+1 → 0..1
		v[inst].panL = std::sqrt(1.f - t);
		v[inst].panR = std::sqrt(t);
	}

	inline float rand01() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng & 0xFFFFFFu) / (float)0x1000000u;
	}

	inline float randGauss01() {
		// Box-Muller transform. Mean 0, stddev 1.
		float u1 = rand01();
		if (u1 < 1e-6f) u1 = 1e-6f;
		float u2 = rand01();
		return std::sqrt(-2.f * std::log(u1)) * std::cos(2.f * (float)M_PI * u2);
	}

	static inline float humanizeScaleForInst(int inst) {
		// Tighter kick, looser hats.
		switch (inst) {
			case BD:  return 0.50f;
			case SN:  return 0.70f;
			case GSN: return 0.80f;
			case CH:  return 1.20f;
			case OH:  return 1.38f;
			case RC:  return 1.10f;
			default:  return 1.00f;
		}
	}

	bool isPolyAccentModeActive() {
		return inputs[ACCENT_CV_INPUT].isConnected() && inputs[ACCENT_CV_INPUT].getChannels() > 1;
	}

	inline void scheduleHumanizedTrigger(int inst, float engineSampleRate) {
		float hum = isPolyAccentModeActive() ? 0.f : clampf(params[HUMANIZE_PARAM].getValue(), 0.f, 1.f);
		if (hum <= 0.f) {
			pendingTrig[inst] = true;
			pendingDelay[inst] = 0;
			pendingGainMult[inst] = 1.f;
			return;
		}

		auto smoothstep01 = [](float edge0, float edge1, float x) {
			float t = (x - edge0) / (edge1 - edge0);
			if (t < 0.f) t = 0.f;
			if (t > 1.f) t = 1.f;
			return t * t * (3.f - 2.f * t);
		};

		float scale = humanizeScaleForInst(inst);
		bool hatInst = isHatInst(inst);
		// Top-end gets progressively wilder (90–100%).
		float wild = smoothstep01(0.88f, 1.00f, hum);
		wild *= wild;
		// Max range: ~2ms normally, up to ~6ms near the top.
		float maxMs = 2e-3f + 4e-3f * wild;
		float maxFrames = maxMs * engineSampleRate;

		// Base delay provides lookahead so we can be "early" or "late" around it.
		float base = hum * scale * maxFrames;

		// Gaussian-ish jitter, clustered around 0.
		float jitterNorm = clampf(randGauss01() * 0.50f, -1.f, 1.f);

		// Near the top end, occasionally throw an outlier (heavy tail) for controlled chaos.
		// This stays musical because it is rare and still centered around the pocket.
		if (wild > 0.f) {
			float pOutlier = 0.18f * wild; // up to ~18% at 100%
			if (rand01() < pOutlier) {
				jitterNorm = clampf(randGauss01() * 1.05f, -1.35f, 1.35f);
			}
		}

		// Slow "pocket drift" at the very top: small correlated bias that changes gradually.
		// This avoids purely-white-noise jitter and sounds more like a performer.
		if (wild > 0.f) {
			humanizeDrift[inst] = humanizeDrift[inst] * 0.97f + randGauss01() * 0.03f;
			if (std::fabs(humanizeDrift[inst]) < 1e-30f) humanizeDrift[inst] = 0.f;
			float driftBias = clampf(humanizeDrift[inst] * 0.35f * wild, -0.50f, 0.50f);
			jitterNorm += driftBias;
			jitterNorm = clampf(jitterNorm, -1.50f, 1.50f);
		}

		if (hatInst) {
			// Hats sit better when their microtiming drags slightly behind the kick/snare pocket.
			float lateBias = 0.18f + 0.20f * hum + ((inst == OH) ? 0.08f : 0.f);
			jitterNorm = jitterNorm * 0.72f + lateBias;
			jitterNorm = clampf(jitterNorm, -0.30f, 1.65f);
		}

		float delayF = base + jitterNorm * base;
		if (delayF < 0.f) delayF = 0.f;
		if (delayF > 2.f * base) delayF = 2.f * base;

		pendingTrig[inst] = true;
		pendingDelay[inst] = (int)std::lround(delayF);

		// Correlated gain: early hits a touch louder, late hits a touch softer.
		// +/-0.5 dB at HUM=1, scaled by per-voice tightness.
		float jitterForGain = clampf(jitterNorm, -1.f, 1.f);
		float db = (-0.5f * jitterForGain) * hum * scale;
		if (hatInst) {
			float hatSwingDb = ((rand01() * 2.f) - 1.f) * (0.35f + 1.05f * hum);
			float lateSofteningDb = std::max(0.f, jitterForGain) * ((inst == OH) ? -0.35f : -0.20f) * (0.40f + hum);
			db += hatSwingDb + lateSofteningDb;
		}
		pendingGainMult[inst] = std::pow(10.f, db / 20.f);
	}

	inline int quantizeKit(float knob, float cvVolts) {
		// 1V per kit step — 0V = no offset, 9V = +9 kits.
		float cv = cvVolts;
		float x = knob + cv;
		int k = (int)std::lround(x);
		if (k < 0) k = 0;
		if (k >= NUM_KITS) k = NUM_KITS - 1;
		return k;
	}

	inline uint32_t calcPhaseIncSemis(float semis, float engineSampleRate, float sampleRate) {
		int s = (int)std::lround(semis);
		if (s < -TUNE_RANGE_SEMIS) s = -TUNE_RANGE_SEMIS;
		if (s > TUNE_RANGE_SEMIS) s = TUNE_RANGE_SEMIS;
		float ratio = semitoneRatio[s + TUNE_RANGE_SEMIS];

		// Base speed to keep sample timing consistent across engine sample rates.
		float base = (sampleRate > 0.f && engineSampleRate > 0.f) ? (sampleRate / engineSampleRate) : 1.f;
		float inc = base * ratio;
		return (uint32_t)std::lround(inc * (float)(1u << 16));
	}

	void chokeOH() {
		Voice& oh = v[OH];
		if (oh.active) {
			oh.choking = true;
			oh.fadeOut = 48;
		}
	}

	int selectLayerEuclidRR(int inst, int baseLayer) {
		RRState& st = rr[inst];
		uint32_t pos = st.hit & 15u;
		st.hit++;

		// 16-hit cycle: evenly-spaced accents.
		// offset2 (rarest): 3/16
		// offset1 (common): 6/16
		const bool off2 = (pos == 2u) || (pos == 8u) || (pos == 14u);
		const bool off1 = (pos == 0u) || (pos == 3u) || (pos == 5u) || (pos == 9u) || (pos == 11u) || (pos == 15u);

		int off = off2 ? 2 : (off1 ? 1 : 0);
		return (baseLayer + off) % 3;
	}

	int selectLayerDensityRR(int inst, int baseLayer, float engineSampleRate) {
		RRState& st = rr[inst];
		uint32_t now = sampleCounter;
		uint32_t dt = now - st.lastFrame;
		st.lastFrame = now;
		uint32_t ph = (++st.hit) + (uint32_t)inst * 11u;

		// Timing thresholds in frames (chosen to match breakcore/amen densities)
		uint32_t fastTh = (uint32_t)std::lround(0.045f * engineSampleRate); // ~45ms
		uint32_t medTh  = (uint32_t)std::lround(0.160f * engineSampleRate); // ~160ms

		if (dt <= fastTh) {
			static constexpr uint8_t pat[4] = {0, 1, 2, 1};
			return (baseLayer + pat[ph & 3u]) % 3;
		}
		if (dt <= medTh) {
			static constexpr uint8_t pat[4] = {0, 1, 0, 2};
			return (baseLayer + pat[ph & 3u]) % 3;
		}

		// Slow hits: mostly stable, with a subtle predictable variation.
		if ((ph & 7u) == 0u) {
			return (baseLayer + 1) % 3;
		}
		return baseLayer;
	}

	void getAccentLayerThresholds(int inst, float sensitivity, float& lowThreshold, float& highThreshold) const {
		float sensitivityBias = 0.5f - sensitivity;
		switch (inst) {
			case BD:
				lowThreshold = 0.34f + sensitivityBias * 0.10f;
				highThreshold = 0.86f + sensitivityBias * 0.05f;
				break;
			case SN:
				lowThreshold = 0.34f + sensitivityBias * 0.10f;
				highThreshold = 0.90f + sensitivityBias * 0.04f;
				break;
			case GSN:
				lowThreshold = 0.28f + sensitivityBias * 0.10f;
				highThreshold = 0.74f + sensitivityBias * 0.06f;
				break;
			case CH:
				lowThreshold = 0.32f + sensitivityBias * 0.12f;
				highThreshold = 0.78f + sensitivityBias * 0.08f;
				break;
			case OH:
				lowThreshold = 0.34f + sensitivityBias * 0.10f;
				highThreshold = 0.80f + sensitivityBias * 0.08f;
				break;
			case RC:
				lowThreshold = 0.34f + sensitivityBias * 0.10f;
				highThreshold = 0.84f + sensitivityBias * 0.06f;
				break;
			default:
				lowThreshold = 0.28f;
				highThreshold = 0.68f;
				break;
		}
		lowThreshold = clampf(lowThreshold, 0.12f, 0.48f);
		highThreshold = clampf(highThreshold, lowThreshold + 0.12f, 0.90f);
	}

	float getAccentVariationChance(int inst) const {
		switch (inst) {
			case BD:  return 0.12f;
			case SN:  return 0.14f;
			case GSN: return 0.18f;
			case CH:  return 0.24f;
			case OH:  return 0.26f;
			case RC:  return 0.26f;
			default:  return 0.10f;
		}
	}

	void getAccentVariationWeights(int inst, float& downWeight, float& upWeight) const {
		switch (inst) {
			case BD:
				downWeight = 0.85f;
				upWeight = 0.55f;
				break;
			case SN:
				downWeight = 0.95f;
				upWeight = 0.60f;
				break;
			case GSN:
				downWeight = 0.95f;
				upWeight = 0.65f;
				break;
			case CH:
				downWeight = 0.90f;
				upWeight = 0.90f;
				break;
			case OH:
				downWeight = 0.85f;
				upWeight = 0.95f;
				break;
			case RC:
				downWeight = 1.00f;
				upWeight = 0.70f;
				break;
			default:
				downWeight = 0.80f;
				upWeight = 0.80f;
				break;
		}
	}

	float accentThresholdProximity(float value, float threshold, float window) const {
		if (window <= 1e-6f) return 0.f;
		return clampf(1.f - std::fabs(value - threshold) / window, 0.f, 1.f);
	}

	float getRoleAwareAccentNorm(int inst, float accentNorm, float engineSampleRate) const {
		float dtSeconds = 0.25f;
		if (accentRoleInitialized_[inst] && engineSampleRate > 1.f) {
			uint32_t dtSamples = sampleCounter - accentRoleLastTriggerSample_[inst];
			dtSeconds = (float)dtSamples / engineSampleRate;
		}

		float adjusted = accentNorm;
		switch (inst) {
			case BD:
				if (dtSeconds < 0.18f) adjusted -= 0.06f;
				else if (dtSeconds > 0.42f) adjusted += 0.04f;
				break;
			case SN:
				if (dtSeconds > 0.20f) adjusted += 0.05f;
				else adjusted -= 0.02f;
				break;
			case GSN:
				adjusted -= 0.10f;
				if (dtSeconds < 0.18f) adjusted -= 0.04f;
				break;
			case CH:
				if (dtSeconds < 0.14f) adjusted -= 0.08f;
				else if (dtSeconds > 0.24f) adjusted += 0.02f;
				break;
			case OH:
				if (dtSeconds < 0.22f) adjusted -= 0.05f;
				else if (dtSeconds > 0.40f) adjusted += 0.04f;
				break;
			case RC:
				if (dtSeconds < 0.22f) adjusted -= 0.10f;
				else if (dtSeconds > 0.42f) adjusted += 0.06f;
				break;
			default:
				break;
		}
		return clampf(adjusted, 0.f, 1.f);
	}

	int choosePolyAccentLayer(int inst, float accentNorm, float sensitivity, float engineSampleRate) {
		float adjustedAccentNorm = getRoleAwareAccentNorm(inst, accentNorm, engineSampleRate);
		float lowThreshold = 0.28f;
		float highThreshold = 0.68f;
		getAccentLayerThresholds(inst, sensitivity, lowThreshold, highThreshold);

		int targetLayer = 1;
		if (adjustedAccentNorm < lowThreshold) targetLayer = 0;
		else if (adjustedAccentNorm >= highThreshold) targetLayer = 2;

		float variationBase = getAccentVariationChance(inst) * (0.35f + 0.65f * sensitivity);
		float downWeight = 0.8f;
		float upWeight = 0.8f;
		getAccentVariationWeights(inst, downWeight, upWeight);
		float lowNear = accentThresholdProximity(adjustedAccentNorm, lowThreshold, 0.18f);
		float highNear = accentThresholdProximity(adjustedAccentNorm, highThreshold, 0.18f);
		float roll = rand01();

		if (targetLayer == 0) {
			float upChance = variationBase * upWeight * (0.20f + 0.80f * lowNear);
			if (roll < upChance) return 1;
			return 0;
		}

		if (targetLayer == 2) {
			float downChance = variationBase * downWeight * (0.20f + 0.80f * highNear);
			if (roll < downChance) return 1;
			return 2;
		}

		float downChance = variationBase * downWeight * (0.55f + 0.45f * lowNear);
		float upChance = variationBase * upWeight * (0.45f + 0.55f * highNear);
		float totalChance = downChance + upChance;
		if (roll < totalChance) {
			float dirRoll = rand01();
			float downWeight = (totalChance > 1e-6f) ? (downChance / totalChance) : 0.5f;
			return (dirRoll < downWeight) ? 0 : 2;
		}
		return 1;
	}

	void startVoice(int inst, const ProcessArgs& args, bool /*isRoll*/) {
		Voice& vc = v[inst];

		// Always apply the pending kit on a new trigger — the voice is being
		// re-started so the old sample pointer becomes irrelevant.
		vc.currentKit = vc.pendingKit;

		// Velocity behavior:
		// - If VEL is patched, it selects the sample layer (and also applies a small gain factor).
		// - If VEL is not patched, poly Accent can drive per-voice layer+gain.
		// - Otherwise, manual LYR and optional RR select the layer.
		bool velConnected = inputs[VEL_INPUT_BASE + inst].isConnected();
		float velNorm = velConnected ? clampf(inputs[VEL_INPUT_BASE + inst].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
		bool accentConnected = inputs[ACCENT_CV_INPUT].isConnected();
		int accentChannels = accentConnected ? inputs[ACCENT_CV_INPUT].getChannels() : 0;
		bool accentPoly = accentChannels > 1;
		int accentChannel = accentPoly ? std::min(inst, accentChannels - 1) : 0;
		float accentNorm = accentConnected ? clampf(inputs[ACCENT_CV_INPUT].getVoltage(accentChannel) / 10.f, 0.f, 1.f) : 0.f;
		float accentAmt = clampf(params[ACCENT_PARAM].getValue(), 0.f, 1.f);
		float accentPolyNorm = accentNorm;
		int baseLayer = (int)std::lround(params[LAYER_SELECT_PARAM].getValue());
		if (baseLayer < 0) baseLayer = 0;
		if (baseLayer > 2) baseLayer = 2;

		int layer = 0;
		if (velConnected) {
			layer = (velNorm < 0.33f) ? 0 : (velNorm < 0.66f ? 1 : 2);
		}
		else if (accentPoly) {
			layer = choosePolyAccentLayer(inst, accentPolyNorm, accentAmt, args.sampleRate);
		}
		else {
			int rrMode = (int)std::lround(params[RR_MODE_PARAM_BASE + inst].getValue());
			if (rrMode < 0) rrMode = 0;
			if (rrMode > 2) rrMode = 2;
			switch (rrMode) {
				default:
				case 0: layer = baseLayer; break;
				case 1: layer = selectLayerEuclidRR(inst, baseLayer); break;
				case 2: layer = selectLayerDensityRR(inst, baseLayer, args.sampleRate); break;
			}
		}

		float velFactor = 1.f;
		if (velConnected) {
			// 0V..10V => 0.75..1.25
			velFactor = 0.75f + 0.5f * velNorm;
		}
		else if (accentPoly) {
			velFactor = 0.75f + 0.6f * accentPolyNorm;
		}
		// Clamp base velocity behavior before applying global accent.
		velFactor = clampf(velFactor, 0.75f, 1.35f);

		// Accent CV (optional): punchy, non-linear (breakcore-friendly)
		// Audition mode: up to +350% at full Accent amount, with a cubic curve (gentle low, explosive high).
		if (accentConnected && !accentPoly) {
			float acc = accentNorm;
			float accAmt = clampf(params[ACCENT_PARAM].getValue(), 0.f, 1.f);
			float curve = acc * acc * acc;
			velFactor *= 1.f + (3.5f * accAmt * curve);
		}
		// Apply humanize correlated gain (set by scheduler when the trigger fires).
		velFactor *= nextGainMult[inst];
		nextGainMult[inst] = 1.f;
		velFactor = clampf(velFactor, 0.65f, 4.50f);
		const Sample& s = g_kits[vc.currentKit][inst][layer];

		vc.data = s.data;
		vc.frames = s.frames;
		vc.sampleRate = s.sampleRate;

		float len = clampf(params[LENGTH_PARAM_BASE + inst].getValue(), 0.f, 1.f);
		uint32_t endF = (uint32_t)std::lround((float)vc.frames * len);
		if (endF < 2) endF = 2;
		if (endF > vc.frames) endF = vc.frames;
		vc.endFrame = endF;
		vc.fadeStartFrame = endF;
		vc.lengthFadeInv = 0.f;
		if (len < 0.999f && endF > 2) {
			uint32_t maxFadeFrames = endF - 1;
			uint32_t fadeFrames = (uint32_t)std::lround((1.f - len) * (float)vc.frames * 0.5f);
			if (fadeFrames < 48u) fadeFrames = 48u;
			if (fadeFrames > maxFadeFrames) fadeFrames = maxFadeFrames;
			vc.fadeStartFrame = endF - fadeFrames;
			vc.lengthFadeInv = (fadeFrames > 0u) ? (1.f / (float)fadeFrames) : 0.f;
		}

		bool genPitchEnabled = (params[GEN_PITCH_PARAM].getValue() > 0.5f);
		float semiKnob = params[TUNE_PARAM_BASE + inst].getValue();
		float semiCv = 0.f;
		if (!genPitchEnabled) {
			semiCv = inputs[TUNE_CV_INPUT_BASE + inst].isConnected() ? inputs[TUNE_CV_INPUT_BASE + inst].getVoltage() * 12.f : 0.f;
		}
		float genSemi = (genPitchEnabled && genPitchAppliesToInst(inst)) ? nextPitchSemis_[inst] : 0.f;
		nextPitchSemis_[inst] = 0.f;
		float phaseInc = (float)calcPhaseIncSemis(semiKnob + semiCv + genSemi, args.sampleRate, (float)vc.sampleRate);
		if (isHatInst(inst)) {
			float hum = isPolyAccentModeActive() ? 0.f : clampf(params[HUMANIZE_PARAM].getValue(), 0.f, 1.f);
			float rateJitter = clampf(randGauss01() * (0.0035f + 0.0045f * hum), -0.012f, 0.012f);
			phaseInc *= 1.f + rateJitter;
		}
		vc.phaseInc = (uint32_t)std::lround(std::max(1.f, phaseInc));

		// Humanize is handled by the trigger scheduler (timing + correlated gain).
		// Always start samples at their transient to preserve punch.
		vc.phase = 0u;

		float lvl = clampf(params[LEVEL_PARAM_BASE + inst].getValue(), 0.f, 1.f);
		vc.gain = lvl * velFactor;

		vc.fadeIn = 16;
		vc.choking = false;
		vc.fadeOut = 0;
		vc.active = true;

		if (inst == CH) {
			chokeOH();
		}

		accentRoleLastTriggerSample_[inst] = sampleCounter;
		accentRoleInitialized_[inst] = true;
	}

	inline float nextSample(Voice& vc, int inst, float hum) {
		if (!vc.active || !vc.data || vc.frames < 2 || vc.endFrame < 2) {
			vc.active = false;
			return 0.f;
		}

		uint32_t idx = vc.phase >> 16;
		if (idx >= vc.endFrame - 1) {
			vc.active = false;
			vc.choking = false;
			vc.fadeOut = 0;
			return 0.f;
		}

		uint32_t frac = vc.phase & 0xFFFFu;
		float a = vc.data[idx] * (1.f / 32768.f);
		float b = vc.data[idx + 1] * (1.f / 32768.f);
		float t = (float)frac * (1.f / 65536.f);
		float s = a + (b - a) * t;

		vc.phase += vc.phaseInc;

		float g = vc.gain;
		// LEN behaves like a cheap per-voice volume fade rather than a hard chop.
		// The fade window is precomputed at trigger time to keep runtime cost minimal.
		if (vc.lengthFadeInv > 0.f && idx >= vc.fadeStartFrame) {
			float k = (float)(vc.endFrame - 1u - idx) * vc.lengthFadeInv;
			if (k < 0.f) k = 0.f;
			if (k > 1.f) k = 1.f;
			g *= k;
		}
		if (vc.fadeIn) {
			float k = (16.f - (float)vc.fadeIn) / 16.f;
			g *= k;
			vc.fadeIn--;
		}
		if (vc.choking && vc.fadeOut) {
			float k = (float)vc.fadeOut / 48.f;
			g *= k;
			vc.fadeOut--;
			if (vc.fadeOut == 0) {
				vc.active = false;
				vc.choking = false;
				return 0.f;
			}
		}

		return s * g;
	}

	inline float nextHatSample(Voice& vc, int inst, float hum) {
		if (!vc.active || !vc.data || vc.frames < 2 || vc.endFrame < 2) {
			vc.active = false;
			return 0.f;
		}

		uint32_t idx = vc.phase >> 16;
		if (idx >= vc.endFrame - 1) {
			vc.active = false;
			vc.choking = false;
			vc.fadeOut = 0;
			return 0.f;
		}

		uint32_t frac = vc.phase & 0xFFFFu;
		float a = vc.data[idx] * (1.f / 32768.f);
		float b = vc.data[idx + 1] * (1.f / 32768.f);
		float t = (float)frac * (1.f / 65536.f);
		float s = a + (b - a) * t;

		// Hat air synthesis (CH and OH only — no isHatInst() branch needed here)
		{
			const float toneCoeff = (inst == OH) ? 0.024f : 0.040f;
			hatToneState_[inst] += toneCoeff * (s - hatToneState_[inst]);
			const float smear = (inst == OH) ? (0.15f + 0.10f * hum) : (0.10f + 0.08f * hum);
			s = s * (1.f - smear) + hatToneState_[inst] * smear;

			const float excite = std::fabs(s) * ((inst == OH) ? 0.040f : 0.028f) * (0.45f + hum);
			hatAirEnv_[inst] = clampf(hatAirEnv_[inst] + excite, 0.f, 1.f);
			const float noise = rand01() * 2.f - 1.f;
			hatAirColor_[inst] += (0.08f + 0.03f * hum) * (noise - hatAirColor_[inst]);
			const float air = noise - hatAirColor_[inst];
			s += air * hatAirEnv_[inst] * ((inst == OH) ? 0.0105f : 0.0075f);
			hatAirEnv_[inst] *= (inst == OH) ? 0.9968f : 0.9938f;
			// Flush subnormal floats to zero — exponential decay toward zero causes
			// massive FPU slowdown on ARM (MetaModule) without DAZ/FTZ hardware flag.
			if (hatAirEnv_[inst]   < 1e-30f) hatAirEnv_[inst]   = 0.f;
			if (std::fabs(hatAirColor_[inst])  < 1e-30f) hatAirColor_[inst]  = 0.f;
			if (std::fabs(hatToneState_[inst]) < 1e-30f) hatToneState_[inst] = 0.f;
		}

		vc.phase += vc.phaseInc;

		float g = vc.gain;
		if (vc.lengthFadeInv > 0.f && idx >= vc.fadeStartFrame) {
			float k = (float)(vc.endFrame - 1u - idx) * vc.lengthFadeInv;
			if (k < 0.f) k = 0.f;
			if (k > 1.f) k = 1.f;
			g *= k;
		}
		if (vc.fadeIn) {
			float k = (16.f - (float)vc.fadeIn) / 16.f;
			g *= k;
			vc.fadeIn--;
		}
		if (vc.choking && vc.fadeOut) {
			float k = (float)vc.fadeOut / 48.f;
			g *= k;
			vc.fadeOut--;
			if (vc.fadeOut == 0) {
				vc.active = false;
				vc.choking = false;
				return 0.f;
			}
		}

		return s * g;
	}

	void process(const ProcessArgs& args) override {
		sampleCounter++;
		// Control-rate update (every 32 samples)
		if (++ctrlDiv >= 32) {
			ctrlDiv = 0;

			float kitKnob = params[KIT_PARAM].getValue();
			float kitCv = inputs[KIT_CV_INPUT].isConnected() ? inputs[KIT_CV_INPUT].getVoltage() : 0.f;
			int targetKit = quantizeKit(kitKnob, kitCv);

			// Treat kit change as a new phrase for generative pitch.
			if (lastQuantizedKit_ >= 0 && targetKit != lastQuantizedKit_) {
				for (int i = 0; i < NUM_INST; ++i) {
					resetGenPitchPhrase(i);
				}
				// Kit knob changed — clear scramble state.
				scrambleActive_ = false;
			}
			lastQuantizedKit_ = targetKit;

			// Scramble button: randomize per-instrument kit for BD, CH, OH, RC.
			// SN and GSN (SN2) always stay on the current kit.
			if (scrambleTrig.process(params[SCRAMBLE_PARAM].getValue())) {
				scrambleActive_ = true;
				for (int i = 0; i < NUM_INST; ++i) {
					if (i == SN || i == GSN) {
						scrambleKitMap_[i] = targetKit;
					} else {
						scrambleKitMap_[i] = (int)(rand01() * NUM_KITS);
						if (scrambleKitMap_[i] >= NUM_KITS) scrambleKitMap_[i] = NUM_KITS - 1;
					}
				}
			}

			for (int i = 0; i < NUM_INST; ++i) {
				int kitForInst = scrambleActive_ ? scrambleKitMap_[i] : targetKit;
				// Keep snares on current kit even if scramble was set before kit change.
				if (scrambleActive_ && (i == SN || i == GSN)) {
					kitForInst = targetKit;
					scrambleKitMap_[i] = targetKit;
				}
				v[i].pendingKit = (uint8_t)kitForInst;
				if (!v[i].active) {
					v[i].currentKit = v[i].pendingKit;
				}
				setPan(i, params[PAN_PARAM_BASE + i].getValue());
			}
		}

		// Triggers (humanized scheduling)
		bool genPitchEnabled = (params[GEN_PITCH_PARAM].getValue() > 0.5f);
		for (int i = 0; i < NUM_INST; ++i) {
			// Explicit thresholds: avoids false triggers near 1V and works great with standard 10V pulses.
			if (trig[i].process(inputs[TRIG_INPUT_BASE + i].getVoltage(), 0.1f, 2.f)) {
				// If a previous trigger is pending, fire it now, then schedule the new one.
				if (pendingTrig[i]) {
					nextGainMult[i] = pendingGainMult[i];
					nextPitchSemis_[i] = pendingPitchSemis_[i];
					pendingTrig[i] = false;
					pendingDelay[i] = 0;
					startVoice(i, args, false);
				}

				if (genPitchEnabled && genPitchAppliesToInst(i)) {
					// Compute generative pitch at trigger time (deterministic for identical trigger patterns).
					float velNorm = 0.f;
					bool velConnected = inputs[VEL_INPUT_BASE + i].isConnected();
					bool accentConnected = inputs[ACCENT_CV_INPUT].isConnected();
					int accentChannels = accentConnected ? inputs[ACCENT_CV_INPUT].getChannels() : 0;
					bool accentPoly = accentChannels > 1;
					int accentChannel = accentPoly ? std::min(i, accentChannels - 1) : 0;
					if (velConnected) {
						velNorm = clampf(inputs[VEL_INPUT_BASE + i].getVoltage() / 10.f, 0.f, 1.f);
					}
					bool accent = (velConnected && velNorm >= 0.82f);
					if (accentConnected) {
						float acc = clampf(inputs[ACCENT_CV_INPUT].getVoltage(accentChannel) / 10.f, 0.f, 1.f);
						accent = accent || (acc >= 0.70f);
					}
					pendingPitchSemis_[i] = computeGenPitchSemis(i, args.sampleRate, velNorm, accent);
				}
				else {
					pendingPitchSemis_[i] = 0.f;
				}
				scheduleHumanizedTrigger(i, args.sampleRate);
			}
		}

		// Fire scheduled triggers when their delays elapse.
		for (int i = 0; i < NUM_INST; ++i) {
			if (!pendingTrig[i]) continue;
			if (pendingDelay[i] <= 0) {
				nextGainMult[i] = pendingGainMult[i];
				nextPitchSemis_[i] = pendingPitchSemis_[i];
				pendingTrig[i] = false;
				startVoice(i, args, false);
			}
			else {
				pendingDelay[i]--;
			}
		}

		float mixL = 0.f;
		float mixR = 0.f;
		float hum = isPolyAccentModeActive() ? 0.f : clampf(params[HUMANIZE_PARAM].getValue(), 0.f, 1.f);

		for (int i = 0; i < NUM_INST; ++i) {
			Voice& vc = v[i];
			float s = isHatInst(i) ? nextHatSample(vc, i, hum) : nextSample(vc, i, hum);

			outputs[INST_OUT_BASE + i].setVoltage(5.f * s);

			if (outputs[INST_OUT_BASE + i].isConnected()) {
				continue;
			}

			mixL += s * vc.panL;
			mixR += s * vc.panR;
		}

		// Effective drive is capped to avoid the harsh/noisy extreme top end.
		constexpr float DRIVE_MAX = 0.94f;
		float driveAmt = clampf(params[DRIVE_PARAM].getValue(), 0.f, 1.f);
		if (inputs[DRIVE_CV_INPUT].isConnected()) {
			float driveCv = clampf(inputs[DRIVE_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			driveAmt = clampf(driveAmt + driveCv, 0.f, 1.f);
		}
		float drive = DRIVE_MAX * driveAmt;
		mixL = softClip(mixL, drive);
		mixR = softClip(mixR, drive);

		const float envIn = std::max(std::fabs(mixL), std::fabs(mixR));
		const float envCoeff = (envIn > mixEnv_) ? envAttackCoeff_ : envReleaseCoeff_;
		mixEnv_ = envIn + envCoeff * (mixEnv_ - envIn);
		if (mixEnv_ < 1e-30f) mixEnv_ = 0.f;
		constexpr float MIX_OUTPUT_GAIN = 2.f; // +6 dB on stereo mix outs only

		outputs[MIX_L_OUTPUT].setVoltage(5.f * MIX_OUTPUT_GAIN * mixL);
		outputs[MIX_R_OUTPUT].setVoltage(5.f * MIX_OUTPUT_GAIN * mixR);
		outputs[ENV_OUTPUT].setVoltage(clampf(20.f * mixEnv_, 0.f, 10.f));
	}
};

// C++11 requires an out-of-class definition for ODR-used constexpr static members.
constexpr int8_t Amenolith::PITCH_TABLE[Amenolith::PITCH_TABLE_SIZE];

static inline void amenolithScaleSvgSwitch(app::SvgSwitch* s, float scale) {
	if (!s)
		return;
	if (scale <= 0.f)
		return;

	s->box.size = s->box.size.mult(scale);
	if (s->fb)
		s->fb->box.size = s->box.size;
	if (s->shadow)
		s->shadow->box.size = s->box.size;
	if (s->sw)
		s->sw->box.size = s->box.size;
}

struct AmenolithCKSS : CKSS {
	AmenolithCKSS() {
		amenolithScaleSvgSwitch(this, 0.75f);
	}
};

struct AmenolithCKSSThree : CKSSThree {
	AmenolithCKSSThree() {
		amenolithScaleSvgSwitch(this, 0.75f);
	}
};

// Per-voice RR mode switch (3 positions). Black 3-position switch.
using AmenolithRRModeSwitch = CKSSThree;

struct AmenolithWidget : ModuleWidget {
	AmenolithWidget(Amenolith* module) {
		setModule(module);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Amenolith.png")));
	#else
		// VCV Rack: no SVG; hardcode 24HP and use PNG directly.
		box.size = Vec(RACK_GRID_WIDTH * 24, RACK_GRID_HEIGHT);
		{
			auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Amenolith.png"));
			panelBg->box.pos = Vec(0, 0);
			panelBg->box.size = box.size;
			addChild(panelBg);
		}
	#endif

		#ifndef METAMODULE
		NVGcolor neonGreen = nvgRGB(0xcd, 0xbc, 0x2d);
		NVGcolor dimGreen  = nvgRGB(0xcd, 0xbc, 0x2d);
		struct AmenolithPort : MVXPort {
			AmenolithPort() {
				imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
				imageHandle = -1;
			}
		};
		struct AmenolithOutputPort : MVXPort {
			AmenolithOutputPort() {
				imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_s_yellow.png");
				imageHandle = -1;
			}
		};
		#endif
		#ifdef METAMODULE
		using AmenolithPort = MVXPort;
		using AmenolithOutputPort = MVXPort;
		#endif

		// Shift everything down by 10px (~2.65mm) to make room for the headline.
		const float yOff = 2.65f;
		const Vec switchShiftPx = Vec(0.f, 3.f);
		const Vec panelShiftPx = Vec(0.f, 13.f);
		auto mm = [&](Vec v) {
			return mm2px(v).plus(panelShiftPx);
		};
		const float mixShiftPx = 22.f;
		const float mixTightenPx = 8.f;
		const float mixRShiftPx = mixShiftPx - mixTightenPx;
		const Vec envOutPos = mm(Vec(102.f, 20.f + yOff));
		const Vec mixLOutPos = mm(Vec(102.f, 20.f + yOff)).plus(Vec(mixShiftPx, 0.f));
		const Vec mixROutPos = mm(Vec(112.f, 20.f + yOff)).plus(Vec(mixRShiftPx, 0.f));

		#ifndef METAMODULE
		auto addLabel = [&](BCPanelLabel* label) {
			label->box.pos = label->box.pos.plus(panelShiftPx);
			addChild(label);
		};
		#endif

		// --- Global row ---
		addParam(createParamCentered<RoundLargeBlackKnob>(mm(Vec(14.f, 20.f + yOff)), module, Amenolith::KIT_PARAM));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(28.f, 20.f + yOff)), module, Amenolith::KIT_CV_INPUT));
		addParam(createParamCentered<TL1105>(mm(Vec(35.f, 20.f + yOff)), module, Amenolith::SCRAMBLE_PARAM));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(42.f, 20.f + yOff)), module, Amenolith::ACCENT_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm(Vec(49.f, 20.f + yOff)), module, Amenolith::ACCENT_PARAM));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(56.f, 20.f + yOff)), module, Amenolith::DRIVE_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm(Vec(66.f, 20.f + yOff)), module, Amenolith::DRIVE_PARAM));
		addParam(createParamCentered<Trimpot>(mm(Vec(76.f, 20.f + yOff)), module, Amenolith::HUMANIZE_PARAM));
		addParam(createParamCentered<AmenolithCKSSThree>(mm(Vec(86.f, 20.f + yOff)).plus(switchShiftPx), module, Amenolith::LAYER_SELECT_PARAM));
		addParam(createParamCentered<AmenolithCKSS>(mm(Vec(94.f, 20.f + yOff)).plus(switchShiftPx), module, Amenolith::GEN_PITCH_PARAM));
		addOutput(createOutputCentered<AmenolithOutputPort>(envOutPos, module, Amenolith::ENV_OUTPUT));
		addOutput(createOutputCentered<AmenolithOutputPort>(mixLOutPos, module, Amenolith::MIX_L_OUTPUT));
		addOutput(createOutputCentered<AmenolithOutputPort>(mixROutPos, module, Amenolith::MIX_R_OUTPUT));

		#ifndef METAMODULE
		// KIT label 3px higher (~0.79mm)
		addLabel(bcCreateLabel(Vec(14.f, 13.f + yOff - 0.79f), "KIT", 8.f, neonGreen));
		addLabel(bcCreateLabel(Vec(28.f, 13.f + yOff), "KIT CV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(35.f, 13.f + yOff), "SCR", 6.5f, dimGreen));
		addLabel(bcCreateLabel(Vec(42.f, 13.f + yOff), "ACC", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(49.f, 13.f + yOff), "SENS", 6.5f, dimGreen));
		addLabel(bcCreateLabel(Vec(56.f, 13.f + yOff), "DRV CV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(66.f, 13.f + yOff), "DRV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(76.f, 13.f + yOff), "HUM", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(86.f, 13.f + yOff), "LAYER", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(94.f, 13.f + yOff), "PITCH", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(102.f, 13.f + yOff), "ENV OUT", 8.f, neonGreen));
		{
			auto* mixLLabel = bcCreateLabel(Vec(102.f, 13.f + yOff), "MIX L", 8.f, neonGreen);
			mixLLabel->box.pos.x += mixShiftPx;
			addLabel(mixLLabel);
		}
		{
			auto* mixRLabel = bcCreateLabel(Vec(112.f, 13.f + yOff), "MIX R", 8.f, neonGreen);
			mixRLabel->box.pos.x += mixRShiftPx;
			addLabel(mixRLabel);
		}

		// Column headers
		const Vec headerDropPx = Vec(0.f, 3.f);
		{
			auto* trigHdr = bcCreateLabel(Vec(16.f, 31.f + yOff), "TRIG", 7.5f, dimGreen);
			trigHdr->box.pos.y += panelShiftPx.y + headerDropPx.y;
			trigHdr->box.pos.x -= 4.f;
			addChild(trigHdr);
		}
		{
			auto* velHdr = bcCreateLabel(Vec(28.f, 31.f + yOff), "VEL", 7.5f, dimGreen);
			addLabel(velHdr);
			velHdr->box.pos.y += headerDropPx.y;
		}
		{
			auto* tuneHdr = bcCreateLabel(Vec(40.f, 31.f + yOff), "TUNE", 7.5f, dimGreen);
			tuneHdr->box.pos.y += panelShiftPx.y + headerDropPx.y;
			tuneHdr->box.pos.x += 2.f;
			addChild(tuneHdr);
		}
		{
			auto* panHdr = bcCreateLabel(Vec(56.f, 31.f + yOff), "PAN", 7.5f, dimGreen);
			addLabel(panHdr);
			panHdr->box.pos.y += headerDropPx.y;
		}
		{
			auto* lvlHdr = bcCreateLabel(Vec(70.f, 31.f + yOff), "LVL", 7.5f, dimGreen);
			addLabel(lvlHdr);
			lvlHdr->box.pos.y += headerDropPx.y;
		}
		{
			auto* semiHdr = bcCreateLabel(Vec(84.f, 31.f + yOff), "SEMI", 7.5f, dimGreen);
			addLabel(semiHdr);
			semiHdr->box.pos.y += headerDropPx.y;
		}
		{
			auto* lenHdr = bcCreateLabel(Vec(98.f, 31.f + yOff), "LENGTH", 7.5f, dimGreen);
			addLabel(lenHdr);
			lenHdr->box.pos.y += headerDropPx.y;
		}
		{
			auto* outHdr = bcCreateLabel(Vec(112.f, 31.f + yOff), "OUT", 7.5f, dimGreen);
			addLabel(outHdr);
			outHdr->box.pos.y += headerDropPx.y;
		}
		#endif

		// --- Instrument rows ---
		const float rowY0 = 40.f + yOff;
		const float rowDy = 18.f;
		const float rowTightenPx = 9.f;
		// Re-spaced columns (even 14mm grid) after removing ROLL.
		#ifndef METAMODULE
		const float xName = 3.5f;
		#endif
		const float xTrig = 14.f;
		const float xVel  = 28.f;
		const float xTune = 42.f;
		const float xRR   = xTrig + 7.f; // beside TRIG jack, leaves clearance before VEL column
		const float xPan  = 56.f;
		const float xLvl  = 70.f;
		const float xSemi = 84.f;
		const float xLen  = 98.f;
		const float xOut  = 112.f;

		#ifndef METAMODULE
		static const char* rowNames[NUM_INST] = {"BD", "SN", "SN2", "CH", "OH", "RC"};
		#endif
		for (int i = 0; i < NUM_INST; ++i) {
			float yy = rowY0 + rowDy * i;
			Vec rowShiftPx = Vec(0.f, -rowTightenPx * i);

			#ifndef METAMODULE
			auto* nameLbl = bcCreateLabel(Vec(xName, yy), rowNames[i], 10.f, neonGreen, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			addLabel(nameLbl);
			nameLbl->box.pos.x += 2.f;
			nameLbl->box.pos.y += rowShiftPx.y;
			#endif

			addInput(createInputCentered<AmenolithPort>(mm(Vec(xTrig, yy)).plus(rowShiftPx), module, Amenolith::TRIG_INPUT_BASE + i));
			// Per-voice RR mode switch (unlabeled), beside TRIG input
			addParam(createParamCentered<AmenolithRRModeSwitch>(mm(Vec(xRR, yy)).plus(rowShiftPx), module, Amenolith::RR_MODE_PARAM_BASE + i));
			addInput(createInputCentered<AmenolithPort>(mm(Vec(xVel, yy)).plus(rowShiftPx), module, Amenolith::VEL_INPUT_BASE + i));
			addInput(createInputCentered<AmenolithPort>(mm(Vec(xTune, yy)).plus(rowShiftPx), module, Amenolith::TUNE_CV_INPUT_BASE + i));

			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xPan, yy)).plus(rowShiftPx), module, Amenolith::PAN_PARAM_BASE + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xLvl, yy)).plus(rowShiftPx), module, Amenolith::LEVEL_PARAM_BASE + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xSemi, yy)).plus(rowShiftPx), module, Amenolith::TUNE_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm(Vec(xLen, yy)).plus(rowShiftPx), module, Amenolith::LENGTH_PARAM_BASE + i));

			addOutput(createOutputCentered<AmenolithOutputPort>(mm(Vec(xOut, yy)).plus(rowShiftPx), module, Amenolith::INST_OUT_BASE + i));
		}
	}

	void appendContextMenu(Menu* menu) override {
		Amenolith* module = dynamic_cast<Amenolith*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Amenolith"));
	}
};
Model* modelAmenolith = createModel<Amenolith, AmenolithWidget>("Amenolith");
