# Aetherion — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Signal Flow](#2-signal-flow)
3. [Controls](#3-controls)
4. [Inputs and Outputs](#4-inputs-and-outputs)
5. [Mode Switch](#5-mode-switch)
6. [Shimmer](#6-shimmer)
7. [Freeze](#7-freeze)
8. [Swell Envelope Follower](#8-swell-envelope-follower)
9. [Tips and Workflows](#9-tips-and-workflows)

---

## 1. Overview

**Aetherion** is a **10 HP stereo hall reverb** with lo-fi tape degradation in the feedback loop. It is designed for ambient, drone, and texture work where the reverb tail itself becomes a musical element.

Key features:

- 8-line feedback delay network (FDN) with Hadamard mixing
- Adjustable predelay (0–250 ms)
- Independent hi-damp and lo-damp tone shaping inside the feedback loop
- Three character modes: Clean, Warm, Degrade
- Shimmer pitch shifting (octave + fifth) with stereo spread
- Freeze: hold the current reverb tail indefinitely
- Swell: built-in envelope follower with adjustable rise/fall and 0–10 V output
- Lo-Fi knob with CV control for tape-style saturation and wow/flutter
- Equal-power dry/wet mix

---

## 2. Signal Flow

```
IN L ──┐
       ├─→ [FREEZE gate] ─→ [SHIMMER inject] ─→ [PREDELAY]
IN R ──┘       │                                      │
          (dry path)                              [FDN reverb]
               │                                      │
               │              ┌── HI DAMP (LP) ──┐    │
               │              │   LO DAMP (HP)    │    │
               │              │   WowFlutter      │    │
               │              │   Hadamard mix     │    │
               │              └───────────────────┘    │
               │                                      │
               │                              [LO-FI saturation]
               │                                      │
               └──── [EQUAL-POWER MIX] ───────────────┘
                              │
                         OUT L / OUT R
```

- **Right input** is normalled to the left input when unpatched.
- **Freeze** zeros the reverb feed while keeping the dry path live.
- **Shimmer** feeds pitch-shifted wet signal back into the reverb input.
- **Lo-fi saturation** is applied at the output stage, not inside the FDN loop.

---

## 3. Controls

### Top Knobs (Rows 1–3)

| Knob | Range | Default | Description |
|------|-------|---------|-------------|
| **PREDELAY** | 0–250 ms | 20 ms | Delay before the signal enters the reverb. Adds space between the dry sound and the onset of the reverb tail. |
| **SIZE** | 0.5×–2.0× | 1.0× | Scales all FDN delay line lengths. Lower values produce smaller, tighter rooms; higher values produce vast halls. |
| **DECAY** | 0.05–20 s | 2 s | Reverb tail length (RT60). Logarithmic knob travel for musical control across the full range. |
| **HI DAMP** | 500–16,000 Hz | 8,000 Hz | Low-pass filter cutoff inside the feedback loop. Lower values darken the tail over time, simulating air absorption. |
| **LO DAMP** | 20–1,000 Hz | 100 Hz | High-pass filter cutoff inside the feedback loop. Higher values thin out the low end of the tail, preventing muddiness. |
| **LO-FI** | 0–1 | 0 | Tape-style degradation depth. Controls both saturation amount and wow/flutter intensity. Effect depends on the MODE switch. |

### Row 4

| Control | Type | Description |
|---------|------|-------------|
| **MIX** | Knob (0–1) | Equal-power dry/wet crossfade. 0 = fully dry, 1 = fully wet. At 0.5, both signals are at equal power (−3 dB each). |
| **SHIMMER** | Toggle switch | Enables the shimmer pitch shifter (octave + fifth intervals fed back into the reverb). |
| **MODE** | 3-position switch | Selects reverb character: Clean, Warm, or Degrade. See [Mode Switch](#5-mode-switch). |

### Swell Section

| Control | Type | Range | Default | Description |
|---------|------|-------|---------|-------------|
| **FREEZE** | Momentary button | — | Off | Each press toggles freeze on/off. See [Freeze](#7-freeze). |
| **RISE** | Trimpot | 1–2,000 ms | 50 ms | Envelope follower attack time. |
| **FALL** | Trimpot | 10–10,000 ms | 500 ms | Envelope follower release time. |

---

## 4. Inputs and Outputs

### Audio Inputs

| Jack | Description |
|------|-------------|
| **IN L** | Left audio input (±5 V audio rate). |
| **IN R** | Right audio input. When unpatched, normalled to IN L for mono-to-stereo operation. |

### CV Inputs

| Jack | Range | Description |
|------|-------|-------------|
| **FRZ G** | Gate (rising edge) | Freeze gate. Each rising edge toggles freeze on/off. Works alongside the FREEZE button. |
| **DCY CV** | 0–10 V | Adds up to +18 seconds to the Decay setting. |
| **SZ CV** | 0–10 V | Adds up to +1.5 to the Size setting (clamped to 2.0 max). |
| **LFI CV** | 0–10 V | Adds to the Lo-Fi knob depth (0–10 V maps to 0–1 additional depth). |

### Audio Outputs

| Jack | Description |
|------|-------------|
| **OUT L** | Left output (±5 V). Bypassed to IN L when the module is bypassed. |
| **OUT R** | Right output (±5 V). Bypassed to IN R when the module is bypassed. |

### Envelope Output

| Jack | Range | Description |
|------|-------|-------------|
| **OUT** (Swell) | 0–10 V | Envelope follower output. Tracks the amplitude of the dry input signal (not affected by freeze). See [Swell](#8-swell-envelope-follower). |

---

## 5. Mode Switch

The MODE switch selects how the Lo-Fi knob and CV affect the reverb character:

| Position | Name | Behavior |
|----------|------|----------|
| Top | **Clean** | Lo-Fi has no effect. Pure digital reverb with no saturation or tape artifacts. |
| Center | **Warm** | 70% of Lo-Fi depth applied. Adds tape saturation and subtle wow/flutter to the reverb tail. Good for vintage-sounding spaces. |
| Bottom | **Degrade** | 100% of Lo-Fi depth. Full tape degradation with heavy saturation and noticeable pitch warble. Useful for lo-fi ambient, VHS-style textures, and sound design. |

The saturation uses a Padé tanh approximant applied at the reverb output. Drive is gain-normalized so increasing Lo-Fi changes the saturation character without changing the perceived volume.

When the Lo-Fi knob is at zero, Warm and Clean sound identical.

---

## 6. Shimmer

When the SHIMMER switch is on, Aetherion feeds pitch-shifted reverb tail back into the reverb input, creating an ethereal, evolving wash.

Two pitch-shift layers are active simultaneously:

- **Octave** (+12 semitones): dual-head granular pitch shifter with Hann crossfade windows
- **Fifth** (+7 semitones): same technique at a 3:2 pitch ratio

The two layers are stereo-spread in opposite directions — the octave shifts the right channel earlier while the fifth shifts it later. This creates width and separation between the intervals.

A soft saturation stage (`x / (1 + |x|)`) prevents feedback runaway at high decay settings while adding subtle even harmonics.

**Tips:**
- Shimmer works best with medium to long decay times.
- The HI DAMP knob controls how quickly the shimmer's brightness fades — lower values produce a darker, more pad-like shimmer.
- Combine with FREEZE to capture a shimmering chord and hold it indefinitely.

---

## 7. Freeze

Freeze holds the current reverb tail by stopping new audio from entering the FDN. The existing tail continues to recirculate.

**Activation:**
- Press the **FREEZE** button (momentary toggle — each press flips the state)
- Send a rising edge to the **FRZ G** input (also a toggle)
- Both methods work simultaneously

**Behavior during freeze:**
- The dry signal path remains live — you can still hear your input over the frozen reverb.
- The SWELL envelope follower continues to track the dry input.
- The green LED next to the FREEZE button lights when freeze is active.

**Tips:**
- Feed a chord into the reverb, freeze it, then play new notes over the frozen wash.
- Automate freeze on/off with a gate sequence for rhythmic textural effects.
- With SHIMMER on, the frozen tail continues to evolve as pitch-shifted signal recirculates.

---

## 8. Swell Envelope Follower

The SWELL section provides a built-in envelope follower that tracks the amplitude of the dry input signal.

**Signal path:** The mono sum of IN L + IN R is rectified and smoothed with independent attack (RISE) and release (FALL) times, then scaled to 0–10 V.

**Important:** SWELL always tracks the dry input, even during freeze. This means you can use the envelope of new playing to modulate other modules while the reverb tail is frozen.

**RISE** controls how quickly the envelope responds to increasing levels. Short values (1–10 ms) track transients tightly; longer values (100+ ms) produce slow swells.

**FALL** controls how quickly the envelope decays after the input level drops. Short values (10–50 ms) follow the input closely; longer values (1,000+ ms) create sustained, slowly-fading modulation.

**Use cases:**
- Patch SWELL OUT to a filter cutoff for reverb-responsive filtering on another voice.
- Use as a VCA control signal to duck or swell a pad in response to your playing.
- Drive an LFO rate or effect depth from the input dynamics.

---

## 9. Tips and Workflows

### Ambient Pad Machine
Set DECAY to 10+ seconds, SIZE to 1.5–2.0, SHIMMER on, MIX fully wet. Play slow chords and let the shimmer build layers. Use HI DAMP around 4,000 Hz to keep the shimmer from getting harsh.

### Lo-Fi Tape Room
Set MODE to Warm, LO-FI around 0.4, DECAY 1–3 seconds, PREDELAY 30–60 ms. Produces a vintage spring/tape reverb character.

### Drone Freeze
Feed a textured sound source into Aetherion with long DECAY and SHIMMER on. Freeze when the tail sounds interesting. Sweep SIZE slowly to shift the frozen tail's pitch character.

### Rhythmic Gating
Patch a clock or gate sequence to FRZ G. The reverb alternates between building and holding, creating a pumping or gated reverb effect.

### Sidechain Modulation
Use SWELL OUT to control a VCA on another channel. Set RISE short and FALL long for a compressor-like sidechain pumping effect driven by the reverb input.

### VHS Degradation
Set MODE to Degrade, LO-FI to 0.7–1.0, DECAY to 5+ seconds. The reverb tail accumulates saturation and pitch warble on each pass through the feedback loop, progressively degrading like an old tape recording.
