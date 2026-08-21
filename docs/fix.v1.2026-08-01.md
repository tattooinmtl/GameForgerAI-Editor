# GameForgerAI remediation guide

> **⚠️ ARCHIVED as of 2026-08-21.** Renamed from `fix.md` (kept, not deleted). Superseded by **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)**. Its Lua repeated-error-cutoff/trust-prompt idea (not carried into any later plan) is recovered as ticket R-16. Read the RoadMap first.

This guide turns every finding in `audit.md` into an implementation plan. Complete the phases in order: security and data safety come before new runtime features, and automated tests should be added alongside each fix rather than postponed until the end.

## Definition of done

The remediation is complete when all of the following are true:

- Untrusted scene data cannot reference or execute files outside the project.
- Saving cannot destroy the last valid scene, and unsaved work is never silently discarded.
- The standalone runtime loads `Game/Project.json`, opens its startup scene, renders it, and runs scripts/animation.
- AI settings have one source of truth and every enabled control changes actual behavior.
- Project, scene, and provider JSON is structurally parsed and validated with useful errors.
- Editor and runtime build in Debug and Release from a clean checkout.
- Unit tests and startup smoke tests run in CI.
- Unfinished UI is either connected or visibly disabled.

## Phase 1: secure project files and Lua execution

### 1. Create one reusable safe-path function

Move project-boundary validation out of `EditorScene` into a shared utility, for example:

- `Engine/include/GameForger/Core/ProjectPaths.hpp`
- `Engine/src/Core/ProjectPaths.cpp`

Provide a function shaped like:

```cpp
std::optional<std::filesystem::path> resolveProjectFile(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& relativePath,
    const std::filesystem::path& requiredSubdirectory);
```

It must:

1. Reject empty and absolute input paths.
2. Canonicalize the project root and required directory.
3. Resolve the candidate with `weakly_canonical` using an `error_code` overload.
4. Compare path components, not string prefixes.
5. Reject candidates outside the required directory.
6. Optionally require a regular file and allowlisted extension.
7. Return the normalized project-relative path used for serialization.

Do not use a raw string prefix check for security. On Windows, account for case-insensitive paths, drive letters, junctions, and symlinks.

Apply the function at every boundary:

- Scripts: require `Game/Scripts` and `.lua`.
- Models: require `Game/Models` and `.glb`, `.gltf`, `.fbx`, `.3ds`, `.obj`, or `.blend`.
- Fonts: require `Game/Fonts` and the supported font extensions.
- Textures/heightmaps: require `Game/Textures` and supported image extensions.
- Scene files: require `Game/Scenes` and `.scene.json` where appropriate.
- Skeletons/animations: constrain them to their configured asset directories.

Update these locations first:

- `Editor/src/EditorScene.cpp`: `CreateScriptCommand`, `AttachScriptCommand`, imported model creation, font assignment.
- `Editor/src/ScriptRuntime.cpp`: validate again immediately before `luaL_loadfile`.
- `Editor/src/SceneSerializer.cpp`: validate all asset references during load.
- `Editor/src/ViewportRenderer.cpp`, `TextMesh.cpp`, and `ModelImport.cpp`: treat invalid resolved paths as errors, not as paths to open.

Validation at both load time and use time is intentional defense in depth.

### 2. Choose and enforce a Lua trust model

The safest model for a game editor is “project scripts are executable code, but receive only an allowlisted game API.” Replace `luaL_openlibs` with selected libraries:

```cpp
luaL_requiref(L, "_G", luaopen_base, 1);
lua_pop(L, 1);
luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
lua_pop(L, 1);
luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
lua_pop(L, 1);
luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
lua_pop(L, 1);
```

Do not expose `io`, `os`, `package`, `debug`, `dofile`, or `loadfile` unless the product explicitly needs them. If module imports are required later, implement a project-local loader that resolves only beneath `Game/Scripts`.

Also:

