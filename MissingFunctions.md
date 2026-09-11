# MissingFunctions.md — GameForgerAI Editor audit

**Audited:** 2026-09-09, extended 2026-09-10 · **Version:** Alpha 0.79.0 (`CMakeLists.txt:7`) ·
**Branch:** `unity-parity-upgrade`

What this is: every gap between this editor and a complete, Unity-class 3D editor, with the
file/line evidence behind each claim. Written as the checklist for the Unity-Parity Upgrade
plan — each row names the phase that closes it.

**Method — what was and was not checked.** Sections 1–8 are a *feature-gap* audit derived
from reading the headers, data model, renderer, script runtime, AI layer and build config.
Sections **1b–1d** are a separate *defect* pass added afterwards: features that exist and are
wired but do not actually work, half-wired UI, dead content, and a verified build + test run.
Not covered by either pass: a live play-test of every feature, GPU/driver-specific rendering
correctness, and performance profiling under load — those belong to the per-phase gauntlet.

Status key: **MISSING** (does not exist) · **PARTIAL** (exists but short of parity) ·
**BROKEN** (wired, ships, silently does nothing) · **DEBT** (works, but will bite) ·
**FIXED** (was a gap, since resolved — kept for the record)

### Verdict

**Nothing here is rotten. Six things were genuinely broken, they shared one cause, and all
six are now fixed — by removing the cause rather than patching the instances.**

- **Broken — ships wrong, user is not told: 0 remaining.** All six are fixed. Originally 3;
  two more (1b.5, 1b.6) surfaced *while fixing the first three*, which is itself the evidence
  that this was a class and not three incidents. All six had one root cause: each host bound
  the `ScriptRuntime` callbacks by hand and the bodies were only *meant* to match. They now
  bind once in Engine, so a callback is bound in both hosts by construction, guarded by
  `testSharedScriptCallbackParity`. The HUD (1b.4) had a second cause - it lived in one host
  rather than in the renderer both hosts share - and was fixed the same way: by moving it to
  the shared layer, not by writing it twice.
- **Wired but inert — announced as working, does nothing: 1.** `RequestAnimationCommand`
  (1c.4). Validated, described to the user, never executed. Currently unreachable because no
  parser op emits it, so it is a trap set for later rather than a live failure.
- **Visibly unfinished — honest about it: 4.** Two disabled menu items, two empty locales, a
  placeholder provider entry (1c.1–1c.3). These are labelled or disabled; nobody is misled.
- **Not started at all — the bulk of this document.** Lights, shadows, camera objects, empty
  objects, the Information tab, packaging, the AI knowledge base, GFScript. These are absent,
  not broken. Absent is the cheaper problem.
- **Not a feature defect but the biggest structural risk:** `main.cpp`'s hierarchy transform
  math has no test coverage and cannot be given any where it currently lives (§1d).

**The pattern worth naming:** every real defect found is a *seam* defect — Editor vs Runtime,
validator vs executor. Nothing is wrong inside any single subsystem. The code is disciplined;
the joins between the two hosts and between the command layers are where it leaks, because
nothing tested a seam. The durable answer is not more tests at the seam but fewer seams:
where both hosts need the same behaviour, it now lives in Engine and is called twice rather
than written twice.

---

## 0. What already works — read this first

This editor is much further along than a feature list suggests, and several things that look
missing are not. Do not rebuild these:

| Feature | Where |
|---|---|
| True parent/child hierarchy, recursive TRS, any depth | `EditorScene.hpp:351-369`, `applyParentConstraints` in `main.cpp` |
| Drag-and-drop reparenting in the Hierarchy, world transform preserved | `reparentEntityKeepingWorldTransform`, `main.cpp:~4680` |
| Add Child (6 primitives + Upload Model) | Hierarchy context menu, `main.cpp:4910-4960` |
| Multi-select with click order + Shift range-select | `SelectionState`, `main.cpp:266-280` |
| Gizmo with grid snapping | `main.cpp:8049-8161` |
| Play / Pause / Step / Stop | `main.cpp:2475-2640` |
| Undo/redo through a validated command bus | `AICommandBus.cpp`, `AICommand.hpp` |
| Heightmap terrain: sculpt, splat paint, heightmap import, Perlin | `Terrain.cpp`, `TerrainTexture.cpp` |
| Model import GLB/glTF/FBX/3DS/OBJ/Blend + GPU skinning | `ModelImport.cpp`, `ViewportRenderer.cpp` |
| Colliders: Box / Mesh / Convex | `EditorScene.hpp:217-249`, `Collision.cpp` |
| Audio engine with per-source reverb/delay/filter/fades | `AudioEngine.cpp`, `AudioSourceEffects.cpp` |
| Lua scripting: entity, input, physics, audio, camera, world, gameManager | `ScriptRuntime.cpp` |
| Script path confinement + `dofile`/`loadfile` removed | `ProjectPaths.cpp`, `ScriptRuntime::startScript` |
| Timeline, keyframe animation, storyboard/cine shots | `TimelinePanel.cpp`, `Storyboard.cpp` |
| Frame profiler + Performance panel with report export | `FrameProfiler.cpp`, `PerformancePanel.cpp` |
| AI Forge planner + agentic AI Cockpit with approval gates | `AICommandPlanner.cpp`, `AICockpit.cpp` |
| Blender MCP bridge | `BlenderClient.cpp`, `BlenderLauncher.cpp` |
| Standalone `GameForgerRuntime.exe` that loads and plays a scene | `Runtime/src/main.cpp` |

