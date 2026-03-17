# Phaseon1 MetaModule Stability Handoff

This document is a mandatory handoff for any new planner/agent touching Phaseon1 DSP, bank loading, or preset behavior.

## Non-negotiable objective

Keep Phaseon1 stable on MetaModule with **no silent-latch failures** (no "click then silence" states).

Historically, Phaseon1 could enter a dead-audio state when combinations of controls (formant/filter/FM/morph/algo/wave/env links) pushed DSP into unstable numeric regions. The core fix was not one patch; it was a safety architecture.

## Root-cause pattern (historical)

- Certain parameter combinations created out-of-range intermediate values.
- One stage produced non-finite values (NaN/Inf) or unstable recursive state.
- Invalid state propagated into integrators/feedback loops.
- Result: click + persistent silence until preset reload/reset.

MetaModule was more sensitive due to ARM/toolchain/real-time constraints.

## Mandatory safety architecture

### 1) Preset/Bank safety

- Bank path handling must be deterministic and consistent.
- Preserve legacy filename/path fallback where required for compatibility.
- Do **not** auto-overwrite a real user bank on transient load failure.
- On preset load/apply:
  - sanitize all params (finite, clamped to legal ranges),
  - reset DSP runtime state to prevent latching a bad previous state.

### 2) Operator/FM safety

- Keep FM/feedback ranges bounded; no unbounded index growth.
- Guard carrier FM index so the sub/fundamental remains stable.
- Non-finite operator state/output must trigger local recovery reset.

### 3) Filter safety

- Avoid Nyquist-edge instability in dynamic cutoff mapping.
- Validate coefficients/state and recover to safe defaults if invalid.
- Avoid harsh always-on clipping that destroys bass.

### 4) Formant safety

- Formant path must remain numerically stable under full knob/CV modulation.
- Preserve finite guards and recovery behavior.
- If using parallel peaks, normalize summed energy before mix.
- Avoid hot pre-filter gain spikes that poison downstream stages.

### 5) Control-rate discipline

- Keep sub-block parameter update cadence (8-sample acceptable).
- Smooth fast macro links to avoid stepping/zipper behavior.
- All macro links must be bounded and stability-aware.

## Implementation policy

- Do **not** remove safety guards for convenience.
- Do **not** introduce unbounded modulation paths.
- For tone shaping, adjust constants first before architecture changes.
- Any edits touching filter/formant/FM are high-risk and require full build validation.

## Required validation

Run full pipeline after high-risk edits:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-all.ps1 -Jobs 8`

Success means:

- no MetaModule silent-latch under aggressive sweeps/preset switching,
- bass fundamental preserved,
- formant/filter expressive without digital collapse,
- bank/preset behavior deterministic and non-destructive.

## Agent bootstrap prompt

Use this at session start:

> You are working on MorphWorx Phaseon1. Keep MetaModule stable at all times. Preserve preset/bank safety, finite guards, bounded FM/filter/formant modulation, and DSP recovery behavior. Do not remove stability protections. Treat filter/formant/FM changes as high risk, validate with full build-all pipeline, and prioritize avoiding silent-latch states (click then silence).