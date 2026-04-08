// Phaseon1.cpp — MorphWorx
//
// Copyright (c) 2026 Bemushroomed.  Licensed under GPL-3.0-or-later.
//
// THIRD-PARTY ATTRIBUTION
// ──────────────────────────────────────────────────────────────────────────────
// Portions of this file adapt the TIMBRE and COLOR macro parameter concepts
// from Mutable Instruments Plaits (https://github.com/pichenettes/eurorack).
//
//   Copyright (c) 2021 Emilie Gillet.
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the "Software"),
//   to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense,
//   and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//
// See THIRD_PARTY_LICENSES.md in the repository root for the complete
// list of third-party attributions.
// ──────────────────────────────────────────────────────────────────────────────

#include "plugin.hpp"

#ifndef METAMODULE
#include "ui/PngPanelBackground.hpp"
#include <osdialog.h>
#endif

#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#include "filesystem/helpers.hh"
#include "patch/patch_file.hh"
#include "gui/notification.hh"
#endif

#include <jansson.h>

#include "phaseon/PhaseonOperator.hpp"
#include "phaseon/PhaseonWavetable.hpp"

#include <atomic>
#include <string>
#include <vector>

using namespace rack;

namespace {

// Custom quantity: show descriptive waveform names for Phaseon1's WAVE knob.
struct Phaseon1WaveParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		// 0 WT, 1 Sine, 2 Fat Sub-Triangle, 3 Sharktooth, 4 Asymmetric Pulse-Sine, 5 Folded Sine, 6 Octave Sub-Growl, 7 Sync Saw
		int idx = (int)std::round(getValue());
		if (idx < 0) idx = 0;
		if (idx > 7) idx = 7;
		static const char* kNames[] = {
			"WT",
			"Sine",
			"Fat Sub-Tri",
			"Sharktooth",
			"Pulse-Sine",
			"Folded Sine",
			"Oct Sub-Growl",
			"Sync Saw"
		};
		return kNames[idx];
	}
};

struct Phaseon1AlgoParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int idx = (int)std::round(getValue());
		if (idx < 0) idx = 0;
		if (idx > 13) idx = 13;
		static const char* kNames[] = {
			"Stack",
			"Pairs",
			"Swarm",
			"Wide",
			"Cascade",
			"Fork",
			"Anchor",
			"Pyramid",
			"Triple",
			"Dual Cas",
			"Ring",
			"D.Mod",
			"M.Bus",
			"FB Ladd"
		};
		return kNames[idx];
	}
};

struct Phaseon1SyncDivParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int idx = (int)std::round(getValue());
		if (idx < 0) idx = 0;
		if (idx > 11) idx = 11;
		static const char* kNames[] = {
			"4 Bars",
			"2 Bars",
			"1 Bar",
			"1/2",
			"1/4",
			"1/4T",
			"1/8",
			"1/8T",
			"1/16",
			"1/16T",
			"1/32",
			"1/64"
		};
		return kNames[idx];
	}
};

struct Phaseon1WtFormParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int idx = (int)std::round(getValue());
		if (idx < 0) idx = 0;
		if (idx > 3) idx = 3;
		static const char* kNames[] = {
			"Off",
			"Growl",
			"Yoi",
			"Tear"
		};
		return kNames[idx];
	}
};