---

## 1. Rendering & lighting — the largest gap

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 1.1 | **No light objects of any kind.** No Directional / Point / Spot entity exists. | MISSING | No `isLight`/`LightData` anywhere in `EditorScene.hpp` | 1 |
| 1.2 | **Lighting is one hardcoded direction, duplicated 5×.** Every shader inlines `normalize(vec3(0.4, 0.85, 0.35))`. Changing the sun means editing five string literals. | MISSING | `ViewportRenderer.cpp:83, 196, 220, 295, 314` | 1 |
| 1.3 | **No shadows at all.** No shadow map, no depth pass, no `castShadows` reaching the renderer. | MISSING | zero `shadow`/`depthMap` hits in `ViewportRenderer.cpp` | 1 |
| 1.4 | **No ambient/IBL control**, no per-scene light settings. | MISSING | — | 1 |
| 1.5 | **No PBR.** `metallic`/`roughness` exist only in a dead header nothing renders from. | MISSING | `Core/Material.hpp:33-35`, referenced only by `Engine/tests/TestMain.cpp` | later |
| 1.6 | **No skybox, no fog.** Background is a flat clear colour. | MISSING | no `skybox`/`fog` symbol in the tree | later |
| 1.7 | **No post-processing** (bloom, tonemap, AO, AA). | MISSING | — | later |
| 1.8 | **No particle system.** `Game/Textures/TEX_PACK_01` ships 8 `FX_*` sprites nothing can use — so no muzzle flash, no impact effect. | MISSING | no `particle` symbol in the tree | 4 (FPS demo needs impacts) |
| 1.9 | **No LOD, no frustum/occlusion culling.** Every entity is submitted every frame. | MISSING | — | later |
| 1.10 | ~~Renderer is duplicated between Editor and Runtime.~~ **Resolved.** `Engine/src/Rendering/SceneRenderer.cpp` no longer exists; `Runtime/src/main.cpp:34,412` includes and drives `ViewportRenderer` directly, so there is exactly one renderer and Phase 1's shader work lands once. | FIXED | `ls Engine/src/Rendering` → no such directory | — |

## 1b. BROKEN — works in the Editor, silently does nothing in a shipped game

This is the most dangerous category in the codebase, because nothing reports it. The Editor's
Play mode and `GameForgerRuntime.exe` configure the **same** `ScriptRuntime` callback set —
but the Runtime used to stub several of them out. A script calling those behaved correctly
when you pressed Play and was silently inert in the built game — no warning, no log, no
failed build. All six below are now fixed; the table is kept as the record of what the class looked
like, because the lesson outlives the bugs.

