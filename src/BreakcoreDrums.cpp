#include "plugin.hpp"
#include "sampler/DrumKits.h"

#include <cmath>

#ifndef METAMODULE
#include <osdialog.h>
#include "ui/PngPanelBackground.hpp"
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace bdkit;

namespace {

#ifndef METAMODULE

static std::string joinPath(const std::string& dir, const std::string& name) {
	if (dir.empty()) return name;
	char last = dir.back();
	if (last == '/' || last == '\\') return dir + name;
	return dir + "/" + name;
}

static bool readAllBytes(const std::string& path, std::vector<uint8_t>& outBytes) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;

	if (std::fseek(f, 0, SEEK_END) != 0) {
		std::fclose(f);
		return false;
	}
	long size = std::ftell(f);
	if (size <= 0) {
		std::fclose(f);
		return false;
	}
	std::rewind(f);

	outBytes.resize((size_t)size);
	size_t nRead = std::fread(outBytes.data(), 1, outBytes.size(), f);
	std::fclose(f);
	return nRead == outBytes.size();
}

static inline uint16_t rd16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rd32(const uint8_t* p) {
	return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

// Minimal WAV reader for quick kit auditioning.
// Supports RIFF/WAVE, PCM format 16-bit, mono. Other formats are rejected.
static bool loadWavMonoPcm16(const std::string& path, std::vector<int16_t>& outPcm, uint32_t& outSampleRate,
	std::string& outErr) {
	outErr.clear();
	std::vector<uint8_t> bytes;
	if (!readAllBytes(path, bytes)) {
		outErr = "Failed to read: " + path;
		return false;
	}
	if (bytes.size() < 44) {
		outErr = "Not a WAV (too small): " + path;
		return false;
	}
	if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
		outErr = "Not a RIFF/WAVE file: " + path;
		return false;
	}

	bool fmtFound = false;
	bool dataFound = false;
	uint16_t audioFormat = 0;
	uint16_t numChannels = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;
	uint32_t dataPos = 0;
	uint32_t dataSize = 0;

	uint32_t pos = 12;
	while (pos + 8 <= bytes.size()) {
		const uint8_t* c = bytes.data() + pos;
		uint32_t chunkSize = rd32(c + 4);
		pos += 8;
		if (pos + chunkSize > bytes.size()) break;

		if (std::memcmp(c, "fmt ", 4) == 0) {
			if (chunkSize < 16) {
				outErr = "Invalid fmt chunk: " + path;
				return false;
			}
			const uint8_t* f = bytes.data() + pos;
			audioFormat = rd16(f + 0);
			numChannels = rd16(f + 2);
			sampleRate = rd32(f + 4);
			bitsPerSample = rd16(f + 14);
			fmtFound = true;
		}
		else if (std::memcmp(c, "data", 4) == 0) {
			dataPos = pos;
			dataSize = chunkSize;
			dataFound = true;
		}

		pos += chunkSize;
		if (pos & 1) pos++; // word-align
	}

	if (!fmtFound || !dataFound) {
		outErr = "Missing fmt/data chunk: " + path;
		return false;
	}
	if (audioFormat != 1) {
		outErr = "WAV must be PCM (format 1): " + path;
		return false;
	}
	if (numChannels != 1) {
		outErr = "WAV must be mono: " + path;
		return false;
	}
	if (bitsPerSample != 16) {
		outErr = "WAV must be 16-bit: " + path;
		return false;
	}
	if (sampleRate == 0) {
		outErr = "Invalid sample rate: " + path;
		return false;
	}

	uint32_t nSamp = dataSize / 2;
	outPcm.resize(nSamp);
	const uint8_t* d = bytes.data() + dataPos;
	for (uint32_t i = 0; i < nSamp; ++i) {
		outPcm[i] = (int16_t)rd16(d + i * 2);
	}
	outSampleRate = sampleRate;
	return true;
}

#endif // !METAMODULE

struct KitParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int k = (int)std::lround(getValue());
		if (k < 0) k = 0;
		if (k > 4) k = 4;
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
	uint32_t sampleRate = 48000;

	// Q16.16 phase: frame index in high 16 bits
	uint32_t phase = 0;
	uint32_t phaseInc = (1u << 16);

	float gain = 0.f;
	float panL = 0.7071f;
	float panR = 0.7071f;

	uint32_t fadeIn = 0;
	uint32_t fadeOut = 0;
	bool choking = false;
};

} // namespace