static inline float clamp01(float x) {
	return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

static inline float lerp(float a, float b, float t) {
	return a + (b - a) * t;
}

static inline float fastExp2Approx(float x) {
	int i = (int)std::floor(x);
	float f = x - (float)i;
	float poly = 1.0f + f * (0.69314718f + f * (0.24022651f + f * 0.05550411f));
	return std::ldexp(poly, i);
}

static inline float fastExpApprox(float x) {
	// exp(x) = 2^(x / ln(2))
	return fastExp2Approx(x * 1.44269504089f);
}

static inline float fastTanApprox(float x) {
	float s = phaseon::phaseon_fast_sin_w0(x);
	float c = phaseon::phaseon_fast_cos_w0(x);
	if (c < 0.0005f && c > -0.0005f) {
		c = (c >= 0.0f) ? 0.0005f : -0.0005f;
	}
	return s / c;
}

static inline uint32_t xorshift32(uint32_t& state) {
	// Deterministic, fast RNG for per-press voicing variants.
	uint32_t x = state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return x;
}

static inline float u01_from_u32(uint32_t x) {
	// 24-bit mantissa-friendly 0..1
	return (float)((x >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

struct ClockTracker {
	int samplesSinceLastClock = 0;
	float baseFreqHz = 2.0f;
	float currentMultiplier = 1.0f;
	bool clockWasHigh = false;
	float slewCoef = 0.f;       // Cached: recomputed when sample rate changes
	float cachedSampleRate = 0.f;

	void reset() {
		samplesSinceLastClock = 0;
		baseFreqHz = 2.0f;
		currentMultiplier = 1.0f;
		clockWasHigh = false;
	}

	inline float process(bool clockHigh, float divKnob, float sampleRate) {
		// Hoist std::exp: recalculate only when sample rate changes.
		if (sampleRate != cachedSampleRate) {
			cachedSampleRate = sampleRate;
			slewCoef = 1.0f - fastExpApprox(-1.0f / (0.005f * sampleRate));
		}

		samplesSinceLastClock++;
		if (clockHigh && !clockWasHigh) {
			if (samplesSinceLastClock > 0) {
				float instantFreq = sampleRate / (float)samplesSinceLastClock;
				baseFreqHz += 0.2f * (instantFreq - baseFreqHz);
			}
			samplesSinceLastClock = 0;
		}
		clockWasHigh = clockHigh;

		if (samplesSinceLastClock > (int)(sampleRate * 2.0f)) {
			baseFreqHz += 0.001f * (2.0f - baseFreqHz);
		}

		int divIndex = (int)(clamp01(divKnob) * 11.99f);
		float targetMultiplier = 1.0f;
		switch (divIndex) {
		case 0:  targetMultiplier = 0.0625f; break;
		case 1:  targetMultiplier = 0.125f;  break;
		case 2:  targetMultiplier = 0.25f;   break;
		case 3:  targetMultiplier = 0.5f;    break;
		case 4:  targetMultiplier = 1.0f;    break;
		case 5:  targetMultiplier = 1.5f;    break;
		case 6:  targetMultiplier = 2.0f;    break;
		case 7:  targetMultiplier = 3.0f;    break;
		case 8:  targetMultiplier = 4.0f;    break;
		case 9:  targetMultiplier = 6.0f;    break;
		case 10: targetMultiplier = 8.0f;    break;
		case 11: targetMultiplier = 16.0f;   break;
		}

		currentMultiplier += slewCoef * (targetMultiplier - currentMultiplier);
		return baseFreqHz * currentMultiplier;
	}
};

struct KineticLFO {
	float phase = 0.f;
	bool gateWasHigh = false;
	float cachedGravity = -1.f;  // Sentinel: force first-computation
	float cachedG = 1.f;
	float cachedNorm = 1.f;
	// Cached sample-rate reciprocal — avoids per-sample float division (~25 ARM cycles → ~3).
	float cachedSr_      = 0.f;
	float cachedSrRecip_ = 0.f;

	void reset() {
		phase = 0.f;
		gateWasHigh = false;
		cachedGravity = -1.f;
		cachedSr_      = 0.f;
		cachedSrRecip_ = 0.f;
	}

	inline float process(float freqHz, float gravity, bool gateHigh, float sampleRate) {
		if (gateHigh && !gateWasHigh) {
			phase = 0.f;
		}
		gateWasHigh = gateHigh;

		if (sampleRate != cachedSr_) {
			cachedSr_      = sampleRate;
			cachedSrRecip_ = 1.0f / sampleRate;
		}
		float f = std::max(0.01f, std::min(freqHz, sampleRate * 0.25f));
		phase += f * cachedSrRecip_;
		phase -= std::floor(phase);

		float s = phaseon::phaseon_fast_sin_01(phase);

		// Optimization: cache gravity-derived values; only recompute when gravity changes.
		if (gravity != cachedGravity) {
			cachedGravity = gravity;
			cachedG = 1.0f + clamp01(gravity) * 4.0f;
			cachedNorm = 1.0f / std::max(0.001f, phaseon::phaseon_fast_tanh(cachedG));
		}

		return phaseon::phaseon_fast_tanh(s * cachedG) * cachedNorm;
	}
};

static inline float applyMod01(float baseVal, float lfoVal, float trimpotVal) {
	float out = baseVal + (lfoVal * trimpotVal);
	if (out > 1.0f) return 1.0f;
	if (out < 0.0f) return 0.0f;
	return out;
}

static inline float applyMod11(float baseVal, float lfoVal, float trimpotVal) {
	float out = baseVal + (lfoVal * trimpotVal);
	if (out > 1.0f) return 1.0f;
	if (out < -1.0f) return -1.0f;
	return out;
}

struct DubstepSVF {
	float sampleRate = 48000.f;

	// TPT Coefficients
	float g = 0.f;      // Integrator gain
	float k = 1.0f;     // Damping (1/Q)
	float a1 = 0.f, a2 = 0.f, a3 = 0.f;   // Pre-solved loop coefficients

	// States (Stereo)
	float s1[2]{0.f, 0.f};
	float s2[2]{0.f, 0.f};

	// Envelope follower (mono)
	float envFollower = 0.f;
	float currentEnvState = 0.f;
	float attackCoef = 0.1f;
	float releaseCoef = 0.001f;

	// Parameters
	float driveGain = 1.f;
	float compensationGain = 1.f;
	float subInjection = 0.f;

	// Morph coefficients
	float mLP = 1.f, mBP = 0.f, mHP = 0.f;

	// Optimization: precalculate log(fMax/fMin) so updateCoefficients avoids std::log per block.
	float logFMaxMin = 1.f;

	void setSampleRate(float sr) {
		float newRate = std::max(1000.f, sr);
		if (newRate == sampleRate) {
			return;
		}
		sampleRate = newRate;
		// Snappy envelope follower: sub-ms attack and tighter release.
		float atkTime = 0.0008f;
		float relTime = 0.028f;
		attackCoef = 1.0f - fastExpApprox(-1.0f / (atkTime * sampleRate));
		releaseCoef = 1.0f - fastExpApprox(-1.0f / (relTime * sampleRate));
		// Precalculate log ratio for frequency mapping (avoids per-block std::log)
		float fMin = 20.f;
		float fMax = sampleRate * 0.485f;
		logFMaxMin = std::log(fMax / fMin);
	}

	void reset() {
		s1[0] = s1[1] = s2[0] = s2[1] = 0.f;
		envFollower = 0.f;
		currentEnvState = 0.f;
	}

	float getEnvFollower() const {
		return envFollower;
	}

	// Update coefficients based on UI cutoff/res/drive/morph and the current envelope follower.
	void updateCoefficients(float cutoffNorm, float resNorm, float driveNorm, float morph, float envModAmt) {
		cutoffNorm = clamp01(cutoffNorm);
		resNorm = clamp01(resNorm);
		driveNorm = clamp01(driveNorm);
		morph = clamp01(morph);
		if (envModAmt < 0.f) envModAmt = 0.f;
		if (envModAmt > 1.f) envModAmt = 1.f;

		float safeEnv = envFollower;
		if (!std::isfinite(safeEnv)) safeEnv = 0.f;
		if (safeEnv < 0.f) safeEnv = 0.f;
		if (safeEnv > 2.f) safeEnv = 2.f;
		currentEnvState = safeEnv;
		float envAmtShaped = envModAmt * (0.65f - 0.15f * envModAmt);
		float envInfluence = safeEnv * envAmtShaped;
		envInfluence = envInfluence / (1.0f + envInfluence * 0.75f);
		float dyn = cutoffNorm + envInfluence;
		if (dyn < 0.f) dyn = 0.f;
		if (dyn > 0.92f) dyn = 0.92f;

		float fMin = 20.f;
		// Optimization: use precalculated logFMaxMin instead of std::log per block-update.
		float f = fMin * fastExpApprox(dyn * logFMaxMin);

		float wd = 3.14159265f * f / sampleRate;
		g = fastTanApprox(wd);
		// Max Q reduced from 25 to 15 — still aggressive but avoids self-oscillation at default settings.
		float Q = 0.5f + (resNorm * resNorm * 14.5f);
		k = 1.0f / Q;

		a1 = 1.0f / (1.0f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;

		if (!std::isfinite(g) || !std::isfinite(k) || !std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a3)) {
			g = 0.f; k = 1.f; a1 = 1.f; a2 = 0.f; a3 = 0.f;
			mLP = 1.f; mBP = 0.f; mHP = 0.f;
			driveGain = 1.f; compensationGain = 1.f; subInjection = 0.f;
		}

		compensationGain = 1.0f + (resNorm * 0.15f); // Less volume boost at high resonance
		float lowCutNorm = f / 800.0f;
		if (lowCutNorm < 0.0f) lowCutNorm = 0.0f;
		if (lowCutNorm > 1.0f) lowCutNorm = 1.0f;
		float lowCutWeight = 1.0f - lowCutNorm;
		subInjection = resNorm * lowCutWeight * 0.6f;

		driveGain = 1.0f + (driveNorm * driveNorm * 4.5f);

		if (morph < 0.5f) {
			float t = morph * 2.0f;
			mLP = 1.0f - t; mBP = t; mHP = 0.0f;
		} else {
			float t = (morph - 0.5f) * 2.0f;
			mLP = 0.0f; mBP = 1.0f - t; mHP = t;
		}
	}

	inline void updateEnv(float monoIn) {
		if (!std::isfinite(monoIn) || !std::isfinite(envFollower)) {
			envFollower = 0.f;
			return;
		}
		float a = std::fabs(monoIn);
		if (a > envFollower)
			envFollower += attackCoef * (a - envFollower);
		else
			envFollower += releaseCoef * (a - envFollower);
		envFollower = clamp(envFollower, 0.f, 2.f);
		if (std::fabs(envFollower) < 1e-15f) envFollower = 0.f;
	}

	inline void processSample(int ch, float& sample) {
		if (!std::isfinite(sample) || !std::isfinite(s1[ch]) || !std::isfinite(s2[ch]) ||
			!std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a3) ||
			!std::isfinite(g) || !std::isfinite(k)) {
			if (ch == 0) reset();
			sample = 0.f;
			return;
		}

		float cleanInput = sample;
		float x = sample;

		float driveAmt = (driveGain - 1.0f) / 4.5f;
		if (driveAmt < 0.f) driveAmt = 0.f;

		float drivenIn = sample * driveGain;
		float offset = driveAmt * 0.25f;
		float driven = phaseon::phaseon_fast_tanh(drivenIn + offset) - phaseon::phaseon_fast_tanh(offset);
		float drivenMix = driven * 0.90f + drivenIn * 0.10f;
		x = lerp(sample, drivenMix, driveAmt);

		float resFeedback = lerp(s2[ch], phaseon::phaseon_fast_tanh(s2[ch]), driveAmt);

		float v3 = (x - s1[ch] * (g + k) - resFeedback) * a1;
		float v1 = v3 * g + s1[ch];
		float v2 = v1 * g + s2[ch];

		s1[ch] = 2.0f * v1 - s1[ch];
		s2[ch] = 2.0f * v2 - s2[ch];

		if (std::fabs(s1[ch]) < 1e-15f) s1[ch] = 0.f;
		if (std::fabs(s2[ch]) < 1e-15f) s2[ch] = 0.f;

		float out = (mLP * v2) + (mBP * v1) + (mHP * v3);

		// Optimization: x^1.5 = x * sqrt(x) — avoids slow std::pow, uses HW sqrt.
		float envVal = std::max(0.0f, std::min(2.0f, currentEnvState));
		float envBloom = envVal * std::sqrt(envVal);
		float fizzScale = driveAmt * 0.2f * (1.0f - mHP) * envBloom; // Reduced: was 0.6, harsh HP injection
		float highFizz = phaseon::phaseon_fast_tanh(v3 * 3.0f) * fizzScale;
		out += highFizz;

		out += (cleanInput * subInjection * mLP);
		out *= compensationGain;

		float outSat = phaseon::phaseon_fast_tanh(out * 0.833f) * 1.2f;
		out = lerp(out, outSat, driveAmt);

		if (!std::isfinite(out)) {
			if (ch == 0) reset();
			sample = 0.f;
			return;
		}

		sample = out;
	}
};

struct SimpleAR {
	float level = 0.f;
	bool gate = false;
	float attackCoeff = 0.0f;   // Precomputed: dt / attackTime
	float decayCoeff  = 0.0f;   // Precomputed: dt / decayTime
	// Cached inputs to avoid redundant recomputation
	float prevAtk = -1.f;
	float prevDec = -1.f;
	float prevDt  = -1.f;

	void reset() {
		level = 0.f;
		gate = false;
		prevAtk = prevDec = prevDt = -1.f;
		attackCoeff = 0.0f;
		decayCoeff = 0.0f;
	}
	void setGate(bool g) { gate = g; }

	// Pass dt (= 1/sampleRate) alongside times. Only recomputes when values change.
	void setTimes(float atk, float dec, float dt) {
		if (!std::isfinite(atk) || !std::isfinite(dec) || !std::isfinite(dt) || dt <= 0.f) {
			attackCoeff = 0.f;
			decayCoeff = 0.f;
			prevAtk = prevDec = prevDt = -1.f;
			return;
		}
		if (atk == prevAtk && dec == prevDec && dt == prevDt) return;
		prevAtk = atk; prevDec = dec; prevDt = dt;
		float attackTime = std::max(0.0005f, atk);
		float decayTime  = std::max(0.0005f, dec);
		attackCoeff = dt / attackTime;
		if (attackCoeff > 1.f) attackCoeff = 1.f;
		decayCoeff = dt / decayTime;
		if (decayCoeff  > 1.f) decayCoeff  = 1.f;
	}

	float tick() {
		// Optimization: division moved to setTimes; no division per sample.
		float target = gate ? 1.f : 0.f;
		float coeff  = gate ? attackCoeff : decayCoeff;
		if (!std::isfinite(level) || !std::isfinite(coeff)) {
			level = 0.f;
			coeff = 0.f;
		}
		coeff = clamp01(coeff);
		level += (target - level) * coeff;
		if (!std::isfinite(level)) level = 0.f;
		level = clamp01(level);
		if (std::fabs(level) < 1e-15f) level = 0.f;
		return level;
	}
};

struct FormantShaper {
	float sampleRate = 48000.f;
	float s1[2][2] = {{0.f, 0.f}, {0.f, 0.f}};
	float s2[2][2] = {{0.f, 0.f}, {0.f, 0.f}};
	// Optimization: slew the g (tangent) coefficient directly instead of slewing
	// frequency and calling std::tan + std::exp per sample (6x each).
	float currentG[2] = {0.f, 0.f};  // Smoothed integrator gain per band
	float targetG[2]  = {0.f, 0.f};  // Target g, computed at block-rate
	float slewCoef = 0.f;                  // Cached slew coefficient (exp)
	float hpState[2] = {0.f, 0.f};
	float lpState[2] = {0.f, 0.f};
	int divider = 0;
	float cachedMorph = 0.f;
	float cachedIntensity = 0.f;
	float lastRequestedIntensity = 0.f;
	float cachedDrive = 0.f;

	void reset() {
		divider = 0;
		cachedMorph = 0.f;
		cachedIntensity = 0.f;
		lastRequestedIntensity = 0.f;
		cachedDrive = 0.f;
		for (int ch = 0; ch < 2; ++ch) {
			hpState[ch] = 0.f;
			lpState[ch] = 0.f;
			for (int i = 0; i < 2; ++i) {
				s1[ch][i] = 0.f;
				s2[ch][i] = 0.f;
			}
		}
		for (int i = 0; i < 2; ++i) { currentG[i] = 0.f; targetG[i] = 0.f; }
	}

	// Optimization: slewCoef is computed once per block in update(); g is slewed
	// at sample-rate but std::exp and std::tan are no longer called per sample.
	inline float processBandpass(int ch, int band, float input, float Q) {
		currentG[band] += slewCoef * (targetG[band] - currentG[band]);

		float g  = currentG[band];
		float k  = 1.0f / std::max(0.1f, Q);
		float a1 = 1.0f / (1.0f + g * (g + k));

		float v3 = (input - s1[ch][band] * (g + k) - s2[ch][band]) * a1;
		float v1 = v3 * g + s1[ch][band];
		float v2 = v1 * g + s2[ch][band];

		s1[ch][band] = 2.0f * v1 - s1[ch][band];
		s2[ch][band] = 2.0f * v2 - s2[ch][band];

		if (std::fabs(s1[ch][band]) < 1e-15f) s1[ch][band] = 0.f;
		if (std::fabs(s2[ch][band]) < 1e-15f) s2[ch][band] = 0.f;

		if (!std::isfinite(v1) || !std::isfinite(s1[ch][band]) || !std::isfinite(s2[ch][band])) {
			s1[ch][band] = 0.f;
			s2[ch][band] = 0.f;
			return 0.f;
		}

		return v1;
	}

	void update(float sr, float fundamentalHz, float vowelPos, float formantAmount) {
		sampleRate = std::max(1000.f, sr);
		cachedMorph = clamp01(vowelPos);
		cachedIntensity = clamp01(formantAmount);
		cachedDrive = 1.0f; // No pre-filter drive: saturating before high-Q bandpass is the main screech cause
		// Compute slewCoef once per block (was computed 6x per sample before).
		slewCoef = 1.0f - fastExpApprox(-1.0f / (0.005f * sampleRate));

		// Fixed-Hz dual formants (vowel morph only), preserves speech character under pitch changes.
		const float f1A = 350.f;
		const float f2A = 750.f;
		const float f1B = 600.f;
		const float f2B = 1150.f;
		float f1Target = lerp(f1A, f1B, cachedMorph);
		float f2Target = lerp(f2A, f2B, cachedMorph);

		const float piOverSr = 3.14159265f / sampleRate;
		targetG[0] = fastTanApprox(piOverSr * f1Target);
		targetG[1] = fastTanApprox(piOverSr * f2Target);
	}

	inline float processChannel(int ch, float input, float intensity) {
		if (intensity <= 0.001f) return input;

		// Feed clean signal directly — no pre-filter drive into resonant BP.
		// Q range 3.5–6.5: speech-realistic vowel character, won't self-oscillate.
		float Q = 3.5f + (intensity * 3.0f);
		float b1 = processBandpass(ch, 0, input, Q);
		float b2 = processBandpass(ch, 1, input, Q);

		// Gentle post-filter level — tanh limits peaks without hard clipping.
		float formantSum = (b1 + b2 * 0.85f) * 0.75f;
		formantSum = phaseon::phaseon_fast_tanh(formantSum);

		// Preserve more dry; formant is a timbral colour, not a replacement.
		float dryDucked = input * (1.0f - intensity * 0.3f);
		float finalOut = dryDucked + (formantSum * intensity * 0.55f);
		if (!std::isfinite(finalOut)) return 0.f;
		return finalOut;
	}

	void process(float& outL, float& outR, float sr, float fundamentalHz, float vowelPos, float formantAmount) {
		if (!std::isfinite(outL)) outL = 0.f;
		if (!std::isfinite(outR)) outR = 0.f;
		float reqVowel = std::isfinite(vowelPos) ? clamp01(vowelPos) : 0.f;
		float reqIntensity = std::isfinite(formantAmount) ? clamp01(formantAmount) : 0.f;

		// Fast-path hard bypass when formant is effectively off.
		if (reqIntensity <= 0.0005f && cachedIntensity <= 0.0005f) {
			for (int ch = 0; ch < 2; ++ch) {
				hpState[ch] = 0.f;
				lpState[ch] = 0.f;
				for (int i = 0; i < 2; ++i) {
					s1[ch][i] = 0.f;
					s2[ch][i] = 0.f;
				}
			}
			for (int i = 0; i < 2; ++i) {
				currentG[i] = 0.f;
				targetG[i] = 0.f;
			}
			cachedIntensity = 0.f;
			lastRequestedIntensity = 0.f;
			return;
		}

		if (++divider >= 16 || std::fabs(reqIntensity - lastRequestedIntensity) > 0.05f) {
			divider = 0;
			update(sr, fundamentalHz, reqVowel, reqIntensity);
			lastRequestedIntensity = reqIntensity;
		}

		outL = processChannel(0, outL, cachedIntensity);
		outR = processChannel(1, outR, cachedIntensity);
	}
};

struct RetroCrusher {
	float sampleRate = 48000.f;
	float holdValue = 0.f;
	float timerPhase = 0.f;

	// Optimization: cache expensive pow() results; only recompute when crushNorm changes.
	float cachedCrushNorm = -1.f;
	float cachedPhaseInc  = 0.f;
	float cachedSteps     = 0.f;
	float cachedFoldFade  = 0.f;

	void setSampleRate(float sr) {
		float newRate = std::max(1000.f, sr);
		if (newRate == sampleRate) {
			return;
		}
		sampleRate = newRate;
		cachedCrushNorm = -1.f; // Force recalculation only after an actual SR change
	}

	void reset() {
		holdValue = 0.f;
		timerPhase = 0.f;
	}

	inline float process(float input, float crushNorm) {
		if (crushNorm <= 0.001f) return input;

		// Recompute derived state only when parameter changes.
		if (crushNorm != cachedCrushNorm) {
			cachedCrushNorm = crushNorm;
			float cn = clamp01(crushNorm);
			float targetSR = sampleRate * fastExpApprox(-3.5065579f * cn);
			cachedPhaseInc = targetSR / sampleRate;

			float bits = 16.0f - (cn * 13.0f);
			cachedSteps = fastExp2Approx(bits);

			float foldFade = (cn - 0.65f) / 0.15f;
			if (foldFade < 0.f) foldFade = 0.f;
			if (foldFade > 1.f) foldFade = 1.f;
			cachedFoldFade = foldFade * foldFade * (3.0f - 2.0f * foldFade);
		}

		timerPhase += cachedPhaseInc;

		if (timerPhase >= 1.0f) {
			timerPhase -= 1.0f;

			float crushed = input;
			if (cachedFoldFade > 0.f) {
				float folded = std::sin(crushed * 3.14159f * (1.0f + (cachedFoldFade * 1.6f)));
				crushed = crushed + (folded - crushed) * cachedFoldFade;
			}
			holdValue = std::round(crushed * cachedSteps) / cachedSteps;
		}

		float finalOut = holdValue;
		if (!std::isfinite(finalOut)) return 0.f;
		return finalOut;
	}
};

struct MasterVoiceBus {
	float sampleRate = 48000.f;
	float lpState = 0.f;
	float dcBlockerState = 0.f;
	float dcBlockerPrev = 0.f;

	void setSampleRate(float sr) {
		float newRate = std::max(1000.f, sr);
		if (newRate == sampleRate) {
			return;
		}
		sampleRate = newRate;
	}

	void reset() {
		lpState = 0.f;
		dcBlockerState = 0.f;
		dcBlockerPrev = 0.f;
	}

	inline void splitSub(float input, float& cleanSub, float& midHighs) {
		lpState += 0.013f * (input - lpState);
		cleanSub = lpState;
		midHighs = input - cleanSub;
	}

	inline float finalize(float combinedSignal, float vcaEnvelope) {
		if (!std::isfinite(combinedSignal) || !std::isfinite(vcaEnvelope)) {
			reset();
			return 0.f;
		}
		float output = combinedSignal * vcaEnvelope;
		float centered = output - dcBlockerPrev + 0.998f * dcBlockerState;
		if (!std::isfinite(centered)) {
			reset();
			return 0.f;
		}
		if (std::fabs(centered) < 1e-15f) centered = 0.f;
		dcBlockerState = centered;
		dcBlockerPrev = output;
		return phaseon::phaseon_fast_tanh(centered / 1.5f);
	}
};

static phaseon::Wavetable downsampleWavetableSmall(const phaseon::Wavetable& src) {
	// Target: small, cache-friendly wavetable
	constexpr int kDstSize = 256;
	constexpr int kDstFrames = 4;

	phaseon::Wavetable dst;
	dst.name = src.name;
	dst.frameSize = kDstSize;
	dst.frameCount = std::max(1, std::min(kDstFrames, src.frameCount));
	dst.data.resize((size_t)dst.frameSize * (size_t)dst.frameCount);

	if (src.frameSize <= 0 || src.frameCount <= 0 || src.data.empty()) {
		std::fill(dst.data.begin(), dst.data.end(), 0.f);
		return dst;
	}

	// Frame remap (if src has fewer frames than dst wants, just map 1:1)
	for (int df = 0; df < dst.frameCount; ++df) {
		float srcFramePos = (dst.frameCount <= 1)
			? 0.f
			: (float)df * (float)(src.frameCount - 1) / (float)(dst.frameCount - 1);
		int sf0 = (int)srcFramePos;
		int sf1 = std::min(src.frameCount - 1, sf0 + 1);
		float ft = srcFramePos - (float)sf0;
		const float* f0 = src.data.data() + (size_t)sf0 * (size_t)src.frameSize;
		const float* f1 = src.data.data() + (size_t)sf1 * (size_t)src.frameSize;

		float* out = dst.data.data() + (size_t)df * (size_t)dst.frameSize;
		for (int i = 0; i < kDstSize; ++i) {
			float srcPos = (float)i * (float)src.frameSize / (float)kDstSize;
			int idx0 = (int)srcPos;
			float it = srcPos - (float)idx0;
			int idx1 = idx0 + 1;
			if (idx0 >= src.frameSize) idx0 = src.frameSize - 1;
			if (idx1 >= src.frameSize) idx1 = 0;

			float s0 = f0[idx0] + (f0[idx1] - f0[idx0]) * it;
			float s1 = f1[idx0] + (f1[idx1] - f1[idx0]) * it;
			out[i] = s0 + (s1 - s0) * ft;
		}
	}

	// Keep the Phaseon1 wavetable in a single-resolution form.
	// The older build that matched the intended preset tone did not generate mipmaps.
	dst.mipData.clear();
	return dst;
}

} // namespace

struct Phaseon1 : Module {
	enum ParamId {
		// =====================================================================
		// v0.2 BREAKING CHANGE: params reordered to match MetaModule panel
		// layout (top-to-bottom, left-to-right scan). Old Phbank.bnk files
		// saved with v1 of the bank format will have params migrated to
		// factory defaults — preset names are preserved.
		// =====================================================================

		// Right-side utility knobs (top of panel, right column)
		PRESET_INDEX_PARAM,
		VOLUME_PARAM,
		WARP_PARAM,

		// Row 1 (y≈26mm): left utility column first, then main grid L→R
		EDIT_OP_PARAM,
		WAVE_PARAM,
		ALGO_PARAM,
		OP_FREQ_PARAM,
		OP_LEVEL_PARAM,
		CHAR_PARAM,

		// Row 2 (y≈37mm)
		SUB_PARAM,
		DENSITY_PARAM,
		MORPH_PARAM,
		COMPLEX_PARAM,
		MOTION_PARAM,
		FM_ENV_AMOUNT_PARAM,

		// Row 3 (y≈47mm)
		TEAR_PARAM,
		ATTACK_PARAM,
		SYNC_ENV_PARAM,
		FILTER_ENV_PARAM,
		LFO_GRAVITY_PARAM,
		FILTER_RESONANCE_PARAM,

		// Row 4 (y≈58mm)
		WT_FORM_PARAM,
		FILTER_MORPH_PARAM,
		COLOR_PARAM,
		BITCRUSH_PARAM,
		WARMTH_PARAM,
		FILTER_DRIVE_PARAM,

		// Row 5 (y≈68mm): left utility column switches first, then main grid
		TEAR_NOISE_PARAM,
		WT_SCROLL_MODE_PARAM,
		TIMBRE_PARAM,
		FORMANT_PARAM,
		EDGE_PARAM,
		DECAY_PARAM,
		FILTER_CUTOFF_PARAM,
		RANDOMIZE_BUTTON_PARAM,
		LFO_ENABLE_PARAM,

		// Row 6 (y≈78mm): modulation trimpots
		WT_FRAME_MOD_TRIMPOT,
		VOWEL_MOD_TRIMPOT,
		OP1_FB_MOD_TRIMPOT,
		DECAY_MOD_TRIMPOT,
		CUTOFF_MOD_TRIMPOT,

		// Non-visible / hidden controls
		LFO_SYNC_DIV_PARAM,       // clock sync division (no knob; set via context menu)
		SYNC_DIV_MOD_TRIMPOT,     // removed from panel, retained for future use
		PRESET_SAVE_PARAM,        // momentary save button

		// Hidden per-operator storage (values persist in patches/presets)
		OP1_WAVE_STORE_PARAM,
		OP2_WAVE_STORE_PARAM,
		OP3_WAVE_STORE_PARAM,
		OP4_WAVE_STORE_PARAM,
		OP1_FREQ_STORE_PARAM,
		OP2_FREQ_STORE_PARAM,
		OP3_FREQ_STORE_PARAM,
		OP4_FREQ_STORE_PARAM,
		OP1_LEVEL_STORE_PARAM,
		OP2_LEVEL_STORE_PARAM,
		OP3_LEVEL_STORE_PARAM,
		OP4_LEVEL_STORE_PARAM,

		RANDOMIZE_SEED_PARAM,    // stored seed for deterministic randomize

		PARAMS_LEN
	};
	enum InputId {
		GATE_INPUT,
		VOCT_INPUT,
		FORMANT_CV_INPUT,
		CLOCK_INPUT,
		// CV modulation inputs (5V = full sweep, bipolar ±5V supported)
		WARP_CV_INPUT,
		MORPH_CV_INPUT,
		COMPLEX_CV_INPUT,
		MOTION_CV_INPUT,
		ATTACK_CV_INPUT,
		DECAY_CV_INPUT,
		BITCRUSH_CV_INPUT,
		WTFRAME_CV_INPUT,
		CUTOFF_CV_INPUT,
		RESONANCE_CV_INPUT,
		DRIVE_CV_INPUT,
		LFO_GRAVITY_CV_INPUT,
		ALL_FREQ_CV_INPUT,
		OP1_LEVEL_CV_INPUT,
		CHARACTER_CV_INPUT,
		SYNC_DIV_CV_INPUT,  // 0-10V → 12 snapping steps; no cable = 1 Bar
		PRESET_CV_INPUT,    // 0-10V → preset slots 1..127
		COLOR_CV_INPUT,     // 0-10V / ±5V → color spectral macro
		EDGE_CV_INPUT,      // ±5V / 0-10V → feedback drive / edge
		INPUTS_LEN
	};
	enum OutputId {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		LFO_OUTPUT,
		ENV_OUTPUT,  // Envelope follower: 0-10V
		OUTPUTS_LEN
	};
	enum LightId {
		GATE_LIGHT,
		LIGHTS_LEN
	};

#ifdef METAMODULE
	enum DisplayId {
		PRESET_DISPLAY = LIGHTS_LEN,
		DISPLAY_IDS_LEN
	};
#endif

	static constexpr int kPresetSlots = 127;
	struct PresetSlot {
		bool used = false;
		std::string name;
		float params[PARAMS_LEN]{};
	};

	static constexpr float kInternalRate = 40960.0f;
	// Shorter internal block for faster envelope/gate reaction and lower modulation latency.
	static constexpr int kBlockSize = 16;

	phaseon::Operator ops[4];
	SimpleAR ampEnv;
	FormantShaper formant;
	RetroCrusher retroCrushL;
	RetroCrusher retroCrushR;
	MasterVoiceBus masterBusL;
	MasterVoiceBus masterBusR;
	ClockTracker clockSync;
	KineticLFO internalLfo;
	float lastLfoValue = 0.f;
	DubstepSVF filter;
	float fundamentalHz = 261.63f;
	float subPhase = 0.f;

	// Tear FX (mids/highs only): short comb + fold + noise.
	static constexpr int kTearDelaySize = 256; // power-of-two for masking
	float tearDelayL[kTearDelaySize] = {};
	float tearDelayR[kTearDelaySize] = {};
	int tearDelayWrite = 0;
	uint32_t tearNoiseState = 0x6D2B79F5u;
	float tearWet = 0.f; // smoothed 0..1

	// UI edit state
	int cachedEditOp = 0; // 0..3
	float cachedWaveEdit = -999.f;
	float cachedFreqEdit = -999.f;
	float cachedLevelEdit = -999.f;

	// Loaded single wavetable (small)
	phaseon::Wavetable wtBuf[2];
	std::atomic<int> wtActive{0};
	std::atomic<bool> hasWt{false};
	std::string wtName;

	// Internal-rate block buffer (with carry sample at index 0)
	float blockL[kBlockSize + 1] = {};
	float blockR[kBlockSize + 1] = {};
	float carryL = 0.f;
	float carryR = 0.f;
	int readIndex = 0;    // 0..kBlockSize-1
	float frac = 0.f;     // 0..1 within internal sample
	// Gate-edge de-click crossfade (host-rate, very short).
	float gateEdgeFromL = 0.f;
	float gateEdgeFromR = 0.f;
	float lastPreVolL = 0.f;
	float lastPreVolR = 0.f;
	int gateEdgeXfadeRemain = 0;
	int gateEdgeXfadeTotal = 0;
	float lastVoct = 0.f;
	float heldGateAnchorVoct = 0.f;
	bool heldPitchChangePending = false;
	int heldPitchStableSamples = 0;

	bool lastGate = false;
	bool lastPresetSave = false;
	bool lastRandomize = false;
	bool presetInitDone = false;
	int presetStartupLockSamples = 0;
#ifdef METAMODULE
	// Debounce PRESET_CV_INPUT on MetaModule: bankApplySlot() calls renderInternalBlock()
	// three times plus resets all DSP state. A slowly-drifting CV jittering across a
	// slot boundary fires this every sample without a cooldown -> CPU spikes.
	// Allow at most one slot switch per ~0.5 s (24 000 samples at 48 kHz).
	static constexpr uint32_t kPresetCvCooldownSamples = 24000;
	uint32_t mmPresetCvCooldown = 0;
#endif
	bool pendingPresetSwitchMute = false;
	int presetSwitchMuteSamples = 0;

	PresetSlot bank[kPresetSlots];
	int currentPreset = 0;
	std::string presetName = "Init";
	bool bankLoaded = false;
	std::string bankFilePath;
	#ifdef METAMODULE
	char mmPresetName[32] = "Init";
	#endif
	static bool sPresetClipboardValid;
	static PresetSlot sPresetClipboard;

	#ifdef METAMODULE
	static void copyPresetNameToBuf(const std::string& src, char* out, size_t outSize) {
		if (!out || outSize == 0) return;
		if (src.empty()) {
			snprintf(out, outSize, "%s", "Init");
			return;
		}
		snprintf(out, outSize, "%.30s", src.c_str());
	}

	void syncPresetDisplayName(const std::string& src) {
		copyPresetNameToBuf(src, mmPresetName, sizeof(mmPresetName));
	}

	void syncPresetDisplayName(const char* src) {
		if (!src || !src[0]) {
			snprintf(mmPresetName, sizeof(mmPresetName), "%s", "Init");
			return;
		}
		snprintf(mmPresetName, sizeof(mmPresetName), "%.30s", src);
	}

	const char* activePresetNameCstr() const {
		return mmPresetName[0] ? mmPresetName : "Init";
	}
	#else
	void syncPresetDisplayName(const std::string& src) {
		presetName = src;
	}

	void syncPresetDisplayName(const char* src) {
		presetName = src ? src : "Init";
	}

	const char* activePresetNameCstr() const {
		return presetName.c_str();
	}
	#endif

	static int clampOpIndex(int v) {
		if (v < 0) return 0;
		if (v > 3) return 3;
		return v;
	}

	static ParamId waveStoreParamForOp(int op) {
		return (ParamId)(OP1_WAVE_STORE_PARAM + clampOpIndex(op));
	}
	static ParamId freqStoreParamForOp(int op) {
		return (ParamId)(OP1_FREQ_STORE_PARAM + clampOpIndex(op));
	}
	static ParamId levelStoreParamForOp(int op) {
		return (ParamId)(OP1_LEVEL_STORE_PARAM + clampOpIndex(op));
	}

	int getEditOpIndex() {
		int v = (int)std::round(params[EDIT_OP_PARAM].getValue());
		return clampOpIndex(v - 1);
	}

	void syncEditKnobsFromStore(int op) {
		params[WAVE_PARAM].setValue(params[waveStoreParamForOp(op)].getValue());
		params[OP_FREQ_PARAM].setValue(params[freqStoreParamForOp(op)].getValue());
		params[OP_LEVEL_PARAM].setValue(params[levelStoreParamForOp(op)].getValue());
		cachedWaveEdit = params[WAVE_PARAM].getValue();
		cachedFreqEdit = params[OP_FREQ_PARAM].getValue();
		cachedLevelEdit = params[OP_LEVEL_PARAM].getValue();
	}

	void pushEditKnobsToStoreIfChanged(int op) {
		float w = params[WAVE_PARAM].getValue();
		float f = params[OP_FREQ_PARAM].getValue();
		float l = params[OP_LEVEL_PARAM].getValue();
		// Only write when changed to avoid stomping values while we are syncing.
		if (w != cachedWaveEdit) {
			params[waveStoreParamForOp(op)].setValue(w);
			cachedWaveEdit = w;
		}
		if (f != cachedFreqEdit) {
			params[freqStoreParamForOp(op)].setValue(f);
			cachedFreqEdit = f;
		}
		if (l != cachedLevelEdit) {
			params[levelStoreParamForOp(op)].setValue(l);
			cachedLevelEdit = l;
		}
	}

	void updateEditState() {
		int op = getEditOpIndex();
		if (op != cachedEditOp) {
			cachedEditOp = op;
			syncEditKnobsFromStore(op);
			return;
		}
		pushEditKnobsToStoreIfChanged(op);
	}

	const char* displayTextCstr() {
		// PRESET_INDEX_PARAM is 1-based (1..kPresetSlots).
		int idx1 = (int)std::round(params[PRESET_INDEX_PARAM].getValue());
		if (idx1 < 1) idx1 = 1;
		if (idx1 > kPresetSlots) idx1 = kPresetSlots;
		// Format: "01/127 Name"
		char buf[64];
		snprintf(buf, sizeof(buf), "%02d/%d %s",
			idx1, kPresetSlots, activePresetNameCstr());
		static char sBuf[64];
		memcpy(sBuf, buf, sizeof(buf));
		return sBuf;
	}

	static std::string defaultSlotName(int idx) {
		char buf[32];
		snprintf(buf, sizeof(buf), "Preset %02d", idx + 1);
		return buf;
	}

	static std::string initSlotName(int idx) {
		char buf[32];
		snprintf(buf, sizeof(buf), "Init %02d", idx + 1);
		return buf;
	}

	bool isPresetApplyGateHigh() {
#ifdef METAMODULE
		if (!inputs[GATE_INPUT].isConnected()) {
			return true;
		}
#endif
		return inputs[GATE_INPUT].getVoltage() > 1.0f;
	}

	void resetHeldPitchTracking(float voct = 0.f) {
		lastVoct = voct;
		heldGateAnchorVoct = voct;
		heldPitchChangePending = false;
		heldPitchStableSamples = 0;
	}

	void armGateEdgeXfade(float sampleRate) {
		gateEdgeFromL = lastPreVolL;
		gateEdgeFromR = lastPreVolR;
		gateEdgeXfadeTotal = (int)(sampleRate * 0.00025f);
		if (gateEdgeXfadeTotal < 2) gateEdgeXfadeTotal = 2;
		if (gateEdgeXfadeTotal > 32) gateEdgeXfadeTotal = 32;
		gateEdgeXfadeRemain = gateEdgeXfadeTotal;
	}

	float currentVoctInputValue() {
		if (!inputs[VOCT_INPUT].isConnected()) {
			return 0.f;
		}
		float voct = inputs[VOCT_INPUT].getVoltage();
		if (voct < -4.f) voct = -4.f;
		if (voct > 4.f) voct = 4.f;
		return voct;
	}

	void warmPresetSwitchState() {
		bool gateHigh = isPresetApplyGateHigh();
		fundamentalHz = 261.63f * fastExp2Approx(currentVoctInputValue());
		ampEnv.setGate(gateHigh);
		ampEnv.level = gateHigh ? 1.0f : 0.0f;
		frac = 0.f;
		for (int flush = 0; flush < 3; ++flush) {
			renderInternalBlock();
		}
		// Force the next host-rate process() pass to resync edge handling from live inputs.
		lastGate = false;
		resetHeldPitchTracking(currentVoctInputValue());
	}

	// Shared volume normaliser (MetaModule only).
	// Returns an empty string for unsaved RAM-only patches so callers can decide
	// how to fall back.
#ifdef METAMODULE
	static std::string normalizeVolume(std::string vol) {
		if (vol == "ram:/" || vol == "ram:") return std::string();
		if (!vol.empty() && vol.back() != '/') vol.push_back('/');
		return vol;
	}

	static void appendUniqueString(std::vector<std::string>& values, const std::string& value) {
		if (value.empty()) return;
		for (const std::string& existing : values) {
			if (existing == value) return;
		}
		values.push_back(value);
	}

	static std::vector<std::string> metaModuleVolumeSearchOrder() {
		std::vector<std::string> volumes;
		appendUniqueString(volumes, normalizeVolume(std::string(MetaModule::Patch::get_volume())));
		appendUniqueString(volumes, std::string("sdc:/"));
		appendUniqueString(volumes, std::string("usb:/"));
		appendUniqueString(volumes, std::string("nor:/"));
		if (volumes.empty()) {
			volumes.push_back("sdc:/");
		}
		return volumes;
	}

	static std::string preferredPatchVolume() {
		std::vector<std::string> volumes = metaModuleVolumeSearchOrder();
		return volumes.empty() ? std::string("sdc:/") : volumes.front();
	}

	static bool metaModulePathExists(const std::string& path) {
		if (path.empty()) return false;
		if (system::exists(path)) return true;
		if (!MetaModule::Filesystem::is_local_path(path)) {
			std::string absolutePath = system::getAbsolute(path);
			if (!absolutePath.empty() && absolutePath != path) {
				return system::exists(absolutePath);
			}
		}
		return false;
	}

	static std::string resolveMetaModuleOpenPath(const std::string& path) {
		if (path.empty()) return path;
		if (MetaModule::Filesystem::is_local_path(path)) return path;
		if (system::exists(path)) return path;
		std::string absolutePath = system::getAbsolute(path);
		return absolutePath.empty() ? path : absolutePath;
	}

	static std::string bundledMetaModulePath(const char* relativePath) {
		return resolveMetaModuleOpenPath(asset::plugin(pluginInstance, relativePath));
	}
#endif

	std::string bankPath() const {
#ifdef METAMODULE
		if (!bankFilePath.empty() && MetaModule::Filesystem::is_local_path(bankFilePath)) {
			return bankFilePath;
		}
		return preferredPatchVolume() + "phaseon1/Phbank.bnk";
#else
		if (!bankFilePath.empty()) return bankFilePath;
		return asset::user("MorphWorx/phaseon1/Phbank.bnk");
#endif
	}

#ifdef METAMODULE
	// Returns the default wavetable path on the preferred patch volume.
	std::string defaultWtPath() const {
		return preferredPatchVolume() + "phaseon1/phaseon1.wav";
	}

	std::string defaultWtBrowserDir() const {
		return preferredPatchVolume() + "phaseon1/";
	}
#endif

	void fillFactoryDefaultSlot(PresetSlot& s, const std::string& name) {
		s.used = true;
		s.name = name;
		for (int p = 0; p < PARAMS_LEN; ++p) {
			float v = params[p].getValue();
			if (p >= 0 && p < (int)paramQuantities.size()) {
				ParamQuantity* q = paramQuantities[p];
				if (q) {
					v = q->getDefaultValue();
				}
			}
			if (p == PRESET_SAVE_PARAM || p == RANDOMIZE_BUTTON_PARAM) {
				v = 0.f;
			}
			s.params[p] = sanitizeParamValue(p, v);
		}
	}

	void fillFactoryDefaultSlotParams(PresetSlot& s) {
		s.used = true;
		for (int p = 0; p < PARAMS_LEN; ++p) {
			float v = params[p].getValue();
			if (p >= 0 && p < (int)paramQuantities.size()) {
				ParamQuantity* q = paramQuantities[p];
				if (q) {
					v = q->getDefaultValue();
				}
			}
			if (p == PRESET_SAVE_PARAM || p == RANDOMIZE_BUTTON_PARAM) {
				v = 0.f;
			}
			s.params[p] = sanitizeParamValue(p, v);
		}
	}

	void captureCurrentToPresetSlot(PresetSlot& s, const std::string& fallbackName) {
		s.used = true;
		if (s.name.empty()) {
			s.name = fallbackName;
		}
		for (int p = 0; p < PARAMS_LEN; ++p) {
			float v = params[p].getValue();
			if (p == PRESET_SAVE_PARAM || p == RANDOMIZE_BUTTON_PARAM) {
				v = 0.f;
			}
			s.params[p] = sanitizeParamValue(p, v);
		}
	}

	float sanitizeParamValue(int paramIndex, float value) {
		if (!std::isfinite(value)) {
			return params[paramIndex].getValue();
		}
		if (paramIndex >= 0 && paramIndex < (int)paramQuantities.size()) {
			ParamQuantity* q = paramQuantities[paramIndex];
			if (q) {
				float lo = q->getMinValue();
				float hi = q->getMaxValue();
				if (value < lo) value = lo;
				if (value > hi) value = hi;
			}
		}
		return value;
	}

	static int migrateVersion1ParamIndex(int oldIndex) {
		switch (oldIndex) {
			case 0: return DENSITY_PARAM;
			case 1: return TIMBRE_PARAM;
			case 2: return EDGE_PARAM;
			case 3: return ALGO_PARAM;
			case 4: return CHAR_PARAM;
			case 5: return WAVE_PARAM;
			case 6: return FORMANT_PARAM;
			case 7: return BITCRUSH_PARAM;
			case 8: return EDIT_OP_PARAM;
			case 9: return OP_FREQ_PARAM;
			case 10: return OP_LEVEL_PARAM;
			case 11: return ATTACK_PARAM;
			case 12: return DECAY_PARAM;
			case 13: return MOTION_PARAM;
			case 14: return MORPH_PARAM;
			case 15: return COMPLEX_PARAM;
			case 16: return FILTER_CUTOFF_PARAM;
			case 17: return FILTER_RESONANCE_PARAM;
			case 18: return FILTER_DRIVE_PARAM;
			case 19: return FILTER_MORPH_PARAM;
			case 20: return FILTER_ENV_PARAM;
			case 21: return PRESET_INDEX_PARAM;
			case 22: return PRESET_SAVE_PARAM;
			case 23: return OP1_WAVE_STORE_PARAM;
			case 24: return OP2_WAVE_STORE_PARAM;
			case 25: return OP3_WAVE_STORE_PARAM;
			case 26: return OP4_WAVE_STORE_PARAM;
			case 27: return OP1_FREQ_STORE_PARAM;
			case 28: return OP2_FREQ_STORE_PARAM;
			case 29: return OP3_FREQ_STORE_PARAM;
			case 30: return OP4_FREQ_STORE_PARAM;
			case 31: return OP1_LEVEL_STORE_PARAM;
			case 32: return OP2_LEVEL_STORE_PARAM;
			case 33: return OP3_LEVEL_STORE_PARAM;
			case 34: return OP4_LEVEL_STORE_PARAM;
			case 35: return FM_ENV_AMOUNT_PARAM;
			case 36: return WARMTH_PARAM;
			case 37: return SYNC_ENV_PARAM;
			case 38: return WARP_PARAM;
			case 39: return LFO_SYNC_DIV_PARAM;
			case 40: return LFO_GRAVITY_PARAM;
			case 41: return SYNC_DIV_MOD_TRIMPOT;
			case 42: return WT_FRAME_MOD_TRIMPOT;
			case 43: return VOWEL_MOD_TRIMPOT;
			case 44: return OP1_FB_MOD_TRIMPOT;
			case 45: return DECAY_MOD_TRIMPOT;
			case 46: return CUTOFF_MOD_TRIMPOT;
			case 47: return VOLUME_PARAM;
			case 48: return SUB_PARAM;
			case 49: return RANDOMIZE_BUTTON_PARAM;
			case 50: return RANDOMIZE_SEED_PARAM;
			case 51: return TEAR_PARAM;
			case 52: return TEAR_NOISE_PARAM;
			case 53: return LFO_ENABLE_PARAM;
			case 54: return COLOR_PARAM;
			case 55: return WT_SCROLL_MODE_PARAM;
			case 56: return WT_FORM_PARAM;
			default: return -1;
		}
	}

	void resetDspStateOnly() {
		for (auto& op : ops) {
			op.reset();
			op.unisonCount = 1;
			op.warpMode = 0;
			op.pdAmount = 0.f;
			op.tear = 0.f;
			op.wsMix = 0.f;
			op.wsDrive = 1.f;
		}
		ampEnv.reset();
		subPhase = 0.f;
		formant.reset();
		retroCrushL.reset();
		retroCrushR.reset();
		masterBusL.reset();
		masterBusR.reset();
		for (int i = 0; i < kTearDelaySize; ++i) {
			tearDelayL[i] = 0.f;
			tearDelayR[i] = 0.f;
		}
		tearDelayWrite = 0;
		tearNoiseState = 0x6D2B79F5u;
		tearWet = 0.f;
		clockSync.reset();
		internalLfo.reset();
		lastLfoValue = 0.f;
		filter.reset();
		fundamentalHz = 261.63f;

		carryL = carryR = 0.f;
		readIndex = 0;
		frac = 0.f;
		gateEdgeFromL = gateEdgeFromR = 0.f;
		lastPreVolL = lastPreVolR = 0.f;
		gateEdgeXfadeRemain = 0;
		gateEdgeXfadeTotal = 0;
		resetHeldPitchTracking();
		blockL[0] = carryL;
		blockR[0] = carryR;
		for (int i = 1; i <= kBlockSize; ++i) {
			blockL[i] = 0.f;
			blockR[i] = 0.f;
		}
		lastGate = false;
		presetStartupLockSamples = 0;
		pendingPresetSwitchMute = false;
		presetSwitchMuteSamples = 0;
	}

	void invalidateRenderedBlock() {
		carryL = 0.f;
		carryR = 0.f;
		blockL[0] = 0.f;
		blockR[0] = 0.f;
		for (int i = 1; i <= kBlockSize; ++i) {
			blockL[i] = 0.f;
			blockR[i] = 0.f;
		}
		readIndex = kBlockSize;
		frac = 0.f;
		gateEdgeFromL = 0.f;
		gateEdgeFromR = 0.f;
		lastPreVolL = 0.f;
		lastPreVolR = 0.f;
		gateEdgeXfadeRemain = 0;
		gateEdgeXfadeTotal = 0;
	}

	void bankCaptureCurrentToSlot(int idx) {
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;
		PresetSlot& s = bank[idx];
		captureCurrentToPresetSlot(s, s.name);
	}

	bool renameCurrentPreset(const std::string& newName) {
		bankEnsureLoaded();
		int idx = currentPreset;
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;
		PresetSlot& s = bank[idx];

		if (!s.used) {
			bankCaptureCurrentToSlot(idx);
		}

		std::string nm = newName;
		while (!nm.empty() && (nm.front() == ' ' || nm.front() == '\t' || nm.front() == '\n' || nm.front() == '\r'))
			nm.erase(nm.begin());
		while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t' || nm.back() == '\n' || nm.back() == '\r'))
			nm.pop_back();
		if (nm.empty()) {
			nm = "Saved";
		}
		if (nm.size() > 28) {
			nm.resize(28);
		}

		s.name = nm;
		syncPresetDisplayName(s.name);

		return bankSave();
	}

	void copyCurrentPresetToClipboard() {
		bankEnsureLoaded();
		int idx = currentPreset;
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;
		sPresetClipboard = {};
		sPresetClipboard.name = bank[idx].used ? bank[idx].name : defaultSlotName(idx);
		captureCurrentToPresetSlot(sPresetClipboard, sPresetClipboard.name);
		sPresetClipboardValid = true;
	}

	bool pasteClipboardToCurrentPreset() {
		if (!sPresetClipboardValid) {
			return false;
		}
		bankEnsureLoaded();
		int idx = currentPreset;
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;

		bank[idx] = sPresetClipboard;
		bank[idx].used = true;
		if (bank[idx].name.empty()) {
			char buf[32];
			snprintf(buf, sizeof(buf), "Preset %02d", idx + 1);
			bank[idx].name = buf;
		}

		bankApplySlot(idx);
		return bankSave();
	}

	bool revertCurrentPreset() {
		bankEnsureLoaded();
		bankApplySlot(currentPreset);
		return true;
	}

	bool initializeCurrentPresetSlot() {
		bankEnsureLoaded();
		int idx = currentPreset;
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;
		fillFactoryDefaultSlot(bank[idx], initSlotName(idx));
		bank[idx].params[PRESET_INDEX_PARAM] = (float)(idx + 1);
		bankApplySlot(idx);
		return bankSave();
	}

	void bankApplySlot(int idx) {
		if (idx < 0) idx = 0;
		if (idx >= kPresetSlots) idx = kPresetSlots - 1;
		PresetSlot& s = bank[idx];
		currentPreset = idx;
		if (!s.used) {
			PresetSlot base{};
			fillFactoryDefaultSlotParams(base);
			for (int p = 0; p < PARAMS_LEN; ++p) {
				float v = sanitizeParamValue(p, base.params[p]);
				params[p].setValue(v);
			}
			cachedEditOp = getEditOpIndex();
			syncEditKnobsFromStore(cachedEditOp);
			params[PRESET_SAVE_PARAM].setValue(0.f);
			params[RANDOMIZE_BUTTON_PARAM].setValue(0.f);
			params[PRESET_INDEX_PARAM].setValue((float)(idx + 1));
			resetDspStateOnly();
			warmPresetSwitchState();
			pendingPresetSwitchMute = true;
			char buf[32];
			snprintf(buf, sizeof(buf), "Empty %02d", idx + 1);
			syncPresetDisplayName(buf);
			return;
		}
		for (int p = 0; p < PARAMS_LEN; ++p) {
			float v = sanitizeParamValue(p, s.params[p]);
			params[p].setValue(v);
		}
		cachedEditOp = getEditOpIndex();
		syncEditKnobsFromStore(cachedEditOp);

#ifdef METAMODULE
		// Audibility guard: if imported bank data zeroes all operator levels,
		// recover to a known-audible state instead of producing only clicks.
		float opLevelSum =
			params[OP1_LEVEL_STORE_PARAM].getValue() +
			params[OP2_LEVEL_STORE_PARAM].getValue() +
			params[OP3_LEVEL_STORE_PARAM].getValue() +
			params[OP4_LEVEL_STORE_PARAM].getValue();
		if (opLevelSum < 0.01f) {
			params[OP1_LEVEL_STORE_PARAM].setValue(1.f);
			params[OP2_LEVEL_STORE_PARAM].setValue(1.f);
			params[OP3_LEVEL_STORE_PARAM].setValue(1.f);
			params[OP4_LEVEL_STORE_PARAM].setValue(1.f);
			params[OP_LEVEL_PARAM].setValue(1.f);
		}
#endif

		// Ensure SAVE button is not stuck on after applying, and sync index knob.
		params[PRESET_SAVE_PARAM].setValue(0.f);
		params[RANDOMIZE_BUTTON_PARAM].setValue(0.f);
		params[PRESET_INDEX_PARAM].setValue((float)(idx + 1));
		syncPresetDisplayName(s.name);

		// Prevent one unstable preset from latching DSP into silent/NaN state.
		resetDspStateOnly();
		warmPresetSwitchState();
		pendingPresetSwitchMute = true;
	}

	void bankInitDefault() {
		for (int i = 0; i < kPresetSlots; ++i) {
			bank[i].used = false;
			bank[i].name = defaultSlotName(i);
		}
		currentPreset = 0;
		syncPresetDisplayName("Init");
		fillFactoryDefaultSlot(bank[0], "Init");
		bank[0].params[PRESET_INDEX_PARAM] = 1.f;
		bankLoaded = true;
	}

	bool bankLoadFromPath(const std::string& path) {
		if (path.empty()) return false;

		json_error_t err;
		json_t* root = json_load_file(path.c_str(), 0, &err);
		if (!root) {
			return false;
		}
		json_t* fmtJ = json_object_get(root, "format");
		json_t* verJ = json_object_get(root, "version");
		json_t* slotsJ = json_object_get(root, "slots");
		if (!fmtJ || !json_is_string(fmtJ) || std::string(json_string_value(fmtJ)) != "MorphWorx.Phaseon1Bank" ||
			!verJ || !json_is_integer(verJ) ||
			!slotsJ || !json_is_array(slotsJ)) {
			json_decref(root);
			return false;
		}

		bankInitDefault();
		int n = (int)json_array_size(slotsJ);
		for (int i = 0; i < kPresetSlots && i < n; ++i) {
			json_t* slotJ = json_array_get(slotsJ, i);
			if (!slotJ || json_is_null(slotJ)) continue;
			PresetSlot& s = bank[i];
			fillFactoryDefaultSlot(s, defaultSlotName(i));
			json_t* nameJ = json_object_get(slotJ, "name");
			if (nameJ && json_is_string(nameJ)) {
				s.name = json_string_value(nameJ);
			}
			// v0.2 BREAKING CHANGE: ParamId enum was reordered in v0.2 to match the
			// MetaModule panel layout. Version 1 banks used the old enum order and
			// must be remapped into the current ParamId layout when loaded.
			const int bankVersion = json_is_integer(verJ) ? (int)json_integer_value(verJ) : 1;
			json_t* paramsJ = json_object_get(slotJ, "params");
			if (paramsJ && json_is_array(paramsJ)) {
				int pn = (int)json_array_size(paramsJ);
				for (int p = 0; p < pn; ++p) {
					json_t* vJ = json_array_get(paramsJ, p);
					if (!vJ || !json_is_number(vJ)) continue;
					float v = (float)json_number_value(vJ);
					int dstParam = p;
					if (bankVersion < 2) {
						dstParam = migrateVersion1ParamIndex(p);
					}
					if (dstParam < 0 || dstParam >= PARAMS_LEN) continue;
					s.params[dstParam] = sanitizeParamValue(dstParam, v);
				}
			}
		}
		json_decref(root);
		bankLoaded = true;
		return true;
	}

	bool bankLoad() {
		std::string primaryPath = bankPath();
	#ifdef METAMODULE
		if (metaModulePathExists(primaryPath) && bankLoadFromPath(primaryPath)) {
			return true;
		}
		{
			std::string bundledPath = bundledMetaModulePath("userwaveforms/Phbank.bnk");
			if (bundledPath != primaryPath && metaModulePathExists(bundledPath) && bankLoadFromPath(bundledPath)) {
				return true;
			}
		}

		std::vector<std::string> candidatePaths;
		std::vector<std::string> volumes = metaModuleVolumeSearchOrder();
		if (!volumes.empty()) {
			volumes.erase(volumes.begin());
		}
		for (const std::string& volume : volumes) {
			appendUniqueString(candidatePaths, volume + "phaseon1/Phbank.bnk");
			appendUniqueString(candidatePaths, volume + "phaseon1/phbank.bnk");
			appendUniqueString(candidatePaths, volume + "MorphWorx/phbank.bnk");
			appendUniqueString(candidatePaths, volume + "morphworx/phbank.bnk");
		}
		for (const std::string& candidate : candidatePaths) {
			if (candidate == primaryPath || !metaModulePathExists(candidate)) continue;
			if (bankLoadFromPath(candidate)) {
				return true;
			}
		}
	#else
		if (bankLoadFromPath(primaryPath)) {
			return true;
		}

		// Rack fallback: try the preset bank bundled inside the plugin package.
		// This auto-loads on first use without requiring the user to manually place the file.
		{
			std::string bundledPath = asset::plugin(pluginInstance, "userwaveforms/Phbank.bnk");
			if (bundledPath != primaryPath && bankLoadFromPath(bundledPath)) {
				return true;
			}
		}
	#endif
		return false;
	}

	bool bankLoadSelectedPath(const std::string& path) {
		std::string previous = bankFilePath;
		if (bankLoadFromPath(path)) {
			bankFilePath = path;
			currentPreset = 0;
			bankApplySlot(0);
			return true;
		}
		bankFilePath = previous;
		return false;
	}

	bool bankSaveToPath(const std::string& path) {
		if (path.empty()) return false;

		json_t* root = json_object();
		json_object_set_new(root, "format", json_string("MorphWorx.Phaseon1Bank"));
		// Version 2: param order matches the v0.2 panel layout (top-bottom, left-right).
		json_object_set_new(root, "version", json_integer(2));
		json_t* slotsJ = json_array();
		for (int i = 0; i < kPresetSlots; ++i) {
			PresetSlot& s = bank[i];
			if (!s.used) {
				json_array_append_new(slotsJ, json_null());
				continue;
			}
			json_t* slotJ = json_object();
			json_object_set_new(slotJ, "name", json_string(s.name.c_str()));
			json_t* paramsJ = json_array();
			for (int p = 0; p < PARAMS_LEN; ++p) {
				float v = s.params[p];
				if (p == PRESET_SAVE_PARAM || p == RANDOMIZE_BUTTON_PARAM) v = 0.f;
				json_array_append_new(paramsJ, json_real(v));
			}
			json_object_set_new(slotJ, "params", paramsJ);
			json_array_append_new(slotsJ, slotJ);
		}
		json_object_set_new(root, "slots", slotsJ);
		std::string dir = system::getDirectory(path);
		if (!dir.empty()) system::createDirectories(dir);
		int rc = json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
		return rc == 0;
	}

	bool bankSave() {
		return bankSaveToPath(bankPath());
	}

	void bankEnsureLoaded() {
		if (bankLoaded) return;
		if (!bankLoad()) {
			bankInitDefault();
	#ifdef METAMODULE
			// Do not auto-save a default bank on MetaModule if startup loading fails.
			// A transient mount/path issue would otherwise overwrite the user's real bank.
			return;
	#else
			bankSave();
	#endif
		}
	}

	Phaseon1() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.25f, "Density (FM index / drive amount)");
		configParam(TIMBRE_PARAM, 0.f, 1.f, 0.5f, "WT Frame");
		configParam(EDGE_PARAM, 0.f, 1.f, 0.25f, "Edge");
		configParam<Phaseon1AlgoParamQuantity>(ALGO_PARAM, 0.f, 13.f, 0.f, "Algorithm");
		paramQuantities[ALGO_PARAM]->snapEnabled = true;
		configParam(CHAR_PARAM, 0.f, 7.f, 0.f, "Character");
		paramQuantities[CHAR_PARAM]->snapEnabled = true;
		configParam<Phaseon1WaveParamQuantity>(WAVE_PARAM, 0.f, 7.f, 0.f, "Wave (Selected Op)");
		paramQuantities[WAVE_PARAM]->snapEnabled = true;
		configParam(FORMANT_PARAM, 0.f, 1.f, 0.f, "Formant");
		configParam(BITCRUSH_PARAM, 0.f, 1.f, 0.f, "Bitcrush");
		configParam(MOTION_PARAM, 0.f, 1.f, 0.26f, "Motion (stereo spread / movement)");
		configParam(MORPH_PARAM, 0.f, 1.f, 0.52f, "Morph (carrier/mod blend)");
		configParam(COMPLEX_PARAM, 0.f, 1.f, 0.0f, "Complex (warp + nonlinear detail)");

		configParam(EDIT_OP_PARAM, 1.f, 4.f, 1.f, "Edit Operator");
		paramQuantities[EDIT_OP_PARAM]->snapEnabled = true;
		configParam(OP_FREQ_PARAM, -24.f, 24.f, 0.f, "Operator Freq");
		paramQuantities[OP_FREQ_PARAM]->snapEnabled = true;
		configParam(OP_LEVEL_PARAM, 0.f, 1.f, 1.f, "Operator Level");
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.12f, "Attack");
		configParam(DECAY_PARAM, 0.f, 1.f, 0.35f, "Decay");
		configParam(FILTER_CUTOFF_PARAM, 0.f, 1.f, 0.6f, "Filter Cutoff");
		configParam(FILTER_RESONANCE_PARAM, 0.f, 1.f, 0.2f, "Filter Resonance");
		configParam(FILTER_DRIVE_PARAM, 0.f, 1.f, 0.f, "Filter Drive");
		configParam(FILTER_MORPH_PARAM, 0.f, 1.f, 0.f, "Filter Morph");
		configParam(FILTER_ENV_PARAM, 0.f, 1.f, 0.f, "Filter Env Link");
		configParam(WARP_PARAM, 0.f, 1.f, 0.f, "Warp Bend");
		configParam(FM_ENV_AMOUNT_PARAM, 0.f, 1.f, 0.65f, "ENV->FM (envelope drives FM index)");
		configParam(WARMTH_PARAM, 0.f, 1.f, 0.45f, "Warmth (feedback saturation tone)");
		configParam(SYNC_ENV_PARAM, 0.f, 1.f, 0.40f, "SYNC ENV.F (envelope to carrier sync)");
		configParam<Phaseon1SyncDivParamQuantity>(LFO_SYNC_DIV_PARAM, 0.f, 11.f, 4.f, "LFO Sync Div");
		paramQuantities[LFO_SYNC_DIV_PARAM]->snapEnabled = true;
		configParam(LFO_GRAVITY_PARAM, 0.f, 1.f, 0.4f, "LFO Grav (shape/weight of internal LFO)");
		configSwitch(LFO_ENABLE_PARAM, 0.f, 1.f, 1.f, "LFO Enable", {"Off", "On"});
		configParam(SYNC_DIV_MOD_TRIMPOT, -1.f, 1.f, 0.f, "Sync Div Mod Depth");
		configParam(WT_FRAME_MOD_TRIMPOT, -1.f, 1.f, 0.f, "WT Frame Mod Depth");
		configParam(VOWEL_MOD_TRIMPOT, -1.f, 1.f, 0.f, "Formant Mod Depth");
		configParam(OP1_FB_MOD_TRIMPOT, -1.f, 1.f, 0.f, "FB SRC (LFO to feedback source/depth)");
		configParam(DECAY_MOD_TRIMPOT, -1.f, 1.f, 0.f, "Decay Mod Depth");
		configParam(CUTOFF_MOD_TRIMPOT, -1.f, 1.f, 0.f, "Cutoff Mod Depth");
		configParam(PRESET_INDEX_PARAM, 1.f, (float)kPresetSlots, 1.f, "Preset Index");
		paramQuantities[PRESET_INDEX_PARAM]->snapEnabled = true;
		configButton(PRESET_SAVE_PARAM, "Preset Save");

		// Hidden per-op storage. Defaults chosen so the module sounds like the current Phaseon1.
		configParam(OP1_WAVE_STORE_PARAM, 0.f, 7.f, 0.f, "OP1 Wave");
		configParam(OP2_WAVE_STORE_PARAM, 0.f, 7.f, 0.f, "OP2 Wave");
		configParam(OP3_WAVE_STORE_PARAM, 0.f, 7.f, 0.f, "OP3 Wave");
		configParam(OP4_WAVE_STORE_PARAM, 0.f, 7.f, 0.f, "OP4 Wave");
		paramQuantities[OP1_WAVE_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP2_WAVE_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP3_WAVE_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP4_WAVE_STORE_PARAM]->snapEnabled = true;

		configParam(OP1_FREQ_STORE_PARAM, -24.f, 24.f, 0.f, "OP1 Freq");
		configParam(OP2_FREQ_STORE_PARAM, -24.f, 24.f, 0.f, "OP2 Freq");
		configParam(OP3_FREQ_STORE_PARAM, -24.f, 24.f, 0.f, "OP3 Freq");
		configParam(OP4_FREQ_STORE_PARAM, -24.f, 24.f, 0.f, "OP4 Freq");
		paramQuantities[OP1_FREQ_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP2_FREQ_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP3_FREQ_STORE_PARAM]->snapEnabled = true;
		paramQuantities[OP4_FREQ_STORE_PARAM]->snapEnabled = true;

		configParam(OP1_LEVEL_STORE_PARAM, 0.f, 1.f, 1.f, "OP1 Level");
		configParam(OP2_LEVEL_STORE_PARAM, 0.f, 1.f, 1.f, "OP2 Level");
		configParam(OP3_LEVEL_STORE_PARAM, 0.f, 1.f, 1.f, "OP3 Level");
		configParam(OP4_LEVEL_STORE_PARAM, 0.f, 1.f, 1.f, "OP4 Level");

		configParam(VOLUME_PARAM, 0.f, 1.f, 0.5f, "Volume");
		configParam(SUB_PARAM, 0.f, 1.f, 0.f, "Sub");
		configButton(RANDOMIZE_BUTTON_PARAM, "Randomize");
		configParam(RANDOMIZE_SEED_PARAM, 0.f, 1.f, 0.f, "Randomize Seed");
		configParam(TEAR_PARAM, 0.f, 1.f, 0.f, "Tear");
		configSwitch(TEAR_NOISE_PARAM, 0.f, 1.f, 1.f, "Tear Fold", {"Off", "On"});
		configParam(COLOR_PARAM, 0.f, 1.f, 0.f, "Color (spectral emphasis / brightness)");
		configSwitch(WT_SCROLL_MODE_PARAM, 0.f, 1.f, 0.f, "WT Scroll", {"Smooth", "Stepped"});
		configParam<Phaseon1WtFormParamQuantity>(WT_FORM_PARAM, 0.f, 3.f, 0.f, "WT Form");
		paramQuantities[WT_FORM_PARAM]->snapEnabled = true;

		configInput(GATE_INPUT, "Gate");
		configInput(VOCT_INPUT, "V/Oct");
		configInput(FORMANT_CV_INPUT, "Formant CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(WARP_CV_INPUT,      "Warp CV");
		configInput(MORPH_CV_INPUT,     "Morph CV");
		configInput(COMPLEX_CV_INPUT,   "Complex CV");
		configInput(MOTION_CV_INPUT,    "Motion CV");
		configInput(ATTACK_CV_INPUT,    "Attack CV");
		configInput(DECAY_CV_INPUT,     "Decay CV");
		configInput(BITCRUSH_CV_INPUT,  "Bitcrush CV");
		configInput(WTFRAME_CV_INPUT,   "WT Frame CV");
		configInput(CUTOFF_CV_INPUT,    "Cutoff CV");
		configInput(RESONANCE_CV_INPUT, "Resonance CV");
		configInput(DRIVE_CV_INPUT,     "Drive CV");
		configInput(LFO_GRAVITY_CV_INPUT, "LFO Gravity CV");
		configInput(ALL_FREQ_CV_INPUT,    "All Freq CV");
		configInput(OP1_LEVEL_CV_INPUT,   "OP1 Level CV");
		configInput(CHARACTER_CV_INPUT,   "Character CV");
		configInput(PRESET_CV_INPUT,      "Preset Select CV");
		configInput(COLOR_CV_INPUT,       "Color CV");
		configInput(EDGE_CV_INPUT,        "Edge CV");
		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");
		configOutput(LFO_OUTPUT, "Internal LFO");
		configOutput(ENV_OUTPUT, "Envelope Follower");

		filter.setSampleRate(kInternalRate);
		masterBusL.setSampleRate(kInternalRate);
		masterBusR.setSampleRate(kInternalRate);
		retroCrushL.setSampleRate(kInternalRate);
		retroCrushR.setSampleRate(kInternalRate);

		onReset();
		bankLoaded = false;
	}

	void onAdd() override {
		Module::onAdd();

#ifdef METAMODULE
		// MetaModule: aggressively force a known-good default patch on add.
		// We ignore any imported parameter state so Phaseon1 always starts
		// from a sound-producing configuration on MetaModule.
		params[DENSITY_PARAM].setValue(0.25f);
		params[TIMBRE_PARAM].setValue(0.5f);
		params[EDGE_PARAM].setValue(0.25f);
		params[ALGO_PARAM].setValue(0.f);
		params[CHAR_PARAM].setValue(0.f);
		params[WAVE_PARAM].setValue(0.f);
		params[FORMANT_PARAM].setValue(0.f);
		params[BITCRUSH_PARAM].setValue(0.f);
		params[MOTION_PARAM].setValue(0.26f);
		params[MORPH_PARAM].setValue(0.52f);
		params[COMPLEX_PARAM].setValue(0.f);

		params[EDIT_OP_PARAM].setValue(1.f);
		params[OP_FREQ_PARAM].setValue(0.f);
		params[OP_LEVEL_PARAM].setValue(1.f);
		params[ATTACK_PARAM].setValue(0.12f);
		params[DECAY_PARAM].setValue(0.35f);
		params[WARP_PARAM].setValue(0.0f);
		params[FM_ENV_AMOUNT_PARAM].setValue(0.65f);
		params[WARMTH_PARAM].setValue(0.45f);
		params[SYNC_ENV_PARAM].setValue(0.40f);
		params[LFO_SYNC_DIV_PARAM].setValue(4.f);
		params[LFO_GRAVITY_PARAM].setValue(0.4f);
		params[SYNC_DIV_MOD_TRIMPOT].setValue(0.f);
		params[WT_FRAME_MOD_TRIMPOT].setValue(0.f);
		params[VOWEL_MOD_TRIMPOT].setValue(0.f);
		params[OP1_FB_MOD_TRIMPOT].setValue(0.f);
		params[DECAY_MOD_TRIMPOT].setValue(0.f);
		params[CUTOFF_MOD_TRIMPOT].setValue(0.f);
		params[LFO_SYNC_DIV_PARAM].setValue(4.f);
		params[LFO_GRAVITY_PARAM].setValue(0.4f);
		params[SYNC_DIV_MOD_TRIMPOT].setValue(0.f);
		params[WT_FRAME_MOD_TRIMPOT].setValue(0.f);
		params[VOWEL_MOD_TRIMPOT].setValue(0.f);
		params[OP1_FB_MOD_TRIMPOT].setValue(0.f);
		params[DECAY_MOD_TRIMPOT].setValue(0.f);
		params[CUTOFF_MOD_TRIMPOT].setValue(0.f);
		params[VOLUME_PARAM].setValue(0.5f);
		params[WT_SCROLL_MODE_PARAM].setValue(0.f);
		params[WT_FORM_PARAM].setValue(0.f);

		// Hidden per-operator storage defaults (match constructor).
		params[OP1_WAVE_STORE_PARAM].setValue(0.f);
		params[OP2_WAVE_STORE_PARAM].setValue(0.f);
		params[OP3_WAVE_STORE_PARAM].setValue(0.f);
		params[OP4_WAVE_STORE_PARAM].setValue(0.f);
		params[OP1_FREQ_STORE_PARAM].setValue(0.f);
		params[OP2_FREQ_STORE_PARAM].setValue(0.f);
		params[OP3_FREQ_STORE_PARAM].setValue(0.f);
		params[OP4_FREQ_STORE_PARAM].setValue(0.f);
		params[OP1_LEVEL_STORE_PARAM].setValue(1.f);
		params[OP2_LEVEL_STORE_PARAM].setValue(1.f);
		params[OP3_LEVEL_STORE_PARAM].setValue(1.f);
		params[OP4_LEVEL_STORE_PARAM].setValue(1.f);
#endif

		cachedEditOp = getEditOpIndex();
		syncEditKnobsFromStore(cachedEditOp);
		onReset();
		bankEnsureLoaded();
#ifdef METAMODULE
		// Deterministic startup on MetaModule: always begin browsing at slot 1.
		currentPreset = 0;
		params[PRESET_INDEX_PARAM].setValue(1.f);
		bankApplySlot(currentPreset);
		presetStartupLockSamples = 36000;
#else
		// In VCV Rack, respect restored preset selection from the host/patch.
		int idxFromKnob = (int)std::round(params[PRESET_INDEX_PARAM].getValue()) - 1;
		if (idxFromKnob < 0) idxFromKnob = 0;
		if (idxFromKnob >= kPresetSlots) idxFromKnob = kPresetSlots - 1;
		currentPreset = idxFromKnob;
		bankApplySlot(currentPreset);
#endif
		presetInitDone = true;
	}

	void onReset() override {
		resetDspStateOnly();
		hasWt.store(false, std::memory_order_release);
		cachedEditOp = getEditOpIndex();
		syncEditKnobsFromStore(cachedEditOp);
		filter.reset();

		// Try to autoload the default Phaseon1 wavetable if none is loaded yet.
		if (!hasWt.load(std::memory_order_acquire)) {
			std::string err; 
#ifdef METAMODULE
			// Prefer a same-volume override, then the bundled factory wavetable,
			// then any other local volumes as a legacy fallback.
			std::vector<std::string> candidateWtPaths;
			appendUniqueString(candidateWtPaths, defaultWtPath());
			appendUniqueString(candidateWtPaths, bundledMetaModulePath("userwaveforms/phaseon1.wav"));
			std::vector<std::string> volumes = metaModuleVolumeSearchOrder();
			if (!volumes.empty()) {
				volumes.erase(volumes.begin());
			}
			for (const std::string& volume : volumes) {
				appendUniqueString(candidateWtPaths, volume + "phaseon1/phaseon1.wav");
			}
			for (const std::string& candidate : candidateWtPaths) {
				if (!metaModulePathExists(candidate)) continue;
				if (loadWavetableFile(candidate, &err, false)) {
					break;
				}
			}
#else
			// On Rack, load from the plugin-bundled userwaveforms folder so it ships inside the .vcvplugin.
			std::string wtPath = asset::plugin(pluginInstance, "userwaveforms/phaseon1.wav");
			loadWavetableFile(wtPath, &err, false);
#endif
		}
	}

	void renderInternalBlock() {
		// Randomize button: generate a new deterministic seed and perturb operator params.
		{
			bool rnd = params[RANDOMIZE_BUTTON_PARAM].getValue() > 0.5f;
			if (rnd && !lastRandomize) {
				uint32_t seed = random::u32();
				float seed01 = (float)(seed & 0x00FFFFFFu) * (1.0f / 16777215.0f);
				params[RANDOMIZE_SEED_PARAM].setValue(seed01);

				auto chooseMusicalSemi = [](int opIndex) -> int {
					static const int kRootPool[] = {-24, -12, 0, 12};
					static const int kSupportPool[] = {-24, -12, -7, 0, 7, 12};
					static const int kColorPool[] = {-24, -19, -12, -7, -5, 0, 7, 12};
					const int* pool = kSupportPool;
					int poolSize = (int)(sizeof(kSupportPool) / sizeof(kSupportPool[0]));
					if (opIndex == 0) {
						pool = kRootPool;
						poolSize = (int)(sizeof(kRootPool) / sizeof(kRootPool[0]));
					}
					else if (opIndex >= 2) {
						pool = kColorPool;
						poolSize = (int)(sizeof(kColorPool) / sizeof(kColorPool[0]));
					}
					return pool[random::u32() % (uint32_t)poolSize];
				};
				auto nudgeAroundCurrent01 = [](float current, float span) -> float {
					float centered = u01_from_u32(random::u32()) * 2.0f - 1.0f;
					return clamp01(current + centered * span);
				};

				// Randomize a few global macros for fast preset exploration.
				params[ALGO_PARAM].setValue((float)(random::u32() % 14u));
				params[CHAR_PARAM].setValue((float)(random::u32() % 8u));
				params[FILTER_ENV_PARAM].setValue(u01_from_u32(random::u32()));
				params[TIMBRE_PARAM].setValue(u01_from_u32(random::u32()));
				params[DECAY_PARAM].setValue(nudgeAroundCurrent01(params[DECAY_PARAM].getValue(), 0.25f));
				params[FILTER_CUTOFF_PARAM].setValue(nudgeAroundCurrent01(params[FILTER_CUTOFF_PARAM].getValue(), 0.10f));
				params[FILTER_RESONANCE_PARAM].setValue(nudgeAroundCurrent01(params[FILTER_RESONANCE_PARAM].getValue(), 0.10f));
				params[WT_FORM_PARAM].setValue((float)(random::u32() % 4u));
				params[FM_ENV_AMOUNT_PARAM].setValue(u01_from_u32(random::u32()));
				params[COLOR_PARAM].setValue(u01_from_u32(random::u32()));
				params[LFO_GRAVITY_PARAM].setValue(u01_from_u32(random::u32()));
				params[SYNC_ENV_PARAM].setValue(u01_from_u32(random::u32()));
				params[EDGE_PARAM].setValue(u01_from_u32(random::u32()));
				params[FORMANT_PARAM].setValue(u01_from_u32(random::u32()));
				params[WARMTH_PARAM].setValue(u01_from_u32(random::u32()));

				// Randomize per-operator core settings (stored params) so presets can be saved.
				for (int oi = 0; oi < 4; ++oi) {
					int wave = (int)(random::u32() % 8u);
					// Keep randomized operator tuning in consonant semitone sets so patches stay musical.
					int semi = chooseMusicalSemi(oi);
					float lvl = 0.25f + u01_from_u32(random::u32()) * 0.75f;
					params[OP1_WAVE_STORE_PARAM + oi].setValue((float)wave);
					params[OP1_FREQ_STORE_PARAM + oi].setValue((float)semi);
					params[OP1_LEVEL_STORE_PARAM + oi].setValue(lvl);
				}

				// Keep edit knob/cache consistent with the new stored values.
				cachedEditOp = getEditOpIndex();
				syncEditKnobsFromStore(cachedEditOp);

#ifdef METAMODULE
				MetaModule::Patch::mark_patch_modified();
#endif
			}
			lastRandomize = rnd;
		}

		updateEditState();

		// Copy wavetable pointer once per block (no locking inside per-sample loop).
		const phaseon::Wavetable* tablePtr = nullptr;
		if (hasWt.load(std::memory_order_acquire)) {
			int active = wtActive.load(std::memory_order_acquire);
			if (active < 0) active = 0;
			if (active > 1) active = 1;
			const phaseon::Wavetable& w = wtBuf[active];
			if (w.frameSize > 0 && w.frameCount > 0 && !w.data.empty()) {
				tablePtr = &w;
			}
		}

		// Cache parameters at control/block rate.
		float density = clamp01(params[DENSITY_PARAM].getValue());
		float timbre = clamp01(params[TIMBRE_PARAM].getValue());
		float edge = clamp01(params[EDGE_PARAM].getValue());
		int algo = (int)std::round(params[ALGO_PARAM].getValue());
		if (algo < 0) algo = 0;
		if (algo > 13) algo = 13;
		float characterPos = clamp(params[CHAR_PARAM].getValue() + inputs[CHARACTER_CV_INPUT].getVoltage() * 1.4f, 0.f, 7.f);
		int characterLowIndex = (int)std::floor(characterPos);
		if (characterLowIndex < 0) characterLowIndex = 0;
		if (characterLowIndex > 7) characterLowIndex = 7;
		int characterHighIndex = characterLowIndex + 1;
		if (characterHighIndex > 7) characterHighIndex = 7;
		float characterBlend = characterPos - (float)characterLowIndex;
		if (characterBlend < 0.f) characterBlend = 0.f;
		if (characterBlend > 1.f) characterBlend = 1.f;
		// Preserve legacy FM tension behavior for old presets by saturating beyond the original 4 modes.
		float character = std::min(characterPos, 3.f) * (1.0f / 3.0f);
		// CV summing helper: 5V = full sweep, ±5V = bipolar. Always clamped to [0,1].
		// Evaluated at block-rate for CPU efficiency on Cortex-A7.
		auto cvSum = [&](float knob, InputId cvIn) -> float {
			return clamp01(knob + inputs[cvIn].getVoltage() * 0.2f);
		};

		edge = cvSum(clamp01(params[EDGE_PARAM].getValue()), EDGE_CV_INPUT);
		float motion     = cvSum(clamp01(params[MOTION_PARAM].getValue()),   MOTION_CV_INPUT);
		float algoMorph  = cvSum(clamp01(params[MORPH_PARAM].getValue()),    MORPH_CV_INPUT);
		float complexAmt = cvSum(clamp01(params[COMPLEX_PARAM].getValue()),  COMPLEX_CV_INPUT);

		// Global Attack/Decay (CPU-friendly)
		float attackBase = cvSum(clamp01(params[ATTACK_PARAM].getValue()),   ATTACK_CV_INPUT);
		float decayBase  = cvSum(clamp01(params[DECAY_PARAM].getValue()),    DECAY_CV_INPUT);
		// atk is block-constant (attackBase not LFO-modulated); hoisted from inner loop.
		const float atk = 0.0010f + (attackBase * attackBase) * 0.2490f;

		float formAmt = cvSum(clamp01(params[FORMANT_PARAM].getValue()),     FORMANT_CV_INPUT);

		float fmEnvAmount = clamp01(params[FM_ENV_AMOUNT_PARAM].getValue());
		float warmthCtl = clamp01(params[WARMTH_PARAM].getValue());
		float syncEnvAmount = clamp01(params[SYNC_ENV_PARAM].getValue());
		float warpBase = cvSum(clamp01(params[WARP_PARAM].getValue()), WARP_CV_INPUT);
		float subLevel = clamp01(params[SUB_PARAM].getValue());
		float tearTarget = clamp01(params[TEAR_PARAM].getValue());
		bool tearFoldEnabled = params[TEAR_NOISE_PARAM].getValue() > 0.5f;
		bool wtScrollStepped = params[WT_SCROLL_MODE_PARAM].getValue() > 0.5f;
		int wtFormMode = (int)std::round(params[WT_FORM_PARAM].getValue());
		if (wtFormMode < 0) wtFormMode = 0;
		if (wtFormMode > 3) wtFormMode = 3;
		const float wtFormBoost = 1.3f;
		const float growlBoost = 2.0f;
		const float yoiBoost = 1.45f;
		const float tearBoost = 1.5f;
		float randSeed01 = clamp01(params[RANDOMIZE_SEED_PARAM].getValue());
		// Click-free TEAR engagement: block-rate smoothing + per-sample ramp.
		float tearWet0 = tearWet;
		float tearWet1 = tearWet0 + (tearTarget - tearWet0) * 0.18f;
		tearWet = tearWet1;
		float tearWetStep = (tearWet1 - tearWet0) * (1.0f / (float)kBlockSize);
		// Map [0,1] → [0,2]: knob@0.5 ≈ former maximum (~exp2f(4)), knob@1.0 = extreme (~exp2f(8)).
		float warpMapped = warpBase * 2.0f;
		// LFO Sync Division: CV overrides knob when connected; otherwise read the knob.
		float lfoSyncDiv;
		if (inputs[SYNC_DIV_CV_INPUT].isConnected()) {
			float cv = clamp(inputs[SYNC_DIV_CV_INPUT].getVoltage(), 0.f, 10.f);
			int idx = (int)std::round(cv * (11.f / 10.f));
			if (idx < 0) idx = 0;
			if (idx > 11) idx = 11;
			lfoSyncDiv = idx * (1.f / 11.f);
		} else {
			// No sync-div cable: force 1 Bar for predictable standalone behavior.
			constexpr int kOneBarIdx = 2;
			lfoSyncDiv = kOneBarIdx * (1.f / 11.f);
		}
		float lfoGravity = cvSum(clamp01(params[LFO_GRAVITY_PARAM].getValue()), LFO_GRAVITY_CV_INPUT);
		float allFreqSemiOffset = 0.f;
		if (inputs[ALL_FREQ_CV_INPUT].isConnected()) {
			// Bipolar ±5V -> ±24 semitones across all operators.
			allFreqSemiOffset = clamp(inputs[ALL_FREQ_CV_INPUT].getVoltage(), -5.f, 5.f) * (24.f / 5.f);
		}
		float op1LevelCvNorm = 0.f;
		if (inputs[OP1_LEVEL_CV_INPUT].isConnected()) {
			// Bipolar ±5V -> ±1.0 normalized level offset (clamped below).
			op1LevelCvNorm = clamp(inputs[OP1_LEVEL_CV_INPUT].getVoltage(), -5.f, 5.f) * 0.2f;
		}
		constexpr float syncDivModTrim = 0.f;  // trimpot removed; mod depth always 0
		float wtFrameModTrim = params[WT_FRAME_MOD_TRIMPOT].getValue();
		float vowelModTrim = params[VOWEL_MOD_TRIMPOT].getValue();
		float op1FbModTrim = params[OP1_FB_MOD_TRIMPOT].getValue();
		float fbBypassAmt = std::fabs(op1FbModTrim);
		if (fbBypassAmt > 1.0f) fbBypassAmt = 1.0f;
		float decayModTrim = params[DECAY_MOD_TRIMPOT].getValue();
		float cutoffModTrim = params[CUTOFF_MOD_TRIMPOT].getValue();

		// Dubstep macro envelope for WT/formant movement.
		// Keep this independent from FILTER_ENV_PARAM so the filter env link knob
		// cannot indirectly overdrive timbre/formant modulation on MetaModule.
		float envDepth = clamp01(params[FILTER_ENV_PARAM].getValue());
		float macroDepth = 0.15f + 0.35f * fmEnvAmount;
		float macroEnv = clamp01(filter.getEnvFollower() * macroDepth);

		const float wtEnvAmount = 0.35f;
		const float formEnvAmount = 0.30f;
		float timbreBase = cvSum(clamp01(timbre + macroEnv * wtEnvAmount), WTFRAME_CV_INPUT);
		float formAmtBase = clamp01(formAmt + macroEnv * formEnvAmount);

		// Ratio families (4-op), driven by 8-position Character morph.
		// The first four sets are preserved exactly for preset compatibility.
		static const float kCharacterRatios[8][4] = {
			{1.f, 2.f, 3.f, 4.f},
			{1.f, 1.5f, 2.f, 3.f},
			{1.f, 1.3333333f, 2.f, 4.f},
			{0.5f, 1.f, 2.f, 5.f},
			{1.f, 0.5f, 1.5f, 2.f},
			{1.f, 1.25f, 1.5f, 2.f},
			{1.f, 1.1f, 1.2f, 1.3f},
			{0.5f, 1.3333333f, 2.6666667f, 5.f},
		};
		float ratios[4];
		for (int i = 0; i < 4; ++i) {
			ratios[i] = lerp(kCharacterRatios[characterLowIndex][i], kCharacterRatios[characterHighIndex][i], characterBlend);
		}

		// FM Character tension curve (non-linear): makes upper half of the knob
		// much more explosive while keeping low values stable.
		float tension = character * std::sqrt(character);
		if (tension < 0.0f) tension = 0.0f;
		if (tension > 1.0f) tension = 1.0f;
		float warmTension = clamp01(0.49f * warmthCtl + 0.65f * tension);  // +40% warmth weight

		// Density drives FM depth; Edge adds aggressive feedback; Morph/Complex season it.
		float fmBase = density * density * 6.0f;
		float fm = fmBase * (0.55f + 0.90f * algoMorph);
		// Make EDGE more obvious: map 0..1 → 0..~1.6 feedback, with a bit of extra push from Complex.
		float edgeBase = edge;
		float fbBase = edgeBase * edgeBase * 1.6f;
		float fb = fbBase * (1.0f + 0.60f * complexAmt);
		// Stability guard (especially important on MetaModule): keep operator
		// self-feedback in a musical/stable range.
		if (fb < 0.0f) fb = 0.0f;
		if (fb > 0.98f) fb = 0.98f;

		float rawLevels[4] = {1.f, 1.f, 1.f, 1.f};
		float rndFrameOff[4] = {0.f, 0.f, 0.f, 0.f};
		float rndFmMul[4] = {1.f, 1.f, 1.f, 1.f};
		float rndLevelMul[4] = {1.f, 1.f, 1.f, 1.f};
		float rndWarpMul[4] = {1.f, 1.f, 1.f, 1.f};
		float rndWarmMul[4] = {1.f, 1.f, 1.f, 1.f};
		float rndFormantMul = 1.f;
		if (randSeed01 > 0.0001f) {
			uint32_t st = (uint32_t)(randSeed01 * 4294967295.0f) ^ 0xC001D00Du;
			for (int oi = 0; oi < 4; ++oi) {
				float r0 = u01_from_u32(xorshift32(st)) * 2.f - 1.f;
				float r1 = u01_from_u32(xorshift32(st)) * 2.f - 1.f;
				float r2 = u01_from_u32(xorshift32(st)) * 2.f - 1.f;
				float r3 = u01_from_u32(xorshift32(st)) * 2.f - 1.f;
				rndFrameOff[oi] = r0 * 0.12f;
				rndFmMul[oi] = 1.0f + r1 * 0.22f;
				rndLevelMul[oi] = 1.0f + r2 * 0.18f;
				rndWarpMul[oi] = 1.0f + r3 * 0.35f;
				rndWarmMul[oi] = 1.0f + (u01_from_u32(xorshift32(st)) * 2.f - 1.f) * 0.12f;
			}
			rndFormantMul = 1.0f + (u01_from_u32(xorshift32(st)) * 2.f - 1.f) * 0.20f;
		}

		// ── Color: Braids-style spectral character macro ─────────────────────────
		float colorAmt = clamp01(params[COLOR_PARAM].getValue() + inputs[COLOR_CV_INPUT].getVoltage() * 0.1f);
		// Pre-pass: detect if any operator is using a loaded wavetable.
		// Governs whether Timbre/Color act on FM parameters or wavetable morphing.
		bool anyWT = false;
		for (int i = 0; i < 4; ++i) {
			int wm = (int)std::round(params[OP1_WAVE_STORE_PARAM + i].getValue());
			if (wm < 0) wm = 0;
			if (wm > 7) wm = 7;
			anyWT |= (wm == 0 && tablePtr != nullptr);
		}
		if (wtFormMode == 1) {
			timbreBase = clamp01(0.08f * wtFormBoost * growlBoost + timbreBase * (1.0f - 0.28f * wtFormBoost * growlBoost));
			formAmtBase = clamp01(formAmtBase + 0.05f * wtFormBoost * growlBoost + syncEnvAmount * 0.05f * growlBoost);
		}
		else if (wtFormMode == 2) {
			timbreBase = clamp01(0.14f * wtFormBoost * yoiBoost + timbreBase * (1.0f - 0.16f * wtFormBoost * yoiBoost) + syncEnvAmount * 0.12f * wtFormBoost * yoiBoost);
			formAmtBase = clamp01(formAmtBase + 0.18f * wtFormBoost * yoiBoost + syncEnvAmount * 0.08f * yoiBoost);
		}
		else if (wtFormMode == 3) {
			wtScrollStepped = true;
			timbreBase = clamp01(0.05f * wtFormBoost * tearBoost + timbreBase * (1.0f - 0.22f * wtFormBoost * tearBoost));
			formAmtBase = clamp01(formAmtBase + 0.02f * wtFormBoost * tearBoost);
		}
		// TIMBRE → FM complexity in non-WT mode.
		// Additive offset centered on 0.5: center = 0 (backward compat), sweep always audible
		// even when DENSITY = 0.  Range: timbreBase 0→1 gives -1.2..+1.2 additive FM index.
		const float timbreAddFm = anyWT ? 0.f : ((timbreBase - 0.5f) * 2.0f * 1.2f);

		for (int i = 0; i < 4; ++i) {
			int waveMode = (int)std::round(params[OP1_WAVE_STORE_PARAM + i].getValue());
			if (waveMode < 0) waveMode = 0;
			if (waveMode > 7) waveMode = 7;
			// 0 WT, 1 Sine, 2 Triangle, 3 Saw, 4 SkewedSine, 5 WarpSine, 6 Rectified, 7 Sync Saw
			int tableIndex = 0;
			switch (waveMode) {
			default:
			case 0: tableIndex = 0; break;
			case 1: tableIndex = -1; break;
			case 2: tableIndex = -2; break;
			case 3: tableIndex = -3; break;
			case 4: tableIndex = -5; break;
			case 5: tableIndex = -7; break;
			case 6: tableIndex = -8; break;
			case 7: tableIndex = -9; break;
			}
			if (!tablePtr && tableIndex == 0) {
				// No wavetable loaded: WT mode falls back to sine.
				tableIndex = -1;
			}

			float semi = params[OP1_FREQ_STORE_PARAM + i].getValue() + allFreqSemiOffset;
			if (semi < -24.f) semi = -24.f;
			if (semi > 24.f) semi = 24.f;
			float ratioMul = fastExp2Approx(semi * (1.0f / 12.0f));
			rawLevels[i] = clamp01(params[OP1_LEVEL_STORE_PARAM + i].getValue());
			if (i == 0 && op1LevelCvNorm != 0.f) {
				rawLevels[i] = clamp01(rawLevels[i] + op1LevelCvNorm);
			}

			ops[i].ratio = ratios[i] * ratioMul;
			float fpInit = clamp01(timbreBase + rndFrameOff[i]);
			if (wtScrollStepped && tableIndex == 0) {
				int steps = 16;
				if (tablePtr && tablePtr->frameCount > 1) steps = tablePtr->frameCount;
				if (steps < 2) steps = 2;
				float s = (float)(steps - 1);
				fpInit = std::round(fpInit * s) / s;
			}
			ops[i].framePos = fpInit;
			ops[i].feedback = fb;
			ops[i].feedbackWarmth = (0.12f + 1.232f * warmTension) * rndWarmMul[i];  // ceiling raised +40% for more aggressive saturation
			ops[i].tableIndex = tableIndex;
			ops[i].phaseWarp = warpMapped;
			ops[i].tear = tearWet1;

			// Complexity: per-operator warp and gentle waveshaping.
			float idxNorm = (float)i / 3.0f;
			float warp = complexAmt * (0.35f + 0.45f * idxNorm);
			warp *= rndWarpMul[i];
			if (warp > 0.001f) {
				ops[i].domain = phaseon::OpDomain::WT_PD;
				ops[i].warpMode = 6; // Classic PD hybrid
				ops[i].pdAmount = warp;
				ops[i].wsMix = 0.30f * complexAmt;
				ops[i].wsDrive = 1.0f + 2.5f * complexAmt;
			}
			else {
				ops[i].domain = phaseon::OpDomain::WT_PM;
				ops[i].warpMode = 0;
				ops[i].pdAmount = 0.f;
				ops[i].wsMix = 0.f;
				ops[i].wsDrive = 1.f;
			}

			switch (wtFormMode) {
			case 1:
				if (tableIndex == 0) {
					ops[i].warpMode = 6;
					ops[i].pdAmount = clamp01(ops[i].pdAmount + wtFormBoost * growlBoost * (complexAmt * (0.18f + 0.18f * idxNorm) + syncEnvAmount * 0.04f));
					ops[i].wsMix = clamp01(std::max(ops[i].wsMix, wtFormBoost * growlBoost * (0.10f * complexAmt)));
					ops[i].wsDrive = std::max(ops[i].wsDrive, 1.0f + wtFormBoost * growlBoost * (0.80f * complexAmt));
				}
				ops[i].feedbackWarmth = clamp01(ops[i].feedbackWarmth + wtFormBoost * growlBoost * (0.10f + 0.18f * complexAmt));
				break;
			case 2:
				if (tableIndex == 0) {
					ops[i].warpMode = 6;
					ops[i].pdAmount = clamp01(ops[i].pdAmount + wtFormBoost * yoiBoost * (0.18f + 0.28f * complexAmt + 0.16f * syncEnvAmount));
					ops[i].wsMix = clamp01(std::max(ops[i].wsMix, wtFormBoost * yoiBoost * (0.10f * complexAmt + 0.12f * syncEnvAmount)));
					ops[i].wsDrive = std::max(ops[i].wsDrive, 1.0f + wtFormBoost * (0.42f * complexAmt + 0.38f * syncEnvAmount));
				}
				ops[i].feedbackWarmth = clamp01(ops[i].feedbackWarmth + wtFormBoost * (0.03f + 0.05f * complexAmt));
				break;
			case 3:
				if (tableIndex == 0) {
					ops[i].warpMode = (i & 1) ? 5 : 4;
					ops[i].pdAmount = clamp01(ops[i].pdAmount + wtFormBoost * tearBoost * (0.20f + 0.30f * complexAmt));
					ops[i].wsMix = clamp01(std::max(ops[i].wsMix, wtFormBoost * tearBoost * (0.12f * complexAmt)));
					ops[i].wsDrive = std::max(ops[i].wsDrive, 1.0f + wtFormBoost * tearBoost * (1.10f * complexAmt));
				}
				ops[i].tear = clamp01(std::max(ops[i].tear, wtFormBoost * tearBoost * (0.22f + 0.34f * complexAmt)));
				ops[i].feedbackWarmth = clamp01(ops[i].feedbackWarmth + wtFormBoost * tearBoost * (0.04f + 0.10f * complexAmt));
				break;
			default:
				break;
			}

			// Motion: stereo width / unison.
			float width = 0.f;
#ifndef METAMODULE
			if (motion > 0.001f) {
				width = motion * (0.06f + 0.04f * idxNorm);
				ops[i].unisonCount = (motion > 0.08f) ? 2 : 1;
			}
			else {
				ops[i].unisonCount = 1;
			}
#else
			// MetaModule: Braids-style inline two-phase synthesis.
			// Keep unisonCount=1 to skip the full voice loop;
			// the MM operator tick handles two phases directly.
			ops[i].unisonCount = 1;
			if (motion > 0.001f) {
				width = motion * (0.06f + 0.04f * idxNorm);
			}
#endif
			ops[i].unisonPhaseSpread = width;
#ifdef METAMODULE
			ops[i].unisonDetune = motion * 0.006f;  // 0..0.6% detune (~10 cents max)
#else
			ops[i].unisonDetune = 0.0f;
#endif
			ops[i].unisonStereo = 1.0f;

			// Conservative bandlimit under heavy motion/complexity.
			ops[i].bandlimitBias = 1.0f + 0.6f * motion + 0.7f * complexAmt;

			ops[i].mipLevel = 0;
			ops[i].cachedPhaseInc = (fundamentalHz * ops[i].ratio) / kInternalRate;
		}

		// COLOR stays spectral only.
		// Do not retune operator ratios here: even non-carrier ratio offsets can make
		// some FM presets feel pitch-unstable, which is surprising for a tone-color macro.

		float opLevelTrim[4];
#ifdef METAMODULE
		bool allZero = true;
		for (int i = 0; i < 4; ++i) {
			if (rawLevels[i] > 0.0001f) {
				allZero = false;
				break;
			}
		}
		for (int i = 0; i < 4; ++i) {
			opLevelTrim[i] = allZero ? 1.0f : rawLevels[i];
		}
#else
		for (int i = 0; i < 4; ++i) {
			opLevelTrim[i] = rawLevels[i];
		}
#endif
		if (wtFormMode == 2) {
			for (int i = 0; i < 4; ++i) {
				opLevelTrim[i] *= (i == 0) ? 0.92f : 0.88f;
			}
		}

		// Algo 0 (Stack): op3/op4 is always sine — fix tableIndex once per block so the
		// post-tick COLOR fold check correctly skips it (avoids fold applying to a sine
		// output when the user has op3 in WT mode).
		if (algo == 0) {
			ops[3].tableIndex = -1;
		}

		blockL[0] = carryL;
		blockR[0] = carryR;

		const float dt = 1.f / kInternalRate;
		float cutoffBase = cvSum(clamp01(params[FILTER_CUTOFF_PARAM].getValue()),    CUTOFF_CV_INPUT);
		float resNorm    = cvSum(clamp01(params[FILTER_RESONANCE_PARAM].getValue()), RESONANCE_CV_INPUT);
		float driveNorm  = cvSum(clamp01(params[FILTER_DRIVE_PARAM].getValue()),     DRIVE_CV_INPUT);
		float morph      = clamp01(params[FILTER_MORPH_PARAM].getValue());
		float envMod = envDepth;
		float bitcrushKnob01 = cvSum(clamp01(params[BITCRUSH_PARAM].getValue()), BITCRUSH_CV_INPUT) * 0.7f;
		const bool bitcrushEnabled = bitcrushKnob01 > 0.001f;
		const bool clockConnected = inputs[CLOCK_INPUT].isConnected();
		const phaseon::Wavetable* activeTable = tablePtr;
		static const float kAlgoLevelMul[14][4] = {
			{1.00f, 0.90f, 0.90f, 0.90f},
			{1.00f, 0.90f, 1.00f, 0.90f},
			{1.00f, 0.90f, 0.90f, 0.90f},
			{0.80f, 0.80f, 0.80f, 0.80f},
			{1.00f, 0.95f, 0.95f, 0.85f},
			{1.00f, 0.95f, 0.92f, 0.88f},
			{1.00f, 0.90f, 0.94f, 0.86f},
			{1.00f, 0.88f, 0.96f, 0.88f},
			{1.00f, 0.90f, 0.82f, 0.82f},
			{1.00f, 0.88f, 1.00f, 0.88f},
			{0.98f, 0.82f, 0.95f, 0.82f},
			{1.00f, 1.00f, 0.86f, 0.86f},
			{1.00f, 0.88f, 0.88f, 0.84f},
			{1.00f, 0.92f, 0.90f, 0.88f},
		};
		static const uint8_t kAlgoCarrierMask[14] = {
			0x1,
			(uint8_t)((1u << 2) | (1u << 0)),
			0x1,
			0xF,
			0x1,
			0x1,
			(uint8_t)((1u << 1) | (1u << 0)),
			0x1,
			(uint8_t)((1u << 2) | (1u << 1) | (1u << 0)),
			(uint8_t)((1u << 2) | (1u << 0)),
			(uint8_t)((1u << 2) | (1u << 0)),
			(uint8_t)((1u << 1) | (1u << 0)),
			0x1,
			0x1,
		};
		for (int oi = 0; oi < 4; ++oi) {
			ops[oi].level = kAlgoLevelMul[algo][oi] * opLevelTrim[oi] * rndLevelMul[oi];
		}
		const phaseon::Wavetable* opTables[4] = {
			(activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr,
			(activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr,
			(activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr,
			(activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr,
		};
		const uint8_t blockCarrierMask = kAlgoCarrierMask[algo];
		// Coefficients will be refreshed in small sub-blocks inside the main loop
		const int filterSubBlockSize = 4;
		int filterSubCounter = 0;

		// True sub oscillator (sine, -1 octave), mixed post-filter so it stays clean.
		float subPhaseInc = (fundamentalHz * 0.5f) / kInternalRate;
		if (!std::isfinite(subPhaseInc)) subPhaseInc = 0.f;
		if (subPhaseInc < 0.f) subPhaseInc = 0.f;
		if (subPhaseInc > 0.5f) subPhaseInc = 0.5f;
		const float subGainMax = 0.85f;

		// TEAR comb delay length (once per block). 0 => bypass.
		int tearDelayLen = 0;
		if (tearWet1 > 0.0005f) {
			float combHz = fundamentalHz * (8.0f + 28.0f * tearWet1);
			if (combHz < 40.f) combHz = 40.f;
			tearDelayLen = (int)(kInternalRate / combHz);
			if (tearDelayLen < 8) tearDelayLen = 8;
			if (tearDelayLen > kTearDelaySize - 1) tearDelayLen = kTearDelaySize - 1;
		}

		// Snapshot block-stable warp/table indices for all operators once per block.
		// tick() reads blockWarpMode/blockTableIndex so the per-sample switch dispatch
		// never re-reads volatile struct fields or chases memory on every sample.
		for (int oi = 0; oi < 4; ++oi)
			ops[oi].prepareBlock();

		const bool lfoEnabled = params[LFO_ENABLE_PARAM].getValue() > 0.5f;
		if (!lfoEnabled) lastLfoValue = 0.f;
		for (int i = 1; i <= kBlockSize; ++i) {
			bool clockHigh = clockConnected && (inputs[CLOCK_INPUT].getVoltage() > 1.0f);
			float lfoOut = 0.f;
			if (lfoEnabled) {
				float syncDivNow = applyMod01(lfoSyncDiv, lastLfoValue, syncDivModTrim);
				float targetLfoFreq = clockSync.process(clockHigh, syncDivNow, kInternalRate);
				lfoOut = internalLfo.process(targetLfoFreq, lfoGravity, lastGate, kInternalRate);
				lastLfoValue = lfoOut;
			}

			float finalWarp = warpMapped;
			float timbreMod = applyMod01(timbreBase, lfoOut, wtFrameModTrim);
			float formAmtMod = applyMod01(formAmtBase, lfoOut, vowelModTrim);
			if (wtFormMode == 1) {
				formAmtMod = clamp01(formAmtMod + wtFormBoost * growlBoost * (0.06f * complexAmt + 0.05f * syncEnvAmount));
			}
			else if (wtFormMode == 2) {
				formAmtMod = clamp01(formAmtMod + wtFormBoost * yoiBoost * (0.18f + 0.16f * syncEnvAmount));
			}
			else if (wtFormMode == 3) {
				formAmtMod = clamp01(formAmtMod);
			}
			formAmtMod = clamp01(formAmtMod * rndFormantMul);
			float edgeNow = applyMod01(edgeBase, lfoOut, op1FbModTrim * 2.0f);
			float fbNow = (edgeNow * edgeNow) * 1.6f * (1.0f + 0.60f * complexAmt);
			float fbModAmt = std::fabs(op1FbModTrim);
			if (fbModAmt > 1.0f) fbModAmt = 1.0f;
			float lfoPol = (op1FbModTrim >= 0.0f) ? lfoOut : -lfoOut;
			float fbModTarget = (0.5f + 0.5f * lfoPol) * 0.98f;
			fbNow = fbNow + (fbModTarget - fbNow) * fbModAmt;
			if (fbNow < 0.0f) fbNow = 0.0f;
			if (fbNow > 0.98f) fbNow = 0.98f;
			float finalCutoff = applyMod01(cutoffBase, lfoOut, cutoffModTrim);

			float decayNow = applyMod01(decayBase, lfoOut, decayModTrim);
			float dec = 0.0050f + (decayNow * decayNow) * 2.4950f;
			ampEnv.setTimes(atk, dec, dt);

			for (int oi = 0; oi < 4; ++oi) {
				float fp = clamp01(timbreMod + rndFrameOff[oi]);
				if (wtFormMode == 1) {
					float growlEnv = clamp01(macroEnv + syncEnvAmount * 0.18f);
					float growlRegion = clamp01((fp - 0.45f) * (1.0f / 0.55f));
					fp = clamp01(0.08f * wtFormBoost * growlBoost + fp * (1.0f - 0.28f * wtFormBoost * growlBoost) + wtFormBoost * growlBoost * (growlEnv * 0.04f));
					ops[oi].phaseWarp = finalWarp * (1.0f + wtFormBoost * growlBoost * (0.18f * growlRegion + 0.08f * complexAmt + 0.04f * growlEnv));
				}
				else if (wtFormMode == 2) {
					float envPush = clamp01(macroEnv + syncEnvAmount * 0.50f);
					float biteEnv = envPush * (0.70f + 0.30f * syncEnvAmount);
					fp = clamp01(0.16f * wtFormBoost * yoiBoost + fp * (1.0f - 0.16f * wtFormBoost * yoiBoost) + wtFormBoost * yoiBoost * envPush * (0.14f + 0.14f * complexAmt) + biteEnv * 0.10f);
					ops[oi].phaseWarp = finalWarp * (1.0f + wtFormBoost * yoiBoost * (0.12f * envPush + 0.08f * biteEnv));
					ops[oi].pdAmount = clamp01(ops[oi].pdAmount + biteEnv * (0.10f + 0.12f * complexAmt));
				}
				else if (wtFormMode == 3) {
					fp = clamp01(0.05f * wtFormBoost * tearBoost + fp * (1.0f - 0.22f * wtFormBoost * tearBoost));
					ops[oi].phaseWarp = finalWarp * (1.0f + wtFormBoost * tearBoost * (0.18f * complexAmt));
					ops[oi].tear = clamp01(tearWet1 + wtFormBoost * tearBoost * (0.22f + 0.36f * complexAmt));
				}
				// wtFormMode == 0: phaseWarp already set to warpMapped in block preamble;
				// no per-sample write needed, preserving Operator::tick() warp cache.
				if (wtScrollStepped && ops[oi].tableIndex == 0) {
					int steps = 16;
					if (tablePtr && tablePtr->frameCount > 1) steps = tablePtr->frameCount;
					if (wtFormMode == 3 && steps > 4) steps = 4;
					if (steps < 2) steps = 2;
					float s = (float)(steps - 1);
					fp = std::round(fp * s) / s;
				}
				ops[oi].framePos = fp;
			}
			// Ops 0–2: feedback is block-constant (set in preamble); op 3 is LFO-modulated.
			ops[3].feedback = fbNow;

			// Update filter coefficients every small sub-block to track envelope changes
			if (filterSubCounter <= 0) {
				filter.updateCoefficients(finalCutoff, resNorm, driveNorm, morph, envMod);
				filterSubCounter = filterSubBlockSize;
			}

			// Dynamic FM link from amp envelope to PM index.
			// Using the amp envelope (not filter env follower) so ENV-FM fires
			// on every gate regardless of density/FM level.
			float env = ampEnv.tick();
			if (!std::isfinite(env)) env = 0.f;
			env = clamp01(env);
			float fmEnvDrive = env * fmEnvAmount;
			if (!std::isfinite(fmEnvDrive)) fmEnvDrive = 0.f;
			fmEnvDrive = clamp01(fmEnvDrive);
			// Filter env follower still drives operator PM-depth env and carrier sync.
			// Keep the expected 0..2 range used by Operator::tick() instead of clamping to 0..1.
			float envFollow = filter.getEnvFollower();
			if (!std::isfinite(envFollow)) envFollow = 0.f;
			envFollow = clamp(envFollow, 0.f, 2.f);

			// Tension-scaled FM index shaping:
			// - Modulators (ops 2/3/4) get the aggressive drive.
			// - Carrier (op1 / index 0) is guarded to keep sub-fundamental stable.
			float modIndexScale = 0.35f + 1.65f * tension;
			float carrierIndexScale = 0.5f * tension;

			// TIMBRE: additive FM boost (fires even at density=0; center=0.5 is backward compat).
			float adjustedFm = fm + timbreAddFm;
			if (adjustedFm < 0.f) adjustedFm = 0.f;

			// ENV→FM should stay clearly audible even when base FM is low or near limiter saturation.
			// Apply envelope shaping before soft limiting:
			// - multiplicative growth for "bloom"
			// - additive kick so low-FM patches still react
			float fmModRaw = adjustedFm * modIndexScale;
			float fmCarrierRaw = adjustedFm * carrierIndexScale;
			float modMul = 1.0f + fmEnvDrive * (1.20f + 2.20f * tension);
			float modAdd = fmEnvDrive * (0.18f + 0.82f * tension);
			float carrierMul = 1.0f + fmEnvDrive * (0.25f + 0.55f * tension);
			float carrierAdd = fmEnvDrive * 0.10f * tension;
			fmModRaw = fmModRaw * modMul + modAdd;
			fmCarrierRaw = fmCarrierRaw * carrierMul + carrierAdd;
			if (!std::isfinite(fmModRaw)) fmModRaw = 0.f;
			if (!std::isfinite(fmCarrierRaw)) fmCarrierRaw = 0.f;
			float fmCarrier = fmCarrierRaw;
			float fmMod = fmModRaw;

			// Soft-limit PM index targets so dense patches remain musical and predictable.
			// Use gentle saturation instead of hard clipping to keep the top end expressive.
			fmCarrier = 2.0f * phaseon::phaseon_fast_tanh(fmCarrier * 0.5f);
			fmMod = 3.0f * phaseon::phaseon_fast_tanh(fmMod * (1.0f / 3.0f));

			// Formant/Sync link: envelope opens carrier phase increment as the sound gets louder.
			// This creates a vocalizing sync/formant feel without destabilizing pitch too much.
			float carrierSyncMul = 1.0f + envFollow * syncEnvAmount * (0.06f + 0.24f * tension);
			if (!std::isfinite(carrierSyncMul)) carrierSyncMul = 1.0f;
			carrierSyncMul = clamp(carrierSyncMul, 1.0f, 1.8f);
			ops[0].cachedPhaseInc = (fundamentalHz * ops[0].ratio * carrierSyncMul) / kInternalRate;

			// Algorithm-specific PM/loudness calibration (golden-locked).
			// If you retune these, update docs/phaseon1-pm-golden-calibration.md.
			struct AlgoCal {
				float modMul;
				float carrierMul;
				float carrierFbMul;
				float carrierTrim;
				float modBleed;
			};
			const AlgoCal kAlgoCal[14] = {
				// Stack
				{0.80f, 0.90f, 0.40f, 1.06f, 0.40f},
				// Pairs
				{0.92f, 0.98f, 0.00f, 0.98f, 0.35f},
				// Swarm
				{0.70f, 0.85f, 0.00f, 1.02f, 0.30f},
				// Wide
				{0.00f, 0.00f, 0.00f, 0.92f, 0.22f},
				// Cascade
				{0.84f, 0.94f, 0.00f, 1.01f, 0.28f},
				// Fork
				{0.90f, 0.95f, 0.00f, 1.00f, 0.31f},
				// Anchor
				{0.76f, 0.93f, 0.00f, 1.00f, 0.24f},
				// Pyramid
				{0.94f, 0.90f, 0.00f, 1.04f, 0.27f},
				// Triple Carrier
				{0.68f, 0.86f, 0.00f, 0.96f, 0.22f},
				// Dual Cascade
				{0.78f, 1.00f, 0.00f, 1.00f, 0.16f},
				// Ring
				{0.74f, 0.92f, 0.00f, 0.98f, 0.26f},
				// Dual Mod
				{0.88f, 0.98f, 0.00f, 0.99f, 0.22f},
				// Mod Bus
				{0.98f, 0.92f, 0.00f, 1.03f, 0.24f},
				// Feedback Ladder
				{0.90f, 0.90f, 0.28f, 1.05f, 0.20f},
			};
			static_assert(sizeof(kAlgoCal) / sizeof(kAlgoCal[0]) == 14, "Phaseon1 algo calibration table must match 14 algorithms");
			const int algoIdx = clamp(algo, 0, 13);
			const AlgoCal& cal = kAlgoCal[algoIdx];

			auto pmDepthForMod = [&](float base, int oi) {
				float v = base * rndFmMul[oi];
				if (!std::isfinite(v)) v = 0.f;
				return clamp(v, 0.f, 3.0f);
			};
			auto pmDepthForCarrier = [&](float base, int oi, float fbAdd = 0.f) {
				float v = base * rndFmMul[oi] + fbAdd;
				if (!std::isfinite(v)) v = 0.f;
				return clamp(v, 0.f, 2.2f);
			};

			uint8_t carrierMask = blockCarrierMask;

			// Algorithms are intentionally constrained so modulators have higher indices.
			switch (algo) {
			default:
			case 0: {
				// Stack: 4->3->2->1->out (0 is main carrier)
				// ops[3].tableIndex is already -1 (forced in block preamble above).
				float stackMod = fmMod * cal.modMul;
				float stackCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(stackMod, 3);
				ops[2].fmDepth = pmDepthForMod(stackMod, 2);
				ops[1].fmDepth = pmDepthForMod(stackMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(stackCarrier, 0, fbNow * cal.carrierFbMul);
				ops[3].tick(fundamentalHz, 0.f, opTables[3], kInternalRate, envFollow);
				ops[2].tick(fundamentalHz, ops[3].output, opTables[2], kInternalRate, envFollow);
				ops[1].tick(fundamentalHz, ops[2].output, opTables[1], kInternalRate, envFollow);
				float growlDriver = phaseon::phaseon_fast_tanh(ops[3].output * 2.0f);
				float combinedModulation = ops[1].output + (growlDriver * 0.4f * fbNow);
				ops[0].tick(fundamentalHz, combinedModulation, opTables[0], kInternalRate, envFollow);
				break;
			}
			case 1: {
				// Pairs: (4->3) + (2->1), carriers: 3 and 1
				float pairMod = fmMod * cal.modMul;
				float pairCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(pairMod, 3);
				ops[2].fmDepth = pmDepthForMod(pairMod, 2);
				ops[1].fmDepth = pmDepthForMod(pairMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(pairCarrier, 0);
				ops[3].tick(fundamentalHz, 0.f, opTables[3], kInternalRate, envFollow);
				ops[2].tick(fundamentalHz, ops[3].output, opTables[2], kInternalRate, envFollow);
				ops[1].tick(fundamentalHz, 0.f, opTables[1], kInternalRate, envFollow);
				float op4Bypass = ops[3].output * (0.25f + 1.75f * fbBypassAmt);
				ops[0].tick(fundamentalHz, ops[1].output + op4Bypass, opTables[0], kInternalRate, envFollow);
				break;
			}
			case 2: {
				// Swarm: 4+3+2 -> 1 (0 is main carrier)
				float swarmMod = fmMod * cal.modMul;
				float swarmCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(swarmMod, 3);
				ops[2].fmDepth = pmDepthForMod(swarmMod, 2);
				ops[1].fmDepth = pmDepthForMod(swarmMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(swarmCarrier, 0);
				ops[3].tick(fundamentalHz, 0.f, opTables[3], kInternalRate, envFollow);
				ops[2].tick(fundamentalHz, 0.f, opTables[2], kInternalRate, envFollow);
				ops[1].tick(fundamentalHz, 0.f, opTables[1], kInternalRate, envFollow);
				float op4Bypass = ops[3].output * (0.25f + 1.75f * fbBypassAmt);
				float mod = ops[1].output + ops[2].output + ops[3].output + op4Bypass;
				ops[0].tick(fundamentalHz, mod, opTables[0], kInternalRate, envFollow);
				break;
			}
			case 3: {
				// Wide: all carriers.
				// TIMBRE → per-operator micro-detune spread (op0 clean, op3 widest).
				float wideSpread = 0.f;
				if (!anyWT)
					wideSpread = (timbreBase - 0.5f) * 2.0f * 0.004f; // ±0.4% pitch spread at ±full
				for (int oi = 0; oi < 4; ++oi) {
					ops[oi].fmDepth = 0.0f;
					if (wideSpread != 0.f)
						ops[oi].cachedPhaseInc *= (1.0f + (oi - 1.5f) * wideSpread);
					ops[oi].tick(fundamentalHz, 0.f, opTables[oi], kInternalRate, envFollow);
				}
				break;
			}
			case 4: {
				// Cascade: 4->3->1 and 2->1 (zero-based: 3->2->0 and 1->0)
				float cascadeMod = fmMod * cal.modMul;
				float cascadeCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(cascadeMod, 3);
				ops[2].fmDepth = pmDepthForMod(cascadeMod, 2);
				ops[1].fmDepth = pmDepthForMod(cascadeMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(cascadeCarrier, 0);
				ops[3].level = 0.85f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.95f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.95f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, 0.f, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[2].output + ops[1].output, t0, kInternalRate, envFollow);
				carrierMask = 0x1;
				break;
			}
			case 5: {
				// Fork: 4->2->1 and 3->1 (zero-based: 3->1->0 and 2->0)
				float forkMod = fmMod * cal.modMul;
				float forkCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(forkMod, 3);
				ops[2].fmDepth = pmDepthForMod(forkMod, 2);
				ops[1].fmDepth = pmDepthForMod(forkMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(forkCarrier, 0);
				ops[3].level = 0.88f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.92f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.95f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, 0.f, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, ops[3].output, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[1].output + ops[2].output, t0, kInternalRate, envFollow);
				carrierMask = 0x1;
				break;
			}
			case 6: {
				// Anchor: 4->3->1 and 2->OUT (zero-based: 3->2->0 and 1->OUT)
				float anchorMod = fmMod * cal.modMul;
				float anchorCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(anchorMod, 3);
				ops[2].fmDepth = pmDepthForMod(anchorMod, 2);
				ops[1].fmDepth = 0.0f;
				ops[0].fmDepth = pmDepthForCarrier(anchorCarrier, 0);
				ops[3].level = 0.86f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.94f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.90f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, 0.f, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[2].output, t0, kInternalRate, envFollow);
				carrierMask = (1u << 1) | (1u << 0);
				break;
			}
			case 7: {
				// Pyramid: 4->3, 2->3, 3->1 (zero-based: 3->2, 1->2, 2->0)
				float pyramidMod = fmMod * cal.modMul;
				float pyramidCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(pyramidMod, 3);
				ops[2].fmDepth = pmDepthForMod(pyramidMod, 2);
				ops[1].fmDepth = pmDepthForMod(pyramidMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(pyramidCarrier, 0);
				ops[3].level = 0.88f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.96f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.88f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, 0.f, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output + ops[1].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[2].output, t0, kInternalRate, envFollow);
				carrierMask = 0x1;
				break;
			}
			case 8: {
				// Triple Carrier: 4->1 and 2/3 direct out (zero-based: 3->0, 1->OUT, 2->OUT)
				float tripleMod = fmMod * cal.modMul;
				float tripleCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(tripleMod, 3);
				ops[2].fmDepth = 0.0f;
				ops[1].fmDepth = 0.0f;
				ops[0].fmDepth = pmDepthForCarrier(tripleCarrier, 0);
				ops[3].level = 0.82f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.82f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.90f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, 0.f, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, 0.f, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[3].output, t0, kInternalRate, envFollow);
				carrierMask = (1u << 2) | (1u << 1) | (1u << 0);
				break;
			}
			case 9: {
				// Dual Cascade: clean dual 2-op branches (zero-based: 3->2 and 1->0)
				float dualMod = fmMod * cal.modMul;
				float dualCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(dualMod, 3);
				ops[2].fmDepth = pmDepthForCarrier(dualCarrier, 2);
				ops[1].fmDepth = pmDepthForMod(dualMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(dualCarrier, 0);
				ops[3].level = 0.88f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 1.0f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.88f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, 0.f, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[1].output, t0, kInternalRate, envFollow);
				carrierMask = (1u << 2) | (1u << 0);
				break;
			}
			case 10: {
				// Ring: partial cross-mod pair plus one clean modulated branch.
				// zero-based: 1<->0 and 3->2
				float ringMod = fmMod * cal.modMul;
				float ringCarrier = fmCarrier * cal.carrierMul;
				float ringPrev0 = ops[0].prevOut;
				float ringPrev1 = ops[1].prevOut;
				ops[3].fmDepth = pmDepthForMod(ringMod, 3);
				ops[2].fmDepth = pmDepthForCarrier(ringCarrier, 2);
				ops[1].fmDepth = pmDepthForMod(ringMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(ringCarrier, 0);
				ops[3].level = 0.82f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.95f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.82f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 0.98f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, ringPrev0, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[1].output + ringPrev1 * 0.25f, t0, kInternalRate, envFollow);
				carrierMask = (1u << 2) | (1u << 0);
				break;
			}
			case 11: {
				// Dual Mod: 4->2->OUT and 3->1->OUT (zero-based: 3->1 and 2->0)
				float dualMod = fmMod * cal.modMul;
				float dualCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(dualMod, 3);
				ops[2].fmDepth = pmDepthForMod(dualMod, 2);
				ops[1].fmDepth = pmDepthForCarrier(dualCarrier, 1);
				ops[0].fmDepth = pmDepthForCarrier(dualCarrier, 0);
				ops[3].level = 0.86f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.86f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 1.0f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, 0.f, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, ops[3].output, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[2].output, t0, kInternalRate, envFollow);
				carrierMask = (1u << 1) | (1u << 0);
				break;
			}
			case 12: {
				// Mod Bus: op4 fans into op3/op2/op1, then op3+op2 feed op1->OUT
				// zero-based: 3->2, 3->1, 3->0 and (2+1)->0
				float busMod = fmMod * cal.modMul;
				float busCarrier = fmCarrier * cal.carrierMul;
				ops[3].fmDepth = pmDepthForMod(busMod, 3);
				ops[2].fmDepth = pmDepthForMod(busMod, 2);
				ops[1].fmDepth = pmDepthForMod(busMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(busCarrier, 0);
				ops[3].level = 0.84f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.88f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.88f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, ops[3].output, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				float busDirect = ops[3].output * (0.18f + 0.32f * fbBypassAmt);
				ops[0].tick(fundamentalHz, ops[2].output + ops[1].output + busDirect, t0, kInternalRate, envFollow);
				carrierMask = 0x1;
				break;
			}
			case 13: {
				// Feedback Ladder: 4->3->2->1 with 2->3 feedback
				// zero-based: 3->2->1->0 with 1->2 using previous sample output for stability
				float ladderMod = fmMod * cal.modMul;
				float ladderCarrier = fmCarrier * cal.carrierMul;
				float feedbackTap = ops[1].prevOut;
				ops[3].fmDepth = pmDepthForMod(ladderMod, 3);
				ops[2].fmDepth = pmDepthForMod(ladderMod, 2);
				ops[1].fmDepth = pmDepthForMod(ladderMod, 1);
				ops[0].fmDepth = pmDepthForCarrier(ladderCarrier, 0, fbNow * cal.carrierFbMul);
				ops[3].level = 0.88f * opLevelTrim[3] * rndLevelMul[3];
				ops[2].level = 0.90f * opLevelTrim[2] * rndLevelMul[2];
				ops[1].level = 0.92f * opLevelTrim[1] * rndLevelMul[1];
				ops[0].level = 1.0f * opLevelTrim[0] * rndLevelMul[0];
				const phaseon::Wavetable* t3 = (activeTable && ops[3].tableIndex >= 0) ? activeTable : nullptr;
				ops[3].tick(fundamentalHz, 0.f, t3, kInternalRate, envFollow);
				const phaseon::Wavetable* t2 = (activeTable && ops[2].tableIndex >= 0) ? activeTable : nullptr;
				ops[2].tick(fundamentalHz, ops[3].output + feedbackTap * 0.42f, t2, kInternalRate, envFollow);
				const phaseon::Wavetable* t1 = (activeTable && ops[1].tableIndex >= 0) ? activeTable : nullptr;
				ops[1].tick(fundamentalHz, ops[2].output, t1, kInternalRate, envFollow);
				const phaseon::Wavetable* t0 = (activeTable && ops[0].tableIndex >= 0) ? activeTable : nullptr;
				ops[0].tick(fundamentalHz, ops[1].output, t0, kInternalRate, envFollow);
				carrierMask = 0x1;
				break;
			}
			}

			// COLOR: consistent global spectral enrichment.
			// Keep it pitch-safe by applying the same post-mix shaping regardless of preset architecture.
			// This avoids the old preset-dependent behavior where COLOR sometimes felt like detune,
			// pitch drift, or no-op depending on WT/PD/tear routing.
			float colorMix = 0.f;
			if (colorAmt > 0.001f) {
				colorMix = 0.42f * colorAmt * colorAmt;
			}

			// Build carrier/mod sums, then apply Morph as carrier↔mod blend.
			float carriersL = 0.f, carriersR = 0.f;
			float modsL = 0.f, modsR = 0.f;
			for (int oi = 0; oi < 4; ++oi) {
				if (carrierMask & (1u << oi)) {
					carriersL += ops[oi].outputL;
					carriersR += ops[oi].outputR;
				}
				else {
					modsL += ops[oi].outputL;
					modsR += ops[oi].outputR;
				}
			}
			float modMix = cal.modBleed * algoMorph;
			float carrierScale = cal.carrierTrim * (1.0f - 0.20f * algoMorph);
			float outL = carriersL * carrierScale + modsL * modMix;
			float outR = carriersR * carrierScale + modsR * modMix;
			if (colorMix > 0.f) {
				float colorDrive = 1.0f + colorAmt * 4.5f;
				float shapedL = phaseon::phaseon_fast_sin_w0(outL * colorDrive);
				float shapedR = phaseon::phaseon_fast_sin_w0(outR * colorDrive);
				outL += (shapedL - outL) * colorMix;
				outR += (shapedR - outR) * colorMix;
			}

			// Always-on SVF filter first.
			float monoIn = 0.5f * (outL + outR);
			filter.updateEnv(monoIn);
			filter.processSample(0, outL);
			filter.processSample(1, outR);

			// Unified master crossover: split clean sub once, process only mids/highs.
			float cleanSubL = 0.f, cleanSubR = 0.f;
			float midHighL = 0.f, midHighR = 0.f;
			masterBusL.splitSub(outL, cleanSubL, midHighL);
			masterBusR.splitSub(outR, cleanSubR, midHighR);

			// Destructive path (mids/highs only): formant then crusher.
			float timbreModFormant = clamp01(timbreMod);
			if (!std::isfinite(timbreModFormant)) timbreModFormant = 0.f;
			if (!std::isfinite(formAmtMod)) formAmtMod = 0.f;
			formAmtMod = clamp01(formAmtMod);
			formant.process(midHighL, midHighR, kInternalRate, fundamentalHz, timbreModFormant, formAmtMod);
			if (bitcrushEnabled) {
				midHighL = retroCrushL.process(midHighL, bitcrushKnob01);
				midHighR = retroCrushR.process(midHighR, bitcrushKnob01);
			}

			// TEAR FX (mids/highs only): fold + short comb + noise. True bypass at 0.
			float tearMix = tearWet0 + tearWetStep * (float)(i - 1);
			if (tearMix > 0.0005f && tearDelayLen > 0) {
				float drive = 1.0f + 7.0f * tearMix;
				float foldedL = tearFoldEnabled ? phaseon::phaseon_fast_sin_w0(midHighL * drive) : midHighL;
				float foldedR = tearFoldEnabled ? phaseon::phaseon_fast_sin_w0(midHighR * drive) : midHighR;
				float nL = (u01_from_u32(xorshift32(tearNoiseState)) * 2.f - 1.f);
				float nR = (u01_from_u32(xorshift32(tearNoiseState)) * 2.f - 1.f);
				// Keep TEAR as an effect (not a generator): only inject noise when input has energy.
				float signalActivity = clamp01((std::fabs(midHighL) + std::fabs(midHighR)) * 1.5f);
				float noiseAmt = (0.01f + 0.06f * tearMix) * signalActivity;
				float preL = foldedL + nL * noiseAmt;
				float preR = foldedR + nR * noiseAmt;

				int read = (tearDelayWrite - tearDelayLen) & (kTearDelaySize - 1);
				float dL = tearDelayL[read];
				float dR = tearDelayR[read];
				float fb = 0.12f + 0.72f * tearMix;
				float combMix = 0.18f + 0.55f * tearMix;
				tearDelayL[tearDelayWrite] = preL + dL * fb;
				tearDelayR[tearDelayWrite] = preR + dR * fb;
				tearDelayWrite = (tearDelayWrite + 1) & (kTearDelaySize - 1);

				float wetL = phaseon::phaseon_fast_tanh((preL + dL * combMix) * (1.0f + 1.4f * tearMix));
				float wetR = phaseon::phaseon_fast_tanh((preR + dR * combMix) * (1.0f + 1.4f * tearMix));
				midHighL = midHighL + (wetL - midHighL) * tearMix;
				midHighR = midHighR + (wetR - midHighR) * tearMix;
			}

			// Sub oscillator (octave down): inject after TEAR so SUB never enters TEAR processing.
			float subL = 0.f;
			float subR = 0.f;
			if (subLevel > 0.0001f && subPhaseInc > 0.f) {
				subPhase += subPhaseInc;
				subPhase -= (float)(int)subPhase;
				if (subPhase < 0.f) subPhase += 1.f;
				float subFund = phaseon::phaseon_fast_sin_01(subPhase);
				float subH2 = phaseon::phaseon_fast_sin_01(phaseon::Operator::wrap01(subPhase * 2.0f));
				float subShaped = phaseon::phaseon_fast_tanh((subFund + 0.18f * subH2) * 1.2f);
				float sub = subShaped * (subGainMax * subLevel);
				subL = sub;
				subR = sub;
			}

			// Recombine clean crossover low band + processed mids/highs + dedicated SUB, then finalize.
			float combinedL = cleanSubL + midHighL + subL;
			float combinedR = cleanSubR + midHighR + subR;
			outL = masterBusL.finalize(combinedL, env);
			outR = masterBusR.finalize(combinedR, env);
			--filterSubCounter;

			blockL[i] = outL;
			blockR[i] = outR;
		}

		carryL = blockL[kBlockSize];
		carryR = blockR[kBlockSize];
		readIndex = 0;
	}

	void process(const ProcessArgs& args) override {
		if (!presetInitDone) {
			presetInitDone = true;
		}

		if (!bankLoaded) {
			outputs[LEFT_OUTPUT].setVoltage(0.f);
			outputs[RIGHT_OUTPUT].setVoltage(0.f);
			outputs[LFO_OUTPUT].setVoltage(0.f);
			outputs[ENV_OUTPUT].setVoltage(0.f);
			lights[GATE_LIGHT].setBrightness(0.f);
			return;
		}

 		// Preset index + save
 		{
			if (presetStartupLockSamples > 0) {
				params[PRESET_INDEX_PARAM].setValue((float)(currentPreset + 1));
				presetStartupLockSamples--;
			} else {
#ifdef METAMODULE
				if (mmPresetCvCooldown > 0) --mmPresetCvCooldown;
#endif
				int idxFromKnob = (int)std::round(params[PRESET_INDEX_PARAM].getValue()) - 1;
				if (inputs[PRESET_CV_INPUT].isConnected()) {
					float cv = clamp(inputs[PRESET_CV_INPUT].getVoltage(), 0.f, 10.f);
					idxFromKnob = (int)std::round(cv * ((float)(kPresetSlots - 1) / 10.f));
				}
				if (idxFromKnob < 0) idxFromKnob = 0;
				if (idxFromKnob >= kPresetSlots) idxFromKnob = kPresetSlots - 1;
				if (idxFromKnob != currentPreset) {
#ifdef METAMODULE
					// Guard: bankApplySlot() is expensive (3x block render + full DSP reset).
					// A jittering CV at a slot boundary would spam it every sample.
					if (mmPresetCvCooldown == 0) {
						bankApplySlot(idxFromKnob);
						mmPresetCvCooldown = kPresetCvCooldownSamples;
					}
#else
					bankApplySlot(idxFromKnob);
#endif
				}
			}
			bool save = params[PRESET_SAVE_PARAM].getValue() > 0.5f;
			if (save && !lastPresetSave) {
				bankCaptureCurrentToSlot(currentPreset);
#ifndef METAMODULE
				// On MetaModule the audio thread must never perform disk I/O.
				// The bank is persisted via dataToJson() when the firmware saves the patch.
				bankSave();
#else
				MetaModule::Patch::mark_patch_modified();
#endif
			}
			lastPresetSave = save;
		}

		updateEditState();

		bool gate = false;
#ifdef METAMODULE
		// MetaModule usability fallback: when no gate cable is connected,
		// run in free-gate mode so the module is immediately audible.
		if (inputs[GATE_INPUT].isConnected()) {
			gate = inputs[GATE_INPUT].getVoltage() > 1.0f;
		} else {
			gate = true;
		}
#else
		gate = inputs[GATE_INPUT].getVoltage() > 1.0f;
#endif
		float voct = 0.f;
		if (inputs[VOCT_INPUT].isConnected()) {
			voct = inputs[VOCT_INPUT].getVoltage();
			// Keep pitch in a sane musical range on MetaModule patches.
			if (voct < -4.f) voct = -4.f;
			if (voct > 4.f) voct = 4.f;
		}

		bool heldPitchRetrigger = false;
		if (gate && lastGate) {
			const float noteThresholdVolts = 0.045f;
			const float stableDeltaVolts = 0.0015f;
			const int stableSamplesRequired = 8;
			float perSampleDelta = std::fabs(voct - lastVoct);
			if (!heldPitchChangePending) {
				if (std::fabs(voct - heldGateAnchorVoct) >= noteThresholdVolts) {
					heldPitchChangePending = true;
					heldPitchStableSamples = (perSampleDelta <= stableDeltaVolts) ? 1 : 0;
				}
			}
			else {
				if (perSampleDelta <= stableDeltaVolts) {
					heldPitchStableSamples++;
				}
				else {
					heldPitchStableSamples = 0;
				}
				if (heldPitchStableSamples >= stableSamplesRequired) {
					heldPitchRetrigger = true;
					heldGateAnchorVoct = voct;
					heldPitchChangePending = false;
					heldPitchStableSamples = 0;
				}
			}
		}
		else {
			resetHeldPitchTracking(voct);
		}

		if (gate && !lastGate) {
			fundamentalHz = 261.63f * fastExp2Approx(voct);
			ampEnv.setGate(true);
			armGateEdgeXfade(args.sampleRate);
			resetHeldPitchTracking(voct);
		} else if (!gate && lastGate) {
			ampEnv.setGate(false);
			armGateEdgeXfade(args.sampleRate);
			resetHeldPitchTracking(voct);
		} else if (gate) {
			fundamentalHz = 261.63f * fastExp2Approx(voct);
			if (heldPitchRetrigger) {
				ampEnv.level = 0.f;
				ampEnv.setGate(true);
				internalLfo.reset();
				armGateEdgeXfade(args.sampleRate);
			}
		}
		lastGate = gate;
		lastVoct = voct;
		lights[GATE_LIGHT].setBrightness(gate ? 1.f : 0.f);

		float hostRate = std::max(1.f, args.sampleRate);
		if (pendingPresetSwitchMute) {
			pendingPresetSwitchMute = false;
			presetSwitchMuteSamples = (int)(hostRate * 0.020f); // ~20 ms hard flush
			if (presetSwitchMuteSamples < 1) presetSwitchMuteSamples = 1;
		}
		float outL = 0.f;
		float outR = 0.f;
		if (presetSwitchMuteSamples <= 0) {
			float step = kInternalRate / hostRate;
			if (step < 0.0001f) step = 0.0001f;
			if (step > 4.0f) step = 4.0f;

			// Ensure we have a block ready.
			if (readIndex >= kBlockSize) {
				renderInternalBlock();
			}

			// Linear interpolation between block[readIndex] and block[readIndex+1]
			float aL = blockL[readIndex];
			float bL = blockL[readIndex + 1];
			float aR = blockR[readIndex];
			float bR = blockR[readIndex + 1];
			outL = aL + (bL - aL) * frac;
			outR = aR + (bR - aR) * frac;

			// Tiny gate-edge crossfade to suppress clicks without audible envelope lag.
			if (gateEdgeXfadeRemain > 0 && gateEdgeXfadeTotal > 0) {
				float t = 1.f - ((float)gateEdgeXfadeRemain / (float)gateEdgeXfadeTotal);
				if (t < 0.f) t = 0.f;
				if (t > 1.f) t = 1.f;
				outL = lerp(gateEdgeFromL, outL, t);
				outR = lerp(gateEdgeFromR, outR, t);
				gateEdgeXfadeRemain--;
			}
			lastPreVolL = outL;
			lastPreVolR = outR;

			frac += step;
			while (frac >= 1.f) {
				frac -= 1.f;
				readIndex++;
				if (readIndex >= kBlockSize) {
					renderInternalBlock();
				}
			}
		}

		float volumeParam = clamp01(params[VOLUME_PARAM].getValue());
		// Gentler gain curve: keep ~unity around 50% volume, reduce max pressure.
		float masterGain = volumeParam * (0.8f + 2.4f * volumeParam);
		if (presetSwitchMuteSamples > 0) {
			outL = 0.f;
			outR = 0.f;
			lastLfoValue = 0.f;
			presetSwitchMuteSamples--;
		}
		// Gentle output guard to reduce harsh clipping while preserving dynamics.
		const float outGuardDrive = 1.15f;
		outL = phaseon::phaseon_fast_tanh(outL * outGuardDrive) / outGuardDrive;
		outR = phaseon::phaseon_fast_tanh(outR * outGuardDrive) / outGuardDrive;
		outputs[LEFT_OUTPUT].setVoltage(outL * 5.f * masterGain);
		outputs[RIGHT_OUTPUT].setVoltage(outR * 5.f * masterGain);
		outputs[LFO_OUTPUT].setVoltage(lastLfoValue * 5.f);
		outputs[ENV_OUTPUT].setVoltage(filter.getEnvFollower() * 10.f);
	}

	bool loadWavetableFile(const std::string& path, std::string* err, bool notifyOnSuccess = true) {
		phaseon::WavetableBank tmp;
		int idx = tmp.loadFromWav(path, 2048);
		if (idx < 0 || idx >= tmp.count()) {
			if (err) *err = "Failed to load WAV";
#ifdef METAMODULE
			MetaModule::Gui::notify_user("Phaseon1: WT load failed", 3000);
#endif
			return false;
		}

		phaseon::Wavetable small = downsampleWavetableSmall(tmp.tables[(size_t)idx]);
		if (small.frameSize <= 0 || small.frameCount <= 0 || small.data.empty()) {
			if (err) *err = "Invalid wavetable";
#ifdef METAMODULE
			MetaModule::Gui::notify_user("Phaseon1: WT invalid", 3000);
#endif
			return false;
		}

		int inactive = 1 - wtActive.load(std::memory_order_acquire);
		if (inactive < 0) inactive = 1;
		if (inactive > 1) inactive = 1;
		wtBuf[inactive] = std::move(small);
		wtName = wtBuf[inactive].name;
		hasWt.store(true, std::memory_order_release);
		wtActive.store(inactive, std::memory_order_release);
#ifdef METAMODULE
		// Notify - wtName is a std::string member; build message in a local char buf
		// to avoid any dynamic allocation in the notification path.
		if (notifyOnSuccess) {
			char notifBuf[48];
			const char* nm = wtName.empty() ? "(unnamed)" : wtName.c_str();
			snprintf(notifBuf, sizeof(notifBuf), "WT loaded: %.36s", nm);
			MetaModule::Gui::notify_user(std::string_view{notifBuf, strlen(notifBuf)}, 2000);
		}
#endif
		return true;
	}

#ifdef METAMODULE
	size_t get_display_text(int display_id, std::span<char> text) override {
		if (display_id != PRESET_DISPLAY) return 0;
		if (text.empty()) return 0;
		const char* src = displayTextCstr();
		if (!src) return 0;
		size_t len = std::min(strlen(src), text.size());
		std::memcpy(text.data(), src, len);
		return len;
	}
#endif
};

bool Phaseon1::sPresetClipboardValid = false;
Phaseon1::PresetSlot Phaseon1::sPresetClipboard = {};

struct Phaseon1Widget : ModuleWidget {
	Phaseon1Widget(Phaseon1* module) {
		setModule(module);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Phaseon1.png")));
	#else
		// VCV Rack: no SVG; hardcode 20HP and use PNG directly.
		box.size = Vec(RACK_GRID_WIDTH * 20, RACK_GRID_HEIGHT);
		{
			auto* panelBg = new bem::PngPanelBackground(asset::plugin(pluginInstance, "res/Phaseon1.png"));
			panelBg->box.pos = Vec(0, 0);
			panelBg->box.size = box.size;
			addChild(panelBg);
		}
	#endif

#ifndef METAMODULE
		struct Phaseon1PortWidget : MVXPort {
			Phaseon1PortWidget() {
				imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_s1.png");
				imageHandle = -1;
			}
		};
		struct Phaseon1OutPortWidget : MVXPort {
			Phaseon1OutPortWidget() {
				imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_s1_red.png");
				imageHandle = -1;
			}
		};
#else
		using Phaseon1PortWidget = MVXPort;
		using Phaseon1OutPortWidget = MVXPort;
#endif
		struct Phaseon1MainKnob : MVXKnob_c {
			Phaseon1MainKnob() {
				box.size = box.size.mult(0.85f);
			}
		};
		struct Phaseon1EditKnob : MVXKnob {
			Phaseon1EditKnob() {
				// Phaseon1: use the white knob art for EDIT.
				// MetaModule doesn't render custom PNG knobs.
#ifndef METAMODULE
				this->bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh_BG.png");
				this->fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh.png");
				this->bgHandle = -1;
				this->fgHandle = -1;
#endif
				componentlibrary::RoundSmallBlackKnob ref;
				this->box.size = Vec(ref.box.size.x * 1.152f, ref.box.size.y * 1.152f);
			}
		};
		struct Phaseon1SubKnob : MVXKnob {
			Phaseon1SubKnob() {
				// Phaseon1: use the white knob art for SUB.
				// MetaModule doesn't render custom PNG knobs.
#ifndef METAMODULE
				this->bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh_BG.png");
				this->fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh.png");
				this->bgHandle = -1;
				this->fgHandle = -1;
#endif
				componentlibrary::RoundSmallBlackKnob ref;
				this->box.size = Vec(ref.box.size.x * 1.152f, ref.box.size.y * 1.152f);
			}
		};
		struct Phaseon1TearKnob : MVXKnob_c {
			Phaseon1TearKnob() {
				// Phaseon1: use the white knob art for TEAR.
				// MetaModule doesn't render custom PNG knobs.
#ifndef METAMODULE
				this->bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh_BG.png");
				this->fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh.png");
				this->bgHandle = -1;
				this->fgHandle = -1;
#endif
				componentlibrary::RoundSmallBlackKnob ref;
				this->box.size = Vec(ref.box.size.x * 1.152f, ref.box.size.y * 1.152f);
			}
		};
		struct Phaseon1FoldSwitch : CKSS {
			Phaseon1FoldSwitch() {
				box.size = box.size.mult(0.75f);
			}
		};
		// 70% trimpot for Phaseon1 (30% smaller visual)
		struct Phaseon1Trimpot : componentlibrary::Trimpot {
			void draw(const DrawArgs& args) override {
				if (box.size.x <= 0.f || box.size.y <= 0.f)
					return;
				constexpr float kScale = 0.70f;
				float cx = box.size.x * 0.5f;
				float cy = box.size.y * 0.5f;
				nvgSave(args.vg);
				nvgTranslate(args.vg, cx, cy);
				nvgScale(args.vg, kScale, kScale);
				nvgTranslate(args.vg, -cx, -cy);
				componentlibrary::Trimpot::draw(args);
				nvgRestore(args.vg);
			}
		};

		// Reuse the Phaseon coordinate system so Phaseon1.png lines up reliably.
		const float panelW = 101.6f;

#ifndef METAMODULE
		// Minimalith/Phaseon-style labels
		const NVGcolor colFM      = nvgRGB( 80, 255, 120); // FM
		const NVGcolor colFilter  = nvgRGB(255,  90,  90); // Filter
		const NVGcolor colFormant = nvgRGB(230, 230, 230); // Formant
		const NVGcolor colEnv     = nvgRGB(120, 170, 255); // Envelope
		const NVGcolor colMotion  = nvgRGB(255, 120, 210); // Thickening / motion
		const NVGcolor colDrive   = nvgRGB(255, 170,  70); // Drive / warmth / crush / volume
		const NVGcolor colSync    = nvgRGB(255, 230,  90); // LFO / sync / clock
		const NVGcolor colWT      = nvgRGB(255, 255, 255); // Wavetable
		const NVGcolor colMisc    = nvgRGB(220, 220, 220); // Misc faceplate text
		const Vec textShiftPx = Vec(0.0f, 7.0f);
		struct Phaseon1CenteredLabel : TransparentWidget {
			std::string text;
			float fontSize;
			NVGcolor color;
			Phaseon1CenteredLabel(Vec centerPos, Vec size, const char* txt, float fs, NVGcolor col) {
				box.size = size;
				box.pos = centerPos.minus(size.mult(0.5f));
				text = txt ? txt : "";
				fontSize = fs;
				color = col;
			}
			void draw(const DrawArgs& args) override {
				std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
				std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
				if (!font) return;
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, fontSize);
				nvgFillColor(args.vg, color);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, box.size.x * 0.5f, fontSize * 0.5f, text.c_str(), NULL);
			}
		};

		// Preset name text: draw over the faceplate (screen art is on the PNG).
		{
			Vec dispPos = mm2px(Vec(6.0f, 17.0f)).plus(textShiftPx).plus(Vec(0.0f, -15.0f));
			Vec dispSize = mm2px(Vec(panelW - 12.0f, 8.0f));
			float fullW = dispSize.x;
			float newW = fullW * 0.70f;
			dispPos.x += (fullW - newW) * 0.5f;
			dispSize.x = newW;
			struct Phaseon1Display : TransparentWidget {
				Phaseon1* module = nullptr;
				bool drawBackground = false;
				Phaseon1Display(Vec pos, Vec size, bool drawBg) {
					box.pos = pos;
					box.size = size;
					drawBackground = drawBg;
				}
				void draw(const DrawArgs& args) override {
					if (drawBackground) {
						nvgBeginPath(args.vg);
						nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.0f);
						nvgFillColor(args.vg, nvgRGB(15, 15, 25));
						nvgFill(args.vg);
						nvgStrokeColor(args.vg, nvgRGB(167, 255, 196));
						nvgStrokeWidth(args.vg, 0.8f);
						nvgStroke(args.vg);
					}
					std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf"));
					if (!font) return;
					nvgFontFaceId(args.vg, font->handle);
					// Draw number prefix (e.g. "01/127") at half font size, then name at full size.
					const char* fullText = "Phaseon1";
					if (module) fullText = module->displayTextCstr();
					// Split on first space after the slash (format: "NN/127 Name")
					std::string full(fullText);
					std::string prefix, namepart;
					auto sp = full.find(' ');
					if (sp != std::string::npos) {
						prefix = full.substr(0, sp);
						namepart = full.substr(sp + 1);
					} else {
						namepart = full;
					}
					const float fullFontSize = 12.0f * 1.30f * 1.35f;
					const float prefixFontSize = fullFontSize * 0.375f;
					const float cy = box.size.y * 0.5f - 2.0f;
					const float cx = box.size.x * 0.5f;
					nvgFillColor(args.vg, nvgRGB(220, 240, 220));
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					if (!prefix.empty()) {
						// prefix: top-left, small
						nvgFontSize(args.vg, prefixFontSize);
						nvgFillColor(args.vg, nvgRGB(160, 200, 160));
						nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
						nvgText(args.vg, -7.0f, cy - fullFontSize * 0.28f, prefix.c_str(), NULL);
					}
					// name: centred, full size
					nvgFontSize(args.vg, fullFontSize);
					nvgFillColor(args.vg, nvgRGB(220, 240, 220));
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgText(args.vg, cx, cy + prefixFontSize * 0.3f - 4.0f, namepart.c_str(), NULL);
				}
			};
			auto* disp = new Phaseon1Display(dispPos, dispSize, false);
			disp->module = module;
			addChild(disp);
		}

		// Preset controls: index trimpot + SAVE button under the screen.
		{
			const float ctrlY = 28.0f;
			// Raise controls a bit more so knob and SAVE sit closer to the screen.
			const Vec presetShiftPx = Vec(0.0f, -41.0f);
			const Vec presetRightShiftPx = Vec(14.0f, 0.0f);
			// Move SAVE + LFO cluster down by 60px from previous layout, +40px for new COLOR knob.
			const Vec presetDownShiftPx = Vec(0.0f, 35.0f + 60.0f + 40.0f);
			const Vec presetClusterShiftPx = presetShiftPx.plus(presetDownShiftPx);
			// Place SAVE + LFO to the right of the screen (see faceplate art).
			const float presetX = panelW - 14.0f;
			const float randX = presetX - 4.2f;
			const float saveX = presetX + 4.0f;
			// Small SAVE button just below the preset trimpot
			const float saveY = ctrlY + 8.0f;
			addParam(createParamCentered<TL1105>(mm2px(Vec(randX, saveY)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(0.f, 9.f)), module, Phaseon1::RANDOMIZE_BUTTON_PARAM));
			const Vec randLabelSize = Vec(40.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(randX, saveY - 5.0f)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(0.f, 9.f)).plus(Vec(0.f, 6.f)), randLabelSize, "RAND", 3.72f, colMisc));
			addParam(createParamCentered<TL1105>(mm2px(Vec(saveX, saveY)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(0.f, 9.f)), module, Phaseon1::PRESET_SAVE_PARAM));
			const Vec saveLabelSize = Vec(40.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(saveX, saveY - 5.0f)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(0.f, 9.f)).plus(Vec(0.f, 6.f)), saveLabelSize, "SAVE", 3.72f, colMisc));
			// LFO on/off switch under SAVE
			const float lfoY = saveY + 8.0f;
			addParam(createParamCentered<Phaseon1FoldSwitch>(mm2px(Vec(presetX, lfoY)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(-1.f, 0.f)).plus(Vec(0.f, 9.f)), module, Phaseon1::LFO_ENABLE_PARAM));
			const Vec lfoLabelSize = Vec(40.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(presetX, lfoY - 5.0f)).plus(textShiftPx).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(-1.f, 0.f)).plus(Vec(0.f, 9.f)).plus(Vec(0.f, 6.f)).plus(Vec(1.f, 0.f)), lfoLabelSize, "LFO", 7.0f, colMisc));
		}