| # | Defect | Editor | Runtime | Evidence |
|---|---|---|---|---|
| 1b.1 | ~~Catapult firing is dead in a shipped game.~~ **FIXED.** | — | — | now via `bindSharedScriptCallbacks` |
| 1b.2 | ~~Catapult aiming state is dead.~~ **FIXED.** | — | — | same |
| 1b.3 | ~~Held-item state is dead.~~ **FIXED.** | — | — | same |
| 1b.4 | ~~Authored HUD does not draw in a shipped game.~~ **FIXED.** The HUD now draws in `ViewportRenderer` - which both hosts already share - instead of in the Editor with ImGui. Crosshair, text, image and panel all render in `GameForgerRuntime`. Two real bugs were found doing it: the UI host was `followedEntity`, which is the **player**, not a camera, so a crosshair parented to the Main Camera failed the parent-chain test the moment a controller script was attached; and text was always centred on its anchor, so a top-left label spilled half its width off-screen. Alignment now follows the anchor. | — | — | — |
| 1b.5 | ~~`projectilesFiredThisTick` never reset in the Runtime~~ — **FIXED.** Found while fixing 1b.1-3. The Editor reset it in its own tick; the Runtime never did, so it accumulated forever. Now `beginGameplayFrame()` in Engine, called by both. | — | — | — |
| 1b.6 | ~~Runtime fired only the `OnPlayStart` audio hook~~ — **FIXED.** Found the same way. `OnProjectileFire` and `OnProjectileHit` were Editor-only, so a project's authored fire and impact sounds were silent in a shipped game. | — | — | — |

**THE STRUCTURAL FIX.** 1b.1-1b.3 were not fixed one at a time. All eight
GameplayState- and AudioEngine-backed callbacks now bind in ONE place -
`bindSharedScriptCallbacks` (`GameplayLoop.hpp`), called by both hosts - so a
callback bound there is bound in both **by construction**. Only `logCallback`
stays per-host, because it genuinely differs (Editor Console vs stderr). This
deleted ~6.8 KB of duplicated binding code across the two `main.cpp` files.

`testSharedScriptCallbackParity` guards it, and was itself verified by
reintroducing the real 1b.3 defect and confirming the test fails on it:
`FAIL: heldItemQueryCallback must read GameplayState, not return a constant`.
It asserts not just that each callback is bound, but that each actually reads
and writes the shared state - a stub returning `false` is also "bound", and
that is precisely what shipped before.

**Why this exists:** the Runtime has no inventory UI and no catapult UI, so the callbacks were
stubbed rather than backed by Runtime-side state. The fix is not UI — it is moving the
gameplay state these read (`heldItemEntityName`, `playerOperatingCatapult`, the projectile
list) into `GameplayState`, which the Runtime already owns and already ticks.

**Two more of this class were caught during the lights/camera work and fixed on the spot,
which is the pattern to keep:** the Runtime had no Main Camera fallback (a scene with a
camera and no player script was unviewable standalone), and it rendered the Editor's ground
grid and light/camera wireframe gizmos into the shipped game. Both were found by actually
running `GameForgerRuntime` against a new scene rather than by reading the Editor's code —
which is the only reliable way to catch a seam defect.

**The structural risk is worse than the three defects.** Nothing prevents a fourth. Any
future `scriptConfig.*` callback can be implemented in `main.cpp` and stubbed in
`Runtime/src/main.cpp`, and the build stays green, the tests stay green, and the failure
only appears after someone ships a game. **Phase 6 must add a parity test** that asserts
every callback in `ScriptRuntime::Config` is non-trivially bound in both hosts.

## 1c. Half-wired UI and dead content

| # | Item | Status | Evidence |
|---|---|---|---|
| 1c.1 | **`Map Humanoid Skeleton...` and `Animation Library...` do nothing.** Bare `ImGui::MenuItem` statements with no `if` — clicking them is a no-op. Documented as deliberate and rendered disabled, so not a trap, but they are two of only four items in the `Character` menu. | PARTIAL | `main.cpp:2432-2433`, comment at `2427` |
| 1c.2 | **French / Mandarin locale options are empty.** Selectable in Settings; no translated strings exist. | PARTIAL | `main.cpp:3028` |
| 1c.3 | **A provider entry in the default list is a placeholder** using an OpenAI-compatible shape rather than its real API. | PARTIAL | `main.cpp:753` |
| 1c.4 | **`RequestAnimationCommand` is validated and described but never executed.** It is a full member of the `AIEditorCommand` variant, the bus validator returns `"Animation command is valid."` (`AICommandBus.cpp:66`), and the planner renders it to the user as `"Play animation 'X' on 'Y'"` (`AICommandPlanner.cpp:335`) — but `EditorScene::execute` has no `is_same_v<Command, RequestAnimationCommand>` branch, so it falls through to `"Animation execution is not connected yet."` A command that passes validation and is announced as an action, then does nothing. Currently unreachable in practice: the planner's op parser has no animation op, so nothing can emit one. **Dead-but-wired, not live-broken** — but it is one parser line away from becoming live-broken. | PARTIAL | `EditorScene.cpp:871-873` vs the 16-member variant in `AICommand.hpp` |
| 1c.6 | **`"This scene property is not implemented yet."`** is the catch-all at the end of `SetPropertyCommand`'s component chain (`EditorScene.cpp:869`) — a correct rejection of an unknown component/property, wearing a misleading message. Not a defect; reword to "Unknown property" so it stops reading like an unfinished feature. | DEBT | `EditorScene.cpp:869` |
| 1c.5 | **14 of 19 shipped Lua scripts are orphans.** Only `fps_controller`, `rigidbody`, `ranged_attacker`, `inventory_system` and `enemy_ai` are referenced by any scene. The rest — including the `FPSController` / `PlayerFPS` / `player_fps` / `controller` / `player_controller` near-duplicates and `Script.lua` / `Script_2.lua` / `test.lua` — are dead weight a user browsing the Scripts folder cannot tell apart from the real ones. | DEBT | scene grep vs `ls Game/Scripts/` |