struct Amenolith : Module {
	enum ParamIds {
		KIT_PARAM,
		DRIVE_PARAM,
		HUMANIZE_PARAM,
		LAYER_SELECT_PARAM,
		ACCENT_PARAM,

		LEVEL_PARAM_BASE,
		PAN_PARAM_BASE = LEVEL_PARAM_BASE + NUM_INST,
		TUNE_PARAM_BASE = PAN_PARAM_BASE + NUM_INST,
		LENGTH_PARAM_BASE = TUNE_PARAM_BASE + NUM_INST,

			// Legacy global RR (kept for backward compatibility, no longer used in UI)
			RR_MODE_PARAM = LENGTH_PARAM_BASE + NUM_INST,

			// Per-voice RR mode (OFF/EUC/DENS), one switch under each TRIG input
			RR_MODE_PARAM_BASE,

			// Generative pitch modulation (global on/off). Appended to preserve indices.
			GEN_PITCH_PARAM = RR_MODE_PARAM_BASE + NUM_INST,
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
		OUTPUTS_LEN = INST_OUT_BASE + NUM_INST
	};
	enum LightIds {
		LIGHTS_LEN
	};

	dsp::SchmittTrigger trig[NUM_INST];
	Voice v[NUM_INST];

	int ctrlDiv = 0;
	uint32_t sampleCounter = 0;

	// Humanize scheduling (per-voice) to preserve transients.
	bool pendingTrig[NUM_INST] = {};
	int pendingDelay[NUM_INST] = {};
	float pendingGainMult[NUM_INST] = {};
	float nextGainMult[NUM_INST] = {};
	float humanizeDrift[NUM_INST] = {};

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

#ifndef METAMODULE
	struct RuntimeSample {
		std::vector<int16_t> pcm;
		uint32_t sampleRate = 48000;
		bool loaded = false;
	};

	struct RuntimeKit {
		RuntimeSample s[NUM_INST][NUM_LAYERS];
		bool loaded = false;
		std::string dirPath;
		std::string lastError;
	};

	RuntimeKit runtimeActive;
	RuntimeKit runtimePending;
	bool runtimePendingReady = false;
	bool runtimeStopVoicesRequested = false;
	std::mutex runtimeMutex;
	bool autoLoadKit01Requested = true;

	static const char* filePrefixForInst(int inst) {
		switch (inst) {
			case BD: return "BD";
			case SN: return "SN1";
			case GSN: return "SN2";
			case CH: return "CH";
			case OH: return "OH";
			default: return "??";
		}
	}

	bool loadRuntimeKitFromDir(RuntimeKit& dst, const std::string& dirPath) {
		dst = RuntimeKit{};
		dst.dirPath = dirPath;
		dst.loaded = false;
		dst.lastError.clear();

		for (int inst = 0; inst < NUM_INST; ++inst) {
			for (int layer = 0; layer < NUM_LAYERS; ++layer) {
				std::string err;
				uint32_t sr = 0;
				std::string fn = std::string(filePrefixForInst(inst)) + "_" + std::to_string(layer + 1) + ".wav";
				std::string path = joinPath(dirPath, fn);
				if (!loadWavMonoPcm16(path, dst.s[inst][layer].pcm, sr, err)) {
					fn = std::string(filePrefixForInst(inst)) + "_" + std::to_string(layer + 1) + ".WAV";
					path = joinPath(dirPath, fn);
					if (!loadWavMonoPcm16(path, dst.s[inst][layer].pcm, sr, err)) {
						dst.lastError = err;
						return false;
					}
				}
				dst.s[inst][layer].sampleRate = sr;
				dst.s[inst][layer].loaded = true;
			}
		}

		dst.loaded = true;
		return true;
	}

	bool queueLoadRuntimeKitFromDir(const std::string& dirPath) {
		std::lock_guard<std::mutex> lock(runtimeMutex);
		RuntimeKit tmp;
		if (!loadRuntimeKitFromDir(tmp, dirPath)) {
			runtimeActive.lastError = tmp.lastError.empty() ? "Failed to load kit" : tmp.lastError;
			return false;
		}
		runtimePending = std::move(tmp);
		runtimePendingReady = true;
		runtimeStopVoicesRequested = true;
		return true;
	}

