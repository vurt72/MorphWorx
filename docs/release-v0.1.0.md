# MorphWorx v0.1.0

VCV Rack 2 and 4ms MetaModule plugin · Initial release.

## What's Included

- **Phaseon1** — 4-operator PM/FM synthesizer with user-loadable wavetable, formant shaper, morphing filter, kinetic LFO, 127-slot preset bank. Full manual below.
- **Trigonomicon** — Generative breakcore/IDM/drill drum trigger pattern generator.
- **Septagon** — Polyrhythmic drum patterns in 7/4 with phase-space warping.
- **Xenostasis** — Self-regulating spectral drone with wavetable + bytebeat cross-modulation.
- **SlideWyrm** — TB-303 style acid pattern generator.
- **Minimalith** — PreenFM2 FM engine with bank loading.
- **Amenolith** — 5-instrument multisample drum kit player.

---

## Installation

### VCV Rack 2 (Windows x64)
1. Download `MorphWorx-0.1.0-win-x64.vcvplugin` from the assets below.
2. Drop it into `%LOCALAPPDATA%\Rack2\plugins-win-x64\` and restart Rack.

The Phaseon1 preset bank (`Phbank.bnk`) and default wavetable (`phaseon1.wav`) are bundled inside the plugin and load automatically.

### 4ms MetaModule
1. Install `MorphWorx.mmplugin` via the MetaModule web UI.
2. Download `MorphWorx-phaseon1-sdcard.zip`, extract it, and copy the `phaseon1/` folder to the **root of your SD card**:
   - `sdc:/phaseon1/Phbank.bnk` — factory preset bank (auto-loads)
   - `sdc:/phaseon1/phaseon1.wav` — default wavetable (auto-loads)
   - `sdc:/phaseon1/xs_*.wav` — optional extra wavetables

---

---

# Phaseon1 — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#overview)
2. [Signal Flow at a Glance](#signal-flow-at-a-glance)
3. [Connections](#connections)
4. [FM / PM — How It Works](#fm--pm--how-it-works)
5. [Macro Controls](#macro-controls)
6. [Per-Operator Editing (EDIT OP)](#per-operator-editing-edit-op)
7. [Algorithms](#algorithms)
8. [Character — Ratio Sets](#character--ratio-sets)
9. [Wavetables](#wavetables)
10. [WT FORM — Macro Timbral Modes](#wt-form--macro-timbral-modes)
11. [Envelope (Attack / Decay)](#envelope-attack--decay)
12. [The Filter](#the-filter)
13. [LFO — Internal, LFO OUT, ENV OUT](#lfo--internal-lfo-out-env-out)
14. [Tear FX](#tear-fx)
15. [Bitcrush](#bitcrush)
16. [Sub Oscillator](#sub-oscillator)
17. [Formant Shaper](#formant-shaper)
18. [Presets — Loading and Saving](#presets--loading-and-saving)
19. [Randomize](#randomize)
20. [CV Reference](#cv-reference)
21. [Trimpots](#trimpots)
22. [Tips and Workflows](#tips-and-workflows)

---

## Overview

**Phaseon1** is a four-operator **Phase Modulation (PM) synthesizer** with a user-loadable wavetable per operator, a dual-formant vowel shaper, a morphing multimode filter with envelope follower, a clock-synced kinetic LFO, bitcrush/sample-rate reducer, sub oscillator, and a 127-slot preset bank — all in one module.

It is deliberately *macro-orientated*: rather than exposing individual operator parameters directly, most controls shape multiple internal parameters at once. This makes it fast to play but sophisticated to explore.

> **PM vs FM:** Phaseon1 uses phase modulation — one operator shifts the phase of another rather than driving it through a VCA. Sonically and architecturally identical to classic DX7-style FM. The words FM and PM are used interchangeably throughout this manual.

---

## Signal Flow at a Glance

```
V/OCT ──────────────────────────────────────────────────────┐
GATE  ──► AR Envelope ──► VCA (master)                      │
CLOCK ──► LFO Clock Tracker                                  │
                                                             ▼
                            ┌──────────────────────────────────────┐
                            │  4 × FM/PM Operators                │
                            │  (ALGO selects routing topology)    │
                            │  WT or built-in waveform per op     │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  Sub oscillator (sine, -1 oct)      │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  DubstepSVF (LP→BP→HP morph filter) │
                            │  Drive · Resonance · Env Follower   │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  FormantShaper (dual-peak BP)       │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  Tear FX (comb · fold · LFSR noise) │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  RetroCrusher (bit/SR reduction)    │
                            └──────────────┬───────────────────────┘
                                           │
                            ┌──────────────▼───────────────────────┐
                            │  VCA · DC blocker · final tanh sat  │
                            └──────────────┬───────────────────────┘
                                           │
                                 L OUT / R OUT