## 1d. Verified build & test status (2026-09-10)

Checked directly, not assumed:

| Check | Result |
|---|---|
| `editor-debug` | builds clean (`ninja: no work to do` — up to date) |
| `runtime-debug` | builds clean |
| `all-release` | builds clean — the historical ImGuizmo `all-release` breakage is **not** currently present |
| CTest | **1/1 passing**, 0.70s — `EngineRegressionTests`, 29 test functions |

**Test-coverage gaps found while verifying:**

- **`Editor/src/main.cpp` has zero test coverage and cannot be given any.** It is 8,765 lines
  compiled straight into the executable target, not a library. `applyParentConstraints`,
  `reparentEntityKeepingWorldTransform`, `worldToScreen`, `findPickupCandidate`,
  `sculptTerrain` and every panel live there — so the hierarchy TRS math, the single most
  load-bearing piece of the Unity-parity story, is untested and untestable as structured.
  Phase 1 should extract the pure-math helpers into `Engine/` as they are touched.
- **3 of 29 tests target dead code.** `testGameObjectComponentModel`, `testAssetDatabase` and
  `testMaterialSerialization` exercise `Core/GameObject.hpp`, `Core/AssetDatabase.hpp` and
  `Core/Material.hpp` — headers nothing in the shipping app includes (see §8.2). Real
  coverage of live code is 26 tests, not 29.
- **No rendering tests, no hierarchy tests, no Editor↔Runtime parity test** (see §1b).
- `testAllShippedScriptsLoad` is the strongest test present — it would catch a broken `.lua`
  and must be extended to `.gfs` in Phase 4.

## 2. Scene objects & hierarchy

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 2.1 | **No Empty object.** `PrimitiveType` is exactly six solid meshes, so there is no transform-only grouping node — the thing every Unity prefab starts with. | MISSING | `AICommand.hpp:13-21` | 2 |
| 2.2 | **No real Camera object.** `isCineCamera` is a cutscene path-follower; there is no camera with FOV, near/far clip, clear colour, or a "main camera" concept. A scene with no controller script has no camera. | PARTIAL | `EditorScene.hpp:320`, `EntityCameraRig:20-33` | 2 |
| 2.3 | **Nothing can be parented to a camera.** No crosshair, no 2D plane, no HUD text riding the camera. | MISSING | UI overlays are hardcoded in `drawGameViewPanel` | 2 |
| 2.4 | **No UI/canvas system.** Crosshair, pickup hints and detection icons are hardcoded ImGui draws, not authorable objects. | MISSING | `main.cpp` `worldToScreen` overlay blocks | 2 |
| 2.5 | **No prefabs.** No way to save an assembled object and re-instance it. Duplicate is a one-shot copy with no link. | MISSING | no `prefab` symbol in the tree | later |
| 2.6 | **No drag-and-drop from the Project panel into the scene or Hierarchy.** Import is menu-only. | MISSING | `Character > Import Model...`, `main.cpp:2362` | 2 |
| 2.7 | **Deleting a parent does not cascade to children.** Deliberate (orphans are promoted, nothing is lost) but not what Unity does and not offered as a choice. | PARTIAL | documented behaviour of `DeleteEntityCommand` | 2 |
| 2.8 | **No layers, no culling masks, no per-object visibility toggle** beyond `active`. | PARTIAL | `SceneEntity::active`, `EditorScene.hpp:257` | later |
| 2.9 | **Object creation is scattered across three menus.** `GameObject` holds six primitives; Terrain and Cine Camera hide in the Toolbox; models hide under `Character`. Nothing tells a new user where anything is. | DEBT | `main.cpp:2331-2443`, `4556-4600` | 7 |

