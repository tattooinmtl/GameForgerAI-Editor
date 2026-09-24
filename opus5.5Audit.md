# Opus 5.5 Audit — GameForgerAI Editor (Alpha 0.79)

**Date:** 2026-09-23 · **Branch:** `TattooAI/app-audit-workflow-reorg-cca6a1` · **Scope:** the whole app — `Editor/`, `Engine/`, `Runtime/`, `Game/`, build files.
**Method:** I read `Editor/src/main.cpp` from top to bottom (7,290 lines, where almost all of the UI lives) and spot-checked the engine, runtime, serializer, script runtime, AI client, scripts, assets and build files with grep.
**Nothing was built or run in this audit.** Every finding below comes from reading the code, with a `file:line` reference. Findings marked **CONFIRMED** follow directly from the code as written. Findings marked **LIKELY** should be checked live in Phase 0.

**Ground rule for the whole plan (your instruction): _we keep everything._** No feature, panel, file, script or menu item is deleted. Things only get **moved, regrouped, renamed, hidden behind a toggle, or fixed**. Where something looks like dead weight, it goes into §4 "Candidates for later removal" for **you** to decide. This plan does not act on that list.

---

## 1. Summary

The editor has a lot of features, but they were added one request at a time and each one landed wherever it was easiest to put. The result:

- **No default layout.** There is no `DockBuilder` code. On a first launch, or after *Edit > Reset Editor Layout*, all 11 panels open as floating windows stacked on top of each other (`main.cpp:7185-7194`).
- **You can't reopen a panel.** None of the main panels has a close button or a *Window* menu entry. Once one is docked somewhere odd, the only way back is Reset Layout, which makes things worse (see above).
- **Creating things happens in 4 different places:** the *GameObject* menu (primitives), the *Character* menu (model import), the *Toolbox* panel (Text Mesh / Cine Camera / Terrain) and the Hierarchy's *Add Child* right-click menu.
- **AI features are spread over 4 places:** the *AI Forge* panel, the *AI Animation* section at the top of the Animation panel, *Generate with AI* inside the Add Script window, and *Settings > AI Setup*.
- **The Inspector is one flat list of about 15 sections** with no collapsing. Castle, Catapult, Pickup Item and Cine Camera appear on every object, even a plain cube.
- **Real bugs**, including one that undoes two steps per Ctrl+Z, one that can overwrite the wrong scene file, and one that breaks parenting when you rename a parent (see §2).

---

## 2. Findings — bugs and things that don't work

Severity: 🔴 data loss / wrong result · 🟠 feature broken or misleading · 🟡 annoyance / performance

### 2.1 Editor core