- Put an instruction-count hook or per-frame execution budget on Lua callbacks so an infinite loop cannot hang the editor.
- Cap Lua memory with a custom allocator if projects may be untrusted.
- Stop a script after repeated errors and include entity/script names in the log.
- Show a one-time “projects contain executable scripts” trust prompt before Play for projects obtained externally.
- Correct the README: call Lua “restricted” only after these changes; otherwise call projects trusted code.

### 3. Protect API credentials

Keep `Game/AI/Providers.local.json` ignored, but migrate secrets toward this priority:

1. Environment variable.
2. Windows Credential Manager entry keyed by provider and project/user.
3. Plaintext local file only as an explicit development fallback.

Add `Game/AI/Providers.local.example.json` containing empty values. At startup, warn if the plaintext fallback is used. Never log authorization headers, keys, full provider request bodies containing secrets, or local-secret file contents.

Rotate the two current keys after making this change if this workspace has ever been archived, shared, or uploaded.

### Phase 1 tests

Add tests proving rejection of:

- `../outside.lua`
- absolute drive and UNC paths
- mixed `.` and `..` components
- a junction/symlink escaping the project
- a `.lua` file outside `Game/Scripts`
- external font/model/texture references in a scene
- Lua calls to `os.execute`, `io.open`, `dofile`, and `require`

Acceptance criterion: opening and playing a malicious test scene cannot read or execute a file outside the project.

## Phase 2: prevent scene data loss

### 4. Make scene saving atomic

Replace direct truncation in `Editor/src/SceneSerializer.cpp` with:

1. Serialize and validate the complete JSON in memory.
2. Write to a uniquely named temporary file in the destination directory.
3. Flush the stream and close it; check both operations.
4. Parse the temporary file back or at minimum verify its expected size and format.
5. Move the existing destination to `.bak` when it exists.
6. Atomically replace the destination with the temporary file using the Windows replacement API, or the best filesystem equivalent with explicit error handling.
7. Restore `.bak` if replacement fails.
8. Remove stale temporary files during a later successful startup/recovery pass.

Keep at least one last-known-good backup. Never delete the backup until the new destination is known to be valid.

### 5. Add dirty-state and close protection

Create a `DocumentState` instead of passing only `currentScenePath`:

```cpp
struct DocumentState {
    std::filesystem::path path;
    bool dirty = false;
    std::uint64_t revision = 0;
    std::uint64_t savedRevision = 0;
};
```

Mark it dirty after every successful mutating command, gizmo edit, terrain edit, inspector change, script attachment, animation edit, undo, and redo. Do not mark it dirty for selection or camera navigation.

Before New, Open, and window close, show Save / Discard / Cancel. Because GLFW close callbacks cannot perform the entire modal workflow cleanly, clear the close flag, open an ImGui modal, and close only after the user's decision.

Change Save to `Ctrl+S`; optionally keep `Ctrl+R` temporarily as a deprecated alias. Display `*` in the title when dirty.

### 6. Add recovery snapshots

When dirty, periodically save a recovery file without changing the main scene path. Store it beneath a project-local recovery directory ignored by Git. On startup, compare timestamps/revisions and offer recovery. Do not autosave directly over the user's scene.

### Phase 2 tests

- Simulate a write failure and verify the original scene remains intact.
- Round-trip every entity field through save/load.
- Verify New/Open/Close offers Save / Discard / Cancel only when dirty.
- Verify successful Save clears dirty state; failed Save does not.
- Verify a recovery file is discoverable after an interrupted session.

## Phase 3: build the actual standalone runtime

### 7. Extract shared game modules from Editor

The runtime cannot use editor-only implementations while core game behavior lives under `Editor`. Split targets along these lines:

```text
GameForgerEngine
  Core engine/version/project configuration
  SceneEntity and scene document model
  Scene serialization and validation
  Animation data/evaluation
  Safe project paths

GameForgerRuntimeCore
  Scene renderer
  Primitive/model/text/terrain asset loading
  Restricted Lua runtime and game API
  Play simulation, camera and input

GameForgerEditor
  ImGui panels, selection, gizmos, command history
  AI generation/planning UI
  Import dialogs and editor-only tools

GameForgerRuntime
  Small application entry point using RuntimeCore
```

