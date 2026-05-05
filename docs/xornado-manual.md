# XORnado Manual

XORnado is a compact single-voice bytebeat and digital-noise generator with 24 equations, smooth equation morphing, external clock/reset control, and a mutation stage that can either phase-distort the equation input or warp the internal free-running speed.

Unlike a conventional oscillator, XORnado builds sound from 8-bit integer formulas. The result ranges from chiptune melody and PWM tones to unstable digital percussion, harsh feedback textures, and evolving logic-noise patterns.

## Overview

- **EQ** scans 24 bytebeat equations and can morph smoothly between neighboring formulas.
- **SPEED** sets the internal rate at which the bytebeat time value `t` advances when the module is free-running.
- **PITCH** multiplies `t` before it hits the equation, shifting the harmonic density of most formulas.
- **LOOP** optionally wraps `t` to a fixed phrase length, but only when a clock is patched.
- **P0 / P1 / P2** are per-equation shaping parameters. Their effect depends entirely on the selected formula.
- **PM DEPTH** and the **PM/FM** switch turn the extra modulation input into either phase mutation or frequency warp.
- **OUT** is the mono blend between the two neighboring equations when EQ sits between integers.
- **OUT L** and **OUT R** expose the lower and upper equations directly, which makes XORnado useful as a stereo or dual-layer source.

XORnado is currently a **single bytebeat voice** with three related outputs, not a multichannel drum bank.

## Controls

- **EQ** selects the current bytebeat formula range from 0 to 23.
  The knob is continuous, not stepped in use: integer positions select one equation exactly, while in-between positions crossfade between the current equation and the next one.

- **SPEED** sets the internal free-running bytebeat rate.
  With no clock driving the module in Step Mode, higher values make `t` advance faster and usually brighten or densify the sound.

- **PITCH** multiplies `t` by 1 to 8 before evaluation.
  In practice this works like a harmonic multiplier rather than a calibrated oscillator pitch control.

- **LOOP** sets the phrase length for the internal time counter when a clock is patched.
  The available settings are:
  - `Free`
  - `16`
  - `32`
  - `64`
  - `128`
  - `256`
  - `512`
  - `1024`
  - `2048`
  - `4096`

- **P0** is equation parameter 0.
- **P1** is equation parameter 1.
- **P2** is equation parameter 2.
  These three knobs are deliberately generic because each formula uses them differently. Some equations treat them like divisors or shift amounts, others as oscillator ratios, masks, PWM thresholds, or feedback weights.

- **VOL** scales the output from 0 to 200%.
  At high settings, especially in bipolar mode, XORnado can produce very hot signals.

- **PM DEPTH** sets how strongly the PM input influences the current mutation mode.

- **PM/FM** chooses how the PM path is applied:
  - **PM** offsets the equation's internal time value before evaluation.
  - **FM** warps the free-running speed engine instead of the equation input.

## Inputs

- **TRIG / CLK** is the external trigger or clock input.
  Its meaning depends on the context-menu Step Mode setting:
  - With **Step Mode off**, each rising edge resets `t` to zero and also clears the internal phase accumulator.
  - With **Step Mode on**, each rising edge increments `t` by one step.

- **PM** is the mutation modulation source.
  If it is unpatched and PM Depth is above zero, XORnado uses the previous output sample as an internal self-feedback modulator.

- **DPT** modulates **PM DEPTH**.

- **EQ CV** scans the equation range.
  A full 0 to 10 V sweep covers the whole 0 to 23 equation span.

- **SPD CV** modulates **SPEED**.

- **VOL CV** modulates **VOL**.

- **LOOP CV** modulates **LOOP**.

- **P0 CV**, **P1 CV**, and **P2 CV** modulate the three generic equation parameters.

- **PTH CV** modulates **PITCH**.

Most of the CV inputs are normalized so that a roughly 0 to 10 V signal can sweep the full knob range.

## Outputs and LED

- **OUT** is the mono crossfade blend.
  At exact integer EQ values it matches both side outputs. Between integers it blends the lower and upper equations.

- **OUT L** is the lower equation only.
  If EQ is 7.35, **OUT L** carries equation 7.

- **OUT R** is the upper equation only.
  If EQ is 7.35, **OUT R** carries equation 8.

- **STEP LED** flashes whenever the module advances its internal time counter from a clock pulse or from the free-running speed engine.

## Equation Set

The equation names come directly from the current implementation.