#else
		// MetaModule preset name text display (rendered above the faceplate).
		{
			struct Phaseon1MMDisplay : MetaModule::VCVTextDisplay {};
			auto* mmDisplay = new Phaseon1MMDisplay();
			mmDisplay->box.pos = mm2px(Vec(10.0f, 16.0f)).plus(Vec(20.0f, -7.0f));
			mmDisplay->box.size = mm2px(Vec(panelW - 20.0f, 12.0f));
			mmDisplay->font = "Default_14";
			mmDisplay->color = Colors565::Green;
			mmDisplay->firstLightId = Phaseon1::PRESET_DISPLAY;
			addChild(mmDisplay);
		}

		// MetaModule preset index control indicator (same position as Rack preset knob).
		{
			const float ctrlY = 28.0f;
			const Vec presetShiftPx = Vec(0.0f, -41.0f);
			const Vec presetRightShiftPx = Vec(7.0f, 0.0f);
			// Move LFO switch down by 60px (relative to previous layout).
			const Vec presetDownShiftPx = Vec(0.0f, 35.0f + 60.0f);
			const Vec presetClusterShiftPx = presetShiftPx.plus(presetDownShiftPx);
			const float presetX = panelW - 14.0f;
			const float randX = presetX - 4.2f;
			// LFO on/off switch under SAVE (MetaModule)
			const float saveY = ctrlY + 8.0f;
			const float lfoY = saveY + 8.0f;
			addParam(createParamCentered<TL1105>(mm2px(Vec(randX, saveY)).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(0.f, 9.f)), module, Phaseon1::RANDOMIZE_BUTTON_PARAM));
			addParam(createParamCentered<Phaseon1FoldSwitch>(mm2px(Vec(presetX, lfoY)).plus(presetClusterShiftPx).plus(presetRightShiftPx).plus(Vec(-1.f, 0.f)).plus(Vec(0.f, 9.f)), module, Phaseon1::LFO_ENABLE_PARAM));
		}
