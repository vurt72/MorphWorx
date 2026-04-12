# Amenolith — User Manual

**MorphWorx** · VCV Rack 2 and 4ms MetaModule

---

## Contents

1. [Overview](#1-overview)
2. [What Amenolith Does](#2-what-amenolith-does)
3. [Signal Flow](#3-signal-flow)
4. [Top Row Controls and Jacks](#4-top-row-controls-and-jacks)
5. [Per-Instrument Rows](#5-per-instrument-rows)
6. [How Layer Selection Works](#6-how-layer-selection-works)
7. [Kit Scramble](#7-kit-scramble)
8. [Humanize and Generative Pitch](#8-humanize-and-generative-pitch)
9. [Outputs and Mix Behavior](#9-outputs-and-mix-behavior)
10. [Tips and Workflows](#10-tips-and-workflows)

---

## 1. Overview

**Amenolith** is a **six-instrument layered drum sampler** with **10 kits**, **3 sample layers per instrument**, per-row round-robin behavior, pan/level/tune/length trims, accent-aware layer selection, stereo mix outputs, individual instrument outputs, and a mix envelope output.

The six instruments are:

- **BD**: bass drum
- **SN**: main snare
- **SN2**: secondary snare / ghost snare
- **CH**: closed hat
- **OH**: open hat
- **RC**: ride/crash lane

Amenolith is designed as a playable drum bank rather than a fixed stereo loop player. Each lane has its own trigger input and output, while the module also provides a shared stereo mix when you want to use it as a compact drum source.

---

## 2. What Amenolith Does

Each instrument loads from the selected **kit** and one of three **layers**.

The active layer can be chosen by:

- the **manual LAYER switch**,
- per-row **RR** mode,
- **velocity input**,
- or **polyphonic accent CV**.

This makes Amenolith useful in a few different roles:

- a straightforward multi-out drum module,
- a dynamic kit that reacts to velocity/accent,
- a slightly unstable breakcore or jungle drum source using humanize and generative pitch,
- or a compact percussion bank with quick kit scrambling.

---

## 3. Signal Flow

At a high level, each row works like this:

`TRIG -> choose kit -> choose layer -> apply tune/length/gain -> optional hat shaping -> row output`

Then the row either:

- goes to its **individual output** if that output is patched,
- or gets summed into the **stereo mix bus** with its pan setting if the row output is unpatched.

After the stereo bus is summed:

- **DRIVE** is applied to the mix bus,
- **ENV OUT** follows the stereo mix envelope,
- and the final signal appears at **MIX L** and **MIX R**.

Important behavior:

- Individual outputs are **pre-mix-bus** taps.
- If a row output is connected, that row is removed from the stereo mix.
- **CH** chokes **OH**, so the two hat rows behave like a classic closed/open pair.

---

## 4. Top Row Controls and Jacks

### KIT

Selects the active kit from **1 to 10**.

### KIT CV

Adds kit offset in whole-kit steps. The module quantizes the result, so it behaves like kit stepping rather than continuous morphing.

### SCR

Triggers **kit scramble**. This does not randomize everything indiscriminately. See [Kit Scramble](#7-kit-scramble).

### ACC

Accent CV input.

This input has two modes:

- **mono accent**: global accent/gain behavior
- **poly accent**: per-row dynamic layer and gain behavior

### SENS

Accent sensitivity. This controls how strongly poly accent CV pushes layer selection.

### DRV CV

Additive CV for mix drive.

### DRV

Drive amount for the stereo mix bus. This does **not** affect individual row outputs if they are patched directly.

### HUM

Humanize amount. This introduces small trigger timing and gain variation.

Important detail:

- if **poly accent mode** is active, humanize is disabled so the row-to-row accent logic stays deterministic and clear.

### LAYER

Manual layer selection switch: **1**, **2**, or **3**.

This is the fallback or base layer when neither velocity nor poly accent is taking control.

### PITCH

Enables or disables **generative pitch**.

When enabled, Amenolith applies trigger-driven semitone changes on selected rows. See [Humanize and Generative Pitch](#8-humanize-and-generative-pitch).

### ENV OUT

Outputs a 0 to 10 V envelope follower from the stereo mix bus.

### MIX L / MIX R

Stereo mixed outputs for any rows whose individual outputs are not currently patched.

---

## 5. Per-Instrument Rows

Each row has the same basic structure.

### TRIG

Trigger input for that instrument.

### RR

Three-position round-robin mode switch:

- **OFF**: use the manual LAYER switch unless velocity or poly accent overrides it
- **EUC**: Euclidean-style repeating layer rotation
- **DENS**: density/timing-aware layer variation

### VEL

Velocity input for the row.

When patched, velocity has top priority for layer choice:

- low voltage -> layer 1
- mid voltage -> layer 2
- high voltage -> layer 3

It also changes gain slightly.

### TUNE CV

1 V/oct style tune modulation input for the row.

Important detail:

- when **generative pitch is on**, this external tune CV is bypassed for rows where generative pitch applies.

### PAN

Stereo pan for the row on the internal mix bus.

### LVL

Row level.

### SEMI

Discrete semitone transposition for the row, from **-3 to +3 semitones**.

### LENGTH

Per-row sample length trim.

This is not just a hard cut. Amenolith applies a short fade near the end so shortened samples feel more musical and less clicky.

### OUT

Individual output for that row.

If this jack is connected, the row is removed from the internal stereo mix.

---

## 6. How Layer Selection Works

Amenolith has **3 layers per instrument** inside each kit. The active layer is chosen in this order of priority.

### 1. Velocity input wins

If a row's **VEL** input is patched, that row selects its layer directly from input level.

This gives you the most explicit dynamic control and is the best choice when sequencing from a velocity-capable source.

### 2. Poly accent mode comes next

If **ACC** is connected with more than one channel, Amenolith treats it as **poly per-row accent**.

In this mode, the module uses:

- accent level,
- row type,
- recent timing context,
- and the **SENS** control

to choose the layer for each row.

This is more nuanced than simple threshold switching. Different rows bias differently so kicks, snares, hats, and ride/crash do not all react identically.

### 3. Manual layer plus RR mode

If neither velocity nor poly accent is controlling the row, the manual **LAYER** switch sets the base layer.

Then the row's **RR** mode decides whether to keep that layer fixed or vary it.

### RR modes in practice

**OFF**:

- always use the manual layer.

**EUC**:

- introduces a repeating, evenly spaced variation pattern.
- useful when you want predictable but non-static alternation.

**DENS**:

- changes layer behavior according to recent hit density.
- useful for breakbeat-like passages where fast repeats should feel more animated than isolated hits.

---

## 7. Kit Scramble

Pressing **SCR** activates a per-instrument kit scramble.

Behavior:

- **BD**, **CH**, **OH**, and **RC** can be reassigned to random kits
- **SN** and **SN2** stay on the current selected kit

That design keeps the snare core stable while letting the kick and cymbal family jump around.

Important details:

- scramble state is stored with the patch
- changing the main **KIT** selection clears the scramble state

This makes scramble useful for controlled auditioning rather than turning the module into full chaos every time the kit knob moves.

---

## 8. Humanize and Generative Pitch

### Humanize

**HUM** adds small trigger timing offsets and correlated gain changes.

General character:

- low values: subtle pocket movement
- mid values: looser timing and more natural instability
- high values: pronounced slop and more aggressive variation, especially on hats

Row behavior is not identical:

- **BD** is tighter
- **SN** and **SN2** are moderate
- **CH** and especially **OH** get the loosest feel

This helps Amenolith stay musical instead of making every row equally random.

### Generative Pitch

With **PITCH** enabled, Amenolith adds deterministic trigger-driven semitone changes on repeating hits.

This applies to all rows **except**:

- **BD**
- **OH**
- **RC**

So the main targets are:

- **SN**
- **SN2**
- **CH**

The system is phrase-aware rather than fully random. Repeated hits tend to descend or vary within a constrained table of semitone offsets, while longer gaps reset the phrase behavior.

Accent and high velocity can bias the result back toward more centered pitches.

This works well for:

- breakcore snare flicker
- shifting hat figures
- slight melodic movement in repeated percussion hits

Important detail:

- when generative pitch is active for a row, the row's external **TUNE CV** input is no longer used for that row.

---

## 9. Outputs and Mix Behavior

### Individual row outputs

Each row has a dedicated output:

- **BD OUT**
- **SN OUT**
- **SN2 OUT**
- **CH OUT**
- **OH OUT**
- **RC OUT**

If a row output is patched:

- that row still plays normally,
- but it no longer contributes to the internal stereo mix.

This is useful when you want hybrid routing, for example:

- kick and snare on separate mixer channels,
- hats and RC left on the internal stereo pair.

### MIX L / MIX R

These outputs contain only the unpatched rows mixed together and then driven by the **DRV** stage.

### ENV OUT

This is a post-mix envelope follower derived from the internal stereo bus. It is useful for:

- ducking external material
- modulating filters or VCAs
- driving sidechain-style movement elsewhere in the patch

Because it follows the internal mix, it is most informative when you are using Amenolith as a bus-based drum source rather than splitting every row out separately.

---

## 10. Tips and Workflows

### 1. Use velocity when you want explicit dynamic control

If your sequencer can send velocity, patch it into the row **VEL** input. That gives the most direct and predictable layer selection.

### 2. Use poly accent when you want behavior, not just louder hits

Poly accent does more than scale volume. It changes row-specific layer choice and feel. This is the better choice when patching from a module like Trigonomicon and you want the kit to feel alive.

### 3. Keep RR mode for pattern texture, not for basic dynamics

RR is best when the incoming triggers are fairly static and you want internal movement. If you already have good velocity or accent data, RR often becomes secondary.

### 4. Split only the rows you actually need to process separately

Because patched row outputs are removed from the internal mix, you can build efficient hybrids:

- patch **BD** and **SN** out individually
- leave hats and RC in the stereo bus
- use **ENV OUT** from that remaining mix for modulation elsewhere

### 5. Use LENGTH as a shape control, not only as a mute-trim

Shortening **CH**, **OH**, and **RC** is often the fastest way to make a kit tighter without changing the kit itself.

### 6. Use SCR to audition tops and kick families quickly

Because scramble keeps the snare rows anchored to the current kit, it is especially useful for finding a new kick/cymbal identity without losing the center of the groove.

### 7. Turn off humanize when checking groove accuracy

If timing feels off while debugging a patch, set **HUM** to zero first. Amenolith can intentionally drag or push rows slightly, especially hats.

### 8. Try generative pitch on repeated CH or SN2 figures

That is where the effect becomes most obvious without destabilizing the whole kit.

---

Amenolith works best when you decide early whether it should behave like a straight sampler, a dynamic accent-reactive drum bank, or a semi-generative break tool. The controls are compact, but the interaction between velocity, poly accent, RR, humanize, and pitch mode gives it a much wider range than a simple six-row sample player.