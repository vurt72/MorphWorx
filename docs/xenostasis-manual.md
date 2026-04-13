# Xenostasis — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [How It Works](#2-how-it-works)
3. [Signal Flow](#3-signal-flow)
4. [Dual Operating Modes](#4-dual-operating-modes)
5. [Controls](#5-controls)
6. [Inputs](#6-inputs)
7. [Outputs](#7-outputs)
8. [Lights](#8-lights)
9. [Wavetable Bank](#9-wavetable-bank)
10. [Bytebeat Engine](#10-bytebeat-engine)
11. [FM Synthesis](#11-fm-synthesis)
12. [Metabolism System](#12-metabolism-system)
13. [Cross Modulation](#13-cross-modulation)
14. [TEAR Effect](#14-tear-effect)
15. [Filter Section](#15-filter-section)
16. [Drive and Output Stage](#16-drive-and-output-stage)
17. [MetaModule Notes](#17-metamodule-notes)
18. [Tips and Workflows](#18-tips-and-workflows)

---

## 1. Overview

**Xenostasis** is a **16 HP autonomous hybrid synthesis organism** that combines wavetable synthesis, bytebeat algorithms, and FM synthesis into a single self-regulating voice. It operates in two modes: as a **free-running drone** that evolves autonomously, or as a **triggered percussion/strike synthesizer** when a clock is connected.

At the heart of Xenostasis is a **metabolism system** — an internal energy model that responds to parameter changes, CV input, and clock triggers. The metabolism drives spectral mutation: wavetable frames drift, bytebeat patterns bend, FM ratios wander, and stereo width fluctuates, all governed by the interplay between Chaos, Stability, and Homeostasis.

Key features:

- 8 procedural wavetable banks with 64 frames each (2048 samples per frame)
- 8 bytebeat algorithm families with continuous character morphing
- 2-operator FM synthesis (percussive) / 3-operator FM synthesis (drone)
- Internal metabolism with energy, stress, residual bias, and organism pulse
- Cross-modulation between wavetable and bytebeat engines
- Built-in 4-pole ladder filter, pitch-tracked comb filter, and TEAR effect
- Drive stage with soft-knee compression and parallel saturation
- Stereo output with energy-driven spatial instability
- Drone mode (free-running) and percussive mode (clock-triggered)

---

## 2. How It Works

Xenostasis is not a conventional synthesizer. It behaves more like a living system:

### The Three Engines

1. **Wavetable Oscillator** — reads from one of 8 procedural wavetable banks. The frame position drifts autonomously (governed by metabolism) while phase warping adds harmonic distortion. In percussive mode, clock strikes push the frame position and warp depth for transient variation.

2. **Bytebeat Engine** — three layered integer-math expressions generate complex, non-repeating digital textures. The bytebeat runs at a pitch-tracked rate and its speed, time curvature, and bit-shift depths are modulated by the metabolism. 8 algorithm families provide radically different characters.

3. **FM Synthesis** — in percussive mode, a 2-operator FM drum patch with randomized ratio, index, and pitch drop fires on each clock strike. In drone mode, a 3-operator FM stack with slowly wandering LFOs creates evolving tonal textures.

### The Metabolism

An internal energy model accumulates excitation from Chaos, Density, clock triggers, and CV activity, then decays under the control of Homeostasis. Energy drives:

- Wavetable frame drift range and speed
- Bytebeat layer mixing and speed
- FM modulation index depth
- Spectral tilt (brightness)
- Stereo spatial instability
- Storm light activation

The system also maintains a **residual bias** that prevents guaranteed return to calm, a **stress exposure** memory that accumulates during high-energy states, and an ultra-slow **organism pulse** (20–120 second cycle) that breathes all parameters.

---

## 3. Signal Flow

```
                 ┌──────────────────────────────────────────┐
  V/OCT ──→ [Pitch] ──→┐                                    │
                        │                                    │
                 ┌──────┴──────┐  ┌──────────┐  ┌─────────┐ │
                 │  Wavetable  │←→│ Bytebeat │←→│   FM    │ │
                 │  Oscillator │  │  Engine   │  │ Synth   │ │
                 │  (8 tables) │  │ (8 modes) │  │(2/3 op) │ │
                 └──────┬──────┘  └────┬─────┘  └────┬────┘ │
                        │ WT VOL       │ BB VOL      │FM VOL│
                        └──────┬───────┴─────────────┘      │
                               │ + Sub osc + Metallic noise │
                               │ (energy-driven)            │
  CLOCK ──→ [Strike Env] ──→ [×VCA] (perc mode only)       │
                               │                            │
                        ┌──────┴──────┐                     │
                        │   Filter    │                     │
                        │ Off/Ladder/ │                     │
                        │    Comb     │                     │
                        └──────┬──────┘                     │
                               │                            │
                        ┌──────┴──────┐                     │
                        │    TEAR     │                     │
                        │ fold+comb+  │                     │
                        │   noise     │                     │
                        └──────┬──────┘                     │
                               │                            │
                        ┌──────┴──────┐                     │
                        │ Drive/Comp  │                     │
                        │ + Width     │                     │
                        └──────┬──────┘                     │
                               │                            │
                        LEFT ──┴── RIGHT                    │
                 └──────────────────────────────────────────┘
```

---

## 4. Dual Operating Modes

### Drone Mode (No Clock Connected)

When the CLOCK input is unpatched, Xenostasis runs as a free-running drone. The three synthesis engines play continuously, and the metabolism evolves the sound autonomously over time. The ENV/WIDTH knob controls stereo width instead of envelope decay.

In drone mode:
- FM uses a 3-operator stack with slowly wandering LFOs
- Wavetable frame position drifts under the influence of energy, stability, and chaos
- The organism pulse breathes all parameters on a 20–120 second cycle
- Audio is always present (no gating)
- ENV/WIDTH controls stereo spread: 0 = mono, 0.5 = natural, 1.0 = double-wide

### Percussive Mode (Clock Connected)

When a clock or trigger is patched to the CLOCK input, Xenostasis becomes a triggered percussion synthesizer. Each trigger fires a new strike with randomized per-hit variation:

- Exponential decay envelope (controlled by ENV/WIDTH knob: 5 ms to 200 ms)
- Randomized wavetable frame jump and phase warp per hit
- Randomized bytebeat time kick per hit
- Randomized resonant noise burst frequency per hit
- Randomized FM ratio, index, and pitch drop per hit
- Randomized decay multiplier (0.7×–1.3×) per hit
- Silent between triggers (VCA fully closed)

The result is that every trigger produces a unique sound — similar in character but never identical. At high Chaos, consecutive hits can sound dramatically different.

---

## 5. Controls

### Row 1: Core Parameters

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **PITCH** | −4 V to +4 V | 0 V (C4) | Base pitch. Combined with V/OCT input. |
| **WT TABLE** | 0–7 (snapped) | 0 | Selects wavetable bank. See [Wavetable Bank](#9-wavetable-bank). |
| **CHAOS** | 0–100% | 0% | Spectral instability. Scrambles oscillator ratios, frame destinations, and bytebeat rules. Low = ordered drone, high = disintegrating noise. |
| **STABILITY** | 0–100% | 60% | Governs how tightly the drone holds its pitch and spectral center. Low = drifting/evolving, high = locked/stable. |
| **HOMEOSTASIS** | 0–100% | 50% | Self-correcting feedback. When the system drifts, it pulls it back toward equilibrium. High = resists chaos, low = organism mutates freely. |

### Row 2: Modulation and Character

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **TEAR** | 0–100% | 0% | Glitch layer: pitch-warps and fragment-loops the output, creating digital tearing, stutter, and granular shredding. |
| **PUNCH** | 0–100% | 50% | Drum/trigger transient intensity. Higher = harder, more percussive hit. Affects frame jump, FM pitch drop, and metallic noise level. |
| **CROSS MOD** | 0–100% | 15% | Cross-modulates the wavetable and bytebeat engines. Adds FM sidebands and spectral entanglement. See [Cross Modulation](#13-cross-modulation). |
| **DENSITY** | 0–100% | 20% | Harmonic and bytebeat event density. Low = sparse/airy, high = thick/saturated spectral mass. |
| **BB CHAR** | 0–100% | 50% | Bytebeat character. Controls bit-shift depths and algorithm morphing within the current mode. |

### Row 3: Mix

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **BB VOL** | 0–200% | 100% | Bytebeat layer volume. 0 = silent, 100% = unity, 200% = boosted. |
| **WT VOL** | 0–100% | 70% | Wavetable oscillator volume. |
| **FM VOL** | 0–100% | 50% | FM synthesis volume. |
| **DRIVE** | 0–100% | 30% | Output drive. Combines soft-knee compression with parallel saturation. See [Drive](#16-drive-and-output-stage). |
| **BB MODE** | 0–7 (snapped) | 0 | Selects bytebeat algorithm family. See [Bytebeat Engine](#10-bytebeat-engine). |

### Row 4: Playback and Filter

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **BB PITCH** | −2 to +2 oct | 0 | Bytebeat pitch offset relative to the main pitch. |
| **CUTOFF** | 0–100% | 100% (open) | Filter cutoff frequency. Exponential mapping from 20 Hz to ~0.45× sample rate. |
| **RES** | 0–100% | 0% | Filter resonance. In Ladder mode, approaches self-oscillation near maximum. In Comb mode, controls feedback amount (0–97%). |
| **FILTER MODE** | Off / Ladder / Comb | Off | Selects the filter type. See [Filter Section](#15-filter-section). |
| **ENV/WIDTH** | 0–100% | 50% | **Percussive mode**: envelope decay (5 ms at 0, 200 ms at 100%). **Drone mode**: stereo width (0 = mono, 50% = natural, 100% = double-wide). |
| **FRAME** | 0–100% | 50% | Manual frame scroll offset. Shifts the wavetable frame position ±40 frames from center. |

---

## 6. Inputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **CLOCK** | Trigger | Gate/trigger | Clock/trigger input. When connected, enables percussive mode. Each rising edge fires a new strike. |
| **V/OCT** | V/Oct Pitch | 1V/oct | Pitch input. Added to the PITCH knob. 0 V = C4 (261.6 Hz). |
| **CHAOS** | Chaos CV | 0–10 V | Added to CHAOS knob (scaled /10). |
| **CROSS** | Cross Mod CV | 0–10 V | Added to CROSS MOD knob (scaled /10). |
| **TABLE** | Table CV | 0–10 V | Overrides WT TABLE knob. 0 V = table 0, 10 V = table 7. |
| **DENSITY** | Density CV | 0–10 V | Added to DENSITY knob (scaled /10). |
| **BB CHAR** | BB Character CV | 0–10 V | Added to BB CHAR knob (scaled /10). |
| **BB MODE** | BB Mode CV | 0–10 V | Overrides BB MODE knob. 0 V = mode 0, 10 V = mode 7. |
| **CUTOFF** | Filter Cutoff CV | 0–10 V | Added to CUTOFF knob (scaled /10). |
| **RES** | Filter Resonance CV | 0–10 V | Added to RES knob (scaled /10). |
| **BB PITCH** | BB Pitch CV | 0–10 V | Modulates bytebeat pitch offset (±2 octaves). |
| **TEAR** | Tear CV | 0–10 V | Added to TEAR knob (scaled /10). |

---

## 7. Outputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **LEFT** | Left Audio | ±5 V | Left channel stereo output. |
| **RIGHT** | Right Audio | ±5 V | Right channel stereo output. |

---

## 8. Lights

| Light | Color | Behavior |
|-------|-------|----------|
| **ENERGY** | Green | Brightness tracks the metabolism energy level (0–100%). |
| **STORM** | Green | Lights up when energy exceeds 60%. Brighter = more intense storm state. |

---

## 9. Wavetable Bank

Xenostasis uses 8 procedurally generated wavetable banks, each containing 64 frames of 2048 samples. The wavetables are generated at startup (not loaded from files) and shared across all module instances.

| Index | Name | Character |
|-------|------|-----------|
| **0** | Feral Machine | Wild, metallic, experimental. 8 modes per 8-frame group: inharmonic metal clusters, ring-mod screech, filtered noise bursts, FM growl, wavefolder cascade, broken oscillator, hard-sync harmonics, granular texture. |
| **1** | Hollow Resonant | Breathy, formant-like. Resonant peaks at harmonics 3, 7, and 11 that shift across frames. Airy, vocal quality. |
| **2** | Abyssal Alloy | Chaotic metallic with immense sub. Strong fundamental + inharmonic clusters + controlled noise. Each 8-frame group uses a different synthesis mode (additive, FM, ring-mod, hard-sync, wavefold, bitmask, noise-metal, extreme alloy). |
| **3** | Substrate | Sub-heavy anchor morphing into pure abyss. Frame 0 is nearly a sine wave. Higher frames progressively add octave, third harmonic, and fourth harmonic with phase rotation, becoming massively dark and bassy. No brightness added. |
| **4** | Submerged Monolith | Oppressive, alien, cinematic. Starts near-sine, gradually introduces irrational-ratio inharmonics, phase warp asymmetry, and subtle FM/wavefold artifacts. Partial clouds create band-limited "noise clusters." |
| **5** | Harsh Noise | Distorted, aggressive, drum-optimized. 8 independent modes: hard-clipped square, bitcrushed sine, ring-mod harsh, noise burst, FM distortion, wavefolder, absolute-value harshness, digital noise + sub. Hard frame switching (no interpolation). |
| **6** | Arcade FX | Lasers, bleeps, zaps, digital chaos. 10 independent modes: descending/rising laser sweeps, bit-pattern bleeps, exponential chirps, interference tones, PWM pulses, bytebeat fragments, ring-mod alien, sample-rate reduction, metallic zaps. Hard frame switching. |
| **7** | Alien Physics Collapse | Chaos modes based on nonlinear maps. 8 groups: logistic-lensed triangle, circle map phase rotation, tent map fracture, fractal warp, recursive phase lens, coupled map product, edge sharpening, and collapse mode. Hard frame switching. |

### Frame Behavior

- **Tables 0–4**: Frames morph smoothly via bilinear interpolation. The frame position drifts continuously, creating gradual timbral evolution.
- **Tables 5–7**: Frames switch hard (no interpolation). Each frame is an independent texture. Moving between frames creates sudden, jarring timbral changes — ideal for percussive and glitch work.

### MetaModule Variant

On MetaModule, wavetable dimensions are reduced (32 frames × 1024 samples) to fit within ARM cache budgets. Simpler generation algorithms are used (morphing sine/saw/tri/square variants) to keep startup fast.

---

## 10. Bytebeat Engine

The bytebeat engine generates audio by evaluating integer-math expressions on incrementing time counters. Three layered expressions run simultaneously at different rates:

- **Layer 1** (main): full bytebeat speed
- **Layer 2** (body): 0.517× speed, adds low-frequency body
- **Layer 3** (shimmer): 1.333× speed, energy-gated for high-energy shimmer

### Bytebeat Modes

| Index | Name | Character |
|-------|------|-----------|
| **0** | Xeno | The original Xenostasis bytebeat. Shifting, evolving digital organism. |
| **1** | Melodic | Classic melodic-ish family. More tonal, quasi-pitched patterns. |
| **2** | Rhythmic | Gated patterns. Bit-masking creates rhythmic on/off structures. |
| **3** | Harsh | XOR grind. Aggressive, abrasive digital noise. |
| **4** | Tonal | More repeating patterns. Less hash, more musicality. |
| **5** | Arp | Steppy / arpeggiated. Step counters create quasi-melodic sequences. |
| **6** | Crackle | Bitmask textures. Sparse, granular, crackling. |
| **7** | Organism | Tied to pitch + energy. The metabolism directly modulates the bytebeat expression. More alive, more responsive to system state. |

### BB CHAR (Character)

The BB CHAR knob continuously morphs between two algorithm variants within each mode. At 0%, shift amounts are minimal (simpler patterns). At 100%, shifts are aggressive (more complex, noisier). Intermediate positions blend the two variants per-sample.

### BB PITCH

The BB PITCH knob offsets the bytebeat time increment by ±2 octaves relative to the main pitch. This allows the bytebeat to run at a different musical pitch than the wavetable oscillator, creating interval relationships between the two engines.

### Time Curvature

A very slow sinusoidal "time curvature" modulates the bytebeat time increment based on the accumulated time counter. This bends the pattern geometry over tens of seconds, ensuring patterns never exactly repeat.

### Clock Sync

In percussive mode, each clock trigger reseeds the bytebeat time counters with a deterministic seed derived from the current mode and character. This means the same mode+character combination produces the same initial pattern on each hit, while different mode+character settings produce different patterns.

---

## 11. FM Synthesis

### Percussive FM (Clock Mode)

When a clock trigger fires, a 2-operator FM drum patch generates:

- **Carrier**: sine oscillator at the current pitch
- **Modulator**: sine oscillator at carrier × ratio

Per-hit randomization:

| Parameter | Range | Effect |
|-----------|-------|--------|
| Ratio | 1.0, 1.41, 2.0, 2.76, 3.0, 3.5, 4.0, 5.33, 7.0 | Harmonic character (bell, metallic, woody, etc.) |
| Index | 2–14 | Brightness at strike peak |
| Pitch Drop | 0–3 octaves | Pitch sweep (0 = steady, 3 = dramatic drop) |

The FM index decays with the strike envelope, so the harmonic content is brightest at the transient and darkens rapidly. Pitch drop also follows the envelope squared, creating characteristic drum-machine pitch sweeps.

### Drone FM (Free-Running)

In drone mode, a 3-operator FM stack generates evolving tonal textures:

- **Modulator 2** → **Modulator 1** → **Carrier** (cascade topology)
- Three independent LFOs at 0.037, 0.013, and 0.0071 cycles per control tick wander the mod:carrier ratio, modulation index, and stereo detuning
- Chaos adds index depth and ratio instability
- Stability constrains all LFO influence (at full stability, ratios are nearly fixed)
- The organism pulse breathes the modulation index
- Stereo: right channel uses a slightly different phase offset and index for decorrelation

FM output passes through `tanh()` saturation scaled by chaos and stability.

---

## 12. Metabolism System

The metabolism is the control system that makes Xenostasis behave like a living organism rather than a static synthesizer. It runs at control rate (every 32 samples).

### Energy

A scalar (0–1) representing the system's current excitation level.

**Energy sources:**
- Chaos parameter (scaled ×4.0)
- Density parameter (scaled ×1.5)
- CV rate-of-change (scaled ×0.5)
- Clock triggers (burst of 0.15 per hit)

**Energy decay:**
$$\text{decayRate} = (0.3 + \text{homeostasis} \times 2.0) + \text{homeostasis} \times 4.0 \times \text{energy}^2$$

At low homeostasis, energy decays slowly (0.3/second base). At high homeostasis, decay accelerates dramatically as energy increases — a nonlinear negative feedback that prevents runaway.

### Residual Bias

A slowly wandering floor value that prevents energy from ever reaching exactly zero. Even in a calm state, there is always a slight residual energy that keeps the system subtly alive. The bias range is influenced by stress exposure and homeostasis.

### Stress Exposure

A long-term memory of storm states. Accumulates during high-energy periods (energy > 0.5), forgets slowly (faster with high homeostasis). Stress exposure slightly widens the residual bias range, so a system that has been through storms never fully returns to its original calm state.

### Organism Pulse

An ultra-slow internal oscillator (0.05 Hz base rate, slower during stress) that creates breathing-like modulation across all parameters. The pulse subtly modifies:
- Cross-modulation depth
- Bytebeat speed
- Frame drift velocity
- FM modulation index (drone mode)

Period: approximately 20 seconds at rest, stretching to 120+ seconds during high stress.

### What Energy Controls

| Parameter | Low Energy | High Energy |
|-----------|------------|-------------|
| Frame drift range | 4 frames | 60+ frames |
| Frame drift speed | Slow | Fast, chaotic |
| BB layer 3 mix | 20% | 90% |
| Spectral tilt | Warm (filtered) | Bright (raw) |
| Stereo instability | Minimal | Wide, decorrelated |
| Storm light | Off | Bright |

---

## 13. Cross Modulation

The CROSS MOD knob controls bidirectional modulation between the wavetable and bytebeat engines:

### Bytebeat → Wavetable

- **Phase warp**: The slew-limited bytebeat signal warps the wavetable oscillator's phase, adding FM-like sidebands. Depth scales with cross amount, energy, and inverse stability.
- **Frame expansion**: A nonlinear (tanh + square) function of the bytebeat signal boosts wavetable frame position by up to ±14 frames, causing dramatic timbral jumps during active bytebeat patterns.

### Wavetable → Bytebeat

- Density and epsilon (non-repetition variable) are influenced by the wavetable state through the metabolism feedback path, creating an indirect modulation loop.

### Organism Pulse Interaction

The organism pulse modulates cross-modulation depth by ±2.5%, creating slow breathing in the entanglement between engines.

---

## 14. TEAR Effect

TEAR is a post-engine glitch/coloration effect identical in architecture to Minimalith's TEAR. It operates on the mid/high frequency band (low frequencies are split off and preserved clean).

### Stages

1. **Wavefold**: `sin(x × drive)` where drive scales 1.0–8.0 with TEAR amount
2. **Noise injection**: xorshift PRNG noise mixed at 1–7% level
3. **Pitch-tracked comb filter**: delay length = sampleRate / (pitch × (8 + 28 × TEAR)), feedback 12–84%
4. **Soft-clip output**: `tanh()` saturation on the wet signal

The dry/wet mix scales linearly with the TEAR knob. At TEAR = 0, the effect is completely bypassed. The comb filter is pitch-tracked, so the resonance harmonically relates to the played pitch.

---

## 15. Filter Section

The FILTER MODE switch selects between three modes:

### Off

No filter applied. The signal passes through unchanged.

### Ladder (4-Pole Moog-Style)

A 24 dB/octave lowpass filter with resonance feedback approaching self-oscillation near maximum:

- **Cutoff**: Exponential mapping from 20 Hz to ~0.45 × sample rate
- **Resonance**: 0 = no feedback, 100% = near self-oscillation (resonance feedback ≈ 3.9)
- Each pole uses `tanh()` nonlinearity for warm, saturating character (VCV Rack) or a cheaper `x/(1+|x|)` approximation (MetaModule)

When cutoff is above 99.5% and resonance is below 0.2%, the ladder filter is automatically bypassed to save CPU.

### Comb (Pitch-Tracked Feedback)

A pitch-tracked feedback comb filter that adds metallic resonance and harmonic sheen:

- **Delay length**: sampleRate / pitch frequency (pitch-tracked)
- **Resonance**: controls feedback amount (0–97%), approaching infinite sustain at maximum
- Internal one-pole lowpass in the feedback path (coefficient 0.55) prevents runaway high frequencies

The comb filter is particularly effective on percussive hits, adding karplus-strong-like plucked string resonance.

---

## 16. Drive and Output Stage

### Drive

The DRIVE knob combines two parallel processing paths:

1. **Soft-knee compression**: Input is pre-gained (1–5×), then any signal exceeding a dynamic threshold (1.2 at min drive, 0.4 at max) is compressed at a ratio of 1:1 to 1:6. This tames peaks while boosting quiet content.

2. **Parallel tanh saturation**: The same pre-gained signal passes through `tanh()` waveshaping for warm harmonic distortion. The saturated path is blended with the compressed path proportionally to the drive amount.

Makeup gain (1.0–1.6×) compensates for compression.

At Drive = 0, a simple cubic soft-clipper is applied instead.

### Stereo Width (Drone Mode)

In drone mode, the ENV/WIDTH knob controls stereo spread using mid/side processing:

$$L = \text{mid} + \text{side} \times \text{width}$$
$$R = \text{mid} - \text{side} \times \text{width}$$

Where width = ENV/WIDTH × 2 (0 = mono, 1.0 = natural at knob center, 2.0 = double-wide at maximum).

### Output Level

The final signal is scaled to ±5 V peak.

---

## 17. MetaModule Notes

### Reduced Wavetable Dimensions

On MetaModule, wavetable banks use 32 frames × 1024 samples (vs 64 × 2048 on VCV Rack) to fit within ARM L1/L2 cache budgets. Simpler generation algorithms (morphing sine/saw/tri/square variants) are used for fast startup.

### Fast Math Substitutions

All transcendental functions (`sin`, `tanh`, `exp2`) are replaced with LUT-based or algebraic approximations:
- `sin()` → 2048-entry LUT with linear interpolation
- `tanh()` → `x / (1 + |x|)` (algebraic approximation)
- `exp2()` → `exp2f()` intrinsic

### Early Exit Optimization

In percussive mode, when the strike envelope is fully closed and no trigger is active, `process()` returns immediately with zero output — skipping all synthesis computation.

### Shared Wavetable Bank

The wavetable bank is allocated once and reference-counted across all Xenostasis instances. Allocation happens in the constructor (UI thread), never in `process()`.

---

## 18. Tips and Workflows

### Quick Start: Drone
Add Xenostasis to the patch. Audio appears on LEFT and RIGHT immediately. Adjust CHAOS to introduce instability. Watch the ENERGY and STORM lights respond.

### Quick Start: Percussion
Patch a clock or trigger source to CLOCK. Each trigger produces a unique percussive hit. Use ENV/WIDTH to control decay length, PUNCH for transient strength, and CHAOS for hit variation.

### The Three-Knob Sweet Spot
The interaction between CHAOS, STABILITY, and HOMEOSTASIS defines the module's personality:

| Setting | Character |
|---------|-----------|
| Low Chaos, High Stability, High Homeostasis | Clean, static, predictable drone |
| Medium Chaos, Medium Stability, Medium Homeostasis | Slowly evolving, organic texture |
| High Chaos, Low Stability, Low Homeostasis | Constantly mutating, unstable, noisy |
| High Chaos, Low Stability, High Homeostasis | Brief storms that always return to calm |
| High Chaos, High Stability, Low Homeostasis | Tense, energy accumulates without resolution |

### Wavetable Selection for Percussion
- **Table 5 (Harsh Noise)** — raw, abrasive industrial percussion
- **Table 6 (Arcade FX)** — laser zaps, bleeps, retro game sounds
- **Table 0 (Feral Machine)** — metallic, processed cymbals and textures
- **Table 2 (Abyssal Alloy)** — heavy, sub-rich kicks with metallic overtones

### Wavetable Selection for Drones
- **Table 3 (Substrate)** — deep, subby, dark ambient
- **Table 1 (Hollow Resonant)** — breathy, vocal, spectral
- **Table 4 (Submerged Monolith)** — oppressive, cinematic, alien
- **Table 7 (Alien Physics Collapse)** — chaotic, abstract, experimental

### Using Cross Modulation
Start with CROSS MOD at 15% (default). As you increase it:
- 0–25%: Subtle spectral interplay; wavetable gains bytebeat-driven shimmer
- 25–50%: Audible FM sidebands; bytebeat textures are imprinted on the wavetable tone
- 50–75%: Heavy entanglement; the two engines merge into a single chaotic entity
- 75–100%: Maximum cross-modulation; timbral chaos dominates

### CV Modulation Techniques
- **CHAOS CV**: Patch an LFO for regular storm/calm cycles, or an envelope follower for input-responsive chaos
- **TABLE CV**: Sweep through all 8 banks with a slow LFO for constantly changing character
- **BB MODE CV**: Sequencer stepping through modes creates rhythmic bytebeat pattern changes
- **BB CHAR CV**: Triangle LFO morphs the bytebeat character continuously
- **TEAR CV**: Envelope follower from a kick drum creates beat-synced glitch

### Minimal Percussion Patch
Clock → CLOCK. Set CHAOS 30%, PUNCH 80%, ENV/WIDTH 20% (short), BB VOL 0, FM VOL 100%, TABLE to 5 (Harsh). Produces punchy, unique FM percussion hits.

### Ambient Drone Patch
No clock. TABLE 3 (Substrate), CHAOS 15%, STABILITY 30%, HOMEOSTASIS 40%, CROSS 25%, WT VOL 70%, BB VOL 50%, FM VOL 30%. Filter Ladder at CUTOFF 60%, RES 30%. Produces a slowly evolving, deep ambient drone.

### Integration with Septagon
Patch Septagon's KICK gate to Xenostasis CLOCK for a kick-drum voice. Different CHAOS and TABLE settings on each Septagon clock output create a full kit from multiple Xenostasis instances.

### Integration with Trigonomicon
Use Trigonomicon's accent or velocity output to modulate Xenostasis's CHAOS CV. Louder hits produce more chaotic, unstable percussion.

### Integration with Minimalith
Mix Xenostasis drone output with Minimalith FM output. Use the same V/OCT source for both. Xenostasis provides evolving texture underneath Minimalith's pitched FM.
