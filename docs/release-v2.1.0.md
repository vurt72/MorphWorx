# MorphWorx v2.1.0

VCV Rack 2 and 4ms MetaModule full release.

## Highlights

- Full MorphWorx module lineup for the current plugin build.
- Rack release packaged as a `.vcvplugin`.
- MetaModule release packaged as a `.mmplugin`.
- Phaseon1 support bundle packaged separately as `MorphWorx-phaseon1-sdcard.zip`.
- MetaModule external file loading now prefers the current patch volume and then falls back across `sdc:/`, `usb:/`, and `nor:/` where applicable.

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
- `MorphWorx-phaseon1-sdcard.zip`

## Installation

### VCV Rack 2 (Windows x64)

1. Download `MorphWorx-2.1.0-win-x64.vcvplugin` from the release assets.
2. Drop it into `%LOCALAPPDATA%\Rack2\plugins-win-x64\`.
3. Restart Rack.

### 4ms MetaModule

1. Install `MorphWorx-2.1.0.mmplugin` with the MetaModule web UI.
2. If you want the factory Phaseon1 wavetable and preset bank available on removable or internal storage, extract `MorphWorx-phaseon1-sdcard.zip` onto the volume where your patch lives.
3. Valid destinations include `sdc:/phaseon1/`, `usb:/phaseon1/`, and `nor:/phaseon1/`.

## Notes

- Phaseon1 looks for its bank and default wavetable on the current patch volume first, then falls back across the other local volumes.
- Minimalith uses the same patch-volume-first behavior for external `.bnk` banks and `userwaveforms/` directories on MetaModule.
- The canonical module manual remains [docs/phaseon1-manual.md](docs/phaseon1-manual.md).