Move code incrementally while retaining behavior. First move plain data and serialization, then rendering/assets, then scripting/play simulation. Keep ImGui dependencies out of RuntimeCore: replace `ImGui::IsKeyDown` in scripts with an input interface implemented by GLFW/runtime and editor preview adapters.

### 8. Parse `Game/Project.json`

Add a `ProjectConfig` model containing:

- format and version
- project name
- startup scene
- provider file
- skeleton profile
- configured asset/script directories

At application startup:

1. Determine project root from an explicit `--project <path>` argument; use current directory only as a compatibility fallback.
2. Load and validate `Game/Project.json`.
3. Resolve all configured paths with the safe-path utility.
4. Report a clear error and exit nonzero when required data is invalid.

Stop hard-coding `Game/Scenes/Startup.scene.json` and asset folder names throughout the application after configuration is available.

### 9. Implement the runtime loop

`Runtime/src/main.cpp` should:

1. Parse command-line options and load `ProjectConfig`.
2. Initialize GLFW/OpenGL and report requested/available versions.
3. Create the shared renderer and asset caches.
4. Load and validate the configured startup scene.
5. Initialize restricted Lua and start attached scripts.
6. Each frame: poll input, clamp delta time, update scripts, evaluate animation, resolve supported physics, update the active camera, render, and swap.
7. Shut down scripts, GPU resources, window, and GLFW in deterministic order.

Use the same scene/play behavior in the editor Game view and standalone runtime. Avoid two implementations that will drift.

### 10. Implement Build Game

Connect `File > Build Game...` to an explicit packaging workflow. A first Windows package can contain:

```text
Build/MyGame/
  MyGame.exe
  Game/Project.json
  Game/Scenes/...
  Game/Scripts/...
  Game/Models/...
  Game/Fonts/...
  Game/Textures/...
  required runtime DLLs/licenses
```

The builder should:

- Save or refuse to build a dirty scene.
- Run project/scene validation first.
- Build or locate a matching Release runtime.
- Copy only referenced/declared assets where possible.
- exclude provider secret files, editor layouts, source-only files, `.vs`, and `out`.
- produce a manifest with app version and file hashes.
- launch an optional packaged-game smoke test.

Do not ship AI API keys in a game build.

### Phase 3 tests

- Runtime loads the configured startup scene rather than a hard-coded path.
- A fixture scene renders at least one primitive in an offscreen/smoke run.
- A fixture Lua script receives start/update and moves an entity.
- Editor Play and Runtime produce equivalent transforms after a deterministic sequence.
- A packaged build starts from outside the source tree with no editor files present.

## Phase 4: replace fragile configuration and JSON handling

### 11. Use one JSON implementation

Preferred fix: adopt a maintained, pinned JSON library through CMake. If retaining `Json.cpp`, correct it before it is used for security-sensitive configuration:

- Require end-of-input after the root value.
- Implement the JSON number grammar exactly and reject non-finite results.
- Reject unescaped control characters.
- Validate four hexadecimal digits in `\uXXXX`.
- Combine surrogate pairs and reject invalid lone surrogates.
- Reject invalid escapes.
- Define duplicate-key behavior, preferably rejection for configuration files.
- Report line, column, and a concise reason.
- Add maximum input size and nesting depth.

Delete the string-search JSON functions in `AIProviderClient.cpp` and `AIChatResponse.cpp` after structured parsing is available.

### 12. Define and validate schemas in code

For `Project.json`, provider config, and scene files:

- Require the expected `format`.
- Support an explicit version range.
- Reject unsupported future versions with a useful message.
- Run documented migrations for older supported versions.
- Validate required types and constraints instead of silently defaulting corrupt fields.

Scene validation should enforce reasonable limits, for example configurable caps on:

- file bytes and nesting depth
- entity count
- unique, non-empty entity names
- finite transform/color/camera values
- scale and camera parameter ranges
- tags/scripts/keyframes per entity
- terrain resolution and exact `resolution * resolution` height count
- animation time ordering and non-negative duration
- asset path location and extension

Collect multiple validation errors where possible so users do not fix them one at a time.

### 13. Make JSON serialization valid and deterministic

