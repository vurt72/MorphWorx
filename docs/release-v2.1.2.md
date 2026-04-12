# MorphWorx v2.1.2

VCV Rack 2 and 4ms MetaModule maintenance release.

## Highlights

### Aetherion
- **MetaModule faceplate fix:** Aetherion now loads its faceplate correctly on MetaModule. The panel path was pointing to a non-existent file.
- **MetaModule port layout:** All ports shifted down for better alignment on the MetaModule touchscreen.

### Phaseon1
- **MetaModule hang fix:** Phaseon1 no longer freezes on MetaModule startup. The module was attempting to write files to the SD card during initialization, which blocked the GUI thread indefinitely. All file access is now read-only at startup, matching how other working modules (e.g. Minimalith) handle bundled assets.
- **VCV Rack → MetaModule preset transfer:** Phaseon1 now embeds the full 127-slot preset bank and active slot index into the patch JSON via `dataToJson()`/`dataFromJson()`. When a user designs presets in VCV Rack and transfers the `.yml` patch file to MetaModule, the exact knob settings and selected preset are faithfully restored. Previously, MetaModule always overrode all parameters with hardcoded defaults, ignoring imported state.
- **Cold start safety preserved:** When no patch data is present (first use on MetaModule), Phaseon1 still falls back to safe defaults and the bundled factory bank.

### All Modules (VCV Rack)
- **MIDI learn / mapping indicator fix:** The colored mapping squares (used by VCV Rack's MIDI-MAP and MetaModule's knob assignment) now render correctly on top of all custom MVXKnob variants. Previously, custom knobs bypassed the `ParamWidget` draw chain, causing the indicator squares to be hidden behind the knob artwork.

## Included Modules

- Aetherion *(new in release assets)*
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

- `MorphWorx-2.1.2-win-x64.vcvplugin`
- `MorphWorx-2.1.2-lin-x64.vcvplugin`
- `MorphWorx-2.1.2-mac-x64.vcvplugin`
- `MorphWorx-2.1.2-mac-arm64.vcvplugin`
- `MorphWorx-v2.1.2.mmplugin`
