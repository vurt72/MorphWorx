# Phaseon1 Architectural Audit — Handoff Document

**Date:** 2026-03-27  
**Status:** Audit complete. Fixes 1.1, 1.3, 1.4, and 3.1 approved. **No edits have been applied yet** — the implementation tool was unavailable in the prior session. This document is the complete brief for the implementation agent.

---

## Files Involved

- `c:\MorphWorx\src\Phaseon1.cpp`
- `c:\MorphWorx\src\phaseon\PhaseonOperator.cpp` — **no changes required** (mipLevel is already consumed correctly inside `Operator::tick()`)

---

## Approved Fixes to Implement

### Fix 3.1 — Mipmap generation (currently dead code)

**Root cause:** `downsampleWavetableSmall()` always calls `dst.mipData.clear()`, so all WT operators use full-resolution tables at every pitch, even when `fundamentalHz * ratio` exceeds half the internal Nyquist. `ops[i].mipLevel` is set to 0 unconditionally per block. The `bandlimitBias` field is computed but never read — it is dead code.

**Edit 3.1-A — `downsampleWavetableSmall()`: generate 2 mip levels instead of clearing them**

Find (exact):
```cpp
	// No mipmaps for Phaseon1 (keep memory and build time minimal)
	dst.mipData.clear();
	return dst;
}
```
Replace with:
```cpp
	// Build 2 mip levels (128 and 64 samples) via 2-tap box-filter decimation.
	// Used by bandlimited wavetable playback at elevated operator pitches.
	{
		constexpr int kMipLevels = 2;
		dst.mipData.resize(kMipLevels);
		int prevSize = kDstSize;
		for (int level = 0; level < kMipLevels; ++level) {
			int nextSize = prevSize / 2;
			const std::vector<float>& prevVec = (level == 0) ? dst.data : dst.mipData[level - 1];
			dst.mipData[level].resize((size_t)nextSize * (size_t)dst.frameCount);
			for (int f = 0; f < dst.frameCount; ++f) {
				const float* src = prevVec.data() + (size_t)f * (size_t)prevSize;
				float* out = dst.mipData[level].data() + (size_t)f * (size_t)nextSize;
				for (int s = 0; s < nextSize; ++s) {
					out[s] = (src[s * 2] + src[s * 2 + 1]) * 0.5f;
				}
			}
			prevSize = nextSize;
		}
	}
	return dst;
}
```

**Edit 3.1-B — Block preamble: add mipLevel selection after the COLOR inharmonicity block**

Find (exact):
```cpp
		if (colorAmt > 0.001f && !anyWT) {
			static constexpr float kInharmOffset[4] = { 0.0f, 0.170f, 0.414f, 0.832f };
			for (int i = 0; i < 4; ++i) {
				ops[i].ratio += colorAmt * kInharmOffset[i] * ops[i].ratio * 0.3f;
				ops[i].cachedPhaseInc = (fundamentalHz * ops[i].ratio) / kInternalRate;
			}
		}

		float opLevelTrim[4];
```
Replace with:
```cpp
		if (colorAmt > 0.001f && !anyWT) {
			static constexpr float kInharmOffset[4] = { 0.0f, 0.170f, 0.414f, 0.832f };
			for (int i = 0; i < 4; ++i) {
				ops[i].ratio += colorAmt * kInharmOffset[i] * ops[i].ratio * 0.3f;
				ops[i].cachedPhaseInc = (fundamentalHz * ops[i].ratio) / kInternalRate;
			}
		}

		// Select bandlimited mip level for WT operators, after all ratio/phaseInc modifications.
		// kInternalRate * 0.25 = 10240 Hz: reference where mip 0 has full harmonic headroom.
		for (int i = 0; i < 4; ++i) {
			if (ops[i].tableIndex == 0) {
				float opFreq = fundamentalHz * ops[i].ratio;
				ops[i].mipLevel = (opFreq > 0.f)
					? (int)std::max(0.f, std::log2f(opFreq / (kInternalRate * 0.25f)))
					: 0;
			}
			// Non-WT operators: mipLevel already defaulted to 0 in block preamble.
		}

		float opLevelTrim[4];
```

---

### Fix 1.3 — Hoist `atk` out of inner loop (it is block-constant)

**Root cause:** `float atk = 0.0010f + (attackBase * attackBase) * 0.2490f;` runs every sample inside the `for (int i = 1; i <= kBlockSize; ++i)` loop. `attackBase` is a `cvSum()` result computed once before the loop and never updated per-sample. It computes identically 16 times per block.

**Edit 1.3-A — Block preamble: add `atk` constant after `attackBase`/`decayBase`**

Find (exact):
```cpp
		// Global Attack/Decay (CPU-friendly)
		float attackBase = cvSum(clamp01(params[ATTACK_PARAM].getValue()),   ATTACK_CV_INPUT);
		float decayBase  = cvSum(clamp01(params[DECAY_PARAM].getValue()),    DECAY_CV_INPUT);

		float formAmt = cvSum(clamp01(params[FORMANT_PARAM].getValue()),     FORMANT_CV_INPUT);
```
Replace with:
```cpp
		// Global Attack/Decay (CPU-friendly)
		float attackBase = cvSum(clamp01(params[ATTACK_PARAM].getValue()),   ATTACK_CV_INPUT);
		float decayBase  = cvSum(clamp01(params[DECAY_PARAM].getValue()),    DECAY_CV_INPUT);
		// atk is block-constant (attackBase not LFO-modulated); hoisted from inner loop.
		const float atk = 0.0010f + (attackBase * attackBase) * 0.2490f;

		float formAmt = cvSum(clamp01(params[FORMANT_PARAM].getValue()),     FORMANT_CV_INPUT);
```