| # | Sev | Finding | Where | Status |
|---|---|---|---|---|
| B1 | 🔴 | **Keyboard shortcuts run twice.** Ctrl+Z/Y are handled in `drawMainMenu` and again in `drawEditorPanels` on the same frame. Result: Ctrl+Z undoes 2 steps, Ctrl+Y redoes 2, and Ctrl+Shift+Z redoes and then undoes (net: nothing happens). | `main.cpp:1441-1468` and `main.cpp:6440-6450` | **FIXED 0.83** |
| B2 | 🔴 | **New Scene keeps the old file path.** `newScene()` clears the entities but not `currentScenePath`. Ctrl+S right after "New Scene" overwrites the scene you had open before (default `Castle.gfprod`) with an empty scene. There is also no "unsaved changes?" prompt on New/Open/Quit. | `main.cpp:1421-1428`, `main.cpp:7168` | **FIXED 0.83** |
| B3 | 🔴 | **Renaming a parent breaks its children.** Parent links are stored by name (`parentName`). `RenameEntityCommand` renames only the entity itself, so every child is orphaned: it jumps to the top of the Hierarchy and stops following. The same happens to Catapult `yawEntityName`/`armEntityName` references. | `EditorScene.cpp:53-70` | **FIXED 0.80** |
| B4 | 🔴 | **The script editor cuts off long scripts.** The Edit Script buffer is a fixed 16 KB and the AI preview is 8 KB. A longer `.lua` is silently truncated when it loads, and pressing **Save** writes the truncated text back to disk. None of today's scripts is that big, but the next long one will be damaged. | `main.cpp:557`, `main.cpp:548`, `main.cpp:2228`, `main.cpp:4956` | **FIXED 0.83** |
| B5 | 🟠 | **Multi-select delete and duplicate only act on one object.** You can select many objects (Ctrl/Shift-click), but `deleteSelected` and `duplicateSelected` only use the primary one. | `main.cpp:750`, `main.cpp:1345` | **FIXED 0.83** |
| B6 | 🟠 | **Many Inspector edits can't be undone.** Undo only records changes that go through the command bus. These edit the entity directly and skip it: the Active checkbox, tag Up/Down, Mask RGB, UV Scale, the 3 material layers, terrain World Size, terrain textures, Import Heightmap, Perlin Generate, sculpt/paint, Animate Object, Loop, and keyframe add/delete. Those edits are also left out of undo snapshots. | `main.cpp:3751, 4038, 4065, 4432-4486, 4724, 4803-4865, 5106, 5741, 5798, 5976, 6198-6303` | CONFIRMED |
| B7 | 🟡 | **Undo only goes back 5 steps**, and each step is a full copy of the scene. | `main.cpp:322` | CONFIRMED |
| B8 | 🟠 | **Script properties (`-- @property`) edited in the Inspector outside Play mode are thrown away.** The field shows the default and ignores the edit, because there's nowhere per-entity to store it. The same `.lua` file is also re-read from disk every frame, for every attached script. | `main.cpp:4980-5031` | CONFIRMED |
| B9 | 🟠 | **Clicking in the Viewport can select hidden (inactive) objects.** `pickEntity` has no `active` check, while the renderer skips inactive objects. | `main.cpp:5601-5652` vs `ViewportRenderer.cpp:887` | **FIXED 0.80** |
| B10 | 🟡 | **Viewport clicks on an imported model re-load the whole model file from disk** (Assimp) for every imported model in the scene, on every click. | `main.cpp:5586-5590` | CONFIRMED |
| B11 | 🟡 | **Hidden panels still render.** No panel checks the return value of `ImGui::Begin`, so the Game view and Cine Camera Preview redraw the whole scene every frame even when their tab is hidden. That's up to 3 full scene renders per frame. | `main.cpp:2445`, `3146`, `6704` | CONFIRMED |
| B12 | 🟡 | **Icons that fail to load stay missing until restart.** `ensureIconTextureGpu` caches a failed load as texture `0`, so it never retries. | `main.cpp:1317-1343` | CONFIRMED |
| B13 | 🟡 | **Ctrl+R also saves** (a second, non-standard save shortcut), and a code comment calls it the main Save key while the menu shows Ctrl+S. | `main.cpp:6451`, `main.cpp:7166` | **FIXED 0.84** |
| B14 | 🟡 | **Storyboard shots aren't saved.** They live only in memory and are gone when you close the editor. | `main.cpp:3288-3292` | CONFIRMED |
| B15 | 🟡 | **The Pickup "Item Name" field sends a command on every keystroke**, so typing a name floods the undo history (partly hidden by the 0.6 s grouping of rapid changes). | `main.cpp:4583` | CONFIRMED |
| B16 | 🟡 | **Play mode leaves steps in the undo history.** After Stop, the first Ctrl+Z restores the snapshot taken before Play, which looks like nothing happened. | `main.cpp:7130-7142`, Stop at `1786-1797` | LIKELY |

### 2.2 Settings / AI

| # | Sev | Finding | Where | Status |
|---|---|---|---|---|
| A1 | 🟠 | **The Settings > AI Setup "Endpoint" and "Model" boxes do nothing.** `AIProviderClient` reads only `Game/AI/Providers.json`, so anything typed there is ignored. | `main.cpp:1867-1868`, `AIProviderClient.cpp:138-159` | **FIXED 0.83** |
| A2 | 🟠 | **"Calibrate / Test provider" freezes the editor.** It runs on the UI thread, and the timeout is 120 s. | `main.cpp:1876-1907`, `Providers.json timeoutSeconds` | **FIXED 0.83** |
| A3 | 🟡 | **The provider list is written twice:** hard-coded in `main.cpp` and again in `Providers.json`. The JSON's `"activeProvider"` is ignored; the UI always starts on entry 0. | `main.cpp:629-641` | **FIXED 0.84** |
| A4 | 🟡 | **Theme and Language choices are not saved** and reset every launch. | `main.cpp:598-607`, `7003-7004` | CONFIRMED |
| A5 | 🟡 | **Placeholder settings pages:** Settings > *Project* ("more settings will land here…") and *Language* (only English works). | `main.cpp:1927-1935`, `2032-2048` | CONFIRMED (placeholder) |
| A6 | 🟡 | **`RequestAnimationCommand` is a dead command type.** It always returns "Animation execution is not connected yet." | `EditorScene.cpp:~646`, `AICommand.hpp:52` | CONFIRMED |

### 2.3 Gameplay / Play mode