	bool queueReplaceOneRuntimeSample(int inst, int layer, const std::string& wavPath) {
		std::string err;
		uint32_t sr = 0;
		std::vector<int16_t> pcm;
		if (!loadWavMonoPcm16(wavPath, pcm, sr, err)) {
			std::lock_guard<std::mutex> lock(runtimeMutex);
			runtimeActive.lastError = err;
			return false;
		}

		std::lock_guard<std::mutex> lock(runtimeMutex);
		runtimePending = runtimeActive;
		runtimePending.loaded = true;
		runtimePending.s[inst][layer].pcm = std::move(pcm);
		runtimePending.s[inst][layer].sampleRate = sr;
		runtimePending.s[inst][layer].loaded = true;
		runtimePendingReady = true;
		runtimeStopVoicesRequested = true;
		return true;
	}

	bool queueReloadRuntimeKit() {
		std::lock_guard<std::mutex> lock(runtimeMutex);
		if (runtimeActive.dirPath.empty()) {
			runtimeActive.lastError = "No kit loaded yet";
			return false;
		}
		RuntimeKit tmp;
		if (!loadRuntimeKitFromDir(tmp, runtimeActive.dirPath)) {
			runtimeActive.lastError = tmp.lastError.empty() ? "Failed to reload kit" : tmp.lastError;
			return false;
		}
		runtimePending = std::move(tmp);
		runtimePendingReady = true;
		runtimeStopVoicesRequested = true;
		return true;
	}

	void clearRuntimeKit() {
		std::lock_guard<std::mutex> lock(runtimeMutex);
		runtimeActive = RuntimeKit{};
		runtimePending = RuntimeKit{};
		runtimePendingReady = false;
		runtimeStopVoicesRequested = true;
	}
#endif

	Amenolith() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam<KitParamQuantity>(KIT_PARAM, 0.f, 4.f, 0.f, "Kit");
		getParamQuantity(KIT_PARAM)->snapEnabled = true;
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive");
		configParam(HUMANIZE_PARAM, 0.f, 1.f, 0.f, "Humanize");
		configSwitch(LAYER_SELECT_PARAM, 0.f, 2.f, 0.f, "Layer", {"1", "2", "3"});
		// Legacy global RR (no longer used)
		configSwitch(RR_MODE_PARAM, 0.f, 2.f, 0.f, "Round robin (legacy)", {"OFF", "EUC", "DENS"});
		configParam(ACCENT_PARAM, 0.f, 1.f, 1.f, "Accent amount");
		configSwitch(GEN_PITCH_PARAM, 0.f, 1.f, 0.f, "Generative pitch", {"Off", "On"});

		for (int i = 0; i < NUM_INST; ++i) {
			configSwitch(RR_MODE_PARAM_BASE + i, 0.f, 2.f, 0.f,
				std::string(instrumentName(i)) + " RR mode", {"OFF", "EUC", "DENS"});
		}

		for (int i = 0; i < NUM_INST; ++i) {
			configParam(LEVEL_PARAM_BASE + i, 0.f, 1.f, 0.8f, std::string(instrumentName(i)) + " level");
			float defaultPan = 0.f;
			switch (i) {
				case BD: defaultPan = 0.0f; break;
				case SN: defaultPan = -0.2f; break;
				case GSN: defaultPan = 0.2f; break;
				case CH: defaultPan = 0.35f; break;
				case OH: defaultPan = -0.35f; break;
				default: defaultPan = 0.f; break;
			}
			configParam(PAN_PARAM_BASE + i, -1.f, 1.f, defaultPan, std::string(instrumentName(i)) + " pan");
			configParam<SemitoneParamQuantity>(TUNE_PARAM_BASE + i, -3.f, 3.f, 0.f, std::string(instrumentName(i)) + " tune (semitones)");
			getParamQuantity(TUNE_PARAM_BASE + i)->snapEnabled = true;
			configParam(LENGTH_PARAM_BASE + i, 0.f, 1.f, 1.f, std::string(instrumentName(i)) + " length");
		}

		configInput(KIT_CV_INPUT, "Kit CV");
		configInput(ACCENT_CV_INPUT, "Accent CV");
		configInput(DRIVE_CV_INPUT, "Drive CV");