#endif

		// 6-column centered grid. Last row is modulation trimpots directly below target knobs.
		const float panelCenter = panelW * 0.5f;
		const float colSpacing = 13.32f;   // 12mm + 5px ≈ 35px → 40px centre-to-centre
		const float mcol1 = panelCenter - 2.5f * colSpacing;
		const float mcol2 = panelCenter - 1.5f * colSpacing;
		const float mcol3 = panelCenter - 0.5f * colSpacing;
		const float mcol4 = panelCenter + 0.5f * colSpacing;
		const float mcol5 = panelCenter + 1.5f * colSpacing;
		// mcol6 removed: SYNC DIV knob replaced by CV input; grid is now 5 columns.
		const Vec macroShiftPx = Vec(20.0f, 12.0f);  // moved up by additional 20px
		float macroY = 26.0f;
		float macroSpacing = 9.8f;
		const float kRowSpacing = macroSpacing + 0.80f; // knob rows: +3px gap each
		const float tearWarpKnobY = macroY + kRowSpacing + 9.3f;

		// Right-side PRESET + VOL knobs, aligned with EDIT/SUB rows.
		// Use the same knob widget/size as EDIT.
		{
			const float rightX = panelW - 14.0f;
			const Vec rightInShiftPx = Vec(-9.f, 0.f);
			addParam(createParamCentered<Phaseon1EditKnob>(mm2px(Vec(rightX, macroY + 0.5f)).plus(macroShiftPx).plus(rightInShiftPx).plus(Vec(0.f, -4.f)), module, Phaseon1::PRESET_INDEX_PARAM));
			addParam(createParamCentered<Phaseon1EditKnob>(mm2px(Vec(rightX, macroY + kRowSpacing + 0.5f)).plus(macroShiftPx).plus(rightInShiftPx).plus(Vec(0.f, -4.f)).plus(Vec(0.f, 20.f)), module, Phaseon1::VOLUME_PARAM));
			addParam(createParamCentered<Phaseon1EditKnob>(mm2px(Vec(rightX, tearWarpKnobY)).plus(macroShiftPx).plus(rightInShiftPx).plus(Vec(0.f, -4.f)).plus(Vec(0.f, 42.f)), module, Phaseon1::WARP_PARAM));
#ifndef METAMODULE
			{
				const Vec labelSize = Vec(64.0f, 10.0f);
				addChild(new Phaseon1CenteredLabel(mm2px(Vec(rightX, macroY - 6.5f)).plus(macroShiftPx).plus(rightInShiftPx).plus(textShiftPx).plus(Vec(0.f, -6.f)), labelSize, "PRESET", 6.2f, colMisc));
				addChild(new Phaseon1CenteredLabel(mm2px(Vec(rightX, macroY + kRowSpacing - 6.5f)).plus(macroShiftPx).plus(rightInShiftPx).plus(textShiftPx).plus(Vec(0.f, 9.f)).plus(Vec(0.f, 5.f)), labelSize, "VOL", 6.2f, colMisc));
				addChild(new Phaseon1CenteredLabel(mm2px(Vec(rightX, macroY + 2.f * kRowSpacing - 6.5f)).plus(macroShiftPx).plus(rightInShiftPx).plus(textShiftPx).plus(Vec(0.f, 9.f)).plus(Vec(0.f, 24.f)), labelSize, "WARP", 6.2f, colMisc));
			}
#endif
		}

		const Vec editPos = mm2px(Vec(mcol1 - colSpacing * 1.02f, macroY + 0.5f)).plus(macroShiftPx).plus(Vec(0.f, -4.f));
		const Vec subPos = mm2px(Vec(mcol1 - colSpacing * 1.02f, macroY + kRowSpacing + 0.5f)).plus(macroShiftPx).plus(Vec(0.f, -4.f)).plus(Vec(0.f, 12.f));
		const Vec tearBase = mm2px(Vec(mcol1 - colSpacing * 1.02f, tearWarpKnobY)).plus(macroShiftPx).plus(Vec(0.f, -4.f)).plus(Vec(0.f, 12.f)).plus(Vec(0.f, 14.f));
		const float utilityStepY = tearBase.y - subPos.y;
		const Vec wtFormPos = tearBase.plus(Vec(0.f, utilityStepY));
