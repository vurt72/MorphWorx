# MorphWorx

VCV Rack 2 and 4ms MetaModule plugin.

## Modules

### Phaseon1
4-operator phase modulation (PM/FM) synthesizer voice with user-loadable wavetable and formant shaper. Macro controls (TIMBRE, COLOR, DENSITY, MOTION) provide expressive performance shaping with extensive CV modulation.  Factory preset bank included.

### Trigonomicon
Generative drum trigger pattern generator. IDM, breakcore, amen-break, and drill-inspired rhythmic structures. Probability-weighted pattern mutation and clock-synced CV outputs.

### Septagon
Polyrhythmic drum pattern generator in 7/4. Phase-space warping, accent layers, and independent trigger densities per voice.

### Xenostasis
Autonomous hybrid synthesis organism. Self-regulating spectral drone combining wavetable oscillation with organic bytebeat cross-modulation and an internal metabolic feedback system.

---

## Installation

### VCV Rack 2 (Windows x64)
Download MorphWorx-x.x.x-win-x64.vcvplugin from [Releases](../../releases) and drop it into %LOCALAPPDATA%\Rack2\plugins-win-x64\. Restart Rack.

The **Phaseon1** preset bank (Phbank.bnk) and default wavetable (phaseon1.wav) are bundled inside the plugin and load automatically.

### 4ms MetaModule
1. Install MorphWorx.mmplugin via the MetaModule web UI.
2. Download MorphWorx-phaseon1-sdcard.zip from [Releases](../../releases), extract it, and copy the phaseon1/ folder to the **root of your SD card**.
   - sdc:/phaseon1/Phbank.bnk — factory preset bank (auto-loads)
   - sdc:/phaseon1/phaseon1.wav — default wavetable (auto-loads)
   - sdc:/phaseon1/xs_*.wav — optional extra wavetables (select in-module)

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
