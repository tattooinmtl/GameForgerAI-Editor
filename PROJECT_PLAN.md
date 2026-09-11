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
| `63f1dc5` | **Mind Graph Phase 0.** Pinned imgui-node-editor to commit `021aa0ea`, not the v0.9.3 tag — the tag fails to compile against ImGui 1.92.3 (it redefines `ImVec2` operators 1.92 now provides). `GIT_SHALLOW FALSE`, same reason ImGuizmo documents. Repo ships no CMakeLists, so its four sources build as `GameForgerNodeEditor`. Canvas renders with draggable links. | Debug + all-release clean; 34/34; canvas verified in the editor |
| `1acc01b` | **§1b.4 HUD — the last one.** Moved HUD drawing out of the Editor's ImGui path into `ViewportRenderer`, which both hosts share. New `OverlayFont` in Engine (extracted, not copied — GameMenu and TextMesh each already had one). Two bugs only visible by running it: the UI host was `followedEntity` (the *player*, not a camera), and text was centred on its anchor so top-left labels spilled off-screen. `UIElementData::fontPath` now actually works. | 3 configs; 34/34; editor clean; **crosshair + both text lines verified in a standalone Runtime run** |

**Also shipped:** 37 GLB models imported to `Game/Models/kit/`; four reusable script presets
(Weapons System, Door, Keypad Panel, Key Item) registered in the Add Script dropdown; the
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
| **P1** | **Mind Graph Phase 0.5 → 1 → 2** | Next. 0.5 is the 30-minute literal-refresh spike; 1 and 2 are the graph model and compiler, both fully testable with no UI. |
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

**Last full run (2026-09-11, `63f1dc5`):** 3 configs clean · **34/34 tests** · editor launches
with empty stderr · FPS demo, lens layers **and the full HUD** verified live in the standalone
Runtime. The parity test was additionally verified by reintroducing the real 1b.3 defect and
confirming it fails — a test nobody has seen fail is not yet a test.

---

## 9. Next action

§1b is closed. Proceeding through the approved run: **Mind Graph Phase 0** (pin the
dependency), then 0.5 (literal-refresh spike), then 1–2 (model + compiler, both fully
testable with no UI), then the panel.