#ifndef METAMODULE
		const Vec utilityLabelOffset = Vec(0.f, -19.f);
#endif

		// EDIT knob above WAVE (mcol1), red accent knob.
		addParam(createParamCentered<Phaseon1EditKnob>(editPos, module, Phaseon1::EDIT_OP_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(54.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(editPos.plus(textShiftPx).plus(utilityLabelOffset), labelSize, "EDIT", 6.2f, colMisc));
		}
#endif

		// SUB knob under EDIT: black knob, same size as EDIT.
		addParam(createParamCentered<Phaseon1SubKnob>(subPos, module, Phaseon1::SUB_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(54.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(subPos.plus(textShiftPx).plus(utilityLabelOffset), labelSize, "SUB", 6.2f, colMisc));
		}
#endif

		// TEAR knob.
		addParam(createParamCentered<Phaseon1TearKnob>(tearBase, module, Phaseon1::TEAR_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(54.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(tearBase.plus(textShiftPx).plus(utilityLabelOffset), labelSize, "TEAR", 6.2f, colMisc));
		}
#endif

		// WT FORM knob under TEAR, same size/type as TEAR.
		addParam(createParamCentered<Phaseon1TearKnob>(wtFormPos, module, Phaseon1::WT_FORM_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(54.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(wtFormPos.plus(textShiftPx).plus(utilityLabelOffset), labelSize, "WT FORM", 5.8f, colWT));
		}
#endif

		// TEAR FOLD + WT Scroll toggles.
		const Vec tearFoldPos = wtFormPos.plus(Vec(-11.f, 39.f));
		const Vec wtScrollPos = tearFoldPos.plus(Vec(22.f, 0.f));
		addParam(createParamCentered<Phaseon1FoldSwitch>(tearFoldPos, module, Phaseon1::TEAR_NOISE_PARAM));
		addParam(createParamCentered<Phaseon1FoldSwitch>(wtScrollPos, module, Phaseon1::WT_SCROLL_MODE_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(54.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(tearFoldPos.plus(textShiftPx).plus(Vec(1.f, -17.f)), labelSize, "TEAR FLD", 5.4f, colMisc));
			addChild(new Phaseon1CenteredLabel(wtScrollPos.plus(textShiftPx).plus(Vec(1.f, -17.f)), labelSize, "WT FRM", 5.2f, colMisc));
		}
#endif

		// Row 1
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx), module, Phaseon1::WAVE_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx), module, Phaseon1::ALGO_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx), module, Phaseon1::OP_FREQ_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx), module, Phaseon1::OP_LEVEL_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx), module, Phaseon1::CHAR_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WAVE",   6.6f, colWT));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ALGO",   6.6f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FREQ",   6.6f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "OP.LVL", 6.6f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "CHAR",   6.6f, colFM));
		}