## 3. Asset pipeline & information

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 3.1 | **No Information tab.** Nothing anywhere surfaces what an asset actually contains. | MISSING | panel list in `main.cpp` — no such `ImGui::Begin` | 3 |
| 3.2 | **Import reads no metadata.** `loadModelMesh` returns vertices and bones only; materials and textures are explicitly discarded, and no counts, bounds, UV-channel info or format version are reported. | MISSING | `ModelImport.hpp:61-97` | 3 |
| 3.3 | **Imported models lose their materials and textures.** They render with the entity's flat `color` until a texture is assigned by hand. | MISSING | `ModelImport.hpp:92` "No materials/textures are read" | 3 |
| 3.4 | **Only the first animation clip is imported.** A file with a walk/run/idle set loses all but one, with no clip picker. | PARTIAL | `ModelImportResult::animations`, `ModelImport.hpp:78-81` | later |
| 3.5 | **Sub-object hierarchy is flattened.** Every mesh in a file is merged into one buffer, so a multi-part model cannot be taken apart. | PARTIAL | `ModelImport.hpp:84-90` | later |
| 3.6 | **No scene/asset history journal.** Nothing records how the scene reached its current state, so the AI has no memory across sessions. (`backintime<gamename>.md`) | MISSING | — | 3 |
| 3.7 | **No asset thumbnails or search in the Project panel.** | PARTIAL | `main.cpp:3144` | 7 |
| 3.8 | **Materials are per-entity only.** No shared material asset — retexturing 50 objects means editing 50 objects. | PARTIAL | `SceneEntity::materialLayers`, `EditorScene.hpp:343` | later |

## 4. Scripting

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 4.1 | **No typed, Inspector-exposed script fields.** Tunables are plain `self.*` assignments the user must find and edit inside the script text. | MISSING | `inventory_system.lua` pattern; `getScriptNumberField` in `ScriptRuntime` | 4 |
| 4.2 | **No event model.** Only `on_start`/`on_update`. No `on_hit`, `on_trigger`, `on_destroy`. | MISSING | script contract in `ScriptRuntime::startScript` | 4 |
| 4.3 | **No coroutines / `wait` / sequences,** which is what scripted sequences and cine shots actually need. | MISSING | — | 4 |
| 4.4 | **No script API for lights, cameras, UI, animation or cine shots** — because none of those objects exist yet. | MISSING | binding list, `ScriptRuntime.cpp:253-259, 506-630, 761-770` | 4 |
| 4.5 | **Scripts cannot spawn or configure other entities.** Projectiles had to be built as an engine-side special case because of this. | PARTIAL | `PlayModeState::Projectile`, `fireProjectile` binding | 4 |
| 4.6 | **AI-generated scripts get user-typed names, with no convention.** `Game/Scripts/` already holds `FPSController.lua`, `PlayerFPS.lua`, `fps_controller.lua`, `player_fps.lua`, `controller.lua`, `player_controller.lua`, `Script.lua`, `Script_2.lua`, `test.lua` — nine files, several of them the same thing. | MISSING | `ls Game/Scripts/`; `scriptCreator.scriptName`, `main.cpp:6527` | 4 |
| 4.6b | **`UIElementData::fontPath` now works.** It used to round-trip through the scene file and change nothing on screen. HUD text bakes one atlas per authored font, cached, falling back to the first usable font in `Game/Fonts` (preferring `.ttf`, since stb_truetype cannot rasterise CFF/PostScript OTF). | FIXED | `ensureOverlayFont` |
| 4.7 | **The AI prompt is not in the script window.** It lives in the Inspector's Add-Script flow, can only create, and cannot modify an existing script. | PARTIAL | `main.cpp:6525-6560`, `ScriptGenerator.hpp` | 4 |
| 4.8 | **No script error line reporting in the editor**, no breakpoints, no watch. | MISSING | — | later |
| 4.9 | **No instruction-count budget on a running script.** An infinite loop in Lua hangs the editor; the sandbox closes the filesystem-escape vector but not the hang vector. | DEBT | noted in the 0.51 sandboxing work | later |

## 4b. Animation panel — what is still unfinished

Audited 2026-09-10 by reading `drawAnimationPanel` (`main.cpp`), `Animation.cpp`,
`AnimationData.hpp` and `TimelinePanel.cpp`.

