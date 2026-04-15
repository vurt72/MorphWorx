# MorphWorx v2.1.3

VCV Rack 2 and 4ms MetaModule maintenance and feature release.

## Highlights

### Aetherion — Major Reverb Upgrade

- **New panel (12 HP):** Expanded from 10 HP to 12 HP with a full 3-column knob layout.
- **Diffusion knob + CV:** New 4-stage allpass diffusion chain (Schroeder) with dedicated knob and CV input. Smears early reflections before they hit the FDN.
- **Mod (Wow/Flutter) knob + CV:** WowFlutter modulation depth now has its own dedicated knob and CV input, decoupled from Lo-Fi depth.
- **Tilt EQ knob + CV:** New shelving EQ. Tilts the wet reverb tail brighter (+) or darker (−) without affecting the dry path.
- **Signal chain corrected:** Order is now Input → Predelay → Shimmer → Diffusion → FDN. Predelay now correctly creates the direct/reverb onset gap.
- **Freeze gate CV fixed:** Gate CV is now level-sensitive (≥1V holds freeze for as long as the signal is high). Previously it was toggle-on-rising-edge, which made it unusable from sequencers and envelopes.
- **Freeze shimmer runaway fixed:** Shimmer injection is gated off with the freeze blend ramp, preventing unbounded feedback buildup through the frozen FDN tail.
- **Click-free freeze:** Entry and exit ramp smoothly over ~5ms, eliminating the hard cut on freeze engage/release.
- **Freeze accuracy:** FDN freeze is now set at audio-rate instead of control-rate, eliminating a potential 15-sample lag on freeze entry.
- **Predelay knob curve:** Quadratic mapping gives 4× finer resolution in the Haas zone (0–40ms = bottom 40% of knob travel).
- **MetaModule CPU savings:** WowFlutter depth-0 early exit saves ~120 cycles/sample at default Mod=0. Shimmer fifth layer (±7 semitone) omitted on MetaModule — octave shimmer layer is preserved; saves ~70 cycles/sample when shimmer is on.
- **MetaModule widget positions corrected:** All knob rows updated to match the new faceplate layout so hardware knob mapping is accurate.

### Ferroklast — Snare 2 VAR Morphing

- **VARI knob morphs SN2 character:** VAR=0 gives a punchy hybrid/modern snare. VAR=1 delivers an authentic TR-808 snare — shorter tone decay, independent upper bridge-tone fade, static pitch (no chirp), and a widened wire buzz band for the classic warm snap character.
- **Save / Recall buttons (VCV Rack):** New SAVE and RECALL momentary buttons let you snapshot all current Ferroklast parameter values as user defaults. The snapshot persists across sessions (written to disk) and is embedded in patch JSON for portability between machines.

### Trigonomicon — MetaModule Fix

- **Knobs now visible on MetaModule:** The three main knobs (PATTERN, MUTATE, BASS) were using a VCV Rack-only slider widget that MetaModule cannot render, causing them to be invisible. Fixed to use standard `RoundSmallBlackKnob`.

## Included Modules

- Aetherion
- Phaseon1
- Trigonomicon
- Septagon
- SlideWyrm
- Minimalith
- Amenolith
- Xenostasis
- FerroKlast
- FerroKlast MM

## Release Assets

- `MorphWorx-2.1.3-win-x64.vcvplugin`
- `MorphWorx-v2.1.3.mmplugin`
