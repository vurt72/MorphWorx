#include "plugin.hpp"
#include "dsp/LookupTables.hpp"
#include "dsp/FerroReverb.hpp"
#include "ui/PngPanelBackground.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int kNumVoices = 8;
enum class FerroklastVariant {
	Full,
	MetaModule,
};
constexpr int kHatOscCount = 6;
constexpr int kControlRate = 16;
constexpr float kTwoPi = 6.28318530718f;
constexpr float kMaterialMacroMax = 2.f;
constexpr float kTransientMacroMax = 3.f;

static const char* kVoiceNames[kNumVoices] = {
	"Kick",
	"Snare 1",
	"Snare 2",
	"CH",
	"OH",
	"Ride",
	"Clap",
	"Rim",
};

static const char* kVoiceShortNames[kNumVoices] = {
	"KICK",
	"SN1",
	"SN2",
	"CH",
	"OH",
	"RIDE",
	"CLAP",
	"RIM",
};

static const float kDefaultLevels[kNumVoices] = {1.0f, 0.88f, 0.76f, 0.56f, 0.62f, 0.58f, 0.60f, 0.54f};
static const float kDefaultDecays[kNumVoices] = {0.42f, 0.30f, 0.34f, 0.08f, 0.44f, 0.55f, 0.34f, 0.18f};
static const float kDefaultTunes[kNumVoices] = {0.f, 0.f, 0.f, 0.8f, 0.4f, 0.f, 0.f, 0.f};
static const float kDefaultVariations[kNumVoices] = {0.50f, 0.50f, 0.50f, 0.43f, 0.47f, 0.30f, 0.52f, 0.48f};
// Lower neutral hat cluster so CH/OH sit closer to a 909-style register.
static const float kHatBaseFreqs[kHatOscCount] = {620.f, 820.f, 1070.f, 1430.f, 1860.f, 2460.f};
static const float kRideBaseFreqs[kHatOscCount] = {1496.3f, 2707.8f, 3708.4f, 6184.8f, 7505.0f, 9120.0f};
static const float kHatOscGains[kHatOscCount] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
static const float kRideOscGains[kHatOscCount] = {0.58f, 0.44f, 1.22f, 0.92f, 1.10f, 0.74f};

// Per-voice transient defaults: Kick, Sn1, Sn2, CH, OH, Ride, Clap, Rim
static const float kDefaultTransAmounts[kNumVoices] = {0.60f, 0.75f, 0.65f, 0.32f, 0.24f, 0.28f, 0.44f, 0.52f};
static const float kDefaultTransTypes[kNumVoices] = {0.42f, 0.08f, 0.12f, 0.1667f, 0.1458f, 0.1875f, 0.24f, 0.10f};
static const float kDefaultTransDecays[kNumVoices] = {0.30f, 0.50f, 0.45f, 0.10f, 0.18f, 0.11f, 0.22f, 0.16f};

enum FilterMode {
	FILTER_LP = 0,
	FILTER_BP = 1,
	FILTER_HP = 2,
};

enum MachineId {
	MACHINE_KICK = 0,
	MACHINE_SNARE1,
	MACHINE_SNARE2,
	MACHINE_METAL,
	MACHINE_CHAOS1,
	MACHINE_RIDE,
	MACHINE_CLAP,
	MACHINE_RIM,
};

struct MachineProfile {
	MachineId machine;
	FilterMode filterMode;
	float baseFreqMin;
	float baseFreqMax;
	float pitchSweep;
	float modIndexBase;
	float modIndexPunch;
	float modIndexAccent;
	float noiseSensitivity;
	float feedbackMax;
	float instabilityMax;
	float clickSensitivity;
	float ampDecayMin;
	float ampDecayMax;
	float noiseDecayMin;
	float noiseDecayMax;
	float pitchDecayMin;
	float pitchDecayMax;
	float modDecayMin;
	float modDecayMax;
	float filterMin;
	float filterMax;
	float resonance;
	float drive;
	float ratioChoices[5];

	MachineProfile(
		MachineId machine,
		FilterMode filterMode,
		float baseFreqMin,
		float baseFreqMax,
		float pitchSweep,
		float modIndexBase,
		float modIndexPunch,
		float modIndexAccent,
		float noiseSensitivity,
		float feedbackMax,
		float instabilityMax,
		float clickSensitivity,
		float ampDecayMin,
		float ampDecayMax,
		float noiseDecayMin,
		float noiseDecayMax,
		float pitchDecayMin,
		float pitchDecayMax,
		float modDecayMin,
		float modDecayMax,
		float filterMin,
		float filterMax,
		float resonance,
		float drive,
		const float (&ratios)[5])
		: machine(machine)
		, filterMode(filterMode)
		, baseFreqMin(baseFreqMin)
		, baseFreqMax(baseFreqMax)
		, pitchSweep(pitchSweep)
		, modIndexBase(modIndexBase)
		, modIndexPunch(modIndexPunch)
		, modIndexAccent(modIndexAccent)
		, noiseSensitivity(noiseSensitivity)
		, feedbackMax(feedbackMax)
		, instabilityMax(instabilityMax)
		, clickSensitivity(clickSensitivity)
		, ampDecayMin(ampDecayMin)
		, ampDecayMax(ampDecayMax)
		, noiseDecayMin(noiseDecayMin)
		, noiseDecayMax(noiseDecayMax)
		, pitchDecayMin(pitchDecayMin)
		, pitchDecayMax(pitchDecayMax)
		, modDecayMin(modDecayMin)
		, modDecayMax(modDecayMax)
		, filterMin(filterMin)
		, filterMax(filterMax)
		, resonance(resonance)
		, drive(drive) {
		for (int i = 0; i < 5; ++i) {
			ratioChoices[i] = ratios[i];
		}
	}
};

static const MachineProfile kProfiles[kNumVoices] = {
	{MACHINE_KICK, FILTER_LP, 38.f, 74.f, 4.8f, 0.24f, 2.8f, 0.32f, 0.05f, 0.08f, 0.02f, 0.48f, 0.001f, 0.80f, 0.03f, 0.10f, 0.010f, 0.055f, 0.025f, 0.16f, 90.f, 3200.f, 0.16f, 0.30f, {0.5f, 1.f, 1.4142f, 1.99f, 2.f}},
	{MACHINE_SNARE1, FILTER_BP, 150.f, 260.f, 0.70f, 0.72f, 2.1f, 0.38f, 0.72f, 0.18f, 0.05f, 0.58f, 0.002f, 0.44f, 0.08f, 0.45f, 0.009f, 0.055f, 0.030f, 0.18f, 700.f, 6400.f, 0.52f, 0.48f, {1.f, 1.4142f, 1.99f, 2.7183f, 3.f}},
	{MACHINE_SNARE2, FILTER_HP, 180.f, 330.f, 0.45f, 0.95f, 2.7f, 0.45f, 0.90f, 0.26f, 0.08f, 0.66f, 0.002f, 0.32f, 0.06f, 0.28f, 0.008f, 0.035f, 0.020f, 0.12f, 1200.f, 9000.f, 0.28f, 0.72f, {1.f, 1.4142f, 1.99f, 2.7183f, 3.1416f}},
	{MACHINE_METAL, FILTER_HP, 340.f, 1400.f, 0.18f, 1.4f, 3.3f, 0.30f, 0.28f, 0.38f, 0.07f, 0.52f, 0.002f, 0.18f, 0.02f, 0.12f, 0.004f, 0.020f, 0.020f, 0.10f, 2200.f, 11000.f, 0.22f, 0.86f, {1.4142f, 1.99f, 2.7183f, 3.f, 4.1f}},
	{MACHINE_CHAOS1, FILTER_BP, 100.f, 1500.f, 0.90f, 1.7f, 4.7f, 0.55f, 0.84f, 0.66f, 0.24f, 0.58f, 0.002f, 0.30f, 0.03f, 0.20f, 0.004f, 0.065f, 0.018f, 0.11f, 450.f, 10000.f, 0.34f, 0.98f, {1.f, 1.4142f, 1.99f, 2.7183f, 4.2361f}},
	{MACHINE_RIDE, FILTER_HP, 280.f, 600.f, 0.30f, 0.6f, 1.8f, 0.25f, 0.15f, 0.26f, 0.08f, 0.48f, 0.002f, 0.70f, 0.020f, 0.12f, 0.003f, 0.050f, 0.015f, 0.09f, 900.f, 12000.f, 0.20f, 0.38f, {0.5f, 1.99f, 2.7183f, 3.1416f, 4.6692f}},
	{MACHINE_CLAP, FILTER_HP, 180.f, 520.f, 0.20f, 0.25f, 0.80f, 0.22f, 0.20f, 0.10f, 0.04f, 0.78f, 0.010f, 0.50f, 0.040f, 0.35f, 0.010f, 0.090f, 0.010f, 0.120f, 700.f, 12000.f, 0.12f, 0.24f, {1.f, 1.25f, 1.5f, 2.f, 3.f}},
	{MACHINE_RIM, FILTER_HP, 320.f, 1200.f, 0.35f, 0.18f, 0.60f, 0.18f, 0.14f, 0.08f, 0.02f, 0.84f, 0.004f, 0.22f, 0.020f, 0.18f, 0.004f, 0.040f, 0.008f, 0.060f, 1200.f, 12000.f, 0.10f, 0.22f, {1.f, 1.4142f, 2.f, 2.5f, 3.f}},
};

static inline float clamp01(float x) {
	if (x < 0.f) return 0.f;
	if (x > 1.f) return 1.f;
	return x;
}

static inline float lerp(float a, float b, float t) {
	return a + (b - a) * t;
}

static inline float smoothstep01(float x) {
	x = clamp01(x);
	return x * x * (3.f - 2.f * x);
}

static inline float onePoleCoef(float cutoffHz, float sampleRate) {
	float safeCutoff = rack::math::clamp(cutoffHz, 5.f, sampleRate * 0.45f);
	return rack::math::clamp(safeCutoff / (safeCutoff + sampleRate), 0.0001f, 0.5f);
}

static inline float remapChaosMacro(float knob01) {
	float x = clamp01(knob01);
	if (x <= 0.18f) {
		float t = smoothstep01(x * (1.f / 0.18f));
		return lerp(0.f, 0.36f, t);
	}
	if (x <= 0.70f) {
		float t = smoothstep01((x - 0.18f) * (1.f / 0.52f));
		return lerp(0.36f, 0.69f, t);
	}
	float t = smoothstep01((x - 0.70f) * (1.f / 0.30f));
	return lerp(0.69f, 1.f, t);
}

static inline float softClip(float x, float drive) {
	float shapedDrive = 1.f + clamp01(drive) * 5.f;
	float y = x * shapedDrive;
	float ay = std::fabs(y);
	return y / (1.f + ay);
}

static inline float amenolithDriveClip(float x, float drive01) {
	drive01 = clamp01(drive01);

	float k = 1.f + drive01 * 6.f;
	float k2 = 1.f;

	if (drive01 > 0.90f) {
		float t = (drive01 - 0.90f) / 0.10f;
		float t2 = t * t;
		float t4 = t2 * t2;
		k += 50.f * t4;
		k2 = 1.f + 10.f * t;
	}

	auto shape = [&](float in) {
		float y = in * k;
		float ay = std::fabs(y);
		float out = y / (1.f + ay);
		if (drive01 > 0.90f) {
			float z = out * k2;
			float az = std::fabs(z);
			out = z / (1.f + az);
		}
		return out;
	};

	float out = shape(x);
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

// Centralized kick-rumble tuning so level/offset can be adjusted without
// retouching the main kick path logic.
constexpr float kRumbleMaskStart = 0.20f;
constexpr float kRumbleMaskSpan = 0.15f;
constexpr float kRumbleTransientGate = 0.0015f;
constexpr float kRumbleAmpGate = 0.20f;
constexpr float kRumbleDecayGate = 0.028f;
constexpr float kRumbleLevelBoost = 1.25f;
constexpr float kRumbleDriveGainBase = 1.00f;
constexpr float kRumbleDriveGainFromDrive = 1.60f;
constexpr float kRumbleDriveGainFromColor = 0.35f;
constexpr float kRumbleDriveAmountFromDrive = 0.85f;
constexpr float kRumbleDriveAmountFromChaos = 0.12f;

static inline float wrap01(float phase) {
	// Use integer truncation → maps to a single VCVT instruction on ARM M7
	// (std::floor invokes libm on many ARM toolchains, costing ~10-20 extra cycles).
	phase -= (float)(int)phase;
	if (phase < 0.f) phase += 1.f;
	return phase;
}

static inline float semitoneRatio(float semitones) {
	return std::exp2(semitones / 12.f);
}

static inline float decayCoef(float timeSeconds, float sampleRate) {
	float safeTime = std::max(0.001f, timeSeconds);
	float samples = std::max(1.f, safeTime * sampleRate);
	return std::exp(-1.f / samples);
}

static inline float moderateKickChaosBitDepth(float levels) {
	if (levels >= 255.f) {
		return levels;
	}
	return std::max(8.f, levels * 0.40f);
}

static inline int moderateKickChaosDecimatePeriod(int period) {
	if (period <= 1) {
		return period;
	}
	return 1 + (period - 1) * 3;
}

static inline float computeKickChaosBytebeatMix(float mix, float ampEnv) {
	if (mix <= 0.001f) {
		return 0.f;
	}
	float decayPos = 1.f - ampEnv;
	float tailBlend = rack::math::clamp((decayPos - 0.18f) * (1.f / 0.60f), 0.f, 1.f);
	tailBlend = tailBlend * (1.f - 0.35f * tailBlend);
	return rack::math::clamp(mix * 0.82f, 0.f, 0.58f) * tailBlend;
}

#ifndef METAMODULE
struct KickRumbleSmear {
	static constexpr int kBufferSize = 8192;
	static constexpr int kBufferMask = kBufferSize - 1;

	float buffer[kBufferSize] = {};
	int writeIndex = 0;
	float smearState = 0.f;
	float subState = 0.f;
	float bodyState = 0.f;
	float output = 0.f;
	float mix = 0.f;
	float sendGain = 0.f;
	float feedback = 0.6f;
	float smearCoef = 0.16f;
	float subCoef = 0.01f;
	float bodyCoef = 0.02f;
	float cleanup = 0.2f;
	float duckDepth = 0.3f;
	float inputDrive = 0.18f;
	float outputDrive = 0.22f;
	int delayA = 900;
	int delayB = 2200;

	void reset() {
		std::fill(buffer, buffer + kBufferSize, 0.f);
		writeIndex = 0;
		smearState = 0.f;
		subState = 0.f;
		bodyState = 0.f;
		output = 0.f;
	}

	void configure(float sampleRate, float rumble, float material, float chaos, float body, float color) {
		if (rumble <= 0.001f) {
			reset();
			mix = 0.f;
			sendGain = 0.f;
			feedback = 0.f;
			inputDrive = 0.f;
			outputDrive = 0.f;
			return;
		}

		float rumbleOpen = smoothstep01((rumble - 0.02f) * (1.f / 0.22f));
		float rumbleDriveStage = smoothstep01((rumble - 0.40f) * (1.f / 0.60f));
		float smearStage = smoothstep01((rumble - 0.82f) * (1.f / 0.18f));
		float smearExtreme = smoothstep01((rumble - 0.92f) * (1.f / 0.08f));
		mix = rack::math::clamp(0.08f * smearStage + 0.34f * smearStage * smearStage + 0.42f * smearExtreme, 0.f, 1.f);
		sendGain = 0.02f * smearStage + 0.10f * smearStage * smearStage + 0.16f * smearExtreme;
		feedback = rack::math::clamp(0.42f + 0.08f * material + 0.10f * smearStage + 0.16f * smearExtreme, 0.30f, 0.86f);
		smearCoef = rack::math::clamp(0.05f + 0.06f * rumbleOpen + 0.08f * smearStage + 0.08f * smearExtreme, 0.02f, 0.40f);
		subCoef = onePoleCoef(lerp(138.f, 78.f, smearStage + smearExtreme * 0.35f), sampleRate);
		bodyCoef = onePoleCoef(lerp(280.f, 150.f, smearStage + smearExtreme * 0.35f), sampleRate);
		cleanup = 0.12f + 0.18f * smearStage + 0.20f * smearExtreme;
		duckDepth = 0.34f + 0.12f * smearStage + 0.12f * smearExtreme;
		inputDrive = rack::math::clamp(0.04f + 0.08f * rumbleDriveStage + 0.18f * chaos + 0.12f * smearStage + 0.18f * smearExtreme, 0.f, 0.9f);
		outputDrive = rack::math::clamp(0.03f + 0.08f * body + 0.05f * color + 0.14f * smearStage + 0.22f * smearExtreme, 0.f, 0.9f);
		delayA = rack::math::clamp((int)std::round(sampleRate * (0.010f + 0.010f * smearStage + 0.012f * smearExtreme)), 64, kBufferSize - 2);
		delayB = rack::math::clamp((int)std::round(sampleRate * (0.022f + 0.024f * smearStage + 0.028f * smearExtreme)), delayA + 1, kBufferSize - 1);
	}

	float process(float input, float kickEnv) {
		if (mix <= 1e-4f && std::fabs(output) < 1e-5f && std::fabs(smearState) < 1e-5f) {
			return 0.f;
		}

		float delayedA = buffer[(writeIndex + kBufferSize - delayA) & kBufferMask];
		float delayedB = buffer[(writeIndex + kBufferSize - delayB) & kBufferMask];
		float excite = softClip(input * sendGain + delayedA * feedback + delayedB * (0.16f + 0.18f * mix), inputDrive);
		smearState += smearCoef * (excite - smearState);
		float writeSample = softClip(smearState, inputDrive * 0.60f + 0.08f);
		buffer[writeIndex] = writeSample + 1e-18f;
		writeIndex = (writeIndex + 1) & kBufferMask;

		float tank = delayedA * (0.76f + 0.12f * mix) + delayedB * (0.22f + 0.18f * mix) + writeSample * 0.28f;
		subState += subCoef * (tank - subState);
		bodyState += bodyCoef * (tank - bodyState);
		float filtered = subState + (bodyState - subState) * (1.f - cleanup);

		kickEnv = clamp01(kickEnv);
		float duckShape = kickEnv * kickEnv * (1.25f - 0.25f * kickEnv);
		float duckGain = 1.f - duckDepth * duckShape;
		float wet = softClip(filtered * (1.20f + 1.50f * mix), outputDrive) * duckGain;
		output += (0.005f + 0.015f * mix) * (wet - output);

		if (!std::isfinite(output) || !std::isfinite(smearState) || !std::isfinite(subState) || !std::isfinite(bodyState)) {
			reset();
			return 0.f;
		}
		if (std::fabs(output) < 1e-15f) output = 0.f;
		if (std::fabs(smearState) < 1e-15f) smearState = 0.f;
		if (std::fabs(subState) < 1e-15f) subState = 0.f;
		if (std::fabs(bodyState) < 1e-15f) bodyState = 0.f;
		return output * mix;
	}
};
#endif

static bool loadRawFloatFile(const std::string& path, std::vector<float>& outData) {
	FILE* file = std::fopen(path.c_str(), "rb");
	if (!file) {
		return false;
	}
	if (std::fseek(file, 0, SEEK_END) != 0) {
		std::fclose(file);
		return false;
	}
	long size = std::ftell(file);
	if (size <= 0 || (size % (long)sizeof(float)) != 0) {
		std::fclose(file);
		return false;
	}
	std::rewind(file);
	outData.resize((size_t)size / sizeof(float));
	size_t readCount = std::fread(outData.data(), sizeof(float), outData.size(), file);
	std::fclose(file);
	if (readCount != outData.size()) {
		outData.clear();
		return false;
	}
	for (float& sample : outData) {
		if (!std::isfinite(sample)) {
			sample = 0.f;
		}
	}
	return outData.size() >= 2;
}

static std::string tr909AssetPath(const char* fileName) {
#ifdef METAMODULE
	return asset::plugin(pluginInstance, std::string("samples/tr909/") + fileName);
#else
	return asset::plugin(pluginInstance, std::string("res/samples/tr909/") + fileName);
#endif
}

static std::string ferroklastMMAssetPath(const char* fileName) {
#ifdef METAMODULE
	return asset::plugin(pluginInstance, std::string("samples/ferroklastmm/") + fileName);
#else
	return asset::plugin(pluginInstance, std::string("res/samples/ferroklastmm/") + fileName);
#endif
}

struct TR909OneShotSample {
	std::string assetPath;
	const char* displayName;
	std::vector<float> data;
	uint32_t sampleRate = 44100;
	bool loadAttempted = false;

	TR909OneShotSample(const std::string& assetPath, const char* displayName)
		: assetPath(assetPath)
		, displayName(displayName) {
	}

	bool ensureLoaded() {
		if (loadAttempted) {
			return data.size() >= 2;
		}
		loadAttempted = true;

		if (!loadRawFloatFile(assetPath, data)) {
			WARN("Ferroklast: could not open TR-909 %s sample at %s", displayName, assetPath.c_str());
			return false;
		}
		INFO("Ferroklast: loaded TR-909 %s sample (%u frames)", displayName, (unsigned)data.size());
		return data.size() >= 2;
	}
};

struct TR909KickSamples {
	std::vector<float> attack;
	std::vector<float> cycle;
	uint32_t sampleRate = 44100;
	bool loadAttempted = false;

	bool ensureLoaded() {
		if (loadAttempted) {
			return attack.size() >= 2 && cycle.size() >= 2;
		}
		loadAttempted = true;

		std::string attackPath = tr909AssetPath("bassdrum-attack.raw");
		std::string cyclePath = tr909AssetPath("bassdrum-cycle.raw");
		if (!loadRawFloatFile(attackPath, attack) || !loadRawFloatFile(cyclePath, cycle)) {
			attack.clear();
			cycle.clear();
			WARN("Ferroklast: could not load TR-909 kick samples at %s and %s", attackPath.c_str(), cyclePath.c_str());
			return false;
		}
		INFO("Ferroklast: loaded TR-909 kick samples (attack %u, cycle %u)", (unsigned)attack.size(), (unsigned)cycle.size());
		return true;
	}
};

static TR909OneShotSample& getTR909RideSample() {
	static TR909OneShotSample sample(tr909AssetPath("ride.raw"), "ride");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getTR909ClapSample() {
	static TR909OneShotSample sample(tr909AssetPath("clap.raw"), "clap");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getTR909RimSample() {
	static TR909OneShotSample sample(tr909AssetPath("rim.raw"), "rim");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getMMClosedHatSoftSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CH_1.raw"), "mm closed hat soft");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getMMClosedHatMediumSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CH_2.raw"), "mm closed hat medium");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getMMClosedHatHardSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CH_3.raw"), "mm closed hat hard");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample* getMMClosedHatSample(int hitIndex) {
	switch (rack::math::clamp(hitIndex, 0, 2)) {
		case 0: return &getMMClosedHatSoftSample();
		case 1: return &getMMClosedHatMediumSample();
		default: return &getMMClosedHatHardSample();
	}
}

static TR909OneShotSample& getMMClapSoftSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CLAP_1.raw"), "mm clap soft");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getMMClapMediumSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CLAP_2.raw"), "mm clap medium");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample& getMMClapHardSample() {
	static TR909OneShotSample sample(ferroklastMMAssetPath("CLAP_3.raw"), "mm clap hard");
	sample.ensureLoaded();
	return sample;
}

static TR909OneShotSample* getMMClapSample(int hitIndex) {
	switch (rack::math::clamp(hitIndex, 0, 2)) {
		case 0: return &getMMClapSoftSample();
		case 1: return &getMMClapMediumSample();
		default: return &getMMClapHardSample();
	}
}

static int selectMMClosedHatHitIndex(float variation, float accent) {
	// Accent defaults to 0.65 when no cable is connected, so centering around that
	// keeps VARI as the primary selector while a hot accent CV pushes toward harder hits.
	float selector = clamp01(variation + (accent - 0.65f) * 1.25f);
	if (selector < (1.f / 3.f)) return 0;
	if (selector < (2.f / 3.f)) return 1;
	return 2;
}

static int selectMMClapHitIndex(float variation) {
	return std::min((int)(clamp01(variation) * 3.f), 2);
}

static TR909KickSamples& getTR909KickSamples() {
	static TR909KickSamples samples;
	samples.ensureLoaded();
	return samples;
}

static TR909OneShotSample* getTR909OneShotSample(MachineId machine) {
	switch (machine) {
		case MACHINE_RIDE: return &getTR909RideSample();
		case MACHINE_CLAP: return &getTR909ClapSample();
		case MACHINE_RIM: return &getTR909RimSample();
		default: return nullptr;
	}
}

static inline uint32_t xorshift32(uint32_t& state) {
	uint32_t x = state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return x;
}

static inline float randomBipolar(uint32_t& state) {
	uint32_t x = xorshift32(state);
	return ((float)((x >> 8) & 0x00FFFFFFu) * (1.f / 8388607.5f)) - 1.f;
}

static inline float sampleSine(float phase) {
	return pwmt::g_sineTable.samplePhase(wrap01(phase));
}

