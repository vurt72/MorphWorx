# MorphWorx v2.1.4

VCV Rack 2 and 4ms MetaModule release alignment and cleanup update.

## Highlights

### BLASK Removed

- **Removed from VCV Rack and MetaModule:** BLASK is no longer part of the MorphWorx release set.
- **Removed from the repository:** The experimental BLASK implementation and its MetaModule packaging assets have been deleted to keep the shipping surface and source tree aligned.

### Glitch Please Officialized

- **Now part of the official release set:** Glitch Please is no longer treated as a hidden extra.
- **Proper upstream attribution added:** Release-facing metadata and license documentation now credit **IDUM Firmware v.99** by **Eli Pechman / Mystic Circuits**.
- **New manual:** Glitch Please now has a dedicated MorphWorx manual and is included in the docs index.

### Release Plumbing Updated

- **Release script is version-dynamic:** `tools/_do_release.ps1` no longer hardcodes 2.1.3 paths and tags.
- **MetaModule tag releases now attach automatically:** The GitHub Actions MetaModule workflow now publishes `.mmplugin` assets on version tags without requiring a manual release toggle.
- **Single release page target:** The intended 2.1.4 release asset set is Windows, Linux, macOS x64, macOS arm64, plus MetaModule on the same GitHub release page.

## Included Modules

- Aetherion
- Amenolith
- FerroKlast
- FerroKlast MM
- Glitch Please
- Kinetrax
- Minimalith
- Phaseon1
- Septagon
- SlideWyrm
- Trigonomicon
- Xenostasis
- XORnado

## Release Assets

- `MorphWorx-2.1.4-win-x64.vcvplugin`
- `MorphWorx-2.1.4-lin-x64.vcvplugin`
- `MorphWorx-2.1.4-mac-x64.vcvplugin`
- `MorphWorx-2.1.4-mac-arm64.vcvplugin`
- `MorphWorx-v2.1.4.mmplugin`