**Edit 1.3-B — Inner loop: remove the now-redundant `atk` declaration**

Find (exact):
```cpp
			float decayNow = applyMod01(decayBase, lfoOut, decayModTrim);
			float atk = 0.0010f + (attackBase * attackBase) * 0.2490f;
			float dec = 0.0050f + (decayNow * decayNow) * 2.4950f;
			ampEnv.setTimes(atk, dec, dt);
```
Replace with:
```cpp
			float decayNow = applyMod01(decayBase, lfoOut, decayModTrim);
			float dec = 0.0050f + (decayNow * decayNow) * 2.4950f;
			ampEnv.setTimes(atk, dec, dt);
```

---

### Fix 1.4 — Remove redundant `ops[0..2].feedback = fb` per-sample assignments

**Root cause:** `fb` is derived purely from block-rate inputs (`edgeBase`, `complexAmt`). Assigning `ops[0].feedback = fb; ops[1].feedback = fb; ops[2].feedback = fb;` 16 times per block is unnecessary. Only `ops[3].feedback = fbNow` is legitimately per-sample (LFO-modulated via `op1FbModTrim`). The three static assignments belong in the block preamble (already performed by `ops[i].feedback = fb` in the operator setup loop).

**Edit 1.4 — Inner loop: remove the three redundant static feedback stores**

Find (exact):
```cpp
			// Keep base feedback on most operators, but drive modulated feedback
			// into Op4 so it cascades through the FM algorithms more audibly.
			ops[0].feedback = fb;
			ops[1].feedback = fb;
			ops[2].feedback = fb;
			ops[3].feedback = fbNow;
```
Replace with:
```cpp
			// Ops 0–2: feedback is block-constant (set in preamble); op 3 is LFO-modulated.
			ops[3].feedback = fbNow;
```

---

### Fix 1.1 — `phaseWarp` cache bust in `Operator::tick()`

**Root cause:** In `wtFormMode == 0`, the per-sample inner loop had an `else { ops[oi].phaseWarp = finalWarp; }` branch that wrote `finalWarp` (a block-constant value) to `ops[oi].phaseWarp` every sample for all 4 operators. Inside `Operator::tick()`, the guard `if (clampedWarp != cachedPhaseWarpInput)` was therefore always triggering because the field changed before the comparison — executing `fastExp2Approx(clampedWarp * 4.0f)` 4× per internal sample when it only needs to run once per block change.

Removing the `else` branch means `wtFormMode == 0` leaves `phaseWarp` untouched per-sample. It is correctly set to `warpMapped` in the block preamble's `ops[i].phaseWarp = warpMapped;` assignment.

**Edit 1.1 — Inner per-operator loop: remove dead `else` branch**

Find (exact):
```cpp
				else if (wtFormMode == 3) {
					fp = clamp01(0.05f * wtFormBoost * tearBoost + fp * (1.0f - 0.22f * wtFormBoost * tearBoost));
					ops[oi].phaseWarp = finalWarp * (1.0f + wtFormBoost * tearBoost * (0.18f * complexAmt + 0.08f * colorAmt));
					ops[oi].tear = clamp01(tearWet1 + wtFormBoost * tearBoost * (0.22f + 0.36f * complexAmt + 0.08f * colorAmt));
				}
				else {
					ops[oi].phaseWarp = finalWarp;
				}
				if (wtScrollStepped && ops[oi].tableIndex == 0) {
```
Replace with:
```cpp
				else if (wtFormMode == 3) {
					fp = clamp01(0.05f * wtFormBoost * tearBoost + fp * (1.0f - 0.22f * wtFormBoost * tearBoost));
					ops[oi].phaseWarp = finalWarp * (1.0f + wtFormBoost * tearBoost * (0.18f * complexAmt + 0.08f * colorAmt));
					ops[oi].tear = clamp01(tearWet1 + wtFormBoost * tearBoost * (0.22f + 0.36f * complexAmt + 0.08f * colorAmt));
				}
				// wtFormMode == 0: phaseWarp already set to warpMapped in block preamble;
				// no per-sample write needed, preserving Operator::tick() warp cache.
				if (wtScrollStepped && ops[oi].tableIndex == 0) {
```

---

## Deferred Fixes (NOT approved — do not implement)

| ID | Finding | Reason deferred |
|---|---|---|
| 1.2 | LFO at full 40960 Hz internal rate | Sound design risk — LFO->filter->formant interaction; needs separate session |
| 2.1 | FormantShaper `float_4` SIMD | Larger structural refactor; separate session |
| 3.4 | DubstepSVF filter coeff steps | Minor zipper, low priority |

---

## Verification after implementation

After applying all six edits, run **MorphWorx: Deploy to Rack2** and verify:
1. No compiler errors or new warnings in `Phaseon1.cpp`
2. In VCV Rack, a loaded WT patch at high pitch (C6+) should be slightly cleaner/less aliased
3. Patches using `wtFormMode == 0` (default) should sound identical to before — the cache fix is transparent
4. All other wt form modes (1/2/3) should be identical — only the `else` fallback (mode 0) was removed

## Audit findings summary (for reference)

See the full audit report in the session where these were generated. The cleared finding:
- **3.3 (DubstepSVF stability)**: CLEARED — `fastTanApprox` denominator-guarded, `dyn` hard-clamped to 0.92, filter numerically stable under all reachable inputs. No action needed.