static inline float sampleSquare(float phase) {
	return wrap01(phase) < 0.5f ? 1.f : -1.f;
}

static inline float selectRatio(const MachineProfile& profile, float color) {
	float scaled = clamp01(color) * 4.f;
	int index = (int)scaled;
	if (index >= 4) index = 3;
	float frac = scaled - (float)index;
	return lerp(profile.ratioChoices[index], profile.ratioChoices[index + 1], frac);
}

struct SimpleSvf {
	float low = 0.f;
	float band = 0.f;
	// Cached coefficients — set once per trigger via setCoeffs() to avoid
	// recomputing (kTwoPi * cutoff / sampleRate) every sample on ARM.
	float f_ = 0.2f;
	float q_ = 0.5f;

	void reset() {
		low = 0.f;
		band = 0.f;
	}

	// Call once per trigger (not per sample) after cutoff/resonance are finalised.
	void setCoeffs(float cutoffHz, float resonance, float sampleRate) {
		float safeCutoff = rack::math::clamp(cutoffHz, 20.f, sampleRate * 0.45f);
		f_ = rack::math::clamp((kTwoPi * safeCutoff) / sampleRate, 0.005f, 0.95f);
		q_ = rack::math::clamp(resonance, 0.05f, 1.8f);
	}

	float process(float input, float cutoffHz, float resonance, float sampleRate, FilterMode mode) {
		// Per-sample hot path: use cached coefficients, no division needed.
		(void)cutoffHz; (void)resonance; (void)sampleRate;

		low += f_ * band;
		float high = input - low - q_ * band;
		band += f_ * high;

		if (!std::isfinite(low) || !std::isfinite(band)) {
			reset();
			return 0.f;
		}
		if (std::fabs(low) < 1e-15f) low = 0.f;
		if (std::fabs(band) < 1e-15f) band = 0.f;

		switch (mode) {
			case FILTER_HP:
				return high;
			case FILTER_BP:
				return band;
			case FILTER_LP:
			default:
				return low;
		}
	}
};

struct DcBlocker {
	float prevInput = 0.f;
	float prevOutput = 0.f;

	void reset() {
		prevInput = 0.f;
		prevOutput = 0.f;
	}

	float process(float input) {
		float output = input - prevInput + 0.995f * prevOutput;
		if (!std::isfinite(output)) {
			reset();
			return 0.f;
		}
		prevInput = input;
		prevOutput = output;
		if (std::fabs(prevOutput) < 1e-15f) prevOutput = 0.f;
		return output;
	}
};

// Number of transient click types available for continuous sweep
constexpr int kNumTransientTypes = 13;

struct LaneState {
	dsp::SchmittTrigger trigger;
	SimpleSvf filter;
	DcBlocker dcBlock;
	uint32_t noiseState = 0x12345678u;
	MachineId machine = MACHINE_KICK;
	bool active = false;
	float carrierPhase = 0.f;
	float modPhase = 0.f;
	float subPhase = 0.f;
	float shellPhaseA = 0.f;
	float shellPhaseB = 0.f;
	float prevModOutput = 0.f;
	float ampEnv = 0.f;
	float pitchEnv = 0.f;
	float modEnv = 0.f;
	float noiseEnv = 0.f;
	float shellEnv = 0.f;
	float ampCoef = 0.999f;
	float pitchCoef = 0.999f;
	float modCoef = 0.999f;
	float noiseCoef = 0.999f;
	float shellCoef = 0.999f;
	float baseFreq = 100.f;
	float pitchSweep = 0.f;
	float modRatio = 1.f;
	float modIndex = 0.f;
	float feedback = 0.f;
	float instability = 0.f;
	float noiseAmount = 0.f;
	float cutoff = 2000.f;
	float resonance = 0.2f;
	float drive = 0.f;
	float gain = 0.7f;
	float bodyAmount = 0.7f;
	float subAmount = 0.f;
	float fmAmount = 0.3f;
	float noiseBodyAmount = 0.1f;
	float noiseBrightAmount = 0.05f;
	float noiseToneCoef = 0.08f;
	float noiseLowState = 0.f;
	float variation = 0.5f;
	float shellFreqA = 180.f;
	float shellFreqB = 330.f;
	float shellMixA = 0.5f;
	float shellMixB = 0.5f;
	float shellTriangleMix = 0.f;
	float shellBuzzAmount = 0.f;
	float snareFatMix = 0.f;
	float crushAmount = 0.f;      // 0 = clean, 1 = max envelope-modulated bitcrush

	// Per-sample chaos effects chain state
	float chaosAmount = 0.f;         // Master gate: 0 = chain skipped
	float chaosBitDepth = 256.f;     // Quantization levels (256=clean, 4=destroyed)
	int chaosDecimatePeriod = 1;     // Sample hold period (1=off, 16=extreme)
	int chaosDecimateCounter = 0;    // Counter for decimation
	float chaosHeldSample = 0.f;     // Held sample for decimation
	float chaosBytebeatMix = 0.f;    // 0–1 bytebeat XOR mangling blend

	float hatTuneScale = 1.f;
	float hatToneEnv = 0.f;
	float hatToneCoef = 0.999f;
	float hatToneMix = 1.f;
	float bellAmp = 0.f;
	float bellAmpCoef = 0.999f;
	float rideBellMix = 0.f;
	float rideEqLowState = 0.f;
	float rideEqMidState = 0.f;
	float rideEqLowCoef = 0.05f;
	float rideEqMidCoef = 0.12f;
	float rideEqBellCut = 0.f;
	float rideEqAirBoost = 0.f;
	bool useKickSample = false;
	float kickCyclePhase = 0.f;
	float kickAttackPos = 0.f;
	float kickAttackRate = 1.f;
	float kickAttackGain = 0.f;
	float kickCycleGain = 1.f;
	uint32_t kickFrame = 0u;
	uint32_t kickReleaseStartFrame = 0u;
	// Rumble sub-bass bloom state (kick only)
	float rumblePhase    = 0.f;
	float rumbleEnv      = 0.f;
	float rumbleAmt      = 0.f;
	float rumbleAttCoef  = 0.0002f;
	float rumbleDecCoef  = 0.9996f;
	bool  rumbleDecaying = false;
	float rumbleOut      = 0.f;
	float rumbleOutSlew  = 0.006f;
	float rumbleFreqRatio = 0.62f;
	float rumbleColorMix = 0.f;
	float rumbleChaosDrive = 0.f;
	uint32_t rumbleDelaySamples = 0u;
	uint32_t rumbleElapsedSamples = 0u;
	float rumbleOnsetEnv = 0.f;
	float rumbleOnsetCoef = 0.01f;
	float rumbleSendLowState = 0.f;
	float rumbleSendLowCoef = 0.02f;
	float rumbleSubState = 0.f;
	float rumbleBodyState = 0.f;
	float rumbleSubCoef = 0.01f;
	float rumbleBodyCoef = 0.02f;
	float rumbleCleanup = 0.f;
	float rumbleDuckDepth = 0.f;
	float rumblePostDrive = 0.f;
	float rumbleSend = 0.f;
	float rumbleMaskStart = kRumbleMaskStart;
	float rumbleMaskSpan = kRumbleMaskSpan;
	float sampleToneLowState = 0.f;
	float sampleToneLowCoef = 0.05f;
	float sampleToneLowMix = 1.f;
	float sampleToneHighMix = 1.f;
	bool useOneShotSample = false;
	TR909OneShotSample* oneShotSample = nullptr;
	float sampleTransientMix = 0.f;
	uint32_t samplePhase = 0u;
	uint32_t samplePhaseInc = 0u;
	uint32_t sampleEndFrame = 0u;
	uint32_t sampleFadeStartFrame = 0u;
	float sampleFadeInv = 0.f;
	float hatPhase[kHatOscCount] = {};
	float hatDetune[kHatOscCount] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float hatGainNorm = 1.f / 6.f;  // Precomputed 1/sum(hatOscGains) set per-trigger in fireLane
	bool quantizeTransientType = false;
	int quantizedTransientType = 0;
	FilterMode filterMode = FILTER_LP;

	// Transient generator state (replaces old clickEnv/clickCoef/clickAmount)
	float transientEnv = 0.f;        // Exponential decay envelope
	float transientCoef = 0.999f;    // Per-sample decay coefficient
	float transientAmount = 0.f;     // Scaled amplitude (SNAP × per-voice × profile)
	float transientType = 0.f;       // 0–1 continuous sweep across click types
	float transientFmRoute = 0.f;    // 0–1 how much transient feeds FM index
	float transientPhase = 0.f;      // Phase accumulator for pitched click types
	float transientFreq = 2000.f;    // Base frequency for pitched clicks (Hz)
	float transientChirpRate = 1.f;  // Per-sample frequency multiplier (<1 = down, >1 = up)
	float transientFilterLP = 0.f;   // 1-pole LP state for noise-filtered types
	float transientSH = 0.f;         // Sample-and-hold value for glitch type
	int transientSHCounter = 0;      // Decimation counter for glitch type
	int transientSHPeriod = 4;       // Decimation period (samples between S&H updates)
	float lastTransientRaw = 0.f;    // Raw transient output (pre-envelope) for FM routing

	static float sampleTriangle(float phase) {
		phase = wrap01(phase);
		return 1.f - 4.f * std::fabs(phase - 0.5f);
	}

	// Generate a single click sample for one of the 13 transient types.
	// noise: fresh white noise sample, noiseBright: HP residual from main noise path
	float generateClickType(int type, float noise, float noiseBright, float sampleRate) {
		switch (type) {
			case 0: { // LP Noise — warm, soft mallet
				transientFilterLP += 0.15f * (noise - transientFilterLP);
				return transientFilterLP;
			}
			case 1: { // White Noise — classic stick hit
				return noise;
			}
			case 2: { // HP Noise — bright, thin click
				transientFilterLP += 0.08f * (noise - transientFilterLP);
				return noise - transientFilterLP;
			}
			case 3: { // BP Noise — tonal noise click
				// Simple resonant BP: feed noise through a tight 1-pole pair
				transientFilterLP += 0.12f * (noise - transientFilterLP);
				float hp = noise - transientFilterLP;
				// Second LP on HP residual creates a bandpass
				float bpState = transientSH; // reuse SH as second filter state for BP
				bpState += 0.18f * (hp - bpState);
				transientSH = bpState;
				return bpState * 1.8f;
			}
			case 4: { // Sine Click — clean, pure tone
				return sampleSine(transientPhase);
			}
			case 5: { // Chirp Down — TR-808 beater, classic
				return sampleSine(transientPhase);
			}
			case 6: { // Chirp Up — unusual IDM attack
				return sampleSine(transientPhase);
			}
			case 7: { // Sine+Noise — hybrid tonal/noise
				return sampleSine(transientPhase) * 0.7f + noise * 0.3f;
			}
			case 8: { // Ring Mod — metallic, bell-like
				return sampleSine(transientPhase) * noise;
			}
			case 9: { // FM Burst — complex harmonic
				float modPhaseLocal = transientPhase * 2.7183f; // e ratio
				float fmMod = sampleSine(modPhaseLocal) * 1.5f;
				return sampleSine(transientPhase + fmMod * 0.25f);
			}
			case 10: { // Square Pulse — aggressive, pitched
				return sampleSquare(transientPhase);
			}
			case 11: { // Digital Pulse — ultra-short, harsh
				return transientEnv > 0.5f ? 1.f : 0.f;
			}
			case 12: { // Glitch — lo-fi digital crackle with sample-and-hold decimation
				if (++transientSHCounter >= transientSHPeriod) {
					transientSHCounter = 0;
					transientSH = noise;
				}
				return transientSH;
			}
			default:
				return noise;
		}
	}

	// Generate the complete transient signal for this sample.
	// Crossfades between adjacent click types for smooth continuous sweep.
	// Returns the raw transient audio (already scaled by amount × envelope).
	// Also stores lastTransientRaw for FM routing.
	float generateTransient(float noise, float noiseBright, float sampleRate) {
		if (transientEnv < 1e-6f) {
			lastTransientRaw = 0.f;
			return 0.f;
		}

		float raw = 0.f;
		if (quantizeTransientType) {
			// MM optimization: choose the nearest click family once per hit instead of
			// blending adjacent models every sample. This mainly targets hat/ride CPU.
			raw = generateClickType(quantizedTransientType, noise, noiseBright, sampleRate);
		} else {
			float typeScaled = transientType * (float)(kNumTransientTypes - 1);
			int typeA = (int)typeScaled;
			if (typeA < 0) typeA = 0;
			if (typeA >= kNumTransientTypes - 1) typeA = kNumTransientTypes - 2;
			int typeB = typeA + 1;
			float frac = typeScaled - (float)typeA;

			float sampleA = generateClickType(typeA, noise, noiseBright, sampleRate);
			float sampleB = generateClickType(typeB, noise, noiseBright, sampleRate);
			raw = lerp(sampleA, sampleB, frac);
		}

		// Store raw for FM routing (before amount/env scaling)
		lastTransientRaw = raw * transientEnv;

		// Scale by amount and envelope
		float result = raw * transientAmount * transientEnv;

		// Advance phase for pitched types
		transientPhase = wrap01(transientPhase + transientFreq / sampleRate);

		// Apply chirp (frequency sweep per sample)
		transientFreq *= transientChirpRate;
		transientFreq = rack::math::clamp(transientFreq, 20.f, sampleRate * 0.45f);

		// Decay envelope
		transientEnv *= transientCoef;

		return result;
	}

	void applyKickRumble(float& signal, float sampleRate, float finalDrive) {
		if (machine != MACHINE_KICK) {
			return;
		}

		rumbleSend = 0.f;
		if (rumbleAmt <= 0.001f) {
			rumbleEnv = 0.f;
			rumbleOut = 0.f;
			rumbleDecaying = false;
			rumbleOnsetEnv = 0.f;
			rumbleSendLowState = 0.f;
			rumbleSubState = 0.f;
			rumbleBodyState = 0.f;
			return;
		}

		if (rumbleElapsedSamples != 0xFFFFFFFFu) {
			++rumbleElapsedSamples;
		}
		rumbleSendLowState += rumbleSendLowCoef * (signal - rumbleSendLowState);
		rumbleOnsetEnv += (1.f - rumbleOnsetEnv) * rumbleOnsetCoef;
		float transientSuppression = 1.f - smoothstep01(rack::math::clamp(transientEnv * 10.f, 0.f, 1.f));
		rumbleSend = rumbleSendLowState * transientSuppression * rumbleOnsetEnv;

		if (!rumbleDecaying) {
			rumbleEnv += (1.f - rumbleEnv) * rumbleAttCoef;
			if (rumbleEnv >= 0.98f) {
				rumbleDecaying = true;
			}
		} else {
			rumbleEnv *= rumbleDecCoef;
		}

		if (rumbleEnv <= 1e-6f && !rumbleDecaying) {
			return;
		}

		float rumbleFreq = rack::math::clamp(baseFreq * rumbleFreqRatio, 28.f, 68.f);
		rumblePhase = wrap01(rumblePhase + rumbleFreq / sampleRate);
		float rumbleGain = rumbleEnv * rumbleOnsetEnv * rumbleAmt * gain * kRumbleLevelBoost;
		float rumbleBase = sampleSine(rumblePhase);
		float rumbleHarm = sampleSine(wrap01(rumblePhase * 2.f));
		float asymmetry = std::fabs(rumbleBase) * 2.f - 0.637f;
		float rumbleWave = rumbleBase * (1.f - 0.38f * rumbleColorMix) + rumbleHarm * rumbleColorMix;
		rumbleWave += asymmetry * (0.08f + 0.32f * rumbleColorMix);
		rumbleWave = softClip(rumbleWave, rumbleChaosDrive);

		rumbleSubState += rumbleSubCoef * (rumbleWave - rumbleSubState);
		rumbleBodyState += rumbleBodyCoef * (rumbleWave - rumbleBodyState);
		float rumbleFiltered = rumbleSubState + (rumbleBodyState - rumbleSubState) * (1.f - rumbleCleanup);

		float duckEnv = clamp01(transientEnv * 18.f + pitchEnv * 0.10f);
		float duckShape = duckEnv * duckEnv * (1.30f - 0.30f * duckEnv);
		float duckGain = 1.f - rumbleDuckDepth * duckShape;
		float rumbleDrive = rack::math::clamp(finalDrive * kRumbleDriveAmountFromDrive + rumbleChaosDrive * kRumbleDriveAmountFromChaos + rumblePostDrive * 0.45f, 0.f, 1.f);
		float rumbleDriveGain = kRumbleDriveGainBase + finalDrive * kRumbleDriveGainFromDrive + rumbleColorMix * kRumbleDriveGainFromColor + rumblePostDrive * 1.30f;
		float rumbleTarget = softClip(rumbleFiltered * rumbleGain * rumbleDriveGain * duckGain, rumbleDrive);
		rumbleOut += rumbleOutSlew * (rumbleTarget - rumbleOut);
		signal += rumbleOut;
	}

	void reset() {
		filter.reset();
		dcBlock.reset();
		active = false;
		carrierPhase = 0.f;
		modPhase = 0.f;
		subPhase = 0.f;
		shellPhaseA = 0.f;
		shellPhaseB = 0.f;
		prevModOutput = 0.f;
		ampEnv = 0.f;
		pitchEnv = 0.f;
		modEnv = 0.f;
		noiseEnv = 0.f;
		shellEnv = 0.f;
		noiseLowState = 0.f;
		snareFatMix = 0.f;
		hatTuneScale = 1.f;
		hatToneEnv = 0.f;
		hatToneCoef = 0.999f;
		hatToneMix = 1.f;
		bellAmp = 0.f;
		bellAmpCoef = 0.999f;
		rideBellMix = 0.f;
		rideEqLowState = 0.f;
		rideEqMidState = 0.f;
		rideEqBellCut = 0.f;
		rideEqAirBoost = 0.f;
		useKickSample = false;
		kickCyclePhase = 0.f;
		kickAttackPos = 0.f;
		kickAttackRate = 1.f;
		kickAttackGain = 0.f;
		kickCycleGain = 1.f;
		kickFrame = 0u;
		kickReleaseStartFrame = 0u;
		rumblePhase    = 0.f;
		rumbleEnv      = 0.f;
		rumbleAmt      = 0.f;
		rumbleDecaying = false;
		rumbleOut      = 0.f;
		rumbleOutSlew  = 0.006f;
		rumbleFreqRatio = 0.62f;
		rumbleColorMix = 0.f;
		rumbleChaosDrive = 0.f;
		rumbleDelaySamples = 0u;
		rumbleElapsedSamples = 0u;
		rumbleOnsetEnv = 0.f;
		rumbleOnsetCoef = 0.01f;
		rumbleSendLowState = 0.f;
		rumbleSendLowCoef = 0.02f;
		rumbleSubState = 0.f;
		rumbleBodyState = 0.f;
		rumbleSubCoef = 0.01f;
		rumbleBodyCoef = 0.02f;
		rumbleCleanup = 0.f;
		rumbleDuckDepth = 0.f;
		rumblePostDrive = 0.f;
		rumbleSend = 0.f;
		rumbleMaskStart = kRumbleMaskStart;
		rumbleMaskSpan = kRumbleMaskSpan;
		sampleToneLowState = 0.f;
		sampleToneLowCoef = 0.05f;
		sampleToneLowMix = 1.f;
		sampleToneHighMix = 1.f;
		useOneShotSample = false;
		oneShotSample = nullptr;
		sampleTransientMix = 0.f;
		samplePhase = 0u;
		samplePhaseInc = 0u;
		sampleEndFrame = 0u;
		sampleFadeStartFrame = 0u;
		sampleFadeInv = 0.f;
		for (int i = 0; i < kHatOscCount; ++i) {
			hatPhase[i] = 0.f;
			hatDetune[i] = 1.f;
		}
		// Transient state
		transientEnv = 0.f;
		transientPhase = 0.f;
		transientFilterLP = 0.f;
		transientSH = 0.f;
		transientSHCounter = 0;
		lastTransientRaw = 0.f;
		quantizeTransientType = false;
		quantizedTransientType = 0;
		transientFreq = 2000.f;
		transientChirpRate = 1.f;
		// Chaos FX state
		chaosDecimateCounter = 0;
		chaosHeldSample = 0.f;
	}

