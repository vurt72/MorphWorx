# MorphWorx Repo Cleanup Audit

Date: 2026-04-07

Scope: classify repo files and folders into four cleanup buckets without deleting anything blindly.

Official current Rack module set:

- Amenolith
- Ferroklast
- FerroklastMM
- Minimalith
- Phaseon1
- Septagon
- SlideWyrm
- Trigonomicon
- Xenostasis

Official current MetaModule set:

- Amenolith
- FerroklastMM
- Minimalith
- Phaseon1
- Septagon
- SlideWyrm
- Trigonomicon
- Xenostasis

## Summary

The repo is not in a state where broad deletion is safe. The main cleanup themes are:

- Generated and local-environment content exists alongside source and should be separated more aggressively with `.gitignore`.
- A few top-level folders are empty or clearly artifact-like.
- Some legacy filenames are still part of the active build and must not be deleted just because the names are old.
- Some old module files are already deleted in the working tree, which suggests cleanup is already underway and should be finished deliberately.

## KEEP

- `.github/`: active workflow and agent instructions live here.
- `docs/`: active manuals, release notes, and handoff docs.
- `Makefile`: current Rack build file.
- `metamodule/`: current MetaModule build, assets, and packaging.
- `phaseon1/`: factory bank and wavetable sources used by release packaging.
- `plugin.json`: authoritative Rack module list.
- `README.md`: current top-level project documentation.
- `res/`: active Rack assets and sample payloads.
- `src/`: active module and shared DSP code.
- `tools/`: current build, deploy, and release scripts.
- `userwaveforms/`: active factory payloads for Phaseon1 and Minimalith distribution.
- `THIRD_PARTY_LICENSES.md`: required licensing file.
- `Instructions.md`: keep for now until it is intentionally consolidated or removed.

## DELETE + GITIGNORE

- `build/`: generated Rack build output.
- `dist/`: generated release artifacts.
- `metamodule/build/`: generated MetaModule build output.
- `metamodule/metamodule-plugins/`: generated MetaModule release packages.
- `plugin.dll`: generated root-level binary artifact.
- `build_release.log`: generated build log.
- `build_err.txt`: generated error log.
- `build2/Rack-SDK/`: extracted SDK/toolchain content, not plugin source.

Recommended `.gitignore` additions:

- `build2/Rack-SDK/`
- `build_err.txt`
- `build_release.log`
- `plugin.dll`

## REMOVE CAREFULLY

- Removal pass completed on 2026-04-07 for:
  - `Csound/`
  - `dep/`
  - `presets/`
- tracked deleted legacy sources already absent from the working tree:
  - `src/BreakcoreDrums.cpp`
  - `src/DrumTrigger.cpp`
  - `src/Phaseon.cpp`
  - `tools/update_phaseon_lfos.py`
  Follow-up audit result: no live source, build, or tool references remain in the current tree.

## INVESTIGATE

- `kits/`: likely old source-material folder for drum samples, but current Amenolith runtime loads from `res/samples/amenolith/`, not `kits/`.
- `phaseon1-manual.md` at repo root: likely duplicate of `docs/phaseon1-manual.md`.
- `build2/` follow-up: workflow YAMLs were moved under `.github/workflows/`; only disposable SDK extraction content should live here now.
- `skills/` and `skills-lock.json`: currently ignored, but verify whether they are intentionally kept for local tooling.
- tool-state directories at repo root such as `.adal/`, `.agent/`, `.agents/`, `.augment/`, `.claude/`, `.cline/`, `.continue/`, `.cortex/`, `.firecrawl/`, `.openhands/`, `.roo/`, `.vscode/`, `.windsurf/`, and similar:
  these are almost certainly local tool state, but cleanup should be coordinated because they may matter to the current user workflow.
- `modules.jpg`: likely reference art, not part of the build.

## High-Risk Stale References

- `src/PhaseWarpedDrums.cpp` is not removable old code. It is the active Septagon implementation and defines `modelSeptagon` while using an older filename.
- `src/PhaseWarpedDrums.hpp` is therefore also active.
- `res/PhaseWarpedDrums.svg` may be stale, but only after confirming the active panel art path is `res/Septagon.png` everywhere.
- old Phaseon-named assets in `res/` and `metamodule/assets/` may be stale, but should only be removed after confirming nothing in Phaseon1 tooling or docs still depends on them.

## Verified Evidence

- `src/plugin.cpp` registers only the official current module set.
- `plugin.json` matches the official Rack release set.
- `metamodule/plugin-mm.json` matches the official MetaModule release set.
- `src/sampler/DrumKits.cpp` loads Amenolith content from `res/samples/amenolith/` in Rack builds and `samples/amenolith/` in MetaModule builds.
- `src/PhaseWarpedDrums.cpp` defines `modelSeptagon` and uses `res/Septagon.png`, so it is active under a legacy filename.

## Suggested Cleanup Order

1. Separate generated content from source by tightening `.gitignore`.
2. Confirm whether `kits/` is archival source material or dead duplicate content.
3. Consolidate duplicate docs such as the root-level `phaseon1-manual.md` if redundant.
4. Finish removing already-deleted legacy source files in a single intentional cleanup commit.
5. Rename legacy-but-active files like `PhaseWarpedDrums.*` only as a dedicated refactor, not as part of blind deletion.