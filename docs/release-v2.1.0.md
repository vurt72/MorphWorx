# MorphWorx v2.1.0

VCV Rack 2 and 4ms MetaModule full release.

## Highlights

- Full MorphWorx module lineup for the current plugin build.
- Rack release packaged as a `.vcvplugin`.
- MetaModule release packaged as a `.mmplugin`.
- MetaModule package now bundles the factory Phaseon1 and Minimalith runtime data directly inside `MorphWorx.mmplugin`.
- MetaModule external file loading prefers the current patch volume, then falls back across `sdc:/`, `usb:/`, and `nor:/`, and finally to the bundled plugin data.

## Included Modules

- Phaseon1
- Trigonomicon
- Septagon
- SlideWyrm
- Minimalith
- Amenolith
- Xenostasis
- FERROKLAST
- FERROKLAST MM

## Release Assets

- `MorphWorx-2.1.0-win-x64.vcvplugin`
- `MorphWorx-2.1.0.mmplugin`

## Installation

### VCV Rack 2 (Windows x64)

1. Download `MorphWorx-2.1.0-win-x64.vcvplugin` from the release assets.
2. Drop it into `%LOCALAPPDATA%\Rack2\plugins-win-x64\`.
3. Restart Rack.

### 4ms MetaModule

1. Install `MorphWorx-2.1.0.mmplugin` with the MetaModule web UI.
2. Phaseon1 and Minimalith factory data are bundled inside the `.mmplugin`, so no separate support archive is required for the default install.
3. If you want to override those defaults, place compatible files on `sdc:/`, `usb:/`, or `nor:/` and the loader will prefer the current patch volume first.

## Notes

- Phaseon1 looks for its bank and default wavetable on the current patch volume first, then falls back across the other local volumes, then the bundled plugin data.
- Minimalith uses the same patch-volume-first behavior for external `.bnk` banks and `userwaveforms/` directories on MetaModule, with bundled plugin defaults as the final fallback.
- The canonical module manual remains [docs/phaseon1-manual.md](docs/phaseon1-manual.md).