Use the chosen JSON library's writer. If keeping a custom writer:

- Escape every U+0000–U+001F control byte.
- Validate/replace invalid UTF-8.
- Use locale-independent finite number formatting.
- Reject NaN and infinity before writing.
- Keep stable field ordering for readable diffs.
- Add a save/load/save golden test.

### Phase 4 tests

Create test corpora for valid, malformed, deeply nested, oversized, Unicode, future-version, old-version, and traversal-containing documents. Fuzz the parser and scene loader once deterministic unit tests pass.

## Phase 5: make AI configuration real and cancellable

### 14. Create a single provider configuration model

Replace the hard-coded `providers` array and transient disconnected settings with:

```cpp
struct ProviderConfig {
    std::string id;
    std::string displayName;
    bool enabled;
    int priority;
    std::string protocol;
    ParsedHttpsEndpoint endpoint;
    std::string model;
    std::string apiKeyEnvironmentVariable;
    std::chrono::seconds timeout;
    std::vector<std::string> capabilities;
};

struct AIConfig {
    std::string activeProvider;
    std::vector<ProviderConfig> providers;
};
```

Load it once, validate unique IDs and supported protocols, and bind Settings to this model. Decide whether Settings edits project configuration or user overrides, show that distinction, and persist via atomic save.

Honor `activeProvider`, `enabled`, `priority`, and timeout. Never silently fall through from a selected provider into fields belonging to another provider.

### 15. Parse endpoints correctly

Use a real URL parser or a small strictly validated HTTPS endpoint type containing host, port, and path/query. Require HTTPS unless a deliberate development option enables localhost HTTP. Do not ignore configured ports or pretend an HTTP URL is HTTPS.

### 16. Make “Test connection” truthful

Rename Calibration to Test Connection. It should:

- validate configuration locally first
- resolve a credential without displaying it
- send a small provider-supported request
- show provider, model, status code, elapsed time, and a redacted error
- avoid mutating project content

### 17. Add request cancellation and safe shutdown

Give each AI operation an owned request object with cancellation state. On cancel/exit:

- signal the stop token
- close/cancel the WinHTTP request handle
- bound the final join time
- discard late results safely

Use the configured timeout with a sensible maximum. Keep network ownership and response state out of raw UI structs where possible. Add Retry only for safe transient failures and use bounded backoff.

### 18. Parse provider responses structurally

For the current OpenAI-compatible protocol, require and parse `choices[0].message.content`; structurally parse documented errors. Add provider adapters if formats diverge. Cap response sizes before accumulating bytes and show an explicit error for truncated/invalid responses.

### Phase 5 tests

- Reformat/minify provider JSON and verify identical behavior.
- Verify missing fields never leak into the next provider object.
- Verify active/disabled/priority/timeout settings are honored.
- Verify escaped and Unicode response content.
- Verify cancellation closes promptly.
- Verify secrets never appear in captured logs/errors.

## Phase 6: finish or disable misleading UI

### 19. Connect existing commands

In `drawMainMenu`:

- Call `performUndo` and `performRedo` from the clickable menu items.
- Disable Undo/Redo when their stacks are empty.
- Connect Build Game only after Phase 3, otherwise render it disabled with a tooltip.
- Keep skeleton mapping and animation library disabled until implemented.

Ensure menu commands and keyboard shortcuts call the same command functions rather than duplicating behavior.

### 20. Make editing uniformly undoable

Audit every mutation path. Route changes through commands or a transaction system, especially direct mutable-entity edits in inspector, terrain, animation, storyboard, imported assets, and script workflows. A drag should produce one history transaction, not one per frame.

History snapshots currently copy the full scene. This is acceptable temporarily with the existing depth cap, but terrain data can make it expensive. Later replace it with command deltas or copy-on-write documents and measure memory.

### 21. Improve unfinished-feature communication

- Remove selectable French/Mandarin choices until translations exist, or label them unavailable.
- Show standalone Runtime readiness separately from Editor Play readiness.
- Put alpha limitations in an About/Project Health panel, not only README.
- Never display a success state for a no-op setting.

