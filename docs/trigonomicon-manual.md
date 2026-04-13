# Trigonomicon — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Signal Flow](#2-signal-flow)
3. [Controls](#3-controls)
4. [Inputs](#4-inputs)
5. [Outputs](#5-outputs)
6. [Pattern Bank](#6-pattern-bank)
7. [Mutation Engine](#7-mutation-engine)
8. [Wild Mode](#8-wild-mode)
9. [Fill Input](#9-fill-input)
10. [Bassline Generator](#10-bassline-generator)
11. [Accent Bus](#11-accent-bus)
12. [Clock Resolution](#12-clock-resolution)
13. [Tips and Workflows](#13-tips-and-workflows)

---

## 1. Overview

**Trigonomicon** is a **10 HP generative drum trigger pattern generator** with musical mutation, a built-in bassline sequencer, and per-voice accent output. It reads from a bank of 130 probability-based patterns and fires trigger pulses on clock input. Each of the six drum voices has its own independent step counter and pattern length, enabling polymetric rhythms and odd time signatures out of the box.

Key features:

- 130 genre-organized patterns spanning Amen/Breakbeat, Jungle, Breakcore, Drum & Bass, UK Garage, Dub/Reggae, IDM, EBM, Electro, Techno, Gqom, and Trap
- Probability-based sequencing — steps fire stochastically, creating organic variation
- Four-layer mutation engine with musically-aware groove enforcement
- Wild mode — six additional chaotic behaviors for extreme live performance
- Fill input (gate + voltage) for breakdown/build-up transitions
- Built-in generative bassline with scale quantization, motif phrases, and TB-303-style portamento
- Per-voice polyphonic accent output for dynamic velocity control
- Three clock resolution modes (÷2, 1:1, ×2)
- Polymetric patterns — each voice can run at its own step length
- Genre-aware LCD display showing the current pattern and style

---

## 2. Signal Flow

```
CLOCK ──→ [Clock Resolution] ──→ [Step Advance]
                                       │
                            ┌──────────┴──────────┐
                            │     Per-Voice        │
                            │   Step Counters      │
                            │  (independent wrap)  │
                            └──────────┬──────────┘
                                       │
                            ┌──────────┴──────────┐
                            │   Mutation Engine    │
                            │                      │
                            │  1. Segment DNA Swap │
                            │  2. Pattern Crossfade│
                            │  3. Density Injection│
                            │  4. Micro Timing     │
                            │  + Wild Layers       │
                            └──────────┬──────────┘
                                       │
                        ┌──── Groove Enforcement ────┐
                        │  Snare anchor, kick/snare  │
                        │  separation, density caps  │
                        └──────────┬─────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                     │
        [Fill Gating]      [Ratchet Engine]      [Bass S&H + Motif]
              │                    │                     │
     ┌────┬────┬────┬────┬────┐   │            ┌────────┴────────┐
     │KICK│SN1 │SN2 │ CH │ OH │RIDE           BTRIG            BCV
     └────┴────┴────┴────┴────┘                (trigger)     (1V/oct)
                    │
              [Accent Bus]
              (6ch poly out)
```

- Each voice wraps at its own pattern length, enabling polymetric interaction.
- Groove enforcement runs at mutation rebuild time (not per-sample) to preserve real-time performance.
- The bassline generator derives its timing from drum triggers — it plays when the drums play.

---

## 3. Controls

| Control | Type | Range | Default | Description |
|---------|------|-------|---------|-------------|
| **CLKRES** | 3-position switch | ÷2 / 1 / ×2 | 1 | Clock resolution multiplier. See [Clock Resolution](#12-clock-resolution). |
| **PAT** | Knob (snapped) | 0–129 | 0 | Selects one of 130 patterns from the bank. Integer-snapped. Also controllable via CV. |
| **MUTATE** | Knob | 0–100% | 0% | Mutation depth. Controls four progressive layers of musical variation. See [Mutation Engine](#7-mutation-engine). |
| **BASS** | Knob (snapped) | 0–9 | 1 | Selects the scale for the built-in bassline generator. See [Bassline Generator](#10-bassline-generator). |
| **WILD** | Toggle switch | Off / On | Off | Enables Wild mode, adding six additional chaotic mutation behaviors. See [Wild Mode](#8-wild-mode). |

---

## 4. Inputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **CLK** | Clock | Gate/trigger | Main clock input. Each rising edge advances the pattern by one step (or as modified by the clock resolution setting). |
| **FILL** | Fill | Gate + voltage (0–10 V) | While the gate is held high, the module enters a breakdown/build-up mode. The voltage determines which voices are muted. On gate release, a transition fill fires. See [Fill Input](#9-fill-input). |
| **RST** | Reset | Gate/trigger | Resets all voice step counters to zero, clears ratchets and fills, and resets the bassline. Does not change the donor pattern or force a new mutation. |
| **MCV** | Mutate CV | 0–10 V | CV control for mutation depth. Added to the MUTATE knob value (clamped to 0–1 combined). |
| **CRS** | Clock Res CV | 0–10 V | CV control for clock resolution (0 V = 1:1, 5 V = ÷2, 10 V = ×2). Overrides the CLKRES switch when patched. |
| **OCT** | Bass Octave CV | −2 V to +1 V | Transposes the bassline by integer octaves (1V/oct, snapped). Applied at musical boundaries (every 32 clocks). |
| **PAT** | Pattern Select CV | 0–10 V | CV control for pattern selection (0 V = pattern 0, 10 V = pattern 129). When both CLK and PAT CV are connected, pattern changes are quantized to the clock grid. |

---

## 5. Outputs

### Drum Trigger Outputs

All drum outputs are 0/10 V trigger pulses.

| Jack | Label | Description |
|------|-------|-------------|
| **KICK** | Kick | Kick drum trigger. Pulse duration: 12 ms (longer for compatibility with hardware drum modules). |
| **SN1** | Snare 1 | Primary snare trigger. Pulse duration: 5 ms. |
| **SN2** | Snare 2 | Ghost snare / clap trigger. Pulse duration: 5 ms. |
| **CH** | Closed HiHat | Closed hihat trigger. Fires a choke on the open hihat channel. Pulse duration: 5 ms. |
| **OH** | Open HiHat | Open hihat trigger. Choked by closed hihat. Pulse duration: 100 ms. |
| **RIDE** | Ride/Crash | Ride cymbal or crash trigger. Pulse duration: 5 ms. |

### Bassline Outputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **BTRIG** | Bass Trigger | 0/10 V gate | Gate output for the bassline. Gate length is proportional to the clock interval. |
| **BCV** | Bass CV | V/oct | 1V/oct pitch CV for the bassline. Tracks the quantized bass note including portamento, octave decoration, and octave CV offset. |

### Accent Output

| Jack | Label | Description |
|------|-------|-------------|
| **ACC** | Accent Bus | 6-channel polyphonic output (0–10 V). Each channel corresponds to one drum voice (ch 1 = Kick, ch 2 = Snare 1, ch 3 = Snare 2, ch 4 = Closed HH, ch 5 = Open HH, ch 6 = Ride). Voltage reflects the musical importance of each hit — strong beats, anchor steps, and authored high-probability hits produce higher voltages. See [Accent Bus](#11-accent-bus). |

---

## 6. Pattern Bank

Trigonomicon contains 130 patterns organized by genre:

| Patterns | Genre | Description |
|----------|-------|-------------|
| 0–5 | Amen / Breakbeat | Flagship breakbeat patterns based on classic Amen break variations |
| 6–7 | Jungle | Flagship jungle patterns with syncopated hats and displaced snares |
| 8–9 | Breakcore | Flagship breakcore with high ghost density and chaotic kick placement |
| 10–12 | Amen (expanded) | Additional Amen patterns with Ride/Crash support |
| 13–15 | Jungle (expanded) | Additional Jungle patterns with Ride/Crash |
| 16–19 | Breakcore (expanded) | Additional Breakcore patterns with Ride/Crash |
| 20–39 | Jungle / Breakcore / Amen | Mixed expansion bank with cross-genre variations |
| 40–44 | Drum & Bass | Liquid and neurofunk-influenced D&B patterns |
| 45–49 | UK Garage | 2-step and 4/4 garage rhythms |
| 50–54 | Dub / Reggae | Sparse, space-heavy dub and reggae rhythms |
| 55–59 | IDM / Drillcore | Experimental time signatures and fractured rhythms |
| 60–79 | EBM / Industrial | Industrial dance and EBM patterns |
| 80–89 | Electro | Detroit electro and synth-funk syncopation |
| 90–99 | Techno | Driving techno patterns (most are snareless) |
| 100–119 | Gqom | Durban broken beat and Gqom rhythms |
| 120–129 | Trap | Atlanta trap, UK drill, bounce, phonk, and hybrid trap |

Each pattern stores per-step probability values (0.0–1.0) for all six voices. Values of 1.0 always fire, values near 0 rarely fire, and values in between provide organic variation. Many patterns use polymetric voice lengths — for example, a pattern might have 16 kick steps but 14 hihat steps, creating evolving phase relationships.

The LCD display at the top of the module shows the current pattern number and genre name.

---

## 7. Mutation Engine

The MUTATE knob (and MCV input) controls four progressive layers of musical variation. As the combined value increases from 0% to 100%, layers activate in sequence:

### Layer 1: Micro Timing Variation (0–40%)

Subtle per-step probability perturbation. Hits that were likely become slightly less certain, and ghost notes become slightly more likely. The original groove character is preserved.

### Layer 2: Pattern Crossfade Blend (25–65%)

On each step, there is a rolling chance that the module reads from the mutated (segment-swapped) pattern buffer instead of the base pattern. This creates gradual morphing between the selected pattern and its genetically related donor.

### Layer 3: Segment DNA Swap (50–85%)

The pattern is divided into four segments per voice. Segments are probabilistically replaced with corresponding segments from a donor pattern chosen from a nearby genre neighborhood (typically ±5 patterns). For Jungle and Breakcore families, the donor may come from a triplet pattern pool to inject rhythmic flavor.

**Hard mutation:** A sharp upward spike on the MCV input (jump > 0.65 from a value below 0.25) triggers a hard mutation — the module immediately chooses a new donor pattern and re-rolls all segment swap thresholds. This is useful for creating dramatic live transitions.

### Layer 4: Chaos — Density Injection & Tracker FX (80–100%)

At extreme MUTATE values, the module injects ghost notes on safe voices, fires ratchet bursts (rapid-fire repeats), and occasionally triggers tracker-style stutter effects (brief step replay) and hard cuts (momentary silence on selected voices).

### Groove Enforcement

After mutation, Trigonomicon runs a multi-pass groove enforcement pass that preserves the musical integrity of breakbeat-family patterns:

- **Snare anchor protection** — the primary backbeat snare position is never mutated away
- **Kick-snare separation** — kicks are ducked on snare anchor steps
- **Density capping** — snare and ghost densities are capped to prevent machine-gun patterns
- **Motif repetition** — if mutation destroys phrase-level repetition, a short motif is replicated
- **Hat gap filling** — empty gaps in the hihat pattern are filled with light ghost hats

The mutation buffer is rebuilt on quantized boundaries (every 2 clock edges by default), not per-sample, ensuring zero audio-rate cost.

---

## 8. Wild Mode

When the WILD switch is on *and* MUTATE is above ~55%, six additional behaviors activate with increasing intensity:

| Layer | Name | Activation | Description |
|-------|------|------------|-------------|
| **W1** | Euclidean Voice Rewrite | At mutation rebuild | 1–2 non-kick voices are overwritten with Euclidean (Bjorklund) rhythms. At extreme depth (>90%), kick and snares may also be rewritten. |
| **W2** | Negative Groove Flip | Per step, >20% wild depth | Non-anchor, non-strong-beat steps have a chance to invert their fire state (hits become rests, rests become hits). Creates counter-groove patterns. |
| **W3** | Voice Phase Drift | At stutter events, >40% wild depth | Teleports 1–2 voice step counters to musically-related phase offsets (⅓, ¼, ⅔ of the pattern length), creating sudden polymetric shifts. |
| **W4** | Hyper-Ratchet | At chaos threshold, >60% | Extends ratchets to kick (normally ratchet-immune) and enables 16× and cascade burst ratchets on hihat. Also randomizes gate length (Gate Length Fuzz). |
| **W5** | Full Chaos Reroll | At hard mutation, >80% | Instead of choosing a nearby donor, picks a fully random donor from the entire bank. Creates dramatic genre-crossing mutations. |
| **W6** | Phrase-Level Snare Rest | >45% wild depth | Occasionally mutes an entire bar of Snare 2 (or rarely Snare 1 at >88% depth) to create phrase-level breathing room. |

Wild mode also compresses the mutation curve — layers activate sooner and overlap more, reaching full chaos at lower MUTATE positions.

---

## 9. Fill Input

The FILL input provides breakdown/build-up control via a single gate + voltage signal. While the gate is held high (>1 V), the module enters a breakdown mode where selected voices are muted. The voltage level determines which voices play:

| Voltage | Mode | Active Voices |
|---------|------|---------------|
| 1–3 V | Kick + Hats | Kick, Closed HH, Open HH, Ride (snares muted) |
| 3–5 V | Kick Only | Kick only |
| 5–7 V | Hats Only | Closed HH, Open HH, Ride only |
| 7–10 V | Silence | All voices muted |

**On gate release:** A transition fill fires automatically. The fill intensity is proportional to the voltage that was held — higher voltages produce denser, more explosive fills. The fill spans two clock periods and is genre-aware (Jungle fills emphasize snare rolls, Breakcore fills add dense kick patterns, etc.).

**Tip:** Patch an envelope or LFO to the FILL input to create evolving breakdown→build-up arcs. A slow ramp from 7 V down to 0 V creates a classic silence→build→drop transition.

---

## 10. Bassline Generator

Trigonomicon includes a generative bassline sequencer that derives its rhythm from the drum pattern. Rather than running its own independent clock, the bassline fires notes when specific drum voices trigger, creating an inherently locked groove.

### How It Works

1. **Source recipe:** At the start of each 64-clock cycle, the module selects a drum trigger source recipe (kick-only, kick+snare, closed hihat, or snares). The recipe determines which drum hits spawn bass notes.
2. **Motif generation:** A 4- or 8-note melodic motif is generated using genre-weighted scale degree vocabularies. Root notes are statistically favored, with occasional leaps to the 3rd, 5th, 7th, or 10th scale degrees.
3. **Quantization:** All notes are quantized to the selected scale. The BASS knob selects from 10 scales.
4. **Portamento:** Some notes use TB-303-style exponential portamento (slide), with probability weighted by the source recipe — hat-driven patterns are more fluid, kick-driven patterns stay punchy.
5. **Octave decoration:** Each note has a small chance (~12.5% each) of a spontaneous ±1 octave leap, inspired by TB-303 accent/slide behavior.
6. **Wild extensions:** In Wild mode, the bass may enter double-time mode (triggering twice as fast) with selective note skipping for rhythmic variation.

### Bass Scale Options

| Index | Scale |
|-------|-------|
| 0 | 3b7+ (custom dark triad) |
| 1 | Minor (Aeolian) — **default** |
| 2 | Phrygian |
| 3 | Dorian |
| 4 | Chromatic |
| 5 | Diminished (Half-Whole) |
| 6 | Blues Scale |
| 7 | Minor Pentatonic |
| 8 | Locrian |
| 9 | Harmonic Minor |

### Bass Octave CV

The OCT input accepts −2 V to +1 V (snapped to integer octaves). The transpose is applied at musical boundaries (every 32 clocks) so the octave shift lands on the beat rather than mid-phrase.

### Connecting the Bassline

Patch **BTRIG** to the gate input and **BCV** to the V/oct input of any monosynth or VCO+VCA combination. The gate length is proportional to the clock interval, so the bassline plays legato at slow tempos and staccato at fast tempos.

---

## 11. Accent Bus

The ACC output is a 6-channel polyphonic signal (0–10 V) that carries per-voice accent information. Channel mapping:

| Channel | Voice |
|---------|-------|
| 1 | Kick |
| 2 | Snare 1 |
| 3 | Snare 2 |
| 4 | Closed HiHat |
| 5 | Open HiHat |
| 6 | Ride/Crash |

Accent voltage is derived from:

- **Authored probability** — higher-probability steps produce higher accent
- **Strong beat position** — downbeats and anchor steps receive a boost
- **Groove anchor status** — protected snare positions get additional emphasis
- **Ratchet hits** — repeated hits within a ratchet burst use a reduced accent (72–78% of the initial hit)

Use the accent bus to drive VCA levels, filter cutoffs, or sample-selection parameters on your drum sound sources for expressive, human-feeling dynamics.

---

## 12. Clock Resolution

The CLKRES switch (or CRS CV input) selects how external clock edges are interpreted:

| Position | Mode | Behavior |
|----------|------|----------|
| Top | ÷2 | Advance one step every other clock edge. Effectively halves the playback speed. |
| Center | 1:1 | One step per clock edge. Standard operation. |
| Bottom | ×2 | Two steps per clock edge. The second step fires as a sub-tick with genre-aware swing timing (Jungle and Breakcore use more swing than straight genres). |

**CV control:** 0 V = 1:1, 5 V = ÷2, 10 V = ×2. The CV input overrides the switch when connected.

---

## 13. Tips and Workflows

### Basic Setup
Patch a clock source to CLK. Connect the six drum outputs to separate drum sound modules or a drum machine. Start with pattern 0 (classic Amen) and gradually increase MUTATE to hear the pattern evolve.

### Breakbeat Exploration
Sweep through patterns 0–39 for the Amen/Jungle/Breakcore range. These patterns have the most sophisticated groove enforcement — mutation stays musically coherent even at high values.

### Live Performance with Fill
Patch a manual gate (button or sustain pedal) to FILL. Hold for a breakdown, release for the drop. Adjust voltage for different breakdown intensities. At 7+ V, the entire drum pattern goes silent — release for a massive fill.

### Generative Ambient Drums
Use patterns 50–54 (Dub/Reggae) or 90–99 (Techno) with MUTATE at 30–50%. The sparse patterns combined with gentle mutation create slowly evolving ambient rhythm textures.

### Polymetric Experimentation
Many patterns in the IDM bank (55–59) use different step lengths per voice. Clock at a moderate rate and listen for the evolving phase relationships between voices.

### Bassline Integration
Connect BTRIG and BCV to a bass synth. The bassline automatically locks to the drum groove. Try different BASS scale settings — Phrygian and Harmonic Minor work well for dark electronic music, Minor Pentatonic for funk.

### Wild Mode Performance
Turn on WILD and sweep MUTATE from 50% to 100% for increasingly chaotic variations. At extreme settings (>90%), Euclidean rewrites, cascade hihat bursts, and genre-crossing mutations create unpredictable but musically anchored chaos.

### Pattern CV Sequencing
Patch a step sequencer or sample-and-hold to the PAT CV input. With CLK connected, pattern changes are quantized to the clock grid, preventing mid-step glitches. This allows automated genre-hopping in a live set.

### Dynamic Accent Control
Split the ACC polyphonic output using VCV's Split module. Patch individual accent channels to VCA CV inputs on your drum voices. Kick accent to drive a compressor sidechain, snare accent to open a filter, hat accent to modulate stereo width.

### Mutation CV Automation
Patch an envelope follower or slow LFO to MCV. The drums evolve with the dynamics of another sound source. A sidechain from a vocal or lead synth creates drums that breathe with the arrangement.