| # | Sev | Finding | Where | Status |
|---|---|---|---|---|
| G1 | 🔴 | **Missing script:** the *Catapult Controller* preset attaches `Game/Scripts/catapult_controller.lua`, which **isn't in the repo**. The Game view's aiming-line overlay also looks for it. | `main.cpp:5239-5246`, `main.cpp:2855` | **FIXED 0.84 (preset greyed out; script not written)** |
| G2 | 🟠 | **Editor Play and the built game (Runtime) play differently.** Holding a weapon with F, catapult operation, the inventory window, castle HP bars, the enemy detection icon, projectile visuals and the enemy-catapult auto-fire timer exist **only** in the Editor's Game panel. `GameForgerRuntime.exe` doesn't have them, so a game can behave differently once built. `findPickupCandidate` is also copied into both. | `main.cpp:2583-2914`, `main.cpp:6407-6433`, `Runtime/src/main.cpp:90-94, 459-468` | CONFIRMED |
| G3 | 🟠 | **E-pickup and F-hold both trigger on "Is Pickup Item" objects.** Standing next to a potion shows "[E] Pick up" *and* "[F] Hold" at once, and F will hold the potion like a weapon. | `main.cpp:2589`, `main.cpp:2664` | CONFIRMED |
| G4 | 🟡 | **Game rules are tied to hard-coded script file paths** (`"Game/Scripts/enemy_ai.lua"`, `inventory_system.lua`, `catapult_controller.lua`). Rename or copy a script and the feature silently turns off. | `main.cpp:2654`, `2751`, `2855` | CONFIRMED |
| G5 | 🟡 | **Castle help text refers to "Is Collider"**, but the checkbox is labelled "Solid (blocks scripted physics)". | `main.cpp:4606` vs `4499` | CONFIRMED |
| G6 | 🟡 | **Outdated code comment:** `PlayModeState` says Lua scripts don't run yet. They do. | `main.cpp:407-411` | CONFIRMED |

### 2.4 Build / project hygiene

| # | Sev | Finding | Where | Status |
|---|---|---|---|---|
| H1 | 🟠 | **`Build-Project.cmd` calls `vcvars64.bat`, which fails on this machine** (VS 18). It only builds the Editor, not Runtime or the tests. | `Build-Project.cmd:24` | CONFIRMED (per the toolchain memory) |
| H2 | 🟡 | **Confusing file extensions:** `.gfprod` is a *scene* but the Save dialog calls it "GameForger Project"; `.gfai` is used both for a built scene *and* for save games (`Game/Saves/save.gfai`). | `main.cpp:918-929`, `1504-1532` | CONFIRMED |
| H3 | 🟡 | **Stray files in the repo root:** `test_import.obj` and a `models/` folder that duplicates `Game/Models`. | repo root | CONFIRMED |
| H4 | 🟡 | **One French string in the English UI:** "Viewport OpenGL indisponible." | `main.cpp:6776` | CONFIRMED |

---

### 2.5 Update after the FPS/items work (Alpha 0.80, same day)

