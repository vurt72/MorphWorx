# Phaseon1 PM Golden Calibration

Purpose: lock PM/FM behavior for `Phaseon1` so algorithm switching remains consistent in punch and loudness.

Source of truth:
- `src/Phaseon1.cpp` (AlgoCal table in render path)

## Locked Algorithm Profile

Order:
- `Stack`
- `Pairs`
- `Swarm`
- `Wide`
- `Cascade`
- `Fork`
- `Anchor`
- `Pyramid`
- `Triple Carrier`
- `Dual Cascade`
- `Ring`
- `Dual Mod`
- `Mod Bus`
- `Feedback Ladder`

- `modMul`: `[0.80, 0.92, 0.70, 0.00, 0.84, 0.90, 0.76, 0.94, 0.68, 0.78, 0.74, 0.88, 0.98, 0.90]`
- `carrierMul`: `[0.90, 0.98, 0.85, 0.00, 0.94, 0.95, 0.93, 0.90, 0.86, 1.00, 0.92, 0.98, 0.92, 0.90]`
- `carrierFbMul`: `[0.40, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.28]`
- `carrierTrim`: `[1.06, 0.98, 1.02, 0.92, 1.01, 1.00, 1.00, 1.04, 0.96, 1.00, 0.98, 0.99, 1.03, 1.05]`
- `modBleed`: `[0.40, 0.35, 0.30, 0.22, 0.28, 0.31, 0.24, 0.27, 0.22, 0.16, 0.26, 0.22, 0.24, 0.20]`

## PM Safety Bounds

- `fmEnvBoost` bounded to `1.0..3.0`
- `fmCarrier` soft-limited with tanh to max effective `~2.0`
- `fmMod` soft-limited with tanh to max effective `~3.0`
- per-op mod PM depth clamped to `0.0..3.0`
- per-op carrier PM depth clamped to `0.0..2.2`
- `carrierSyncMul` bounded to `1.0..1.8`

## Golden Check States

Use these as regression references (same note, velocity, filter, output gain):

1. Low drive state
- Density `0.20`, Character `0.20`, Morph `0.50`, FM Env `0.40`, Sync Env `0.30`
- Expect: all algos clean/stable, no PM sputter, Wide quietest but present.

2. Mid musical state
- Density `0.45`, Character `0.45`, Morph `0.55`, FM Env `0.65`, Sync Env `0.40`
- Expect: Stack and Cascade punch similarly, Pairs and Dual Cascade stay cleaner, Wide and Triple Carrier remain broader and less dense.

3. High stress state
- Density `0.85`, Character `0.85`, Morph `0.70`, FM Env `1.00`, Sync Env `1.00`
- Expect: aggressive PM with no runaway pitch sync or sudden collapse.

## Retune Policy

If AlgoCal values are changed:
- Update this document in the same commit.
- Rebuild both targets:
- Rack: `MorphWorx: Build Rack (plugin.dll)`
- MetaModule: `MorphWorx: Build MetaModule (Release)`
- Re-check all three golden states above.
