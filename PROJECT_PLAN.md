# PROJECT_PLAN.md — GameForgerAI Editor

Living engineering record, per `docs/CLAUDE.md` §7. Updated after every meaningful phase.

**Current objective:** bring the editor to Unity parity, then add Mind Graph visual scripting.
**Branch:** `unity-parity-upgrade` (pushed) · **Base:** `main` · **Version:** Alpha 0.79.0
**Stable fallback:** `main` and `phase-a-d-source` are untouched.

---

## 1. Current state

A native Win32 C++20 / OpenGL 4.6 / GLFW / Dear ImGui editor plus a standalone
`GameForgerRuntime.exe` that plays built games, and an AI layer (AI Forge planner + agentic
AI Cockpit) over a multi-provider config.

Two reference documents govern this work:

- **`MissingFunctions.md`** — the audit. Every gap with file/line evidence, graded
  MISSING / PARTIAL / BROKEN / DEBT / FIXED. Read this before proposing work.
- **`docs/MindGraph-Plan.md`** — the visual scripting addon, planned, not started.

---

## 2. Discovered architecture

- **One renderer.** `ViewportRenderer` is shared by the Editor viewport, the Editor Game
  view and the Runtime. The old `SceneRenderer` duplication is gone.
- **One scene model.** `SceneEntity` is the live model. `Core/GameObject.hpp` +
  `Component` + `Material` + `AssetDatabase` are a **second, dead** model reached only by
  `TestMain.cpp` (audit §8.2). Do not extend them.
- **Entity identity is the name.** `EditorScene.hpp:440`: the save format identifies
  entities by name, not id. `parentName`, tags and every cross-entity reference follow this.
  There is no entity GUID.
- **All mutation goes through `AICommandBus`** — validated and undoable — except a
  documented set of per-frame bookkeeping paths (terrain sculpt, keyframe recording, camera
  pose sync, effects sliders). Extending that exception list requires a stated reason.
- **Scripts are Lua**, sandboxed (`base`/`table`/`string`/`math` only, `dofile`/`loadfile`
  removed), path-confined by `resolveProjectFile`, run by `ScriptRuntime` in **both** hosts.

---

## 3. DECISIONS

| # | Decision | Why |
|---|---|---|
| D1 | GFScript transpiles to Lua; it is not a new VM | Zero risk to shipped scripts; inherits sandbox, both hosts, tests |
| D2 | Shadows: sun CSM + spot maps; point lights lit but not occluding | Cube maps are the expensive case; stated in the Inspector, not hidden |
| D3 | Light falloff is a smooth window, **not** inverse-square | No PBR/HDR/tonemapping here — physically-correct attenuation makes intensity 1 invisible at 2m |
| D4 | A built game is a single `.gfpak` with a VFS read path | Not started |
| D5 | AI knowledge is OKF `.okf.md`, ported from `C:\GameForgerAI` | Same format both projects, editable without a rebuild |
| D6 | Viewmodels are **children of the Main Camera** | No separate viewmodel pass; reuses the parent-constraint solver |
| D7 | Cine is a **mode on a Camera**, not a separate object type | One object films gameplay or flies a path; effects shared by both |
| D8 | The authoring Viewport is never colour-graded | You cannot place an object accurately through a heat map |
| D9 | Mind Graph compiles to Lua; no C++ graph interpreter | A second engine means teaching every binding/sandbox/host twice — the seam that caused §1b |
| D10 | Mind Graph links serialize by **(nodeId, pinStringId)**, never int index | Ints assigned at load shift when a node's pins change, silently rebinding links |
| D11 | Graph scene literals store **names/paths**, not GUIDs | This engine has no entity GUID; adding one is a scene-format overhaul, not a node-editor detail |

---

## 4. DONE

Each item verified as stated — builds, tests and live runs actually executed.

