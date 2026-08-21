# Phase 0.a — DEF-01, DEF-02, DEF-03 (Gauntlet Audit, high-severity)

**Agent:** Claude
**Date:** 2026-08-21
**Status:** done — verified

## Tickets covered
- **DEF-01** — `all-release` build broken by upstream `imguizmo` CMake target
- **DEF-02** — `Project.json` truncated on write (no atomic staging)
- **DEF-03** — Model/font/texture paths bypass `resolveProjectFile` (traversal risk)

## What changed
- `CMakeLists.txt` — marked the upstream `imguizmo` FetchContent target `EXCLUDE_FROM_ALL` (it was never linked against; `GameForgerImGuizmo` is the real target).
- `Editor/src/main.cpp` — `updateProjectStartupScene` now writes via tmp-file + `.bak` + rename, matching `SceneSerializer::saveScene`'s existing atomic-write pattern.
- `Engine/src/Editor/ViewportRenderer.cpp` — `ensureImportedMeshGpu`, `ensureTextMeshGpu`, `ensureTerrainLayerTexturesGpu` now resolve their `sourcePath`/`fontPath`/`diffusePath`/`normalPath`/`heightPath` through `core::resolveProjectFile()`, confined to `Game/Models`, `Game/Fonts`, `Game/Textures` respectively.

## Verification
- Debug build: clean (0 errors; pre-existing `[[nodiscard]]` C4834 warnings unrelated to this ticket, tracked separately as DEF-04).
- Release build: clean.
- `cmake --build --preset all-release`: previously failing, now clean (confirmed via a fresh `cmake --preset windows-x64` reconfigure + build, and confirmed no `imguizmo.lib` artifact gets produced under the default target).
- `ctest --test-dir out/build/windows-x64 -C Debug`: 9/9 passing, no regressions.
- Version bumped `CMakeLists.txt`/`README.md` 0.78 → 0.79 per this project's versioning convention.

## Notes for next reader
- The audit report's own file:line citations for DEF-03 (`TextMesh.cpp:322`, `TerrainTexture.cpp:142`) were wrong — the actual `projectRoot`-join call sites are all in `ViewportRenderer.cpp`. Grep before trusting a citation from `PROJECT_AUDIT_GAUNTLET_REPORT.md`, don't assume line numbers are current.
- R-14 (Phase 0.5) asks to re-verify whether asset confinement is complete beyond what this ticket touched (scene deserialization boundaries, etc.) — this ticket only covered the three `ViewportRenderer.cpp` call sites the audit named.