#endif

		// Row 2
		macroY += kRowSpacing;
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx), module, Phaseon1::DENSITY_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx), module, Phaseon1::MORPH_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx), module, Phaseon1::COMPLEX_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx), module, Phaseon1::MOTION_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx), module, Phaseon1::FM_ENV_AMOUNT_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DENSITY", 6.0f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MORPH",   6.4f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "CMPLX",   6.4f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "MOTION",  6.4f, colMotion));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ENV->FM", 6.0f, colFM));
		}
#endif

		// Row 3
		macroY += kRowSpacing;
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx), module, Phaseon1::ATTACK_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx), module, Phaseon1::SYNC_ENV_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx), module, Phaseon1::FILTER_ENV_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx), module, Phaseon1::LFO_GRAVITY_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx), module, Phaseon1::FILTER_RESONANCE_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "ATTACK",   6.0f, colEnv));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "SYNC ENV.F", 5.8f, colEnv));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FILT.ENV", 6.0f, colFilter));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "LFO GRAV", 6.0f, colSync));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "RES",      6.4f, colFilter));
		}
#endif

		// Row 4
		macroY += kRowSpacing;
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx), module, Phaseon1::FILTER_MORPH_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx), module, Phaseon1::COLOR_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx), module, Phaseon1::BITCRUSH_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx), module, Phaseon1::WARMTH_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx), module, Phaseon1::FILTER_DRIVE_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FLT MRPH",  5.8f, colFilter));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "COLOR",     6.0f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "CRUSH",     6.4f, colDrive));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WARMTH",    6.2f, colDrive));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DRIVE",     6.4f, colDrive));
		}