| Commit | Work | Verification |
|---|---|---|
| `a902fbc` | **Audit** → `MissingFunctions.md`. Two passes: feature gaps, then a defect pass. Found the §1b class: three `ScriptRuntime` callbacks real in the Editor, stubbed in the Runtime — catapult firing, catapult aiming, held-item state — so a scene that plays correctly ships broken. | 3 configs build clean; CTest green |
| `ee12342` | **Verdict + correction.** Counts up front. Corrected one mischaracterised finding; the real one beside it is `RequestAnimationCommand` — validated, announced to the user, never executed. | — |
| `2ccc67e` | **Lights, Camera, Empty, UI elements + shadows.** Replaced the hardcoded `normalize(vec3(0.4,0.85,0.35))` inlined in 4 shaders. Shadow atlas (one texture, per-light tiles — GLSL cannot index sampler arrays dynamically). `isGizmoOnlyEntity()` replaced scattered `isCineCamera` checks, which also closed an out-of-bounds: the mesh pass indexes a 6-element array and `Empty` is the 7th enum value. | Debug clean; editor runs with empty stderr (compileProgram logs GLSL failures there, so silence is evidence) |
| `1dcf97c` | **Editor + Runtime wiring.** Menus, Toolbox, Add Child, 3 Inspector sections, picking, `setLens`. **Runtime: added Main Camera fallback and disabled editor gizmos** — both were §1b-class defects found by *running* the Runtime, not reading code. | 32/32 tests; standalone run with sun+spot+point shadows |
| `db795ec` | **Camera lens layers; Cine became a mode.** Full-screen post pass: 7 named filters incl. Heat Map, gradient map, grade, grain, flicker, scanlines, vignette, chromatic aberration, 6 presets. **Two bugs caught by running it:** the Runtime never called `setCameraEffects` (a grade would not have shipped), and `resize()` never created the post buffer (pass silently skipped — a stderr probe printing `anyEnabled=1 program=1 fbo=0` named it). | 3 configs; 33/33; Old Film verified in the Runtime |
| `d7f6d8f` | **Animation panel audit** — twelve items, §4b. | — |
| `902310b` | **Animation panel: frames, retiming, preview fix.** Preview used to permanently overwrite the authored transform; now snapshot/restore. FPS grid + frame/key stepping (times stay in seconds — the grid is an authoring aid). Per-row retime, re-sorting on release. Copy key. | 3 configs; 33/33; editor runs clean |
| `d430aad` | **Mind Graph plan** authored. | — |
| `aaff04d` | **Mind Graph plan amended** — §13 catalog/pin contract, §14 literal refresh (correcting the review's GUID recommendation), §15 breadcrumb wrapper, §16 amended phases. This file created. | — |
| `307fddf` | **§1b parity: five defects fixed by removing the cause.** All eight GameplayState/AudioEngine-backed `ScriptRuntime` callbacks now bind once in Engine (`bindSharedScriptCallbacks`), called by both hosts, so a callback is bound in both **by construction**. Deleted ~6.8 KB of duplicated binding. Two *new* instances of the class surfaced while fixing the first three: `projectilesFiredThisTick` never reset in the Runtime, and the Runtime firing only `OnPlayStart` audio hooks. | 3 configs; 34/34; **test verified by reintroducing the real defect** and confirming it fails |
| `4e59bd8` | **Default dock layout (7.3b).** There was none: a fresh clone and Edit > Reset both left every panel floating, and the editor saved that, so the mess repeated every launch. Now builds the Unity-style arrangement — Hierarchy/Project left, Inspector/Toolbox right, log and authoring tabs bottom, 3D views centre. | Verified by deleting the ini and screenshotting the result |
| `4dc97e6` | **Mind Graph Phase 1** — graph model + `.gfgraph` serializer, no UI. Links serialize as `(nodeId, pinId STRING)`, never an integer index; ids recomputed from content so a hand-edited file cannot reissue a live id; broken links preserved and reported rather than dropped. | 3 configs; 36/36 |
| `9c92b81` | **Mind Graph Phase 2** — 12-node catalog + graph→Lua compiler with its own runtime prelude. Added `world:setLightIntensity` / `setLightColor`, without which Set Light had nothing to call. **`testMindGraphGeneratedLuaRuns` caught a real bug**: the emitter wrote `play(clip, loop)` when the binding is `play(clip, VOLUME, loop)`. | 3 configs; 40/40 |
| `63f1dc5` | **Mind Graph Phase 0.** Pinned imgui-node-editor to commit `021aa0ea`, not the v0.9.3 tag — the tag fails to compile against ImGui 1.92.3 (it redefines `ImVec2` operators 1.92 now provides). `GIT_SHALLOW FALSE`, same reason ImGuizmo documents. Repo ships no CMakeLists, so its four sources build as `GameForgerNodeEditor`. Canvas renders with draggable links. | Debug + all-release clean; 34/34; canvas verified in the editor |
| `1acc01b` | **§1b.4 HUD — the last one.** Moved HUD drawing out of the Editor's ImGui path into `ViewportRenderer`, which both hosts share. New `OverlayFont` in Engine (extracted, not copied — GameMenu and TextMesh each already had one). Two bugs only visible by running it: the UI host was `followedEntity` (the *player*, not a camera), and text was centred on its anchor so top-left labels spilled off-screen. `UIElementData::fontPath` now actually works. | 3 configs; 34/34; editor clean; **crosshair + both text lines verified in a standalone Runtime run** |

| `0d45678` | **Mind Graph Phase 3** — the panel: categorised searchable palette, canvas, details strip with scene-bound dropdowns, compile bar. Pin handles are rebuilt per frame and never persisted; `literalResolves()` implements §14 (names/paths, not GUIDs, checked against the live scene). | 3 configs; 40/40; canvas driven in the editor |
| `2825336` | **Panels menu.** Every panel was hardcoded open, so a closed one could not be reopened and Mind Graph had no visible entry at all. `PanelVisibility` in `main()`, each panel taking only its own flag by reference, plus "Show All Panels". | 3 configs; 40/40 |
| `63738bd` | **Scripts panel (§4.7).** Scripting was fused into the Inspector — the file list, attach controls and AI prompt all buried inside one object's property sheet, with a *modal* editor that blocked the app. Now a dockable panel with a real editor and a Create/Modify switch, so the AI can change an existing script rather than only write a new one. | 3 configs; 40/40 |
| `6b9a51c` | **Hierarchy → Scripts bridge.** Right-click an object → `Add Script...` selects it and raises the panel. `Load to Object` is always present and refuses with "No object is selected!" rather than vanishing — a control that disappears when unusable teaches nothing. | 3 configs; 40/40 |
| *(this commit)* | **Consolidated the old script UI away.** Deleted the `Add Script` and `Edit Script` modals, `ScriptCreatorState`, `ScriptEditorState` and two orphaned naming helpers (~500 lines); moved the 14 presets into the Scripts panel; repointed every entry point (Inspector `Edit`/`Add Script...`, Hierarchy right-click, Project browser `.lua` click) through `requestOpenPath`. Two defects went with it: the same script could be open in a modal *and* the panel with two divergent copies of its text, and applying **any** preset set `Collider` — so an Audio Manager on an empty silently blocked the player. `marksCollider` is now per-preset data that matches each description. | 3 configs clean, no new warnings; 40/40 |

**Also shipped:** 37 GLB models imported to `Game/Models/kit/`; four reusable script presets
(Weapons System, Door, Keypad Panel, Key Item) registered as presets; the
`FPS_controller_scene_demo.gfprod` demo (72 entities) plus its generator; Lua bindings for
`input:getScrollDelta`, `input:isMouseButtonDown`, `world:setEntityActive`,
`world:isEntityActive`; `entityForward()`, `gameCameraEye()`, `applyCameraPoseToEntity()`,
`syncMainCameraToPlayView()` in Engine.

---

## 5. TODO

**Immediate — needs approval before code (per `docs/CLAUDE.md` §1):**

| Priority | Work | Notes |
|---|---|---|
| ~~P0~~ | ~~§1b Runtime parity fix~~ | **DONE** (`307fddf`) — and the state never needed moving; it had always been in `GameplayState`. The callbacks were simply never wired to it. |
| ~~P0~~ | ~~§1b.4 HUD~~ | **DONE** (`1acc01b`). §1b now has zero remaining defects. |
| ~~P1~~ | ~~Mind Graph Phase 0~~ | **DONE** (`63f1dc5`). It earned its keep: the latest tag (v0.9.3, 2023) does **not** compile against our ImGui 1.92.3 — 1.92 defines `ImVec2` comparison operators the tag redefines. Pinned to the commit SHA with the reproduction recorded in `CMakeLists.txt`. |
| ~~P1~~ | ~~Mind Graph Phases 1 and 2~~ | **DONE** (`4dc97e6`, `9c92b81`). Model, serializer, 12-node catalog and graph-to-Lua compiler, all with no UI and all tested. |
| ~~P1~~ | ~~Mind Graph Phase 3~~ | **DONE** (`0d45678`). The literal-refresh spike folded into it, as planned. |
| ~~P1~~ | ~~Scripts panel + Panels menu (§4.7, §7.3)~~ | **DONE** (`2825336`, `63738bd`, `6b9a51c`, and the consolidation above). The Inspector's old script UI is fully removed, not merely bypassed. |
| **P1** | **Mind Graph Phase 4** | Next: Trigger Zone entity trait, enter/exit in `GameplayLoop` (Engine, both hosts) plus its parity test. |
| P1 | **Serialized script fields** | Read `self.*` back after `on_start`, store per-entity overrides, re-apply on Play. Makes `weapons_system.lua`'s slot table editable without opening the file. |
| P2 | **Animation 4b.4** — property tracks | Key light intensity, camera FOV, lens layers, `active`, UI opacity. The largest animation item and the one that unlocks real cutscenes. |
| P2 | **Animation 4b.5** — interpolation modes | Ease in/out and **stepped** — stepped is what frame-by-frame is built on. |

**Main-plan phases not started:** 3 (Information tab + `backintime<gamename>.md`),
4 (GFScript), 5 (OKF + provider limits + subagents), 6 (`.gfpak`), 7 (brand theme + intro.mp4
splash + dock layout).

**Deferred, recorded in the audit:** PBR, skybox/fog, post-AA, particles, LOD/culling,
prefabs, layers, multi-clip model import, sub-object hierarchy, shared material assets,
script debugging, script CPU budget, plaintext API keys.

---

## 5b. PROPOSED — `physicsExpanded`, an all-in-one physics controller

**Status: awaiting approval (`docs/CLAUDE.md` §1). No code written.**

### 5b.1 What was inspected

| Read | What it established |
|---|---|
| `EditorScene.hpp:280-390` | Ten additive traits already follow one shape: `bool isX` + `XData`. A physics trait is the eleventh, not a new pattern. |
| `ViewportRenderer.cpp:1069-1165` | `createGizmoMeshes()` already builds a unit-radius wireframe sphere (the point light's three orthogonal rings), scaled to the light's range at draw time. The sphere the request asks for already exists; it needs colours and a second draw pass. |
| `ViewportRenderer.cpp:1661-1740` | The gizmo pass is gated on `isGizmoOnlyEntity(entity)` — so it **skips anything with a mesh**. A brick cannot get a gizmo through this path. A second pass is required. |
| `main.cpp:9046-9048` | Three renderer instances: `viewportRenderer`, `gameViewRenderer`, `cineCameraRenderer`. **None calls `setShowEditorGizmos(false)`.** |
| `Runtime/src/main.cpp:418` | Only the Runtime turns gizmos off. |
| `GameplayLoop.hpp:225` | `tickProjectiles(scene, commandBus, gameplay, isPlaying, deltaTime)` — the engine already owns simulated motion for both hosts. `tickPhysicsBodies` belongs beside it. |
| `Collision.hpp:40` | `resolveBoxCollision` is the one collision query; `rigidbody.lua` reaches it via `self.physics:resolve()`. |
| `Game/Scripts/rigidbody.lua` | Current gravity is ~40 lines of Lua per object: one axis, no rotation, no fields, no mass. |
| `MindGraph/NodeCatalog.cpp` | 12 nodes across `event.` / `flow.` / `audio.` / `world.`. A `physics.` category is additive. |

### 5b.2 Discovered — an existing defect this work must fix

**Editor gizmos are visible in the editor's Game view and cine preview.** `setShowEditorGizmos`
defaults true and only `GameForgerRuntime` turns it off, so light, camera, Empty and UI
wireframes draw into both in-editor play surfaces today. The request ("we don't see those
wireframes in game") cannot be satisfied for physics without fixing this for every gizmo.
Two lines, and it corrects existing behaviour — recorded as audit item **1c.6**.

### 5b.3 Intended change

**One trait, one struct, one script.** `bool hasPhysicsBody` + `PhysicsBodyData physics` on
`SceneEntity`, additive exactly like `hasCollider`/`isCastle`.

`PhysicsBodyData` — three groups:

**Body** (how this object moves): `bodyType` (Static/Kinematic/Dynamic), `mass`,
`centerOfMassOffset` (local vec3 — the centre of gravity), `centerOfMassRadius` (its gizmo
size), `gravityScale`, `useCustomGravity` + `customGravity` (a space scene has no global
down), `linearDrag`, `angularDrag`, `restitution`, `friction`,
`freezePositionX/Y/Z` + `freezeRotationX/Y/Z`, `maxSpeed`, `canSleep` + `sleepThreshold`,
`initialVelocity`, `initialAngularVelocity`, `continuousCollision`.

**Field** (how this object pulls and pushes others — the planet/asteroid case):
`fieldEnabled`, `attractionEnabled` + `attractionRadius` + `attractionStrength`,
`repulsionEnabled` + `repulsionRadius` + `repulsionStrength`, `falloff`
(Constant / Linear / InverseSquare), `affectsTag` (empty = everything).

**Linkage:** `inheritFieldFromParent` (default **true**) — a child with the trait shares its
parent's field values; untick it and the child becomes an independent well with its own
push/pull, which is the rule the request states.

**Three coloured wireframes**, built from the existing `ring()` helper, drawn in a new pass:

| Gizmo | Colour | Radius from |
|---|---|---|
| Centre of gravity | amber `(1.00, 0.80, 0.25)` | `centerOfMassRadius`, positioned at `centerOfMassOffset` |
| Attraction (pull) | cyan `(0.30, 0.70, 1.00)` | `attractionRadius` |
| Repulsion (push) | red `(1.00, 0.35, 0.30)` | `repulsionRadius` |

Cool = inward, warm = outward, and neither collides with the warm yellows already used by
lights or with the selection outline.

**Simulation lives in Engine, not Lua** (decision D12 below): `tickPhysicsBodies()` in
`GameplayLoop.cpp`, called by both hosts beside `tickProjectiles`. Semi-implicit Euler with
fixed substeps, contact through the existing `resolveBoxCollision`.

**`physics_expanded.lua`** is then the authoring and scripting surface — preset #15, exposing
the trait to gameplay through new `self.physics:` bindings (`applyForce`, `applyImpulse`,
`getVelocity`, `setVelocity`, `setGravityScale`, `setFieldEnabled`, `setFieldStrength`,
`setFieldRadius`). It does not integrate motion itself.

**Mind Graph** gains a `physics.` category addressing wells **by entity tag**, reusing the
existing tag system rather than introducing a second namespace: `physics.set_field`,
`physics.apply_impulse`, `physics.set_gravity_scale`, `physics.freeze`.

### 5b.4 Two calls I made that are worth overruling if wrong

1. **Gizmos draw for selected entities only.** A scene of 50 bricks with three always-on
   spheres each is unreadable, and Unity shows this class of gizmo on selection. The
   alternative is always-on with a global toggle.
2. **Mind Graph addresses wells by the existing entity tag**, not a new `physicsTag` field.
   One tag namespace, already serialized, already in the Inspector, already used by zone
   nodes. The alternative is a dedicated field that cannot collide with gameplay tags.

### 5b.5 Honest limit

This delivers gravity, directional fields, drag, bounce, mass, per-axis constraints and
sleep, with AABB contact. It is **not** a constraint-solving rigid-body engine: no contact
manifolds, no friction cones, no joints, no inertia tensor. Bricks, boxes, planks and crates
will fall, bounce, settle, and be pulled and pushed correctly; a twenty-brick wall will not
stack with perfect stability. Saying so now is worth more than discovering it at the demo.

### 5b.6 Files likely to change (~14)

`AICommand.hpp` (struct, enums, name helpers, `SetPropertyCommand` component `"Physics"`) ·
`EditorScene.hpp` (trait) · `AICommandBus.cpp` (validation/routing) · `SceneSerializer.cpp`
(nested `"physics"`, additive, **enums by name**) · `GameplayLoop.hpp/.cpp`
(`tickPhysicsBodies`) · `ViewportRenderer.hpp/.cpp` (3 sphere gizmos + selected-only pass) ·
`ScriptRuntime.cpp` (bindings) · `MindGraph/NodeCatalog.cpp` + `GraphCompiler.cpp` ·
`Editor/src/main.cpp` (Inspector Physics section; `setShowEditorGizmos(false)` on the game
and cine renderers) · `Editor/src/ScriptsPanel.cpp` (preset #15) · `Runtime/src/main.cpp`
(call the tick) · `Game/Scripts/physics_expanded.lua` (new) · `Engine/tests/`.

No new third-party dependency. No new build step.

### 5b.7 Risks

| Risk | Mitigation |
|---|---|
| Physics is the classic place a demo scene silently diverges between hosts | It lives in Engine and ships with a parity test — the §1b rule applied *before* the defect, not after |
| An unstable integrator makes objects explode or sink at low frame rates | Fixed substeps independent of frame time; `maxSpeed` clamp; a test asserting a dropped box settles and stays settled |
| O(bodies × wells) per frame | Wells are few by nature; radius rejection before force maths; a frame-time check against the Phase 0 baseline, which is already in the validation bar |
| Serializing new enums by ordinal would silently corrupt scenes on reorder | Project rule already: by name, with an unknown-value-to-default test |
| `SceneEntity` grows again | It is the established pattern; the alternative (a side table keyed by name) is worse given identity **is** the name |

### 5b.8 Validation

Debug + Release + `all-release` clean, no new warnings · 40 → ~46 tests green, built with
`--target GameForgerTests` · no frame-time regression on the reference scene · and driven
live: drop a stack of bricks, pull them into orbit around an asteroid with attraction on and
repulsion off, confirm all three wireframes are adjustable in the Viewport and **absent from
the Game view and the Runtime**.

New tests: field falloff (inverse-square at 2r is exactly ¼) · child inherits parent field,
and does not when unticked · frozen axis never moves · serialization round-trip including
unknown enum → default · a dropped body settles and sleeps · **both hosts reach
`tickPhysicsBodies`** (the parity test).

### 5b.9 Proposed decision to add to §3

| # | Decision | Why |
|---|---|---|
| D12 | Physics integration lives in Engine (`tickPhysicsBodies`), called by both hosts; `physics_expanded.lua` is the authoring surface, not the simulator | Same reasoning as D9. Field accumulation is O(bodies × wells) per frame — interpreted Lua caps that at a handful of objects — and `tickProjectiles` already set the precedent that the engine owns simulated motion. It is also testable without a Lua VM. |

---

## 5c. PROPOSED — the FPS viewmodel: hands, rifle, bullets, impacts

**Status: awaiting approval. Asset copied in (no code changed).**

Requested: the Weapons System preset should build a hand set from
`animated_fps_hands_rifle_animation.glb`, use its rifle as the gun, and make it
"work perfectly with bullets shooting out and impacts".

### 5c.1 What the asset actually is

Probed by parsing the GLB's own JSON chunk (`asset.extras` carries the credit):

- **Animated FPS hands (rifle animation pack)** by **Cransh**, **CC-BY-4.0** — attribution
  is a licence obligation, recorded in `Game/Models/viewmodel/CREDITS.md` and owed a line in
  the game's credits screen, not just a file in the repo.
- 5 meshes (arms + ACR rifle, silencer, scope, pmag), **26,694 triangles**, one skin with
  **81 joints**, 16 embedded textures (~11.8 MB of the 14.5 MB).
- **8 animation clips**: Idle, Walk, Run, Draw, Shoot, Reload_Fast, rifle_inspect, OneShot.

Copied to `Game/Models/viewmodel/`. Loose-tree only for now; `.gfpak` is Phase 6.

### 5c.2 Four blockers between this asset and "works perfectly"

| # | Blocker | Evidence |
|---|---|---|
| B1 | **Only one animation clip is imported.** `ImportedModel` keeps the file's *first* `aiAnimation` and auto-loops it. Here that is `Draw` — so the hands would loop a weapon-raise forever, and Idle/Walk/Run/Shoot/Reload are in the file but unreachable. | `ModelImport.hpp:78` states it outright |
| B2 | **No muzzle exists.** `weapons_system.lua` already looks for an entity tagged `gun_muzzle` (`muzzle_tag = "gun_muzzle"`, line 32) and falls back to the player's own position — nothing in the project ever creates that entity, so every shot currently leaves the player's chest. | `weapons_system.lua:28-33, 82-94` |
| B3 | **Bullets are invisible.** `GameplayState::Projectile` is simulated, moved and despawned, but never drawn by anything. | `GameplayLoop.cpp:180-262`; no projectile case in `ViewportRenderer` |
| B4 | **There are no impacts, and bullets pass through walls.** On a hit `tickProjectiles` does `++projectilesHitThisTick` and discards the position — nothing marks where it landed. Worse, a straight `fireProjectile` only tests entities that carry the damage tag, so a bullet flies through walls, floors and terrain and only ever stops on something tagged `Enemy`. | `GameplayLoop.cpp:236-262` |

B1 and B4 are the two that make "perfectly" impossible today; neither is a script change.

### 5c.3 Intended change

**1 — Multi-clip import (engine).** Import every `aiAnimation`, keyed by name.
`ImportedMeshData` gains `clipName`, `clipSpeed`, `clipLoop` (serialized, additive; empty
`clipName` keeps today's "first clip" behaviour so no existing scene changes). Inspector gets
a dropdown listing the file's real clip names. New binding
`self.entity:playClip(name, loop)` plus `clipFinished()`, so a script can drive Shoot on
fire, Reload on reload, Walk/Run from movement speed, Idle otherwise.

**2 — Viewmodel rig (preset).** Applying **Weapons System** now also: imports the GLB if it
is not already in the scene, parents it to the Main Camera (D6), sets the viewmodel offset,
and creates a child Empty tagged **`gun_muzzle`** at the barrel tip — the tag the script has
always been asking for. One click gets hands + rifle in front of the camera.

**3 — Visible bullets.** Each projectile draws as a short tracer in the line pass the gizmos
already use. Cheap, no new geometry, and it reads correctly at speed — a sphere at 60 m/s is
a strobe, a tracer is a streak.

**4 — Impacts and honest collision (engine).** `Projectile` records its hit point and the
surface normal. `fireProjectile` stops on **any** collider, not only tagged entities —
tag-gating stays, but only for *damage*, not for whether the bullet exists. On impact,
`tickProjectiles` spawns a short-lived impact marker at the hit point, oriented to the
normal, despawned on a timer. Both hosts get it: it lives in `GameplayLoop`.

### 5c.4 The call worth overruling

**B4 changes existing behaviour.** Today a bullet passing through a wall is what
`enemy_ai.lua` and `ranged_attacker.lua` rely on — an enemy shooting from behind cover still
hits. Making bullets stop on geometry is correct for a shooter and will make those two
presets miss shots they currently land. I intend to change it anyway and fix the presets,
because "bullets go through walls" is not a behaviour worth preserving. Say so if you would
rather keep it opt-in per weapon.

### 5c.5 Honest limits

- 26,694 triangles and 81 joints on screen every frame, skinned on the GPU. Fine alone;
  it will show up in the frame-time budget and gets measured against the Phase 0 baseline.
- 16 embedded textures at up to 1.8 MB each — the model is 14.5 MB in a repo that was 1.6 MB
  of models before it. Worth knowing before `.gfpak` (Phase 6) has to carry it.
- Impact *decals* are not in scope: a decal needs projected geometry the renderer has no pass
  for. The impact marker is a small oriented effect entity, which is what the existing
  renderer can do honestly.

### 5c.6 Files likely to change

`ModelImport.hpp/.cpp` (all clips, lookup by name) · `EditorScene.hpp` (`clipName`/
`clipSpeed`/`clipLoop`) · `SceneSerializer.cpp` · `ViewportRenderer.cpp` (clip selection in
skinning; tracer pass) · `ScriptRuntime.cpp` (`playClip`, `clipFinished`) ·
`GameplayLoop.hpp/.cpp` (hit point + normal, collider-stop, impact spawn) ·
`Editor/src/ScriptsPanel.cpp` (Weapons System preset builds the rig) ·
`Game/Scripts/weapons_system.lua` (clip driving, muzzle, recoil) ·
`enemy_ai.lua` + `ranged_attacker.lua` (adjust for B4) · `Engine/tests/`.

### 5c.7 Validation

Debug + Release + `all-release` clean · tests green plus new ones: all 8 clips import and
are addressable by name · unknown clip name falls back rather than crashing · a projectile
stops on an untagged collider · impact position is recorded at the surface, not the entity
centre · round-trip of the new fields · **both hosts spawn impacts** (parity). Then driven
live: apply the preset, see hands and rifle, fire, watch tracers leave the muzzle and impacts
appear on a wall — in the editor Game view **and** the standalone Runtime.

---

## 5d. PROPOSED — Empty-based player + a real character controller

**Status: awaiting approval. The Inspector button bug in it is already FIXED (`6edd6c5`).**

Requested: build the player from an **Empty** rather than a capsule — collider on the Empty,
tagged `Player`, with FPS Controller + Weapons System + Inventory attached; expose **ground
distance** and a **step height** so a staircase is walked up instead of jumped; make the
terrain hold a collider; and return the Inspector to its real job — adjusting values and
seeing the change live — now that scripts are attached from the Scripts panel.

### 5d.1 What already works (verified, not assumed)

| Claim | Reality |
|---|---|
| An Empty can carry a collider | **Yes.** `generatePrimitiveMesh` handles `PrimitiveType::Empty` explicitly (no geometry, no out-of-bounds — the trap that bit the renderer does not exist here), and a Box collider uses `colliderWorldAabb` = position ± scale, which an Empty has. |
| The FPS controller works on a non-capsule | **Yes.** `fps_controller.lua:29-30` hardcodes `collider_radius = 0.4` / `collider_height = 2.0`; it never reads the entity's mesh or scale. |
| Terrain can be collided with | **Yes, already.** `Collision.cpp:744-767` samples the heightmap and grounds you on it. It is gated on `hasCollider`, and the Inspector's "Solid" checkbox (`main.cpp:6467`) is not hidden for terrain — so this is a tick-the-box, not a missing feature. |

So the Empty player is mostly reachable today. Three things genuinely are not.

### 5d.2 The three real gaps

| # | Gap | Evidence |
|---|---|---|
| G1 | **No step-up. This is the headline request.** `resolveBoxCollision` pushes a box *out* of what it hits. Walking into a 20 cm stair is identical to walking into a wall — you stop, and the only way up is Space. There is no step offset anywhere in the collision code. | `Collision.cpp:735+`, no step term |
| G2 | **Ground distance is not a tunable.** `resolve()` returns a `grounded` bool decided inside the engine; nothing exposes how far below the collider still counts as ground. On a slope or a stair edge that is the difference between walking and stuttering. | `Collision.hpp:40-46` |
| G3 | **The Inspector cannot edit script values outside Play.** Exposed script fields read `prop.defaultNumber` when `scriptRuntime.isRunning()` is false, and the write is inside `if (scriptRuntime.isRunning())` — so in edit mode you drag a slider and the value is **silently discarded**. That is precisely the "adjust values and see the change live" the request is asking for, and it does not work. | `main.cpp:~7050-7100` |

### 5d.3 Intended change

**1 — Character controller in Engine.** `resolveBoxCollision` gains `stepHeight` and
`groundProbeDistance`. Step-up is the standard sweep: when a horizontal move is blocked,
retry it raised by `stepHeight`, and accept the result only if the raised position is clear
**and** there is ground under it — which is what stops a character climbing a wall one step
at a time. Both defaults are 0, so every existing caller behaves exactly as it does now.
Surfaced to Lua as `self.physics:resolve(pos, radius, height, stepHeight, groundDistance)`
with the extra arguments optional.

**2 — A Player preset.** One tick in the Scripts panel builds it: an **Empty** named Player,
Box collider sized to the controller, tag `Player`, and FPS Controller + Weapons System +
Inventory attached together. That is the object the request describes, without assembling it
by hand each time.

**3 — Serialized script fields (the Inspector's real job).** Per-entity overrides stored on
`SceneEntity`, serialized, applied to the script instance at Play start, and editable in
**edit mode** — where the change is written to the entity, not thrown away. This is the P1
"Serialized script fields" item already in §5; the request is the reason to do it now.

**4 — Terrain collider, made obvious.** The mechanism exists; what is missing is that nothing
tells you to tick it. The Terrain section gets the Solid checkbox inline with a one-line note,
and the Player preset warns when no collider-enabled ground exists in the scene.

### 5d.4 The call worth overruling

**`stepHeight` and `groundProbeDistance` default to 0**, i.e. every existing script keeps
today's behaviour until it opts in, and `fps_controller.lua` opts in with Unity-like values
(0.3 m step, 0.1 m probe). The alternative is making step-up the default for everyone, which
is friendlier but silently changes how every existing scene's movement feels.

### 5d.5 Honest limit

Step-up on an **AABB** is not a capsule sweep. It will climb stairs and curbs reliably and
will not climb walls, but on a steep ramp an AABB still behaves like a box on a slope — it
steps up in discrete jumps rather than sliding smoothly. A true slope limit needs the surface
normal, which `resolveBoxCollision` does not currently return. I would add the normal in the
same pass if you want slope handling too; without it, "walkable slope angle" is not something
I can honestly claim.

### 5d.6 Files likely to change

`Collision.hpp/.cpp` (step sweep, ground probe, optionally the contact normal) ·
`ScriptRuntime.cpp` (`resolve` extra args) · `fps_controller.lua` +
`third_person_controller.lua` (opt in) · `EditorScene.hpp` + `SceneSerializer.cpp` (script
field overrides) · `Editor/src/main.cpp` (Inspector edit-mode writes; Terrain Solid note) ·
`Editor/src/ScriptsPanel.cpp` (Player preset).

### 5d.7 Validation

New tests: a box walks up a `stepHeight` stair and does **not** walk up one a millimetre
taller · step-up is refused when nothing is under the raised position (no wall-climbing) ·
`grounded` honours the probe distance · zero-defaults reproduce current behaviour exactly
(regression guard for every existing scene) · script-field overrides round-trip and reach the
running instance. Then driven live: build the Player preset on an Empty, walk a staircase
without jumping, and change a script value in the Inspector **while not in Play** and see it
persist.

---

## 6. BLOCKED

Nothing is blocked on an external dependency.

**Process note, not a blocker:** `docs/CLAUDE.md` mandates approval before each change phase
and `PROJECT_PLAN.md` as source of truth. Work through `902310b` was done under direct user
instruction (which that document's own §23 ranks above itself) without this file existing.
This file now exists; from here, each phase stops for approval.

---

## 7. RISKS

| Risk | Mitigation |
|---|---|
| **`main.cpp` is ~9,000 lines, single TU, no forward declarations** — a helper must be defined textually above its call site | Extract pure helpers into Engine as they are touched. Do not attempt a big-bang split. |
| **`ctest` can report green against a stale binary** — `editor-debug` does not build `GameForgerTests` (audit 5.7) | Always `--target GameForgerTests` before believing a test result. Hit this live: 3 new tests reported the old count of 29 until the target was built by name. |
| **imgui-node-editor is a real third-party dependency** | Mind Graph Phase 0 does nothing else; `feedback_imguizmo_tags` records this exact class of gotcha |
| **Editor/Runtime seam** — 4 defects so far, all "real in Editor, stubbed in Runtime" | Every new host-facing feature ships with a parity test or lives in Engine called by both |
| **MSVC env absent from this shell**; linking fails while the exe runs | Build through `vcvars64.bat`; `taskkill` before linking |

---

## 8. VALIDATION

Standing bar for every phase:

1. `editor-debug`, `runtime-debug`, `all-release` — all build clean, no new warnings.
2. Full CTest **with `GameForgerTests` freshly built by name**.
3. `GameForgerEditor.exe` launches with empty stderr (GLSL compile/link failures print there).
4. Anything host-facing is verified **by running `GameForgerRuntime.exe`**, not by reading the
   Editor's code. Every §1b defect was found that way and none were found any other way.
5. No frame-time regression on the reference scene vs the Phase 0 baseline.

**Last full run (2026-09-11, script-UI consolidation):** Debug + all-release clean with no new
warnings · **40/40 tests** · editor launches with empty stderr · FPS demo, lens layers **and the
full HUD** verified live in the standalone Runtime. The parity test was additionally verified by
reintroducing the real 1b.3 defect and confirming it fails — a test nobody has seen fail is not
yet a test.

**Toolchain note:** the MSVC environment now comes from
`C:\Program Files\Microsoft Visual Studio8\Community\Common7\Toolssdevcmd.bat`
(`-arch=x64 -host_arch=x64`). VS 2022 is gone from this machine, and the `vcvars64.bat` shim
beside it exits 1 with "The filename, directory name, or volume label syntax is incorrect" —
calling VsDevCmd directly works. A build that reports missing `<algorithm>` means the env step
silently failed, not that the code is broken.

---

## 9. Next action

§1b closed, layout fixed, Mind Graph 0–3 done, and scripting now lives in its own panel with
the Inspector's old modal path deleted rather than merely bypassed. **Mind Graph Phase 4 is
next** — the Trigger Zone entity trait plus enter/exit in `GameplayLoop`, which must land in
Engine so both hosts get it by construction; that is the §1b lesson applied before the fact
rather than after.