	float render(float sampleRate, float finalDrive, bool metaModuleVariant) {
		rumbleSend = 0.f;
		if (!active) {
			return 0.f;
		}

		bool isSnare = (machine == MACHINE_SNARE1 || machine == MACHINE_SNARE2);
		bool isHat = (machine == MACHINE_METAL || machine == MACHINE_CHAOS1 || machine == MACHINE_RIDE);

#ifndef METAMODULE
		if (machine == MACHINE_KICK && useKickSample) {
			TR909KickSamples& kickSamples = getTR909KickSamples();
			if (kickSamples.attack.size() < 2 || kickSamples.cycle.size() < 2) {
				reset();
				return 0.f;
			}

			float noise = randomBipolar(noiseState);
			float transientSample = generateTransient(noise, noise, sampleRate);
			float kickChaosBitDepth = chaosBitDepth;
			int kickChaosDecimatePeriod = chaosDecimatePeriod;
			if (kickChaosBitDepth < 255.f) {
				kickChaosBitDepth = moderateKickChaosBitDepth(kickChaosBitDepth);
			}
			if (kickChaosDecimatePeriod > 1) {
				kickChaosDecimatePeriod = moderateKickChaosDecimatePeriod(kickChaosDecimatePeriod);
			}

			float freqMul = 1.f + pitchSweep * pitchEnv;
			freqMul = rack::math::clamp(freqMul, 0.25f, 12.f);
			float cycleFreq = rack::math::clamp(baseFreq * freqMul, 20.f, sampleRate * 0.45f);

			float cyclePos = kickCyclePhase * (float)kickSamples.cycle.size();
			int cycleIndex = (int)cyclePos;
			float cycleFrac = cyclePos - (float)cycleIndex;
			int cycleNext = cycleIndex + 1;
			if (cycleNext >= (int)kickSamples.cycle.size()) {
				cycleNext = 0;
			}
			float cycleSample = kickSamples.cycle[cycleIndex] + (kickSamples.cycle[cycleNext] - kickSamples.cycle[cycleIndex]) * cycleFrac;
			float signal = cycleSample * kickCycleGain * ampEnv;

			if (kickAttackPos < (float)(kickSamples.attack.size() - 1u)) {
				int attackIndex = (int)kickAttackPos;
				float attackFrac = kickAttackPos - (float)attackIndex;
				float attackA = kickSamples.attack[attackIndex];
				float attackB = kickSamples.attack[attackIndex + 1];
				signal += (attackA + (attackB - attackA) * attackFrac) * kickAttackGain;
				kickAttackPos += kickAttackRate;
			}
			signal += transientSample * (0.32f + 0.18f * kickAttackGain);

			sampleToneLowState += sampleToneLowCoef * (signal - sampleToneLowState);
			if (!std::isfinite(sampleToneLowState)) sampleToneLowState = 0.f;
			{
				float toneLow = sampleToneLowState;
				float toneHigh = signal - toneLow;
				signal = toneLow * sampleToneLowMix + toneHigh * sampleToneHighMix;
			}

			kickCyclePhase = wrap01(kickCyclePhase + cycleFreq / sampleRate);
			if (kickFrame++ >= kickReleaseStartFrame) {
				ampEnv *= ampCoef;
			}
			pitchEnv *= pitchCoef;

			if (chaosAmount > 0.001f) {
				if (kickChaosBitDepth < 255.f) {
					signal = std::round(signal * kickChaosBitDepth) / kickChaosBitDepth;
				}
				if (kickChaosDecimatePeriod > 1) {
					if (++chaosDecimateCounter >= kickChaosDecimatePeriod) {
						chaosDecimateCounter = 0;
						chaosHeldSample = signal;
					}
					signal = chaosHeldSample;
				}
				if (chaosBytebeatMix > 0.001f) {
					float decayPos = 1.f - ampEnv;
					float bbMix = computeKickChaosBytebeatMix(chaosBytebeatMix, ampEnv);
					if (bbMix > 0.001f) {
						int ival = (int)(signal * 127.f);
						int shift = 1 + (int)(decayPos * 4.f);
						ival = ival ^ (ival >> shift);
						ival = rack::math::clamp(ival, -127, 127);
						float byteSignal = (float)ival * (1.f / 127.f);
						signal = lerp(signal, byteSignal, bbMix);
					}
				}
			}

			signal = softClip(signal, drive);
			signal = dcBlock.process(signal);
			signal *= gain;
			signal = amenolithDriveClip(signal, finalDrive);
			applyKickRumble(signal, sampleRate, finalDrive);

			if (!std::isfinite(signal)) {
				reset();
				return 0.f;
			}

			if (ampEnv < 1e-5f && kickAttackPos >= (float)(kickSamples.attack.size() - 1u) && rumbleEnv < 1e-5f) {
				reset();
			}

			return signal;
		}
#endif

		// MM sample optimization: use the direct one-shot sample path in both VCV Rack and
		// MetaModule builds when the module variant is FerroklastMM.
		if (metaModuleVariant && useOneShotSample && (machine == MACHINE_RIDE || machine == MACHINE_METAL || machine == MACHINE_CLAP)) {
			TR909OneShotSample* s = oneShotSample;
			if (!s || s->data.size() < 2) { reset(); return 0.f; }
			const uint32_t sampleFrames = (uint32_t)s->data.size();
			const uint32_t endFrame = std::min(sampleEndFrame, sampleFrames);
			if (endFrame < 2 || samplePhaseInc == 0u) { reset(); return 0.f; }
			const uint32_t idx = samplePhase >> 16;
			if (idx >= endFrame - 1u) { reset(); return 0.f; }
			const uint32_t frac = samplePhase & 0xFFFFu;
			float sig = s->data[idx] + (s->data[idx + 1u] - s->data[idx]) * ((float)frac * (1.f / 65536.f));
			samplePhase += samplePhaseInc;
			ampEnv = 1.f;
			if (sampleFadeInv > 0.f && idx >= sampleFadeStartFrame) {
				float fade = (float)(endFrame - 1u - idx) * sampleFadeInv;
				fade = rack::math::clamp(fade, 0.f, 1.f);
				sig *= fade;
				ampEnv = fade;
			}
			sampleToneLowState += sampleToneLowCoef * (sig - sampleToneLowState);
			if (!std::isfinite(sampleToneLowState)) sampleToneLowState = 0.f;
			{
				float toneLow = sampleToneLowState;
				float toneHigh = sig - toneLow;
				sig = toneLow * sampleToneLowMix + toneHigh * sampleToneHighMix;
			}
			if (chaosAmount > 0.001f) {
				if (chaosBitDepth < 255.f) {
					sig = std::round(sig * chaosBitDepth) / chaosBitDepth;
				}
				if (chaosDecimatePeriod > 1) {
					if (++chaosDecimateCounter >= chaosDecimatePeriod) {
						chaosDecimateCounter = 0;
						chaosHeldSample = sig;
					}
					sig = chaosHeldSample;
				}
			}
			sig = softClip(sig, drive);
			sig = dcBlock.process(sig);
			sig *= gain;
			sig = amenolithDriveClip(sig, finalDrive);
			if (idx >= endFrame - 2u) {
				reset();
			}
			return std::isfinite(sig) ? sig : 0.f;
		}

#ifndef METAMODULE
		if ((machine == MACHINE_RIDE || machine == MACHINE_CLAP || machine == MACHINE_RIM) && useOneShotSample) {
			TR909OneShotSample* sample = oneShotSample;
			if (!sample) {
				reset();
				return 0.f;
			}
			uint32_t sampleFrames = (uint32_t)sample->data.size();
			uint32_t endFrame = std::min(sampleEndFrame, sampleFrames);
			if (sampleFrames < 2 || endFrame < 2 || samplePhaseInc == 0u) {
				reset();
				return 0.f;
			}

			uint32_t idx = samplePhase >> 16;
			if (idx >= endFrame - 1u) {
				reset();
				return 0.f;
			}

			uint32_t frac = samplePhase & 0xFFFFu;
			float a = sample->data[idx];
			float b = sample->data[idx + 1u];
			float t = (float)frac * (1.f / 65536.f);
			float signal = a + (b - a) * t;
			samplePhase += samplePhaseInc;

			ampEnv = 1.f;
			if (sampleFadeInv > 0.f && idx >= sampleFadeStartFrame) {
				float fade = (float)(endFrame - 1u - idx) * sampleFadeInv;
				fade = rack::math::clamp(fade, 0.f, 1.f);
				signal *= fade;
				ampEnv = fade;
			}

			if (sampleTransientMix > 0.f) {
				float noise = randomBipolar(noiseState);
				float transientSample = generateTransient(noise, noise, sampleRate);
				signal += transientSample * sampleTransientMix;
			}

			sampleToneLowState += sampleToneLowCoef * (signal - sampleToneLowState);
			if (!std::isfinite(sampleToneLowState)) sampleToneLowState = 0.f;
			{
				float toneLow = sampleToneLowState;
				float toneHigh = signal - toneLow;
				signal = toneLow * sampleToneLowMix + toneHigh * sampleToneHighMix;
			}

			if (chaosAmount > 0.001f) {
				if (chaosBitDepth < 255.f) {
					signal = std::round(signal * chaosBitDepth) / chaosBitDepth;
				}
				if (chaosDecimatePeriod > 1) {
					if (++chaosDecimateCounter >= chaosDecimatePeriod) {
						chaosDecimateCounter = 0;
						chaosHeldSample = signal;
					}
					signal = chaosHeldSample;
				}
				if (chaosBytebeatMix > 0.001f) {
					float decayPos = 1.f - ampEnv;
					float bbMix = chaosBytebeatMix * decayPos;
					if (bbMix > 0.001f) {
						int ival = (int)(signal * 127.f);
						int shift = 1 + (int)(decayPos * 4.f);
						ival = ival ^ (ival >> shift);
						ival = rack::math::clamp(ival, -127, 127);
						float byteSignal = (float)ival * (1.f / 127.f);
						signal = lerp(signal, byteSignal, bbMix);
					}
				}
			}

			signal = softClip(signal, drive);
			signal = dcBlock.process(signal);
			signal *= gain;
			signal = amenolithDriveClip(signal, finalDrive);

			if (!std::isfinite(signal)) {
				reset();
				return 0.f;
			}

			if (sampleTransientMix > 0.f && transientEnv < 1e-5f && idx >= endFrame - 2u) {
				reset();
			}

			return signal;
		}
#endif

		float noise = randomBipolar(noiseState);
		float instNoise = randomBipolar(noiseState);
		float freqMul = 1.f + pitchSweep * pitchEnv;
		freqMul = rack::math::clamp(freqMul, 0.1f, 12.f);
		float carrierFreq = rack::math::clamp(baseFreq * freqMul, 10.f, sampleRate * 0.45f);
		float modFreq = rack::math::clamp(baseFreq * modRatio * (1.f + instability * instNoise), 10.f, sampleRate * 0.45f);
		float shellPitchMul = isSnare ? rack::math::clamp(1.f + pitchSweep * pitchEnv, 0.5f, 2.5f) : 1.f;
		float shellFreqNowA = rack::math::clamp(shellFreqA * shellPitchMul, 40.f, sampleRate * 0.45f);
		float shellFreqNowB = rack::math::clamp(shellFreqB * shellPitchMul, 40.f, sampleRate * 0.45f);

		carrierPhase = wrap01(carrierPhase + carrierFreq / sampleRate);
		modPhase = wrap01(modPhase + modFreq / sampleRate);
		subPhase = wrap01(subPhase + (carrierFreq * 0.5f) / sampleRate);
		shellPhaseA = wrap01(shellPhaseA + shellFreqNowA / sampleRate);
		shellPhaseB = wrap01(shellPhaseB + shellFreqNowB / sampleRate);

		float modOut = sampleSine(modPhase);
		float modInput = modOut + feedback * prevModOutput + noiseAmount * noise * (0.2f + 0.8f * noiseEnv);
		prevModOutput = modOut;

		noiseLowState += noiseToneCoef * (noise - noiseLowState);
		if (!std::isfinite(noiseLowState)) noiseLowState = 0.f;
		float noiseBody = noiseLowState;
		float noiseBright = noise - noiseLowState;

		// Generate transient (shared across all paths)
		float transientSample = generateTransient(noise, noiseBright, sampleRate);

		float dryCarrier = sampleSine(carrierPhase);
		float subCarrier = sampleSine(subPhase);
		float effectiveModIndex = modIndex * modEnv;
		float fmCarrier = sampleSine(carrierPhase + (effectiveModIndex * modInput * 0.25f));
		float shellOscA = sampleSine(shellPhaseA);
		float shellPureB = sampleSine(shellPhaseB);
		float shellTriB = sampleTriangle(shellPhaseB);
		float shellOscB = lerp(shellPureB, shellTriB, shellTriangleMix);

		if (isSnare) {
			float shellCore = shellOscA * shellMixA + shellOscB * shellMixB;
			float bodyBuzz = (noiseBody * shellBuzzAmount + noiseBright * shellBuzzAmount * 0.12f);
			float bodySignal = softClip((shellCore + bodyBuzz) * bodyAmount, drive * 0.35f) * ampEnv;
			float wireRaw = (noiseBody * noiseBodyAmount + noiseBright * noiseBrightAmount) * noiseEnv;

			// Envelope-modulated bitcrusher: clean at attack, crushed during decay
			if (crushAmount > 0.001f) {
				float envPos = 1.f - noiseEnv; // 0 at attack, 1 at silence
				float crushDepth = envPos * envPos * crushAmount;
				// Step size: 1/2048 (clean) → 1/8 (crushed)
				float stepSize = (1.f / 2048.f) + crushDepth * (1.f / 8.f - 1.f / 2048.f);
				float invStep = 1.f / stepSize;
				wireRaw = std::round(wireRaw * invStep) * stepSize;
			}

			float colorSignal = fmCarrier * fmAmount * modEnv;
			float fatLayer = filter.process(wireRaw, cutoff, resonance, sampleRate, FILTER_BP);
			float reinforcedBody = bodySignal * (1.f + 0.22f * snareFatMix);
			float wireShaped = fatLayer * lerp(0.85f, 1.30f, snareFatMix)
				+ transientSample * lerp(0.90f, 0.55f, snareFatMix)
				+ colorSignal * 0.24f;
			float signal = reinforcedBody + wireShaped;

			// Chaos effects chain (snare path)
			if (chaosAmount > 0.001f) {
				// Bitcrush
				if (chaosBitDepth < 255.f) {
					signal = std::round(signal * chaosBitDepth) / chaosBitDepth;
				}
				// Decimation
				if (chaosDecimatePeriod > 1) {
					if (++chaosDecimateCounter >= chaosDecimatePeriod) {
						chaosDecimateCounter = 0;
						chaosHeldSample = signal;
					}
					signal = chaosHeldSample;
				}
				// Bytebeat XOR mangling — linear decay gate so it's audible from mid-decay
				if (chaosBytebeatMix > 0.001f) {
					float decayPos = 1.f - ampEnv; // 0 at attack, 1 at silence
					float bbMix = chaosBytebeatMix * decayPos;
					if (bbMix > 0.001f) {
						int ival = (int)(signal * 127.f);
						// Evolving XOR pattern: shift amount changes with envelope
						int shift = 1 + (int)(decayPos * 4.f);
						ival = ival ^ (ival >> shift);
						ival = rack::math::clamp(ival, -127, 127);
						float byteSignal = (float)ival * (1.f / 127.f);
						signal = lerp(signal, byteSignal, bbMix);
					}
				}
			}

			signal = softClip(signal, drive);
			signal = dcBlock.process(signal);
			signal *= gain;
			signal = amenolithDriveClip(signal, finalDrive);

			if (!std::isfinite(signal)) {
				reset();
				return 0.f;
			}

			ampEnv *= ampCoef;
			pitchEnv *= pitchCoef;
			modEnv *= modCoef;
			noiseEnv *= noiseCoef;
			shellEnv *= shellCoef;

			if (ampEnv < 1e-5f && noiseEnv < 1e-5f && transientEnv < 1e-5f && modEnv < 1e-5f) {
				reset();
			}

			return signal;
		}

		if (isHat) {
			const float* hatBaseFreqs = (machine == MACHINE_RIDE) ? kRideBaseFreqs : kHatBaseFreqs;
			const float* hatOscGains = (machine == MACHINE_RIDE) ? kRideOscGains : kHatOscGains;
			float hatMix = 0.f;
			for (int i = 0; i < kHatOscCount; ++i) {
				float hatFreq = rack::math::clamp(hatBaseFreqs[i] * hatTuneScale * hatDetune[i], 200.f, sampleRate * 0.45f);
				hatPhase[i] = wrap01(hatPhase[i] + hatFreq / sampleRate);
				hatMix += sampleSquare(hatPhase[i]) * hatOscGains[i];
			}
			hatMix *= hatGainNorm;  // precomputed 1/gainSum, no per-sample division
			float hatNoise = noiseBody * noiseBodyAmount + noiseBright * noiseBrightAmount;
			float hatTone = hatMix * hatToneMix * hatToneEnv * (0.72f + 0.18f * transientEnv);
			float hatRaw = hatTone + hatNoise * (0.32f + 0.28f * noiseEnv);
			if (machine == MACHINE_RIDE) {
				float metallicSpray = hatMix * noiseBright * hatToneEnv * (0.18f + 0.18f * noiseEnv);
				float hissBed = hatNoise * (0.62f + 0.24f * noiseEnv);
				float railLayer = hatTone * (0.42f + 0.10f * transientEnv);
				hatRaw = hissBed + railLayer + metallicSpray + transientSample * 0.16f;
			}

			// Ride bell: FM carrier adds sustained metallic ping
			if (machine == MACHINE_RIDE) {
				float bellTone = fmCarrier * fmAmount * bellAmp * rideBellMix * (0.32f + 0.48f * modEnv);
				hatRaw += bellTone;
			}

			float hatShaped = filter.process(hatRaw, cutoff, resonance, sampleRate, filterMode);
			if (machine == MACHINE_RIDE) {
				rideEqLowState += rideEqLowCoef * (hatShaped - rideEqLowState);
				rideEqMidState += rideEqMidCoef * (hatShaped - rideEqMidState);
				float bellBand = rideEqMidState - rideEqLowState;
				float airBand = hatShaped - rideEqMidState;
				hatShaped = hatShaped - bellBand * rideEqBellCut + airBand * rideEqAirBoost;
			}
			float signal = hatShaped * ampEnv + transientSample * (machine == MACHINE_RIDE ? 0.28f : 2.4f);

			// Chaos effects chain (hat path)
			if (chaosAmount > 0.001f) {
				if (chaosBitDepth < 255.f) {
					signal = std::round(signal * chaosBitDepth) / chaosBitDepth;
				}
				if (chaosDecimatePeriod > 1) {
					if (++chaosDecimateCounter >= chaosDecimatePeriod) {
						chaosDecimateCounter = 0;
						chaosHeldSample = signal;
					}
					signal = chaosHeldSample;
				}
				if (chaosBytebeatMix > 0.001f) {
					float decayPos = 1.f - ampEnv;
					float bbMix = chaosBytebeatMix * decayPos;
					if (bbMix > 0.001f) {
						int ival = (int)(signal * 127.f);
						int shift = 1 + (int)(decayPos * 4.f);
						ival = ival ^ (ival >> shift);
						ival = rack::math::clamp(ival, -127, 127);
						float byteSignal = (float)ival * (1.f / 127.f);
						signal = lerp(signal, byteSignal, bbMix);
					}
				}
			}

			signal = softClip(signal, drive);
			signal = dcBlock.process(signal);
			signal *= gain;
			signal = amenolithDriveClip(signal, finalDrive);

			if (!std::isfinite(signal)) {
				reset();
				return 0.f;
			}

			ampEnv *= ampCoef;
			pitchEnv *= pitchCoef;
			modEnv *= modCoef;
			noiseEnv *= noiseCoef;
			hatToneEnv *= hatToneCoef;
			bellAmp *= bellAmpCoef;

			if (ampEnv < 1e-5f && noiseEnv < 1e-5f && transientEnv < 1e-5f && hatToneEnv < 1e-5f && modEnv < 1e-5f) {
				reset();
			}

			return signal;
		}

		// General path (Kick, Chaos2, etc.)
		float shellCore = shellOscA * shellMixA + shellOscB * shellMixB;
		float shellBuzz = (shellOscA * noiseBody + shellOscB * noiseBright * 0.7f) * shellBuzzAmount * noiseEnv;
		float shell = (shellCore + shellBuzz) * shellEnv;
		float extraExciter = 0.f;
		if (machine == MACHINE_METAL || machine == MACHINE_CHAOS1) {
			float ring = dryCarrier * modOut;
			float buzz = (modOut >= 0.f ? 1.f : -1.f) * (0.08f + instability * 0.22f);
			extraExciter = ring * (0.18f + fmAmount * 0.72f)
				+ buzz * (0.04f + noiseBrightAmount * 0.40f)
				+ noiseBright * feedback * 0.18f;
		}
		float tonal = dryCarrier * bodyAmount + subCarrier * subAmount + shell;
		float exciter = fmCarrier * fmAmount
			+ noiseBody * noiseBodyAmount * noiseEnv
			+ noiseBright * noiseBrightAmount * noiseEnv
			+ transientSample
			+ extraExciter;
		float filtered = filter.process(exciter, cutoff, resonance, sampleRate, filterMode);
		float signal = tonal + filtered;

		// Chaos effects chain (general path)
		if (chaosAmount > 0.001f) {
			float localChaosBitDepth = (machine == MACHINE_KICK)
				? moderateKickChaosBitDepth(chaosBitDepth)
				: chaosBitDepth;
			int localChaosDecimatePeriod = (machine == MACHINE_KICK)
				? moderateKickChaosDecimatePeriod(chaosDecimatePeriod)
				: chaosDecimatePeriod;
			if (localChaosBitDepth < 255.f) {
				signal = std::round(signal * localChaosBitDepth) / localChaosBitDepth;
			}
			if (localChaosDecimatePeriod > 1) {
				if (++chaosDecimateCounter >= localChaosDecimatePeriod) {
					chaosDecimateCounter = 0;
					chaosHeldSample = signal;
				}
				signal = chaosHeldSample;
			}
			if (chaosBytebeatMix > 0.001f) {
				float decayPos = 1.f - ampEnv;
				float bbMix = (machine == MACHINE_KICK)
					? computeKickChaosBytebeatMix(chaosBytebeatMix, ampEnv)
					: chaosBytebeatMix * decayPos;
				if (bbMix > 0.001f) {
					int ival = (int)(signal * 127.f);
					int shift = 1 + (int)(decayPos * 4.f);
					ival = ival ^ (ival >> shift);
					ival = rack::math::clamp(ival, -127, 127);
					float byteSignal = (float)ival * (1.f / 127.f);
					signal = lerp(signal, byteSignal, bbMix);
				}
			}
		}

		signal = softClip(signal, drive);
		signal = dcBlock.process(signal);
		signal *= ampEnv * gain;
		signal = amenolithDriveClip(signal, finalDrive);
		applyKickRumble(signal, sampleRate, finalDrive);

		if (!std::isfinite(signal)) {
			reset();
			return 0.f;
		}

		ampEnv *= ampCoef;
		pitchEnv *= pitchCoef;
		modEnv *= modCoef;
		noiseEnv *= noiseCoef;
			hatToneEnv *= hatToneCoef;
		shellEnv *= shellCoef;

		if (ampEnv < 1e-5f && noiseEnv < 1e-5f && transientEnv < 1e-5f && shellEnv < 1e-5f && rumbleEnv < 1e-5f) {
			reset();
		}

		return signal;
	}
};

struct Ferroklast : Module {
	enum ParamId {
		// MetaModule-visible macro controls first, in panel traversal order.
		KICK_RUMBLE_PARAM,
		KICK_ATTACK_TONE_PARAM,
		PUNCH_PARAM,
		BODY_PARAM,
		COLOR_PARAM,
		SNARE_RES_PARAM,
		SNARE_CUT_PARAM,
		SNARE_DRIVE_PARAM,
		KICK_CURVE_PARAM,
		KICK_PITCH_DEPTH_PARAM,
		SNAP_PARAM,
		NOISE_PARAM,
		TRANSIENT_PARAM,
		MATERIAL_PARAM,
		CHAOS_PARAM,
		FINAL_DRIVE_PARAM,
		LEVEL_PARAM_BASE,
		TUNE_PARAM_BASE = LEVEL_PARAM_BASE + kNumVoices,
		DECAY_PARAM_BASE = TUNE_PARAM_BASE + kNumVoices,
		VARIATION_PARAM_BASE = DECAY_PARAM_BASE + kNumVoices,
		TRANS_AMOUNT_PARAM_BASE = VARIATION_PARAM_BASE + kNumVoices,
		TRANS_TYPE_PARAM_BASE = TRANS_AMOUNT_PARAM_BASE + kNumVoices,
		TRANS_DECAY_PARAM_BASE = TRANS_TYPE_PARAM_BASE + kNumVoices,
		TRANS_FM_PARAM_BASE = TRANS_DECAY_PARAM_BASE + kNumVoices,
		// Full-size Rack-only controls after MM-visible ones.
		RVB_SEND_PARAM_BASE = TRANS_FM_PARAM_BASE + kNumVoices,
		RVB_TIME_PARAM = RVB_SEND_PARAM_BASE + kNumVoices,
		RVB_DAMP_PARAM,
		RVB_DIFF_PARAM,
		RVB_AMOUNT_PARAM,
		DUCK_DEPTH_PARAM,
		DUCK_PUMP_PARAM,
		DUCK_ON_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		ACCENT_INPUT,
		DECIMATE_CV_INPUT,
		DRIVE_CV_INPUT,
		COLOR_CV_INPUT,
		BODY_CV_INPUT,
		PUNCH_CV_INPUT,
		SNAP_CV_INPUT,
		NOISE_CV_INPUT,
		VARIATION_CV_INPUT,
		MATERIAL_CV_INPUT,
		TRANSIENT_CV_INPUT,
		RVB_DAMP_CV_INPUT,
		RVB_DIFF_CV_INPUT,
		RVB_AMOUNT_CV_INPUT,
		RVB_TIME_CV_INPUT,
		SNARE_CUT_CV_INPUT,
		KICK_LENGTH_CV_INPUT,
		TRIG_INPUT_BASE,
		INPUTS_LEN = TRIG_INPUT_BASE + kNumVoices
	};

	enum OutputId {
		AUDIO_OUTPUT_BASE,
		REV_WET_L_OUTPUT = AUDIO_OUTPUT_BASE + kNumVoices,
		REV_WET_R_OUTPUT,
		SIDECHAIN_OUTPUT,
		GROOVE_CV_OUTPUT,
		OUTPUTS_LEN
	};

	enum LightId {
		HIT_LIGHT_BASE,
		LIGHTS_LEN = HIT_LIGHT_BASE + kNumVoices
	};

	LaneState lanes[kNumVoices];
	float macroSmooth[8] = {0.64f, 0.35f, 0.52f, 0.45f, 0.28f, 0.22f, 0.50f, 0.50f};
	float finalDriveSmooth = 0.f;
	float snareCutSmooth = 0.50f;
	float snareResSmooth = 0.30f;
	float snareDriveSmooth = 0.0f;
	// Kick-sidechain ducking
	float duckDepthSmooth_ = 0.f;
	float duckPumpSmooth_  = 0.4f;
	bool  duckOnCache_     = false;
	float duckHoldEnv_     = 0.f;  // peak-hold envelope, releases independently of kick length
	float duckReleaseCoef_ = 0.999f; // pre-computed at control rate
	float grooveCvHeld_ = 0.18f;
	float grooveCvOutput_ = 0.18f;
	float grooveMetalMemory_ = 0.f;
	float grooveAccentMemory_ = 0.f;
	float grooveTextureDecayCoef_ = 0.9997f;
	float grooveAccentDecayCoef_ = 0.9998f;
	float grooveOutputSlewCoeff_ = 0.02f;
	int controlDivider = 0;
	float sampleRateCache = 48000.f;
	FerroklastVariant variant_ = FerroklastVariant::Full;

#ifndef METAMODULE
	KickRumbleSmear kickRumbleSmear_;
	morphworx::Reverb reverb_;
	float reverb_buffer_[65536] = {};
	float rvbHpfState_ = 0.f;
	float rvbHpfXPrev_ = 0.f;
	float rvbHpfCoeff_ = 0.9958f; // 1-pole HPF @ ~150Hz / 48kHz; updated at control rate
#endif