#endif

		// Row 5: target knobs for modulation row below
		macroY += kRowSpacing;
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx), module, Phaseon1::TIMBRE_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx), module, Phaseon1::FORMANT_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx), module, Phaseon1::EDGE_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx), module, Phaseon1::DECAY_PARAM));
		addParam(createParamCentered<Phaseon1MainKnob>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx), module, Phaseon1::FILTER_CUTOFF_PARAM));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT FRAME", 6.0f, colWT));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FORMANT",  6.0f, colFormant));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FB SRC",   6.0f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DECAY",    6.0f, colEnv));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.5f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "CUTOFF",   6.0f, colFilter));
		}
#endif

		// Row 6: trimpots (last row), aligned with target knobs above
		macroY += macroSpacing;
		const Vec trimpotShift = Vec(0.f, -3.f);
		addParam(createParamCentered<Phaseon1Trimpot>(mm2px(Vec(mcol1, macroY)).plus(macroShiftPx).plus(trimpotShift), module, Phaseon1::WT_FRAME_MOD_TRIMPOT));
		addParam(createParamCentered<Phaseon1Trimpot>(mm2px(Vec(mcol2, macroY)).plus(macroShiftPx).plus(trimpotShift), module, Phaseon1::VOWEL_MOD_TRIMPOT));
		addParam(createParamCentered<Phaseon1Trimpot>(mm2px(Vec(mcol3, macroY)).plus(macroShiftPx).plus(trimpotShift), module, Phaseon1::OP1_FB_MOD_TRIMPOT));
		addParam(createParamCentered<Phaseon1Trimpot>(mm2px(Vec(mcol4, macroY)).plus(macroShiftPx).plus(trimpotShift), module, Phaseon1::DECAY_MOD_TRIMPOT));
		addParam(createParamCentered<Phaseon1Trimpot>(mm2px(Vec(mcol5, macroY)).plus(macroShiftPx).plus(trimpotShift), module, Phaseon1::CUTOFF_MOD_TRIMPOT));