## Phase 7: versioning, build hygiene, tests, and CI

### 22. Generate one application version

Use `configure_file` in CMake to generate a version header from `PROJECT_VERSION`. Replace:

- `Engine::version()` hard-coded `0.1.0`
- WinHTTP user agent `0.1`
- any separately maintained visible version strings

Keep scene/provider schema versions separate from application versions.

### 23. Make clean builds reproducible

- Document `Build-Project.cmd` as the Windows entry point because it initializes `vcvars64`.
- Update it to build editor and runtime, with an argument for Debug/Release.
- Add a non-pausing CI variant or PowerShell build script.
- Pin all FetchContent revisions to immutable commit hashes, not only mutable-looking tags.
- Review dependency licenses and copy required notices into packages.
- Resolve the Assimp/CMake CMP0175 warning by selecting compatible CMake/dependency versions or setting the appropriate policy at a controlled boundary.
- Keep `out`, `.vs`, secret files, recovery data, and packages out of source control.

### 24. Add test targets

Enable CTest and create focused executables:

```cmake
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
```

Suggested suites:

- `GameForgerJsonTests`
- `GameForgerSceneTests`
- `GameForgerProjectPathTests`
- `GameForgerCommandTests`
- `GameForgerAnimationTests`
- `GameForgerScriptTests`
- `GameForgerProviderTests`
- `GameForgerRuntimeSmokeTests`

Keep most tests graphics-free. Introduce interfaces/fakes for filesystem-sensitive, input, clock, and HTTP behavior.

### 25. Add Windows CI

Add `.github/workflows/build.yml` that:

1. Checks out with a clean tree.
2. Installs/uses the supported CMake, Ninja, and MSVC toolchain.
3. Configures from scratch.
4. Builds editor and runtime in Debug and Release.
5. Runs CTest with failure output.
6. Runs a runtime smoke test.
7. Uploads logs only on failure and Release packages only from approved release jobs.

Add secret scanning and a check that forbidden generated/secret files are not tracked.

### 26. Clarify platform support

State supported Windows versions, architecture, Visual Studio workload, CMake range, and minimum graphics capability in README. At startup, print the detected OpenGL version/renderer and a clear failure message. If broad hardware support matters, consider an OpenGL 4.1/4.5 fallback or another backend as a separate project decision.

## Phase 8: complete multi-format 3D import

The app is already partway there. `CMakeLists.txt` currently enables Assimp's GLTF, FBX, 3DS, OBJ, and BLEND importers, and the Windows file dialog already lists `.glb`, `.gltf`, `.fbx`, `.3ds`, `.obj`, and `.blend`. Therefore basic static geometry from all six formats should reach `loadModelMesh`. The remaining work is validation, dependency copying, format normalization, materials/textures, hierarchy, useful diagnostics, and runtime packaging.

### 27. Define what “supported” means

Use two explicit support levels:

- **Geometry preview:** triangles, normals, node transforms, bounds, selection, scene save/load.
- **Production import:** geometry plus UVs, materials, textures, hierarchy, animation/skin data where the source format provides them, persistent imported assets, and runtime packaging.

Initially advertise `.3ds`, `.obj`, and `.blend` as geometry-preview formats until their material/dependency tests pass. Do not imply that enabling an Assimp CMake flag makes the entire format production-ready.

Recommended format priority:

1. `.glb` — preferred interchange format because geometry, materials, textures, rig, and animation can live in one file.
2. `.gltf` — supported, but must import its `.bin` buffers and external images.
3. `.fbx` — common interchange format with exporter/version differences; import with warnings.
4. `.obj` — reliable static meshes but depends on `.mtl` and image sidecars; no rig/animation.
5. `.3ds` — legacy static format with naming/material/coordinate limitations.
6. `.blend` — convenience input only; direct compatibility varies by Blender file version and external dependencies. Recommend exporting `.glb` from Blender when direct import fails.

### 28. Centralize format registration and validation

Create `ModelFormat.hpp/.cpp` with a table containing extension, display name, capabilities, and enabled state. Generate or build the file-dialog filter from this table instead of repeating extensions in CMake comments, UI strings, path validation, and documentation.