	Ferroklast(FerroklastVariant variant = FerroklastVariant::Full)
		: variant_(variant) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(PUNCH_PARAM, 0.f, 1.f, 0.64f, "Punch — FM depth & pitch sweep intensity (all voices)");
		configParam(BODY_PARAM, 0.f, 1.f, 0.35f, "Body — fundamental weight & sub harmonics (Kick, Snares)");
		configParam(COLOR_PARAM, 0.f, 2.f, 0.65f, "Color — FM ratio & tonal character (all voices)");
		configParam(FINAL_DRIVE_PARAM, 0.f, 1.f, 0.f, "Final drive — Amenolith-style last-stage saturation (all voices)");
		configParam(SNAP_PARAM, 0.f, 1.f, 0.45f, "Snap — transient intensity & brightness (all voices)");
		configParam(NOISE_PARAM, 0.f, 1.f, 0.28f, "Noise — noise amount, >50% adds bitcrusher on snare wires (all voices)");
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.22f, "Ruin — texture-focused through most of the range, destructive in the top end (all voices)");
		configParam(MATERIAL_PARAM, 0.f, kMaterialMacroMax, 0.50f, "Material — body versus exciter balance (all voices)");
		configParam(TRANSIENT_PARAM, 0.f, kTransientMacroMax, 0.50f, "Transient — global attack character and decay contour (all voices)");
		configParam(SNARE_CUT_PARAM, 0.f, 1.f, 0.50f, "Snare filter cutoff — wire brightness (Snare 1 & 2 only)");
		configParam(SNARE_RES_PARAM, 0.3f, 1.f, 0.30f, "Snare filter resonance — wire ring (Snare 1 & 2 only)");
		configParam(SNARE_DRIVE_PARAM, 0.f, 1.f, 0.0f, "Snare filter overdrive (Snare 1 & 2 only)");

		for (int i = 0; i < kNumVoices; ++i) {
			configParam(LEVEL_PARAM_BASE + i, 0.f, 1.f, kDefaultLevels[i], std::string(kVoiceNames[i]) + " level");
			configParam(TUNE_PARAM_BASE + i, (i == 0) ? -12.f : -36.f, 12.f, kDefaultTunes[i], std::string(kVoiceNames[i]) + " tune", " st");
			configParam(DECAY_PARAM_BASE + i, 0.f, 1.f, kDefaultDecays[i], std::string(kVoiceNames[i]) + " decay");
			configParam(VARIATION_PARAM_BASE + i, 0.f, 1.f, kDefaultVariations[i], (i == 0)
				? std::string(kVoiceNames[i]) + " variation — pitch plus round/body to snappy/beater morph"
				: std::string(kVoiceNames[i]) + " variation");
			configParam(TRANS_AMOUNT_PARAM_BASE + i, 0.f, 1.f, kDefaultTransAmounts[i], std::string(kVoiceNames[i]) + " trans amount");
			configParam(TRANS_TYPE_PARAM_BASE + i, 0.f, 1.f, kDefaultTransTypes[i], std::string(kVoiceNames[i]) + " trans type");
			configParam(TRANS_DECAY_PARAM_BASE + i, 0.f, 1.f, kDefaultTransDecays[i], (i == 0)
				? std::string(kVoiceNames[i]) + " transient decay & rumble decay (0 = rumble off)"
				: std::string(kVoiceNames[i]) + " trans decay");
			configParam(TRANS_FM_PARAM_BASE + i, 0.f, 1.f, 0.f, std::string(kVoiceNames[i]) + " trans FM route");
			configParam(RVB_SEND_PARAM_BASE + i, 0.f, 1.f, 0.f, std::string(kVoiceNames[i]) + " reverb send");
			configInput(TRIG_INPUT_BASE + i, std::string(kVoiceNames[i]) + " trigger");
			configOutput(AUDIO_OUTPUT_BASE + i, std::string(kVoiceNames[i]) + " audio");
		}
		configParam(RVB_TIME_PARAM,   0.f, 1.f, 0.00f, "Reverb time");
		configParam(RVB_DAMP_PARAM,   0.f, 1.f, 0.85f, "Reverb absorption (damping)");
		configParam(RVB_DIFF_PARAM,   0.f, 1.f, 0.68f, "Reverb diffusion");
		configParam(RVB_AMOUNT_PARAM, 0.f, 1.f, 1.00f, "Reverb amount");
		configOutput(REV_WET_L_OUTPUT, "Reverb wet L");
		configOutput(REV_WET_R_OUTPUT, "Reverb wet R");
		configOutput(SIDECHAIN_OUTPUT, "Sidechain out");
		configOutput(GROOVE_CV_OUTPUT, "Groove CV out");
		configParam(DUCK_DEPTH_PARAM, 0.f, 1.f, 0.f,  "Duck depth");
		configParam(DUCK_PUMP_PARAM,  0.f, 1.f, 0.4f, "Duck pump — convex quadratic (0) to concave (1): shapes how long the duck tail lingers");
		configSwitch(DUCK_ON_PARAM,   0.f, 1.f, 0.f,  "Kick ducking", {"Off", "On"});
		configParam(KICK_CURVE_PARAM,  0.f, 1.f, 0.5f, "Kick curve — 808 fast pitch drop (0) to 909 slow sweep (1)");
		configParam(KICK_RUMBLE_PARAM, 0.f, 1.f, 0.f,  "Kick rumble — delayed sub-bass bloom that swells after the transient");
		configParam(KICK_ATTACK_TONE_PARAM, 0.f, 1.f, 0.5f, "Kick attack tone — dark beater thud (0) to bright click (1)");
		configParam(KICK_PITCH_DEPTH_PARAM, 0.f, 1.f, 0.5f, "Kick pitch depth — shallow thud (0) to deep falling sweep (1)");

		configInput(ACCENT_INPUT, "Accent bus (poly 0-10V, Trigonomicon-compatible)");
		configInput(DECIMATE_CV_INPUT, "Decimate CV");
		configInput(DRIVE_CV_INPUT, "Drive CV");
		configInput(COLOR_CV_INPUT, "Color CV");
		configInput(BODY_CV_INPUT, "Body CV");
		configInput(PUNCH_CV_INPUT, "Punch CV");
		configInput(SNAP_CV_INPUT, "Snap CV");
		configInput(NOISE_CV_INPUT, "Noise CV");
		configInput(VARIATION_CV_INPUT, "Variation CV");
		configInput(MATERIAL_CV_INPUT, "Material CV (0-10V additive)");
		configInput(TRANSIENT_CV_INPUT, "Transient CV (0-10V additive)");
		configInput(RVB_DAMP_CV_INPUT, "Reverb damp CV (0-10V additive)");
		configInput(RVB_DIFF_CV_INPUT, "Reverb diffusion CV (0-10V additive)");
		configInput(RVB_AMOUNT_CV_INPUT, "Reverb amount CV (0-10V additive)");
		configInput(RVB_TIME_CV_INPUT, "Reverb time CV (0-10V additive)");
		configInput(SNARE_CUT_CV_INPUT, "Snare cut CV");
		configInput(KICK_LENGTH_CV_INPUT, "Kick length CV");

#ifndef METAMODULE
		getTR909KickSamples();
		getTR909RideSample();
		getTR909ClapSample();
		getTR909RimSample();
		reverb_.Init(reverb_buffer_);
#else
		if (variant_ == FerroklastVariant::MetaModule) {
			getTR909RideSample();
		}