**What works:** enable per entity, Loop toggle, a seconds scrubber, Record mode (continuous
capture of the posed transform at the scrub time), Add/Update Keyframe, a keyframe list with
Go To and Delete, Preview Playback, and the AI animation generator. Sampling is solid —
linear position/scale with **quaternion slerp** for rotation, so a 0→350° key takes the 10°
shortcut instead of spinning the long way.

| # | Gap | Status | Evidence |
|---|---|---|---|
| 4b.1 | ~~It is not frame-based at all.~~ **Fixed.** The panel now has an FPS field (1-240, default 24), snap-to-frames, a live frame counter, and First / Prev-frame / Next-frame / Last plus Prev-key / Next-key stepping. Keyframe times stay in SECONDS - the grid is an authoring aid, so changing the rate never rewrites existing keys. | FIXED | `TransformKeyframe::time` is a float in seconds. There is no FPS setting, no frame numbers, no snap-to-frame, and no step-one-frame-forward/back. For a panel described as frame-by-frame this is the headline gap — you cannot step through an animation a frame at a time. |
| 4b.2 | ~~Previewing permanently moves the object.~~ **Fixed.** The pose is snapshotted when Preview starts. A non-looping preview that runs to the end restores it automatically; a preview stopped by hand leaves the object where it is and offers Restore Pose, because stopping mid-way usually means "I want to look at this frame". | FIXED | `Preview Playback` writes each sampled pose through the command bus and never restores the pose it started from, so previewing leaves the entity wherever the animation ended. Play mode has snapshot/restore; this path does not. `AnimationPanelState` holds no saved pose. |
| 4b.3 | ~~Keyframes cannot be retimed.~~ **Fixed.** Each keyframe row has a draggable time field, quantised to the frame grid, re-sorting on release rather than mid-drag (sorting during the drag would reorder the list under the cursor and hand the drag to a different key the moment two crossed). The Timeline panel still only edits CineShot - generalising it remains worthwhile, but a plain object can now be retimed. | FIXED | Go To and Delete only. The draggable timeline that *can* retime exists — but `drawTimelinePanel` takes `std::vector<CineShot>&`, so it only ever edits cine shots. A plain animated object can be retimed nowhere. |
| 4b.4 | **Only transforms can be keyed.** `TransformKeyframe` is position/rotation/scale. Nothing can animate a light's intensity or colour, a camera's FOV or its lens layers, an entity's `active` flag, a material blend, or a UI element's opacity — which is most of what a cutscene actually needs, and all of it now exists as data. | MISSING | `AnimationData.hpp:15-21` |
| 4b.5 | **No interpolation control.** Every segment is linear (slerp for rotation). No ease-in/out, no constant/stepped hold — and stepped is precisely what frame-by-frame animation is built on. No curve or graph editor. | MISSING | `Animation.cpp` `sampleAnimation` |
| 4b.6 | **One clip per entity.** `SceneEntity::animation` is a single `EntityAnimation`, so there is no idle/walk/run set and no way to switch between them from a script. Separately, model import still keeps only the first clip from a GLB (3.4). | MISSING | `EditorScene.hpp` `animation` |
| 4b.7 | **Keyframe edits are not undoable.** Delete and the Loop toggle write through `findEntityMutable`, so a misclicked Delete cannot be undone. Consistent with the documented bookkeeping exception (§8.4), but losing recorded work to one click is a harsher consequence than the other cases in that list. | DEBT | panel's Delete handler |
| 4b.8 | **Partly done.** Each row has Copy, which duplicates the key one frame later - enough to build a hold or a variation without re-posing. Still no paste across entities, no multi-select, no mirroring, no time-scaling a whole animation. | PARTIAL |, no multi-select, no mirroring, no time-scaling a whole animation. |
| 4b.9 | **No onion skinning**, the standard frame-by-frame tool for seeing neighbouring poses. | MISSING | — |
| 4b.10 | **No playback speed or reverse**, and preview always runs at 1x forward from the first key. | MISSING | preview block |
| 4b.11 | **No animation events.** Nothing can call a script function or fire a trigger at a given time. Audio cues exist but, like retiming, only for cine shots. | MISSING | `Storyboard.hpp` `AudioCue` |
| 4b.12 | **Record mode does not auto-advance.** It rewrites the keyframe at the current scrub time every frame, so capturing a sequence means moving the scrubber by hand between every pose. | PARTIAL | panel's record block |

