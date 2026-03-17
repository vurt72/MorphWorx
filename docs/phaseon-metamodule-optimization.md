# Phaseon MetaModule Optimization — Notes & Plans

> **Important (Phaseon1 handoff):** For current/ongoing MetaModule stability requirements and guardrails for the active Phaseon1 work, read [docs/phaseon1-metamodule-stability-handoff.md](docs/phaseon1-metamodule-stability-handoff.md) first.

**Date:** February 2026
**Status:** Scrapped — Phaseon removed from MetaModule build due to excessive CPU usage.
These notes document what was tried and what could be revisited.

---

## The Problem

Phaseon uses ~70–80% CPU on MetaModule (Cortex-A7 @ 600 MHz, 48 kHz sample rate).
For comparison, Minimalith (PreenFM2 engine, also 6-op FM) uses ~23%.

## Root Cause Analysis

### Why Minimalith (PreenFM2) Is Fast

- **Integer table lookups** — oscillators use a 2048-sample lookup table indexed by a 32-bit integer phase accumulator. No float math in the inner loop.
- **Block-linearized envelopes** — envelope targets are computed once per 32-sample block, then the per-sample tick is just `level += increment` (one add).
- **No transcendentals** — no `sinf`, `expf`, `tanhf`, `powf` anywhere in the per-sample path.
- **Tiny memory footprint** — one shared lookup table for all operators. Fits entirely in L1 cache.

### Why Phaseon Is Expensive

| Per-sample cost item | Operations |
|---|---|
| 7 envelope divisions (`dt / attack`, `dt / decay`, etc.) | 7 float divides |
| 6 × `freq * ratio / sampleRate` | 6 float divides |
| 6 × `phaseon_fast_tanh(feedback * prevOut)` (rational approximation) | 6 × ~15 ALU ops |
| 6 × bilinear wavetable interpolation (4 float loads from 2 frames) | 24 cache-miss-prone loads |
| 6 × phase warp switch-case (BendPlus/Sync/Quantize/Asym/PD) | branches + math |
| 6 × waveshaping (`tanh(sample * drive)`) | 6 × ~15 ALU ops |
| Network bus: sum 6 ops + one-pole filter + `tanh` | ~20 ops |
| Chaos: 6-op slew + retarget timer | ~15 ops |
| GrowlLoop, scramble, motion wobble per operator | ~15 ops × 6 |
| Formant: 6 biquad filters | ~30 ops |
| Bitcrusher: `std::floor` + sample-and-hold | ~10 ops |
| OutputPolish: asymmetric soft clip + transient clipper | ~15 ops |
| **Total estimate** | **~400+ ALU-equivalent ops/sample** |

### Cache Pressure (The Biggest Hidden Cost)

Each wavetable is **2048 samples × 16 frames = 128 KB per table**.
With 6 operators reading from potentially different tables, total working set is ~768 KB.
The Cortex-A7 has a **32 KB L1 data cache**. Every wavetable read is essentially a cache miss.

PreenFM2 uses a single shared 2048-sample sine table (~8 KB) — fits entirely in L1.

---

## Optimizations Attempted

### Round 1: Algorithmic (Minimal Impact)

These changes were applied but did not noticeably reduce CPU:

1. **Block-linearized envelopes** — `updateBlock()` every 16 samples computes envelope target; `tickFast()` = `level += increment` per sample. Eliminates 7 divisions per sample.

2. **Cached `phaseInc`** — precompute `freq * ratio / sampleRate` once per control-rate block instead of per-sample. Eliminates 6 divisions per sample.

3. **MetaModule operator fast path** (`#ifdef METAMODULE`):
   - No unison loop (hardcoded single voice)
   - Inlined `wrap01` (avoids function call)
   - Conditional feedback tanh (skip when `feedback == 0`)

4. **Nearest-frame wavetable lookup** on MetaModule — 2 memory loads instead of 4 (no frame interpolation).

