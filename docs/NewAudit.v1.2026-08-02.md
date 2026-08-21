# GameForgerAI follow-up audit

> **⚠️ ARCHIVED as of 2026-08-21.** Renamed from `NewAudit.md` (kept, not deleted). Fully superseded — both its findings (N1/N2 skeletal rigging bugs) were verified fixed in later audits and are documented history in `README.md`'s Alpha 0.56 changelog entry and `PROJECT_AUDIT_GAUNTLET_REPORT.md` §2.5. Nothing here is uncaptured. See **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)** for the active plan.

Audit date: 2026-08-02  
Compared: `audit.md`, `fix.md`, and the current workspace implementation  
Verification: source review plus the canonical `Build-Project.cmd` Debug editor build

## Executive summary

Only part of the first security phase from `fix.md` has been implemented. Script attachment and execution now use a shared project-path resolver, and Lua no longer opens the `io`, `os`, `package`, or `debug` libraries. The editor currently configures and builds successfully in Debug through `Build-Project.cmd`.

The product is not at the remediation guide's definition of done. The standalone runtime, data-loss protections, structured JSON/provider configuration, truthful AI settings, tests, CI, and most UI/build work remain outstanding. Asset confinement is still incomplete. Two bugs were also found in the newly added skeletal-animation implementation.

Status totals for the 22 original audit findings:

- Fixed: 0
- Partially fixed: 2 (`H2`, `L1`)
- Still outstanding: 20
- Newly identified defects: 2

## What has been fixed or improved

### H2 — partially fixed: script confinement and Lua libraries

- `Engine/src/Core/ProjectPaths.cpp` now performs component-wise, case-insensitive containment checks after `weakly_canonical` resolution and rejects absolute paths and disallowed extensions.
- `CreateScriptCommand` and `AttachScriptCommand` require `.lua` paths beneath `Game/Scripts`.
- `ScriptRuntime::startScript` validates again immediately before `luaL_loadfile`, so a traversal path loaded from a hand-edited scene is refused at execution time.
- Lua now opens only base, table, string, and math libraries, and explicitly removes `dofile` and `loadfile`. The previously exposed `io`, `os`, `package`, and `debug` libraries are no longer opened.

This is not fully fixed because scene deserialization still accepts and stores invalid script paths, attached paths are stored in their original rather than normalized form, other assets do not use the resolver, and there is no instruction budget, memory cap, repeated-error cutoff, or project trust prompt. The helper also does not optionally enforce “regular file” as proposed in `fix.md`.

### L1 — partially fixed: editor build verification

`Build-Project.cmd` successfully configured and built `GameForgerEditor` in Debug on 2026-08-02. This removes yesterday's uncertainty caused by running outside a Visual Studio developer environment. It does not verify Runtime, Release, a clean checkout, or tests.

## Original findings still outstanding

### High priority

- **H1 — Runtime does not run the authored game:** `Runtime/src/main.cpp` still only creates a GLFW/OpenGL window and clears it. It does not read `Project.json`, load the startup scene, render entities, or run scripts/animation. `Runtime/CMakeLists.txt` still links only Engine, GLAD, GLFW, and GLM.
- **H2 — security work incomplete:** see the partial-fix section above. In particular, model/font/texture/scene paths are not consistently confined and Lua resource limits/trust UX remain absent.
- **H3 — AI settings are not applied:** the UI still selects from hard-coded provider state and passes only a provider ID. `AIProviderClient` rereads endpoint/model from provider JSON, while `activeProvider`, `enabled`, `priority`, `capabilities`, and configured timeout behavior remain unused.
- **H4 — provider/response parsing is fragile:** `AIProviderClient.cpp` still searches formatted JSON text with `find`, and `AIChatResponse.cpp` still returns the first string field named `content` rather than structurally reading the documented response shape.
- **H5 — scene saves remain destructive:** `SceneSerializer.cpp` still writes the destination directly with `std::ios::trunc`. There is no temporary write/atomic replacement, backup, dirty flag, unsaved-change prompt, or recovery snapshot.

### Medium priority

