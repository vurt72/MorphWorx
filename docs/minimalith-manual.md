# Minimalith — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Attribution and Origins](#2-attribution-and-origins)
3. [Changes and Additions](#3-changes-and-additions)
4. [Signal Flow](#4-signal-flow)
5. [Panel Sections](#5-panel-sections)
6. [Controls](#6-controls)
7. [Inputs](#7-inputs)
8. [Outputs](#8-outputs)
9. [FM Algorithms](#9-fm-algorithms)
10. [FM Editor Trims](#10-fm-editor-trims)
11. [TEAR Effect](#11-tear-effect)
12. [Sidechain Compressor](#12-sidechain-compressor)
13. [Bank Management](#13-bank-management)
14. [User Waveforms](#14-user-waveforms)
15. [Preset Morph (VCV Rack Only)](#15-preset-morph-vcv-rack-only)
16. [MetaModule Notes](#16-metamodule-notes)
17. [Tips and Workflows](#17-tips-and-workflows)

---

## 1. Overview

**Minimalith** is a **16 HP FM synthesizer voice** built on the PreenFM2 engine. It loads PreenFM2 `.bnk` bank files and plays their patches as a monophonic voice module controlled by V/oct pitch and gate inputs. On top of the original PreenFM2 synthesis, Minimalith adds five real-time performance macro trims (Algorithm, Evolution, IM Scan, Drift, Tear), a built-in sidechain compressor, preset morphing, and full CV control over all parameters.

Key features:

- Full PreenFM2 6-operator FM synthesis engine with 28 algorithms
- Loads native PreenFM2 `.bnk` bank files (up to 128 patches per bank)
- Five FM Editor macro trims that reshape patches without editing individual parameters
- TEAR effect — post-engine wavefold + noise + comb coloration
- Built-in sidechain compressor with attack/release control
- Preset Morph — generates hybrid patches by blending two random presets
- Per-preset trim state recall (saved across sessions)
- CV modulation for Algorithm, IM Scan, Evolution, Drift, Tear, and Random Preset
- Monophonic with glide (portamento)
- User waveform support (User1–User6 .txt files)
- PreenFM2 bank interop — trims can be baked back into bank files for hardware compatibility

---

## 2. Attribution and Origins

Minimalith is built on the **PreenFM2** open-source digital polyphonic FM synthesizer engine, created by **Xavier Hosxe**. The PreenFM2 is a hardware synthesizer with a 6-operator FM engine, 28 algorithms, modulation matrix, LFOs, step sequencer, and extensive filter/effect options.

- **PreenFM2** — [https://github.com/Ixox/preenfm2](https://github.com/Ixox/preenfm2)
- **Creator** — Xavier Hosxe
- **License** — GNU General Public License v3.0 or later (GPL-3.0-or-later)

Some PreenFM2 source files also carry copyright notices from Emilie Gillet (Copyright 2009–2011) for portions originally derived from early Mutable Instruments firmware, used under GPL-3.0.

Full license text and attribution details are in [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

---

## 3. Changes and Additions

The MorphWorx port makes the following changes and additions relative to the original PreenFM2 engine:

### Architecture Changes

- **Monophonic voice module** — the PreenFM2's polyphonic multi-timbral architecture is reduced to a single monophonic voice, controlled by standard Eurorack V/oct and gate signals
- **VCV Rack integration** — native VCV Rack 2 module with PNG faceplate, custom port/knob widgets, NanoVG text rendering, context menu, and full serialization
- **4ms MetaModule support** — cross-compiled for ARM with real-time safety: no heap allocation in the audio thread, soft-takeover for physical trimpots, debounced preset switching, and control-rate parameter reads
- **Block-based processing** — PreenFM2's native block size is retained internally; the wrapper handles per-sample I/O to/from VCV Rack's audio stream

### New Features

- **Five FM Editor macro trims** — ALGO, EVO, IM SCAN, DRIFT, and TEAR provide high-level control over patch character without editing individual PreenFM2 parameters. See [FM Editor Trims](#10-fm-editor-trims).
- **TEAR effect** — a lightweight post-engine effect combining wavefold, comb delay, and noise injection. Not part of the original PreenFM2. See [TEAR Effect](#11-tear-effect).
- **Built-in sidechain compressor** — envelope-follower driven ducking with adjustable attack, release, and amount. See [Sidechain Compressor](#12-sidechain-compressor).
- **Preset Morph** (VCV Rack only) — generates new hybrid patches by stochastically blending two random bank presets. See [Preset Morph](#15-preset-morph-vcv-rack-only).
- **Per-preset trim sidecar** — trim positions for EVO, IM, Drift, Tear, and Tear Fold are saved per bank slot in a JSON sidecar file and recalled on preset change
- **CV inputs** — six CV inputs (TEAR, ALGO, EVO, IM SCAN, DRIFT, RANDOM) for real-time modulation
- **Octave trim** — ±5 octave offset applied to the pitch input
- **Panic button** — immediately kills stuck voices and resets engine state
- **Bank export with TEAR interop** — TEAR settings can be baked into PreenFM2 effect slots for hardware compatibility

### Behavioral Changes

- **Gate/pitch model** — uses standard Eurorack 1V/oct pitch and gate (≥1 V) rather than MIDI note numbers; pitch retrigger detection at ~1 semitone threshold
- **Glide** — engine glide operates in log-frequency space for musically correct portamento
- **Trim neutrality** — IM SCAN and DRIFT trims are neutral at center position (0.5); turning away from center increases modulation in both directions
- **Deferred engine init** — engine initialization is deferred to `onAdd()` to prevent crashes from premature initialization in VCV Rack's module lifecycle

---

## 4. Signal Flow

```
              ┌────────────────────────────────────┐
V/OCT ──→ [Octave Trim] ──→ [Frequency Conversion] │
              │                    │                │
GATE  ──→ [Note On/Off] ──→ [PreenFM2 Engine]      │
              │                    │                │
              │    ┌───────────────┤                │
              │    │ 6 Operators   │                │
              │    │ 28 Algorithms │                │
  ALGO CV ──→─┤    │ Mod Matrix    │                │
  EVO  CV ──→─┤    │ 3 LFOs       │                │
  IM   CV ──→─┤    │ 6 Envelopes  │                │
  DRIFT CV ──→┤    │ Filter/FX    │                │
              │    └───────┬───────┘                │
              │            │ stereo audio           │
              │    ┌───────┴───────┐                │
  TEAR CV ──→─┤    │ TEAR Effect   │                │
              │    │ fold+comb+    │                │
              │    │ noise         │                │
              │    └───────┬───────┘                │
              │            │                        │
              │    ┌───────┴───────┐                │
  SC IN  ──→──┤    │ Sidechain     │                │
              │    │ Compressor    │                │
              │    └───────┬───────┘                │
              │            │                        │
              │      [Volume]                       │
              │         │  │                        │
              └─────────┼──┼────────────────────────┘
                        │  │
                     LEFT  RIGHT
```

---

## 5. Panel Sections

The 16 HP panel is organized top-to-bottom:

| Section | Contents |
|---------|----------|
| **Display** | LCD showing patch name, FX type, and patch number (e.g., "12 / 64") |
| **Preset** | LOAD button, Prev/Next buttons (VCV) or Preset knob (MetaModule), MORPH button (VCV only), VOLUME, PANIC |
| **FM Editor** | Five macro trims: ALGO, EVO, IM SCAN, DRIFT, TEAR + FOLD switch |
| **CV Inputs** | Six CV jacks: TEAR, ALGO, EVO, IM SCAN, DRIFT, RANDOM |
| **Gate / V/Oct** | GATE input + light, V/OCT input, OCT trim |
| **Outputs** | LEFT and RIGHT audio outputs, SC IN jack, sidechain AMT/ATK/REL trims |

---

## 6. Controls

### Preset Section

| Control | Type | Description |
|---------|------|-------------|
| **LOAD** | Button | Opens a file browser to load a PreenFM2 `.bnk` bank file. |
| **< / >** (VCV) | Buttons | Step through presets one at a time (with 120 ms cooldown). |
| **PRESET** (MetaModule) | Knob | Continuous preset select (0–1 maps across all bank slots). |
| **MORPH** (VCV only) | Button | Generates a new hybrid patch by blending two random bank presets. |
| **VOLUME** | Trimpot (0–2) | Output volume multiplier. 1.0 = unity gain, 2.0 = +6 dB. |
| **PANIC** | Button | Kills all stuck voices, resets engine state, clears gate tracking. |

### FM Editor Trims

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **ALGO** | 0–27 (snapped) | Patch default | Selects one of 28 PreenFM2 FM algorithms. See [FM Algorithms](#9-fm-algorithms). |
| **EVO** | 0–1 | 0 | Evolution — morphs oscillator shapes and envelope times. See [FM Editor Trims](#10-fm-editor-trims). |
| **IM SCAN** | 0–1 (center=neutral) | 0.5 | Scans a spotlight across modulation indices IM1–IM5. Center = no change. |
| **DRIFT** | 0–1 (center=neutral) | 0.5 | Spreads operator detuning for chorus/unison effects. Center = no change. |
| **TEAR** | 0–1 | 0 | Post-engine wavefold + comb + noise coloration. 0 = bypass. |
| **FOLD** | Switch (Off/On) | On | Enables/disables the wavefold component of TEAR. |

### Other Controls

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **OCT** | −5 to +5 (snapped) | 0 | Octave offset applied to the V/OCT input. |
| **SC AMT** | 0–1 | 0 | Sidechain compression amount (0 = off, 1 = full ducking). |
| **SC ATK** | 0–1 | 0.18 | Sidechain attack speed (exponential mapping: 0.5–50 ms). |
| **SC REL** | 0–1 | 0.38 | Sidechain release speed (exponential mapping: 20–500 ms). |

---

## 7. Inputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **GATE** | Gate | Gate (≥1 V on) | Note gate input. Rising edge triggers note-on; falling edge triggers note-off. |
| **V/OCT** | V/Oct Pitch | 1V/oct | Pitch input. 0 V = C4 (261.6 Hz). Continuous while gate is held (enables glide). |
| **TEAR** | Tear CV | −5 V to +5 V | Adds to the TEAR trim value (scaled ×0.2). Combined value clamped to 0–1. |
| **ALGO** | Algorithm CV | −5 V to +5 V | Sweeps through all 28 algorithms. −5 V = Algo 1, +5 V = Algo 28. |
| **EVO** | Evolution CV | 0–10 V | Unipolar evolution control. 0 V = no evolution, 10 V = maximum. Overrides EVO trim when patched. |
| **IM SCAN** | IM Scanner CV | −5 V to +5 V | Bipolar scan across modulation indices IM1–IM5. Overrides IM SCAN trim when patched. |
| **DRIFT** | Drift CV | −5 V to +5 V | Bipolar operator detune spread. Overrides DRIFT trim when patched. |
| **RANDOM** | Random Preset Trigger | Gate/trigger | Rising edge loads a random preset from the current bank. On MetaModule, a 0.5 s cooldown prevents spam. |
| **SC IN** | Sidechain Input | 0–10 V | Envelope signal for sidechain compression. Higher voltage = more ducking. |

---

## 8. Outputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **LEFT** | Left Audio | ±10 V | Left channel audio output (stereo FM engine output + TEAR + sidechain). |
| **RIGHT** | Right Audio | ±10 V | Right channel audio output. |

---

## 9. FM Algorithms

The PreenFM2 engine provides 28 FM algorithms, selectable via the ALGO trim or ALGO CV input. Each algorithm defines how the six operators (oscillators) are connected — which operators modulate which, and which operators output audio directly.

Algorithms range from simple 2-operator setups to complex 6-operator feedback networks. The algorithm number (0–27) maps directly to the PreenFM2's algorithm set.

**Common algorithm characteristics:**

- **Algo 0–5**: Simple carrier-modulator pairs and stacks. Good starting points for classic FM sounds (electric piano, bells, bass).
- **Algo 6–15**: Branching topologies where multiple modulators feed a single carrier, or carriers share modulators. Rich harmonic interactions.
- **Algo 16–23**: Complex parallel and feedback topologies. High harmonic density; good for pads, textures, and metallic timbres.
- **Algo 24–27**: Maximum complexity topologies with extensive cross-modulation. Capable of extreme, noise-like timbres at high modulation indices.

The ALGO CV input sweeps continuously across all 28 algorithms, enabling algorithmic morphing during performance. Because algorithm changes are discrete (integer-snapped), the transition points may produce clicks at audio rate — for smooth morphing, modulate at control rate (slow LFO or stepped CV).

---

## 10. FM Editor Trims

The FM Editor provides five macro controls that reshape PreenFM2 patches at a high level without requiring access to the original patch editor. Each trim modulates multiple internal parameters simultaneously.

### ALGO (Algorithm)

Directly selects one of 28 FM algorithms. On patch load, the trim snaps to the patch's default algorithm. Turning the knob overrides the algorithm for the current patch.

### EVO (Evolution)

A unipolar (0–1) macro that simultaneously morphs:

- **Oscillator shapes** — sweeps through Sin → Saw → Square → Random as EVO increases (in four equal zones: 0–0.25, 0.25–0.50, 0.50–0.75, 0.75–1.0)
- **Attack times** — shortened exponentially (×0.05 at EVO = 1.0), making attacks percussive
- **Decay times** — shortened exponentially (×0.10 at EVO = 1.0), creating shorter, punchier envelopes
- **Release times** — lengthened exponentially (×5.0 at EVO = 1.0), creating ambient tails

At EVO = 0, the patch plays with its original parameters. As EVO increases, the sound becomes progressively more transformed — shorter, brighter attacks with longer, textured releases.

### IM SCAN (Modulation Index Scanner)

A bipolar-from-center (0–1, center = 0.5 = neutral) macro that spotlights one modulation index pair at a time while suppressing others. The five modulation indices (IM1–IM5) are spread evenly across the 0–1 range:

| Trim Position | Spotlit Index | Effect |
|---------------|---------------|--------|
| 0.0 | IM1 | Maximum IM1, others suppressed |
| 0.25 | IM2 | Maximum IM2 |
| 0.5 (center) | Neutral | No change from patch defaults |
| 0.75 | IM4 | Maximum IM4 |
| 1.0 | IM5 | Maximum IM5 |

The transition between indices uses smoothstep blending with a Gaussian-like bump function, so intermediate positions create smooth crossfades between adjacent index pairs. The modulation amount scales from 0 at center to full at the extremes.

Velocity sensitivity for each index pair is modulated in parallel, offset by half a zone width.

### DRIFT (Operator Detune Spread)

A bipolar-from-center (0–1, center = 0.5 = neutral) macro that applies asymmetric detuning across all six operators. Each operator receives a unique spread pattern:

| Operator | Spread Factor |
|----------|---------------|
| Osc 1 | +1.00 |
| Osc 2 | −0.73 |
| Osc 3 | +1.27 |
| Osc 4 | −1.13 |
| Osc 5 | +0.53 |
| Osc 6 | −0.87 |

Maximum detune is ±6 semitones per operator. The spread factors ensure that detuning creates a wide, chorus-like stereo field rather than a simple frequency shift. A power curve (exponent 0.75) is applied so small movements from center are subtle while extreme positions are dramatic.

At center (0.5), all operators use their original patch detuning. Turning clockwise or counterclockwise from center applies increasing spread in opposite directions.

### TEAR

See [TEAR Effect](#11-tear-effect).

---

## 11. TEAR Effect

TEAR is a lightweight post-engine effect that adds lo-fi coloration to the FM output. It is **not** part of the original PreenFM2 — it is a MorphWorx addition. TEAR consists of three cascaded stages:

### 1. Wavefold (when FOLD switch is On)

The stereo signal is driven through `sin(x * drive)` where drive scales from 1.0 (subtle) to 6.5 (aggressive) with TEAR amount. This produces harmonic folding that adds overtones and grit.

When FOLD is Off, this stage is bypassed and only the comb + noise stages apply.

### 2. Noise Injection

A small amount of xorshift PRNG noise is mixed into the signal. The noise level scales from 0.4% at low TEAR to 3.2% at maximum. This adds analog-style hiss and imperfection.

### 3. Short Comb Delay

A 128-sample circular delay buffer with feedback creates a short comb filter effect. The delay length (8–128 samples) and feedback amount (8–63%) both scale with TEAR. The result is a metallic, resonant coloration reminiscent of degraded digital audio.

The three stages are mixed into the dry signal proportionally to the TEAR amount. At TEAR = 0, the effect is completely bypassed (zero CPU cost). The final output passes through `tanh()` soft clipping.

TEAR can be CV-modulated via the TEAR input (−5 V to +5 V, scaled ×0.2 and added to the trim).

### TEAR Bank Interop

TEAR settings are stored in the PreenFM2 effect slot (filter type 49) when saving banks. This makes TEAR-enhanced presets portable to other PreenFM2-compatible systems. A legacy fallback mode (type 48) is available via the context menu for older firmware.

---

## 12. Sidechain Compressor

Minimalith includes a built-in envelope-follower sidechain compressor on its audio output. This enables ducking the FM synth in response to an external signal (typically a kick drum or bass trigger).

### Controls

| Control | Range | Description |
|---------|-------|-------------|
| **SC IN** (input) | 0–10 V | The sidechain envelope signal. Higher voltage = more ducking. |
| **AMT** | 0–1 | Compression amount. 0 = no ducking, 1 = full ducking (output drops to silence at maximum input). |
| **ATK** | 0–1 | Attack speed. Maps exponentially from 0.5 ms (fast, snappy response) to 50 ms (slow, soft onset). |
| **REL** | 0–1 | Release speed. Maps exponentially from 20 ms (fast recovery) to 500 ms (slow, pumping recovery). |

### Behavior

The sidechain follower tracks the SC IN voltage with asymmetric attack/release smoothing. The envelope is squared before application for a more aggressive knee. The resulting gain reduction is:

$$\text{gain} = 1 - \text{env}^2 \times \text{amount}$$

When SC IN is unpatched, the compressor is inactive regardless of AMT setting.

---

## 13. Bank Management

### Loading Banks

Click **LOAD** (or use the right-click context menu → "Load PreenFM2 Bank") to open a file browser. Select any PreenFM2 `.bnk` file. The bank is loaded immediately, and the first patch is selected.

Minimalith ships with a **Default.bnk** factory bank that loads automatically on first use.

### Navigating Presets

- **VCV Rack**: Use the **< / >** buttons to step through presets one at a time. A 120 ms cooldown prevents accidental rapid stepping.
- **MetaModule**: Turn the **PRESET** knob to sweep through all patches in the bank.
- **Random**: Send a trigger to the **RANDOM** input to jump to a random patch.

### Saving Presets

Right-click the module and select:

- **Save current preset to bank slot** — bakes the current trim settings and algorithm override into the selected bank slot and writes the `.bnk` file to disk.
- **Save bank** — writes the entire bank (including all TEAR interop data) to the current file path.
- **Save bank as...** (VCV Rack only) — saves the bank to a new file path.

### Copy / Paste (VCV Rack Only)

- **Copy preset** — captures the current patch parameter snapshot (including baked trims) to a clipboard.
- **Paste preset to current slot** — overwrites the current bank slot with the copied preset.

### Rename Preset (VCV Rack Only)

Right-click → type a new name in the text field → press Enter. The name is saved to the bank file (12 character limit, matching PreenFM2's format).

### Trim Sidecar Persistence

Trim positions (EVO, IM SCAN, DRIFT, TEAR, TEAR FOLD) are saved per bank slot in `MorphWorx/MinimalithTrims.json` in the Rack user directory (or `minimalith/MinimalithTrims.json` on MetaModule SD card). When you switch presets and return, your trim positions are restored.

---

## 14. User Waveforms

The PreenFM2 engine supports six user-definable waveforms (User1–User6). These are loaded from `.txt` files containing 1024 sample values.

### File Locations

Minimalith searches for user waveforms in the following directories (in order):

**VCV Rack:**
1. Plugin bundled: `<plugin>/userwaveforms/`
2. User directory: `<Rack user>/MorphWorx/userwaveforms/`
3. Legacy paths: `<Rack user>/Bemushroomed/userwaveforms/`, `<Rack user>/pfm2/waveform/`

**MetaModule:**
1. SD card: `minimalith/userwaveforms/`
2. SD card: `MorphWorx/userwaveforms/`
3. Bundled plugin defaults

### File Format

Each user waveform file (`usr1.txt` through `usr6.txt`) contains 1024 floating-point sample values defining one cycle of the waveform. See the `userwaveforms/` directory in the MorphWorx plugin for example files.

### Missing Waveform Detection

If a patch references a User waveform that is not loaded, the display shows a warning (e.g., "MISSING USR1+USR3") and the affected operators are silenced to prevent undefined behavior.

---

## 15. Preset Morph (VCV Rack Only)

The **MORPH** button (VCV Rack only) generates a new hybrid patch by blending two randomly selected bank presets. The morph algorithm:

1. Picks two random presets from the current bank
2. Selects a random blend alpha (35–65% range, avoiding extremes)
3. For **continuous parameters** (modulation indices, mix levels, envelope times, detuning): linearly interpolates between the two presets
4. For **discrete parameters** (algorithm, oscillator shapes, waveform types, LFO shapes, matrix sources/destinations): randomly picks from either parent
5. For **step sequencer data**: each step is randomly inherited from either parent

The result is loaded immediately. The display shows "MORPH A+B" where A and B are the source preset numbers. The morphed patch is ephemeral — navigating away discards it. To keep it, use "Save current preset to bank slot."

---

## 16. MetaModule Notes

### Soft Takeover

On MetaModule, physical trimpots use soft takeover (pickup) behavior. When a preset is loaded, trim positions are captured as baselines. The trimpot must move past a 2% threshold from its baseline before Minimalith responds, preventing accidental parameter jumps on preset change.

### Random Preset Cooldown

The RANDOM trigger input has a 0.5 second (24,000 sample) cooldown on MetaModule to prevent CPU spikes from rapid-fire trigger sources. In VCV Rack, there is no cooldown.

### Preset Display

On MetaModule, the preset name and patch number are shown via the MetaModule dynamic text display system. Two-line format: preset name on line 1, patch number on line 2.

### File Paths

MetaModule looks for bank files and user waveforms on the SD card (`sdc:/`), USB (`usb:/`), and internal storage (`nor:/`) in a defined search order. Banks can be placed in `minimalith/` or `MorphWorx/` directories on any volume.

---

## 17. Tips and Workflows

### Quick Start
Load the factory Default.bnk (happens automatically on first use). Patch a V/oct source to V/OCT and a gate to GATE. Audio appears on LEFT and RIGHT. Use **< / >** to browse presets.

### Sound Design with Trims
Start with any preset and explore the five FM Editor trims:

- **ALGO** — change the FM topology to find fundamentally different timbral families
- **EVO** — push right for percussive, bright attacks with long ambient tails; ideal for plucks and mallets
- **IM SCAN** — sweep slowly for moving, vocal-like timbral animation
- **DRIFT** — add subtle width (near center) or dramatic detuned chorus (at extremes)
- **TEAR** — add lo-fi character, from subtle warmth to aggressive digital destruction

### CV Animation
Patch slow LFOs to the CV inputs for evolving textures:

- **EVO CV** — an envelope follower creates velocity-responsive timbre (brighter on louder notes)
- **IM SCAN CV** — a triangle LFO creates sweeping harmonic animation
- **DRIFT CV** — a random S&H creates unpredictable detuning per note
- **ALGO CV** — a stepped sequencer creates rhythmic algorithm changes

### Sidechain Pumping
Patch a kick drum trigger through an envelope generator into SC IN. Set AMT to 0.6–0.8, ATK short (0.1), REL medium (0.4). The FM pad ducks on each kick, creating a pumping electronic music effect.

### Generative Preset Exploration
Patch a clock divider (firing once every 4–8 bars) into RANDOM. Minimalith cycles through bank presets automatically, creating evolving timbral journeys. With a well-curated bank, this makes an effective generative texture source.

### Morph Chains (VCV Rack)
Click MORPH repeatedly to generate hybrid patches. When you find one you like, save it to a bank slot. Build an entire custom bank from morphed presets.

### Integration with SlideWyrm
Patch SlideWyrm's PITCH output to Minimalith's V/OCT, and SlideWyrm's GATE to Minimalith's GATE. The acid sequencer drives the FM synth — set EVO high and IM SCAN to a slow LFO for evolving acid-FM textures.

### Integration with Trigonomicon
Use Trigonomicon's Bass CV (BCV) and Bass Trigger (BTRIG) to drive Minimalith as a bass voice. The drum-locked triggering creates grooves that lock perfectly with the percussion.

### Building a Custom Bank
1. Load any bank as a starting point
2. Navigate to a slot, adjust trims to taste
3. Right-click → "Save current preset to bank slot"
4. "Save bank as..." to create a new `.bnk` file
5. The bank is compatible with original PreenFM2 hardware