```

**LFO OUT** and **ENV OUT** are available as independent patch points at all times.

---

## Connections

### Inputs

| Jack | Range | Description |
|---|---|---|
| **GATE** | 0–10 V | Note-on / note-off. Rising edge resets the AR envelope and LFO phase. |
| **V/OCT** | ±5 V (0 V = C4) | 1 V/octave pitch tracking for the whole voice. |
| **CLOCK** | gate/trigger | External clock for LFO rate. The ClockTracker measures BPM from pulse intervals and applies the SYNC DIV division. |
| **FORMANT CV** | 0–10V / ±5V | Adds to the Formant knob. |
| **WARP CV** | 0–10V / ±5V | Adds to the WARP knob. |
| **MORPH CV** | 0–10V / ±5V | Adds to MORPH. |
| **COMPLEX CV** | 0–10V / ±5V | Adds to COMPLEX. |
| **MOTION CV** | 0–10V / ±5V | Adds to MOTION. |
| **ATTACK CV** | 0–10V / ±5V | Adds to envelope Attack. |
| **DECAY CV** | 0–10V / ±5V | Adds to envelope Decay. |
| **BITCRUSH CV** | 0–10V / ±5V | Adds to Bitcrush depth. |
| **WT FRAME CV** | 0–10V / ±5V | Adds to wavetable frame position (TIMBRE). |
| **CUTOFF CV** | 0–10V / ±5V | Adds to Filter Cutoff. |
| **RESONANCE CV** | 0–10V / ±5V | Adds to Filter Resonance. |
| **DRIVE CV** | 0–10V / ±5V | Adds to Filter Drive. |
| **LFO GRAV CV** | 0–10V / ±5V | Adds to LFO Gravity. |
| **ALL FREQ CV** | ±5V | Shifts all four operator frequencies by ±24 semitones simultaneously. |
| **OP1 LEVEL CV** | ±5V | Offsets Operator 1's level by ±1.0 (bipolar). |
| **CHARACTER CV** | 0–10V | Adds to CHARACTER. Sweeps ratio sets. |
| **SYNC DIV CV** | 0–10V | Selects LFO clock division in 12 snapping steps. Overrides knob when connected. |
| **PRESET CV** | 0–10V | Selects preset slot 1–127. |
| **COLOR CV** | 0–10V / ±5V | Adds to the COLOR macro. |
| **EDGE CV** | ±5V / 0–10V | Adds to EDGE (operator feedback drive). |

### Outputs

| Jack | Range | Description |
|---|---|---|
| **L OUT** | ±10 V | Left audio output. |
| **R OUT** | ±10 V | Right audio output. |
| **LFO OUT** | ±5 V | Internal LFO signal for external patching. Shape controlled by LFO GRAV. |
| **ENV OUT** | 0–10 V | Envelope follower from the post-filter audio. Tracks playing dynamics. |

---

## FM / PM — How It Works

Phaseon1 contains **four operators** (OP1–OP4). Each operator is a band-limited oscillator that can either output audio directly (a *carrier*) or add phase to another operator's accumulator (a *modulator*). Which operator does which is set by the **ALGO** knob.

### Modulation Equation

When operator B modulates operator A at FM index *I*:

```
output_A = sin(phase_A + I × output_B)
```

- Low *I* → nearly sinusoidal output.
- As *I* grows → sidebands appear at `fa ± n × fb`, creating complex, buzzy, metallic, or formant-rich spectra.
- Sideband character depends on the **ratio** of OP-B frequency to OP-A frequency, set by CHARACTER and per-operator FREQ offsets.

### Key Macro Relationships

| Macro | What It Actually Does |
|---|---|
| **DENSITY** | FM index (*I*). Left = clean carriers. Right = dense spectral clutter up to metallic/noise. |
| **EDGE** | Operator self-feedback. Adds grit and harmonic edge. Interacts with WARMTH for saturation colour. |
| **MORPH** | Scales FM index an additional 0–90%, giving a second dimension of spectral openness alongside DENSITY. |
| **COMPLEX** | Adds per-operator phase distortion (PD) warping and waveshaping. |
| **COLOR** | Spreads upper operator ratios into slight inharmonicity. With COMPLEX: deepens PD depth and drive. In WT mode, inactive on ratios. |
| **WARMTH** | Saturation character of operator self-feedback. Low = gentle harmonics. High = warm distorted growl. |
| **FM ENV AMT** | How much the AR envelope drives FM index over time. High values → bright attack bloom that decays to knob-set depth. |
| **SYNC ENV.F** | How much the AR envelope drives carrier frequency sync. Adds a detuned pitch transient on attack. |

### Operator Frequency Ratios

Each operator's frequency is a **ratio** relative to the V/OCT fundamental, further multiplied by the per-operator **OP FREQ** semitone offset.

Example: fundamental A3 (220 Hz), CHARACTER ratio for OP2 = 2.0, OP FREQ = +7 semitones (factor ≈ 1.498) → `220 × 2.0 × 1.498 = 659 Hz`.

Integer or simple-fraction ratios = harmonic/musical results. Irrational ratios = bell, metallic, or clangorous timbres.

---

## Macro Controls

### Top Row

| Knob | Default | Description |
|---|---|---|
| **DENSITY** | 0.25 | FM index macro. Left = clean, right = dense spectral clutter. |
| **TIMBRE** | 0.50 | Wavetable frame position (WT operators). In pure FM mode: additive FM index offset across the spectrum. |
| **EDGE** | 0.25 | Operator feedback level. Adds harmonically rich grit. Interacts with WARMTH. |
| **ALGO** | 0 | Selects the modulation routing topology (0 = Stack … 13 = FB Ladder). |
| **CHARACTER** | 0 | Selects / blends between 8 frequency ratio sets. |

### Performance Row

| Knob | Default | Description |
|---|---|---|
| **MOTION** | 0.26 | Stereo width and unison detune. Zero = mono; increasing = subtle stereo spread. Higher values add a second voice per operator with slight detuning. |
| **MORPH** | 0.52 | Morphs carrier/modulator FM index relationship. Right = more open. |
| **COMPLEX** | 0.00 | Phase distortion depth and waveshaping per operator. |
| **COLOR** | 0.00 | Spectral character. Inharmonic ratio spread in FM mode; enhances PD when combined with COMPLEX. |
| **VOLUME** | 0.50 | Master output. 0.5 = unity. 1.0 ≈ +12 dB. |

### Refinement Row

| Knob | Default | Description |
|---|---|---|
| **FM ENV AMT** | 0.65 | AR envelope → FM index modulation depth. |
| **WARMTH** | 0.45 | Feedback saturation character. |
| **SYNC ENV.F** | 0.40 | AR envelope → carrier frequency sync amount. |
| **WARP** | 0.00 | Phase-warp bend applied to all operators. |

---

## Per-Operator Editing (EDIT OP)

Three knobs share one panel position but apply independently to each of the four operators:

| Knob | Description |
|---|---|
| **EDIT OP** | Selects which operator is being edited (1–4). Switching jumps WAVE, OP FREQ, and OP LEVEL to that operator's stored values. |
| **WAVE** | Waveform for the selected operator. Snaps to 8 options: **WT**, **Sine**, **Fat Sub-Tri**, **Sharktooth**, **Pulse-Sine**, **Folded Sine**, **Oct Sub-Growl**, **Sync Saw**. |
| **OP FREQ** | Semitone transposition −24 to +24 (integer steps). Adjusts the operator's ratio within the active CHARACTER set. |
| **OP LEVEL** | Output level for the selected operator (0–1). |

> **Important:** Always select the operator first with EDIT OP, then adjust its parameters. The display knobs jump to show the target operator's current stored values when EDIT OP changes.

Values for all four operators are stored independently and saved with every preset.

---

## Algorithms

**ALGO** snaps to 14 routing topologies.

| # | Name | Routing | Character |
|---|---|---|---|
| 0 | **Stack** | 4→3→2→1→out | Serial FM chain. Maximum modulation depth. One carrier, three modulators. |
| 1 | **Pairs** | (4→3)+(2→1)→out | Two independent FM pairs mixed. Thicker, less extreme than Stack. |
| 2 | **Swarm** | (4+3+2)→1→out | Three modulators all into one carrier. Very dense sidebands. |
| 3 | **Wide** | All 4 to output | Additive — all operators go directly to audio. No inter-modulation. Ultra-thick unison. |
| 4 | **Cascade** | Branching tree | OP4 feeds OP3 and OP2; each continues to output. Complex, differently-voiced pairs. |
| 5 | **Fork** | Split routing | One modulator drives two carriers plus a separate pair. Hybrid. |
| 6 | **Anchor** | Asymmetric | Fixed anchor operator before the carrier. Different harmonic balance to Stack. |
| 7 | **Pyramid** | Three-level tree | Three modulators cascade to one carrier. Extreme depth, different shaping from Stack. |
| 8 | **Triple** | Triple modulator | Cascade variant with three separate paths. |
| 9 | **Dual Cas** | Two 2-op stacks | Two independent 2-op chains. Cleaner but still lively. |
| 10 | **Ring** | Feedback ring + stack | Ring of three operators creates chaotic self-modulating texture; separate clean stack outputs too. |
| 11 | **D.Mod** | Cross-modulation | 6→1, 5→2, 4→3. Asymmetric, harsh. Great for industrial. |
| 12 | **M.Bus** | Multi-bus | Hybrid routing with a shared modulation bus. |
| 13 | **FB Ladd** | Feedback ladder | Cascading feedback chain. Dramatic pitch and spectral sweeps. |

**MORPH** additionally scales how strongly modulators interact with carriers across all algorithms.

---

## Character — Ratio Sets

**CHARACTER** (0–7, snapping) selects and smoothly morphs between eight curated frequency ratio families.

| Position | Ratios (OP1–4) | Sound Character |
|---|---|---|
| 0 | 1, 2, 3, 4 | Classic harmonic-series FM. Reedy, brass-like. |
| 1 | 1, 1.5, 2, 3 | Warm, even. Pads and basses. |
| 2 | 1, 1.333, 2, 4 | 4th/5th flavour; slightly nasal. |
| 3 | 0.5, 1, 2, 5 | Sub fundamental prominent. Kicks and basses. |
| 4 | 1, 0.5, 1.5, 2 | Thick low end with a half-speed component. |
| 5 | 1, 1.25, 1.5, 2 | Dense but harmonic. Mid-range leads. |
| 6 | 1, 1.1, 1.2, 1.3 | Near-unison. Natural beating / chorus texture. |
| 7 | 0.5, 1.333, 2.667, 5 | Wide octave + stretch. Large spectral range. |

Between integer positions ratios are linearly interpolated — sweeping CHARACTER with a CV gives a continuous evolution through ratio space.

---

## Wavetables

### Default Wavetable

Phaseon1 ships with a factory wavetable (`phaseon1.wav`) that loads automatically. It contains multiple frames of a bass/lead waveform ideal for dubstep growls and morphing basses.

### Loading a Custom Wavetable

- **VCV Rack:** Right-click the module → **Load Wavetable…**
- **MetaModule:** Place `.wav` files in `sdc:/phaseon1/` and browse from the module.

### Wavetable Format

- Standard 16-bit or 32-bit float WAV, mono.
- Single-cycle or multi-frame (concatenated single-cycle waveforms).
- Frame sizes: 256, 512, 1024, or 2048 samples.
- Multi-frame wavetables are downsampled to 4 × 256-sample frames internally.
- Serum-format wavetables (with `clm ` marker chunks) are supported.

When a wavetable is loaded, operators with **WAVE = WT** use it; other WAVE settings use built-in waveforms. You can mix WT and non-WT operators freely.

### TIMBRE — Wavetable Frame Position

**TIMBRE** (and **WT FRAME CV**) controls which frame is played. Frames are linearly interpolated. Sweep this knob or automate it for smooth timbral morphing.

**WT SCROLL mode** (switch):
- **Smooth** — continuous interpolation between frames.
- **Stepped** — snaps to individual frames for crisp discrete timbre jumps.

---

## WT FORM — Macro Timbral Modes

**WT FORM** activates pre-tuned macro timbral recipes. Most dramatic with WT operators active.

| Setting | Name | Effect |
|---|---|---|
| **Off** | Normal | Standard FM+WT operation. |
| **Growl** | Growl | Biases wavetable to low frames, increases feedback saturation and phase distortion, boosts formant content. Optimised for classic dubstep growl basses. |
| **Yoi** | Yoi | Biases to mid frames, adds strong formant vowel movement, increases phase distortion, links SYNC ENV.F more aggressively. Creates "yoi yoi yoi" vowel-sweep growl. |
| **Tear** | Tear | Forces stepped WT scroll, biases to low frames, applies asymmetric phase distortion per operator, increases TEAR FX. Aggressive harmonic shredding for heavy dubstep. |

---

## Envelope (Attack / Decay)

Phaseon1 uses a simple **AR envelope** as its amplitude envelope.

| Control | Description |
|---|---|
| **ATTACK** | Attack time. 0 = nearly instant; 1 = slow over several seconds. |
| **DECAY** | Decay / release time. 0 = instant cut; 1 = long release. |

Both have CV inputs (5 V = full sweep, additive). Beyond VCA, the envelope is also routed to FM index depth (via **FM ENV AMT**) and to carrier frequency sync (via **SYNC ENV.F**).

---

## The Filter

Phaseon1's filter is a **Topology Preserving Transform (TPT) zero-delay-feedback State-Variable Filter (SVF)** with tanh-based nonlinear saturation.

| Control | Description |
|---|---|
| **CUTOFF** | Cutoff frequency. Maps exponentially from ~20 Hz to near-Nyquist. |
| **RESONANCE** | Q from ~0.5 to ~15. Prominent resonant peak, won't self-oscillate. |
| **DRIVE** | Pre-filter tanh saturation. At full drive the filter is notably coloured and aggressive. |
| **MORPH** | Filter type: 0 = Lowpass, 0.5 = Bandpass, 1.0 = Highpass. Continuously variable. |
| **ENV LINK** | Links internal envelope follower to cutoff. 0 = no tracking; 1 = strong auto-filter. |

The internal envelope follower also feeds the **ENV OUT** jack (0–10 V) and slightly modulates wavetable frame position. Attack ~0.8 ms, release ~28 ms.

---

## LFO — Internal, LFO OUT, ENV OUT

### Internal LFO

A single-cycle sinusoidal LFO with gravity-driven waveshaping.

| Control | Description |
|---|---|
| **LFO SYNC DIV** | LFO rate relative to CLOCK. 12 steps: 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/64. Without CLOCK patched the LFO runs at an estimated rate. |
| **LFO GRAV** | Waveform shape. 0 = pure sine. Higher values compress peaks and steepen zero-crossings (bouncy, stepped feel via tanh shaping). |
| **LFO ENABLE** | Toggle. Off = LFO frozen at zero across all mod paths. |

**Gate reset:** Every rising GATE edge resets LFO phase to 0 for synchronized, deterministic wobble starts.

### LFO OUT

Emits the shaped LFO at ±5 V. Use as a general-purpose CV. Patch it back into any of Phaseon1's own CV inputs for self-modulation.

### ENV OUT

Emits the filter's envelope follower at 0–10 V. Tracks playing dynamics. Useful for:
- External auto-filter effects
- Feedback into CUTOFF CV, DECAY CV, FORMANT CV, etc.

---

## Tear FX

Multi-stage spectral shredding applied to mid/high frequencies only (sub content is preserved).

| Control | Description |
|---|---|
| **TEAR** knob | 0 = off, 1 = maximum shred. Smoothly engaged — no clicks. |
| **TEAR FOLD** toggle | Adds wavefold + LFSR noise layer on top of the comb effect. Full TEAR + FOLD = very noisy and agitated. |

The comb delay is short (256 samples max), creating a gritty, serrated texture at the high end without destroying bass weight.

---

## Bitcrush

Combined **sample-rate reducer and bit-crusher**:

- Low values: mild sample-rate reduction, subtle lo-fi grit.
- Mid values: sample-rate and bit depth both reduce. Obvious digital aliasing.
- High values (>0.65): a wavefold layer blends in before quantization. Extreme lo-fi.

Maps from 16-bit/full-rate (0) to 3-bit/very reduced sample rate (1). CV input is additive (5 V = full sweep).

---

## Sub Oscillator

**SUB** mixes a pure sine wave one octave below the fundamental into the output. It is independent of the FM routing — added after the operator engine. Use it to extend bass weight on mid-frequency patches without affecting the FM timbre.

---

## Formant Shaper

Dual high-Q bandpass filter (TPT SVF) adding vowel/speech character.

| Control | Description |
|---|---|
| **FORMANT** knob | 0 = bypassed; 1 = full vowel character. |
| **FORMANT CV** | Additive CV. |

Two parallel BP peaks with Q 3.5–6.5 morph between a lower (350–600 Hz) and upper (750–1150 Hz) pair. Dry signal is preserved with slight ducking at full wet. With WT FORM **Growl** or **Yoi** the formant shaper receives additional macro boosting.

> **Tip:** Patch LFO OUT into FORMANT CV with the VOWEL MOD trimpot for automatic LFO-driven vowel movement — the classic dubstep wobble.

---

## Presets — Loading and Saving

Phaseon1 stores up to **127 preset slots** in a bank file (`Phbank.bnk`). A factory bank ships bundled with the plugin.

### Navigation

- **PRESET INDEX** knob — scrolls through slots 1–127. Display shows `01/127 Name`.
- **PRESET CV** — 0–10 V selects slots 1–127.

Patches change immediately on navigation with a brief mute crossfade to avoid clicks.

### Saving a Preset

1. Dial in your sound.
2. Navigate to the desired slot with PRESET INDEX.
3. Right-click → **Save Preset**, or press the **SAVE** button.

All current parameter values, including per-operator WAVE, FREQ, LEVEL, are written and saved to disk automatically.

### Additional Preset Operations (right-click menu)

- **Rename Preset** — up to 28 characters.
- **Copy Preset / Paste Preset** — duplicate a preset to any other slot.
- **Initialize Preset Slot** — reset the slot to factory defaults.
- **Load Bank** — swap the entire 127-slot bank from a `.bnk` file.

### Bank File Locations

| Platform | Path |
|---|---|
| VCV Rack (Windows) | `%APPDATA%\Rack2\MorphWorx\phaseon1\Phbank.bnk` |
| VCV Rack (macOS) | `~/Library/Application Support/Rack2/MorphWorx/phaseon1/Phbank.bnk` |
| VCV Rack (Linux) | `~/.Rack2/MorphWorx/phaseon1/Phbank.bnk` |
| 4ms MetaModule | `sdc:/phaseon1/Phbank.bnk` |

---

## Randomize

The **RANDOMIZE** button generates a musically-informed random patch:

- Algorithm, Character, WT FORM randomized freely.
- Filter cutoff, resonance, and decay nudged ±10–25% from current positions (not reset entirely).
- Per-operator WAVE, FREQ, LEVEL randomized using curated musical semitone pools:
  - **OP1** (carrier): root octave offsets only (−24, −12, 0, +12).
  - **OP2**: consonant intervals (±octave, ±fifth, unison).
  - **OP3–4** (modulators): wider intervals including thirds, fourths, sevenths.
- A deterministic seed is saved so the exact variant is captured when you save a preset.

---

## CV Reference

All CV inputs: **5 V = full parameter sweep** (additive with knob, clamped to valid range). Bipolar inputs accept ±5 V.

| Input | Notes |
|---|---|
| **V/OCT** | 1 V/octave, 0 V = C4 (261.63 Hz). |
| **GATE** | High >1.5 V. Triggers AR env and resets LFO phase on rising edge. |
| **CLOCK** | Any gate/trigger. BPM measured from successive pulse intervals. |
| **ALL FREQ CV** | ±5 V = ±24 semitones across all four operators simultaneously. |
| **OP1 LEVEL CV** | ±5 V = ±1.0 level offset for OP1 only (bipolar). |
| **SYNC DIV CV** | 0–10 V → 12 LFO sync steps. Overrides knob when patched. |
| **PRESET CV** | 0–10 V → preset slots 1–127. |

---

## Trimpots

Trimpots set the **depth of LFO → destination** modulation (−1 to +1; 0 = LFO has no effect).

| Trimpot | Destination | Notes |
|---|---|---|
| **WT FRAME MOD** | Wavetable frame position (TIMBRE) | Positive = LFO sweeps frames upward on positive phase. |
| **VOWEL MOD** | Formant shaper amount | Set to ~0.5 with LFO for automatic vowel motion. |
| **FB SRC** | Operator 1 feedback depth | Rhythmic FM distortion wobble synchronized to LFO. |
| **DECAY MOD** | Envelope decay time | Rhythmic amplitude gating. |
| **CUTOFF MOD** | Filter cutoff | Smooth or snappy filter sweeps depending on LFO GRAV. |

---

## Tips and Workflows

### Classic Dubstep Growl Bass

1. Load the default `phaseon1.wav` wavetable (auto-loads).
2. Set WT FORM = **Growl**.
3. ALGO = **Stack** or **Swarm**, CHARACTER = **0 or 1**.
4. DENSITY ≈ 0.6, EDGE ≈ 0.4, WARMTH ≈ 0.7, MORPH ≈ 0.5.
5. Patch CLOCK from your sequencer. Set LFO SYNC DIV to 1/8 or 1/16.
6. VOWEL MOD trimpot ≈ 0.4–0.6. WT FRAME MOD ≈ 0.3.
7. FORMANT ≈ 0.3. FILTER CUTOFF ≈ 0.55–0.65, RESONANCE ≈ 0.4.
8. Hold a low gate and enjoy the wobble locked to your clock tempo.

### Metallic / Industrial Lead

1. All four operators: WAVE = **Sine**.
2. CHARACTER = **6–7**, DENSITY ≈ 0.7, EDGE ≈ 0.5, COMPLEX ≈ 0.4, COLOR ≈ 0.3.
3. ALGO = **Ring** or **D.Mod**.
4. Short ATTACK, medium DECAY. Filter at moderate cutoff with some DRIVE.

### Smooth FM Pad

1. ALGO = **Pairs** or **Wide**, CHARACTER = **1 or 2**, DENSITY ≈ 0.2–0.4.
2. MOTION ≈ 0.4–0.6 for stereo spread. Long ATTACK and DECAY. Filter fully open.
3. COMPLEX = 0, COLOR = 0. WARMTH ≈ 0.4 for slight warmth.

### Layering WT and FM Operators

Set OP1 and OP2 to **WAVE = WT**, set OP3/OP4 to **Sine** or **Fat Sub-Tri**. In **Pairs** algorithm: the WT pair outputs directly while the sine pair handles independent FM. Sweep TIMBRE to morph the WT pair while the sine pair stays fixed.

### Envelope-Following Filter

Set FILTER ENV LINK to 0.5–0.8 and FM ENV AMT to 0.5–0.7. The filter opens automatically on attack. Patch ENV OUT to external destinations for multi-module envelope following.

---

*Phaseon1 — MorphWorx. Copyright © 2026 Bemushroomed. Licensed under GPL-3.0-or-later.*
