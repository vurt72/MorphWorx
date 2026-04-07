# MorphWorx

VCV Rack 2 and 4ms MetaModule plugin.

## Modules

### Trigonomicon
Generative drum trigger pattern generator. IDM, breakcore, amen-break, and drill-inspired rhythmic structures. Probability-weighted pattern mutation and clock-synced CV outputs.

### SlideWyrm
TB-303 style acid pattern generator with slide and accent handling.

### Septagon
Polyrhythmic drum pattern generator in 7/4. Phase-space warping, accent layers, and independent trigger densities per voice.

### Minimalith
Compact PreenFM2-based FM synth voice with bank loading and CV modulation.

### Amenolith
Five-instrument multisample drum player with kits, velocity layers, roll behavior, tune/length trim, and individual outputs.

### Phaseon1
4-operator phase modulation (PM/FM) synthesizer voice with user-loadable wavetable and formant shaper. Macro controls (TIMBRE, COLOR, DENSITY, MOTION) provide expressive performance shaping with extensive CV modulation. Factory preset bank included.

📖 **[Full Manual](docs/phaseon1-manual.md)**

### Xenostasis
Autonomous hybrid synthesis organism. Self-regulating spectral drone combining wavetable oscillation with organic bytebeat cross-modulation and an internal metabolic feedback system.

### FERROKLAST
Six-lane FM percussion synth for kicks, snares, metallic hats, and chaotic IDM percussion voices.

### FERROKLAST MM
MetaModule-focused FERROKLAST variant with a reduced voice set and trimmed feature surface.

## Official Release Set

The official MorphWorx release tracks these Rack modules:

- Amenolith
- FERROKLAST
- FERROKLAST MM
- Minimalith
- Phaseon1
- Septagon
- SlideWyrm
- Trigonomicon
- Xenostasis

The MetaModule build ships the same set except full FERROKLAST, which is replaced by the MetaModule-specific FERROKLAST MM.

---

## Installation

### VCV Rack 2 (Windows x64)
Download MorphWorx-x.x.x-win-x64.vcvplugin from [Releases](../../releases) and drop it into %LOCALAPPDATA%\Rack2\plugins-win-x64\. Restart Rack.

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