		for (int i = 0; i < NUM_INST; ++i) {
			configInput(TRIG_INPUT_BASE + i, std::string(instrumentName(i)) + " trig");
			configInput(VEL_INPUT_BASE + i, std::string(instrumentName(i)) + " velocity");
			configInput(TUNE_CV_INPUT_BASE + i, std::string(instrumentName(i)) + " tune CV (1V/oct)");
			configOutput(INST_OUT_BASE + i, std::string(instrumentName(i)) + " out");
		}

		configOutput(MIX_L_OUTPUT, "Mix L");
		configOutput(MIX_R_OUTPUT, "Mix R");

		for (int s = -TUNE_RANGE_SEMIS; s <= TUNE_RANGE_SEMIS; ++s) {
			semitoneRatio[s + TUNE_RANGE_SEMIS] = std::exp2f((float)s / 12.f);
		}

		// Initial pan state from params (updated at control rate in process())
		for (int i = 0; i < NUM_INST; ++i) {
			setPan(i, params[PAN_PARAM_BASE + i].getValue());
		}

		initKits();
	}

	void onReset() override {
		for (int i = 0; i < NUM_INST; ++i) {
			genPitch_[i] = GenPitchState{};
			pendingPitchSemis_[i] = 0.f;
			nextPitchSemis_[i] = 0.f;
			resetGenPitchPhrase(i);
		}
		lastQuantizedKit_ = -1;
	}

	void setPan(int inst, float pan) {
		// pan -1..+1, constant-power
		float angle = (pan * 0.5f + 0.5f) * (float)M_PI * 0.5f;
		v[inst].panL = std::cos(angle);
		v[inst].panR = std::sin(angle);
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
			case CH:  return 1.00f;
			case OH:  return 1.00f;
			default:  return 1.00f;
		}
	}

	inline void scheduleHumanizedTrigger(int inst, float engineSampleRate) {
		float hum = clampf(params[HUMANIZE_PARAM].getValue(), 0.f, 1.f);
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
			float driftBias = clampf(humanizeDrift[inst] * 0.35f * wild, -0.50f, 0.50f);
			jitterNorm += driftBias;
			jitterNorm = clampf(jitterNorm, -1.50f, 1.50f);
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
		pendingGainMult[inst] = std::pow(10.f, db / 20.f);
	}

	inline int quantizeKit(float knob0to4, float cvVolts0to10) {
		float cv = clampf(cvVolts0to10 / 10.f, 0.f, 1.f) * 4.f;
		float x = knob0to4 + cv;
		int k = (int)std::lround(x);
		if (k < 0) k = 0;
		if (k > 4) k = 4;
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

	void startVoice(int inst, const ProcessArgs& args, bool /*isRoll*/) {
		Voice& vc = v[inst];

		// Deferred kit switching per instrument
		if (!vc.active) {
			vc.currentKit = vc.pendingKit;
		}

		// Velocity behavior:
		// - If VEL is patched, it selects the sample layer (and also applies a small gain factor).
		// - If VEL is not patched, manual LYR and optional RR select the layer.
		bool velConnected = inputs[VEL_INPUT_BASE + inst].isConnected();
		float velNorm = velConnected ? clampf(inputs[VEL_INPUT_BASE + inst].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
		int baseLayer = (int)std::lround(params[LAYER_SELECT_PARAM].getValue());
		if (baseLayer < 0) baseLayer = 0;
		if (baseLayer > 2) baseLayer = 2;

		int layer = 0;
		if (velConnected) {
			layer = (velNorm < 0.33f) ? 0 : (velNorm < 0.66f ? 1 : 2);
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
		// Clamp base velocity behavior before applying global accent.
		velFactor = clampf(velFactor, 0.75f, 1.35f);

		// Accent CV (optional): punchy, non-linear (breakcore-friendly)
		// Audition mode: up to +350% at full Accent amount, with a cubic curve (gentle low, explosive high).
		if (inputs[ACCENT_CV_INPUT].isConnected()) {
			float acc = clampf(inputs[ACCENT_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			float accAmt = clampf(params[ACCENT_PARAM].getValue(), 0.f, 1.f);
			float curve = acc * acc * acc;
			velFactor *= 1.f + (3.5f * accAmt * curve);
		}
		// Apply humanize correlated gain (set by scheduler when the trigger fires).
		velFactor *= nextGainMult[inst];
		nextGainMult[inst] = 1.f;
		velFactor = clampf(velFactor, 0.65f, 4.50f);
		const Sample* sp = nullptr;
		Sample runtimeSample;

#ifndef METAMODULE
		// Runtime kit auditioning overrides KIT 1 (index 0) only.
		if (vc.currentKit == 0) {
			std::lock_guard<std::mutex> lock(runtimeMutex);
			if (runtimeActive.loaded && runtimeActive.s[inst][layer].loaded && !runtimeActive.s[inst][layer].pcm.empty()) {
				runtimeSample.data = runtimeActive.s[inst][layer].pcm.data();
				runtimeSample.frames = (uint32_t)runtimeActive.s[inst][layer].pcm.size();
				runtimeSample.sampleRate = runtimeActive.s[inst][layer].sampleRate;
				sp = &runtimeSample;
			}
		}
#endif
		if (!sp) {
			sp = &g_kits[vc.currentKit][inst][layer];
		}
		const Sample& s = *sp;

		vc.data = s.data;
		vc.frames = s.frames;
		vc.sampleRate = s.sampleRate;

		float len = clampf(params[LENGTH_PARAM_BASE + inst].getValue(), 0.f, 1.f);
		uint32_t endF = (uint32_t)std::lround((float)vc.frames * len);
		if (endF < 2) endF = 2;
		if (endF > vc.frames) endF = vc.frames;
		vc.endFrame = endF;

		bool genPitchEnabled = (params[GEN_PITCH_PARAM].getValue() > 0.5f);
		float semiKnob = params[TUNE_PARAM_BASE + inst].getValue();
		float semiCv = 0.f;
		if (!genPitchEnabled) {
			semiCv = inputs[TUNE_CV_INPUT_BASE + inst].isConnected() ? inputs[TUNE_CV_INPUT_BASE + inst].getVoltage() * 12.f : 0.f;
		}
		float genSemi = genPitchEnabled ? nextPitchSemis_[inst] : 0.f;
		nextPitchSemis_[inst] = 0.f;
		vc.phaseInc = calcPhaseIncSemis(semiKnob + semiCv + genSemi, args.sampleRate, (float)vc.sampleRate);

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
	}

	inline float nextSample(Voice& vc) {
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
		// Cheap end fade to avoid clicks when LEN truncates the sample.
		// Fade over a small fixed number of frames (~1ms @ 48kHz).
		{
			const uint32_t endFadeFrames = 48;
			uint32_t framesLeft = vc.endFrame - idx;
			if (framesLeft <= endFadeFrames + 1) {
				float k = (float)(framesLeft - 1) / (float)endFadeFrames;
				if (k < 0.f) k = 0.f;
				if (k > 1.f) k = 1.f;
				g *= k;
			}
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
			}
			lastQuantizedKit_ = targetKit;

			for (int i = 0; i < NUM_INST; ++i) {
				v[i].pendingKit = (uint8_t)targetKit;
				if (!v[i].active) {
					v[i].currentKit = v[i].pendingKit;
				}
				setPan(i, params[PAN_PARAM_BASE + i].getValue());
			}

			// (roll removed)

#ifndef METAMODULE
			// Apply pending runtime kit only when voices are stopped.
			// This avoids invalidating sample pointers while they're playing.
			if (runtimeStopVoicesRequested) {
				for (int i = 0; i < NUM_INST; ++i) {
					v[i].active = false;
					v[i].choking = false;
					v[i].fadeOut = 0;
				}
				runtimeStopVoicesRequested = false;
			}
			bool allIdle = true;
			for (int i = 0; i < NUM_INST; ++i) {
				if (v[i].active) { allIdle = false; break; }
			}
			if (allIdle && runtimePendingReady) {
				if (runtimeMutex.try_lock()) {
					runtimeActive = std::move(runtimePending);
					runtimePending = RuntimeKit{};
					runtimePendingReady = false;
					runtimeMutex.unlock();
				}
			}
#endif
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

				if (genPitchEnabled) {
					// Compute generative pitch at trigger time (deterministic for identical trigger patterns).
					float velNorm = 0.f;
					bool velConnected = inputs[VEL_INPUT_BASE + i].isConnected();
					if (velConnected) {
						velNorm = clampf(inputs[VEL_INPUT_BASE + i].getVoltage() / 10.f, 0.f, 1.f);
					}
					bool accent = (velConnected && velNorm >= 0.82f);
					if (inputs[ACCENT_CV_INPUT].isConnected()) {
						float acc = clampf(inputs[ACCENT_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
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

		for (int i = 0; i < NUM_INST; ++i) {
			Voice& vc = v[i];
			float s = nextSample(vc);

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

		outputs[MIX_L_OUTPUT].setVoltage(5.f * mixL);
		outputs[MIX_R_OUTPUT].setVoltage(5.f * mixR);
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
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Amenolith.svg")));

		#ifndef METAMODULE
		// Optional PNG faceplate (when provided). Keep SVG for correct sizing.
		auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Amenolith.png"));
		panelBg->box.pos = Vec(0, 0);
		panelBg->box.size = box.size;
		addChild(panelBg);
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
		#endif
		#ifdef METAMODULE
		using AmenolithPort = MVXPort;
		#endif

		// Shift everything down by 10px (~2.65mm) to make room for the headline.
		const float yOff = 2.65f;
		const Vec switchShiftPx = Vec(0.f, 3.f);
		const Vec panelShiftPx = Vec(0.f, 13.f);
		auto mm = [&](Vec v) {
			return mm2px(v).plus(panelShiftPx);
		};

		#ifndef METAMODULE
		auto addLabel = [&](BCPanelLabel* label) {
			label->box.pos = label->box.pos.plus(panelShiftPx);
			addChild(label);
		};
		#endif

		// --- Global row ---
		addParam(createParamCentered<RoundLargeBlackKnob>(mm(Vec(14.f, 20.f + yOff)), module, Amenolith::KIT_PARAM));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(28.f, 20.f + yOff)), module, Amenolith::KIT_CV_INPUT));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(42.f, 20.f + yOff)), module, Amenolith::ACCENT_CV_INPUT));
		addInput(createInputCentered<AmenolithPort>(mm(Vec(56.f, 20.f + yOff)), module, Amenolith::DRIVE_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm(Vec(66.f, 20.f + yOff)), module, Amenolith::DRIVE_PARAM));
		addParam(createParamCentered<Trimpot>(mm(Vec(76.f, 20.f + yOff)), module, Amenolith::HUMANIZE_PARAM));
		addParam(createParamCentered<AmenolithCKSSThree>(mm(Vec(86.f, 20.f + yOff)).plus(switchShiftPx), module, Amenolith::LAYER_SELECT_PARAM));
		addParam(createParamCentered<AmenolithCKSS>(mm(Vec(94.f, 20.f + yOff)).plus(switchShiftPx), module, Amenolith::GEN_PITCH_PARAM));
		addOutput(createOutputCentered<AmenolithPort>(mm(Vec(102.f, 20.f + yOff)), module, Amenolith::MIX_L_OUTPUT));
		addOutput(createOutputCentered<AmenolithPort>(mm(Vec(112.f, 20.f + yOff)), module, Amenolith::MIX_R_OUTPUT));

		#ifndef METAMODULE
		// KIT label 3px higher (~0.79mm)
		addLabel(bcCreateLabel(Vec(14.f, 13.f + yOff - 0.79f), "KIT", 8.f, neonGreen));
		addLabel(bcCreateLabel(Vec(28.f, 13.f + yOff), "KIT CV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(42.f, 13.f + yOff), "ACC", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(56.f, 13.f + yOff), "DRV CV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(66.f, 13.f + yOff), "DRV", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(76.f, 13.f + yOff), "HUM", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(86.f, 13.f + yOff), "LAYER", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(94.f, 13.f + yOff), "PITCH", 8.f, dimGreen));
		addLabel(bcCreateLabel(Vec(102.f, 13.f + yOff), "MIX L", 8.f, neonGreen));
		addLabel(bcCreateLabel(Vec(112.f, 13.f + yOff), "MIX R", 8.f, neonGreen));

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
		static const char* rowNames[NUM_INST] = {"BD", "SN", "SN2", "CH", "OH"};
		#endif
		for (int i = 0; i < NUM_INST; ++i) {
			float yy = rowY0 + rowDy * i;

			#ifndef METAMODULE
			auto* nameLbl = bcCreateLabel(Vec(xName, yy), rowNames[i], 10.f, neonGreen, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			addLabel(nameLbl);
			nameLbl->box.pos.x += 2.f;
			#endif

			addInput(createInputCentered<AmenolithPort>(mm(Vec(xTrig, yy)), module, Amenolith::TRIG_INPUT_BASE + i));
			// Per-voice RR mode switch (unlabeled), beside TRIG input
			addParam(createParamCentered<AmenolithRRModeSwitch>(mm(Vec(xRR, yy)), module, Amenolith::RR_MODE_PARAM_BASE + i));
			addInput(createInputCentered<AmenolithPort>(mm(Vec(xVel, yy)), module, Amenolith::VEL_INPUT_BASE + i));
			addInput(createInputCentered<AmenolithPort>(mm(Vec(xTune, yy)), module, Amenolith::TUNE_CV_INPUT_BASE + i));

			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xPan, yy)), module, Amenolith::PAN_PARAM_BASE + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xLvl, yy)), module, Amenolith::LEVEL_PARAM_BASE + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm(Vec(xSemi, yy)), module, Amenolith::TUNE_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm(Vec(xLen, yy)), module, Amenolith::LENGTH_PARAM_BASE + i));

			addOutput(createOutputCentered<AmenolithPort>(mm(Vec(xOut, yy)), module, Amenolith::INST_OUT_BASE + i));
		}
	}