Before import:

- lowercase and validate the extension
- require a regular file with a reasonable size cap
- verify the matching Assimp importer exists using `Assimp::Importer::IsExtensionSupported`
- reject unsupported files before copying them
- inspect with `ReadFile` and return Assimp's diagnostic without creating a scene entity on failure

After CMake configure, add a small test executable that asserts all six extensions are reported as supported. This detects cached CMake options or dependency changes that silently omit an importer.

### 29. Fix source-asset copying and name collisions

`importModelIntoProject` currently copies only the selected file and silently reuses a same-named destination. That works best for self-contained `.glb`, but breaks sidecar-based formats and can associate a newly selected model with an unrelated old file.

Import each model into its own directory:

```text
Game/Models/<sanitized-name>-<short-hash>/
  source/model.ext
  dependencies/...
  import.json
  generated/model.gfa-model
```

The hash should come from canonical source identity/content, not only the filename. Never silently reuse a destination solely because its filename matches.

Handle dependencies per format:

- `.glb`: copy the single file; inspect embedded images/buffers.
- `.gltf`: parse buffer/image URIs and copy only safe relative local files. Reject traversal, absolute paths, network URLs, and unsupported data URIs unless deliberately implemented.
- `.obj`: copy the OBJ, referenced `.mtl` files, and texture maps referenced by each MTL. Preserve relative relationships or rewrite them in generated metadata.
- `.fbx`: extract embedded textures when present; resolve external texture paths relative to the FBX first, then ask the user to locate missing files. Copy resolved dependencies into the asset directory.
- `.3ds`: resolve and copy material texture filenames, accounting for legacy short/truncated names and case differences.
- `.blend`: collect external images/libraries reported by the importer where possible. If dependencies cannot be enumerated safely, show warnings and recommend Blender's “pack resources” followed by GLB export.

Record original source path only as informational metadata. Scene/runtime references must point to project-local generated/source assets.

### 30. Replace the flat float buffer with an imported model asset

`ModelImportResult` currently contains only interleaved position and normal floats, and `appendMeshNode` flattens every mesh. Replace it with structures such as:

```cpp
struct ImportedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec2 uv0;
    glm::vec4 color;
};

struct ImportedSubmesh {
    std::string name;
    std::vector<ImportedVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t materialIndex;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
};

struct ImportedNode {
    std::string name;
    glm::mat4 localTransform;
    std::vector<std::uint32_t> submeshes;
    std::vector<ImportedNode> children;
};

struct ImportedModel {
    std::vector<ImportedSubmesh> meshes;
    std::vector<ImportedMaterial> materials;
    ImportedNode root;
    ImportReport report;
};
```

Use indexed rendering rather than expanding every triangle. Preserve local node transforms instead of baking everything into one buffer. This is necessary for separate materials, hierarchy, animation, and efficient GPU memory.

Add Assimp post-process behavior deliberately:

- `aiProcess_Triangulate`
- `aiProcess_JoinIdenticalVertices`
- `aiProcess_GenSmoothNormals` only when normals are absent
- `aiProcess_CalcTangentSpace` when normal mapping will be supported
- `aiProcess_ImproveCacheLocality`
- `aiProcess_ValidateDataStructure`
- consider `aiProcess_SortByPType` and reject/non-render line/point-only meshes

Do not blindly use `aiProcess_PreTransformVertices`; it destroys hierarchy needed later. Normalize coordinates and units through explicit import metadata rather than unexplained format-specific transforms.

### 31. Import materials and textures

Add a shared material model with at least:

- base color factor and base-color texture
- metallic and roughness factors/textures
- normal texture
- emissive factor/texture
- alpha mode/cutoff and double-sided flag

Map glTF PBR fields directly. Convert legacy OBJ/MTL, 3DS, FBX, and BLEND/Assimp material properties to the closest supported PBR values and list any lossy conversion in `ImportReport`.

Upgrade the shader and GPU cache from `baseColor` only to material-aware indexed drawing. Add an image loader/cache, sRGB handling for color textures, linear handling for data maps, sampler settings, a missing-texture checkerboard, and deterministic resource cleanup.

