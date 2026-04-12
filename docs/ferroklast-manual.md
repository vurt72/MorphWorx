# FerroKlast — User Manual

**MorphWorx** · VCV Rack 2

This manual covers the **full Rack version of FerroKlast**. It does **not** describe **FerroKlast MM**, which has a reduced lane set and a different panel layout.

---

## Contents

1. [Overview](#1-overview)
2. [What FerroKlast Is](#2-what-ferroklast-is)
3. [Panel Layout](#3-panel-layout)
4. [Connections](#4-connections)
5. [Global Controls](#5-global-controls)
6. [Per-Voice Columns](#6-per-voice-columns)
7. [Voice Reference](#7-voice-reference)
8. [Reverb, Ducking, and Utility Outputs](#8-reverb-ducking-and-utility-outputs)
9. [Accent and CV Behavior](#9-accent-and-cv-behavior)
10. [Patch Tips](#10-patch-tips)

---

## 1. Overview

**FerroKlast** is an **eight-lane hybrid FM percussion instrument**. It combines synthesized drum bodies, noise layers, transient generators, selective TR-909-derived sample layers, per-lane trims, a dedicated stereo reverb send/return path, kick-driven ducking, and two utility CV outputs for patch animation.

The full Rack version exposes these lanes:

- **Kick**
- **Snare 1**
- **Snare 2**
- **CH** (closed hat)
- **OH** (open hat / more chaotic metallic lane)
- **Ride**
- **Clap**
- **Rim**

FerroKlast is not a single stereo drum machine output. It is an **eight-output drum voice bank** with separate dry outputs per lane plus dedicated wet reverb outputs.

---

## 2. What FerroKlast Is

At a high level, each hit is built from some combination of:

- an FM or resonant pitched body,
- filtered noise,
- a dedicated transient generator,
- optional sample-derived attack or one-shot material,
- lane-specific filtering and saturation,
- optional kick-driven ducking,
- optional send into the shared stereo reverb.

The module is designed around two layers of control:

- **Global macros** shape the overall machine character.
- **Per-lane trimpots** set level, tuning, decay, variation, transient behavior, and reverb send for each drum voice.

This makes FerroKlast fast to dial in as a whole kit, while still giving enough per-voice control to differentiate lanes.

---

## 3. Panel Layout

FerroKlast is organized into four zones.

### Kick section

- **RUMBLE**: delayed sub-bass bloom behind the kick.
- **CURVE**: pitch-fall contour from fast 808-style drop toward slower 909-style sweep.
- **ATK TRANSIENT**: dark beater thud to bright click.
- **PTCH DPTH**: shallower or deeper kick sweep.

### Global / snare macro section

Top row:

- **PUNCH**
- **BODY**
- **COLOR**
- **SN RES**
- **SN CUT**
- **SN DRV**

Bottom row:

- **SNAP**
- **NOISE**
- **TRANSIENT**
- **MATERIAL**
- **RUIN**
- **DRIVE**

### Reverb and ducking section

- **TIME**, **DAMP**, **DIFF**, **AMNT** for the shared stereo reverb.
- **DPT**, **ON**, **PMP** trimmers/switch for kick-driven ducking.

### Eight lane columns

Each lane has:

- **LEVEL**
- **TUNE**
- **DECAY**
- **VAR**
- **TR AMT**
- **TR TYPE**
- **TR DEC**
- **RVB** send
- individual **TRIG** input
- individual **OUT** output

For the **Kick** lane, the last transient-decay row is labeled **TR/RM** because that control also influences how the kick rumble tail behaves.

---

## 4. Connections

### Trigger inputs

Each lane has its own trigger input:

- **Kick TRIG**
- **Snare 1 TRIG**
- **Snare 2 TRIG**
- **CH TRIG**
- **OH TRIG**
- **Ride TRIG**
- **Clap TRIG**
- **Rim TRIG**

Triggers are expected to be standard Rack gates/triggers. Each trigger fires the corresponding lane directly.

### Macro CV inputs

Left CV column:

- **ACC**: accent bus, polyphonic 0 to 10 V, Trigonomicon-compatible
- **RUIN**: additive CV for the RUIN macro
- **DRV**: additive CV for final drive
- **CLR**: additive CV for COLOR
- **BDY**: additive CV for BODY
- **PCH**: additive CV for PUNCH
- **SNP**: additive CV for SNAP
- **SNC**: additive CV for snare cutoff

Right CV column:

- **MAT**: additive CV for MATERIAL
- **TRS**: additive CV for TRANSIENT
- **DMP**: additive CV for reverb damping
- **DFF**: additive CV for reverb diffusion
- **AMT**: additive CV for reverb amount
- **TIM**: additive CV for reverb time
- **VAR**: additive CV for all lane variation parameters
- **KLEN**: additive CV for kick decay/length

### Outputs

Dry lane outputs:

- **Kick OUT**
- **Snare 1 OUT**
- **Snare 2 OUT**
- **CH OUT**
- **OH OUT**
- **Ride OUT**
- **Clap OUT**
- **Rim OUT**

Utility and wet outputs:

- **SC**: kick sidechain envelope output, 0 to 10 V
- **GRV**: groove CV output, 0 to 10 V derived from recent rhythmic activity
- **Reverb Out L**
- **Reverb Out R**

Important behavior:

- The eight lane outputs are **dry outputs**.
- The shared reverb does **not** get mixed back into the dry lane outputs.
- Reverb is available only on the dedicated **wet stereo pair**.

---

## 5. Global Controls

### Kick controls

#### RUMBLE

Adds a delayed sub bloom behind the kick. Low settings keep the kick tight. Higher settings open a larger post-hit low-frequency swell with more density and drive.

Use this with care in dense mixes. It is designed to bloom after the transient rather than stack directly on top of it.

#### CURVE

Shapes the kick pitch envelope:

- left: faster, more 808-like pitch drop
- right: slower, more 909-like falling sweep

#### ATK TRANSIENT

Changes the kick attack color from muted beater thump to brighter click.

#### PTCH DPTH

Scales how deep the kick pitch sweep is. Lower values stay more thud-like. Higher values produce a more obvious downward pitch dive.

### Global macros

#### PUNCH

Primary hit-energy macro. It increases FM depth and pitch-sweep intensity across the kit.

- Low: softer, flatter hits
- High: harder, more aggressive attacks and stronger pitch motion

#### BODY

Emphasizes weight and low-frequency body, especially on kick and snare lanes.

- Low: leaner, more attack-led voices
- High: more mass, more shell/body presence

#### COLOR

Shifts FM ratio behavior and tonal brightness. The control range extends beyond a simple 0 to 1 feel; pushing it harder moves many lanes into more inharmonic or sharper territory.

- Lower settings stay rounder and more neutral
- Higher settings push metallicity, brightness, and ratio stretch

#### SNAP

Controls transient intensity and brightness across the kit.

- Low: softer front edge
- High: sharper, brighter, more percussive attack

#### NOISE

Sets how much noise-based component is mixed into the voices.

- Low: cleaner, more tonal drum bodies
- High: more wires, hiss, sand, and breath

Above roughly the midpoint, the snare-wire side can become more crushed and aggressive.

#### TRANSIENT

Global transient-character macro. It affects how much emphasis the dedicated transient layer gets and how its timing/contour feels.

This control reaches beyond a normal 0 to 1 feel. Pushing it higher can make the transients much more explicit and stylized.

#### MATERIAL

Balances **body** against **exciter/noise/edge**.

- Lower: more exciter-forward, thinner, brighter, sharper
- Higher: more mass, tone, shell, and sustain

This is one of the most useful macros for deciding whether the kit reads as clean drum synthesis, metallic percussion, or broken-machine texture.

#### RUIN

The destructive texture macro. Through most of the range it adds grit, reduction, and unstable behavior in stages. At the top end it becomes deliberately more damaged.

In practice this introduces combinations of:

- bit reduction,
- sample-rate reduction,
- harsher instability,
- bytebeat-like destruction in the more extreme zone.

#### DRIVE

Final-stage saturation on the rendered voices. Use this as the last broad-stroke density control after the drum shapes are already in place.

### Snare-only controls

#### SN RES

Controls snare-wire filter resonance or ring on **Snare 1** and **Snare 2** only.

#### SN CUT

Controls snare-wire brightness cutoff on **Snare 1** and **Snare 2** only.

#### SN DRV

Adds extra drive to the snare filter path only.

---

## 6. Per-Voice Columns

Each lane has the same per-voice trim layout.

### LEVEL

Lane output level before the output jack.

### TUNE

Pitch offset for the lane.

- Kick range: approximately -12 to +12 semitones
- Other lanes: approximately -36 to +12 semitones

### DECAY

Main decay length for the lane. What this means depends on the voice, but it generally affects body length, noise tail, and envelope release.

### VAR

Per-lane character variation. This is not a simple pitch control. It shifts lane-specific internals such as:

- shell/body balance,
- sample start position,
- metallic balance,
- beater versus roundness,
- transient contour,
- timbral emphasis.

For the **Kick**, this is especially audible as a round/body-to-snappy/beater morph.

### TR AMT

How much of the dedicated transient generator is added to the lane.

### TR TYPE

Selects the transient model. FerroKlast sweeps across a bank of transient types rather than just offering one click.

These types span noise clicks, filtered ticks, short chirps, sine pings, FM-like pings, square-like attacks, and glitchier sample-and-hold style edges.

### TR DEC

Transient decay time. Higher values lengthen the transient layer.

For the **Kick**, this row also interacts with rumble behavior, so very short settings can effectively keep the rumble out of the way while longer settings let more tail develop.

### RVB

Per-lane send amount into the shared stereo reverb.

The dry lane outputs remain dry. This control only feeds the shared reverb bus.

---

## 7. Voice Reference

### Kick

The kick is a hybrid voice with synthesized body behavior and TR-909-derived attack/cycle material in the full Rack build.

Best controls:

- **BODY** for weight
- **PUNCH** for impact
- **CURVE** and **PTCH DPTH** for sweep style
- **RUMBLE** for post-hit sub bloom
- **ATK TRANSIENT** for beater brightness

Use **VAR** to move between rounder and more beater-forward presentations.

### Snare 1

The fuller snare lane. It favors body-plus-wire balance and responds well to **SN CUT**, **SN RES**, **SN DRV**, **NOISE**, and **BODY**.

Use when you want a more central snare voice with shell and wire identity.

### Snare 2

The brighter or leaner companion snare. It can read more cutting and upper-focused than Snare 1 depending on settings.

Good for layering with Snare 1 or alternating between two snare colors in a sequence.

### CH

Closed hat lane. Tight metallic voice with short envelope behavior.

Good controls:

- **COLOR** for metallic brightness
- **RUIN** for crust and reduction
- **DECAY** for tight-to-looser closure
- **TR TYPE** and **TR AMT** for attack definition

### OH

Open hat lane. More unstable and extended than CH, with a more chaotic metallic/noise profile.

When **CH** is triggered, it partially chokes **OH**, so the two lanes behave like a classic closed/open hat pair.

### Ride

Hybrid ride voice with sampled and synthesized behavior in the full Rack version.

Use **DECAY**, **COLOR**, and **MATERIAL** to move between flatter 909-style ride territory and more bell-heavy or metallic states.

### Clap

Hybrid clap using noise/transient design plus sampled character in the full Rack build.

Use **VAR** and the transient controls to change whether it feels soft and broad or sharper and more synthetic.

### Rim

Sharp short voice with strong transient identity. Useful for syncopation, ghost hits, and higher-register punctuation.

It responds especially well to **TR AMT**, **TR TYPE**, **VAR**, and moderate **RUIN**.

---

## 8. Reverb, Ducking, and Utility Outputs

### Shared stereo reverb

The full Rack version includes a shared stereo reverb with four controls:

- **TIME**: tail length
- **DAMP**: high-frequency absorption
- **DIFF**: diffusion / density of the space
- **AMNT**: overall wet engine input amount

Each lane has its own **RVB** send trimmer. The dedicated wet outputs are:

- **Reverb Out L**
- **Reverb Out R**

Practical note:

- Keep lane outs patched dry for tight drum mixing.
- Patch the wet stereo pair into a mixer return if you want reverb as a parallel effect.

### Kick ducking

FerroKlast includes kick-driven ducking for the non-kick lanes.

- **ON** enables/disables the effect.
- **DPT** sets duck depth.
- **PMP** sets release feel, from tighter/faster to longer lingering pump.

This is useful when you want internal pumping without adding an external compressor.

### SC output

**SC** outputs the kick envelope as a 0 to 10 V control signal. You can use it to drive external VCAs, filters, or sidechain-like modulation elsewhere in the patch.

### GRV output

**GRV** outputs a groove descriptor CV derived from recent hits, accents, metallic activity, variation, and macro state. In practice it behaves like a rhythmic control source that follows how the kit is playing rather than simply mirroring one lane.

Useful destinations:

- external filter modulation
- delay time wobble
- distortion bias
- sequencer mutation depth
- reverb size or send modulation in another module

---

## 9. Accent and CV Behavior

### Accent bus

The **ACC** input is designed as a **polyphonic 0 to 10 V accent bus** and is compatible with **Trigonomicon** style accent routing.

Behavior:

- with one channel connected, the same accent affects all lanes
- with multiple channels, each lane reads its own channel where available
- if the accent bus is not connected, FerroKlast uses an internal default accent around a musically useful mid-high value rather than treating every hit as flat zero-accent

Accent influences more than level. It can also affect punch, transient emphasis, variation response, and groove CV behavior.

### CV ranges

Most macro CV inputs are additive and expect **0 to 10 V**.

Important exceptions in feel:

- **COLOR** has a wider effective range than the other macros.
- **MATERIAL** extends beyond a normal 0 to 1 style range.
- **TRANSIENT** also extends beyond a normal 0 to 1 style range.

This means heavy CV into those inputs can push the module into deliberately exaggerated terrain.

---

## 10. Patch Tips

### 1. Start with the kit, then specialize lanes

Set **PUNCH**, **BODY**, **COLOR**, **SNAP**, **MATERIAL**, and **RUIN** first. After the overall machine feels right, refine each lane with **VAR**, **DECAY**, and the transient rows.

### 2. Use CH and OH as a pair

Because CH chokes OH, they work well as a musical pair. Keep **CH** short and snappy, then let **OH** carry the longer metallic tail.

### 3. Keep RUIN below extremes until the kit is balanced

RUIN is easiest to control when the drum balances are already close. If you push it too early, you can lose the lane identities before the kit is established.

### 4. Treat reverb as a parallel bus

Patch the eight dry outputs into your mixer, then bring back **Reverb Out L/R** on a separate stereo return. That keeps kick and snare definition intact while letting hats, clap, and rim occupy more space.

### 5. Patch SC to external dynamics or modulation

The sidechain output is useful far beyond ducking. It can rhythmically open filters, reduce ambience, or drive a VCA on unrelated textures every time the kick hits.

### 6. Patch GRV somewhere interesting

GRV is not just another envelope follower. It carries some memory of the pattern's density, accent, and metallic motion. It works well on destinations that benefit from a slowly shifting rhythmic bias.

### 7. For heavier techno kicks

- raise **BODY**
- raise **PUNCH**
- set **CURVE** toward the slower side
- add a little **RUMBLE**
- use moderate **DRIVE**
- keep **RUIN** low unless you want deliberate destruction

### 8. For broken-metal hats and tops

- push **COLOR**
- push **RUIN** into the mid/high range
- increase **SNAP** and **TRANSIENT**
- shorten **CH** decay
- use **OH** or **Ride** for the longer metallic wash

---

FerroKlast is at its best when treated as a whole machine rather than eight isolated modules. Get the shared macro behavior right first, then use the lane trimmers to decide which hits anchor the groove and which ones spray texture around it.