| EQ | Name | Notes |
|----|------|-------|
| 0 | Hope | Airy bitwise chord texture |
| 1 | Love | Stepped harmonic lattice |
| 2 | Life | Dense cascading pattern |
| 3 | Age | Raw-`t` arp rotator |
| 4 | Morph | Dual-XOR oscillator with a saw floor |
| 5 | Monk | Vocal-ish gated formant behavior |
| 6 | NERV | Unstable digital crunch |
| 7 | Trurl | Tinny pulse/noise hybrid |
| 8 | Pirx | High-register alias texture |
| 9 | Snaut | Feedback-driven formula |
| 10 | Hari | Feedback-heavy sign-pattern variant |
| 11 | Kris | Bright reactive bit pattern |
| 12 | Tide | Melodic cascade formula |
| 13 | Bregg | Feedback hooks |
| 14 | Avon | Wide-range XOR pattern |
| 15 | Orac | Another feedback-heavy line |
| 16 | Mantis | Cross-modulated XOR texture |
| 17 | Kernal | SID-style pulse/PWM tone |
| 18 | Warp | Slow AM motion plus XOR colour |
| 19 | Phase | Self-product harmonic cycling |
| 20 | Ritual | Dual modulo beating |
| 21 | Vortex | XOR pair with a saw baseline |
| 22 | Helix | Dual saw XOR mask |
| 23 | Grieg | Parametric chiptune formula |

The names are best treated as starting points rather than strict categories. The three parameter knobs can push each equation well outside its default character.

## Clock, Loop, and Mutation Behavior

### Free-running behavior

With no trigger patched, XORnado advances `t` from its internal speed engine.

- **SPEED** controls how fast `t` advances.
- **FM** mode is most obvious here, because it directly modulates that internal speed engine.
- **LOOP** does nothing unless a clock is patched.

### Clocked reset behavior

With a trigger patched and **Step Mode off**:

- each rising edge resets `t` to zero,
- the phase accumulator also resets,
- the free-running speed engine still runs between resets.

This is the best mode when you want every incoming pulse to retrigger the same bytebeat phrase from its beginning.

### Clocked step behavior

With a trigger patched and **Step Mode on**:

- each rising edge advances `t` by exactly one,
- the module behaves more like a clocked bytebeat sequencer,
- PM remains useful because it offsets the evaluated time value,
- FM is much less central because the free-running speed engine is no longer what is driving the phrase.

### Loop behavior

Loop length only applies when the trigger/clock input is patched.

- `Free` leaves `t` unwrapped.
- `16` through `4096` wrap `t` to a power-of-two phrase length.

In other words, **LOOP is a clock-synchronized phrase wrap, not a free-running transport loop**.

### PM and FM

- In **PM** mode, the PM path offsets the equation's evaluated time value before the formula is run.
  This produces abrupt mutation, phase tearing, and self-feedback aliasing when the PM jack is unpatched.

- In **FM** mode, the PM path modulates the effective **SPEED** value used by the internal free-running engine.
  This is most audible when XORnado is free-running or when an external trigger is resetting phrases while the internal engine continues between resets.

If the **PM** input is unpatched and PM Depth is above zero, XORnado uses its own previous output sample as the modulator. That makes it easy to move from stable tones into unstable recursive digital behavior without extra patching.

## Context Menu

Right-click the module for two additional options.

- **Step Mode (clock = step t)**
  Toggles whether incoming clock pulses step the bytebeat counter forward or reset it to the start.

- **Bipolar ±10V**
  Changes the output mapping from unipolar 0 to 10 V to a bipolar, zero-centered version that reaches roughly ±10 V peaks.

Both settings are stored with the patch.

## Tips and Workflows

- Use **EQ** as a timbre scan, not just a preset selector.
  Slow CV into EQ is often more interesting than modulating P0/P1/P2 first, because it moves across whole formulas and also exposes the in-between crossfade states.

- Use **OUT L** and **OUT R** as a stereo pair when EQ sits between integers.
  They give you the two neighboring formulas separately, while **OUT** provides the blended center image.

- Use **Step Mode off** with a rhythmic reset source for repeatable phrases.
  This is the easiest way to get drum-like hits, repeated bytebeat attacks, or phrase-locked bass stabs.

- Use **Step Mode on** when you want the incoming clock to act like a sequencer advancing the formula one sample-state at a time.
  This often creates stepped melodies, gated logic rhythms, or pseudo-pattern memory.

- Treat **P0**, **P1**, and **P2** as algorithm-dependent macro controls.
  If an equation sounds dead or too static, sweep all three through broad ranges before abandoning it.

- Use **PM DEPTH** with the **PM** jack unpatched to exploit the built-in self-feedback path.
  Small amounts add grit; large amounts can push many equations into chaotic digital sputter.

- Use **FM** mode when XORnado is free-running and **PM** mode when the clock is acting like a step sequencer.
  That is not a hard rule, but it is the quickest path to results.

- Switch to **Bipolar ±10V** when XORnado is modulating filters, waveshapers, comparators, or other bipolar destinations.
  Stay in unipolar mode when you want a more conventional audio-rate signal or when your downstream destination expects only positive voltages.