Copy or extract every texture into the model asset directory. Store project-relative normalized paths in generated metadata. Packaging must follow these references.

### 32. Add a persistent generated asset format

Do not make the editor/runtime call Assimp every time the mesh is rendered or picked. Import once and write a versioned `model.gfa-model` plus copied/generated textures. It should contain normalized vertices, indices, submeshes, nodes, materials, bounds, skeleton/animation references, source hash, importer version, and import settings.

Then:

- `SceneEntity::ImportedMeshData` references the generated asset, not the raw source.
- Viewport picking reads cached bounds instead of re-importing the source.
- Renderer uploads the generated data once per asset, shared by all entities using it.
- Runtime consumes the generated format and does not need Assimp unless runtime source import is an explicit feature.
- Reimport compares the source/dependency hashes and retains entity/material overrides where possible.

### 33. Add import options and a report

Show an import preview/modal with:

- detected format/importer
- mesh, vertex, triangle, material, texture, bone, and animation counts
- source units and up/forward axes when known
- scale and axis conversion controls
- generate missing normals/tangents options
- hierarchy-preservation option
- missing dependencies and unsupported-feature warnings
- estimated generated asset size

On success, display warnings rather than only “Model loaded.” On failure, do not create an entity or leave partial copied assets; stage the import in a temporary directory and commit it only after generation succeeds.

### 34. Format-specific behavior users need to know

#### GLB / glTF

- Prefer glTF 2.0 and `.glb` for Blender exports.
- Support external `.bin` and image files for `.gltf`.
- Preserve multiple primitives/materials, node hierarchy, skins, and animation clips.
- Handle embedded images and data buffers with size limits.
- Respect glTF's coordinate conventions and convert once into the engine convention.

#### FBX

- Test ASCII and binary files from the exporter versions you intend to support.
- Normalize FBX units and axes and report conversions.
- Preserve submeshes/material slots, bones, skin weights, and clips rather than flattening.
- Warn for unsupported constraints, modifiers, custom properties, cameras, or lights.
- Treat embedded and external textures separately.

#### OBJ

- Import `.mtl`, `usemtl` groups, smoothing groups, normals, and UVs.
- Generate normals when absent and split vertices where UV/normal indices differ.
- Resolve texture paths relative to the MTL file, not just the OBJ.
- State clearly that OBJ does not carry bones or animation.

#### 3DS

- Expect legacy limits such as short names, limited material semantics, and format-specific coordinate/unit behavior.
- Preserve object/material grouping where Assimp provides it.
- Warn when names collide after legacy truncation or when unsupported data is discarded.
- Recommend GLB for re-export when fidelity is poor.

#### BLEND

- Treat direct `.blend` import as best effort, not the canonical pipeline.
- Detect the importer failure cleanly and show: “Open this file in Blender and export glTF 2.0 (.glb).”
- Document the tested Blender file-version range for the pinned Assimp release.
- Do not attempt to run Blender automatically without an explicit, separately configured Blender conversion tool and user approval.
- If you later add Blender-assisted conversion, call a configured Blender executable in background/headless mode, export to a temporary GLB, capture logs, and then use the normal GLB importer. Keep this optional because it adds a large external dependency and executes Blender/Python content.

### 35. Multi-format import test matrix

Store small licensed fixtures under `Tests/Assets/Models`:

| Fixture | GLB | glTF | FBX | OBJ | 3DS | BLEND |
|---|---:|---:|---:|---:|---:|---:|
| triangle/mesh geometry | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| node transforms | ✓ | ✓ | ✓ | n/a | ✓ | ✓ |
| UV + base texture | ✓ | ✓ | ✓ | ✓ | ✓ | ✓/best effort |
| multiple materials | ✓ | ✓ | ✓ | ✓ | ✓ | ✓/best effort |
| missing sidecar error | n/a | ✓ | ✓ | ✓ | ✓ | ✓ |
| skeleton + animation | ✓ | ✓ | ✓ | n/a | n/a | best effort |
| malformed/oversized input | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