#endif
		if (variant_ == FerroklastVariant::MetaModule) {
			getMMClosedHatSoftSample();
			getMMClosedHatMediumSample();
			getMMClosedHatHardSample();
			getMMClapSoftSample();
			getMMClapMediumSample();
			getMMClapHardSample();
		}
		onReset();
	}

	bool isMetaModuleVariant() const {
		return variant_ == FerroklastVariant::MetaModule;
	}

	int resolveTriggerLane(int sourceLaneIndex) const {
		if (!isMetaModuleVariant()) {
			return sourceLaneIndex;
		}
		if (sourceLaneIndex == 4) {
			return 3;
		}
		if (sourceLaneIndex == 7) {
			return -1;
		}
		return sourceLaneIndex;
	}

	int resolveOutputLane(int outputLaneIndex) const {
		if (!isMetaModuleVariant()) {
			return outputLaneIndex;
		}
		if (outputLaneIndex == 4) {
			return 3;
		}
		if (outputLaneIndex == 7) {
			return -1;
		}
		return outputLaneIndex;
	}

	bool allowsOneShotSample(MachineId machine) const {
#ifdef METAMODULE
		return isMetaModuleVariant() && machine == MACHINE_RIDE;
#else
		return machine == MACHINE_RIDE || machine == MACHINE_CLAP || machine == MACHINE_RIM;
#endif
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		// These coefficients depend only on sample rate — precompute once rather than
		// recomputing every control-rate block (which would call std::exp ~3000×/sec).
		grooveTextureDecayCoef_ = std::exp(-1.f / (0.140f * e.sampleRate));
		grooveAccentDecayCoef_  = std::exp(-1.f / (0.220f * e.sampleRate));
		grooveOutputSlewCoeff_  = 1.f - std::exp(-1.f / (0.0025f * e.sampleRate));
		sampleRateCache = e.sampleRate;
	}

	void onReset() override {
		for (int i = 0; i < kNumVoices; ++i) {
			lanes[i].reset();
			lanes[i].noiseState = 0x12345678u ^ (uint32_t)(0x9E3779B9u * (i + 1));
		}
		macroSmooth[0] = params[PUNCH_PARAM].getValue();
		macroSmooth[1] = params[BODY_PARAM].getValue();
		macroSmooth[2] = params[COLOR_PARAM].getValue();
		macroSmooth[3] = params[SNAP_PARAM].getValue();
		macroSmooth[4] = params[NOISE_PARAM].getValue();
		macroSmooth[5] = params[CHAOS_PARAM].getValue();
		macroSmooth[6] = params[MATERIAL_PARAM].getValue();
		macroSmooth[7] = params[TRANSIENT_PARAM].getValue();
	#ifndef METAMODULE
		kickRumbleSmear_.reset();
	#endif
		finalDriveSmooth = params[FINAL_DRIVE_PARAM].getValue();
		snareCutSmooth = params[SNARE_CUT_PARAM].getValue();
		snareResSmooth = params[SNARE_RES_PARAM].getValue();
		snareDriveSmooth = params[SNARE_DRIVE_PARAM].getValue();
		grooveCvHeld_ = 0.18f;
		grooveCvOutput_ = 0.18f;
		grooveMetalMemory_ = 0.f;
		grooveAccentMemory_ = 0.f;
	}

	void updateGrooveCvFromHit(MachineId machine, float accent, float variationCentered, float decay, float body, float color, float chaos, float material) {
		float colorNorm = rack::math::clamp(color * 0.5f, 0.f, 1.f);
		float primaryOffset = (accent - 0.5f) * 0.16f + variationCentered * 0.10f;
		float macroOffset = (body - 0.5f) * 0.08f + (material - 0.5f) * 0.08f + chaos * 0.04f;

		switch (machine) {
			case MACHINE_METAL:
				grooveMetalMemory_ = rack::math::clamp(grooveMetalMemory_ * 0.60f + 0.08f + accent * 0.16f + colorNorm * 0.10f + decay * 0.04f, 0.f, 1.f);
				grooveAccentMemory_ = rack::math::clamp(grooveAccentMemory_ * 0.82f + accent * 0.06f, 0.f, 1.f);
				return;
			case MACHINE_CHAOS1:
				grooveMetalMemory_ = rack::math::clamp(grooveMetalMemory_ * 0.58f + 0.12f + accent * 0.18f + colorNorm * 0.12f + decay * 0.05f, 0.f, 1.f);
				grooveAccentMemory_ = rack::math::clamp(grooveAccentMemory_ * 0.80f + accent * 0.07f, 0.f, 1.f);
				return;
			case MACHINE_RIDE:
				grooveMetalMemory_ = rack::math::clamp(grooveMetalMemory_ * 0.54f + 0.18f + accent * 0.20f + colorNorm * 0.18f + decay * 0.06f, 0.f, 1.f);
				grooveAccentMemory_ = rack::math::clamp(grooveAccentMemory_ * 0.78f + accent * 0.08f, 0.f, 1.f);
				return;
			default:
				break;
		}

		float target = grooveCvHeld_;
		switch (machine) {
			case MACHINE_KICK:
				target = 0.12f + accent * 0.10f + decay * 0.05f + grooveMetalMemory_ * 0.10f + macroOffset * 0.45f;
				break;
			case MACHINE_SNARE1:
				target = 0.58f + primaryOffset * 0.55f + grooveMetalMemory_ * 0.18f + colorNorm * 0.06f + chaos * 0.03f;
				break;
			case MACHINE_SNARE2:
				target = 0.48f + primaryOffset * 0.50f + grooveMetalMemory_ * 0.15f + colorNorm * 0.08f;
				break;
			case MACHINE_CLAP:
				target = 0.72f + primaryOffset * 0.45f + grooveMetalMemory_ * 0.12f + colorNorm * 0.05f;
				break;
			case MACHINE_RIM:
				target = 0.84f + primaryOffset * 0.35f - grooveMetalMemory_ * 0.06f + chaos * 0.03f;
				break;
			default:
				return;
		}

		target += grooveAccentMemory_ * 0.12f;
		grooveCvHeld_ = rack::math::clamp(target, 0.05f, 0.95f);
		grooveAccentMemory_ = rack::math::clamp(grooveAccentMemory_ * 0.55f + accent * 0.45f + grooveMetalMemory_ * 0.12f, 0.f, 1.f);
		grooveMetalMemory_ = rack::math::clamp(grooveMetalMemory_ * 0.82f + colorNorm * 0.02f, 0.f, 1.f);
	}

	float readAccent(int lane) {
		if (!inputs[ACCENT_INPUT].isConnected()) {
			return 0.65f;
		}
		int channels = inputs[ACCENT_INPUT].getChannels();
		if (channels <= 1) {
			return clamp01(inputs[ACCENT_INPUT].getVoltage() * 0.1f);
		}
		int channel = lane;
		if (channel >= channels) channel = channels - 1;
		return clamp01(inputs[ACCENT_INPUT].getVoltage(channel) * 0.1f);
	}

	float readMacroCv01(int inputId) {
		if (!inputs[inputId].isConnected()) {
			return 0.f;
		}
		return clamp01(inputs[inputId].getVoltage() * 0.1f);
	}

	float readMacroCv02(int inputId) {
		if (!inputs[inputId].isConnected()) {
			return 0.f;
		}
		return rack::math::clamp(inputs[inputId].getVoltage() * 0.2f, 0.f, 2.f);
	}

	void updateMacros() {
		const int ids[8] = {PUNCH_PARAM, BODY_PARAM, COLOR_PARAM, SNAP_PARAM, NOISE_PARAM, CHAOS_PARAM, MATERIAL_PARAM, TRANSIENT_PARAM};
		const int cvIds[8] = {PUNCH_CV_INPUT, BODY_CV_INPUT, COLOR_CV_INPUT, SNAP_CV_INPUT, NOISE_CV_INPUT, DECIMATE_CV_INPUT, MATERIAL_CV_INPUT, TRANSIENT_CV_INPUT};
		for (int i = 0; i < 8; ++i) {
			float target = params[ids[i]].getValue();
			if (i < 8) {
				if (i == 2) {
					target += readMacroCv02(cvIds[i]);
				} else {
					target += readMacroCv01(cvIds[i]);
				}
			}
			macroSmooth[i] += 0.18f * (target - macroSmooth[i]);
			if (i == 2) {
				macroSmooth[i] = rack::math::clamp(macroSmooth[i], 0.f, 2.f);
			} else if (i == 6) {
				macroSmooth[i] = rack::math::clamp(macroSmooth[i], 0.f, kMaterialMacroMax);
			} else if (i == 7) {
				macroSmooth[i] = rack::math::clamp(macroSmooth[i], 0.f, kTransientMacroMax);
			} else {
				macroSmooth[i] = clamp01(macroSmooth[i]);
			}
		}
		float finalDriveTarget = params[FINAL_DRIVE_PARAM].getValue() + readMacroCv01(DRIVE_CV_INPUT);
		finalDriveSmooth += 0.18f * (finalDriveTarget - finalDriveSmooth);
		finalDriveSmooth = clamp01(finalDriveSmooth);
		float snareCutTarget = params[SNARE_CUT_PARAM].getValue() + readMacroCv01(SNARE_CUT_CV_INPUT);
		snareCutSmooth += 0.18f * (snareCutTarget - snareCutSmooth);
		snareCutSmooth = clamp01(snareCutSmooth);
		float snareResTarget = params[SNARE_RES_PARAM].getValue();
		snareResSmooth += 0.18f * (snareResTarget - snareResSmooth);
		snareResSmooth = clamp01(snareResSmooth);
		float snareDriveTarget = params[SNARE_DRIVE_PARAM].getValue();
		snareDriveSmooth += 0.18f * (snareDriveTarget - snareDriveSmooth);
		snareDriveSmooth = clamp01(snareDriveSmooth);
	}

	void fireLane(int laneIndex, float sampleRate, int sourceLaneIndex = -1) {
		if (sourceLaneIndex < 0) {
			sourceLaneIndex = laneIndex;
		}
		const MachineProfile& profile = kProfiles[sourceLaneIndex];
		LaneState& lane = lanes[laneIndex];

		float punch = macroSmooth[0] * macroSmooth[0] * (3.f - 2.f * macroSmooth[0]);
		float body = macroSmooth[1];
		float color = macroSmooth[2];
		// Snares respond 30% less to color — preserves wire/shell character at high color settings
		if (profile.machine == MACHINE_SNARE1 || profile.machine == MACHINE_SNARE2)
			color *= 0.70f;
		float snap = macroSmooth[3];
		float noiseMacro = macroSmooth[4];
		float chaos = remapChaosMacro(macroSmooth[5]);
		float kickChaos = rack::math::clamp(chaos * 1.75f, 0.f, 1.f);
		float voiceChaos = (profile.machine == MACHINE_KICK) ? kickChaos : chaos;
		float materialRaw = macroSmooth[6];
		float transientRaw = macroSmooth[7];
		float transientVoiceMax = (profile.machine == MACHINE_SNARE1
			|| profile.machine == MACHINE_SNARE2)
			? 1.9f
			: (profile.machine == MACHINE_METAL
			|| profile.machine == MACHINE_CHAOS1)
			? 0.7f
			: kTransientMacroMax;
		transientRaw = rack::math::clamp(transientRaw, 0.f, transientVoiceMax);
		float material = clamp01(materialRaw);
		float materialExtra = rack::math::clamp(materialRaw - 1.f, 0.f, kMaterialMacroMax - 1.f);
		float transientMacro = clamp01(transientRaw);
		float transientExtra = rack::math::clamp(transientRaw - 1.f, 0.f, transientVoiceMax - 1.f);
		float materialCentered = (material - 0.5f) * 2.f + materialExtra * 0.75f;
		float transientCentered = (transientMacro - 0.5f) * 2.f + transientExtra * 0.90f;
		float accent = readAccent(sourceLaneIndex);
		float accentShape = 0.25f + 0.75f * accent * (2.f - accent);
		float tune = params[TUNE_PARAM_BASE + sourceLaneIndex].getValue();
		float level = params[LEVEL_PARAM_BASE + sourceLaneIndex].getValue();
		float decayCv = (profile.machine == MACHINE_KICK) ? readMacroCv01(KICK_LENGTH_CV_INPUT) : 0.f;
		float decay = rack::math::clamp(params[DECAY_PARAM_BASE + sourceLaneIndex].getValue() + decayCv, 0.f, 1.f);
		float variation = rack::math::clamp(params[VARIATION_PARAM_BASE + sourceLaneIndex].getValue() + readMacroCv01(VARIATION_CV_INPUT), 0.f, 1.f);
		float variationCentered = (variation - 0.5f) * 2.f;
		float kickPunch = punch;
		float kickVariationCentered = variationCentered;
		if (profile.machine == MACHINE_KICK && inputs[ACCENT_INPUT].isConnected()) {
			kickPunch = rack::math::clamp(punch + accent * 0.85f, 0.f, 1.f);
			kickVariationCentered = rack::math::clamp(variationCentered + (accent - 0.5f) * 0.90f, -1.f, 1.f);
		}
		float kickCurve  = params[KICK_CURVE_PARAM].getValue();
		float kickRumble = clamp01(params[KICK_RUMBLE_PARAM].getValue());
		float kickAttackTone = params[KICK_ATTACK_TONE_PARAM].getValue();
		float kickPitchDepth = params[KICK_PITCH_DEPTH_PARAM].getValue();
		float kickAttackToneCentered = (kickAttackTone - 0.5f) * 2.f;
		float kickPitchDepthScale = 0.20f + 1.60f * kickPitchDepth;
		float kickVariantMorph = clamp01(kickVariationCentered * 0.5f + 0.5f);
		float kickVariantRound = 1.f - kickVariantMorph;
		float kickColorNorm = rack::math::clamp(color * 0.5f, 0.f, 1.f);
		float kickColorExtra = rack::math::clamp(color - 1.f, 0.f, 1.f);
		float rumbleMaterialGain = 0.75f + 0.75f * material + 0.25f * materialExtra;
		float rumbleFreqRatio = rack::math::clamp(0.58f + kickColorNorm * 0.12f + kickColorExtra * 0.06f, 0.56f, 0.82f);
		float rumbleColorMix = rack::math::clamp(0.03f + kickColorNorm * 0.20f + kickColorExtra * 0.12f, 0.f, 0.40f);
		float rumbleChaosDrive = 0.04f + kickChaos * 0.55f;
		float snareCut = snareCutSmooth;
		float snareRes = snareResSmooth;
		float snareDrive = snareDriveSmooth;

		if (!isMetaModuleVariant() && laneIndex == 3) {
			LaneState& openHat = lanes[4];
			openHat.ampEnv *= 0.12f;
			openHat.noiseEnv *= 0.12f;
			openHat.transientEnv *= 0.25f;
			if (openHat.ampEnv < 1e-4f && openHat.noiseEnv < 1e-4f) {
				openHat.reset();
			}
		}

		// Kick gets its own BODY range so the macro is audible without collapsing the default tuning.
		float kickBodyMacro = rack::math::clamp(0.65f + 0.70f * body, 0.f, 1.35f);
		float bodyForVoice = (profile.machine == MACHINE_KICK) ? kickBodyMacro : body;
		updateGrooveCvFromHit(profile.machine, accent, variationCentered, decay, bodyForVoice, color, voiceChaos, material);

		float freqBase = lerp(profile.baseFreqMin, profile.baseFreqMax, clamp01(bodyForVoice + decay * 0.15f));
		freqBase *= semitoneRatio(tune);
		freqBase = rack::math::clamp(freqBase, 20.f, sampleRate * 0.42f);

		lane.active = true;
		lane.ampEnv = 1.f;
		lane.pitchEnv = 1.f;
		lane.modEnv = 1.f;
		lane.noiseEnv = 1.f;
		lane.machine = profile.machine;
		lane.variation = variation;
		lane.baseFreq = freqBase;
		lane.pitchSweep = profile.pitchSweep * (0.45f + 0.9f * punch) * (0.65f + 0.35f * accentShape);
		lane.modRatio = selectRatio(profile, clamp01(color + decay * 0.12f + sourceLaneIndex * 0.03f));
		lane.modIndex = profile.modIndexBase + profile.modIndexPunch * punch + profile.modIndexAccent * accentShape;
		lane.feedback = rack::math::clamp(voiceChaos * profile.feedbackMax * (1.f + voiceChaos * 1.5f) * (0.55f + 0.45f * accentShape), 0.f, 0.92f);
		lane.instability = rack::math::clamp(voiceChaos * profile.instabilityMax * (1.f + voiceChaos * 2.f) * (0.35f + 0.65f * accentShape), 0.f, 0.5f);
		lane.noiseAmount = rack::math::clamp(noiseMacro * profile.noiseSensitivity + voiceChaos * profile.noiseSensitivity * 0.25f, 0.f, 1.5f);
		lane.cutoff = rack::math::clamp(profile.filterMin + (profile.filterMax - profile.filterMin) * clamp01(snap * 0.6f + color * 0.25f + voiceChaos * 0.15f), 20.f, sampleRate * 0.45f);
		lane.resonance = profile.resonance;
		lane.drive = rack::math::clamp(profile.drive + punch * 0.2f + voiceChaos * 0.8f, 0.f, 1.8f);
		lane.gain = level * (0.35f + 0.65f * accentShape);
		lane.filterMode = profile.filterMode;
		lane.bodyAmount = 0.55f;
		lane.subAmount = 0.f;
		lane.fmAmount = 0.28f;
		lane.noiseBodyAmount = 0.14f;
		lane.noiseBrightAmount = 0.06f;
		lane.noiseToneCoef = 0.08f;
		lane.shellEnv = 0.f;
		lane.shellCoef = decayCoef(0.06f, sampleRate);
		lane.hatToneEnv = 1.f;
		lane.hatToneCoef = 0.999f;
		lane.hatToneMix = 1.f;
		lane.bellAmp = 0.f;
		lane.bellAmpCoef = 0.999f;
		lane.rideBellMix = 0.f;
		lane.rideEqLowState = 0.f;
		lane.rideEqMidState = 0.f;
		lane.rideEqLowCoef = 0.05f;
		lane.rideEqMidCoef = 0.12f;
		lane.rideEqBellCut = 0.f;
		lane.rideEqAirBoost = 0.f;
		lane.useKickSample = false;
		lane.kickCyclePhase = 0.f;
		lane.kickAttackPos = 0.f;
		lane.kickAttackRate = 1.f;
		lane.kickAttackGain = 0.f;
		lane.kickCycleGain = 1.f;
		lane.kickFrame = 0u;
		lane.kickReleaseStartFrame = 0u;
		lane.sampleToneLowState = 0.f;
		lane.sampleToneLowCoef = 0.05f;
		lane.sampleToneLowMix = 1.f;
		lane.sampleToneHighMix = 1.f;
		lane.useOneShotSample = false;
		lane.oneShotSample = nullptr;
		lane.sampleTransientMix = 0.f;
		lane.samplePhase = 0u;
		lane.samplePhaseInc = 0u;
		lane.sampleEndFrame = 0u;
		lane.sampleFadeStartFrame = 0u;
		lane.sampleFadeInv = 0.f;
		lane.shellFreqA = freqBase;
		lane.shellFreqB = freqBase * 1.8f;
		lane.shellMixA = 0.f;
		lane.shellMixB = 0.f;
		lane.shellTriangleMix = 0.f;
		lane.shellBuzzAmount = 0.f;
		lane.snareFatMix = 0.f;
		lane.crushAmount = 0.f;

		// Chaos effects chain: staged activation from chaos knob
		// Bitcrush → Decimation → Bytebeat with crossfades between stages
		lane.chaosAmount = voiceChaos;
		if (isMetaModuleVariant()) {
			lane.chaosBitDepth = 256.f;
			lane.chaosDecimatePeriod = 1 + (int)std::round(voiceChaos * 7.f);
			lane.chaosDecimateCounter = 0;
			lane.chaosHeldSample = 0.f;
			lane.chaosBytebeatMix = 0.f;
		} else {
		{
			// Snares, hats, and clap are more sensitive — use weaker crush/decimate ceilings
			bool isSensitive = (profile.machine == MACHINE_SNARE1
				|| profile.machine == MACHINE_SNARE2
				|| profile.machine == MACHINE_METAL
				|| profile.machine == MACHINE_CHAOS1
				|| profile.machine == MACHINE_CLAP);
			// Bitcrush: fades in 0.10–0.25, fades out 0.30–0.45, fades back in 0.65–0.80
			float crushIn = rack::math::clamp((voiceChaos - 0.10f) * (1.f / 0.15f), 0.f, 1.f);
			float crushOut = 1.f - rack::math::clamp((voiceChaos - 0.30f) * (1.f / 0.15f), 0.f, 1.f);
			float crushReturn = rack::math::clamp((voiceChaos - 0.65f) * (1.f / 0.15f), 0.f, 1.f);
			float crushMix = std::max(crushIn * crushOut, crushReturn);
			// Sensitive voices: 6→4 bits (16 levels min); others: 6→2 bits (4 levels min)
			float crushBitsMin = isSensitive ? 4.f : 2.f;
			float crushBits = lerp(6.f, crushBitsMin, crushMix);
			lane.chaosBitDepth = crushMix > 0.001f ? std::pow(2.f, crushBits) : 256.f;
			// Decimation: fades in 0.25–0.45, fades out 0.55–0.75, fades back in 0.70–0.85
			float decIn = rack::math::clamp((voiceChaos - 0.25f) * (1.f / 0.20f), 0.f, 1.f);
			float decOut = 1.f - rack::math::clamp((voiceChaos - 0.55f) * (1.f / 0.20f), 0.f, 1.f);
			float decMixFirst = decIn * decOut;
			float decMixReturn = rack::math::clamp((voiceChaos - 0.70f) * (1.f / 0.15f), 0.f, 1.f);
			// Sensitive voices: return leg max period 2; others: max period 4
			int decReturnMax = isSensitive ? 2 : 4;
			int decPeriodFirst = 1 + (int)(decMixFirst * 5.f);
			int decPeriodReturn = 1 + (int)(decMixReturn * (float)decReturnMax);
			lane.chaosDecimatePeriod = std::max(decPeriodFirst, decPeriodReturn);
			lane.chaosDecimateCounter = 0;
			lane.chaosHeldSample = 0.f;
			// Bytebeat: fades in 0.50–0.70, max 0.85 (envelope further modulates)
			lane.chaosBytebeatMix = rack::math::clamp((voiceChaos - 0.50f) * (1.f / 0.20f), 0.f, 1.f) * 0.85f;
		}
		}

		lane.hatTuneScale = 1.f;
		for (int i = 0; i < kHatOscCount; ++i) {
			lane.hatPhase[i] = 0.f;
			lane.hatDetune[i] = 1.f;
		}
		// Precompute 1/gainSum for whatever gain set this voice will use.
		{
			const float* gainSet = (profile.machine == MACHINE_RIDE) ? kRideOscGains : kHatOscGains;
			float sum = 0.f;
			for (int i = 0; i < kHatOscCount; ++i) sum += gainSet[i];
			lane.hatGainNorm = 1.f / (sum > 1e-6f ? sum : 1.f);
		}
		lane.ampCoef = decayCoef(lerp(profile.ampDecayMin, profile.ampDecayMax, decay), sampleRate);
		lane.noiseCoef = decayCoef(lerp(profile.noiseDecayMin, profile.noiseDecayMax, clamp01(decay + noiseMacro * 0.2f)), sampleRate);
		lane.pitchCoef = decayCoef(lerp(profile.pitchDecayMin, profile.pitchDecayMax, decay), sampleRate);
		lane.modCoef = decayCoef(lerp(profile.modDecayMin, profile.modDecayMax, clamp01(decay * 0.7f + punch * 0.3f)), sampleRate);
		lane.hatToneCoef = lane.ampCoef;

		// Transient generator initialization (per-voice params + SNAP macro)
		float transAmt = params[TRANS_AMOUNT_PARAM_BASE + sourceLaneIndex].getValue();
		float transType = params[TRANS_TYPE_PARAM_BASE + sourceLaneIndex].getValue();
		float transDecay = params[TRANS_DECAY_PARAM_BASE + sourceLaneIndex].getValue();
		(void)params[TRANS_FM_PARAM_BASE + sourceLaneIndex].getValue(); // knob unused; param kept for patch compat

		lane.transientEnv = 1.f;
		lane.transientPhase = 0.f;
		lane.transientFilterLP = 0.f;
		lane.transientSH = 0.f;
		lane.transientSHCounter = 0;
		lane.lastTransientRaw = 0.f;
		lane.transientType = clamp01(transType + transientCentered * 0.34f);
		lane.quantizeTransientType = false;
		lane.quantizedTransientType = 0;
		if (isMetaModuleVariant()) {
			switch (profile.machine) {
				case MACHINE_METAL:
				case MACHINE_CHAOS1:
				case MACHINE_RIDE:
				case MACHINE_CLAP:
				case MACHINE_RIM:
					lane.quantizeTransientType = true;
					lane.quantizedTransientType = rack::math::clamp((int)(lane.transientType * (float)(kNumTransientTypes - 1) + 0.5f), 0, kNumTransientTypes - 1);
					break;
				default:
					break;
			}
		}
		lane.transientFmRoute = 0.f; // knob unused — param kept for patch compatibility
		// Amount: per-voice trimpot × SNAP macro × profile sensitivity × accent
		lane.transientAmount = rack::math::clamp(transAmt * snap * profile.clickSensitivity * (0.6f + 0.4f * accentShape) * (0.25f + 2.00f * transientMacro + 1.25f * transientExtra), 0.f, 2.4f);
		// Decay: per-voice trimpot modulated by SNAP (higher SNAP = shorter decay)
		float transDecayTime = (0.0005f + 0.015f * transDecay * (1.2f - snap * 0.6f)) * (0.35f + 2.40f * transientMacro + 1.75f * transientExtra);
		lane.transientCoef = decayCoef(transDecayTime, sampleRate);
		// Default transient freq and chirp — overridden per machine below
		lane.transientFreq = 2000.f * (0.65f + 1.10f * transientMacro + 0.55f * transientExtra);
		lane.transientChirpRate = 1.f;
		lane.transientSHPeriod = 4 + (int)(8.f * (1.f - lane.transientType));

#ifndef METAMODULE
		if (profile.machine == MACHINE_KICK) {
			TR909KickSamples& kickSamples = getTR909KickSamples();
			lane.useKickSample = kickSamples.attack.size() >= 2 && kickSamples.cycle.size() >= 2;
			if (lane.useKickSample) {
				float colorNorm = rack::math::clamp(color * 0.5f, 0.f, 1.f);
				float colorExtra = rack::math::clamp(color - 1.f, 0.f, 1.f);
				float attackTune = semitoneRatio(tune * 0.45f + kickVariationCentered * 0.22f);
				lane.kickCyclePhase = wrap01(0.05f + 0.78f * variation + randomBipolar(lane.noiseState) * 0.08f);
				lane.kickAttackPos = rack::math::clamp(lane.transientType * 40.f + kickVariationCentered * 36.f, 0.f, (float)(kickSamples.attack.size() - 2u));
				lane.kickAttackRate = ((float)kickSamples.sampleRate / sampleRate) * attackTune;
				lane.kickAttackGain = (0.20f + snap * 0.32f + kickPunch * 0.16f + transAmt * 0.72f + std::fabs(kickVariationCentered) * 0.26f + colorNorm * 0.18f + colorExtra * 0.08f) * (0.70f + 0.90f * transientMacro);
				lane.kickCycleGain = (0.82f + bodyForVoice * 0.54f + decay * 0.14f - kickVariationCentered * 0.24f - colorNorm * 0.12f) * (0.82f + 0.36f * material);
				lane.kickAttackGain *= lerp(0.82f, 1.28f, kickVariantMorph);
				lane.kickCycleGain *= lerp(1.14f, 0.86f, kickVariantMorph);
				lane.kickFrame = 0u;
				lane.kickReleaseStartFrame = (uint32_t)std::round(sampleRate * (0.0005f + decay * 0.040f + variation * 0.005f + kickVariantRound * 0.010f));
				float kickToneCutoff = 180.f + colorNorm * 2200.f + colorExtra * 1200.f;
				kickToneCutoff *= rack::math::clamp(1.f + kickAttackToneCentered * 0.90f, 0.25f, 2.20f);
				lane.sampleToneLowCoef = rack::math::clamp(kickToneCutoff / (kickToneCutoff + sampleRate), 0.001f, 0.5f);
				lane.sampleToneLowMix = (1.02f - colorNorm * 0.18f - colorExtra * 0.06f) * (0.82f + 0.50f * material);
				lane.sampleToneHighMix = (0.28f + snap * 0.16f + colorNorm * 0.74f + colorExtra * 0.32f) * (1.10f - 0.30f * material);
				lane.sampleToneLowMix *= lerp(1.18f, 0.86f, kickVariantMorph);
				lane.sampleToneHighMix *= lerp(0.76f, 1.36f, kickVariantMorph);
				lane.kickAttackGain *= rack::math::clamp(1.f + kickAttackToneCentered * 0.18f, 0.70f, 1.30f);
				lane.sampleToneLowMix = rack::math::clamp(lane.sampleToneLowMix * rack::math::clamp(1.f - kickAttackToneCentered * 0.28f, 0.60f, 1.35f), 0.f, 2.6f);
				lane.sampleToneHighMix = rack::math::clamp(lane.sampleToneHighMix * rack::math::clamp(1.f + kickAttackToneCentered * 0.55f, 0.55f, 1.85f), 0.f, 2.6f);
				lane.transientFmRoute = 0.f;
				lane.transientAmount = rack::math::clamp(transAmt * (0.28f + 0.52f * snap) * (0.65f + 0.35f * accentShape), 0.f, 1.0f);
				lane.transientAmount *= lerp(0.82f, 1.26f, kickVariantMorph);
				float kickTransDecayTime = (0.0008f + 0.014f * transDecay * (1.05f - snap * 0.20f)) * (0.70f + 1.60f * transientMacro);
				lane.transientCoef = decayCoef(kickTransDecayTime, sampleRate);
				lane.transientAmount = rack::math::clamp(lane.transientAmount * (0.35f + 2.10f * transientMacro + 1.30f * transientExtra), 0.f, 2.4f);
				lane.transientFreq = (850.f + snap * 760.f + kickVariationCentered * 420.f) * (0.70f + 0.90f * transientMacro + 0.60f * transientExtra);
				lane.transientFreq *= rack::math::clamp(1.f + kickAttackToneCentered * 0.45f, 0.55f, 1.55f);
				lane.transientChirpRate = 0.9988f + 0.0007f * snap;
				lane.transientChirpRate = rack::math::clamp(lane.transientChirpRate + kickAttackToneCentered * 0.00020f, 0.9980f, 1.0004f);
			}
		}
#endif

		if (isMetaModuleVariant() && profile.machine == MACHINE_METAL) {
			int hhHitIndex = selectMMClosedHatHitIndex(variation, accent);
			TR909OneShotSample* oneShotSample = getMMClosedHatSample(hhHitIndex);
			lane.oneShotSample = oneShotSample;
			lane.useOneShotSample = oneShotSample && oneShotSample->data.size() >= 2;
			if (lane.useOneShotSample) {
				float colorNorm = rack::math::clamp(color * 0.5f, 0.f, 1.f);
				float colorExtra = rack::math::clamp(color - 1.f, 0.f, 1.f);
				float playbackSemitones = tune * 0.12f;
				float tailFraction = rack::math::clamp(0.08f + 0.20f * decay + 0.12f * accent, 0.06f, 0.42f);
				float fadeTimeSeconds = 0.005f + 0.004f * decay;
				lane.sampleTransientMix = 0.f;
				lane.transientEnv = 0.f;
				lane.transientAmount = 0.f;
				lane.transientFmRoute = 0.f;
				lane.lastTransientRaw = 0.f;
				lane.sampleToneLowCoef = rack::math::clamp((2200.f + colorNorm * 4200.f + colorExtra * 1800.f) / (2200.f + colorNorm * 4200.f + colorExtra * 1800.f + sampleRate), 0.001f, 0.5f);
				lane.sampleToneLowMix = 0.86f - colorNorm * 0.18f;
				lane.sampleToneHighMix = 1.08f + colorNorm * 0.72f + colorExtra * 0.18f;
				lane.gain *= 1.45f + 0.18f * accent;

				float playbackRatio = ((float)oneShotSample->sampleRate / sampleRate) * semitoneRatio(playbackSemitones);
				playbackRatio = rack::math::clamp(playbackRatio, 0.25f, 4.f);
				uint32_t frames = (uint32_t)oneShotSample->data.size();
				lane.samplePhase = 0u;
				lane.samplePhaseInc = (uint32_t)std::max(1.f, std::round(playbackRatio * 65536.f));
				lane.sampleEndFrame = std::max<uint32_t>(2u, (uint32_t)std::round((frames - 1u) * tailFraction));
				uint32_t fadeFrames = std::min<uint32_t>((uint32_t)(fadeTimeSeconds * oneShotSample->sampleRate), lane.sampleEndFrame - 1u);
				lane.sampleFadeStartFrame = lane.sampleEndFrame > fadeFrames ? lane.sampleEndFrame - fadeFrames : 0u;
				lane.sampleFadeInv = fadeFrames > 0u ? 1.f / (float)fadeFrames : 0.f;
			}
		}

		if (profile.machine == MACHINE_RIDE || profile.machine == MACHINE_CLAP || profile.machine == MACHINE_RIM) {
			TR909OneShotSample* oneShotSample = nullptr;
			if (isMetaModuleVariant() && profile.machine == MACHINE_CLAP) {
				oneShotSample = getMMClapSample(selectMMClapHitIndex(variation));
			} else if (allowsOneShotSample(profile.machine)) {
				oneShotSample = getTR909OneShotSample(profile.machine);
			}
			lane.oneShotSample = oneShotSample;
			lane.useOneShotSample = oneShotSample && oneShotSample->data.size() >= 2;
			if (lane.useOneShotSample) {
				float colorNorm = rack::math::clamp(color * 0.5f, 0.f, 1.f);
				float colorExtra = rack::math::clamp(color - 1.f, 0.f, 1.f);
				float playbackSemitones = tune * 0.16f;
				float startFraction = 0.f;
				float tailFraction = 0.20f + 0.80f * decay;
				float fadeTimeSeconds = 0.018f;
				lane.sampleTransientMix = 0.f;

				switch (profile.machine) {
					case MACHINE_RIDE:
						playbackSemitones += variationCentered * 0.05f;
						startFraction = 0.f;
						fadeTimeSeconds = 0.018f;
						lane.sampleTransientMix = 0.f;
						lane.transientEnv = 0.f;
						lane.transientAmount = 0.f;
						lane.transientFmRoute = 0.f;
						lane.lastTransientRaw = 0.f;
						lane.sampleToneLowCoef = rack::math::clamp((1400.f + colorNorm * 6200.f + colorExtra * 2200.f) / (1400.f + colorNorm * 6200.f + colorExtra * 2200.f + sampleRate), 0.001f, 0.5f);
						lane.sampleToneLowMix = 2.80f - colorNorm * 0.70f - colorExtra * 0.20f;
						lane.sampleToneHighMix = 1.40f + colorNorm * 2.40f + colorExtra * 1.00f;
						// Ride sample is normalised to ~±1.0; synthesised voices peak much higher.
						// Override gain to compensate: 2.0x so it sits level with kick/snare at unity.
						lane.gain *= 2.0f;
						break;
					case MACHINE_CLAP:
						if (isMetaModuleVariant()) {
							playbackSemitones += tune * -0.06f;
							startFraction = 0.f;
							tailFraction = rack::math::clamp(0.18f + 0.32f * decay, 0.16f, 0.52f);
							fadeTimeSeconds = 0.006f + 0.004f * decay;
							lane.sampleTransientMix = 0.f;
							lane.transientEnv = 0.f;
							lane.transientAmount = 0.f;
							lane.transientFmRoute = 0.f;
							lane.lastTransientRaw = 0.f;
							lane.sampleToneLowCoef = rack::math::clamp((450.f + colorNorm * 3500.f + colorExtra * 1600.f) / (450.f + colorNorm * 3500.f + colorExtra * 1600.f + sampleRate), 0.001f, 0.5f);
							lane.sampleToneLowMix = 1.00f - colorNorm * 0.28f - colorExtra * 0.08f;
							lane.sampleToneHighMix = 0.52f + colorNorm * 1.00f + colorExtra * 0.26f;
							lane.gain *= 1.35f;
						} else {
							playbackSemitones += variationCentered * 0.35f;
							startFraction = rack::math::clamp(0.012f + std::max(0.f, variationCentered) * 0.018f, 0.f, 0.05f);
							tailFraction = 0.30f + 0.70f * decay;
							fadeTimeSeconds = 0.012f;
							lane.sampleTransientMix = 0.12f + transAmt * 0.48f + snap * 0.12f;
							lane.transientAmount = rack::math::clamp(transAmt * (0.24f + 0.60f * snap) * (0.70f + 0.30f * accentShape), 0.f, 1.10f);
							lane.transientCoef = decayCoef(0.0010f + 0.018f * transDecay, sampleRate);
							lane.transientFreq = 1600.f + snap * 1400.f + variationCentered * 500.f;
							lane.transientChirpRate = 0.9993f + 0.0005f * snap;
							lane.sampleToneLowCoef = rack::math::clamp((300.f + colorNorm * 4200.f + colorExtra * 1800.f) / (300.f + colorNorm * 4200.f + colorExtra * 1800.f + sampleRate), 0.001f, 0.5f);
							lane.sampleToneLowMix = 1.08f - colorNorm * 0.40f - colorExtra * 0.12f;
							lane.sampleToneHighMix = 0.18f + colorNorm * 1.20f + colorExtra * 0.42f;
						}
						break;
					case MACHINE_RIM:
						playbackSemitones += variationCentered * 0.55f;
						startFraction = rack::math::clamp(0.004f + std::max(0.f, variationCentered) * 0.024f, 0.f, 0.08f);
						tailFraction = 0.38f + 0.62f * decay;
						fadeTimeSeconds = 0.008f;
						lane.sampleTransientMix = 0.20f + transAmt * 0.55f + snap * 0.18f;
						lane.transientAmount = rack::math::clamp(transAmt * (0.34f + 0.62f * snap) * (0.70f + 0.30f * accentShape), 0.f, 1.20f);
						lane.transientCoef = decayCoef(0.0007f + 0.010f * transDecay, sampleRate);
						lane.transientFreq = 2400.f + snap * 1800.f + variationCentered * 900.f;
						lane.transientChirpRate = 0.9990f + 0.0006f * snap;
						lane.sampleToneLowCoef = rack::math::clamp((700.f + colorNorm * 5200.f + colorExtra * 2400.f) / (700.f + colorNorm * 5200.f + colorExtra * 2400.f + sampleRate), 0.001f, 0.5f);
						lane.sampleToneLowMix = 1.02f - colorNorm * 0.24f - colorExtra * 0.10f;
						lane.sampleToneHighMix = 0.30f + colorNorm * 1.10f + colorExtra * 0.36f;
						break;
					default:
						break;
				}

				float playbackRatio = ((float)oneShotSample->sampleRate / sampleRate) * semitoneRatio(playbackSemitones);
				playbackRatio = rack::math::clamp(playbackRatio, 0.125f, 4.f);
				uint32_t frames = (uint32_t)oneShotSample->data.size();
				uint32_t startFrame = std::min<uint32_t>((uint32_t)std::round((frames - 2u) * startFraction), frames - 2u);
				lane.samplePhase = startFrame << 16;
				lane.samplePhaseInc = (uint32_t)std::max(1.f, std::round(playbackRatio * 65536.f));
				lane.sampleEndFrame = std::max(startFrame + 2u, (uint32_t)std::round((frames - 1u) * tailFraction));
				uint32_t fadeFrames = std::min<uint32_t>((uint32_t)(fadeTimeSeconds * oneShotSample->sampleRate), lane.sampleEndFrame - startFrame - 1u);
				lane.sampleFadeStartFrame = lane.sampleEndFrame > fadeFrames ? lane.sampleEndFrame - fadeFrames : startFrame;
				lane.sampleFadeInv = fadeFrames > 0u ? 1.f / (float)fadeFrames : 0.f;
			}
		}

		if (profile.machine == MACHINE_KICK) {
			float rumbleOpen = smoothstep01((kickRumble - 0.02f) * (1.f / 0.22f));
			float rumbleCurve = rumbleOpen * rumbleOpen;
			float rumbleDriveStage = smoothstep01((kickRumble - 0.40f) * (1.f / 0.60f));
			float rumbleExtreme = smoothstep01((kickRumble - 0.72f) * (1.f / 0.28f));
			float rumbleDensity = clamp01(rumbleCurve + rumbleDriveStage * 0.55f + rumbleExtreme * 0.35f);
			lane.rumbleAmt = (0.38f * rumbleOpen + 1.55f * rumbleCurve + 1.70f * rumbleDriveStage + 2.20f * rumbleExtreme * rumbleExtreme) * rumbleMaterialGain;
			lane.rumbleAttCoef = 0.00016f + rumbleOpen * 0.00028f + rumbleDriveStage * 0.00042f + rumbleExtreme * 0.00072f;
			float rumbleTrimEnable = smoothstep01((transDecay - 0.02f) * (1.f / 0.10f));
			float rumbleTailScale = 0.f;
			if (transDecay < 0.5f) {
				float tailT = transDecay * 2.f;
				rumbleTailScale = lerp(0.14f, 0.42f, tailT);
			}
			else {
				float tailT = (transDecay - 0.5f) * 2.f;
				rumbleTailScale = lerp(0.42f, 0.82f, tailT);
			}
			lane.rumbleAmt *= rumbleTrimEnable;
			rumbleTailScale *= rumbleTrimEnable;
			lane.rumbleDecCoef = decayCoef((0.22f + decay * (0.50f + 0.55f * rumbleOpen) + 0.35f * rumbleDriveStage) * (0.72f + 0.66f * material + 0.28f * rumbleExtreme) * rumbleTailScale, sampleRate);
			lane.rumbleDecaying = false;
			lane.rumbleFreqRatio = rack::math::clamp(rumbleFreqRatio - 0.04f * rumbleDriveStage - 0.03f * rumbleExtreme, 0.48f, 0.82f);
			lane.rumbleColorMix = rack::math::clamp(rumbleColorMix + 0.08f * rumbleOpen + 0.20f * rumbleDriveStage + 0.18f * rumbleExtreme, 0.02f, 0.78f);
			lane.rumbleChaosDrive = rack::math::clamp(rumbleChaosDrive + 0.10f * rumbleOpen + 0.26f * rumbleDriveStage + 0.26f * rumbleExtreme, 0.04f, 1.f);
			lane.rumbleSubCoef = onePoleCoef(lerp(165.f, 86.f, rumbleDensity), sampleRate);
			lane.rumbleBodyCoef = onePoleCoef(lerp(320.f, 145.f, rumbleDensity), sampleRate);
			lane.rumbleCleanup = 0.04f + 0.28f * rumbleDriveStage + 0.24f * rumbleExtreme;
			lane.rumbleDuckDepth = 0.08f + 0.18f * rumbleOpen + 0.24f * rumbleDriveStage + 0.16f * rumbleExtreme;
			lane.rumbleOutSlew = 0.0045f + 0.012f * rumbleOpen + 0.014f * rumbleDriveStage + 0.014f * rumbleExtreme;
			lane.rumblePostDrive = 0.04f + 0.10f * rumbleOpen + 0.28f * rumbleDriveStage + 0.44f * rumbleExtreme;
			lane.rumbleDelaySamples = 0u;
			lane.rumbleElapsedSamples = 0u;
			float rumbleOnsetTime = 0.004f + 0.003f * rumbleDriveStage + 0.006f * rumbleExtreme;
			lane.rumbleOnsetCoef = 1.f - decayCoef(rumbleOnsetTime, sampleRate);
			lane.rumbleOnsetEnv = 0.f;
			lane.rumbleSendLowState = 0.f;
			lane.rumbleSendLowCoef = onePoleCoef(lerp(150.f, 90.f, rumbleDriveStage + rumbleExtreme * 0.35f), sampleRate);
			lane.rumbleMaskStart = lerp(kRumbleMaskStart + 0.02f, 0.08f, rumbleDriveStage);
			lane.rumbleMaskSpan = lerp(kRumbleMaskSpan + 0.03f, 0.08f, rumbleDriveStage);
			if (kickRumble <= 0.001f) {
				lane.rumbleEnv = 0.f;
				lane.rumbleOut = 0.f;
				lane.rumbleDecaying = false;
				lane.rumbleDelaySamples = 0u;
				lane.rumbleElapsedSamples = 0u;
				lane.rumbleOnsetEnv = 0.f;
				lane.rumbleSendLowState = 0.f;
				lane.rumbleSubState = 0.f;
				lane.rumbleBodyState = 0.f;
				lane.rumbleSend = 0.f;
			}
#ifndef METAMODULE
			kickRumbleSmear_.configure(sampleRate, kickRumble, material, kickChaos, bodyForVoice, color);
#endif
		}

		switch (profile.machine) {
			case MACHINE_KICK:
				if (lane.useKickSample) {
					lane.baseFreq = 53.f * semitoneRatio(tune * 0.80f + kickVariationCentered * 1.10f);
					lane.pitchSweep = (2.2f + kickPunch * 3.2f + snap * 0.55f + std::fabs(kickVariationCentered) * 1.50f) * kickPitchDepthScale;
					lane.drive = rack::math::clamp(0.04f + kickPunch * 0.14f + color * 0.08f + kickChaos * 0.10f + std::fabs(kickVariationCentered) * 0.10f, 0.f, 0.56f);
					lane.ampCoef = decayCoef(0.002f + decay * 0.200f + std::max(0.f, kickVariationCentered) * 0.040f, sampleRate);
					{
						float pBase = lerp(0.006f, 0.030f, kickCurve);
						float pDcy  = lerp(0.032f, 0.090f, kickCurve);
						lane.pitchCoef = decayCoef(pBase + pDcy * decay + kickPunch * 0.010f + std::max(0.f, -kickVariationCentered) * 0.010f, sampleRate);
					}
					lane.noiseCoef = 1.f;
					lane.modCoef = 1.f;
					lane.filter.reset();
					lane.dcBlock.reset();
					break;
				}
					lane.modRatio = rack::math::clamp(lerp(0.5f, 1.08f, clamp01(color * 0.45f + kickPunch * 0.15f)) + kickVariationCentered * 0.22f, 0.35f, 1.6f);
					lane.modIndex = 0.05f + kickPunch * 0.85f + accentShape * 0.12f + std::fabs(kickVariationCentered) * 0.30f;
				lane.feedback = kickChaos * 0.04f;
				lane.instability = kickChaos * 0.01f;
				lane.bodyAmount = rack::math::clamp((0.62f + 0.38f * bodyForVoice) * lerp(1.18f, 0.84f, kickVariantMorph), 0.f, 1.20f);
				lane.subAmount = rack::math::clamp((0.10f + 0.26f * bodyForVoice) * lerp(1.22f, 0.80f, kickVariantMorph), 0.f, 0.60f);
				lane.fmAmount = 0.06f + 0.18f * kickPunch + 0.08f * color;
				lane.noiseAmount = 0.01f + noiseMacro * 0.04f;
				lane.noiseBodyAmount = 0.02f;
				lane.noiseBrightAmount = (0.02f + snap * 0.04f) * lerp(0.78f, 1.42f, kickVariantMorph);
				lane.noiseToneCoef = 0.015f + 0.050f * variation;
				lane.cutoff = rack::math::clamp(480.f + snap * 1800.f + color * 1100.f + kickVariationCentered * 700.f + kickVariantMorph * 700.f - kickVariantRound * 180.f, 80.f, sampleRate * 0.2f);
				lane.cutoff = rack::math::clamp(lane.cutoff + kickAttackToneCentered * 900.f, 80.f, sampleRate * 0.24f);
				lane.resonance = 0.10f;
				lane.drive = rack::math::clamp(0.14f + kickPunch * 0.16f + kickChaos * 0.06f + kickVariantMorph * 0.08f, 0.f, 0.5f);
				lane.ampCoef = decayCoef(0.012f + decay * 0.190f + bodyForVoice * 0.030f, sampleRate);
				lane.modCoef = decayCoef(0.010f + 0.028f * (1.f - decay), sampleRate);
				lane.noiseCoef = decayCoef(0.006f + 0.018f * snap, sampleRate);
				{
				float pBase = lerp(0.008f, 0.018f, kickCurve);
				float pDcy  = lerp(0.020f, 0.064f, kickCurve);
				lane.pitchCoef = decayCoef(pBase + pDcy * decay, sampleRate);
			}
				lane.pitchSweep *= kickPitchDepthScale;
				lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * rack::math::clamp(1.f - kickAttackToneCentered * 0.20f, 0.65f, 1.30f), 0.f, 0.20f);
				lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * rack::math::clamp(1.f + kickAttackToneCentered * 0.70f, 0.45f, 1.90f), 0.f, 0.18f);
				lane.transientAmount *= lerp(0.82f, 1.24f, kickVariantMorph);
				// Kick transient: low chirp for beater click
				lane.transientFreq = 800.f + snap * 600.f + kickVariationCentered * 260.f;
				lane.transientFreq *= rack::math::clamp(1.f + kickAttackToneCentered * 0.40f, 0.55f, 1.50f);
				lane.transientChirpRate = 0.9985f + 0.001f * snap;
				lane.transientChirpRate = rack::math::clamp(lane.transientChirpRate + kickAttackToneCentered * 0.00020f, 0.9980f, 1.0004f);
				break;

			case MACHINE_SNARE1:
				lane.baseFreq = 180.f * semitoneRatio(tune * 0.12f);
					lane.modRatio = 1.0f + clamp01(color) * 0.70f + variationCentered * 0.30f;
					lane.modIndex = 0.10f + clamp01(color) * 0.70f + std::fabs(variationCentered) * 0.35f;
				lane.bodyAmount = 0.72f + bodyForVoice * 0.10f;
				lane.subAmount = 0.06f + bodyForVoice * 0.06f;
				lane.fmAmount = 0.05f + clamp01(color) * 0.24f;
				lane.noiseBodyAmount = 0.18f + noiseMacro * 0.20f + bodyForVoice * 0.10f;
				lane.noiseBrightAmount = 0.48f + noiseMacro * 0.32f + snap * 0.08f;
					lane.noiseToneCoef = 0.030f + 0.070f * variation;
					lane.cutoff = rack::math::clamp(600.f + snareCut * 3600.f - bodyForVoice * 160.f + accentShape * 1800.f + clamp01(color) * 500.f + variationCentered * 700.f, 300.f, sampleRate * 0.40f);
				lane.filterMode = FILTER_BP;
				lane.resonance = rack::math::clamp(0.08f + snareRes * 0.82f, 0.f, 0.95f);
				lane.snareFatMix = 1.f - snareCut;
				lane.drive = rack::math::clamp(0.14f + punch * 0.06f + snareDrive * 0.60f, 0.f, 0.90f);
				lane.crushAmount = rack::math::clamp((noiseMacro - 0.5f) * 2.f, 0.f, 1.f);
				lane.pitchSweep = 0.54f + punch * 0.12f;
				lane.pitchCoef = decayCoef(0.010f + punch * 0.005f, sampleRate);
				lane.ampCoef = decayCoef(0.001f + decay * 0.050f + bodyForVoice * 0.014f, sampleRate);
				lane.noiseCoef = decayCoef(0.004f + decay * 0.179f + bodyForVoice * 0.030f, sampleRate);
				lane.shellEnv = 0.f;
				lane.shellCoef = lane.ampCoef;
					lane.shellFreqA = 176.f * semitoneRatio(tune * 0.10f + variationCentered * 2.0f) * (1.f + clamp01(color) * 0.18f);
					lane.shellFreqB = lane.shellFreqA * (1.72f + 0.28f * variation);
				lane.shellMixA = 0.65f;
				lane.shellMixB = 0.42f * (1.f - 0.40f * (1.f - snareCut));
					lane.shellTriangleMix = 0.04f + 0.20f * variation + clamp01(color) * 0.09f;
				lane.shellBuzzAmount = 0.06f + bodyForVoice * 0.10f;
				// Snare 1 transient: mid-frequency for stick articulation
					lane.transientFreq = 2000.f + snap * 1200.f + variationCentered * 550.f;
				lane.transientChirpRate = 0.9992f;
				break;

			case MACHINE_SNARE2:
				// TR-808-style snare: two near-pure-sine "bridge circuit" tones + BP-filtered noise.
				// Tone 1: ~180 Hz (body thud). Tone 2: 1.83x = ~330 Hz (inharmonic upper mode).
				// baseFreq matched to SNARE1 (180 Hz) so both snares are tuned together.
				// Noise: white noise through BP ~1-5kHz = the characteristic 808 wire/snap buzz.
				// Ref: CSound: aMod1 oscil .4, 178, 1; aMod2 oscil .2, 326, 1; butbp noise, 5000, 1
				lane.baseFreq = 180.f * semitoneRatio(tune * 0.15f);
				// No FM: 808 body tones are near-pure sine from bridge circuits, not FM operators
				lane.modRatio = 1.0f;
				lane.modIndex = 0.0f;
				lane.fmAmount = 0.0f;
				// Shell oscillators = bridge circuit tones (pure sine via shellTriangleMix=0)
				lane.shellFreqA      = lane.baseFreq;                                               // lower mode ~180 Hz
				lane.shellFreqB      = lane.baseFreq * (1.83f + variationCentered * 0.08f);         // upper mode ~330 Hz
				lane.shellMixA       = 0.55f + bodyForVoice * 0.08f;                                // dominant lower tone
				lane.shellMixB       = 0.22f + bodyForVoice * 0.06f + clamp01(color) * 0.10f;      // upper mode; COLOR shifts balance
				lane.shellTriangleMix = 0.0f;   // pure sine waveform — authentic 808 bridge character
				lane.shellBuzzAmount  = 0.0f;   // noise stays completely out of the tone pathway
				lane.shellEnv        = 0.f;
				lane.shellCoef       = lane.ampCoef;
				lane.bodyAmount = 0.38f + bodyForVoice * 0.06f;
				lane.subAmount  = 0.0f;
				// Noise (snappy): noiseToneCoef very small -> noiseBright ≈ white noise.
				// BP filter at 1-5kHz shapes it into the 808 wire buzz character.
				// noiseBrightAmount boosted: BP filter passes ~8% of white noise energy,
				// so raw amount must be high to compete with the body sine tones.
				lane.noiseBodyAmount   = 0.04f + noiseMacro * 0.06f;
				lane.noiseBrightAmount = 1.60f + noiseMacro * 0.80f + snap * 0.40f;
				lane.noiseToneCoef     = 0.004f + 0.008f * variation;   // tight LP -> noiseBright ≈ white
				// snareCut = "SNAPPY brightness": higher = crispier high-freq crack
				lane.cutoff = rack::math::clamp(
					1200.f + snareCut * 3600.f + accentShape * 400.f + snap * 300.f + variationCentered * 300.f,
					400.f, sampleRate * 0.42f);
				lane.filterMode  = FILTER_BP;
				// High damping (wide BP) is critical: at resonance=0.75, BW = 0.75*cutoff.
				// Low resonance would give Q=20+ and pass <1% of noise energy — inaudible.
				lane.resonance   = rack::math::clamp(0.70f + snareRes * 0.20f, 0.f, 0.95f);
				lane.snareFatMix = 0.0f;    // clean tone/noise separation — no fat body mix
				lane.crushAmount = 0.0f;
				lane.drive = rack::math::clamp(0.05f + punch * 0.08f + snareDrive * 0.45f, 0.f, 0.80f);
				// Near-zero pitch sweep: 808 bridge tones are essentially constant pitch
				lane.pitchSweep = 0.09f + punch * 0.05f;
				lane.pitchCoef  = decayCoef(0.006f + punch * 0.003f, sampleRate);
				// Tone decay: DECAY + BODY control body length
				lane.ampCoef   = decayCoef(0.020f + decay * 0.160f + bodyForVoice * 0.040f, sampleRate);
				// Snappy (noise) decay: always shorter than tone body
				// snareCut = 0 -> tight snap; snareCut = 1 -> longer washy wire trail
				lane.noiseCoef = decayCoef(0.010f + snareCut * 0.060f + decay * 0.035f, sampleRate);
				// Transient: sharp downward-chirping stick click
				lane.transientFreq    = 2200.f + snap * 900.f + variationCentered * 450.f;
				lane.transientChirpRate = 0.9985f;
				break;

			case MACHINE_METAL:
				if (lane.useOneShotSample) {
					lane.drive = rack::math::clamp(0.03f + color * 0.05f + chaos * 0.06f, 0.f, 0.24f);
					lane.ampCoef = 1.f;
					lane.noiseCoef = 1.f;
					lane.pitchCoef = 1.f;
					lane.modCoef = 1.f;
					lane.hatToneCoef = 1.f;
					lane.filter.reset();
					lane.dcBlock.reset();
					break;
				}
				lane.baseFreq = 1.f;
				lane.bodyAmount = 0.0f;
				lane.subAmount = 0.f;
				lane.fmAmount = 0.0f;
				lane.noiseAmount = 0.06f + noiseMacro * 0.10f;
				lane.noiseBodyAmount = 0.05f + noiseMacro * 0.04f;
				lane.noiseBrightAmount = 0.18f + noiseMacro * 0.12f;
					lane.noiseToneCoef = 0.070f + 0.120f * variation;
					lane.cutoff = rack::math::clamp(4200.f + clamp01(color) * 2400.f + snap * 900.f + variationCentered * 1400.f, 1800.f, sampleRate * 0.42f);
				lane.filterMode = FILTER_HP;
				lane.resonance = 0.07f;
				lane.drive = rack::math::clamp(0.10f + chaos * 0.08f, 0.f, 0.30f);
				lane.hatToneMix = 1.0f;
					lane.hatTuneScale = 0.94f * semitoneRatio(tune * 0.24f + variationCentered * 1.8f) * (1.00f + variationCentered * 0.12f);
				lane.ampCoef = decayCoef(0.001f + decay * 0.035f, sampleRate);
				lane.noiseCoef = lane.ampCoef;
				lane.hatToneCoef = lane.ampCoef;
				{
					// Color-driven inharmonic oscillator spread (0-2 range)
					// Offsets: alternating positive/negative for natural beating texture
					static constexpr float kClrSpread[kHatOscCount] = {0.f, 0.170f, -0.085f, 0.414f, -0.230f, 0.618f};
					for (int i = 0; i < kHatOscCount; ++i) {
							lane.hatDetune[i] = 1.f + randomBipolar(lane.noiseState) * (0.006f + 0.030f * variation)
							+ clamp01(color) * kClrSpread[i] * 0.08f;
					}
				}
				// Closed hat transient: high-frequency for stick articulation
					lane.transientFreq = 4000.f + snap * 2000.f + variationCentered * 1200.f;
				lane.transientChirpRate = 0.9996f;
				break;

			case MACHINE_CHAOS1:
				lane.baseFreq = 1.f;
				lane.bodyAmount = 0.0f;
				lane.subAmount = 0.0f;
				lane.fmAmount = 0.0f;
				lane.noiseAmount = 0.10f + noiseMacro * 0.12f;
				lane.noiseBodyAmount = 0.06f + noiseMacro * 0.05f;
				lane.noiseBrightAmount = 0.22f + noiseMacro * 0.16f;
					lane.noiseToneCoef = 0.060f + 0.120f * variation;
					lane.cutoff = rack::math::clamp(3600.f + clamp01(color) * 2000.f + snap * 700.f + variationCentered * 1200.f, 1400.f, sampleRate * 0.34f);
				lane.filterMode = FILTER_HP;
				lane.resonance = 0.07f;
				lane.drive = rack::math::clamp(0.08f + chaos * 0.08f, 0.f, 0.26f);
				lane.hatToneMix = 0.82f + 0.06f * clamp01(color);
					lane.hatTuneScale = 0.92f * semitoneRatio(tune * 0.22f + variationCentered * 2.0f) * (1.00f + variationCentered * 0.15f);
				lane.ampCoef = decayCoef(0.001f + decay * 0.304f, sampleRate);
				lane.noiseCoef = lane.ampCoef;
				lane.hatToneCoef = decayCoef(0.008f + decay * 0.120f, sampleRate);
				{
					static constexpr float kClrSpread[kHatOscCount] = {0.f, 0.170f, -0.085f, 0.414f, -0.230f, 0.618f};
					for (int i = 0; i < kHatOscCount; ++i) {
							lane.hatDetune[i] = 1.f + randomBipolar(lane.noiseState) * (0.012f + 0.055f * variation)
							+ clamp01(color) * kClrSpread[i] * 0.075f;
					}
				}
				// Open hat transient: similar to CH but slightly lower
					lane.transientFreq = 3600.f + snap * 1800.f + variationCentered * 1000.f;
				lane.transientChirpRate = 0.9994f;
				break;

			case MACHINE_RIDE:
				if (lane.useOneShotSample) {
					lane.drive = rack::math::clamp(0.02f + color * 0.06f + chaos * 0.08f, 0.f, 0.24f);
					lane.ampCoef = 1.f;
					lane.noiseCoef = 1.f;
					lane.modCoef = 1.f;
					lane.hatToneCoef = 1.f;
					lane.filter.reset();
					lane.dcBlock.reset();
					break;
				}
				lane.baseFreq = 1496.f * semitoneRatio(tune * 0.06f);
					lane.modRatio = 2.806f + variationCentered * 0.10f;
					lane.modIndex = 0.20f + punch * 0.16f + variation * 0.10f;
				lane.bodyAmount = 0.0f;
				lane.subAmount = 0.0f;
				lane.fmAmount = 0.15f + color * 0.05f + body * 0.02f;
				lane.noiseAmount = 0.08f + noiseMacro * 0.10f;
				lane.noiseBodyAmount = 0.06f + noiseMacro * 0.04f;
				lane.noiseBrightAmount = 0.20f + noiseMacro * 0.12f + snap * 0.05f;
					lane.noiseToneCoef = 0.045f + 0.070f * variation;
					lane.cutoff = rack::math::clamp(1800.f + color * 1200.f + snap * 220.f + variationCentered * 300.f, 900.f, sampleRate * 0.42f);
				lane.filterMode = FILTER_HP;
				lane.resonance = 0.26f + color * 0.10f;
				lane.drive = rack::math::clamp(0.08f + chaos * 0.10f, 0.f, 0.30f);
				lane.hatToneMix = 0.62f + snap * 0.06f + 0.04f * clamp01(color);
				lane.bellAmp = 1.f;
				lane.bellAmpCoef = decayCoef(0.030f + decay * 0.700f, sampleRate);
				lane.rideBellMix = 0.075f + snap * 0.025f + 0.015f * clamp01(color);
					lane.hatTuneScale = semitoneRatio(tune * 0.05f + variationCentered * 0.16f);
				lane.ampCoef = decayCoef(0.005f + decay * 2.000f, sampleRate);
				lane.noiseCoef = decayCoef(0.020f + decay * 0.460f, sampleRate);
				lane.modCoef = decayCoef(0.050f + decay * 1.086f, sampleRate);
				lane.hatToneCoef = decayCoef(0.080f + decay * 0.648f, sampleRate);
				{
					float rideSnapShape = 0.08f + 0.20f * snap;
					lane.transientAmount = rack::math::clamp(transAmt * rideSnapShape * profile.clickSensitivity * (0.50f + 0.10f * accentShape), 0.f, 0.45f);
					float rideTransDecayTime = 0.0018f + 0.018f * transDecay * (1.10f - snap * 0.08f);
					lane.transientCoef = decayCoef(rideTransDecayTime, sampleRate);
				}
				lane.rideEqLowCoef = rack::math::clamp(900.f / (900.f + sampleRate), 0.001f, 0.5f);
				lane.rideEqMidCoef = rack::math::clamp(2600.f / (2600.f + sampleRate), 0.001f, 0.5f);
				lane.rideEqBellCut = 0.14f + (1.f - snap) * 0.08f;
				lane.rideEqAirBoost = 0.10f + snap * 0.08f;
				for (int i = 0; i < kHatOscCount; ++i) {
						lane.hatDetune[i] = 1.f + randomBipolar(lane.noiseState) * (0.002f + 0.007f * variation);
				}
				// Ride transient: mid-high stick ping
					lane.transientFreq = 5600.f + snap * 900.f + variationCentered * 500.f;
				lane.transientChirpRate = 0.9994f;
				break;

				case MACHINE_CLAP:
					if (lane.useOneShotSample) {
						lane.drive = rack::math::clamp(0.06f + color * 0.06f + chaos * 0.10f + std::fabs(variationCentered) * 0.04f, 0.f, 0.34f);
						lane.ampCoef = 1.f;
						lane.noiseCoef = 1.f;
						lane.pitchCoef = 1.f;
						lane.modCoef = 1.f;
						lane.hatToneCoef = 1.f;
						lane.filter.reset();
						lane.dcBlock.reset();
						break;
					}
					lane.baseFreq = 1.f;
					lane.bodyAmount = 0.f;
					lane.subAmount = 0.f;
					lane.fmAmount = 0.f;
					lane.noiseAmount = 0.22f + noiseMacro * 0.18f;
					lane.noiseBodyAmount = 0.12f;
					lane.noiseBrightAmount = 0.38f + snap * 0.10f;
					lane.cutoff = rack::math::clamp(1400.f + clamp01(color) * 4600.f + variationCentered * 1200.f, 800.f, sampleRate * 0.42f);
					lane.filterMode = FILTER_HP;
					lane.resonance = 0.08f;
					lane.drive = rack::math::clamp(0.10f + chaos * 0.10f, 0.f, 0.36f);
					lane.ampCoef = decayCoef(0.004f + decay * 0.32f, sampleRate);
					lane.noiseCoef = lane.ampCoef;
					lane.transientFreq = 1800.f + snap * 1200.f + variationCentered * 700.f;
					lane.transientChirpRate = 0.9994f;
					break;

				case MACHINE_RIM:
					if (lane.useOneShotSample) {
						lane.drive = rack::math::clamp(0.04f + color * 0.07f + chaos * 0.10f + std::fabs(variationCentered) * 0.06f, 0.f, 0.34f);
						lane.ampCoef = 1.f;
						lane.noiseCoef = 1.f;
						lane.pitchCoef = 1.f;
						lane.modCoef = 1.f;
						lane.hatToneCoef = 1.f;
						lane.filter.reset();
						lane.dcBlock.reset();
						break;
					}
					lane.baseFreq = 1.f;
					lane.bodyAmount = 0.f;
					lane.subAmount = 0.f;
					lane.fmAmount = 0.f;
					lane.noiseAmount = 0.10f + noiseMacro * 0.08f;
					lane.noiseBodyAmount = 0.06f;
					lane.noiseBrightAmount = 0.18f + snap * 0.08f;
					lane.cutoff = rack::math::clamp(2200.f + clamp01(color) * 5000.f + variationCentered * 1600.f, 1000.f, sampleRate * 0.44f);
					lane.filterMode = FILTER_HP;
					lane.resonance = 0.08f;
					lane.drive = rack::math::clamp(0.08f + chaos * 0.10f, 0.f, 0.32f);
					lane.ampCoef = decayCoef(0.002f + decay * 0.12f, sampleRate);
					lane.noiseCoef = lane.ampCoef;
					lane.transientFreq = 2600.f + snap * 1600.f + variationCentered * 1100.f;
					lane.transientChirpRate = 0.9991f;
					break;
		}

		if (profile.machine == MACHINE_METAL || profile.machine == MACHINE_CHAOS1 || profile.machine == MACHINE_RIDE) {
			lane.carrierPhase = wrap01(randomBipolar(lane.noiseState) * 0.5f + 0.5f);
			lane.modPhase = wrap01(randomBipolar(lane.noiseState) * 0.5f + 0.5f);
			lane.subPhase = wrap01(randomBipolar(lane.noiseState) * 0.5f + 0.5f);
		}

		// COLOR > 1.0: Phaseon1-inspired inharmonic spread
		// Below 1.0 everything is identical to before. Above 1.0 the extra amount
		// progressively introduces inharmonic content differently per voice class.
		{
			float colorExtra = rack::math::clamp(color - 1.0f, 0.f, 1.f);
			if (colorExtra > 0.001f) {
				bool isHatVoice = (profile.machine == MACHINE_METAL || profile.machine == MACHINE_CHAOS1);

				if (isHatVoice) {
					// Oscillator inharmonic spread — wider fanning at high color
					static constexpr float kInharmSpread[kHatOscCount] = {
						0.000f, 0.170f, 0.414f, 0.618f, 0.832f, 1.000f
					};
					for (int i = 1; i < kHatOscCount; ++i) {
						lane.hatDetune[i] *= (1.f + colorExtra * kInharmSpread[i] * 0.40f);
					}
					// Lower resonance (= more ring) as color pushes past 1.0
					lane.resonance = rack::math::clamp(lane.resonance - colorExtra * 0.04f, 0.05f, 1.8f);
					// Pull cutoff back DOWN into oscillator range — buzzy/raw texture
					lane.cutoff = rack::math::clamp(lane.cutoff - colorExtra * 3200.f, 600.f, sampleRate * 0.42f);
				} else if (profile.machine == MACHINE_RIDE) {
					// Ride: inharmonic modulator detuning + deeper FM
					lane.modRatio = rack::math::clamp(lane.modRatio * (1.f + colorExtra * 0.35f), 0.1f, 12.f);
					lane.modIndex = rack::math::clamp(lane.modIndex + colorExtra * 2.2f, 0.f, 6.f);
					lane.fmAmount = rack::math::clamp(lane.fmAmount + colorExtra * 0.30f, 0.f, 1.f);
				} else {
					// Kick & Snares: inharmonic modulator offset + FM sideband boost
					lane.modRatio = rack::math::clamp(lane.modRatio * (1.f + colorExtra * 0.25f), 0.1f, 12.f);
					lane.modIndex = rack::math::clamp(lane.modIndex + colorExtra * 2.0f, 0.f, 6.f);
					lane.fmAmount = rack::math::clamp(lane.fmAmount + colorExtra * 0.25f, 0.f, 1.f);
				}
			}
		}

		{
			float materialBodyScale = 0.72f + 0.56f * material + 0.82f * materialExtra;
			float materialExciterScale = rack::math::clamp(1.20f - 0.45f * material - 0.35f * materialExtra, 0.25f, 1.40f);
			float materialFmScale = 0.82f + 0.58f * material + 0.75f * materialExtra;
			float transientSampleScale = 0.35f + 1.65f * transientMacro + 1.25f * transientExtra;

			switch (profile.machine) {
				case MACHINE_KICK:
					if (lane.useKickSample) {
							lane.kickCycleGain *= 0.82f + 0.36f * material + 0.60f * materialExtra;
							lane.sampleToneLowMix = rack::math::clamp(lane.sampleToneLowMix * (0.80f + 0.55f * material + 0.75f * materialExtra), 0.f, 2.4f);
							lane.sampleToneHighMix = rack::math::clamp(lane.sampleToneHighMix * rack::math::clamp(1.20f - 0.45f * material - 0.25f * materialExtra, 0.25f, 2.f), 0.f, 2.4f);
					} else {
						lane.bodyAmount = rack::math::clamp(lane.bodyAmount * materialBodyScale, 0.f, 1.35f);
							lane.subAmount = rack::math::clamp(lane.subAmount + material * 0.14f + materialExtra * 0.24f, 0.f, 0.80f);
						lane.fmAmount = rack::math::clamp(lane.fmAmount * (1.10f - 0.32f * material), 0.f, 1.f);
						lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.85f + 0.35f * material), 0.f, 1.f);
						lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * materialExciterScale, 0.f, 1.f);
					}
					break;

				case MACHINE_SNARE1:
					lane.bodyAmount = rack::math::clamp(lane.bodyAmount * materialBodyScale, 0.f, 1.40f);
					lane.subAmount = rack::math::clamp(lane.subAmount * (0.70f + 0.70f * material), 0.f, 0.35f);
					lane.fmAmount = rack::math::clamp(lane.fmAmount * materialFmScale, 0.f, 1.f);
					lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.76f + 0.48f * material), 0.f, 1.40f);
					lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * materialExciterScale, 0.f, 1.60f);
					lane.shellBuzzAmount = rack::math::clamp(lane.shellBuzzAmount * (0.78f + 0.52f * material), 0.f, 1.f);
					lane.snareFatMix = rack::math::clamp(lane.snareFatMix + materialCentered * 0.18f, 0.f, 1.f);
					break;

				case MACHINE_SNARE2:
					// 808 snare material: shifts bridge tone balance and noise density.
					// No fmAmount/subAmount/shellBuzzAmount/snareFatMix (all 0 in 808 synthesis).
					lane.bodyAmount = rack::math::clamp(lane.bodyAmount * materialBodyScale, 0.f, 1.40f);
					lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.76f + 0.48f * material), 0.f, 1.40f);
					lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * materialExciterScale, 0.f, 1.60f);
					// Material shifts upper bridge tone mode emphasis (low material = thuddy, high = bright)
					lane.shellMixB = rack::math::clamp(lane.shellMixB * (0.60f + 0.80f * material), 0.f, 0.80f);
					break;

				case MACHINE_METAL:
				case MACHINE_CHAOS1:
					lane.hatToneMix = rack::math::clamp(lane.hatToneMix * (0.74f + 0.46f * material), 0.f, 1.40f);
					lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.72f + 0.55f * material), 0.f, 1.f);
					lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * materialExciterScale, 0.f, 1.f);
					lane.resonance = rack::math::clamp(lane.resonance + material * 0.04f, 0.05f, 1.8f);
					break;

				case MACHINE_RIDE:
					lane.hatToneMix = rack::math::clamp(lane.hatToneMix * (0.78f + 0.34f * material), 0.f, 1.40f);
					lane.rideBellMix = rack::math::clamp(lane.rideBellMix * (0.55f + 0.95f * material), 0.f, 1.f);
					lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.76f + 0.42f * material), 0.f, 1.f);
					lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * (1.18f - 0.38f * material), 0.f, 1.f);
					lane.fmAmount = rack::math::clamp(lane.fmAmount * materialFmScale, 0.f, 1.f);
					if (lane.useOneShotSample) {
						lane.sampleToneLowMix = rack::math::clamp(lane.sampleToneLowMix * (0.80f + 0.55f * material), 0.f, 2.f);
						lane.sampleToneHighMix = rack::math::clamp(lane.sampleToneHighMix * (1.15f - 0.35f * material), 0.f, 2.f);
					}
					break;

				case MACHINE_CLAP:
				case MACHINE_RIM:
					lane.noiseBodyAmount = rack::math::clamp(lane.noiseBodyAmount * (0.78f + 0.42f * material), 0.f, 1.f);
					lane.noiseBrightAmount = rack::math::clamp(lane.noiseBrightAmount * materialExciterScale, 0.f, 1.6f);
					lane.sampleTransientMix = rack::math::clamp(lane.sampleTransientMix * transientSampleScale, 0.f, 1.5f);
					if (lane.useOneShotSample) {
						lane.sampleToneLowMix = rack::math::clamp(lane.sampleToneLowMix * (0.82f + 0.52f * material), 0.f, 2.f);
						lane.sampleToneHighMix = rack::math::clamp(lane.sampleToneHighMix * (1.18f - 0.40f * material), 0.f, 2.f);
					}
					break;
			}

					lane.transientType = clamp01(lane.transientType + transientCentered * 0.20f);
			lane.transientSHPeriod = 4 + (int)(8.f * (1.f - lane.transientType));
					lane.transientAmount = rack::math::clamp(lane.transientAmount * (0.82f + 0.36f * material + 0.45f * materialExtra) * transientSampleScale, 0.f, 3.2f);
					lane.transientFmRoute = clamp01(lane.transientFmRoute * (0.25f + 2.50f * transientMacro + 1.50f * transientExtra) + 0.18f * transientMacro + 0.22f * transientExtra);
					lane.transientFreq = rack::math::clamp(lane.transientFreq * (0.55f + 1.35f * transientMacro + 0.85f * transientExtra), 120.f, sampleRate * 0.45f);
			// Snares: skip the pow exponentiation — the pow makes transients last up to 1.2s
			// (raising a near-1 coef to power 0.24), which is audibly "noisy" on percussive hits.
			// Natural transientCoef (from decayCoef at line 1608) gives ~0.3s max — clean and crisp.
			// Also cap transientAmount lower: the secondary ×3.1 multiplication otherwise pushes
			// short transients to 3.2 amplitude for snares, overwhelming the body tones.
			if (profile.machine == MACHINE_SNARE1 || profile.machine == MACHINE_SNARE2) {
				lane.transientCoef = rack::math::clamp(lane.transientCoef, 1e-6f, 0.999999f); // no pow
				lane.transientAmount = rack::math::clamp(lane.transientAmount, 0.f, 1.0f);
			} else {
					lane.transientCoef = std::pow(rack::math::clamp(lane.transientCoef, 1e-6f, 0.999999f), 1.f / (0.45f + 2.25f * transientMacro + 1.60f * transientExtra));
			}
					lane.kickAttackGain *= 0.45f + 1.40f * transientMacro + 1.10f * transientExtra;
					lane.sampleTransientMix = rack::math::clamp(lane.sampleTransientMix * transientSampleScale, 0.f, 2.5f);
		}
		// Cache SVF coefficients once per trigger: avoids (2π·cutoff/sampleRate) division every sample.
		lane.filter.setCoeffs(lane.cutoff, lane.resonance, sampleRate);
	}

	void process(const ProcessArgs& args) override {
		sampleRateCache = args.sampleRate;
		if (++controlDivider >= kControlRate) {
			controlDivider = 0;
			updateMacros();
			// Duck knob smoothing at control rate — envelope itself (lanes[0].ampEnv) is per-sample
			duckOnCache_     = params[DUCK_ON_PARAM].getValue() > 0.5f;
			duckDepthSmooth_ += 0.18f * (params[DUCK_DEPTH_PARAM].getValue() - duckDepthSmooth_);
			duckPumpSmooth_  += 0.18f * (params[DUCK_PUMP_PARAM].getValue()  - duckPumpSmooth_);
			// Hoist release coef to control rate: pump=0 -> 30ms, pump=1 -> 400ms
			{
				const float releaseTime = 0.030f + duckPumpSmooth_ * 0.370f;
				duckReleaseCoef_ = std::exp(-1.f / (releaseTime * sampleRateCache));
			}
			// groove coefs are sample-rate-only constants; updated in onSampleRateChange.
#ifndef METAMODULE
			// Update reverb parameters at control rate (not per-sample)
			// Clamp reverb_time to 0.98 max: krt > 1.0 in the feedback loop causes
			// exponential divergence regardless of the allpass structure.
			// Squared curve: low knob range has fine-grained control over short tails.
			// TIME=0.00 → krt=0.00 (single scatter pass, no tail ~gated ambience)
			// TIME=0.25 → krt=0.061  TIME=0.50 → krt=0.245  TIME=1.00 → krt=0.98
			const float rvbTimeKnob = clamp01(params[RVB_TIME_PARAM].getValue() + readMacroCv01(RVB_TIME_CV_INPUT));
			reverb_.set_time(rvbTimeKnob * rvbTimeKnob * 0.98f);
			// Squared curve: first half = subtle warmth, second half = dramatically dark.
			// DAMP=0 → lp=0.99 (~flat), DAMP=0.5 → lp=0.75 (-4dB@Nyq), DAMP=1 → lp=0.05 (-34dB@Nyq).
			const float damp = clamp01(params[RVB_DAMP_PARAM].getValue() + readMacroCv01(RVB_DAMP_CV_INPUT));
			reverb_.set_lp(0.99f - 0.94f * (damp * damp));
			const float diff = clamp01(params[RVB_DIFF_PARAM].getValue() + readMacroCv01(RVB_DIFF_CV_INPUT));
			reverb_.set_diffusion(0.40f + 0.50f * diff);
			const float amount = clamp01(params[RVB_AMOUNT_PARAM].getValue() + readMacroCv01(RVB_AMOUNT_CV_INPUT));
			reverb_.set_input_gain(0.20f * amount);
			reverb_.set_amount(1.0f); // pure wet output
			// HPF coefficient: 1-pole HPF at ~150Hz, sample-rate-independent
			const float hpfCutHz = 150.f;
			rvbHpfCoeff_ = sampleRateCache / (sampleRateCache + 2.0f * 3.14159265f * hpfCutHz);
#endif
		}
		grooveMetalMemory_ *= grooveTextureDecayCoef_;
		grooveAccentMemory_ *= grooveAccentDecayCoef_;

		for (int i = 0; i < kNumVoices; ++i) {
			if (lanes[i].trigger.process(inputs[TRIG_INPUT_BASE + i].getVoltage(), 0.1f, 2.f)) {
				int targetLane = resolveTriggerLane(i);
				if (targetLane >= 0) {
					fireLane(targetLane, args.sampleRate, i);
				}
			}
		}
		grooveCvOutput_ += grooveOutputSlewCoeff_ * (grooveCvHeld_ - grooveCvOutput_);
		grooveCvOutput_ = rack::math::clamp(grooveCvOutput_, 0.f, 1.f);

#ifndef METAMODULE
		float rvbSend = 0.f;
#endif

		// Kick-sidechain duck: peak-hold envelope with pump-controlled release.
		// Attack: instant - holds at maximum suppression regardless of kick length.
		// Release: pump=0 -> fast (~30ms), pump=1 -> slow (~400ms).
		// Floor: 0.02 (~-34 dB) at full depth for hard sidechain-compressor effect.
		{
			const float kickAmp = lanes[0].ampEnv;
			if (kickAmp > duckHoldEnv_)
				duckHoldEnv_ = kickAmp;
			else
				duckHoldEnv_ *= duckReleaseCoef_;
		}
		const float duckGain = duckOnCache_
			? rack::math::clamp(1.f - duckDepthSmooth_ * duckHoldEnv_, 0.02f, 1.f)
			: 1.f;
		const float sidechainEnvelope = rack::math::clamp(lanes[0].ampEnv, 0.f, 1.f);
		// Per-voice spectral weights: weight=0 → no ducking, weight=1 → full duckGain.
		static constexpr float kDuckWeights[kNumVoices] = {
			0.f,   // 0 Kick    — source; never ducked by itself
			0.90f, // 1 Snare 1
			0.90f, // 2 Snare 2
			0.90f, // 3 Metal   — closed-hat; needs to pump clearly in techno
			0.80f, // 4 Chaos
			0.90f, // 5 Ride    — needs to pump clearly in techno
			0.85f, // 6 Clap
			0.80f, // 7 Rim
		};

		float renderedSamples[kNumVoices] = {};
		bool renderedValid[kNumVoices] = {};
		for (int i = 0; i < kNumVoices; ++i) {
			int renderLane = resolveOutputLane(i);
			float finalDrive = 0.94f * finalDriveSmooth;
			float sample = 0.f;
			if (renderLane >= 0) {
				if (!renderedValid[renderLane]) {
					renderedSamples[renderLane] = lanes[renderLane].render(args.sampleRate, finalDrive, isMetaModuleVariant());
					renderedValid[renderLane] = true;
				}
				sample = renderedSamples[renderLane];
			}
#ifndef METAMODULE
			if (i == 0) {
				float kickRumbleDuckEnv = rack::math::clamp(lanes[0].ampEnv + lanes[0].transientEnv * 0.35f, 0.f, 1.f);
				sample += kickRumbleSmear_.process(lanes[0].rumbleSend, kickRumbleDuckEnv);
			}
#endif
			// Apply weighted ducking: formula ensures weight=0 → no change, weight=1 → full duckGain
			if (renderLane >= 0) {
				sample *= 1.f - (1.f - duckGain) * kDuckWeights[renderLane];
			}
			outputs[AUDIO_OUTPUT_BASE + i].setVoltage(sample * 5.f);
			float lightValue = 0.f;
			if (renderLane >= 0) {
				lightValue = rack::math::clamp(lanes[renderLane].ampEnv * 0.8f + lanes[renderLane].transientEnv * 0.2f, 0.f, 1.f);
			}
			lights[HIT_LIGHT_BASE + i].setBrightness(lightValue);
#ifndef METAMODULE
			// Accumulate reverb send bus — voice OUTs remain 100% dry
			rvbSend += sample * params[RVB_SEND_PARAM_BASE + i].getValue();
#endif
		}

#ifndef METAMODULE
		// 1-pole HPF at ~150Hz: blocks kick fundamental from muddying the reverb tail.
		// Topology: y[n] = coeff * (y[n-1] + x[n] - x[n-1])
		float hpf = rvbHpfCoeff_ * (rvbHpfState_ + rvbSend - rvbHpfXPrev_);
		rvbHpfXPrev_ = rvbSend;
		rvbHpfState_ = hpf;
		rvbHpfState_ += 1e-18f; // denormal prevention: tiny DC offset keeps IIR away from subnormals

		float revL = hpf, revR = hpf;
		reverb_.Process(&revL, &revR, 1);

		// Stereo wet to dedicated output jacks — individual voice OUTs untouched
		outputs[REV_WET_L_OUTPUT].setVoltage(revL * 5.f);
		outputs[REV_WET_R_OUTPUT].setVoltage(revR * 5.f);
#else
		outputs[REV_WET_L_OUTPUT].setVoltage(0.f);
		outputs[REV_WET_R_OUTPUT].setVoltage(0.f);
#endif
		outputs[SIDECHAIN_OUTPUT].setVoltage(10.f * sidechainEnvelope);
		outputs[GROOVE_CV_OUTPUT].setVoltage(10.f * grooveCvOutput_);
	}
};

