# GameForgerAI full-app audit

> **⚠️ ARCHIVED as of 2026-08-21.** Renamed from `audit.md` (kept, not deleted). The earliest audit in this project's history — fully superseded by later audits and now by **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)**. Nothing here is uncaptured except one nuance from its companion `fix.v1.2026-08-01.md` (Lua repeated-error cutoff / trust prompt), recovered as ticket R-16.

Audit date: 2026-08-01  
Scope: CMake/build configuration, Engine, Editor, Runtime, project/scene data, Lua scripts, AI-provider integration, repository hygiene, documentation, and automated verification.

## Executive summary

The editor contains a substantial working prototype, but it is not yet a shippable game editor/runtime pair. The largest functional gap is that `GameForgerRuntime` does not consume `Game/Project.json`, load `Startup.scene.json`, render entities, run Lua, or package a game; it only clears a GLFW window. There are also project-boundary/security problems around attached Lua scripts, several UI controls that do nothing or do not affect runtime behavior, weak hand-written JSON parsing, non-atomic scene saving, and no automated tests or CI.

Counts below distinguish confirmed defects from expected alpha limitations:

- Critical: 0
- High: 5
- Medium: 10
- Low: 7
- Known/declared product gaps: 6 groups

## High priority

### H1. The standalone runtime does not run the game

**Evidence:** `Runtime/src/main.cpp` creates an OpenGL 4.6 window, clears it to a solid color, and swaps buffers. It never reads `Game/Project.json`, loads a scene, creates a renderer, runs animation, starts Lua, handles game input, or loads assets. `Runtime/CMakeLists.txt` links only the small Engine/OpenGL/GLFW/GLM set and not the editor's scene, renderer, serializer, model, text, terrain, animation, or Lua implementations.

**Impact:** “Build Game” cannot produce a playable version of what is authored in the editor. The editor's Game view is the only game-like execution path.

**Recommendation:** Move reusable scene data, serialization, rendering, animation, model/text/terrain loading, and script runtime code into Engine/runtime libraries. Make Runtime read `Game/Project.json`, resolve `startupScene`, load it, and execute the same play loop as the editor. Add a packaged-game smoke test.

### H2. Attached scripts are not confined to the project and execute with full Lua standard libraries

**Evidence:** `EditorScene::AttachScriptCommand` checks only `exists(projectRoot / value.scriptPath)` and does not call `isInsideProject`, unlike `CreateScriptCommand`. `SceneSerializer` accepts arbitrary strings in every entity's `scripts` array. `ScriptRuntime::startScript` then evaluates `projectRoot / scriptPath` with `luaL_loadfile`. The runtime initializes Lua's standard libraries, which include filesystem/process-capable APIs such as `io` and `os`.

**Impact:** Opening an untrusted scene and pressing Play can execute a Lua file referenced through `..` traversal or an absolute path outside the project. A malicious script has the user's normal process permissions; this is not a sandbox despite README language calling it one.

**Recommendation:** Canonicalize every script path and reject anything outside `Game/Scripts`; reject absolute paths and traversal both when attaching and loading a scene. Expose an allowlisted Lua environment instead of `luaL_openlibs`, or clearly treat projects as trusted executable code and warn before first execution.

### H3. AI settings shown in the UI are not applied

**Evidence:** Settings exposes editable Endpoint and Model fields plus a “Calibration” state in `Editor/src/main.cpp`, but AI calls receive only a provider ID. `AIProviderClient` rereads endpoint/model from `Game/AI/Providers.json`; it never receives or reads the edited `AISetupState` values. The active provider is also initialized to hard-coded index 0 rather than reading `activeProvider` from the configuration. `timeoutSeconds`, `enabled`, `priority`, and `capabilities` are ignored.

**Impact:** Users can see a success/calibrated UI while requests still use different settings. Configuration has multiple sources of truth (hard-coded providers, editable transient UI state, and JSON).

**Recommendation:** Define one provider configuration model, parse it once with the real JSON parser, bind Settings directly to it, persist changes deliberately, and pass the resolved configuration to the client. Make “Calibrate/Test” perform an actual lightweight request and report its result.

### H4. The provider parser is formatting-dependent and can select values from the wrong object

