# Septagon — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [Attribution](#2-attribution)
3. [How It Works](#3-how-it-works)
4. [Signal Flow](#4-signal-flow)
5. [Controls](#5-controls)
6. [Inputs](#6-inputs)
7. [Outputs](#7-outputs)
8. [Lights](#8-lights)
9. [Time Modes](#9-time-modes)
10. [Metric Groupings](#10-metric-groupings)
11. [Pattern Generation](#11-pattern-generation)
12. [Phase Warping](#12-phase-warping)
13. [Trigger Extraction](#13-trigger-extraction)
14. [Ratchets and Flams](#14-ratchets-and-flams)
15. [Pattern Evolution](#15-pattern-evolution)
16. [Density and Chaos](#16-density-and-chaos)
17. [Swing](#17-swing)
18. [Metric Tension and Accent Curve](#18-metric-tension-and-accent-curve)
19. [Tips and Workflows](#19-tips-and-workflows)

---

## 1. Overview

**Septagon** is a **24 HP generative polyrhythmic drum pattern generator** designed around the 7/4 time signature. It produces complete drum patterns (kick, snare, ghost snare, closed hat, open hat) with per-hit velocity, metric accent, and fill detection — all driven by five macro parameters and an external clock.

Septagon does not generate audio. It outputs gate and velocity CV for each drum voice, plus utility outputs for metric grouping, fills, bar phase, and an accent curve. Patch the outputs to any drum modules or samplers.

Key features:

- Three time modes: native 7/4, standard 4/4, and 7-over-4 polyrhythmic overlay
- Five macro controls (Chaos, Density, Swing, Tension, Evolution) with CV modulation
- Five standard metric groupings for 7/4 (3-2-2, 2-2-3, 4-3, 3-4, 2-3-2) plus Auto mode
- Continuous energy-field generation with phase-space warping
- Probabilistic trigger extraction with per-voice velocity curves
- Ratchets, flams, and hat choke applied post-extraction
- Pattern memory with gradual evolution or discontinuous breaks
- Multi-bar patterns (1, 2, or 4 bars per cycle)
- Auto-regeneration every cycle or every 4 cycles
- Amortized generation (one bar per audio frame) to prevent CPU spikes
- Utility outputs: 3 group gates, fill gate, bar phase (0–10 V), metric accent CV

---

## 2. Attribution

Septagon is an original MorphWorx module — not a port. The concept of polyrhythmic drum generation in odd meters with energy-field based phase-space warping is original to MorphWorx.

---

## 3. How It Works

Septagon generates drum patterns through a multi-stage pipeline:

### Stage 1: Pattern Snapshot

A seed value and five macro parameters create a **PatternSnapshot** containing:
- A metric grouping (how the 7 beats divide into 2–3 groups)
- Kick "gravity wells" (Gaussian energy bumps defining where kicks concentrate)
- Warp parameters (macro and micro distortion amounts)

### Stage 2: Energy Field Generation

For each bar, five continuous **energy fields** are computed on a 28-step grid (7 beats × 4 subdivisions):
- **Kick field** — peaks at beat 1, group downbeats, and syncopated positions
- **Primary snare field** — placed in 1–3 groups using backbeat, syncopation, call-response, or pushed strategies
- **Ghost snare field** — low-velocity hits adjacent to primary snares
- **Closed hat field** — regular 8th-note grid, with 16ths added at higher density
- **Open hat field** — placed on last-beat and mid-bar downbeats

### Stage 3: Phase Warping

A **warp function** distorts the time axis of each energy field before trigger extraction. This shifts hits earlier or later in the bar, creating organic syncopation beyond the grid. The warp combines:
- Macro offsets at group boundaries (subtle time compression/expansion between groups)
- Micro turbulence (sine-wave oscillation adding micro-timing variation)
- Cubic drift (smooth polynomial compression/expansion across the bar)

### Stage 4: Trigger Extraction

The warped energy fields are converted to discrete **triggers** (phase + velocity). Each drum voice has different extraction settings optimized for its musical role:
- Kicks: high threshold (0.5), strong velocity (0.6–1.0), tight grid quantization
- Primary snares: medium-high threshold (0.6), moderate humanization
- Ghost snares: low threshold (0.45), soft velocity (0.2–0.5), loose grid
- Closed hats: low threshold (0.25), dense placement
- Open hats: medium threshold (0.4), strong velocity

### Stage 5: Post-Processing

After extraction, additional rhythmic features are applied:
- **Ratchets** — probabilistic sub-divisions (doubles, triplets, reverse rolls)
- **Flams** — grace notes placed just before main hits
- **Swing** — systematic displacement of off-beat subdivisions
- **Hat choke** — removes open-hat triggers that overlap with closed hats

### Stage 6: Playback

On each clock pulse, the bar phase advances by 1/totalBeats. Triggers fire when the phase crosses their position. Each trigger produces a 3 ms gate pulse and holds its velocity until the next trigger.

---

## 4. Signal Flow

```
                  ┌─────────────────────────────────────┐
                  │         Pattern Generator            │
                  │                                      │
  Chaos ──→       │  PatternMemory → EnergyFields →     │
  Density ──→     │  PhaseWarper → TriggerExtractor →   │
  Evolution ──→   │  Ratchets/Flams → Swing → HatChoke │
  Tension ──→     │                                      │
  Grouping ──→    └──────────────────┬──────────────────┘
                                     │
                            Trigger Buffers
                     (kick, snare, ghost, chat, ohat, fill)
                                     │
  CLOCK ──→ [Phase Advance] ──→ [Trigger Scanner]
                  │                  │
                  │    ┌─────────────┼──────────────┐
                  │    │             │              │
                  │  KICK         SNARE          GHOST
                  │  gate+vel    gate+vel       gate+vel
                  │    │             │              │
                  │  C.HAT        O.HAT          FILL
                  │  gate+vel    gate+vel        gate
                  │    │             │              │
                  │    └─────────────┼──────────────┘
                  │                  │
                  └──→ [Utility Outputs]
                       PHASE, ACCENT, GRP1, GRP2, GRP3
```

---

## 5. Controls

### Main Knobs

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **CHAOS** | 0–100% | 27% | Controls syncopation intensity, ratchet/flam probability, warp amount, and pattern complexity. See [Density and Chaos](#16-density-and-chaos). |
| **DENSITY** | 0–100% | 100% | Controls how many hits appear in each voice. Higher = more active patterns. See [Density and Chaos](#16-density-and-chaos). |
| **SWING** | −100% to +100% | 15% | Displaces off-beat subdivisions forward or backward. See [Swing](#17-swing). |
| **TENSION** | 0–100% | 51% | Scales the metric accent curve, controlling how strongly group boundaries are emphasized. See [Metric Tension](#18-metric-tension-and-accent-curve). |
| **EVOLUTION** | 0–100% | 64% | Controls how much each regeneration differs from the previous pattern. See [Pattern Evolution](#15-pattern-evolution). |
| **GROUPING** | 0–5 (snapped) | 5 (Auto) | Selects the metric grouping. See [Metric Groupings](#10-metric-groupings). |

### Switches

| Control | Positions | Default | Description |
|---------|-----------|---------|-------------|
| **TIME MODE** | 7/4 / 4/4 / 7-over-4 | 7/4 | Selects the rhythmic framework. See [Time Modes](#9-time-modes). |
| **PATTERN LENGTH** | 1 bar / 2 bars / 4 bars | 4 bars | Number of bars in one complete pattern cycle. |
| **AUTO REGEN** | Manual / Every Cycle / Every 4 Cycles | Every 4 Cycles | When to automatically regenerate the pattern. |

### Button

| Control | Description |
|---------|-------------|
| **GEN** | Momentary button. Triggers immediate pattern regeneration (with 150 ms cooldown). |

---

## 6. Inputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **CLOCK** | Clock | Gate/trigger | External clock input. Each pulse advances the bar phase by 1 beat. **Required** — Septagon does not advance without a clock. |
| **CHAOS** | Chaos CV | 0–10 V | Added to the CHAOS knob (scaled to 0–1). |
| **DENS** | Density CV | 0–10 V | Added to the DENSITY knob (scaled to 0–1). |
| **EVOL** | Evolution CV | 0–10 V | Added to the EVOLUTION knob (scaled to 0–1). |
| **SWING** | Swing CV | 0–10 V | Added to the SWING knob (scaled to 0–1). Note: combined value allows −1 to +1. |
| **TENS** | Tension CV | 0–10 V | Added to the TENSION knob (scaled to 0–1). |
| **GEN** | Generate Trigger | Gate/trigger | Rising edge triggers pattern regeneration (150 ms cooldown). |
| **RESET** | Reset | Gate/trigger | Resets the bar phase to 0 and clears all trigger states. |

---

## 7. Outputs

### Drum Voice Outputs (5 voices × 2 jacks each)

Each drum voice has a **gate** output and a **velocity** output:

| Voice | Gate Jack | Velocity Jack | Description |
|-------|-----------|---------------|-------------|
| **KICK** | KICK gate | KICK vel | Bass drum. Strong on beat 1 and group downbeats. |
| **SNARE** | SNARE gate | SNARE vel | Primary snare. Backbeats and syncopated placements. |
| **GHOST** | GHOST gate | GHOST vel | Ghost snare. Soft hits adjacent to primary snares. |
| **C.HAT** | C.HAT gate | C.HAT vel | Closed hi-hat. Regular 8th/16th note grid. |
| **O.HAT** | O.HAT gate | O.HAT vel | Open hi-hat. Sparse accents on downbeats. |

**Gate behavior:** 10 V pulse, 3 ms duration (except FILL = 80 ms).

**Velocity behavior:** 0–10 V, sample-and-hold (velocity persists until the next trigger for that voice).

### Utility Outputs

| Jack | Label | Range | Description |
|------|-------|-------|-------------|
| **GRP1** | Group Gate 1 | 0/10 V | 10 V pulse at the start of the first metric group (e.g., beats 1–3 in a 3-2-2 grouping). |
| **GRP2** | Group Gate 2 | 0/10 V | 10 V pulse at the start of the second metric group (e.g., beats 4–5). |
| **GRP3** | Group Gate 3 | 0/10 V | 10 V pulse at the start of the third metric group (e.g., beats 6–7). |
| **FILL** | Fill Active Gate | 0/10 V | 10 V whenever a ratchet or flam hit fires. Use to trigger fill-specific processing. |
| **PHASE** | Bar Phase | 0–10 V | Continuous ramp (0 V at bar start, 10 V at bar end). Useful for envelope following or modulation synced to the bar. |
| **ACCENT** | Metric Accent CV | 0–10 V | Continuous metric accent curve output. Peaks at group downbeats, dips between beats. Scaled by the TENSION control. |

---

## 8. Lights

| Light | Color | Description |
|-------|-------|-------------|
| **KICK** | Green | Flashes on kick triggers. |
| **SNARE** | Green | Flashes on snare triggers. |
| **GHOST** | Green | Flashes on ghost snare triggers. |
| **C.HAT** | Green | Flashes on closed hat triggers. |
| **O.HAT** | Green | Flashes on open hat triggers. |
| **GEN** | Green | Lit while the GENERATE button is pressed. |
| **PHASE** | Green | Brightness tracks bar phase (dim at start, bright at end). |

---

## 9. Time Modes

The TIME MODE switch selects the fundamental rhythmic framework:

### 7/4 (Top Position)

Classic Septagon mode. The bar has 7 beats with 4 subdivisions each (28 grid steps). Everything — generation, warping, accents, group gates — operates in 7/4 meter.

**Clock input:** Each clock pulse advances by 1/7 of the bar (or 1/(7×bars) for multi-bar patterns).

### 4/4 (Center Position)

Standard 4/4 time. The bar has 4 beats with 4 subdivisions (16 grid steps). The GROUPING knob selects a 4/4-appropriate 2+2 grouping. All outputs operate in 4/4.

**Clock input:** Each clock pulse advances by 1/4 of the bar.

### 7-over-4 (Bottom Position)

Polyrhythmic overlay mode. The **transport** (clock, phase, and hit placement) runs in 4/4, but the **accent curve** and **group gate outputs** follow a 7-beat overlay. The result is that hits land on a standard 4/4 grid, but the dynamic emphasis patterns follow an asymmetric 7-beat cycle that drifts against the 4/4 structure.

This creates evolving polyrhythmic tension — the accent peaks shift relative to the kick/snare placement on each cycle, producing patterns that never repeat in the same perceptual way even though the trigger positions are fixed.

**Clock input:** Each clock pulse advances by 1/4 of the bar (4/4 transport).

---

## 10. Metric Groupings

In 7/4 mode, the 7 beats are divided into 2–3 groups of unequal length. The grouping defines where downbeat accents fall and shapes the feel of the pattern.

### Standard Groupings

| Index | Pattern | Feel |
|-------|---------|------|
| 0 | **3-2-2** | Heavy downbeat, quick turnaround at beats 4 and 6. Common in Balkan music and progressive rock. |
| 1 | **2-2-3** | Two quick groups followed by a long resolution. Creates a "rushing then landing" feel. |
| 2 | **4-3** | Two large groups — basically a long 4 followed by a short 3. Closest to "4/4 with an extra beat." |
| 3 | **3-4** | Inverse of 4-3. The short group comes first, creating early tension that resolves into a longer phrase. |
| 4 | **2-3-2** | Centered asymmetry. The long group in the middle creates a "bookended" feel. |
| 5 | **Auto** | The grouping is selected automatically by the pattern generation algorithm and evolves with each regeneration. |

### How Groupings Affect Patterns

- **Kick placement**: Kicks strongly favor group downbeats (first beat of each group).
- **Snare placement**: Snares target the middle or end of groups (backbeat strategy).
- **Accent curve**: Peaks at group downbeats (accent = 1.0), lower between beats (accent = 0.3–0.5).
- **Group gates**: GRP1/GRP2/GRP3 fire at the start of each respective group.
- **Warp function**: Macro warp points are placed at group boundaries, so warp distortion clusters around group transitions.

---

## 11. Pattern Generation

### Energy Fields

Each drum voice is computed as a continuous **energy field** — a 28-sample (for 7/4) array of floating-point values representing "how likely a hit should occur" at each grid position. Energy fields are continuous functions, not on/off grids.

### Kick Field Generation

1. **Beat 1 anchor**: Full energy at phase 0 (always).
2. **Group downbeats**: Energy peak with probability 40%–90% (scales with density).
3. **Syncopated 16ths**: "And" positions between beats receive energy scaled by chaos × density.
4. **Ghost kicks**: Low-energy hits on early subdivisions (requires chaos > 0.4, density > 0.5).

### Snare Field Generation

Primary snares use one of four placement strategies per group, selected probabilistically:

| Strategy | Probability | Placement |
|----------|------------|-----------|
| **Backbeat** | 45% | Middle of the group (e.g., beat 2 of a 3-beat group) |
| **Syncopated** | 25% | "And" position (off-beat 16th) |
| **Call-response** | 15% | One grid step after the nearest kick |
| **Pushed** | 15% | One grid step before the normal position |

Snares avoid overlapping strong kicks (kicks with energy > 0.6 cause snares to shift forward).

### Ghost Snare Generation

Ghost snares are placed ±1 subdivision adjacent to primary snares with 25–35% probability. They have low velocity (0.2–0.5) and loose grid quantization. Ghost snares require density > 0.2 and chaos > 0.1.

### Hat Field Generation

- **Closed hats**: Always present on 8th-note grid (beat + "and" of each beat). At density > 0.3, 16th notes are added. Closed hats are removed where snares are strong.
- **Open hats**: Placed on the last beat's downbeat (classical position), with optional mid-bar placements at higher chaos. Closed hats within the choke window (≈4 ms) of an open hat are removed.

---

## 12. Phase Warping

After energy fields are computed, a **warp function** distorts the time axis before trigger extraction. This is the core innovation that gives Septagon its organic, non-mechanical feel.

### Macro Warp

Keyframe phase offsets placed at group boundaries. Each group boundary receives 2–3 warp points that shift time by −2.5% to +2% of the bar. The effect is subtle time compression at group starts (hits arrive slightly early) and expansion at group ends (hits linger).

### Micro Turbulence

A sine-wave modulation with randomized frequency (6.0 ± 1.5 Hz) and amplitude scaled by the chaos parameter. This adds imperceptible micro-timing variation — similar to a human drummer's natural time drift.

### Cubic Drift

A polynomial curve (x³ − 0.5x) that creates smooth acceleration/deceleration across the bar. Controlled by the chaos parameter. At zero chaos, the time axis is perfectly regular.

### How Warp Is Applied

Each energy field is re-sampled at the warped phase positions. A hit at grid position 12/28 might end up at phase position 12.3/28 after warping — it arrives slightly late. The continuous nature of energy fields means warping produces smooth, musically coherent displacements rather than abrupt jumps.

---

## 13. Trigger Extraction

Energy fields are converted to discrete triggers using per-voice extraction configurations:

### Per-Voice Extraction Settings

| Voice | Threshold | Velocity Range | Min Spacing | Grid Strength | Humanization |
|-------|-----------|----------------|-------------|---------------|--------------|
| **Kick** | 0.50 | 0.6–1.0 | 100 ms | 95% (tight) | 8% |
| **Snare** | 0.60 | 0.5–1.0 | 140 ms | 90% | 12% |
| **Ghost** | 0.45 | 0.2–0.5 | 80 ms | 70% (loose) | 20% |
| **Closed Hat** | 0.25 | 0.4–1.0 | 30 ms | 85% | 15% |
| **Open Hat** | 0.40 | 0.6–1.0 | 60 ms | 80% | 10% |

### Extraction Process

1. **Peak finding**: Locate local maxima in the energy field above the threshold.
2. **Grid quantization**: Snap peaks toward the nearest grid step. Quantize strength controls how tightly hits lock to the grid (95% for kicks = nearly on-grid, 70% for ghosts = loose and organic).
3. **Velocity calculation**: Base velocity from energy level, modified by subdivision accent (downbeats louder than 16ths), metric accent curve, and humanization jitter.
4. **Spacing enforcement**: Reject triggers too close to existing ones (e.g., kicks must be ≥100 ms apart).

### Density Scaling

The DENSITY parameter scales all thresholds inversely — higher density lowers thresholds, allowing more triggers through. It also tightens minimum spacing, allowing hits to cluster closer together.

---

## 14. Ratchets and Flams

Ratchets and flams are applied **after** trigger extraction, adding rhythmic embellishments to the base pattern.

### Ratchets

Ratchets add 1–2 extra hits subdividing an existing trigger. They are enabled when **chaos ≥ 0.60** (for hats and snares) or **chaos ≥ 0.72** (for kicks).

| Mode | Weight | Description |
|------|--------|-------------|
| **Straight** | 50% | 1–2 extra hits spaced at half-grid or full-grid intervals after the original hit. Classic drum roll feel. |
| **Triplet** | 18% + chaos | Extra hits spaced at 1/3 grid intervals. Creates a triplet subdivision against the base rhythm. |
| **Reverse** | 8% + chaos | Extra hits placed *before* the original hit, with rising velocity. Creates a reverse-crescendo roll. |

Ratchet velocity is typically 55–80% of the original hit, with slight random variation.

All ratchet hits are flagged and included in the **FILL gate** output, making them easy to detect and process separately.

### Flams

Flams are grace notes placed 1/128 of a bar before the main hit. They are enabled when **chaos ≥ 0.30**, with probability scaling from 0% to 40% as chaos increases.

Flam velocity is 65% of the main hit. Flams are most common on snares.

Like ratchets, flam hits are included in the FILL gate output.

---

## 15. Pattern Evolution

Septagon maintains a **ring buffer of 8 past patterns**. Each regeneration creates a new pattern that evolves from the current one. The EVOLUTION knob controls how much each generation departs from its predecessor.

### Evolution Decision

Each regeneration makes a binary decision: **continue** (gradual mutation) or **break** (discontinuous jump).

$$P(\text{break}) = \text{evolution} \times 0.5 + \text{chaos} \times 0.25$$

At Evolution = 0.64 (default) and Chaos = 0.27 (default), the break probability is:

$$0.64 \times 0.5 + 0.27 \times 0.25 = 0.3875 + 0.0675 = 38.75\%$$

### Continue (Gradual Mutation)

When continuing, the pattern mutates smoothly:
- **Grouping**: May rotate (e.g., 3-2-2 → 2-2-3) or slightly mutate while preserving the total of 7 beats
- **Kick gravity wells**: Centers shift by ±2%, strengths vary ±15%, widths vary ±1%
- **Warp amounts**: Drift by ±10%
- Small chance (10% × chaos) to remove a gravity well; small chance (12% × chaos) to add one

### Break (Discontinuous Jump)

When breaking, the pattern jumps to a new state:
- **Grouping**: Fresh random selection from the 5 standard groupings
- **Kick gravity wells**: Completely regenerated
- **Warp amounts**: Jump by ±20%

### Musical Effect

At low Evolution, patterns stay very similar across regenerations — subtle variation within a consistent groove. At high Evolution, patterns shift dramatically, creating a sense of constant change. The break/continue decision prevents patterns from drifting too far too slowly; occasional breaks inject novelty.

---

## 16. Density and Chaos

Density and Chaos are the two primary macro parameters. They interact in complementary ways:

### Density

Controls **how many hits** appear in each voice:
- Low density (0–30%): Sparse patterns. Only strong kick downbeats and basic hat grid survive.
- Medium density (30–70%): Balanced patterns with clear kick/snare interplay and regular hats.
- High density (70–100%): Dense, busy patterns with ghost snares, 16th-note hats, and frequent syncopation.

**Effects on generation:**
- Gate threshold scaled by (1.5 − density) — higher density = lower thresholds = more triggers
- Minimum spacing between hits tightened at high density
- Ghost snares enabled above 20% density
- 16th-note hats enabled above 30% density

### Chaos

Controls **how complex and unpredictable** the patterns are:
- Low chaos (0–30%): Patterns are grid-locked, predictable, and mechanical. No ratchets, no flams, minimal warp.
- Medium chaos (30–60%): Moderate warp, occasional flams (chaos > 30%), syncopated kick placements.
- High chaos (60–100%): Maximum warp distortion, ratchets enabled (chaos > 60%), frequent flams, ghost kicks, reverse rolls, and extreme micro-timing variation.

**Chaos thresholds:**
| Feature | Chaos Required |
|---------|---------------|
| Flams (snare) | ≥ 30% |
| Ghost kicks | ≥ 40% |
| Ratchets (hats, snares) | ≥ 60% |
| Triplet ratchets | ≥ 60% (probability scales with chaos) |
| Ratchets (kick) | ≥ 72% |
| Reverse ratchets | ≥ 60% (probability scales with chaos) |

### Interaction

Chaos and Density together determine the pattern's character:

| | Low Density | High Density |
|---|-------------|--------------|
| **Low Chaos** | Minimal, on-grid: kick + snare + 8th hats | Busy but mechanical: dense grid, no fills |
| **High Chaos** | Sparse but wild: few hits, maximum warp and ratchets | Maximum complexity: dense fills, ratchets, flams, heavy warp |

---

## 17. Swing

The SWING parameter displaces off-beat subdivisions (the "ands" — every other 16th note) forward or backward in time:

- **Positive swing** (0 to +100%): Off-beats are delayed, creating a shuffle/swing feel.
- **Negative swing** (−100% to 0): Off-beats arrive early, creating a "pushed" or anxious feel.
- **Zero swing**: Perfectly even subdivisions.

Swing is applied after ratchets and flams (so ratchet sub-hits also swing), but before hat choke (so the final choke distances reflect swung timing).

In 7/4 mode, swing interacts with the asymmetric grouping to create complex push-pull effects within each group. A 3-beat group with positive swing feels very different from a 2-beat group with the same swing amount.

---

## 18. Metric Tension and Accent Curve

### Accent Curve

Septagon continuously generates a **metric accent curve** — a 28-sample function (matching the grid resolution) that encodes the natural dynamic emphasis of the current grouping. The curve has:

- **Peak accents (1.0)** at group downbeats (the first beat of each group)
- **Medium accents (0.5)** at the last beat of each group
- **Soft accents (0.3)** at other positions
- A within-beat ramp factor (0.8 → 1.0) that emphasizes the onset of each beat

### Tension Parameter

The TENSION knob (0–100%) scales the accent curve:

$$\text{tensionScale} = 0.4 + 0.6 \times \text{tension}$$

At low tension (0%), accents are flattened (all hits have similar emphasis). At high tension (100%), accents are fully pronounced — group downbeats are dramatically louder than off-beats.

### How Tension Affects Patterns

The accent curve modulates trigger velocity during extraction:

$$\text{velocity} = \text{base} \times (0.7 + 0.3 \times \text{accent})$$

At maximum tension, a hit at a group downbeat receives 100% velocity while a hit deep in an off-beat position receives 70%.

The accent curve is also output directly on the **ACCENT** jack (0–10 V), enabling external modules to follow the same dynamic contour.

### Tension in 7-over-4 Mode

In 7-over-4 mode, the accent curve follows the 7-beat overlay while the trigger timing follows 4/4. This creates a polyrhythmic accent pattern where emphasis peaks drift across the 4/4 grid on successive cycles.

---

## 19. Tips and Workflows

### Getting Started
Patch a clock source to CLOCK. Patch KICK gate to a kick drum, SNARE gate to a snare drum, C.HAT gate to a hi-hat. Hit GEN to create a pattern. Adjust CHAOS and DENSITY to taste.

### Velocity-Sensitive Drums
Patch both the gate and velocity outputs for each voice. Most drum modules accept a velocity input (sometimes labeled "accent" or "level"). The velocity output provides per-hit dynamic variation that brings the pattern to life.

### Using the Accent Curve
Patch the ACCENT output to a filter cutoff or reverb send. The pattern's natural metric emphasis shapes the processing, creating dynamic movement that follows the groove structure rather than a simple LFO.

### Group-Synced Effects
Patch GRP1, GRP2, and GRP3 to switch between three different effect chains, reverb settings, or filter states. Each group gate fires at its respective downbeat, so you can have different processing for each section of the bar — e.g., dry/tight on the 3-beat group, reverbed on the 2-beat groups.

### Fill Detection
The FILL gate fires on every ratchet and flam hit. Patch it to:
- A separate drum voice (e.g., a tambourine or crash hit on fills)
- An effect send (only apply distortion/delay to fill notes)
- A visual indicator (LED or scope) to monitor pattern complexity

### Evolution Workflows

**Slow Evolution (20–40%):** Patterns change subtly across regenerations. Good for long sets where you want consistency with gradual drift. Set AUTO REGEN to "Every 4 Cycles" for a slowly evolving groove.

**High Evolution (70–100%):** Each regeneration sounds significantly different. Good for generative music or finding new patterns. Set AUTO REGEN to "Every Cycle" for constant variation.

**Manual Breaks:** Set Evolution high and AUTO REGEN to "Manual." Send triggers to the GEN input at dramatic moments (end of a phrase, drop, etc.) for intentional pattern changes.

### 7-over-4 Polyrhythm
Set TIME MODE to 7-over-4. The 4/4 transport keeps everything locked to standard time, but the accent curve and group gates follow the asymmetric 7-beat overlay. Patch ACCENT to a VCA controlling the hi-hat level for a polyrhythmic emphasis pattern that shifts against the kick/snare grid.

### Multi-Bar Patterns
Set PATTERN LENGTH to 4 bars for maximum variation. Each bar is generated with slightly increasing chaos (bar 2 is 5% more chaotic than bar 1, bar 3 is 10%, etc.), creating a natural build-up across the cycle. Multi-bar patterns also mean ratchets and fills spread across a longer time frame, giving the pattern more room to breathe.

### CV Modulation
Patch slow LFOs or envelopes to the CV inputs for evolving patterns:
- **CHAOS CV**: An envelope follower from the mix bus increases complexity during loud sections.
- **DENSITY CV**: A triangle LFO creates breathing patterns that alternate between sparse and dense.
- **SWING CV**: A random S&H drifts the swing amount between straight and shuffled.
- **TENSION CV**: An inverted envelope from the sidechain source reduces tension on kick hits, then rebuilds it between kicks.

### Integration with Ferroklast
Patch KICK, SNARE, C.HAT, and O.HAT gates directly to Ferroklast's corresponding trigger inputs. Septagon provides the pattern; Ferroklast provides the sound. Use the velocity outputs to modulate Ferroklast's per-voice level or decay.

### Integration with SlideWyrm
Use Septagon's KICK gate as SlideWyrm's clock input for kick-synced acid patterns. Or use the PHASE output (0–10 V) to modulate SlideWyrm's DENSITY CV for patterns that become denser as the bar progresses.

### Integration with Minimalith
Patch Septagon's ACCENT output to Minimalith's EVO CV input. The FM synth becomes more aggressive at metric accents (group downbeats) and mellower between beats — creating a groove-locked timbral rhythm.