struct FKLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;
	NVGalign align;

	FKLabel(Vec pos, const char* text, float fontSize, NVGcolor color, NVGalign align = NVG_ALIGN_CENTER) {
		box.pos = pos;
		box.size = Vec(80, fontSize + 4);
		this->text = text;
		this->fontSize = fontSize;
		this->color = color;
		this->align = align;
	}

	void draw(const DrawArgs& args) override {
		std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
		float y = fontSize / 2.f;
		nvgText(args.vg, 0, y, text.c_str(), NULL);
	}
};

static FKLabel* fkLabel(Vec mmPos, const char* text, float fontSize, NVGalign align = NVG_ALIGN_CENTER) {
	Vec pxPos = mm2px(mmPos);
	NVGcolor white = nvgRGB(0xff, 0xff, 0xff);
	return new FKLabel(pxPos, text, fontSize, white, align);
}
static FKLabel* fkLabelCol(Vec mmPos, const char* text, float fontSize, NVGcolor color, NVGalign align = NVG_ALIGN_CENTER) {
	Vec pxPos = mm2px(mmPos);
	(void)color;
	NVGcolor white = nvgRGB(0xff, 0xff, 0xff);
	return new FKLabel(pxPos, text, fontSize, white, align);
}

// Ferroklast-local knob variants — 44% larger than RoundSmallBlackKnob (20% up from global 1.2f base).
// Using local structs avoids affecting other modules that use the global MVXKnob_c/red.
struct FKKnob_grey : MVXKnob_c {
	FKKnob_grey() {
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size.mult(1.44f);
	}
};
struct FKKnob_red : MVXKnob_red {
	FKKnob_red() {
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size.mult(1.44f);
	}
};