**Order I would fix these in:** 4b.2 first (it silently destroys authored transforms),
then 4b.1 + 4b.3 together (frames and retiming are one piece of work, and generalising
`drawTimelinePanel` to take any `EntityAnimation` rather than only `CineShot` gets both),
then 4b.4 (property tracks — the largest, and the one that unlocks animating the lights,
cameras and lens layers that now exist), then 4b.5.

## 5. Build & distribution

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 5.1 | **`Build Game...` does not produce a package.** It writes a `.gfai` copy of the scene and repoints `Project.json`. No models, textures, fonts, sounds or scripts travel with it. | PARTIAL | `main.cpp:2245-2272` | 6 |
| 5.2 | **A built game cannot be handed to anyone.** The Runtime resolves every asset against a live project tree on disk. | MISSING | `Runtime/src/main.cpp:70-95` | 6 |
| 5.3 | **No asset-dependency walker.** Nothing can answer "what does this scene actually need". | MISSING | — | 6 |
| 5.4 | **No engine-version compatibility check** on a loaded game. | MISSING | — | 6 |
| 5.5 | **The Runtime takes no command-line game argument** and has no file association. | MISSING | `Runtime/src/main.cpp` | 6 |
| 5.6 | **`GameForgerRuntime` is not in the default build preset** — it needs an explicit `--target`, so it silently goes stale. | DEBT | `CMakePresets.json` | 6 |
| 5.7 | **Neither is `GameForgerTests`, and that makes `ctest` lie.** `cmake --build --preset editor-debug` builds only `GameForgerEditor`, so a following `ctest -C Debug` happily runs a **stale test binary** and reports all green — including for tests that do not exist in it yet. Confirmed directly: three newly added tests reported "29 passed" (the old count) until `--target GameForgerTests` was built explicitly, then "32 passed". Any "CTest green" claim made without building that target first is worthless. Add both executables to the preset, or always build the target by name. | DEBT | `CMakePresets.json:35-46` | 6 |

## 6. AI layer

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 6.1 | **No knowledge base.** The Cockpit ships 7 scene tools + 2 project tools and no domain knowledge whatsoever. The sibling web project solves exactly this with OKF. | MISSING | `AICockpit.cpp:157+` vs `C:\GameForgerAI\catalog\OKF-SPEC.md` | 5 |
| 6.2 | **No skills** for level making, UI design, scripting, animation, cine shots, lighting or asset import. | MISSING | — | 5 |
| 6.3 | **`Providers.json` records no limits.** No `maxContextTokens`, `maxOutputTokens`, `maxToolCalls` — the web project has a researched, source-cited table for all of these. | MISSING | `Game/AI/Providers.json` vs `C:\GameForgerAI\server\utils\aiProvider.js` | 5 |
| 6.4 | **No context budgeting.** Nothing clamps the prompt to the provider's window; a long injection overflows silently. The web project guards this. | MISSING | cf. `aiProvider.js:917-923` | 5 |
| 6.5 | **No context / tool-call meter** in the UI. | MISSING | cf. `client/src/components/ContextMeter.jsx` | 5 |
| 6.6 | **One flat agent loop, no roles.** No level-designer → scripter → UI-designer → cine-director hand-off, and every pass carries every tool. | PARTIAL | `sendCockpitPrompt`, `AICockpit.hpp:134` | 5 |
| 6.7 | **The AI planner cannot reach most of the editor.** 15 ops, covering entities, transforms, tags, terrain and scripts — nothing for audio, animation, cine, materials, colliders, pickups, or the new object types. | PARTIAL | `AICommandPlanner.cpp:212-301` | 5 |
| 6.8 | **API keys sit in plaintext** in `Game/AI/Providers.local.json`. | DEBT | long-standing audit item | later |

## 7. Shell, UX & polish