**Evidence:** `AIProviderClient.cpp` searches literal text such as `"id": "<id>"` and then finds the next occurrence of keys without respecting JSON object boundaries. Strings are terminated at the next quote without handling escapes. A valid minified/reformatted file may fail, and a missing field can fall through into a later provider. `AIChatResponse.cpp` similarly returns the first key named `content` anywhere in a response rather than structurally reading `choices[0].message.content`.

**Impact:** Valid configuration or provider responses can be rejected or misread; malformed or reordered content may silently use the wrong endpoint/model/key name.

**Recommendation:** Use the existing structured JSON parser (after correcting M4 below) or a maintained JSON library, validate a schema, constrain lookups to the selected provider object, and parse the documented response/error shape.

### H5. Scene writes are destructive and non-atomic

**Evidence:** `saveScene` opens the destination with `std::ios::trunc` and writes directly to it. There is no temporary file, flush/close verification followed by atomic replace, backup, recovery file, or dirty-scene prompt before New/Open/exit.

**Impact:** A crash, disk-full condition, or power loss during save can destroy the last good scene. Users can also discard unsaved work without warning.

**Recommendation:** Write beside the destination, flush and close successfully, then atomically replace it; retain a backup or recovery copy. Track a dirty flag and prompt on New, Open, project exit, and window close. Add periodic opt-in recovery snapshots.

## Medium priority

### M1. Visible menu commands are inert

`File > Build Game...`, `Edit > Undo`, `Edit > Redo`, `Character > Map Humanoid Skeleton...`, and `Character > Animation Library...` create menu items without handling their return values. Undo/redo keyboard shortcuts exist, which makes the clickable Edit commands especially misleading. Disable unfinished commands and label them “Coming soon,” or connect them.

### M2. Project metadata is mostly decorative

`Game/Project.json` declares `startupScene`, provider config, skeleton profile, asset directories, and script directory, but code searches show no consumer for the project file. Paths are hard-coded throughout the editor. Parse and validate this file at startup, or remove fields that are not contracts yet.

### M3. The custom JSON parser accepts invalid documents and numbers

`Json.cpp::parseDocument` does not require end-of-input after the parsed value, so valid JSON followed by arbitrary junk succeeds. `parseNumber` permits `+`, repeated signs, and punctuation in invalid positions and calls `strtod` without checking where conversion stopped. Unicode escape handling does not validate hex or combine UTF-16 surrogate pairs. Tighten the parser or replace it; add malformed-input and Unicode tests.

### M4. Scene loading performs little schema/range validation

The loader does not validate the scene `format` or `version`. It silently defaults wrong field types, skips unnamed objects, accepts duplicate entity names/tags/scripts, arbitrary asset paths, non-finite/extreme transforms, invalid terrain dimensions, and unbounded arrays/file sizes. Validate before replacing the current scene and report field-level errors. Put explicit limits on file size, entity count, terrain resolution/heights, scripts, tags, and keyframes.

### M5. Asset paths from scene files are not consistently constrained

Imported mesh, font, terrain texture, and script references are read as plain strings. Several existence checks join them to `projectRoot`, but only script creation has an explicit project-boundary check. Normalize and require project-relative paths within the appropriate asset directory at import, command execution, deserialization, and use.

### M6. Scene save JSON escaping is incomplete

`SceneSerializer::escapeJson` escapes a few common characters but emits other bytes below U+0020 directly, which produces invalid JSON. The request-body escaper in `AIProviderClient` has the same general issue. Escape all control characters and validate UTF-8.

### M7. Configuration versions disagree

The CMake project reports `0.49.0`, while `Engine::version()` returns `0.1.0`, and the HTTP user agent is `GameForgerAIEditor/0.1`. Generate one version header from CMake and use it in UI, serialization compatibility, logging, and network identification.

### M8. No automated tests or CTest registration exist

There is no `enable_testing`, `add_test`, or test target. High-value unit targets are JSON parsing, scene round trips and validation, path containment, command undo/redo, animation interpolation, collision behavior, provider parsing, and AI response parsing. Add runtime/editor startup smoke tests where graphics automation permits.

### M9. No CI workflow verifies clean checkout builds

`.github` contains no visible build/test workflow, and FetchContent downloads dependencies during configure. Add pinned Windows CI for configure, Debug/Release builds, tests, and a clean-checkout smoke run. Consider dependency hashes/lock strategy and caching for reproducibility.

### M10. Closing the editor can block for the full network timeout