#ifndef METAMODULE
		{
			const Vec labelSize = Vec(64.0f, 10.0f);
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol1, macroY - 6.0f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "WT MOD",   5.9f, colWT));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol2, macroY - 6.0f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FORM MOD", 5.9f, colFormant));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol3, macroY - 6.0f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "FB MOD",   5.9f, colFM));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol4, macroY - 6.0f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "DEC MOD",  5.9f, colEnv));
			addChild(new Phaseon1CenteredLabel(mm2px(Vec(mcol5, macroY - 6.0f)).plus(macroShiftPx).plus(textShiftPx), labelSize, "CUT MOD",  5.9f, colFilter));
		}
#endif

		// Ports: bottom row grid (8 columns, xL=10mm to xR=91.6mm).
		// Three rows: CV-A (y=95), CV-B (y=106), Main (y=116). 11mm vertical gap ≈ 32px.
		// Labels (Rack only) sit 5mm above each port center so text clears port circles.
		{
			const float xL = 10.0f;
			const float xR = panelW - 10.0f;
			const float dxRaw = (xR - xL) / 8.0f;
			const float pxPerMmX = mm2px(Vec(1.f, 0.f)).x;
			const float shrinkMm = (pxPerMmX > 0.f) ? (4.f / pxPerMmX) : 0.f;
			const float dx = std::max(0.1f, dxRaw - shrinkMm);
			const float centerX = 0.5f * (xL + xR);
			const float xStart = centerX - 0.5f * (dx * 8.0f);
			const Vec rowShift = Vec(0.f, -3.f);
			auto xAt  = [&](int col) { return xStart + dx * (float)col; };
			auto portAt = [&](int col, float ymm) {
				return mm2px(Vec(xAt(col), ymm)).plus(rowShift);
			};

			const float rowA = 99.65f;
			const float pxPerMmY = mm2px(Vec(0.f, 1.f)).y;
			const float rowStepMm = (pxPerMmY > 0.f) ? (30.f / pxPerMmY) : 0.f;  // 30px vertical spacing
			const float rowB = rowA + rowStepMm;
			const float rowM = rowB + rowStepMm;

			// ── CV Row A ──────────────────────────────────────────────────────────
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(0, rowA), module, Phaseon1::PRESET_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(1, rowA), module, Phaseon1::WARP_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(2, rowA), module, Phaseon1::MORPH_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(3, rowA), module, Phaseon1::COMPLEX_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(4, rowA), module, Phaseon1::MOTION_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(5, rowA), module, Phaseon1::ATTACK_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(6, rowA), module, Phaseon1::DECAY_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(7, rowA), module, Phaseon1::BITCRUSH_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(8, rowA), module, Phaseon1::WTFRAME_CV_INPUT));

			// ── CV Row B ──────────────────────────────────────────────────────────
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(0, rowB), module, Phaseon1::CHARACTER_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(1, rowB), module, Phaseon1::CUTOFF_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(2, rowB), module, Phaseon1::RESONANCE_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(3, rowB), module, Phaseon1::DRIVE_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(4, rowB), module, Phaseon1::FORMANT_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(5, rowB), module, Phaseon1::LFO_GRAVITY_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(6, rowB), module, Phaseon1::ALL_FREQ_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(7, rowB), module, Phaseon1::OP1_LEVEL_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(8, rowB), module, Phaseon1::EDGE_CV_INPUT));

			// ── Main row ──────────────────────────────────────────────────────────
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(0, rowM), module, Phaseon1::COLOR_CV_INPUT));
			addOutput(createOutputCentered<Phaseon1OutPortWidget>(portAt(1, rowM), module, Phaseon1::LFO_OUTPUT));
			addOutput(createOutputCentered<Phaseon1OutPortWidget>(portAt(2, rowM), module, Phaseon1::ENV_OUTPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(3, rowM), module, Phaseon1::SYNC_DIV_CV_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(4, rowM), module, Phaseon1::CLOCK_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(5, rowM), module, Phaseon1::GATE_INPUT));
			addInput(createInputCentered<Phaseon1PortWidget>(portAt(6, rowM), module, Phaseon1::VOCT_INPUT));
			addOutput(createOutputCentered<Phaseon1OutPortWidget>(portAt(7, rowM), module, Phaseon1::LEFT_OUTPUT));
			addOutput(createOutputCentered<Phaseon1OutPortWidget>(portAt(8, rowM), module, Phaseon1::RIGHT_OUTPUT));
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(xAt(5) + 5.f, rowM - 5.f)).plus(rowShift), module, Phaseon1::GATE_LIGHT));

#ifndef METAMODULE
			// Port labels — 5mm above each port center, small font.
			const float lf = 5.6f;  // label font size
			const Vec lsz = Vec(52.f, 10.f);

			// CV Row A labels
			const float la = rowA - 4.0f;  // was -5mm, +4px lower
			addChild(new Phaseon1CenteredLabel(portAt(0, la), lsz, "PRESET",   lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(1, la), lsz, "WARP",     lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(2, la), lsz, "MORPH",    lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(3, la), lsz, "CMPLX",    lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(4, la), lsz, "MOTION",   lf, colMotion));
			addChild(new Phaseon1CenteredLabel(portAt(5, la), lsz, "ATK",      lf, colEnv));
			addChild(new Phaseon1CenteredLabel(portAt(6, la), lsz, "DEC",      lf, colEnv));
			addChild(new Phaseon1CenteredLabel(portAt(7, la), lsz, "CRUSH",    lf, colDrive));
			addChild(new Phaseon1CenteredLabel(portAt(8, la), lsz, "WT FRAME", lf, colWT));

			// CV Row B labels
			const float lb = rowB - 4.0f;  // was -5mm, +4px lower
			addChild(new Phaseon1CenteredLabel(portAt(0, lb), lsz, "CHAR",      lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(1, lb), lsz, "CUT",       lf, colFilter));
			addChild(new Phaseon1CenteredLabel(portAt(2, lb), lsz, "RES",       lf, colFilter));
			addChild(new Phaseon1CenteredLabel(portAt(3, lb), lsz, "DRIVE",     lf, colDrive));
			addChild(new Phaseon1CenteredLabel(portAt(4, lb), lsz, "FORMANT",   lf, colFormant));
			addChild(new Phaseon1CenteredLabel(portAt(5, lb), lsz, "LFO GRAV",  lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(6, lb), lsz, "ALL FREQ",  lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(7, lb), lsz, "OP1 LEVEL", lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(8, lb), lsz, "EDGE",      lf, colDrive));

			// Main row labels
			const float lm = rowM - 4.0f;  // was -5mm, +4px lower
			addChild(new Phaseon1CenteredLabel(portAt(0, lm), lsz, "COLOR",    lf, colMotion));
			addChild(new Phaseon1CenteredLabel(portAt(1, lm), lsz, "LFO>",     lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(2, lm), lsz, "ENV>",     lf, colEnv));
			addChild(new Phaseon1CenteredLabel(portAt(3, lm), lsz, "LFO SYNC", lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(4, lm), lsz, "CLK",      lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(5, lm), lsz, "GATE",     lf, colSync));
			addChild(new Phaseon1CenteredLabel(portAt(6, lm), lsz, "V/OCT",    lf, colFM));
			addChild(new Phaseon1CenteredLabel(portAt(7, lm), lsz, "L>",       lf, colMisc));
			addChild(new Phaseon1CenteredLabel(portAt(8, lm), lsz, "R>",       lf, colMisc));
#endif
		}
	}

	void appendContextMenu(Menu* menu) override {
		Phaseon1* module = dynamic_cast<Phaseon1*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);

#ifndef METAMODULE
		menu->addChild(createMenuLabel("Preset:"));

		struct RevertPresetItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				module->revertCurrentPreset();
			}
		};
		auto* revertItem = new RevertPresetItem();
		revertItem->text = "Revert current preset";
		revertItem->module = module;
		menu->addChild(revertItem);

		struct InitPresetItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				module->initializeCurrentPresetSlot();
			}
		};
		auto* initItem = new InitPresetItem();
		initItem->text = "Initialize current preset slot (overwrite)";
		initItem->module = module;
		menu->addChild(initItem);

		menu->addChild(new MenuSeparator);

		struct LoadBankItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				std::string dir = system::getDirectory(module->bankPath());
				osdialog_filters* filters = osdialog_filters_parse("Phaseon1 Bank:bnk");
				char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? NULL : dir.c_str(), NULL, filters);
				osdialog_filters_free(filters);
				if (!path) return;
				module->bankLoadSelectedPath(std::string(path));
				free(path);
			}
		};
		auto* loadBankItem = new LoadBankItem();
		loadBankItem->text = "Load bank...";
		loadBankItem->module = module;
		menu->addChild(loadBankItem);

		struct SaveBankItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				module->bankSave();
			}
		};
		auto* saveBankItem = new SaveBankItem();
		saveBankItem->text = "Save bank";
		saveBankItem->module = module;
		menu->addChild(saveBankItem);

		struct SaveBankAsItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				std::string dir = system::getDirectory(module->bankPath());
				if (!dir.empty()) system::createDirectories(dir);
				osdialog_filters* filters = osdialog_filters_parse("Phaseon1 Bank:bnk");
				char* path = osdialog_file(OSDIALOG_SAVE, dir.empty() ? NULL : dir.c_str(), NULL, filters);
				osdialog_filters_free(filters);
				if (!path) return;
				std::string p = path;
				free(path);
				if (system::getExtension(p).empty()) {
					p += ".bnk";
				}
				if (module->bankSaveToPath(p)) {
					module->bankFilePath = p;
				}
			}
		};
		auto* saveBankAsItem = new SaveBankAsItem();
		saveBankAsItem->text = "Save bank as...";
		saveBankAsItem->module = module;
		menu->addChild(saveBankAsItem);

		menu->addChild(new MenuSeparator);

		struct CopyPresetItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				module->copyCurrentPresetToClipboard();
			}
		};
		auto* copyItem = new CopyPresetItem();
		copyItem->text = "Copy current preset";
		copyItem->module = module;
		menu->addChild(copyItem);

		struct PastePresetItem : MenuItem {
			Phaseon1* module = nullptr;
			void onAction(const event::Action& e) override {
				if (!module) return;
				module->pasteClipboardToCurrentPreset();
			}
		};
		auto* pasteItem = new PastePresetItem();
		pasteItem->text = "Paste into current slot (overwrite)";
		pasteItem->module = module;
		pasteItem->disabled = !Phaseon1::sPresetClipboardValid;
		menu->addChild(pasteItem);

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Rename preset (Enter to confirm):"));
		struct Phaseon1RenameField : ui::TextField {
			Phaseon1* module = nullptr;
			Phaseon1RenameField() {
				box.size.x = 220.f;
			}
			void onSelectKey(const SelectKeyEvent& e) override {
				if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER) {
					if (module) {
						module->renameCurrentPreset(getText());
					}
					MenuOverlay* overlay = getAncestorOfType<MenuOverlay>();
					if (overlay) overlay->requestDelete();
					e.consume(this);
					return;
				}
				ui::TextField::onSelectKey(e);
			}
		};
		auto* renameField = new Phaseon1RenameField;
		renameField->module = module;
		renameField->setText(module->presetName);
		renameField->selectAll();
		menu->addChild(renameField);

		menu->addChild(new MenuSeparator);
#endif

		struct LoadItem : MenuItem {
			Phaseon1* module;
			void onAction(const event::Action& e) override {
				if (!module) return;

#ifdef METAMODULE
				// MetaModule: async file browser (runs in GUI context)
				std::string startDir = module->defaultWtBrowserDir();
				async_open_file(startDir.c_str(), "wav,WAV", "Load wavetable",
					[module = module](char* path) {
						if (path) {
							std::string err;
							module->loadWavetableFile(path, &err);
							free(path);
							MetaModule::Patch::mark_patch_modified();
						}
					}
				);
#else
				osdialog_filters* filters = osdialog_filters_parse("WAV:wav");
				char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
				osdialog_filters_free(filters);
				if (path) {
					std::string err;
					module->loadWavetableFile(path, &err);
					free(path);
				}
#endif
			}
		};

		auto* item = new LoadItem();
		item->text = "Load wavetable (.wav)…";
		item->module = module;
		menu->addChild(item);
	}
};

Model* modelPhaseon1 = createModel<Phaseon1, Phaseon1Widget>("Phaseon1");