5. **Skip OutputPolish** (saturation + transient clipper) on MetaModule.

6. **Skip formant** when `formantAmount == 0` (hard gate).

7. **`deformLfo` uses `phaseon_fast_sin_01`** instead of `sinf`.

8. **Bitcrusher uses integer round** instead of `std::floor`.

### Round 2: Feature Removal (Still Not Enough)

More aggressive cuts, also applied but still insufficient:

1. **Reduced to 4 operators** (was 6) — 33% less work.
2. **Shrunk wavetables to 256 samples × 4 frames** (was 2048 × 16) — 128× smaller, fits L1.
3. **Disabled NETWORK** (cross-operator coupling bus) entirely.
4. **Disabled CHAOS** (per-operator FM chaos targets + slew) entirely.
5. **Disabled phase warping** (all warp modes: Bend/Sync/Quantize/Asym/PD).
6. **Disabled waveshaping** (per-operator tanh drive).
7. **Disabled GrowlLoop, scramble, motion wobble, macro LFO** per-operator modulation.
8. **Disabled bitcrusher + formant + soft-clip** post-processing.
9. **Disabled carrier-sum soft clip + PUNCH amplitude thump**.

Even with all of the above, CPU remained too high for practical use.

---

## What Would Be Needed to Actually Ship

To get Phaseon to Minimalith-level CPU (~25%), a near-complete rewrite of the inner loop would be required:

### Option A: PreenFM2-Style Architecture
- Replace float wavetable with **integer phase accumulator + integer-indexed sine table**
- All operator math in **fixed-point or integer**
- Envelopes as linear ramps (already done)
- No `tanh`, `sin`, `exp`, `pow` anywhere per-sample
- Estimated effort: **major rewrite** of PhaseonOperator and PhaseonVoice

### Option B: Massive Downsampling
- Run Phaseon at **12 kHz** (4× downsample from 48 kHz)
- Would need anti-alias filtering on output
- Risk: FM artifacts at low sample rates, aliasing
- Estimated effort: **moderate** — wrap tick in decimator

### Option C: Reduce to 2 Operators
- Only 2 ops = a single carrier + modulator
- Combined with all Round 2 cuts, might be viable
- But loses most of what makes Phaseon interesting
- Estimated effort: **small** — just change `kNumOps`

### Option D: Hybrid — Procedural Sine Only
- Skip wavetable entirely, all ops use `phaseon_fast_sin_01` (no memory access)
- Combined with 4 ops + no features = pure FM synth
- Essentially becomes a simpler PreenFM2
- Estimated effort: **small** but questionable value vs just using Minimalith

---

## File Locations

Key files for implementation:

- `src/phaseon/PhaseonVoice.hpp` — main per-sample `tick()`, envelope processing, operator routing
- `src/phaseon/PhaseonOperator.cpp` — operator inner loop (MetaModule fast path `#ifdef METAMODULE`)
- `src/phaseon/PhaseonWavetable.cpp` — wavetable generation (table sizes defined here)
- `src/phaseon/PhaseonMacros.hpp` — `applyMacros()`, `SpectralTilt`, macro state arrays
- `src/phaseon/PhaseonPolish.hpp` — `OutputPolish` post-processing
- `src/phaseon/PhaseonAlgorithm.hpp` — FM algorithm definitions (hardcoded 6-op, `isCarrier[6]`)
- `src/Phaseon.cpp` — module wrapper, process(), preset system
- `metamodule/CMakeLists.txt` — source file list for MetaModule build
- `src/plugin.cpp` / `src/plugin.hpp` — model registration

---

## Current State (Feb 2026)

- Phaseon is **excluded** from the MetaModule build (`CMakeLists.txt`, `plugin.cpp`, `plugin-mm.json`)
- All `#ifdef METAMODULE` optimization code is still in the source files (harmless, not compiled)
- VCV Rack build is **unaffected** — full 6-op Phaseon with all features
- The optimization `#ifdef` blocks can be used as a starting point if revisited