For every fixture, assert import success/failure, counts, finite bounds, normalized safe dependency paths, stable generated output, and no crash. Add a visual golden/screenshot test for textured models after the material renderer exists.

### 36. Delivery plan for these extensions

Use four reviewable increments:

1. **Static geometry support:** validate all six extensions, stage/copy safely, fix same-name collisions, create fixtures, and confirm geometry/normals/bounds.
2. **Dependency-aware assets:** import glTF buffers/images, OBJ/MTL, FBX/3DS textures, and best-effort BLEND dependencies into per-model directories.
3. **Materials and hierarchy:** add indexed submeshes, nodes, UVs, materials, texture rendering, generated model assets, caching, and runtime packaging.
4. **Rigging and animation:** add bones, weights, clips, retargeting, and GPU skinning for GLB/glTF and FBX first; treat BLEND as best effort and OBJ/3DS as static-only.

Acceptance criterion: each enabled extension can import a documented fixture without crashing, missing dependencies are actionable, the scene stores only safe project-local references, and a packaged runtime renders the same result as the editor.

## Phase 9: declared alpha feature gaps

These are larger product tracks and should not block the security/data-safety work above.

### 37. Model and material pipeline

Stop flattening imports into one mesh. Preserve node hierarchy, per-mesh transforms, material slots, texture references, normals/tangents, and GPU resources. Define asset import metadata so original source and generated runtime asset are separate. Add missing-texture and unsupported-material fallbacks.

### 38. Skeletons and animation library

Define bone hierarchy, inverse bind matrices, vertex weights, animation clips, retarget mapping, and GPU skinning. Then connect the two currently inert Character menu items. Validate imported rigs and provide a preview before applying mappings.

### 39. Physics

Treat the current AABB resolver as prototype character collision. Add fixed-timestep simulation, swept tests, layers/masks, stable grounding, rotated/terrain colliders, and deterministic editor/runtime parity—or integrate a maintained physics library. Do not market it as a general physics engine until those needs are met.

### 40. Localization

Extract user-facing strings into resource tables, select locale at startup, support font glyph coverage, and test layouts with longer translations. Enable a language only when its resource coverage meets an explicit threshold.

### 41. Production rendering and content

Plan material/shader management, lighting, shadows, texture color spaces, audio playback/mixing, resource lifetime, scene hierarchy, prefab/reuse workflows, and asset dependency tracking. Add each feature to RuntimeCore first or simultaneously so Editor preview cannot outrun packaged games again.

## Suggested milestone plan

### Milestone A: safe project editing

Complete Phases 1 and 2. Release criterion: no path escape, restricted Lua, atomic saves, dirty prompts, recovery, and passing security/data-loss tests.

### Milestone B: first real playable build

Complete Phase 3. Release criterion: a packaged fixture game starts outside the source tree, loads its configured scene, renders, accepts input, and runs Lua.

### Milestone C: reliable AI and file formats

Complete Phases 4 and 5. Release criterion: schema-validated files, one truthful provider configuration, structured responses, cancellation, and no secret leakage.

### Milestone D: trustworthy alpha

Complete Phases 6 and 7. Release criterion: no enabled no-op UI, consistent undo, one version, clean Debug/Release CI, tests, packaging, and documented requirements.

### Milestone E: production feature tracks

Work through Phase 9 based on the first intended game. Set acceptance tests per track and preserve editor/runtime parity.

## Practical first ten changes

If starting immediately, use this exact order:

1. Add `ProjectPaths` and path traversal tests.
2. Apply it to script attachment, scene loading, and `startScript`.
3. Restrict Lua libraries and add an execution budget.
4. Implement atomic scene save plus backup tests.
5. Add document dirty state and Save / Discard / Cancel.
6. Enable CTest and land JSON/scene/path unit suites.
7. Correct or replace the JSON parser.
8. Parse `Project.json` into a validated `ProjectConfig`.
9. Extract shared scene/serialization/runtime code from Editor.
10. Make Runtime load and execute the startup scene, then connect Build Game.

Avoid implementing all fixes in one giant change. Each numbered item should be a reviewable commit with tests and a working editor at the end.
