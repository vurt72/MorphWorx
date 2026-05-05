# Glitch Please Manual

Glitch Please is an IDUM-derived gate and clock mangler for VCV Rack 2 and 4ms MetaModule.
It is based on IDUM Firmware v.99 by Eli Pechman / Mystic Circuits and ports that timing-manipulation idea set into MorphWorx.

At a high level, Glitch Please listens to one clock lane and four trigger lanes, then applies probabilistic modifications to timing, repetitions, order, skips, loop playback, and ghost-note behavior.

## Core Controls

- `MODE`: Selects one of the eight modification modes.
- `PROB`: Sets the probability that the selected modification will engage on a given clock step.
- `LENGTH`: Sets how long the modification lasts once engaged.
- `PARAM`: Mode-specific depth or behavior control.

## Buttons And Switches

- `CYCLE`: Cycles through active modes.
- `MODE`: Manual mode-step button.
- `LOOP`: Captures and enables loop playback.
- `PARAM RES`: Quantizes the parameter control to odd, even, or power-of-two style steps.
- `LENGTH RES`: Quantizes the length control with the same stepped options.
- `MERGE`: Chooses how looped material combines with incoming trigger data.
  - `Replace`
  - `Add`
  - `Cut`
- `GHOST`: Selects ghost timing style.
  - `Flam`
  - `Ghost`
  - `Drag`

## Modes

Glitch Please exposes eight timing and trigger modification modes:

- `Hold`
- `Burst`
- `Ratchet`
- `Ball`
- `Rotate`
- `Delay`
- `Break`
- `Skip`

These modes operate probabilistically according to the current `PROB`, `LENGTH`, and `PARAM` settings.

## Inputs

- `CLOCK`: Master clock input.
- `TRIG 1-4`: Four trigger lanes processed alongside the master clock.
- `MODE CV`: CV over mode selection.
- `PROB CV`: CV over modification probability.
- `LENGTH CV`: CV over modification length.
- `PARAM CV`: CV over the mode-specific parameter.
- `LOOP GATE`: Enables or controls loop behavior depending on configuration.
- `MERGE CV`: CV over merge policy selection.
- `GHOST CV`: CV over ghost timing selection.

## Outputs

- `CLOCK`: Modified clock output.
- `TRIG 1-4`: Modified trigger lanes.
- `GHOST`: Additional ghost/flam/drag timing output.

## Looping

Glitch Please can capture a recent section of activity and replay it as a loop.
Internally, the loop engine records up to 16 steps of trigger history with sub-clock timing detail so loop playback can preserve timing relationships rather than only whole-step events.

## Attribution

Glitch Please is a MorphWorx derivative port of IDUM Firmware v.99 by Eli Pechman / Mystic Circuits.
The adapted source in `src/GlitchPlease.cpp` carries the original Creative Commons Attribution-ShareAlike 4.0 notice.
See [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md) for the release attribution and license summary.