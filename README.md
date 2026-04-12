# MorphWorx

VCV Rack 2 and 4ms MetaModule plugin.

📚 **[Documentation Index](docs/README.md)**

## Available Manuals

- **[Aetherion Manual](docs/aetherion-manual.md)**
- **[Amenolith Manual](docs/amenolith-manual.md)**
- **[Phaseon1 Manual](docs/phaseon1-manual.md)**
- **[FerroKlast Manual](docs/ferroklast-manual.md)**

## Modules

### Aetherion
Stereo hall reverb with lo-fi tape degradation in the feedback loop. 8-line FDN with Hadamard mixing, shimmer pitch shifting (octave + fifth), freeze, swell envelope follower, and three character modes (Clean, Warm, Degrade).

📖 **[Full Manual](docs/aetherion-manual.md)**

### Trigonomicon
Generative drum trigger pattern generator. IDM, breakcore, amen-break, and drill-inspired rhythmic structures. Probability-weighted pattern mutation and clock-synced CV outputs.

### SlideWyrm
TB-303 style acid pattern generator with slide and accent handling.

### Septagon
Polyrhythmic drum pattern generator in 7/4. Phase-space warping, accent layers, and independent trigger densities per voice.

### Minimalith
Compact PreenFM2-based FM synth voice with bank loading and CV modulation.

### Amenolith
Six-instrument layered drum sampler with 10 kits, 3-layer velocity/accent response, per-row round robin, and individual plus stereo mix outputs.

📖 **[Full Manual](docs/amenolith-manual.md)**

### Phaseon1
4-operator phase modulation (PM/FM) synthesizer voice with user-loadable wavetable and formant shaper. Macro controls (TIMBRE, COLOR, DENSITY, MOTION) provide expressive performance shaping with extensive CV modulation. Factory preset bank included.

📖 **[Full Manual](docs/phaseon1-manual.md)**

### Xenostasis
Autonomous hybrid synthesis organism. Self-regulating spectral drone combining wavetable oscillation with organic bytebeat cross-modulation and an internal metabolic feedback system.

### FerroKlast
Eight-lane hybrid FM percussion synth for kick, dual snares, hats, ride, clap, and rim with dedicated wet reverb outputs and groove CV.

📖 **[Full Manual](docs/ferroklast-manual.md)**

### FerroKlast MM
MetaModule-focused FerroKlast variant with a reduced voice set and trimmed feature surface.

## Official Release Set

The official MorphWorx release tracks these Rack modules:

- Aetherion
- Amenolith
- FerroKlast
- FerroKlast MM
- Minimalith
- Phaseon1
- Septagon
- SlideWyrm
- Trigonomicon
- Xenostasis

The MetaModule build ships the same set except full FerroKlast, which is replaced by the MetaModule-specific FerroKlast MM.

---

## Installation

### VCV Rack 2
Download the platform-specific `.vcvplugin` from [Releases](../../releases), install it in your Rack 2 plugin folder, and restart Rack.

The **Phaseon1** preset bank (Phbank.bnk) and default wavetable (phaseon1.wav) are bundled inside the plugin and load automatically.

### 4ms MetaModule
1. Install MorphWorx.mmplugin via the MetaModule web UI.
2. Phaseon1 and Minimalith factory data now ship inside MorphWorx.mmplugin, so no separate support zip is required for the default experience.
3. On MetaModule, both modules still prefer external files on the current patch volume first, then fall back across the other local volumes and finally the bundled plugin data.
4. Optional external overrides can live on `sdc:/`, `usb:/`, or `nor:/` using the same paths already supported by the loaders:
   - `phaseon1/Phbank.bnk`
   - `phaseon1/phaseon1.wav`
   - `minimalith/Default.bnk`
   - `minimalith/userwaveforms/`
   - `MorphWorx/phbank.bnk`
   - `MorphWorx/Default.bnk`

---

## License

MorphWorx is free software licensed under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later).  
See the [SPDX identifier in plugin.json](plugin.json) and <https://www.gnu.org/licenses/gpl-3.0.html> for the full text.

### Third-party code

This plugin incorporates portions of code from third-party open-source projects. Full copyright notices and license texts are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

| Component | Source | License |
|-----------|--------|---------|
| TIMBRE & COLOR macros (Phaseon1) | [Mutable Instruments Plaits](https://github.com/pichenettes/eurorack) — Copyright (c) 2021 Emilie Gillet. | MIT |
| Quantizer scale tables (SlideWyrm) | [Mutable Instruments Braids](https://github.com/pichenettes/eurorack) / [Ornament & Crime](https://github.com/mxmxmx/O_C) | MIT |
| PreenFM2 synth engine (Minimalith) | [PreenFM2](https://github.com/Ixox/preenfm2) — Xavier Hosxe | GPL-3.0 |