- **Fixed:** B3 (rename now carries `parentName`/catapult references over), B8 (per-object script property values are saved with the scene; the `.lua` is only re-parsed when its file time changes), B9 (Viewport picking skips hidden objects). Collisions and projectiles also skip hidden objects now.
- **Partly addressed:** G2. Pickup/drop logic is now shared (`GameplayLoop.cpp`) and the duplicate `findPickupCandidate` in Runtime is gone. Runtime still lacks the grid/hotbar/effects UI.
- **NEW B17 🟠 (FIXED in 0.81 follow-up — `getRight` now `cross(forward, up)`, regression test `testGetRightMatchesScreenRight`) `self.entity:getRight()` was inverted.** The game camera (glm::lookAt looking down +Z) shows +X on screen-LEFT, confirmed from a runtime screenshot of known object positions. `getRight` returns +X, so D strafes left in `fps_controller.lua` and `third_person_controller.lua`. `ScriptRuntime.cpp` `luaEntityGetRight`. Not changed yet (it alters existing scripts' feel); `fps_player.lua` uses its own `(-forward.z, 0, forward.x)`.
- **NEW H5 🔴 (fixed) Fresh configure failed.** `GIT_SHALLOW TRUE` with ImGuizmo's commit-hash `GIT_TAG` can't check out, so every new clone and CI run failed at configure. Removed `GIT_SHALLOW` (`CMakeLists.txt`).

### 2.6 Update after the magic/XP/runtime work (Alpha 0.81)

- **G2 largely addressed:** the gameplay HUD now has one layout (`GameplayHud.cpp`) drawn by both the Editor (ImGui) and GameForgerRuntime (new `RuntimeHud`), so effects, damage numbers, bars, hotbar and messages look the same in both. The Editor-only catapult/castle overlays are still Editor-only.
- **NEW H6 🟠 (fixed) Upside-down images.** `StbImageImpl.cpp` flips every stb load for OpenGL, but `SplashScreen.cpp` and every 2D icon path assumed top-down rows, so all splash screens and inventory/icon-picker icons drew upside down.
- **NEW H7 🟠 (fixed) Blank runtime text.** The bundled `Thuast Demo.otf` is CFF-flavoured, which stb_truetype can't bake, so the runtime pause menu never showed any text. It now falls back to a TrueType font.
- **NEW H8 🟡 (fixed) Stale saves hijacked the startup scene.** A `Game/Saves` save always won over Project.json. It now only resumes when `save.meta.json` names the current startup scene; other saves are ignored, not deleted.

### 2.7 Update 2026-09-24 (Alpha 0.82)

- **B17 fixed** (see its entry above): `getRight()` now points right; regression-tested against the real camera.
- **Demo scripts packaged:** the FPS Demo kit lives in `Game/Scripts/FPSDemo/` + `Game/Icons/FPSDemo/`, with its own `enemy.lua`, and can be copied into any project with File > Import FPS Demo Kit into This Project. The small starter scripts in `Game/Scripts/` are separate (`enemy_ai.lua` restored to its original).
- **Plan status:** Phases 0-8 (§6) are still **not started** - waiting for the user's "start Phase 0". Phase 2's menu work should keep the new File menu item next to Build Game.

### 2.8 Update 2026-09-24 (Alpha 0.83)

- **Fixed:** B4, B1, B2, B5, A1, A2 (the user asked for these six, in that order). Details: `docs/planFix_AGY_CHANGELOG.md` §1f.
- **B1 note:** the single handler stayed in `drawEditorPanels` (no new `handleEditorShortcuts()` function).
- **B2 note:** the editor also starts with no scene file now (it used to point at `Castle.gfprod` while showing an empty scene).
- **Plan status (corrected in §2.9):** B3 and B9 were already fixed in 0.80 (§2.5) - this line wrongly listed them as open.

### 2.9 Update 2026-09-24 (Alpha 0.84)

- **Fixed:** B13 (Ctrl+R no longer saves), A3 (provider list only in `Providers.json`, starts on its `activeProvider`), G1 (Catapult Controller preset greyed out with "file missing" - the script itself is still missing; writing it is the user's call), and the gizmo keys no longer react while Ctrl is held (Ctrl+Y / Ctrl+R used to also switch the gizmo). Details: changelog §1g.
- **Phase 1 status:** all Phase 1 code is done (B1, B2, B3, B4, B5, B9, G1, A1, A2). Waiting for the user's manual checks listed in Phase 1. Phases 0 and 2-8 not started.

### 2.10 Update 2026-09-24 (Alpha 0.85)

- **NEW B18 🔴 (fixed 0.85) Editor Viewport camera inverted:** A/D swapped, middle-mouse/Shift pan inverted on both axes ("right" was screen-left). Now `cameraBasis()`, regression-tested.
- **NEW B19 🟠 (fixed 0.85) Third-person camera pitch inverted:** mouse up looked down (Editor Game view + Runtime). Regression-tested.
- Phase status unchanged: Phase 1 code done, waiting for the user's manual checks; Phases 0 and 2-8 not started.

## 3. Findings — duplication and things that don't make sense

| # | Finding | Where |
|---|---|---|
| D1 | **4 copies of the same "import file into the project" function:** `importModelIntoProject`, `importFontIntoProject`, `importTextureIntoProject`, `importIconIntoProject`. All 4 also silently reuse an existing file with the same name, even when it's a different file. | `main.cpp:1135, 1216, 1250, 1282` |
| D2 | **5 copies of the Windows file-dialog code** (scene save/open, font, image, model). | `main.cpp:914-1130` |
| D3 | **2 texture pickers** (`drawTexturePicker`, `drawIconPicker`) that differ only in which folder they browse. | `main.cpp:4240`, `4321` |
| D4 | **Creation is split over 4 places:** GameObject menu, Character menu, Toolbox panel, Hierarchy *Add Child*. The *Character* menu holds only "Import Model" plus 2 greyed-out placeholders. | `main.cpp:1590-1695`, `3444-3576`, `3809-3862` |
| D5 | **AI is split over 4 places:** AI Forge panel, AI Animation (inside the Animation panel), AI script generation (inside the Add Script window), AI Setup (in Settings). | `6540`, `6037`, `5305`, `1842` |
| D6 | **"Settings" is a bare item on the menu bar** next to real menus, and it mixes *project* settings (AI providers, project) with *editor preferences* (theme, language). | `main.cpp:1697`, `2071` |
| D7 | **The Play / Pause / Step buttons live inside the menu bar code** and contain about 60 lines of play-start logic inline. | `main.cpp:1707-1837` |
| D8 | **Game-specific options appear on every object.** Castle, Catapult and Pickup Item are always shown, and the tag presets (`PlayerCastle`, `EnemyCatapult`…) are hard-coded into the general Inspector. | `main.cpp:4100-4102`, `4567-4701` |
| D9 | **The same thing is done 3 ways:** *Delete* exists in the Edit menu, the Hierarchy right-click menu and the Inspector's "Delete Entity" button. *Active* toggles exist in the Hierarchy and the Inspector. (Having more than one is fine; the problem is that they behave differently, e.g. the Hierarchy delete handles selection differently.) | `1574`, `3864`, `5121` |
| D10 | **A second, unused engine architecture.** `Core/GameObject`, `Component`, `AssetDatabase` and `Material` are compiled and unit-tested, but the Editor and Runtime never use them. Everything still runs on the monolithic `SceneEntity`. AGENTS.md and the changelog describe this work as "complete". | `Engine/src/Core/*`, `Engine/tests/TestMain.cpp:306` |
| D11 | **17 scripts in `Game/Scripts/`, but only 7 are used** by a scene or preset. There are 4 different FPS controllers (`fps_controller.lua`, `FPSController.lua`, `PlayerFPS.lua`, `player_fps.lua`) and 2 player controllers. | `Game/Scripts/` |
| D12 | **Dead helpers:** `yawForward()` is defined and never called. | `main.cpp:2244` |
| D13 | **Everything is in one 7,290-line `main.cpp`,** with no forward declarations. Your memory notes already record 2 compile errors caused by "helper is defined below the caller". | `Editor/src/main.cpp` |

---

## 4. Candidates for later removal (NOT touched by this plan — your call)

Kept on purpose, per your instruction. Listed so you can decide later:
`yawForward()` · `RequestAnimationCommand` · the 10 unreferenced scripts in D11 · the greyed-out *Map Humanoid Skeleton…* / *Animation Library…* menu items · the Language placeholder options · `test_import.obj` and root `models/` · the unused `Core/GameObject`/`Component`/`AssetDatabase`/`Material` layer (or: wire it in; see the RoadMap) · the Ctrl+R save shortcut.

---

## 5. Target workflow — panels grouped by job

**Principle:** each area of the screen has one job, and every panel for that job is a **tab in the same dock area**. You always know where to look: things you *build with* on the left, things you *look through* in the centre, things you *tweak* on the right, and things that are *time-based or output* at the bottom.

```
┌──────────────────────────── Menu: File  Edit  Create  Window  Help ───────────────────────────┐
│                          [▶ Play] [⏸ Pause] [⏭ Step]   (toolbar row)                         │
├───────────────┬───────────────────────────────────────────────┬───────────────────────────────┤
│ ① SCENE       │ ② VIEWS                                        │ ③ PROPERTIES                  │
│ [Hierarchy]   │ [Scene View] [Game] [Cine Preview]             │ [Inspector] [AI Forge]        │
│ [Create]      │                                                │                               │
│               │                                                │                               │
├───────────────┴───────────────────────────────────────────────┴───────────────────────────────┤
│ ④ ASSETS & OUTPUT          [Project] [Console]   │ ⑤ TIME     [Animation] [Storyboard]         │
└──────────────────────────────────────────────────┴───────────────────────────────────────────────┘
   Floating (opened on demand): Settings, Inventory (Play only), popups (Rename, Add Script, Edit Script, Text Mesh)
```

| Group | Dock area | Tabs (today's name → new name) | Why it belongs here |
|---|---|---|---|
| ① **Scene** | left | Hierarchy · Toolbox → **Create** | What's in the scene and how to add more |
| ② **Views** | centre | Viewport → **Scene View** · Game · Cine Camera Preview → **Cine Preview** | Every 3D view in one place, one click apart |
| ③ **Properties** | right | Inspector · AI Forge | Both act on the current selection or scene |
| ④ **Assets & Output** | bottom-left | Project · Console | Files in, messages out (Unity convention) |
| ⑤ **Time** | bottom-right | Animation · Storyboard | Both are timeline/keyframe work, and they depend on each other already (Storyboard needs a path recorded in Animation) |

**The new *Window* menu** lists every panel under the same 5 group headings, with a check mark if it's open, plus *Reset Layout* (which now rebuilds **this** layout instead of an empty one).

**The new *Create* menu** (replacing *GameObject* + *Character*, with the same items):
`3D Primitives ▸ Cube/Sphere/Cylinder/Cone/Plane/Capsule` · `Import Model…` · `Text Mesh…` · `Terrain` · `Cine Camera` · (greyed) `Map Humanoid Skeleton…`, `Animation Library…`. The **Create panel** (the old Toolbox) shows the same list as buttons. The Hierarchy's *Add Child* keeps its menu and uses the same list.

**Menu bar** (matches RoadMap ticket **T4-1**):
- **File:** New / Open / Save / Save As / — / Build Game… / — / Project Settings…
- **Edit:** Undo / Redo / — / Copy / Paste / Duplicate / Delete / Select All / — / Preferences…
- **Create:** the Create list above
- **Window:** the panel groups above, plus Reset Layout
- **Help:** Keyboard Shortcuts (read-only list of the shortcuts that already exist)
- Play/Pause/Step move to a thin **toolbar row** below the menu.

**Settings split (nothing removed):** *File > Project Settings…* opens the same window on **Project** and **AI Providers**. *Edit > Preferences…* opens it on **Appearance** and **Language**. It's one window with the categories grouped under two headings.

**The Inspector gets collapsible sections in a fixed order** (matches RoadMap tickets **R-34** / **T4-5**):
1. **Header** — Active, Name, Shape, Tags
2. **Transform** — Position/Rotation/Scale, Pivot, Parent + Local transform
3. **Rendering** — Appearance/Material, Text Mesh*, Terrain* (* only when the object is that kind)
4. **Physics** — Collider
5. **Behaviour** — Scripts (+ Add Script…), Camera Rig
6. **Gameplay** — Pickup Item, Castle, Catapult
7. **Cinematic** — Cine Camera, Animation

Optional parts (Pickup Item, Castle, Catapult, Cine Camera, Collider, Animation) are **shown only once enabled**, and an **"Add Component ▾"** button at the bottom lists the ones not yet on the object. Nothing is lost: enabling one from the list is the same as today's checkbox.

**AI grouped in one place:** AI Forge gets 3 tabs: **Scene** (today's planner) · **Animation** (the AI Animation section moved from the Animation panel) · **Scripts** (a shortcut that opens the existing Add Script → Generate flow for the selected object). The Animation panel keeps a small "✨ Generate with AI → AI Forge" button, so the old path still works.

---

## 6. Build plan — phase by phase

Every phase ends with a **verification gate**: build, tests, and a short manual checklist you run. **We don't start the next phase until you say that phase passes.** Each phase is one commit (or a small group of commits) on this branch, so any phase can be reverted on its own.

**Standard gate (every phase):**
```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\vsdevcmd.bat" -arch=x64 -host_arch=x64 -no_logo
cmake --preset windows-x64
cmake --build --preset editor-debug
cmake --build --preset runtime-debug
cmake --build --preset all-release
out\build\windows-x64\Engine\Debug\GameForgerTests.exe
```
✅ = both configs build with zero new warnings, all tests pass, the editor launches, the window title shows the version, and `Game/Scenes/Castle.gfprod` opens.

Following the repo's rules (AGENTS.md §3), each phase also gets a `docs/phases/` log entry, a `planFix_AGY_CHANGELOG.md` line and a version bump (0.80, 0.81, …).

---

### Phase 0 — Baseline & safety net *(no behaviour change)*

**Goal:** a known-good starting point to compare every later phase against.

**How:**
1. Fix `Build-Project.cmd` (H1): call `vsdevcmd.bat` (found via vswhere), guard with `if not defined INCLUDE`, and build Editor + Runtime + tests.
2. Run the standard gate and record the test count and warning count in `docs/phases/phase-ui.0.md`.
3. Back up the current `GameForgerEditorLayout.ini` to `GameForgerEditorLayout.ini.pre-reorg` (your own layout stays restorable).
4. Take window-only screenshots (the `PrintWindow` method from memory) of the current layout, for a before/after comparison.
5. Add a small ms/frame readout to the Viewport header, and record the frame time with the Game tab visible and hidden.
6. Check the 1 LIKELY finding (B16) live, and mark it CONFIRMED or dropped.

**Verify (you):** `Build-Project.cmd` runs to the end on a double-click · the baseline numbers are written down · the screenshots are in the phase log.

---

### Phase 1 — Critical bug fixes *(no panels move yet)*

**Goal:** fix what can lose data or does the wrong thing, before any UI reshuffle, so the fixes are testable on their own.

| Fix | How | Regression test (added to `Engine/tests/TestMain.cpp` where the logic is engine-side) |
|---|---|---|
| B1 double shortcuts | Handle shortcuts in **one** place: a new `handleEditorShortcuts()` called once per frame. Remove the duplicate Ctrl+Z/Y branch from `drawMainMenu` (the menu *items* stay). | Manual: 3 moves, then Ctrl+Z ×1 → exactly 1 move undone |
| B2 New Scene path | `newScene()` sets `currentScenePath` to empty. Save with an empty path turns into *Save As…*. Add a dirty flag and an "Unsaved changes — Save / Don't Save / Cancel" popup on New/Open/Close. | Manual: open Castle, New, Ctrl+S → Save-As dialog appears, Castle.gfprod unchanged |
| B3 rename orphans | `RenameEntityCommand` also rewrites every `parentName` / `catapult.yawEntityName` / `armEntityName` that matched the old name. | Engine test: parent + child, rename parent, child's `parentName` follows |
| B4 script truncation | Script editor and AI preview use a growing `std::string` buffer (`ImGuiInputTextFlags_CallbackResize`) instead of fixed arrays. | Engine/manual: open a 40 KB `.lua`, Save, file size unchanged |
| B5 multi-delete/duplicate | `deleteSelected`/`duplicateSelected` loop over `multiSelectedIds` (one undo step for all of them). | Manual: select 3, Delete → 3 gone, Ctrl+Z → 3 back |
| B9 picking hidden objects | `pickEntity` skips `!entity.active`. | Manual |
| G1 missing script | Keep the preset. Show "⚠ file missing" and disable it if the file isn't on disk. (Restoring or writing `catapult_controller.lua` is a separate decision for you.) | Manual |
| A1 dead Endpoint/Model | Show the real values from `Providers.json`, read-only, plus an "Open Providers.json" button. (Making them editable = writing JSON back; that's Phase 5 if you want it.) | Manual |
| A2 frozen Test button | Run the probe on a worker thread, the same pattern AI Forge already uses (`main.cpp:6600`). | Manual: click Test with the network off → editor stays responsive |

**Verify (you):** the standard gate · the 9 manual checks above · open Castle.gfprod, play and stop, save as a new file, reopen, and it matches.

---

### Phase 2 — Menus, toolbar, Window menu, closable panels

**Goal:** you can reach every panel, and the menus are organised by job (§5).

**How:**
1. Add a `PanelVisibility` struct (one `bool` per panel). Every `ImGui::Begin("X")` becomes `Begin("X", &vis.x)`, so each panel gets a close button.
2. Visibility is saved to `Game/EditorPrefs.json` (new file), together with the theme and language, which also fixes A4.
3. New **Window** menu grouped as in §5.
4. **GameObject + Character → Create** (same items). **Settings** moves to *File > Project Settings…* and *Edit > Preferences…* (same window, D6).
5. Play/Pause/Step move from the menu bar into a toolbar row (`drawPlayToolbar()`), code moved unchanged (D7).
6. New **Help > Keyboard Shortcuts** (read-only table).

**Verify (you):** close every panel with its X, then reopen each one from *Window* · restart the editor and panel visibility plus the theme are remembered · every old menu action is still reachable (a checklist of all 20 of today's menu items goes in the phase log).

---

### Phase 3 — Default grouped dock layout

**Goal:** the §5 layout appears on first launch and on *Reset Layout*.

**How:**
1. New `buildDefaultLayout(dockspaceId)` using `ImGui::DockBuilder*`: split left 18% / right 24% / bottom 28%, then split the bottom 50/50, then dock each panel into its group, in tab order.
2. Rename window titles and keep the old ones as stable IDs: `"Scene View###Viewport"`, `"Create###Toolbox"`, `"Cine Preview###Cine Camera Preview"`. Your saved layout keeps working, because ImGui matches on the part after `###`.
3. Store a `layoutVersion` in `EditorPrefs.json`. If it's missing or older, build the default layout once. Your `.ini` backup from Phase 0 lets you get your old layout back.
4. *Reset Layout* calls `buildDefaultLayout` (today it loads an empty ini, which makes the floating pile).
5. Fix B11 while here: skip rendering when `Begin` returns false (hidden tab or collapsed).

**Verify (you):** delete `GameForgerEditorLayout.ini`, launch, and the 5 groups appear as drawn in §5 · drag things around, then *Window > Reset Layout* puts everything back · the Game tab hidden behind Scene View no longer costs frame time (compare against the Phase 0 frame-time baseline; there's no profiler yet, so Phase 0 adds a simple ms/frame readout to the Viewport header) · the before/after screenshot goes in the phase log.

---

### Phase 4 — Inspector regrouping + Add Component

**Goal:** the Inspector reads top-to-bottom in the §5 order, and optional parts hide until used.

**How:**
1. Split `drawInspector` (about 1,550 lines, `main.cpp:3968-5526`) into one function per section: `inspectorHeader`, `inspectorTransform`, `inspectorRendering`, `inspectorPhysics`, `inspectorBehaviour`, `inspectorGameplay`, `inspectorCinematic`. The code moves as-is, with no logic change.
2. Wrap each in `CollapsingHeader` (default open for Header/Transform, and remember open/closed per section).
3. Optional parts show only when enabled. An **Add Component ▾** combo lists the disabled ones, and picking one sends the *same* `SetPropertyCommand{…,"enabled",true}` today's checkbox sends. Each shown part gets a "⋮ Remove" that sends `enabled=false` (same as unticking today).
4. Fix G5 (help text wording) and B15 (commit Item Name on deactivate, not per keystroke).

**Verify (you):** a plain cube shows only Header / Transform / Rendering / Physics / Behaviour · Add Component → Castle shows the Castle block with HP fields · open `Castle.gfprod` and every castle/catapult object still shows its settings · the old checkboxes' behaviour is the same (save, reload, identical JSON diff).

---

### Phase 5 — One place to Create, one place for AI

**Goal:** close out D4 and D5.

**How:**
1. `Create` panel (the old Toolbox) and `Create` menu are both built from **one** shared list: `struct CreateAction{label, group, fn}`. The Hierarchy *Add Child* reuses the primitive/model subset. Nothing is dropped: Text Mesh, Cine Camera and Terrain keep their exact current behaviour.
2. AI Forge becomes tabbed: **Scene** (today's) · **Animation** (move `drawAiAnimationSection` here; the Animation panel keeps a link button) · **Scripts** (opens the existing Add Script → Generate for the selection).
3. Optional (your call): make AI Setup's Endpoint/Model editable by writing `Providers.json` atomically (tmp + `.bak` + rename, the same as `updateProjectStartupScene`), and respect its `activeProvider` (A3).

**Verify (you):** every creation path from the Phase 0 checklist still creates the same thing · generate an animation from AI Forge › Animation and from the Animation panel's link, same result · AI script generation still reachable from Inspector › Add Script.

---

### Phase 6 — Clean-ups that don't change behaviour

**Goal:** remove the copy-paste (D1–D3, D13) and the smaller issues, with **no visible change**.

**How:**
1. `importAssetIntoProject(src, subfolder)` replaces the 4 import copies. It also fixes the silent same-name collision by adding `_2`, `_3` unless the files are byte-identical.
2. `showFileDialog(kind, …)` replaces the 5 dialog copies.
3. One `drawAssetPicker(roots, exts)` replaces the texture and icon pickers (D3).
4. Split `main.cpp` into files by panel group, matching §5: `ui/ScenePanels.cpp` (Hierarchy, Create) · `ui/ViewPanels.cpp` (Scene View, Game, Cine) · `ui/Inspector.cpp` · `ui/AssetPanels.cpp` (Project, Console) · `ui/TimePanels.cpp` (Animation, Storyboard) · `ui/AIForge.cpp` · `ui/Menus.cpp` · `EditorState.hpp`. This ends the "helper defined below its caller" compile errors.
5. Remaining 🟡 items: B6 (route direct edits through `SetPropertyCommand`, or take an undo snapshot around them) · B7 (undo depth 5 → 50) · B8 (cache `parseScriptProperties` per path + file mtime, and store per-entity overrides in the scene) · B10 (cache model bounds) · B12 (retry failed icons) · B14 (save Storyboard shots with the scene) · H4 (translate the string).

**Verify (you):** the standard gate · a full click-through of the Phase 0 checklist, where everything behaves exactly as before · the `.gfprod` saved before and after this phase is byte-identical for the same scene (except B14's new `storyboard` block).

---

### Phase 7 — Editor ↔ Runtime play parity

**Goal:** the game plays the same in Editor Play and in `GameForgerRuntime.exe` (G2–G4).

**How:**
1. Move the Game-view gameplay code (F-hold, pickup, enemy-catapult timer) into `Engine/src/Runtime/GameplayLoop.cpp`, and the overlays (detection icon, projectile dots, HP bars, aiming line, hints) into a shared `GameplayOverlay` that both Editor and Runtime call. Nothing is removed from the Editor. It just calls the shared version.
2. Delete the copied `findPickupCandidate` in `Runtime/src/main.cpp`, since both will use the one engine copy. (That's code de-duplication, not feature removal.)
3. G3: F-hold only for objects tagged `Weapon`, or with a new "Holdable" flag in Pickup Item. **Your choice** — until you decide, the current behaviour is kept.
4. G4: detect features by component/flag, not by hard-coded script path (e.g. an `@feature inventory` annotation). Keep the path check as a fallback so existing scenes still work.

**Verify (you):** Build Game on Castle, run `GameForgerRuntime.exe`, and it shows the same HP bars, detection icon, projectiles and F-hold as Editor Play · same pickup/inventory behaviour in both.

---

### Phase 8 — Docs & hand-off

Update `README.md` (new layout, menus and shortcuts), AGENTS.md (point to the new `ui/` files), the RoadMap progress table (T4-1, R-34, T4-5 partial, R-31 partial), and `planFix_AGY_CHANGELOG.md`. Take final before/after screenshots. Then go back to §4 with you and decide what, if anything, gets removed.

---

## 7. Phase summary

| Phase | What you'll see | Risk | Rough size |
|---|---|---|---|
| 0 | Nothing visible (build script works) | none | small |
| 1 | Undo works right, safe New Scene, rename keeps children, multi-delete | low | medium |
| 2 | Window menu, closable panels, Create menu, toolbar, prefs remembered | low | medium |
| 3 | **The grouped layout** (the main visual change) | low–medium (ini migration) | small–medium |
| 4 | Tidy collapsible Inspector + Add Component | medium (biggest code move) | medium–large |
| 5 | One Create list, AI in one panel | low | medium |
| 6 | Nothing visible (code split, dedupe, small fixes) | medium (big file split) | large |
| 7 | Built game matches Editor Play | medium | large |
| 8 | Docs | none | small |

**Suggested stopping point if you want the visible win fast:** Phases 0 → 1 → 2 → 3 give you the grouped layout with the dangerous bugs fixed. Phases 4–8 can follow at your pace.

---

## 8. Decisions I need from you before the matching phase starts

1. **Phase 1 / G1:** is `catapult_controller.lua` somewhere else (another branch or folder), or should I write a new one from the preset's description?
2. **Phase 5:** should AI Setup's Endpoint/Model become editable (writes `Providers.json`), or stay read-only?
3. **Phase 7 / G3:** what makes an object *holdable* with F — a `Weapon` tag, or a new "Holdable" checkbox?
4. **Layout:** are you happy with the 5 groups in §5, or do you want AI Forge as its own dock area instead of a tab next to the Inspector?
