# SlideWyrm — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Attribution and Origins](#2-attribution-and-origins)
3. [Changes and Additions](#3-changes-and-additions)
4. [Signal Flow](#4-signal-flow)
5. [Controls](#5-controls)
6. [Inputs](#6-inputs)
7. [Outputs](#7-outputs)
8. [Indicator Lights](#8-indicator-lights)
9. [Pattern Generation](#9-pattern-generation)
10. [Density](#10-density)
11. [Slide (Portamento)](#11-slide-portamento)
12. [Gate Modes](#12-gate-modes)
13. [Accent Envelope](#13-accent-envelope)
14. [Seed and Regeneration](#14-seed-and-regeneration)
15. [Scale Reference](#15-scale-reference)
16. [Tips and Workflows](#16-tips-and-workflows)

---

## 1. Overview

**SlideWyrm** is a **12 HP generative TB-303 style acid pattern sequencer** that algorithmically creates note sequences with slides, accents, and octave transpositions. It generates complete acid bassline patterns from a single seed value and advances them on external clock, outputting 1V/oct pitch CV, a gated output with accent-aware voltage levels, and a shaped accent envelope.

Key features:

- Seed-based deterministic pattern generation — same seed always produces the same pattern
- Probabilistic gate, slide, accent, and octave decoration per step
- 31 scales including Western modes, world scales, and exotic tunings
- Adjustable density that simultaneously controls note-on probability and pitch variety
- Exponential portamento (slide) with four depth levels and CV control
- Three accent envelope shapes (Snap, Reverse, Medium) with three trigger modes
- Three gate modes (Off, Cycle, Slide) for articulation control
- Adjustable gate length with CV modulation
- 8, 16, or 32 step patterns
- Seed lock for repeatable performances
- Root note CV input for live key changes
- Display showing current scale, root, and octave

---

## 2. Attribution and Origins

SlideWyrm is a port of the **TB-3PO** Hemisphere applet for **Ornament & Crime**, originally written by **Logarhythm**. The Ornament & Crime platform was created by Patrick Dowling, Max Stadler, and Tim Churches.

The scale tables are derived from **Mutable Instruments Braids** by Emilie Gillet and from the **Ornament & Crime** project.

All original code is used under the **MIT License**. Full license text and attribution details are in [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

---

## 3. Changes and Additions

The MorphWorx port makes the following changes and additions relative to the original TB-3PO applet:

### Architecture Changes

- **Standalone VCV Rack module** — ported from the Hemisphere 64×128 pixel half-screen applet to a full 12 HP module with dedicated knobs, switches, jacks, and display
- **4ms MetaModule support** — cross-compiled for the MetaModule embedded platform with real-time safety constraints
- **PRNG replacement** — the original `std::mt19937` (2.5 KB state) is replaced with a xoshiro128** generator (16 bytes), seeded via SplitMix32 expansion, for MetaModule compatibility and deterministic behavior
- **Control-rate decimation** — parameter reads and light updates run at ÷64 sample rate to reduce CPU on MetaModule ARM targets
- **Transcendental elimination** — per-sample `exp()` calls for slide portamento replaced with precomputed coefficients calculated at clock edges using a fast exp2 polynomial approximant

### New Features

- **Slide Amount knob + CV** — four-level portamento depth (None, Slight, Medium, High) with CV modulation; original TB-3PO had a fixed slide behavior
- **Gate Length knob + CV** — adjustable gate duration (6.3–100 ms) with CV control; original had a fixed gate time
- **Gate Mode switch** — three articulation modes (Off, Cycle, Slide) that extend gate length on specific steps for legato phrasing
- **Accent Envelope output** — dedicated shaped envelope output (0–8 V) with three selectable shapes (Snap, Reverse, Medium) and three trigger cadence modes (Every 4th, Every 8th, Random)
- **Root CV input** — accepts 1V/oct pitch voltage and extracts the pitch class, enabling live key changes from a keyboard or another sequencer
- **Regenerate trigger input** — separate from Reset; regenerates the pattern without resetting the step counter
- **Regen cooldown** — 50 ms debounce on regeneration triggers to prevent CPU spikes from rapid-fire trigger sources
- **LCD display** — shows current scale name, root note, and octave offset
- **Expanded scale bank** — 31 scales (TB-3PO had a subset); includes world scales (Bhairav, Japanese, Arabic, Gypsy, Egyptian, Bali Pelog, Hirajoshi, Iwato, Kumoi, Pelog, Prometheus, Tritone) and the custom 3b7+ triad

### Behavioral Changes

- **Gate output voltage** — accent steps output 5 V, non-accent steps output 3 V (original was binary on/off), enabling accent-aware downstream processing without needing the separate accent output
- **Slide portamento** — uses exponential convergence (SlideWyrm-style) instead of linear interpolation, producing more musical TB-303-like pitch sweeps
- **Amortized regeneration** — pattern regeneration is split across four phases (pitch first half → density first half → pitch second half → density second half), each running on a separate process cycle, to avoid single-frame CPU spikes

---

## 4. Signal Flow

```
SEED ──→ [PRNG (xoshiro128**)] ──→ Pattern Generation
              │                         │
              │           ┌─────────────┼─────────────────┐
              │           │ Per-Step    │   Per-Step       │
              │           │ gates[]     │   notes[]        │
              │           │ slides[]    │   octUps[]       │
              │           │ accents[]   │   octDowns[]     │
              │           └──────┬──────┴────────┬─────────┘
              │                  │               │
CLOCK ──→ [Step Advance] ──→ [Gate Logic]   [Pitch Lookup]
              │                  │               │
              │           ┌──────┴──────┐   ┌────┴────┐
              │           │ Gate Mode   │   │ Scale   │
              │           │  Off/Cycle/ │   │ Root    │
              │           │  Slide      │   │ Octave  │
              │           └──────┬──────┘   │Transpose│
              │                  │          └────┬────┘
              │                  │               │
              │           ┌──────┴──────┐   ┌────┴────────┐
              │           │ GATE output │   │ Slide Engine │
              │           │ 3V / 5V     │   │ (exp portam) │
              │           └─────────────┘   └────┬────────┘
              │                                  │
              │                            PITCH output
              │                             (1V/oct)
              │
              └──→ [Accent Trigger] ──→ [Envelope Shaper]
                                              │
                                        ACCENT output
                                          (0-8V)
```

---

## 5. Controls

### Knobs

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **DENSITY** | 0–14 | 7 | Controls both note-on probability and pitch variety. See [Density](#10-density). |
| **SCALE** | 0–30 (snapped) | 15 (GUNA) | Selects one of 31 musical scales. The display shows the current scale name. |
| **ROOT** | C–B (snapped) | C | Sets the root note (tonic) of the selected scale. Overridden by ROOT CV when patched. |
| **OCTAVE** | −3 to +3 (snapped) | 0 | Global octave offset applied to all notes. |
| **SEED** | 0–65535 | 0 | The seed value for pattern generation. Same seed + same settings = same pattern. |
| **SLIDE AMOUNT** | 0–3 (snapped) | 3 (High) | Portamento depth. 0 = none, 1 = slight (15 ms), 2 = medium (40 ms), 3 = high (80 ms). |
| **GATE LENGTH** | 6.3–100 ms | ~50 ms | Duration of the gate pulse for each step. |

### Switches

| Control | Positions | Default | Description |
|---------|-----------|---------|-------------|
| **LOCK** | Unlocked / Locked | Unlocked | When locked, the seed is preserved across regenerations and resets. When unlocked, each regeneration picks a new random seed. |
| **STEPS** | 8 / 16 / 32 | 16 | Number of steps in the pattern. |
| **GATE** | Off / Cycle / Slide | Off | Gate extension mode. See [Gate Modes](#12-gate-modes). |
| **ACC** (Accent Shape) | Snap / Reverse / Medium | Snap | Selects the envelope shape for the accent output. See [Accent Envelope](#13-accent-envelope). |
| **ACC.STEP** (Accent Mode) | Every 4 / Every 8 / Random | Every 4 | Selects which steps trigger the accent envelope. See [Accent Envelope](#13-accent-envelope). |

### Button

| Control | Description |
|---------|-------------|
| **REGEN** | Momentary button. Regenerates the pattern (and picks a new seed if LOCK is off). Resets the step counter to 0. |

---

## 6. Inputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **CLK** | Clock | Gate/trigger | Main clock input. Each rising edge advances the pattern by one step. |
| **RST** | Reset | Gate/trigger | Resets the step counter to 0 and regenerates the pattern. If LOCK is off, a new seed is chosen. |
| **TRN** | Transpose CV | ±5 V | Added directly to the pitch output voltage. Use for live transposition or pitch modulation. |
| **ROT** | Root CV | 1V/oct | Sets the root note by pitch class. The pitch voltage is converted to a note name (0 V = C, 1/12 V = C#, etc.). Overrides the ROOT knob when patched. |
| **DNS** | Density CV | 0–10 V | Added to the DENSITY knob value (0–10 V maps to 0–15 additional density). Combined value is clamped to 0–14. |
| **REG** | Regen Trigger | Gate/trigger | Regenerates the pattern without resetting the step counter. Useful for live pattern changes while maintaining rhythmic position. |
| **SLIDE CV** | Slide Amount CV | 0–10 V | Added to the SLIDE AMOUNT knob (0–10 V maps to 0–3 additional). |
| **GATE CV** | Gate Length CV | 0–10 V | Added to the GATE LENGTH knob (0–10 V maps to 0–1 additional). |

---

## 7. Outputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **PITCH** | Pitch | 1V/oct | Pitch CV output. Includes scale quantization, root offset, octave offset, transpose CV, and portamento (slide). |
| **GATE** | Gate | 0 / 3 / 5 V | Gate output. **5 V** on accent steps, **3 V** on normal steps, **0 V** when the gate is off. This allows downstream modules to respond to accents even without using the dedicated accent output. |
| **ACC** | Accent | 0–8 V | Shaped accent envelope output. The envelope shape and trigger cadence are set by the ACC and ACC.STEP switches. |

---

## 8. Indicator Lights

Four LEDs show per-step attributes for the current step:

| Light | Label | Description |
|-------|-------|-------------|
| Green | **SLD** | Lit when the current step has a slide (portamento to the next note). |
| Green | **ACC** | Lit when the current step is an accent. |
| Green | **UP** | Lit when the current step has a +1 octave decoration. |
| Green | **DN** | Lit when the current step has a −1 octave decoration. |

---

## 9. Pattern Generation

SlideWyrm generates patterns deterministically from a seed value. The generation process produces five parallel data streams for each step:

| Data | Type | Description |
|------|------|-------------|
| **gates** | Bitmask (32-bit) | Whether each step sounds a note or rests. |
| **notes** | Array (0–N) | Scale degree index for each step. N depends on the scale size and density. |
| **slides** | Bitmask (32-bit) | Whether each step slides (portamento) into the next. |
| **accents** | Bitmask (32-bit) | Whether each step is accented. |
| **octUps / octDowns** | Bitmasks (32-bit) | Per-step ±1 octave transpositions. |

### Generation Rules

- **Gate probability** scales with density. At center density (7), roughly 50% of steps are gated. Moving away from center in either direction increases gate density.
- **Slide probability** is ~18% for a new slide, ~10% for consecutive slides (slides tend to appear in short runs).
- **Accent probability** is ~16% for a new accent, ~7% for consecutive accents.
- **Octave decoration** — ~40% of steps with a new pitch receive an octave shift (50/50 up or down).
- **Pitch repetition** — at low density, notes frequently repeat the previous step's pitch, creating monotone ostinato. At high density, notes span the full scale range.

The pattern is regenerated in four amortized phases to avoid CPU spikes.

---

## 10. Density

The DENSITY knob (0–14, center = 7) has a dual function that mirrors the original TB-3PO behavior:

### Below Center (0–6): Sparse / Repetitive

- Fewer gates (more rests)
- Narrower pitch range (notes cluster near the root)
- Higher pitch repetition probability (ostinato-like)

### Center (7): Balanced

- Moderate gate density (~50%)
- Medium pitch range
- Natural mix of pitch movement and repetition

### Above Center (8–14): Dense / Wide

- More gates (fewer rests)
- Wider pitch range (notes span the full scale)
- Lower pitch repetition probability (more melodic movement)

The density is evaluated at each clock edge, so CV modulation of density creates evolving pattern characteristics over time. When density or scale size changes, the module schedules a pattern regeneration.

---

## 11. Slide (Portamento)

When a step has the slide flag set, the pitch output glides exponentially from the current note to the next note. This emulates the TB-303's characteristic portamento behavior.

The SLIDE AMOUNT knob selects four depth levels:

| Value | Label | Glide Time | Character |
|-------|-------|------------|-----------|
| 0 | None | 0 ms | Instant pitch change — slide flag is ignored |
| 1 | Slight | 15 ms | Quick glide, subtle pitch sweep |
| 2 | Medium | 40 ms | Classic 303 slide feel |
| 3 | High | 80 ms | Long, dramatic pitch sweeps |

The portamento uses exponential convergence (not linear ramp), matching the resonant filter + VCO interaction character of the original TB-303. The convergence coefficient is precomputed at each clock edge for efficiency.

During a slide, the gate remains open regardless of the next step's gate state, creating legato phrasing.

---

## 12. Gate Modes

The GATE switch selects how the gate pulse duration is modified on certain steps:

| Position | Mode | Behavior |
|----------|------|----------|
| Top | **Off** | All gates use the base GATE LENGTH duration. No extensions. |
| Center | **Cycle** | Every 4th step (steps 0, 4, 8, 12...) gets a longer gate (1.5× base length, clamped to 75% of the clock period). Creates a periodic emphasis pattern. |
| Bottom | **Slide** | Steps that are being slid *into* (arrival step of a slide) get a longer gate (1.5× base length, clamped to 90% of the clock period). Creates legato phrasing on slide notes. |

In Cycle and Slide modes, non-extended steps use the same gate length as Off mode. The minimum gate fraction ensures the extension is always audible even when GATE LENGTH is set short.

---

## 13. Accent Envelope

The ACC output provides a shaped envelope triggered on accent steps. Two switches control the behavior:

### Accent Shape (ACC switch)

| Position | Shape | Attack | Decay | Character |
|----------|-------|--------|-------|-----------|
| Top | **Snap** | 1 ms | 20 ms (exponential) | Sharp percussive spike. Good for filter pings and click accents. |
| Center | **Reverse** | 40 ms (linear ramp) | 5 ms (linear drop) | Swelling envelope that peaks and drops sharply. Good for builds and reverse-style effects. |
| Bottom | **Medium** | 3 ms | 60 ms (exponential) | Moderate punch with sustain. Good for VCA drive and general-purpose accent. |

### Accent Trigger Mode (ACC.STEP switch)

| Position | Mode | Description |
|----------|------|-------------|
| Top | **Every 4** | Accent fires on every 4th step (steps 0, 4, 8, 12...), regardless of the pattern's accent flags. Creates a regular rhythmic emphasis. |
| Center | **Every 8** | Accent fires on every 8th step (steps 0, 8, 16, 24...). Half the frequency of Every 4. |
| Bottom | **Random** | Accent fires on steps that have the accent flag set in the generated pattern (~16% of steps, with a tendency for short runs). |

The accent envelope output peaks at 8 V and is suitable for patching to filter cutoff CV, VCA gain, or any modulation destination.

---

## 14. Seed and Regeneration

### Seed

The SEED knob (0–65535) determines the pattern. The same seed, scale, root, octave, density, and step count will always produce the same pattern. This enables:

- **Recall** — note a seed value to return to a favorite pattern later
- **Exploration** — sweep the SEED knob to audition different patterns
- **Reproducibility** — share seed values with other users of the same module

### Lock

When the LOCK switch is **on**, the seed is preserved across regenerations and resets. The pattern always regenerates from the current seed value. This is useful for:

- Changing density or scale while keeping the same melodic skeleton
- Resetting the step counter without changing the pattern

When LOCK is **off** (default), each regeneration (via RST, REGEN button, or REG input) picks a new random seed, producing a completely new pattern.

### Regeneration Trigger

The REG input triggers a pattern regeneration without resetting the step counter. This allows seamless live pattern changes — the new pattern begins from wherever the current step is, rather than jumping back to step 0. A 50 ms cooldown prevents rapid-fire triggers from causing CPU spikes.

---

## 15. Scale Reference

SlideWyrm includes 31 scales. The SCALE knob selects by index, and the display shows the scale name.

| Index | Name | Notes | Intervals (semitones from root) |
|-------|------|-------|---------------------------------|
| 0 | Chromatic | 12 | 0 1 2 3 4 5 6 7 8 9 10 11 |
| 1 | Major | 7 | 0 2 4 5 7 9 11 |
| 2 | Minor | 7 | 0 2 3 5 7 8 10 |
| 3 | Pentatonic Major | 5 | 0 2 4 7 9 |
| 4 | Pentatonic Minor | 5 | 0 3 5 7 10 |
| 5 | Blues | 6 | 0 3 5 6 7 10 |
| 6 | Dorian | 7 | 0 2 3 5 7 9 10 |
| 7 | Phrygian | 7 | 0 1 3 5 7 8 10 |
| 8 | Lydian | 7 | 0 2 4 6 7 9 11 |
| 9 | Mixolydian | 7 | 0 2 4 5 7 9 10 |
| 10 | Locrian | 7 | 0 1 3 5 6 8 10 |
| 11 | Harmonic Minor | 7 | 0 2 3 5 7 8 11 |
| 12 | Melodic Minor | 7 | 0 2 3 5 7 9 11 |
| 13 | Whole Tone | 6 | 0 2 4 6 8 10 |
| 14 | Diminished | 8 | 0 2 3 5 6 8 9 11 |
| 15 | GUNA | 7 | 0 1 4 5 7 8 10 |
| 16 | Bhairav | 7 | 0 1 4 5 7 8 11 |
| 17 | Japanese | 5 | 0 1 5 7 8 |
| 18 | Arabic | 7 | 0 1 4 5 7 8 11 |
| 19 | Spanish | 7 | 0 1 4 5 7 8 10 |
| 20 | Gypsy | 7 | 0 2 3 6 7 8 11 |
| 21 | Egyptian | 5 | 0 2 5 7 10 |
| 22 | Hawaiian | 7 | 0 2 3 5 7 9 11 |
| 23 | Bali Pelog | 5 | 0 1 3 7 8 |
| 24 | Hirajoshi | 5 | 0 2 3 7 8 |
| 25 | Iwato | 5 | 0 1 5 6 10 |
| 26 | Kumoi | 5 | 0 2 3 7 9 |
| 27 | Pelog | 7 | 0 1 3 6 7 8 10 |
| 28 | Prometheus | 6 | 0 2 4 6 9 10 |
| 29 | Tritone | 6 | 0 1 4 6 7 10 |
| 30 | 3b7+ | 4 | 0 4 7 11 |

### Scale Character Notes

- **GUNA** (default) — Indian raga-derived scale with tension between the flat 2nd and major 3rd. Particularly effective for acid lines due to its dark, chromatic quality.
- **Bhairav / Arabic** — identical intervals; the quintessential "Eastern" sound with augmented 2nd intervals.
- **Phrygian / Spanish** — dark, flamenco-influenced modes. Spanish adds the major 3rd for a "Phrygian dominant" character.
- **Hirajoshi / Kumoi** — Japanese pentatonic scales with distinctive minor-second intervals that create haunting, sparse melodies.
- **3b7+** — a 4-note triad (major chord + major 7th). Extremely constrained; produces strong tonal center with minimal dissonance. Good for pad-like bass patterns.
- **Chromatic** — all 12 semitones. Maximum dissonance potential; pairs well with low density for controlled chromaticism.
- **Pentatonic Minor** — the "can't go wrong" scale. Every interval is consonant; good for ambient and melodic acid.

---

## 16. Tips and Workflows

### Classic Acid Bass
Set SCALE to Blues (5) or Pentatonic Minor (4), ROOT to your song key, DENSITY to 9–11, SLIDE AMOUNT to 3 (High). Patch PITCH to a resonant lowpass filter → VCA, GATE to the VCA envelope, and ACC to the filter cutoff CV. The 303 sound emerges naturally.

### Ambient Sequences
Use GUNA (15), Japanese (17), or Hirajoshi (24) scales at low DENSITY (3–5). Set SLIDE AMOUNT to 2 (Medium) and OCTAVE to −1 or −2. The sparse, sliding patterns make excellent generative ambient melodies.

### Locked Exploration
Turn LOCK on and set a SEED value. Now sweep SCALE, ROOT, and DENSITY to hear different interpretations of the same underlying pattern structure. The gate rhythm and accent placement stay constant while the pitch content transforms.

### Live Key Changes
Patch a keyboard or sequencer's pitch output to the ROT (Root CV) input. The pattern's root note follows the incoming pitch class, enabling live key changes. Combine with TRN (Transpose) for octave shifts.

### Polyrhythmic Interlocking
Use two SlideWyrms with different STEPS settings (e.g., 8 and 16, or 16 and 32) clocked from the same source. The different pattern lengths create evolving polyrhythmic interactions.

### Pattern Morphing
Patch a slow LFO to the DNS (Density CV) input. As density sweeps, the pattern regenerates with different gate densities and pitch ranges, creating gradual evolution. With LOCK on, the seed stays constant so the changes feel like variations rather than completely new patterns.

### Accent-Driven Filter
Patch ACC to a filter cutoff with ACC shape set to Snap. Set ACC.STEP to Random for organic accent placement, or Every 4 for a driving, regular pulse. The 8 V peak is enough to drive most filters through their full range with appropriate attenuverter settings.

### Slide-Extended Legato
Set GATE mode to Slide and increase GATE LENGTH. Slide notes will sustain nearly to the next clock edge, creating legato phrases that connect through slides while non-slide notes remain staccato. This mimics the 303's distinctive "long slide, short staccato" articulation.

### Self-Modulating Root
Patch SlideWyrm's own PITCH output back into the ROT input. The module's pitch output at each clock edge becomes the root note for the next step's scale lookup, creating a self-referencing feedback loop where the pattern modulates its own tonality.

### Integration with Trigonomicon
Patch Trigonomicon's bass trigger (BTRIG) into SlideWyrm's CLK input. The acid line locks to the drum pattern's bass rhythm. Use Trigonomicon's BCV as SlideWyrm's TRN input so both modules share the same pitch center.