struct FerroklastWidget : ModuleWidget {
	FerroklastWidget(Ferroklast* module) {
		setModule(module);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ferroklast.png")));
	#else
		box.size = Vec(RACK_GRID_WIDTH * 38, RACK_GRID_HEIGHT);
		{
			auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Ferroklast.png"));
			panelBg->box.pos  = Vec(0, 0);
			panelBg->box.size = box.size;
			addChild(panelBg);
		}
	#endif

		const float trimColsTighter = 13.f * (25.4f / 75.f);
		const float trimColsLeft = 14.f * (25.4f / 75.f);
		const float trimColStart = 18.f - trimColsLeft;
		const float trimColStep = 24.f - trimColsTighter;
		const float xCols[kNumVoices] = {
			trimColStart + 0.f * trimColStep,
			trimColStart + 1.f * trimColStep,
			trimColStart + 2.f * trimColStep,
			trimColStart + 3.f * trimColStep,
			trimColStart + 4.f * trimColStep,
			trimColStart + 5.f * trimColStep,
			trimColStart + 6.f * trimColStep,
			trimColStart + 7.f * trimColStep,
		};
		const bool isMetaModuleVariantUi = module && module->isMetaModuleVariant();
		auto isVisibleLane = [&](int lane) {
			return !isMetaModuleVariantUi || (lane != 4 && lane != 7);
		};
		auto laneDisplayX = [&](int lane) {
			if (isMetaModuleVariantUi && lane == 3) {
				return (xCols[3] + xCols[4]) * 0.5f;
			}
			return xCols[lane];
		};
		auto laneDisplayName = [&](int lane) -> const char* {
			if (isMetaModuleVariantUi && lane == 3) {
				return "HH";
			}
			return kVoiceShortNames[lane];
		};
		const float yUp = 12.0f; // total upward shift (~26px)
		// Unified spacing for all macro knobs: 5px tighter than before.
		// [0]=KICK  [1..6]=GLOBAL macros  [7..8]=REVERB — all evenly spaced.
			const float kKnobSpacing = 24.f - 21.f * (25.4f / 75.f) - 5.f * (25.4f / 75.f);
			const float topSectionRight = 9.f * (25.4f / 75.f);
			const float topKnobXs[10] = {
			8.f + topSectionRight,
			8.f + topSectionRight + kKnobSpacing,
			8.f + topSectionRight + 2.f * kKnobSpacing,
			8.f + topSectionRight + 3.f * kKnobSpacing,
			8.f + topSectionRight + 4.f * kKnobSpacing,
			8.f + topSectionRight + 5.f * kKnobSpacing,
			8.f + topSectionRight + 6.f * kKnobSpacing,
			8.f + topSectionRight + 7.f * kKnobSpacing,
			8.f + topSectionRight + 8.f * kKnobSpacing,
				8.f + topSectionRight + 9.f * kKnobSpacing,
		};
		const float topSectionUp = 7.f * (25.4f / 75.f);
		const float topRowY = 21.f - yUp - topSectionUp;
		const float bottomRowY = 42.f - yUp - topSectionUp;
		const float topLabelY = 12.f - yUp - topSectionUp;
		const float bottomLabelY = 33.f - yUp - topSectionUp;
		const float largeKnobLabelSize = 9.f;
		const float boostedRowLabelSize = 8.75f;
		const float voiceHeadlineSize = 16.8f * 0.85f * 0.80f; // 21 * 0.8 * 0.85 * 0.80 shrink
		const float voiceHeadlineShiftMm = 4.f * (25.4f / 75.f); // kept for other uses
		// Shift constants for large knobs + their labels (25px base; +10px extra for knobs/labels only, headlines stay)
		const float knobShiftMm = 25.f * (25.4f / 75.f);
		const float knobBodyShiftMm = knobShiftMm + 10.f * (25.4f / 75.f);
		const float bottomRowLift = 12.f * (25.4f / 75.f); // bottom knob row 12px higher
		const float knobLabelDown = 3.f * (25.4f / 75.f);
		const float lowerRowExtraUp = 4.f * (25.4f / 75.f);
		const float knobRowsUp = 4.f * (25.4f / 75.f);
		const float upperRowsExtraUp = 3.f * (25.4f / 75.f);
		const float headlineExtraUp = 3.f * (25.4f / 75.f);
		const float lowestKnobRowExtraUp = 3.f * (25.4f / 75.f);
		const float topKnobY    = topRowY    + knobBodyShiftMm - knobRowsUp - upperRowsExtraUp;
		const float bottomKnobY = bottomRowY + knobBodyShiftMm - bottomRowLift - lowerRowExtraUp - upperRowsExtraUp - lowestKnobRowExtraUp;
		const float topKnobLbY    = topLabelY    + knobBodyShiftMm + knobLabelDown - knobRowsUp - upperRowsExtraUp;
		const float bottomKnobLbY = bottomLabelY + knobBodyShiftMm - bottomRowLift + knobLabelDown - lowerRowExtraUp - knobRowsUp - upperRowsExtraUp;
		// Headline Y uses base shift only (stays in place)
		const float headlineLbY    = topLabelY    + knobShiftMm - headlineExtraUp;

		// Section headlines — same Y row, 25% larger font, coloured
		const float headlineSize = largeKnobLabelSize * 1.25f;
		const NVGcolor colReverb = nvgRGB(0x25, 0x4e, 0xaf);
		const NVGcolor colRed    = nvgRGB(0xcc, 0x22, 0x22);
			addChild(fkLabel   (Vec((topKnobXs[0] + topKnobXs[1]) * 0.5f, headlineLbY - 3.f), "KICK",   headlineSize));
			addChild(fkLabel   (Vec((topKnobXs[2] + topKnobXs[4]) * 0.5f, headlineLbY - 3.f), "GLOBAL", headlineSize));
			if (!isMetaModuleVariantUi) {
				addChild(fkLabelCol(Vec((topKnobXs[8] + topKnobXs[9]) * 0.5f, headlineLbY - 3.f), "REVERB", headlineSize, colReverb));
			}
		// SNARE headline above SN RES/SN CUT/SN DRV controls
			addChild(fkLabelCol(Vec((topKnobXs[5] + topKnobXs[7]) * 0.5f, headlineLbY - 3.f), "SNARE", headlineSize, colRed));
			// Kick columns: existing rumble/curve plus new attack tone/pitch depth controls.
		addChild(fkLabel(Vec(topKnobXs[0], topKnobLbY),    "RUMBLE", largeKnobLabelSize));
		addChild(fkLabel(Vec(topKnobXs[0], bottomKnobLbY), "CURVE",  largeKnobLabelSize));
			addChild(fkLabel(Vec(topKnobXs[1], topKnobLbY),    "ATK TRANSIENT", largeKnobLabelSize));
			addChild(fkLabel(Vec(topKnobXs[1], bottomKnobLbY), "PTCH DPTH", largeKnobLabelSize));
		addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[0], topKnobY)),    module, Ferroklast::KICK_RUMBLE_PARAM));
		addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[0], bottomKnobY)), module, Ferroklast::KICK_CURVE_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[1], topKnobY)),    module, Ferroklast::KICK_ATTACK_TONE_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[1], bottomKnobY)), module, Ferroklast::KICK_PITCH_DEPTH_PARAM));
		// Global macro rows [1..6]:
		//   Top:    PUNCH  BODY  COLOR  SN RES  SN CUT  SN DRV
		//   Bottom: SNAP   NOISE TRANS  MATERIAL CHAOS DRIVE
		const char* topRowLabels[6]    = {"PUNCH", "BODY", "COLOR", "SN RES", "SN CUT", "SN DRV"};
		const char* bottomRowLabels[6] = {"SNAP", "NOISE", "TRANSIENT", "MATERIAL", "RUIN", "DRIVE"};
		for (int i = 0; i < 6; ++i) {
				addChild(fkLabel(Vec(topKnobXs[i + 2], topKnobLbY),    topRowLabels[i],    largeKnobLabelSize));
				addChild(fkLabel(Vec(topKnobXs[i + 2], bottomKnobLbY), bottomRowLabels[i], largeKnobLabelSize));
		}
		// Top row: PUNCH BODY COLOR (grey) | SN RES SN DRV SN CUT (red)
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[2], topKnobY)), module, Ferroklast::PUNCH_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[3], topKnobY)), module, Ferroklast::BODY_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[4], topKnobY)), module, Ferroklast::COLOR_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[5], topKnobY)), module, Ferroklast::SNARE_RES_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[6], topKnobY)), module, Ferroklast::SNARE_CUT_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[7], topKnobY)), module, Ferroklast::SNARE_DRIVE_PARAM));
		// Bottom row: SNAP NOISE TRANS (grey) | DRIVE CHAOS MATERIAL (grey)
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[2], bottomKnobY)), module, Ferroklast::SNAP_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[3], bottomKnobY)), module, Ferroklast::NOISE_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[4], bottomKnobY)), module, Ferroklast::TRANSIENT_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[5], bottomKnobY)), module, Ferroklast::MATERIAL_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[6], bottomKnobY)), module, Ferroklast::CHAOS_PARAM));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(topKnobXs[7], bottomKnobY)), module, Ferroklast::FINAL_DRIVE_PARAM));

		// FERROKLAST title removed to make room for reverb controls.
		// Reverb macro knobs — positions [7] and [8] of the unified grid.
			const float xRvb1 = topKnobXs[8], xRvb2 = topKnobXs[9];
		const float rightClusterLeft = 125.f * (25.4f / 75.f);
		const float xCv = 209.f - rightClusterLeft;   // hoisted: needed by DUCK and CV sections
		const float xCv2 = 217.f + 5.f * (25.4f / 75.f) - rightClusterLeft;
		if (!isMetaModuleVariantUi) {
			addChild(fkLabel(Vec(xRvb1, topKnobLbY), "TIME", largeKnobLabelSize));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(xRvb1, topKnobY)), module, Ferroklast::RVB_TIME_PARAM));
			addChild(fkLabel(Vec(xRvb2, topKnobLbY), "DAMP", largeKnobLabelSize));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(xRvb2, topKnobY)), module, Ferroklast::RVB_DAMP_PARAM));
			addChild(fkLabel(Vec(xRvb1, bottomKnobLbY), "DIFF", largeKnobLabelSize));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(xRvb1, bottomKnobY)), module, Ferroklast::RVB_DIFF_PARAM));
			addChild(fkLabel(Vec(xRvb2, bottomKnobLbY), "AMNT", largeKnobLabelSize));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(xRvb2, bottomKnobY)), module, Ferroklast::RVB_AMOUNT_PARAM));
		}

		// DUCK section — moved 10px up from original position (20px offset cancels the 10px knob shift + adds 10px up)
		const float duckDownMm  = 28.f * (25.4f / 75.f) - 20.f * (25.4f / 75.f);
		const float duckUpExtra = 6.f * (25.4f / 75.f); // plus the shared 3px upper-row lift = 9px total
		const float duckExtraUp = 15.f * (25.4f / 75.f);
		const float duckY       = bottomKnobY   + duckDownMm - duckUpExtra - duckExtraUp;
		const float duckLbY     = bottomKnobLbY + duckDownMm - duckUpExtra - duckExtraUp;
		const float duckSpacingMm = 4.f + 7.f * (25.4f / 75.f); // control width + 7px gap (+3px)
		const float duckMidX    = (xCv + xCv2) * 0.5f;
		const float xD1 = duckMidX - duckSpacingMm, xDMid = duckMidX, xD2 = duckMidX + duckSpacingMm;
		const float duckLabelShift = 7.f * (25.4f / 75.f);
		addChild(fkLabel(Vec((xD1 + xD2) * 0.5f, duckLbY), "DUCKING", largeKnobLabelSize));
		addChild(fkLabel(Vec(xD1,   duckLbY + duckLabelShift), "DPT", 7.f));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(xD1,   duckY)), module, Ferroklast::DUCK_DEPTH_PARAM));
		addChild(fkLabel(Vec(xDMid, duckLbY + duckLabelShift), "ON", 7.f));
		addParam(createParamCentered<CKSS>(mm2px(Vec(xDMid, duckY)), module, Ferroklast::DUCK_ON_PARAM));
		addChild(fkLabel(Vec(xD2,   duckLbY + duckLabelShift), "PMP", 7.f));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(xD2,   duckY)), module, Ferroklast::DUCK_PUMP_PARAM));

		// Per-voice trimpot rows: add 3px vertical spacing while keeping the block upper-aligned.
		const float trimRowExtraSpacing = 3.f * (25.4f / 75.f);
		const float rowSpacing = 7.0f + trimRowExtraSpacing;
		const float rowStart = 66.f - yUp - trimRowExtraSpacing * 7.f;
		const float trimGridDown = 7.f * (25.4f / 75.f);
		const float trimGridStart = rowStart + trimGridDown;
		const float trigOutShift = 12.f * (25.4f / 75.f);  // trig+out ports 12px lower
		const float cvSpacing = rowSpacing + 3.f * (25.4f / 75.f); // +3px extra between CV ports
		const float cvShiftUp = 6.f * (25.4f / 75.f);      // CV columns 6px higher
		const float cvStart   = rowStart - cvShiftUp;
		const float cellLbOffX = -1.5f - 9.f * (25.4f / 75.f);  // mm left of each trimpot center, RIGHT-aligned
		const float cellLbFont = 6.25f;
		const float trigOutExtraLeft = 4.f * (25.4f / 75.f);
		const float bottomOutputShift = 8.f * (25.4f / 75.f);
		const float wetLabelY = rowStart + rowSpacing * 7.f + 14.f + trigOutShift;
		const float wetY = wetLabelY + bottomOutputShift;
		const float cvOutY = wetY - 8.0f;
		const float rightIoShiftUp = 15.f * (25.4f / 75.f);
		const float rightIoSpread = 1.f * (25.4f / 75.f);
		const float macroCvSpacing = cvSpacing + rightIoSpread;
		const float macroCvStart = cvStart - rightIoShiftUp;
		const float macroCvOutY = macroCvStart + macroCvSpacing * 8.f;
		const float macroWetY = macroCvOutY + macroCvSpacing;
		const float portTopLabelOffset = 16.f * (25.4f / 75.f);
		const float reverbHeadingOffset = 18.f * (25.4f / 75.f);

		// Row labels: right-aligned just before each voice column's trimpots (all 8 columns)
		const char* rowLabels[7] = {"LEVEL", "TUNE", "DECAY", "VAR", "TR AMT", "TR TYPE", "TR DEC"};
		for (int r = 0; r < 7; ++r) {
			for (int i = 0; i < kNumVoices; ++i) {
				if (!isVisibleLane(i)) {
					continue;
				}
				const char* rowLabel = rowLabels[r];
				if (r == 6 && i == 0) {
					rowLabel = "TR/RM";
				}
				addChild(fkLabel(Vec(laneDisplayX(i) + cellLbOffX, trimGridStart + rowSpacing * (float)r - 0.8f), rowLabel, cellLbFont, NVG_ALIGN_RIGHT));
			}
		}
		for (int i = 0; i < kNumVoices; ++i) {
			if (!isVisibleLane(i)) {
				continue;
			}
			const float x = laneDisplayX(i);
			if (!isMetaModuleVariantUi) {
				addChild(fkLabel(Vec(x + cellLbOffX, trimGridStart + rowSpacing * 7.f - 0.8f), "RVB", cellLbFont, NVG_ALIGN_RIGHT));
			}
			addChild(fkLabel(Vec(x + cellLbOffX - trigOutExtraLeft, cvOutY - 0.8f), "TRIG", cellLbFont, NVG_ALIGN_RIGHT));
			addChild(fkLabel(Vec(x + cellLbOffX - trigOutExtraLeft, wetY - 0.8f), "OUT",  cellLbFont, NVG_ALIGN_RIGHT));
		}

		// Right-side macro CV column (9 ports: ACC–SNP + SNC)
		const char* cvLabels[8] = {"ACC", "RUIN", "DRV", "CLR", "BDY", "PCH", "SNP", "SNC"};
		for (int r = 0; r < 8; ++r) {
			addChild(fkLabel(Vec(xCv, macroCvStart + macroCvSpacing * (float)r - portTopLabelOffset), cvLabels[r], 5.5f));
		}
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart)), module, Ferroklast::ACCENT_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing)), module, Ferroklast::DECIMATE_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 2.f)), module, Ferroklast::DRIVE_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 3.f)), module, Ferroklast::COLOR_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 4.f)), module, Ferroklast::BODY_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 5.f)), module, Ferroklast::PUNCH_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 6.f)), module, Ferroklast::SNAP_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv, macroCvStart + macroCvSpacing * 7.f)), module, Ferroklast::SNARE_CUT_CV_INPUT));

		// Second CV column: material, transient, reverb CVs + VAR + kick length
		const char* cv2Labels[8] = {"MAT", "TRS", "DMP", "DFF", "AMT", "TIM", "VAR", "KLEN"};
		for (int r = 0; r < 8; ++r) {
			if (isMetaModuleVariantUi && r >= 2 && r <= 5) {
				continue;
			}
			addChild(fkLabel(Vec(xCv2, macroCvStart + macroCvSpacing * (float)r - portTopLabelOffset), cv2Labels[r], 5.5f));
		}
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart)), module, Ferroklast::MATERIAL_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing)), module, Ferroklast::TRANSIENT_CV_INPUT));
		if (!isMetaModuleVariantUi) {
			addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 2.f)), module, Ferroklast::RVB_DAMP_CV_INPUT));
			addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 3.f)), module, Ferroklast::RVB_DIFF_CV_INPUT));
			addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 4.f)), module, Ferroklast::RVB_AMOUNT_CV_INPUT));
			addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 5.f)), module, Ferroklast::RVB_TIME_CV_INPUT));
		}
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 6.f)), module, Ferroklast::VARIATION_CV_INPUT));
		addInput(createInputCentered<MVXport_silver>(mm2px(Vec(xCv2, macroCvStart + macroCvSpacing * 7.f)), module, Ferroklast::KICK_LENGTH_CV_INPUT));

		// CV outputs above the wet reverb outputs to keep the modulation cluster compact.
		addChild(fkLabel(Vec(xCv, macroCvOutY - portTopLabelOffset), "SC", 5.5f));
		addChild(fkLabel(Vec(xCv2, macroCvOutY - portTopLabelOffset), "GRV", 5.5f));
		addOutput(createOutputCentered<MVXport_silver_red>(mm2px(Vec(xCv,  macroCvOutY)), module, Ferroklast::SIDECHAIN_OUTPUT));
		addOutput(createOutputCentered<MVXport_silver_red>(mm2px(Vec(xCv2, macroCvOutY)), module, Ferroklast::GROOVE_CV_OUTPUT));
		if (!isMetaModuleVariantUi) {
			addChild(fkLabel(Vec((xCv + xCv2) * 0.5f, macroWetY - reverbHeadingOffset), "REVERB OUT", 6.5f));
			addChild(fkLabel(Vec(xCv, macroWetY - portTopLabelOffset), "L", 5.5f));
			addChild(fkLabel(Vec(xCv2, macroWetY - portTopLabelOffset), "R", 5.5f));
			addOutput(createOutputCentered<MVXport_silver_red>(mm2px(Vec(xCv,  macroWetY)), module, Ferroklast::REV_WET_L_OUTPUT));
			addOutput(createOutputCentered<MVXport_silver_red>(mm2px(Vec(xCv2, macroWetY)), module, Ferroklast::REV_WET_R_OUTPUT));
		}

		// Voice name labels — placed just above the first trimpot row
		for (int i = 0; i < kNumVoices; ++i) {
			if (!isVisibleLane(i)) {
				continue;
			}
			addChild(fkLabel(Vec(laneDisplayX(i), trimGridStart - 4.f - 8.f * (25.4f / 75.f)), laneDisplayName(i), voiceHeadlineSize));
		}

		for (int i = 0; i < kNumVoices; ++i) {
			if (!isVisibleLane(i)) {
				continue;
			}
			float x = laneDisplayX(i);
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart)),                         module, Ferroklast::LEVEL_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing)),             module, Ferroklast::TUNE_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 2.f)),       module, Ferroklast::DECAY_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 3.f)),       module, Ferroklast::VARIATION_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 4.f)),       module, Ferroklast::TRANS_AMOUNT_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 5.f)),       module, Ferroklast::TRANS_TYPE_PARAM_BASE + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 6.f)),       module, Ferroklast::TRANS_DECAY_PARAM_BASE + i));
			// RVB SEND trimpot at row 7 (shifted up from 8 after TR FM removal)
			if (!isMetaModuleVariantUi) {
				addParam(createParamCentered<Trimpot>(mm2px(Vec(x, trimGridStart + rowSpacing * 7.f)),       module, Ferroklast::RVB_SEND_PARAM_BASE + i));
			}
			// Hit light: 6mm to the right of the trimpot — prevents overlap/unclickable overlap
			addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(x + 6.f, trimGridStart + rowSpacing * 7.f)), module, Ferroklast::HIT_LIGHT_BASE + i));
			addInput(createInputCentered<MVXport_silver>(mm2px(Vec(x, cvOutY)), module, Ferroklast::TRIG_INPUT_BASE + i));
			addOutput(createOutputCentered<MVXport_silver_red>(mm2px(Vec(x, wetY)), module, Ferroklast::AUDIO_OUTPUT_BASE + i));
		}
	}
};

