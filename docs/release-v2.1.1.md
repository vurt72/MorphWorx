# MorphWorx v2.1.1

VCV Rack 2 and 4ms MetaModule maintenance release.

## Highlights

- Fixes a Phaseon1 wavetable regression that made WT presets sound wrong relative to their intended tone.
- Restores the expected Phaseon1 preset-switch behavior after the wavetable regression fix.
- Makes Phaseon1 COLOR behave consistently across presets as a pitch-safe spectral macro instead of acting like detune or pitch drift on some patches.
- Updates the Phaseon1 manual to match the corrected COLOR behavior.

## Included Modules

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

- `MorphWorx-2.1.1-win-x64.vcvplugin`
- `MorphWorx-2.1.1-lin-x64.vcvplugin`
- `MorphWorx-2.1.1-mac-x64.vcvplugin`
- `MorphWorx-2.1.1-mac-arm64.vcvplugin`
- `MorphWorx-v2.1.1.mmplugin`

## Notes

- This release is focused on correcting Phaseon1 behavior rather than introducing new modules or controls.
- Existing presets should now load and browse more reliably, especially those using the bundled Phaseon1 wavetable.
- COLOR is now intentionally consistent: it should brighten or roughen the tone without changing pitch.