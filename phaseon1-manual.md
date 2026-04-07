# Phaseon1 — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Signal Flow at a Glance](#2-signal-flow-at-a-glance)
3. [Connections](#3-connections)
4. [FM / PM — How It Works](#4-fm--pm--how-it-works)
5. [Macro Controls](#5-macro-controls)
6. [Per-Operator Editing (EDIT OP)](#6-per-operator-editing-edit-op)
7. [Algorithms](#7-algorithms)
8. [Character — Ratio Sets](#8-character--ratio-sets)
9. [Wavetables](#9-wavetables)
10. [WT FORM — Macro Timbral Modes](#10-wt-form--macro-timbral-modes)
11. [Envelope (Attack / Decay)](#11-envelope-attack--decay)
12. [The Filter](#12-the-filter)
13. [LFO — Internal, LFO OUT, ENV OUT](#13-lfo--internal-lfo-out-env-out)
14. [Tear FX](#14-tear-fx)
15. [Bitcrush](#15-bitcrush)
16. [Sub Oscillator](#16-sub-oscillator)
17. [Formant Shaper](#17-formant-shaper)
18. [Presets — Loading and Saving](#18-presets--loading-and-saving)
19. [Randomize](#19-randomize)
20. [CV Reference](#20-cv-reference)
21. [Trimpots](#21-trimpots)
22. [Installation and File Locations](#22-installation-and-file-locations)
23. [Tips and Workflows](#23-tips-and-workflows)

---

## 1. Overview

**Phaseon1** is a four-operator **Phase Modulation (PM) synthesizer** with a user-loadable wavetable per operator, a dual-formant vowel shaper, a morphing multimode filter with envelope follower, a clock-synced kinetic LFO, bitcrush/sample-rate reducer, sub oscillator, and a 127-slot preset bank — all in one module.

It is deliberately *macro-orientated*: rather than exposing individual operator parameters directly, most controls shape multiple internal parameters at once. This makes it fast to play but sophisticated to explore.

> **PM vs FM:** Phaseon1 uses *phase modulation* — one operator shifts the phase of another rather than driving it through a VCA. Sonically indistinguishable from classic FM synthesis but computationally identical to the DX7 architecture. The words FM and PM are used interchangeably throughout this manual.

---

## 2. Signal Flow at a Glance

```
V/OCT ─────────────────────────────────────────────────────┐
GATE  ──► AR Envelope ──► VCA (master)                     │
CLOCK ──► LFO Clock Tracker                                 │
                                                            ▼
                            ┌─────────────────────────────────────┐
                            │  4 × FM/PM Operators                │
                            │  (ALGO selects routing topology)    │
                            │  WT or built-in waveform per op     │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  Sub oscillator (sine, -1 oct)      │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  DubstepSVF (multimode filter)      │
                            │  LP → BP → HP morph                 │
                            │  Drive · Resonance · Env Follower   │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  FormantShaper (dual-peak BP)       │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  Tear FX (comb · fold · LFSR noise) │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  RetroCrusher (bit/SR reduce)       │
                            └──────────────┬──────────────────────┘
                                           │
                            ┌──────────────▼──────────────────────┐
                            │  VCA · DC blocker · final tanh sat  │
                            └──────────────┬──────────────────────┘
                                           │
                                 L OUT / R OUT
```

**LFO OUT** and **ENV OUT** are available as independent patch points at all times.

---

## 3. Connections

### Inputs

| Jack | Range | Description |
|---|---|---|
| **GATE** | 0–10 V | Note-on / note-off. Rising edge resets the AR envelope and the LFO phase. |
| **V/OCT** | ±5 V (0 V = C4) | 1 V/octave pitch tracking for the whole voice. |
| **CLOCK** | gate/trigger | External clock for LFO rate. The internal ClockTracker measures the BPM from the pulse interval and applies the LFO SYNC DIV division. |
| **FORMANT CV** | 0–10 V / ±5V | Additive modulation for the Formant knob. |
| **WARP CV** | 0–10 V / ±5V | Additive modulation for phase-warp bend. |
| **MORPH CV** | 0–10 V / ±5V | Additive modulation for the MORPH macro. |
| **COMPLEX CV** | 0–10 V / ±5V | Additive modulation for COMPLEX. |
| **MOTION CV** | 0–10 V / ±5V | Additive modulation for MOTION. |
| **ATTACK CV** | 0–10 V / ±5V | Additive modulation for envelope Attack. |
| **DECAY CV** | 0–10 V / ±5V | Additive modulation for envelope Decay. |
| **BITCRUSH CV** | 0–10 V / ±5V | Additive modulation for Bitcrush depth. |
| **WT FRAME CV** | 0–10 V / ±5V | Additive modulation for wavetable frame position (TIMBRE). |
| **CUTOFF CV** | 0–10 V / ±5V | Additive modulation for Filter Cutoff. |
| **RESONANCE CV** | 0–10 V / ±5V | Additive modulation for Filter Resonance. |
| **DRIVE CV** | 0–10 V / ±5V | Additive modulation for Filter Drive. |
| **LFO GRAV CV** | 0–10 V / ±5V | Additive modulation for LFO Gravity (waveform shape). |
| **ALL FREQ CV** | ±5V | Shifts all four operator frequencies by ±24 semitones simultaneously. |
| **OP1 LEVEL CV** | ±5V | Offsets Operator 1's level by ±1.0 (bipolar). |
| **CHARACTER CV** | 0–10V | Additive modulation for CHARACTER. Sweeps ratio sets (0 V = position 0, 10 V = position 7). |
| **SYNC DIV CV** | 0–10V | Selects LFO clock division in 12 snapping steps (overrides knob when connected). |
| **PRESET CV** | 0–10V | Selects preset slot 1–127 (0 V = slot 1, 10 V = slot 127). |
| **COLOR CV** | 0–10V / ±5V | Additive modulation for the COLOR macro. |
| **EDGE CV** | ±5V / 0–10V | Additive modulation for EDGE (operator feedback drive). |

### Outputs

| Jack | Range | Description |
|---|---|---|
| **L OUT** | ±10 V | Left audio output. |
| **R OUT** | ±10 V | Right audio output. |
| **LFO OUT** | ±5 V | Internal LFO signal available as a CV source. Shape set by LFO GRAV. |
| **ENV OUT** | 0–10 V | Envelope follower derived from the post-filter audio signal. Tracks playing dynamics. Useful for patching filter self-modulation or external CV. |

---

## 4. FM / PM — How It Works

Phaseon1 contains **four operators** (OP1–OP4). Each operator is a band-limited oscillator that can either output audio to the final mix (a *carrier*) or add phase to another operator's phase accumulator (a *modulator*). Which operator does which is set by the **ALGO** knob.

### What Does Modulation Do?

When operator B modulates operator A at an amount *I* (FM index):

```
output_A = sin(phase_A + I × output_B)
```

- At low *I* values the spectrum is nearly sinusoidal.
- As *I* grows additional sidebands appear at frequencies of `fa ± n × fb` (where *n* is an integer), creating increasingly complex, buzzy, metallic, or formant-rich spectra.
- The quality of those sidebands depends entirely on the **ratio** of OP freq B to OP freq A (set by CHARACTER and per-operator FREQ).

### Key Macro Relationships

| Macro | Primary Internal Effect |
|---|---|
| **DENSITY** | FM index (*I* above). Low = clean carriers. High = spectral complexity up to metallic/noise. |
| **EDGE** | Operator self-feedback (each operator feeds back into its own modulation input). Creates distortion and edge. Goes from smooth to chaotic, depends on WARMTH for saturation colour. |
| **MORPH** | Scales the FM index an additional 0–90%, giving a second dimension of spectral openness alongside DENSITY. |
| **COMPLEX** | Adds phase-distortion (PD) warping and gentle waveshaping per operator. Layers timbral complexity on top of the FM routing. |
| **COLOR** | At zero: no effect. Increasing: spreads upper operator ratios into slight inharmonicity (golden-ratio-derived offsets), or — in COLOR+COMPLEX together — deepens PD depth and drive. In WT-mode operators, COLOR is inactive on ratios. |
| **WARMTH** | Controls the saturation character of operator self-feedback. Low = gentle overtones. High = warm distorted edges, essential for dubstep-style growls. |
| **FM ENV AMT** | How much the AR envelope drives the FM index over time. High values give a bright attack bloom that decays back to the knob-set FM depth. |
| **SYNC ENV.F** | How much the AR envelope drives the carrier frequency via phase synchronisation. Adds a detuned pitch transient on attack. |

### Operator Frequency Ratios

Each operator's frequency is expressed as a **ratio** relative to the fundamental pitch (set by V/OCT). By default all four operators use the ratios from the selected CHARACTER set, then further multiplied by the per-operator **OP FREQ** semitone offset.

Example: if the fundamental is A3 (220 Hz) and an operator has CHARACTER ratio 2.0 and an OP FREQ offset of +7 semitones (factor ≈ 1.498), its frequency is `220 × 2.0 × 1.498 = 659 Hz`.

**Keeping modulators at integer or simple-fraction ratios produces harmonic (musical) results. Irrational ratios produce bell, metallic, or clangorous timbres.** The CHARACTER knob navigates between curated harmonic and inharmonic ratio families.

---

## 5. Macro Controls

### Top Row

| Knob | Default | Description |
|---|---|---|
| **DENSITY** | 0.25 | FM index macro. Left = clean, right = dense spectral clutter. |
| **TIMBRE** | 0.50 | Wavetable frame position (when any operator is in WT mode). In pure FM (no WT operators), acts as an additive FM offset across the spectrum. |
| **EDGE** | 0.25 | Operator feedback level. Adds harmonically rich grit. Interacts with WARMTH. |
| **ALGO** | 0 | Selects the modulation routing topology (0 = Stack … 13 = FB Ladder). See [Algorithms](#7-algorithms). |
| **CHARACTER** | 0 | Selects / blends between the 8 frequency ratio sets. See [Character — Ratio Sets](#8-character--ratio-sets). |

### Performance Row

| Knob | Default | Description |
|---|---|---|
| **MOTION** | 0.26 | Stereo width and unison detune. At zero: mono. Increasing: subtle stereo spread per operator. Higher values initiate a second voice for each operator with slight detuning. |
| **MORPH** | 0.52 | Morphs the carrier/modulator FM index relationship. Centre = balanced, right = more open/complex. |
| **COMPLEX** | 0.00 | Adds phase distortion depth and wave folding. Works multiplicatively with COLOR. |
| **COLOR** | 0.00 | Spectral character. In FM mode: introduces inharmonic ratio offsets. With COMPLEX: extends PD and waveshaping depth. |
| **VOLUME** | 0.50 | Master output level. 0.5 = unity gain. 1.0 ≈ +12 dB. |

### Refinement Row

| Knob | Default | Description |
|---|---|---|
| **FM ENV AMT** | 0.65 | AR envelope → FM index modulation depth. |
| **WARMTH** | 0.45 | Saturation character of operator feedback. |
| **SYNC ENV.F** | 0.40 | AR envelope → carrier frequency sync amount (pitch transient). |
| **WARP** | 0.00 | Phase-warp bend applied to all operators simultaneously. Low amounts add subtle harmonics; higher amounts create dramatic spectral bending. |

---

## 6. Per-Operator Editing (EDIT OP)

Three knobs share one panel position but apply independently to each of the four operators:

| Knob | Description |
|---|---|
| **EDIT OP** | Selects which operator is being edited (1–4). Switching jumps WAVE, OP FREQ, and OP LEVEL to that operator's stored values. |
| **WAVE** | Sets the waveform for the selected operator. Snaps to 8 choices: **WT** (user wavetable or built-in default), **Sine**, **Fat Sub-Tri**, **Sharktooth**, **Pulse-Sine**, **Folded Sine**, **Oct Sub-Growl**, **Sync Saw**. |
| **OP FREQ** | Semitone transposition for the selected operator from −24 to +24 semitones (in integer steps). Adjusts the operator's ratio within the active CHARACTER set. |
| **OP LEVEL** | Output level for the selected operator (0–1). Scales operator contribution to the FM mix and output. |

> **Important:** The WAVE, OP FREQ, and OP LEVEL knobs are shared display knobs. When you move EDIT OP, the knob values jump to show the target operator's actual stored settings. This is intentional — always select the operator first, then adjust its parameters.

Values for all four operators are stored independently in hidden params and are saved with every preset.

---

## 7. Algorithms

The **ALGO** knob snaps to 14 routing topologies that determine how the four operators connect. The display shows the algorithm name.

| # | Name | Routing Description | Character |
|---|---|---|---|
| 0 | **Stack** | 4→3→2→1→out | Maximum serial FM depth. One carrier, three modulators in a chain. Deepest modulation colour. |
| 1 | **Pairs** | (4→3)+(2→1)→out | Two independent FM pairs. Each pair adds its own character; thicker than Stack with less extreme modulation depth. |
| 2 | **Swarm** | (4+3+2)→1→out | Three modulators all feeding one carrier. Very dense sidebands; useful for aggressive basses and leads. |
| 3 | **Wide** | All 4 to output | Additive synthesis — all operators go directly to audio. Useful for ultra-thick unison pads or inharmonic layering. No inter-modulation. |
| 4 | **Cascade** | Branching tree | Op 4 feeds ops 3 and 2; each branch continues to output. Creates complex but differently-voiced upper/lower pairs. |
| 5 | **Fork** | Split routing | One modulator drives two carriers plus a separate pair. Hybrid between pairs and cascade. |
| 6 | **Anchor** | Asymmetric | One operator modulates through a fixed anchor before the carrier. Different harmonic balance to Stack. |
| 7 | **Pyramid** | Three-level tree | Three modulators cascade to a single carrier in a pyramid. Extreme modulation depth with slightly different spectral shaping than Stack. |
| 8 | **Triple** | Triple modulator | Variant of cascade with three separate paths. |
| 9 | **Dual Cas** | Two 2-op stacks | Two independent 2-op chains to output. Cleaner but still lively. |
| 10 | **Ring** | Feedback ring + stack | Three operators form a feedback modulation ring while another separate 3-op stack outputs cleanly. Chaotic self-modulating texture. |
| 11 | **D.Mod** | Cross-modulation | Each operator modulates a different partner (6→1, 5→2, 4→3). Asymmetric, harsh, great for industrial and noise. |
| 12 | **M.Bus** | Multi-bus | Hybrid routing with a shared modulation bus. |
| 13 | **FB Ladd** | Feedback ladder | Cascading feedback chain. Progressively unstable; dramatic pitch and spectral sweeps. |

**MORPH** additionally scales how strongly modulator operators interact with their carriers, providing a continuous soft fade between less and more FM-heavy character within any algorithm.

---

## 8. Character — Ratio Sets

**CHARACTER** (range 0–7, snapping) selects and smoothly morphs between eight curated frequency ratio families. This is the quickest way to change the musical character of a patch without touching individual operator frequencies.

| Position | Name | Ratios (OP1–4) | Sound Character |
|---|---|---|---|
| 0 | Harmonic | 1, 2, 3, 4 | Classic harmonic-series FM. Reedy, brass-like. |
| 1 | Balanced | 1, 1.5, 2, 3 | Warm, even, works for pads and basses. |
| 2 | Upper Odd | 1, 1.333, 2, 4 | Adds a 4th/5th flavour; slightly nasal. |
| 3 | Sub-Heavy | 0.5, 1, 2, 5 | Sub fundamental prominent; useful for kicks and basses. |
| 4 | Low/Half | 1, 0.5, 1.5, 2 | Thickens the low end with a half-speed component. |
| 5 | Tight Stack | 1, 1.25, 1.5, 2 | Dense but harmonic; good for mid-range leads. |
| 6 | Near-unison | 1, 1.1, 1.2, 1.3 | Slightly detuned upper operators for a natural beating/chorus texture. |
| 7 | Stretch | 0.5, 1.333, 2.667, 5 | Wide octave + stretch; large spectral range. |

Between integer positions the ratios are linearly interpolated, so sweeping CHARACTER with a CV gives a continuous evolution through ratio space.

**OP FREQ** (per-operator semitone offset, −24 to +24) provides precise fine-tuning on top of whatever CHARACTER set is active. Use it to lock specific harmonic relationships (e.g., an operator exactly 7 semitones up for a perfect fifth modulator).

---

## 9. Wavetables

### Default Wavetable

Phaseon1 ships with a factory wavetable (`phaseon1.wav`) that loads automatically. It contains multiple frames of a carefully designed bass/lead waveform and is ideal for dubstep growls and morphing basses.

### Loading a Custom Wavetable

#### VCV Rack

Right-click the module panel → **Load Wavetable…** Choose any `.wav` file on your system. Multi-frame wavetables (multiple single-cycle waveforms concatenated) are supported.

#### 4ms MetaModule

Place `.wav` files in `phaseon1/` on the same volume as the current patch when possible. Phaseon1 browses the current patch volume first, then falls back to `sdc:/`, `usb:/`, and `nor:/`.

Examples:
- `sdc:/phaseon1/mytable.wav`
- `usb:/phaseon1/mytable.wav`
- `nor:/phaseon1/mytable.wav`

### Wavetable Format

Phaseon1 accepts standard single-cycle or multi-frame wavetables:

- **File format:** 16-bit or 32-bit floating-point WAV, mono.
- **Single-cycle:** any power-of-two frame size (256, 512, 1024, or 2048 samples). The entire file is one frame.
- **Multi-frame:** multiple single-cycle waveforms concatenated into one file. Phaseon1 automatically detects the frame boundaries and maps up to four frames internally (it downsamples the wavetable to four 256-sample frames for memory efficiency).
- Common wavetable sources: Serum `.wav` exports, AKWF single-cycle library, any compatible wavetable synth export. **Serum-format wavetables with clm marker chunks are supported.**

When a wavetable is loaded, operators set to **WAVE = WT** use it. Operators on any other WAVE setting use their built-in waveform instead. You can mix WT and non-WT operators freely.

### TIMBRE — Wavetable Frame Position

The **TIMBRE** knob (and **WT FRAME CV** input) controls which frame in the wavetable is played. At 0.0 it reads frame 0; at 1.0 it reads the last frame. Between frames, Phaseon1 interpolates linearly. This lets you morph through different timbral characters by moving a single knob or automating it with a CV.

**WT SCROLL mode** (switch, context menu):
- **Smooth** — continuous interpolation between frames.
- **Stepped** — snaps to individual frames. Gives crisp discrete timbre jumps.

> The AR envelope follower from the filter is automatically mixed into TIMBRE at a small amount (modulated by FM ENV AMT) to give envelope-following timbre brightness without extra patching.

---

## 10. WT FORM — Macro Timbral Modes

**WT FORM** is a 4-position switch that activates pre-tuned macro timbral recipes on top of your base settings. It is most dramatic when wavetable operators are active.

| Setting | Name | Effect |
|---|---|---|
| **Off** | Normal | Standard FM+WT operation. All parameters behave as described elsewhere. |
| **Growl** | Growl | Biases the wavetable into the low-frame region, increases feedback saturation and phase distortion depth, boosts formant content. Optimised for classic dubstep growl basses. Combine with DENSITY and EDGE. |
| **Yoi** | Yoi | Biases the wavetable toward mid frames, adds strong formant vowel movement, increases phase distortion, and links the SYNC ENV.F parameter more aggressively to envelope. Creates that "yoi yoi yoi" vowel-sweep growl. Best with moderate COMPLEX and COLOR. |
| **Tear** | Tear | Forces stepped WT scroll, biases into low-frame territory, applies asymmetric phase distortion (alternating warp modes per operator), and increases the TEAR FX level. Aggressive harmonic shredding. Great combined with high EDGE and COLOR. |

WT FORM values are saved with presets.

---

## 11. Envelope (Attack / Decay)

Phaseon1 uses a simple **AR (Attack / Release)** envelope as its amplitude envelope. There is no separate sustain or release — the DECAY control sets how fast the level falls once the gate goes low.

| Control | Range | Description |
|---|---|---|
| **ATTACK** | 0–1 | Attack time. At 0 = nearly instant, at 1 = slow rise over several seconds. |
| **DECAY** | 0–1 | Decay / release time. At 0 = instant cut, at 1 = long release. |

Both have CV inputs that add to the knob value (5 V = full sweep).

The envelope serves two additional roles beyond VCA:

1. **FM ENV AMT** routes the envelope level into the FM index, creating a prominent attack transient and then settling at the knob-set density.
2. **SYNC ENV.F** routes the envelope into a carrier frequency sync amount, adding a subtle pitch sweep transient at note onset.

---

## 12. The Filter

Phaseon1's filter (`DubstepSVF`) is a **Topology Preserving Transform (TPT) / zero-delay-feedback State-Variable Filter (SVF)** with tanh-based nonlinear saturation.

### Controls

| Control | Description |
|---|---|
| **CUTOFF** | Filter cutoff frequency, 0–1 normalized (maps exponentially from ~20 Hz to near-Nyquist). |
| **RESONANCE** | Filter resonance. Q range from 0.5 to ≈15. High values produce prominent resonant peak without self-oscillation. |
| **DRIVE** | Adds pre-filter soft saturation (tanh). At full drive the filter becomes notably coloured and aggressive. |
| **MORPH** | Morphs the filter type: 0 = Lowpass, 0.5 = Bandpass, 1.0 = Highpass. Intermediate positions are continuous crossfades. |
| **ENV LINK** | Links the internal envelope follower to the cutoff. At 0 = no follower. At 1 = strong envelope-following, making the filter open on louder/brighter attacks. |

### Envelope Follower

The filter contains a fast internal envelope follower that tracks the pre-filter signal amplitude. This follower feeds:

- Filter cutoff (via ENV LINK),
- Wavetable frame morphing (small automatic amount),
- The **ENV OUT** jack (0–10 V).

The follower uses a sub-millisecond attack (~0.8 ms) and a ~28 ms release, giving a snappy but musical dynamic response.

---

## 13. LFO — Internal, LFO OUT, ENV OUT

### Internal LFO

Phaseon1 contains a **KineticLFO** — a single-cycle sinusoidal LFO with a gravity-driven waveshaping stage.

**How it is routed internally:**

The LFO automatically modulates several destinations at once via the trimpots (see [Trimpots](#21-trimpots)):
- Wavetable frame position (WT FRAME MOD trimpot)
- Formant vowel amount (VOWEL MOD trimpot)
- Operator 1 feedback depth (FB SRC trimpot)
- Envelope Decay time (DECAY MOD trimpot)
- Filter cutoff (CUTOFF MOD trimpot)

**Controls:**

| Control | Description |
|---|---|
| **LFO SYNC DIV** | Sets the LFO rate relative to the incoming CLOCK signal in 12 steps: 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/64. Without a clock patch the knob has no effect on its own; the LFO defaults to 1 Bar tempo. |
| **LFO GRAV** | Shapes the LFO waveform. At 0 = pure sine. Increasing applies a `tanh`-based gravity compression that flattens peaks and steepens the zero-crossings, creating a more bouncy, stepped feel at high values. |
| **LFO ENABLE** | Toggle switch. Off = LFO frozen at zero (all LFO mod paths go silent). The LFO still generates its signal to LFO OUT when enabled. |

**Gate reset:** Every rising edge on the GATE input resets the LFO phase to 0. This ensures a deterministic LFO start position for every note, useful for synchronized wobbles.

### LFO OUT

The LFO output jack emits the shaped LFO waveform at ±5 V. Use it as a general-purpose CV source for any destination in your patch, including feeding it back into Phaseon1's own CV inputs.

### ENV OUT

The envelope follower output emits 0–10 V. It follows playing dynamics (louder/more complex audio → higher voltage). Useful for auto-filter effects elsewhere in your patch or for feedback routing into CUTOFF CV, DECAY CV, etc.

---

## 14. Tear FX

**TEAR** applies a multi-stage spectral shredding effect: a short comb delay (applied to mid/high frequencies only, leaving the low end intact) followed by optional wavefold and an LFSR (linear-feedback shift register) noise injection.

| Control | Description |
|---|---|
| **TEAR** knob | 0 = off, 1 = maximum shred. Smoothly fades in with no clicks. |
| **TEAR FOLD** toggle | Enables the wavefold + LFSR noise layer on top of the comb effect. At full TEAR + FOLD the sound becomes very noisy and agitated. Useful for industrial and heavy dubstep. |

Tear is applied to the mid/high band only; the sub-frequency content is preserved to avoid losing bass weight. Sub is split out before the Tear stage and recombined after.

---

## 15. Bitcrush

**BITCRUSH** is a combined **sample-rate reducer and bit-crusher**:

- At low values: subtle sample-rate reduction (from the module's internal 40960 Hz downward), adding a mild lo-fi grit.
- At mid values: sample-rate and bit depth both reduce, giving obvious stepped/digital aliasing.
- At high values (above ~0.65): a **wavefold** layer blends in before quantization for extreme lo-fi character.

The control maps from 16-bit/full-rate (0) down to 3-bit/very reduced sample rate (1).

CV input scales additively with the knob (5 V = full sweep).

---

## 16. Sub Oscillator

**SUB** mixes in a pure sine wave one octave below the fundamental into the output signal. Unlike the FM operators, the sub oscillator does not interact with the FM routing — it is an independent sine tone added after the operator engine.

Use the SUB knob (0–1) to blend in sub-bass weight independently of the FM sound architecture. Ideal for extending bass weight on mid-frequency patches.

---

## 17. Formant Shaper

The **FormantShaper** is a dual-band high-Q bandpass filter (TPT SVF) that adds vowel/speech character to the sound.

| Control | Description |
|---|---|
| **FORMANT** knob | 0 = bypassed. 1.0 = full vowel character. |
| **FORMANT CV** | Additive CV. Opens the formant amount. |

Internally, the shaper contains two parallel bandpass peaks with Q values from 3.5 to 6.5 (rising with the FORMANT knob). The two peaks morph between a lower (350–600 Hz) and upper (750–1150 Hz) pair. The morph position is driven automatically by the macro envelope follower. The dry signal is preserved (ducked slightly at full wet) to maintain body.

With WT FORM set to **Growl** or **Yoi**, the formant shaper receives additional macro boosting and becomes the primary timbral shaping voice.

> **Tip:** Patch LFO OUT into FORMANT CV with the VOWEL MOD trimpot for automatic LFO-driven vowel movement — the classic dubstep wobble.

---

## 18. Presets — Loading and Saving

Phaseon1 stores up to **127 preset slots** in a bank file (`Phbank.bnk`). A factory bank ships bundled with the plugin.

### Navigation

| Control | Description |
|---|---|
| **PRESET INDEX** knob | Scrolls through preset slots 1–127. The display (Rack: tooltip; MetaModule: panel display) shows `01/127 Preset Name`. |
| **PRESET CV** input | 0–10 V selects slots 1–127. Useful for hands-free preset scanning. |

### Loading a Preset

Turn the PRESET INDEX knob or send a CV to PRESET CV. The patch changes immediately with a brief mute crossfade to avoid clicks.

### Saving a Preset

1. Dial in the sound you want.
2. Use PRESET INDEX to navigate to the desired slot.
3. **Right-click the module** → **Save Preset**; or press the **SAVE** button.

All current parameter values (including per-operator WAVE, FREQ, LEVEL, and all hidden stored params) are written to the selected slot and automatically saved to disk.

### Renaming a Preset

Right-click the module → **Rename Preset…** Enter a name up to 28 characters. The name is saved immediately.

### Copying and Pasting Presets

Right-click → **Copy Preset** and **Paste Preset** let you duplicate a preset to a different slot.

### Initializing a Preset Slot

Right-click → **Initialize Preset Slot** resets the selected slot to factory defaults.

### Bank File Locations

| Platform | Path |
|---|---|
| VCV Rack (Windows) | `%APPDATA%\Rack2\MorphWorx\phaseon1\Phbank.bnk` |
| VCV Rack (macOS) | `~/Library/Application Support/Rack2/MorphWorx/phaseon1/Phbank.bnk` |
| VCV Rack (Linux) | `~/.Rack2/MorphWorx/phaseon1/Phbank.bnk` |
| 4ms MetaModule | Current patch volume first, e.g. `sdc:/phaseon1/Phbank.bnk`, `usb:/phaseon1/Phbank.bnk`, or `nor:/phaseon1/Phbank.bnk` |

The factory bank bundled inside the plugin package is loaded automatically on first use and serves as the fallback if the user bank is missing. On MetaModule, Phaseon1 prefers the current patch volume and then falls back across the other local volumes.

### Loading an External Bank

Right-click → **Load Bank…** to browse for any `.bnk` file. Switching banks swaps the full 127-slot content.

---

## 19. Randomize

The **RANDOMIZE** button generates a musically-informed random patch:

- Algorithm, Character, and WT FORM are randomized freely.
- Filter cutoff, resonance, and decay are nudged ±10–25% from their current positions (not reset entirely) to keep patches in a useful range.
- Per-operator WAVE, FREQ, and LEVEL are randomized using curated musical semitone pools:
  - **OP1** (primary carrier): root octave offsets only (−24, −12, 0, +12).
  - **OP2** (secondary): consonant intervals (±octave, ±fifth, unison).
  - **OP3–4** (modulators/colour): wider intervals including thirds, fourths, sevenths.
- A deterministic seed is written into the `RANDOMIZE SEED` hidden parameter so the exact variant is saved when you save a preset.

Pressing Randomize again generates a new seed and a new variant. Once happy with a result, save a preset to lock it in.

---

## 20. CV Reference

All CV inputs follow the same scaling rule unless noted otherwise:

> **5 V = full parameter sweep** (knob 0 → 1). CV is added to the knob value and the sum is clamped to the valid parameter range. Bipolar inputs accept ±5 V for bidirectional control.

### Special Inputs

| Input | Notes |
|---|---|
| **V/OCT** | Standard 1 V/octave, 0 V = C4 (261.63 Hz). |
| **GATE** | High >1.5 V. Triggers AR envelope and resets LFO phase on rising edge. |
| **CLOCK** | Any gate/trigger. An internal tempo tracker measures BPM from successive pulses. |
| **ALL FREQ CV** | ±5 V = ±24 semitones, applied to all four operators simultaneously. |
| **OP1 LEVEL CV** | ±5 V = ±1.0 level offset for OP1 only. |
| **SYNC DIV CV** | 0–10 V maps to 12 LFO sync division steps. When patched, this overrides the SYNC DIV knob. |
| **PRESET CV** | 0–10 V, maps to preset slots 1–127. |

---

## 21. Trimpots

Trimpots appear as small knobs and set the **depth of LFO→destination** modulation. All are bipolar (−1 to +1); a setting of 0 = the LFO has no effect on that destination.

| Trimpot | Destination | Notes |
|---|---|---|
| **WT FRAME MOD** | Wavetable frame position (TIMBRE) | Positive = LFO sweeps frames upward on LFO positive phase. |
| **VOWEL MOD** | Formant shaper amount | Positive = LFO opens formant on positive phase. Set to ~0.5 with LFO for automatic vowel motion. |
| **FB SRC** | Operator 1 feedback depth | Modulates how much OP1 feeds back into itself. Can produce rhythmic FM distortion wobble in time with the LFO. |
| **DECAY MOD** | Envelope decay time | LFO shortens or lengthens the decay — useful for rhythmic amplitude gating. |
| **CUTOFF MOD** | Filter cutoff | Combined with LFO GRAV, can create smooth or snappy filter sweeps. |

---

## 22. Installation and File Locations

### VCV Rack 2

1. Download `MorphWorx-x.x.x-win-x64.vcvplugin` (or the appropriate platform build) from the [Releases page](../../releases).
2. Drop the file into your Rack plugins folder and restart Rack. Rack will extract it automatically.

The factory preset bank (`Phbank.bnk`) and default wavetable (`phaseon1.wav`) are bundled inside the `.vcvplugin` archive and load automatically on first use. No manual file placement is required.

### 4ms MetaModule

1. Install `MorphWorx.mmplugin` via the MetaModule web UI.
2. Download `MorphWorx-phaseon1-sdcard.zip` from the Releases page, extract it, and copy the `phaseon1/` folder to the root of the volume where your patch lives.
  Valid locations include:
  - `sdc:/phaseon1/Phbank.bnk`, `sdc:/phaseon1/phaseon1.wav`, `sdc:/phaseon1/xs_*.wav`
  - `usb:/phaseon1/Phbank.bnk`, `usb:/phaseon1/phaseon1.wav`, `usb:/phaseon1/xs_*.wav`
  - `nor:/phaseon1/Phbank.bnk`, `nor:/phaseon1/phaseon1.wav`, `nor:/phaseon1/xs_*.wav`
3. Phaseon1 searches the current patch volume first, then falls back to the other local volumes if the files are not found there.

---

## 23. Tips and Workflows

### Classic Dubstep Growl Bass

1. Load a multi-frame basis wavetable or use the default `phaseon1.wav`.
2. Set WT FORM = **Growl**.
3. ALGO = **Stack** or **Swarm**, CHARACTER = **0 or 1**.
4. DENSITY ≈ 0.6, EDGE ≈ 0.4, WARMTH ≈ 0.7, MORPH ≈ 0.5.
5. Patch CLOCK from your sequencer. Set LFO SYNC DIV to 1/8 or 1/16.
6. Set VOWEL MOD trimpot to 0.4–0.6. Set WT FRAME MOD to 0.3.
7. FORMANT ≈ 0.3. FILTER CUTOFF ≈ 0.55–0.65, RESONANCE ≈ 0.4.
8. Play a low note, hold the gate. The wobble rate locks to your clock. Adjust LFO GRAV for rounder (low) or snappier (high) wobble shape.

### Metallic / Industrial Lead

1. All four operators on WAVE = **Sine**.
2. CHARACTER = **6–7** (Near-Unison or Stretch).
3. DENSITY ≈ 0.7, EDGE ≈ 0.5, COMPLEX ≈ 0.4, COLOR ≈ 0.3.
4. ALGO = **Ring** or **Cross** — enjoy the clanging.
5. Low ATTACK, medium DECAY. FILTER at moderate cutoff with some DRIVE.

### Smooth FM Pad

1. ALGO = **Pairs** or **Wide**.
2. CHARACTER = **1 or 2**, DENSITY ≈ 0.2–0.4.
3. MOTION ≈ 0.4–0.6 for stereo spread.
4. ATTACK longish, DECAY long. FILTER fully open.
5. COMPLEX = 0, COLOR = 0 — keep it clean.
6. WARMTH ≈ 0.4 gives the slightest edge warmth for character.

### Using Randomize for Preset Discovery

1. Start from a preset you partially like.
2. Hit RANDOMIZE once. If the result is interesting, save it immediately.
3. Keep hitting RANDOMIZE — each variant is different but sonically musical because the pitch-pool logic constrains operator tuning to consonant intervals.
4. Once you find a direction you like, refine manually with DENSITY, EDGE, and ALGO, then save.

### Layering WT and FM Operators

Set OP1 and OP2 to **WAVE = WT** and OP3/OP4 to **Sine** or **Fat Sub-Tri**. In an ALGO like **Pairs**, OP3→OP4→output and OP1 (WT) goes directly to output. Sweep TIMBRE to morph the WT pair while the sine pair stays fixed — a hybrid texture that's part wavetable, part FM.

### Envelope-Following Filter

Set FILTER ENV LINK to 0.5–0.8 and FM ENV AMT to 0.5–0.7. On note attack the envelope follower opens the cutoff automatically. Combined with a medium RESONANCE, this gives an auto-wah effect keyed to your own playing dynamics. Patch ENV OUT to an external destination too for multi-module envelope following.

---

*Phaseon1 — MorphWorx. Copyright © 2026 Bemushroomed. Licensed under GPL-3.0-or-later.*