struct FerroklastMM : Ferroklast {
	FerroklastMM()
		: Ferroklast(FerroklastVariant::MetaModule) {
	}
};

// ── FerroklastMMWidget ── dedicated 24HP MetaModule layout ──────────────────
// Visible lane→voice-index map:
//   slot 0=Kick(0), 1=Sn1(1), 2=Sn2(2), 3=HH/CH(3), 4=Ride(5), 5=Clap(6)
struct FerroklastMMWidget : ModuleWidget {
	static const int kMMVoices = 6;

	FerroklastMMWidget(FerroklastMM* module) {
		setModule(module);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FerroklastMM.png")));
	#else
		box.size = Vec(RACK_GRID_WIDTH * 24, RACK_GRID_HEIGHT);
		{
			auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/FerroklastMM.png"));
			panelBg->box.pos  = Vec(0, 0);
			panelBg->box.size = box.size;
			addChild(panelBg);
		}
	#endif

		// All text is baked into the panel PNG — no addChild(label) calls here;
		// those produce "DISPLAY" placeholders in MetaModule.

		static const int kMMVoiceLanes[kMMVoices] = {0, 1, 2, 3, 5, 6};

		// ── Macro knob block — top band ──────────────────────────────────
		const float macroStart = 8.5f;
		const float macroStep  = 13.4f;
		const float macroTopY  = 21.0f;  // 9 lower
		const float macroBotY  = 38.5f;  // 9 lower

		const int macroTopParams[8] = {
			Ferroklast::KICK_RUMBLE_PARAM,
			Ferroklast::KICK_ATTACK_TONE_PARAM,
			Ferroklast::PUNCH_PARAM,
			Ferroklast::BODY_PARAM,
			Ferroklast::COLOR_PARAM,
			Ferroklast::SNARE_RES_PARAM,
			Ferroklast::SNARE_CUT_PARAM,
			Ferroklast::SNARE_DRIVE_PARAM,
		};
		const int macroBotParams[8] = {
			Ferroklast::KICK_CURVE_PARAM,
			Ferroklast::KICK_PITCH_DEPTH_PARAM,
			Ferroklast::SNAP_PARAM,
			Ferroklast::NOISE_PARAM,
			Ferroklast::TRANSIENT_PARAM,
			Ferroklast::MATERIAL_PARAM,
			Ferroklast::CHAOS_PARAM,
			Ferroklast::FINAL_DRIVE_PARAM,
		};
		static const char* kMacroTopLabels[8] = {"RMBL", "ATONE", "PCH", "BODY", "CLR", "SN.RS", "SN.CT", "SN.DV"};
		static const char* kMacroBotLabels[8] = {"CRV",  "P.DPT", "SNAP", "NOIS", "TRNS", "MAT",   "RUIN",  "DRV"};
		const float macroLabelFontSz = 11.0f;  // matches colLabelFontSz
		const float macroLabelOffY   = 9.0f;   // 1mm lower (was 10)
#ifndef METAMODULE
		const float macroGroupFontSz = 11.0f;
		const float macroGroupY = 5.5f;
		addChild(fkLabel(Vec(macroStart + macroStep * 0.5f, macroGroupY), "KICK", macroGroupFontSz));
		addChild(fkLabel(Vec(macroStart + macroStep * 3.0f, macroGroupY), "GLOBAL", macroGroupFontSz));
		addChild(fkLabel(Vec(macroStart + macroStep * 6.0f, macroGroupY), "SNARES", macroGroupFontSz));
#endif
		for (int i = 0; i < 8; ++i) {
			const float x = macroStart + i * macroStep;
#ifndef METAMODULE
			addChild(fkLabel(Vec(x, macroTopY - macroLabelOffY), kMacroTopLabels[i], macroLabelFontSz));
			addChild(fkLabel(Vec(x, macroBotY - macroLabelOffY), kMacroBotLabels[i], macroLabelFontSz));
#endif
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(x, macroTopY)), module, macroTopParams[i]));
			addParam(createParamCentered<FKKnob_grey>(mm2px(Vec(x, macroBotY)), module, macroBotParams[i]));
		}

		// ── 6-voice column trimpot + trig/out grid ───────────────────────
		const float colStart   = 10.5f;
		const float colStep    = 15.5f;  // 2mm closer horizontally, left-aligned
		const float rowStart   = 57.0f;  // 2mm lower
		const float rowSpacing = 7.5f;  // +2mm spread per row
		// TRIG net +18mm. OUT matches Groove CV Y.
		const float trigY      = 96.0f  + 18.0f;
		const float outY       = 123.6f;
		// Uniform row spacing — all 7 rows equally spaced.
		auto mmRowY = [&](int r) -> float {
			return rowStart + (float)r * rowSpacing;
		};

		const int paramBases[7] = {
			Ferroklast::LEVEL_PARAM_BASE,
			Ferroklast::TUNE_PARAM_BASE,
			Ferroklast::DECAY_PARAM_BASE,
			Ferroklast::VARIATION_PARAM_BASE,
			Ferroklast::TRANS_AMOUNT_PARAM_BASE,
			Ferroklast::TRANS_TYPE_PARAM_BASE,
			Ferroklast::TRANS_DECAY_PARAM_BASE,
		};

		static const char* kMMColLabels[kMMVoices] = {"BD", "SN1", "SN2", "HH", "RIDE", "CLP"};
		static const char* kMMRowLabels[9] = {"LVL", "TUNE", "DECY", "VARI", "TRNS", "TYPE", "TDEC", "TRG", "OUT"};
		const float labelFontSz    = 5.5f;   // row labels (LVL, TUNE, etc.)
		const float colLabelFontSz = 11.0f;  // column labels (BD, SN1...) — 2× larger
		const float colLabelOffY   = 7.5f;   // 2mm lower headlines (less offset above row 0)
		const float rowLabelOffX   = 4.5f;

		for (int slot = 0; slot < kMMVoices; ++slot) {
			const int vi = kMMVoiceLanes[slot];
			const float x = colStart + slot * colStep;

#ifndef METAMODULE
			// Drum-type label above each column
			addChild(fkLabel(Vec(x, mmRowY(0) - colLabelOffY), kMMColLabels[slot], colLabelFontSz));
#endif

			for (int r = 0; r < 7; ++r) {
				addParam(createParamCentered<Trimpot>(
					mm2px(Vec(x, mmRowY(r))), module, paramBases[r] + vi));
#ifndef METAMODULE
				// Row labels on the left of the first column only
				if (slot == 0) {
					addChild(fkLabel(Vec(x - rowLabelOffX, mmRowY(r)), kMMRowLabels[r], labelFontSz, NVG_ALIGN_RIGHT));
				}
#endif
			}

#ifndef METAMODULE
			// Hit indicator light — useful in Rack, excluded in MetaModule (no light support)
			addChild(createLightCentered<SmallLight<RedLight>>(
				mm2px(Vec(x + 5.f, mmRowY(6))), module, Ferroklast::HIT_LIGHT_BASE + vi));
			// TRG / OUT row labels — 40% larger font, extra left offset to clear port
			if (slot == 0) {
				const float trgFontSz  = labelFontSz * 1.4f;
				const float trgOffX    = rowLabelOffX + 1.5f;  // 2mm more right (was +3.5)
				addChild(fkLabel(Vec(x - trgOffX, trigY - 1.0f), kMMRowLabels[7], trgFontSz, NVG_ALIGN_RIGHT));
				addChild(fkLabel(Vec(x - trgOffX, outY  - 1.0f), kMMRowLabels[8], trgFontSz, NVG_ALIGN_RIGHT));
			}
#endif

			addInput(createInputCentered<MVXport_silver>(
				mm2px(Vec(x, trigY)), module, Ferroklast::TRIG_INPUT_BASE + vi));
			addOutput(createOutputCentered<MVXport_silver_red>(
				mm2px(Vec(x, outY)), module, Ferroklast::AUDIO_OUTPUT_BASE + vi));
		}

		// ── Right-side MM CV / output port grid (2 vertical stacks × 5 ports) ──────
		const float cvCol0X = 105.5f;
		const float cvCol1X = 116.5f;
		const float cvGridY0 = 57.0f;
		const float cvGridStepY = 11.0f;
		const float cvLabelOffY = 5.5f;  // 1 higher than previous
		const float cvLabelFontSz = 5.0f;

		const int leftColPorts[5] = {
			Ferroklast::ACCENT_INPUT,
			Ferroklast::DECIMATE_CV_INPUT,
			Ferroklast::COLOR_CV_INPUT,
			Ferroklast::SNARE_CUT_CV_INPUT,
			Ferroklast::PUNCH_CV_INPUT,
		};
		const int rightColPorts[5] = {
			Ferroklast::MATERIAL_CV_INPUT,
			Ferroklast::VARIATION_CV_INPUT,
			Ferroklast::KICK_LENGTH_CV_INPUT,
			Ferroklast::SIDECHAIN_OUTPUT,
			Ferroklast::GROOVE_CV_OUTPUT,
		};
		static const char* kLeftColLabels[5] = {"ACC", "RUIN", "COLOR", "SN CUT", "PUNCH"};
		static const char* kRightColLabels[5] = {"MATERIAL", "VARI", "K LEN", "SC", "GROOVE"};

		for (int r = 0; r < 5; ++r) {
			const float y = cvGridY0 + cvGridStepY * (float)r;
#ifndef METAMODULE
			addChild(fkLabel(Vec(cvCol0X, y - cvLabelOffY), kLeftColLabels[r], cvLabelFontSz));
			addChild(fkLabel(Vec(cvCol1X, y - cvLabelOffY), kRightColLabels[r], cvLabelFontSz));
#endif
			addInput(createInputCentered<MVXport_silver>(
				mm2px(Vec(cvCol0X, y)), module, leftColPorts[r]));
			if (r < 3) {
				addInput(createInputCentered<MVXport_silver>(
					mm2px(Vec(cvCol1X, y)), module, rightColPorts[r]));
			} else {
				addOutput(createOutputCentered<MVXport_silver_red>(
					mm2px(Vec(cvCol1X, y)), module, rightColPorts[r]));
			}
		}

		// Dummy off-screen outputs: REV_WET ports are in the param table but unused in MM DSP
		addOutput(createOutputCentered<MVXport_silver_red>(
			mm2px(Vec(-20.f, -20.f)), module, Ferroklast::REV_WET_L_OUTPUT));
		addOutput(createOutputCentered<MVXport_silver_red>(
			mm2px(Vec(-20.f, -20.f)), module, Ferroklast::REV_WET_R_OUTPUT));
	}
};

} // namespace

Model* modelFerroklast   = createModel<Ferroklast,   FerroklastWidget>  ("Ferroklast");
Model* modelFerroklastMM = createModel<FerroklastMM, FerroklastMMWidget>("FerroklastMM");