#ifndef METAMODULE
	void step() override {
		ModuleWidget::step();
		Amenolith* mod = dynamic_cast<Amenolith*>(this->module);
		if (mod && mod->autoLoadKit01Requested) {
			mod->autoLoadKit01Requested = false;
			mod->queueLoadRuntimeKitFromDir(asset::plugin(pluginInstance, "drums_src/BreakcoreDrums/Kit01"));
		}
	}
#endif

	void appendContextMenu(Menu* menu) override {
		Amenolith* module = dynamic_cast<Amenolith*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Amenolith (VCV)"));

#ifndef METAMODULE
		menu->addChild(createMenuItem("Load Kit01 folder (audition → KIT 1)", "",
			[=]() {
				// Select the Kit01 directory itself (loads all samples from that folder).
				char* dirPath = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
				if (!dirPath) return;
				module->queueLoadRuntimeKitFromDir(std::string(dirPath));
				free(dirPath);
			}));

		menu->addChild(createMenuItem("Reload last audition kit", "",
			[=]() {
				module->queueReloadRuntimeKit();
			}));

		menu->addChild(createMenuItem("Clear audition kit (use built-in)", "",
			[=]() {
				module->clearRuntimeKit();
			}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Replace one sample (audition kit)"));

		auto replaceOne = [=](int inst, int layer, const std::string& label) {
			menu->addChild(createMenuItem(label, "",
				[=]() {
					char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, osdialog_filters_parse("WAV:wav,WAV"));
					if (!path) return;
					module->queueReplaceOneRuntimeSample(inst, layer, std::string(path));
					free(path);
				}));
		};

		replaceOne(BD, 0, "BD layer 1 (soft)...");
		replaceOne(BD, 1, "BD layer 2 (med)...");
		replaceOne(BD, 2, "BD layer 3 (hard)...");
		replaceOne(SN, 0, "SN layer 1 (soft)...");
		replaceOne(SN, 1, "SN layer 2 (med)...");
		replaceOne(SN, 2, "SN layer 3 (hard)...");
		replaceOne(GSN, 0, "SN2 layer 1 (soft)...");
		replaceOne(GSN, 1, "SN2 layer 2 (med)...");
		replaceOne(GSN, 2, "SN2 layer 3 (hard)...");
		replaceOne(CH, 0, "CH layer 1 (soft)...");
		replaceOne(CH, 1, "CH layer 2 (med)...");
		replaceOne(CH, 2, "CH layer 3 (hard)...");
		replaceOne(OH, 0, "OH layer 1 (soft)...");
		replaceOne(OH, 1, "OH layer 2 (med)...");
		replaceOne(OH, 2, "OH layer 3 (hard)...");

		if (!module->runtimeActive.dirPath.empty()) {
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuLabel("Audition dir: " + module->runtimeActive.dirPath));
		}
		if (!module->runtimeActive.lastError.empty()) {
			menu->addChild(createMenuLabel("Last error: " + module->runtimeActive.lastError));
		}
#else
		menu->addChild(createMenuLabel("Audition loader disabled on MetaModule"));
#endif
	}
};
Model* modelAmenolith = createModel<Amenolith, AmenolithWidget>("Amenolith");