Shutdown joins all AI worker threads. WinHTTP timeouts are hard-coded to 120 seconds, and there is no cancellation path. Closing during a stalled request can make the app appear frozen for up to roughly two minutes per in-flight request. Use cancellation/stop tokens, close request handles on shutdown, and derive bounded timeouts from validated provider settings.

## Low priority

### L1. Build verification currently depends on the launch environment

The audit build reconfigured successfully but compilation stopped at `Runtime/src/main.cpp` because MSVC could not locate the standard header `cstdio`. This indicates the shell did not have a complete Visual Studio developer environment, not a demonstrated source defect. `Build-Project.cmd` correctly calls `vcvars64.bat`; make that script the documented canonical verification command and add CI evidence of a clean build.

### L2. CMake emits Assimp/CMake policy warnings

Reconfiguration under CMake 4.3 reports CMP0175 developer warnings from Assimp 5.4.3. Pin a supported CMake range/policy set or update the dependency when practical so genuine warnings remain visible.

### L3. Only the editor is built by the convenience build script

`Build-Project.cmd` builds `editor-debug` but not `runtime-debug`, despite the repository presenting both. Add separate flags or build both by default; always build Release in CI.

### L4. Keyboard shortcut choice for Save is surprising

Save is bound and labeled `Ctrl+R`, a convention usually associated with refresh/reload; `Ctrl+S` is expected. Unless there is a strong product reason, use `Ctrl+S` and reserve `Ctrl+R` for reload/recompile.

### L5. Generated/local directories are present in the workspace

`out/` and `.vs/` contain large generated artifacts and IDE snapshots. They are ignored by `.gitignore`, but this workspace has no readable Git metadata, so tracked-state verification was impossible. Ensure neither directory is present in source archives and initialize/restore repository metadata before release.

### L6. Local API keys are stored in plaintext

`Game/AI/Providers.local.json` currently contains two non-empty keys (values were not copied into this report) and is ignored. Ignoring prevents normal Git commits but does not protect backups, malware, shared machines, accidental archives, or IDE snapshots. Prefer environment variables or Windows Credential Manager; add a startup warning if plaintext local secrets are used and provide a redacted example file.

### L7. Platform requirements are implicit and restrictive

Editor code directly includes Win32/WinHTTP APIs and both apps require OpenGL 4.6. Make Windows and GPU/driver requirements prominent, fail with actionable diagnostics, and avoid suggesting portability until platform abstractions exist.

## Known and declared product gaps

These are not newly discovered regressions, but they are unfinished user-facing areas that should remain explicit in release criteria:

1. Model imports flatten meshes and omit materials, textures, hierarchy, skinning, and skeletal animation.
2. Humanoid mapping and animation-library UI are placeholders; AI animation commands are not connected through the general scene command executor.
3. Physics is a small script-side, discrete AABB resolver with no swept collision or rotation-aware colliders; tunneling and unstable edge cases are expected.
4. French and Mandarin settings are placeholders with no localization resources.
5. The renderer/content pipeline does not yet cover a production game's lighting/material/audio needs.
6. The README identifies the project as Alpha 0.49; feature claims should clearly distinguish editor preview behavior from standalone runtime behavior.

## Recommended order of work

1. Decide the trust model for projects, then fix script/asset path confinement and Lua capabilities.
2. Create a shared engine/gameplay layer and make Runtime load and execute the configured startup scene.
3. Add dirty tracking plus atomic scene saves and recovery.
4. Replace or harden JSON/provider parsing and validate project/scene schemas.
5. Make Settings and all enabled menu items truthful and functional.
6. Add unit tests, clean-checkout CI, and Debug/Release editor/runtime build gates.
7. Address the remaining alpha content/rendering/physics gaps according to the intended first playable game.

## Verification notes

- Read-only source scans covered all first-party C++, headers, Lua/project configuration, CMake files, and launch/build scripts; vendored STB headers and generated dependency/build trees were excluded from code-quality findings.
- No API key value was included in this report.
- A build was attempted with the existing `out/build/windows-x64` tree. CMake configure/generate completed, then MSVC failed before compiling project code because the invoked environment could not find a C++ standard-library header. Consequently this audit does **not** claim either a successful clean build or a compiler-detected application error.
- Git status/tracked-file checks could not run because this workspace does not expose a `.git` repository.