- **M1 — visible commands remain inert:** Build Game, clickable Undo/Redo, Map Humanoid Skeleton, and Animation Library still ignore the return value of `ImGui::MenuItem`. Comments acknowledging placeholders do not disable or label them in the UI.
- **M2 — project metadata remains decorative:** no production consumer of `Game/Project.json` or its `startupScene` contract was found.
- **M3 — JSON parser still accepts invalid input:** `parseDocument` still does not require end-of-input; `parseNumber` still accepts invalid sign/punctuation sequences and does not verify `strtod` consumption; Unicode hex and surrogate handling remain incomplete.
- **M4 — scene schema/range validation remains weak:** scene format/version, duplicates, finite transforms, file/entity/array limits, terrain dimensions, scripts, and asset fields are still not comprehensively validated before use.
- **M5 — asset confinement remains incomplete:** imported models are still opened with `projectRoot / sourcePath`; fonts, textures/heightmaps, and serialized asset references do not consistently pass through `resolveProjectFile` at load and use boundaries.
- **M6 — JSON escaping remains incomplete:** scene and request serializers still do not robustly escape every control byte or validate UTF-8.
- **M7 — versions still disagree:** CMake now declares `0.55.0`, but the engine/network identifiers are not generated from one shared version source.
- **M8 — no automated tests:** no CTest setup, test targets, or registered tests exist.
- **M9 — no build/test CI:** `.github` still contains no workflow that builds and tests clean Debug/Release editor/runtime checkouts.
- **M10 — shutdown can block on AI:** WinHTTP timeouts remain hard-coded to 120 seconds and shutdown still joins worker threads without request cancellation.

### Low priority

- **L1 — only partially resolved:** only the Debug editor target was built; see above.
- **L2 — Assimp/CMake warnings remain:** the successful configure emitted repeated CMP0175 warnings from Assimp.
- **L3 — convenience build still builds only the editor:** `Build-Project.cmd` invokes only the `editor-debug` preset.
- **L4 — Save remains bound to `Ctrl+R`:** both the menu label and implementation still use the surprising shortcut.
- **L5 — generated directories remain in the workspace:** `out/` and `.vs/` remain present, and usable Git metadata is still unavailable, so tracked-state verification remains impossible.
- **L6 — plaintext local-key fallback remains:** `Game/AI/Providers.local.json` is still the credential mechanism; no Credential Manager integration, warning, or redacted example file was found. Key values were not copied or reported.
- **L7 — platform requirements remain implicit/restrictive:** the code remains directly tied to Win32/WinHTTP and OpenGL 4.6 without the recommended startup diagnostics or clearly enforced fallback policy.

## New defects found

### N1 — High: skinned models omit the scene root inverse transform

`ViewportRenderer::computeSkinningMatrices` calculates each palette entry as:

```text
animatedNodeWorld * inverseBindMatrix
```

The imported scene's inverse global/root transform is neither stored by `ModelImport` nor applied by the renderer. Assimp skeletal evaluation generally needs the mesh/scene global inverse in the palette calculation. Files whose root node carries coordinate-system, unit-scale, or exporter transforms can therefore be translated, rotated, or scaled incorrectly when animation is enabled, even when their static import looks correct.

**Recommended fix:** retain the imported scene global inverse (and the mesh-node relationship where applicable), calculate the palette in the same coordinate space used for vertices, and add a fixture whose root has a non-identity transform.

### N2 — Medium: partially keyed animation tracks discard bind-pose components

When an animation channel exists, `computeSkinningMatrices` replaces the complete local bind transform. Missing position keys default to `(0,0,0)`, missing rotation keys to identity, and missing scale keys to `(1,1,1)`. Valid animation channels may omit a component that should remain at its bind/local value. Such bones snap to the origin, lose their bind rotation, or lose their bind scale instead of preserving the unanimated components.

**Recommended fix:** decompose `localBindTransform` first and use its translation, rotation, and scale as the fallback for each empty key stream. Add tests for rotation-only, translation-only, and scale-only bone channels.

## Declared feature gaps still present

The original known-gap groups remain release concerns. Static import has recently gained an initial skeletal-animation preview path, but it still lacks a persistent generated asset, materials/textures, full hierarchy/submesh semantics, robust dependency copying, clip selection, runtime parity, and the validation/test matrix required by `fix.md`. Physics, localization, production rendering/audio/content tooling, and the Character workflows remain prototype or placeholder work.

## Recommended next work

1. Finish Phase 1: validate every serialized asset at load and use boundaries, normalize stored paths, add Lua execution/memory limits, and add security tests.
2. Implement atomic save, dirty prompts, and recovery before more authoring features.
3. Fix the two skeletal-palette defects and add small rigged-model tests.
4. Harden the JSON parser and scene schema, then replace provider string searches with structural parsing.
5. Extract shared runtime modules and make Runtime load `Project.json` and its startup scene.
6. Add CTest and Windows CI gates for Debug/Release editor and runtime; update the convenience build to cover both.

## Verification notes

- `Build-Project.cmd` completed successfully for `GameForgerEditor` Debug.
- The configure step still emitted Assimp CMP0175 developer warnings.
- Runtime and Release were not built by the canonical script, and no automated tests exist to run.
- Git comparison was unavailable because the workspace does not expose a usable `.git` repository; conclusions are based on the two reports and current files.