| # | Gap | Status | Evidence | Phase |
|---|---|---|---|---|
| 7.1 | **No brand theme.** Stock `ImGui::StyleColorsDark()` with a few tweaks — nothing of the yellow/black logo identity. | MISSING | `main.cpp:2922-2930` | 7 |
| 7.2 | **Splash is a still image;** `Game/Branding/intro.mp4` (1.2 MB) ships unused. | PARTIAL | `SplashScreen.hpp` | 7 |
| 7.3 | **Default dock layout** — now laid out like a known editor (see 7.3b). Still missing the Information tab beside Hierarchy, which arrives with Phase 3. | PARTIAL | `EditorLayout.cpp` | 3 |
| 7.3b | ~~The default layout does not place every panel, and the mess it produces is self-perpetuating.~~ **FIXED.** There was no default layout at all - `buildDefaultDockLayout` now builds one: Hierarchy + Project left, Inspector + Toolbox right, log and authoring tabs across the bottom, 3D views filling the centre. Applied on a fresh clone (no ini) and on Edit > Reset Editor Layout, which previously only cleared the ini and left everything floating. Original finding: Deleting `GameForgerEditorLayout.ini` (or a fresh clone, which has none) leaves Timeline, Audio, Project Settings, Performance and Mind Graph floating and overlapping rather than docked — `EditorLayout.cpp`'s default only places the panels that existed when it was written. Worse, the editor then *saves* that floating arrangement, so the next launch repeats it: the only way out is Edit > Reset Editor Layout. Found by deleting the ini during Phase 0 verification. | BROKEN | `EditorLayout.cpp` vs the panel list in `main.cpp` | 7 |
| 7.4 | **No icons on Hierarchy rows** — a light, a camera and a cube all read as plain text. | MISSING | `drawHierarchyPanel` | 7 |
| 7.5 | **No in-app help, shortcut list, or onboarding.** | MISSING | — | later |

## 8. Structural debt

| # | Item | Why it matters |
|---|---|---|
| 8.1 | **`Editor/src/main.cpp` is 8,765 lines**, single TU, no forward declarations — a helper must be defined textually above its call site or the build fails with `C3861`. Every panel, tool and interaction lives in it. | Any phase touching UI pays this tax. Split panels out as they are edited; do not attempt a big-bang refactor. |
| 8.2 | **A second, dead entity model exists.** `Core/GameObject.hpp` + `Core/Component.hpp` + `Core/Material.hpp` + `Core/AssetDatabase.hpp` are referenced by nothing but `Engine/tests/TestMain.cpp`. The live model is `SceneEntity`. | Two sources of truth, one of them tested and unused. Decide: adopt it, or delete it. Do not silently extend it. |
| 8.3 | **Renderer duplication** (see 1.10). | Shader changes must land twice. |
| 8.4 | **Some editor state bypasses the command bus** (terrain sculpt, keyframe edits, storyboard reorder, appearance fields) and is therefore not undoable. | Deliberate and documented, but the exceptions list grows — new features should justify joining it. |
| 8.5 | **`cmake --build` can report success and hand back a stale exe** when only a `configure_file()`-generated header changed. | Verify the exe's mtime advanced, or touch a `.cpp`, before calling a version bump live. |
| 8.6 | **MSVC env is not in this shell.** A bare `cmake --build` fails on missing `INCLUDE`; it must go through `vcvars64.bat`. Linking fails while `GameForgerEditor.exe` is running. | Every build step in every phase. |

---

## Coverage by phase

| Phase | Closes |
|---|---|
| 1 — Lights + shadows | 1.1 – 1.4 · extract main.cpp math helpers to `Engine/` for testability (§1d) |
| 2 — Camera, Empty, UI children | 2.1 – 2.4, 2.6, 2.7 |
| 3 — Information tab | 3.1 – 3.3, 3.6 |
| 4 — GFScript + FPS scripts | 1.8, 4.1 – 4.7 · 4b.1 – 4b.5 (animation panel) · 1c.5 (prune orphan scripts) · extend `testAllShippedScriptsLoad` to `.gfs` |
| 5 — OKF, provider limits, subagents | 6.1 – 6.7 · 1c.3 |
| 6 — `.gfpak` packaging | 5.1 – 5.6 · **1b.1 – 1b.3 + the parity test** |
| 7 — Shell polish | 2.9, 3.7, 7.1 – 7.4 · 1c.1, 1c.2 |
| Deferred | 1.5 – 1.7, 1.9, 2.5, 2.8, 3.4, 3.5, 3.8, 4.8, 4.9, 1c.4, 6.8, 7.5, and all of §8 |

### Do these first, regardless of phase order

1. **§1b — the three Runtime stubs.** This is the only category where the editor lies to the
   user: they build a game that works in Play and ships broken. It sits in Phase 6 because
   that is where the Runtime is already open, but if a siege/catapult demo matters sooner,
   pull it forward — the fix is small (move three pieces of state into `GameplayState`), and
   the parity test that prevents a fourth is smaller still.
2. **§1d — get `main.cpp`'s transform math under test.** Every phase from 1 onward edits that
   file. Extracting the pure functions as they are touched costs almost nothing per phase and
   is the difference between the hierarchy work being verifiable and being hoped-for.
