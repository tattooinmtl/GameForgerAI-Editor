# GameForgerAI — Session Changelog & Progress Tracker

**Document:** `planFix_AGY_CHANGELOG.md` (name kept for continuity; content now tracks the active roadmap below)  
**Master Plan Reference:** [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) (active) — supersedes [`planFix_AGY.v1.2026-08-18.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY.v1.2026-08-18.md) (archived, kept for history) as of 2026-08-21.  
**Comparative Audit Reference:** [`Compared.md`](file:///C:/GameForgerAI-Editor/docs/Compared.md)  
**Agent Operating Guide:** [`AGENTS.md`](file:///C:/GameForgerAI-Editor/AGENTS.md)  
**Last Updated:** 2026-08-21 (Session: Gauntlet Audit Remediation, Claude + Gemini split)

---

## 1. Session Log: 2026-08-18 (Fix Plan & Unity 3D Parity Synthesis)

### Major Actions & Artifacts Created

1. **Comprehensive Comparative Audit Created ([`Compared.md`](file:///C:/GameForgerAI-Editor/docs/Compared.md))**
   - Conducted deep-dive comparative audit against Unity 3D LTS / Unity 6 standards across 20 engine and editor domains.
   - Established the Master Parity Gap Inventory and prioritized roadmap.

2. **Creation of the Master Fix & Parity Plan ([`planFix_AGY.v1.2026-08-18.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY.v1.2026-08-18.md), archived 2026-08-21 — see [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) for the active version))**
   - Unified all 47 defect fixes from `audit-2026-08-13.md` and `PlanFixAuditResults.md` with Unity 3D parity systems.
   - Structured into 6 prioritized tiers and 5 delivery milestones.

3. **Creation of Agent Operational Manual ([`AGENTS.md`](file:///C:/GameForgerAI-Editor/AGENTS.md))**
   - Defined Agent Execution Loop, C++20 conventions, and mapped tasks to 117+ specialized agent skills in `C:/.skills/skills/`.

4. **Implementation & Verification of Quick Wins (M0.79 Completed 100%):**
   - **Q-1 & Q-2 (`PrimitiveMeshes.cpp`):** Fixed capsule bottom hemisphere inverted normal bug and corrected CCW winding order across all primitives. Verified by `testPrimitiveMeshes` with geometric cross-product alignment checks.
   - **Q-8 (`main.cpp`):** Implemented real-time hierarchy search filter (`##HierarchySearch`) with automatic recursive expansion of matching subtrees.
   - **Q-9 (`main.cpp`):** Implemented Viewport Local/Global transform space toggle and grid snapping passed to `ImGuizmo::Manipulate`.
   - **Q-10 (`main.cpp`):** Implemented Pause/Resume and Step Frame toolbar controls in `PlayModeState` and wired into the simulation loop.
   - **Q-11 (`EditorScene.hpp`, `SceneSerializer.cpp`, `ViewportRenderer.cpp`, `GameplayLoop.cpp`, `main.cpp`):** Added `active` boolean flag to `SceneEntity`, serialized in `.scene`, respected in rendering, outlines, scripts, animations, projectile collisions, Hierarchy toggle, and Inspector header.

5. **Implementation & Verification of Tier 1 Milestone Foundations:**
   - **T1-3 (`SceneSerializer.cpp`):** Hardened scene loader with format header verification, empty parent path safety, and atomic Windows file replacement.
   - **T1-4 (`Json.cpp`):** Hardened JSON parser with document completeness validation to reject trailing garbage payloads.
   - **T1-7 (`Engine/tests/TestMain.cpp`, `CMakeLists.txt`, `Engine/CMakeLists.txt`):** Established automated test runner registered with CTest. 100% tests passing (`ctest --test-dir out/build/windows-x64 -C Debug`).

---

## 1a. Session Log: 2026-08-21 (Gauntlet Audit Remediation)

**Source:** [`PROJECT_AUDIT_GAUNTLET_REPORT.md`](file:///C:/GameForgerAI-Editor/docs/PROJECT_AUDIT_GAUNTLET_REPORT.md), an external re-audit run against Alpha 0.78. Findings tracked as DEF-01 through DEF-08, now in [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) Phase 0 (originally logged in `planFix_AGY.v1.2026-08-18.md` §2a, since archived). Work split across two agents this session — task assignments and verification instructions live in [`Gemini_todo_list.md`](file:///C:/GameForgerAI-Editor/docs/Gemini_todo_list.md).

### Claude — completed & verified this session (Alpha 0.79)
- **DEF-01** (`CMakeLists.txt`): upstream ImGuizmo's own `imguizmo` CMake target compiled `GraphEditor.cpp`/`ImCurveEdit.cpp`/`ImGradient.cpp` with no `imgui.h` include path, breaking `cmake --build --preset all-release`. Marked it `EXCLUDE_FROM_ALL` since `GameForgerImGuizmo` (correctly wired, links `GameForgerImGui`) is the target everything actually uses.
- **DEF-02** (`Editor/src/main.cpp`, `updateProjectStartupScene`): replaced direct `std::ofstream(..., ios::trunc)` truncation of `Project.json` with the same atomic tmp-file + `.bak` + rename pattern `SceneSerializer::saveScene` already uses for scene files.
- **DEF-03** (`Engine/src/Editor/ViewportRenderer.cpp`): model (`ensureImportedMeshGpu`), font (`ensureTextMeshGpu`), and terrain/material texture (`ensureTerrainLayerTexturesGpu`) loaders joined `projectRoot` with a scene-authored relative path with no traversal confinement. All three now resolve through the existing `core::resolveProjectFile()` (the same check script paths have used since 0.51), confined to `Game/Models`, `Game/Fonts`, `Game/Textures` respectively. Note: the audit report's citations for this one (`TextMesh.cpp:322`, `TerrainTexture.cpp:142`) pointed at the wrong files — the real join-with-`projectRoot` call sites are in `ViewportRenderer.cpp`; verify citations against current code before trusting them.
- **Verification:** Debug build ✅, Release build ✅, `cmake --build --preset all-release` (previously failing, confirmed via a fresh reconfigure) ✅, full CTest suite (9/9) ✅. Version bumped 0.78 → 0.79 (`CMakeLists.txt`, `README.md` Version History).
- **Still open, owned by Claude** (needs more judgment than a mechanical fix): DEF-05 (JSON parser `+`-prefix / `strtod` end-pointer / UTF-16 surrogate pairs), DEF-07 (Lua memory-budgeted allocator).

### Gemini — assigned this session, not yet started
- DEF-04 ([[nodiscard]] C4834 warnings, ~70+ call sites), DEF-06 (CI Release matrix), DEF-08 (plaintext API key handling). Full instructions, skills to load, and the verification command to run before marking done: [`Gemini_todo_list.md`](file:///C:/GameForgerAI-Editor/docs/Gemini_todo_list.md).
- **Rule for whoever picks these up:** update the checkbox in `Gemini_todo_list.md` AND append a dated entry below this one in this same changelog file AND update the status in `RoadMap2026-08-21.md` — don't consider a DEF-NN ticket closed until all three reflect it.

---

## 1m. Session Log: 2026-09-24 (G7: turned colliders - Alpha 0.89)

The user asked to fix G7 (colliders ignored rotation) first. Every change:
1. **`Engine/include/GameForger/Editor/Transform.hpp`, `Engine/src/Editor/Transform.cpp`**: new `OrientedBox colliderBox(entity)` - the entity's [-1,1] box through `composeEntityTransform` (rotation, scale AND pivot), with `contains(point)` and `worldHalfSize()`; `axisAligned` when not turned.
2. **`Engine/src/Editor/ScriptRuntime.cpp` `resolveBoxCollision`** (`self.physics:resolve` - player, enemies, rigidbodies): unturned colliders keep the old axis-aligned logic (now centered on the pivot-corrected box); turned ones use new `pushOutOfOrientedBox` - separating-axis test (3 world axes, 3 box axes, 9 cross products), push along least penetration; mostly-upward pushes go straight up and set grounded (tilted box = ramp you stand on, no sliding), mostly-sideways pushes stay level (walls never lift or sink the mover); standing over a box's top face prefers its up axis (same idea as the old "fully contained" rule). Quick reject on the box's world bounds.
3. **`Engine/src/Runtime/GameplayLoop.cpp`**: the catapult boulder vs castle hit test uses `colliderBox(...).contains()`.
4. **Pivot** now counts for colliders too (it was ignored - a bottom-pivoted box collided half underground).
5. **Workarounds from 0.87 undone**: the demo "Rock Cliff" is turned 30 deg again (`FpsRigBuilder.cpp`), the climbable.lua warning note is gone, the climbing test's turned wall is solid again and the player stands on top of it. Demo scene regenerated.
6. **Test** `testRotatedColliders`: a 45 deg wall no longer blocks where only its unturned box was, and pushes out along its real face, level; a 20 deg tilted box is a ramp (grounded, pushed straight up); a pivoted box is solid where drawn; an unturned wall blocks exactly as before. 30 tests.
7. **Version** 0.88 -> 0.89.
* **Verified:** Debug build of all targets, zero new warnings on changed lines, 30/30 tests (incl. all climbing/enemy tests).
* **Not changed (not asked):** `world:raycast` / `findDamageable` (weapon hits) still use the unturned position +/- scale box - the same issue for shots near turned objects.

## 1l. Session Log: 2026-09-24 (goblins, Orc Warlord boss, shields + parry - Alpha 0.88)

The user asked for a pre-made set of 2 goblins (weak, range + melee, with shields), a shield for the player on the other mouse button (attack + defend, parry), and an orc boss with a big spiked club (melee). Every change:
1. **One humanoid builder** (`Engine/src/Editor/FpsRigBuilder.cpp`): `buildPlayerBody` became `buildHumanoid(look, weapon, shield)` - same joints for the player, goblins and orc; `HumanoidLook` (size, bulk, colors, head style Human/Goblin/Orc, backpack, bare arms/chest). New part sets: goblin head (ears, pointy nose, yellow eyes), orc head (tusks, brow, jaw, topknot), orc straps + pauldron, goblin dagger, goblin spear, orc spiked club (12 spikes), round shields. Builder `setIgnoreTag`: enemy model parts are tagged "NoRaycast" (hits land on the hidden capsule).
2. **Monsters in the demo arena** (`buildMonster`/`buildMonsters`): hidden Enemy capsule (tag Empty) + health.lua + enemy.lua + body. "Goblin Cutter" (60 HP, dagger, shield), "Goblin Spearthrower" (45 HP, spears, shield, keeps 7 m away), "Orc Warlord" (500 HP, club, 30 damage, knockback 7, boss bar). Values are per-object Inspector properties.
3. **`Game/Scripts/FPSDemo/enemy.lua` rewritten** as one configurable enemy: new properties attack_style (melee|ranged), damage, attack_range, attack_cooldown, windup, chase/wander speed, search/escape radius, keep_distance, projectile_speed, has_shield, shield_block, knockback, is_boss, boss_title, body_name, model_scale. Melee = wind-up then strike (the parry window); ranged = lobbed spears (flight, hits, walls); shields block from the front unless attacking/stunned; stuns cancel attacks; boss bar while fighting; body animation (walk, run, attack, throw, guard, stunned). The Chasers keep their old values but now also wind up (0.3 s) before hitting.
4. **`health.lua`**: before damage, asks the object's other scripts `modify_incoming_damage(amount, attacker, info)` (shields); new `get_health()`; sends `on_death` when a non-player dies.
5. **`fps_player.lua` shield**: right mouse raises it (not with pistol/AK-47 - they keep aiming); 0.6x speed and no attacking while blocking; BLOCK = 80% less damage from the front; PARRY (raised within `parry_window` 0.3 s of the hit) = no damage + stuns an attacker within 4.5 m for 1.6 s; `on_knockback` (parry cancels, block halves); first-person `FPSRig.Shield` rises from below on the left; third-person body carries it and raises it (and faces the camera) while blocking. New properties has_shield, parry_window, block_reduction.
6. **HUD** (`Engine/src/Runtime/GameplayHud.cpp`): HUD bars with order >= 100 are drawn as a wide bar across the top (the boss bar) - Editor Game view and Runtime.
7. **Demo scene** regenerated.
8. **Tests** (`Engine/tests/TestMain.cpp`): `testFpsDemoMonsterModels`, `testFpsDemoGoblinsAndShields` (goblin hits; block = 80% less; parry = no damage + stun; goblin shield 60% from the front but not while stunned; spears hit and the thrower keeps its distance), `testFpsDemoOrcBoss` (boss bar, heavy hit, knockback, defeat clears the bar + victory message). 29 tests.
9. **Version** 0.87 -> 0.88.
* **Verified:** Debug build of all targets, zero new warnings on changed lines, 29/29 tests. Runtime screenshots: goblins + orc approaching with the boss bar, third-person shield raised, first-person shield raised with the orc coming (first version of the first-person shield covered most of the view - made it smaller and moved it left).
* **Not done (not asked):** no death animation (defeated enemies disappear, as before); the third-person body still doesn't hold the equipped weapon; the shield has no durability.

## 1k. Session Log: 2026-09-24 (FPS Demo full body + climbing - Alpha 0.87)

The user asked for a full third-person body for the demo player (built like the hands), with jump/run/walk/crouch/climb animations, and a script to put on any object to climb it (ladder, wall, cliff) with a 0-360 slider in the Inspector that locks the climb angle. Every change:
1. **Slider property type** (`Engine/include/GameForger/Editor/ScriptRuntime.hpp`, `Engine/src/Editor/ScriptRuntime.cpp`, `Editor/src/main.cpp`): `-- @property <name> slider <min>|<max> <default>` - a number drawn as an Inspector `SliderFloat` (per-object, undoable, live during Play like numbers). Invalid ranges (max <= min) are skipped.
2. **New Lua API** (`ScriptRuntime.cpp`): `self.entity:getPivot()`, `self.camera:setEyeHeight(h)` / `getEyeHeight()` (Play-time change of the first-person eye height).
3. **`Game/Scripts/FPSDemo/climbable.lua`** (new, part of the kit: `fpsdemo::kClimbable`, counted by `fpsDemoKitInstalled`): works out the climbable face from the object's position, scale, pivot and Y rotation; `climb_angle` (slider 0-360) = which side is climbed, turning with the object (0 = +Z side, 90 = +X, 180 = -Z, 270 = -X); `reach`. Every frame it tells Player-tagged objects in reach (`on_climbable_near`). It works as the "tag": no separate tag is needed.
4. **`fps_player.lua`**: crouch (Left Ctrl: half speed, lower collider, eye 0.55 lower, no jump); climbing (walk into a climbable face with W; W/S up/down, A/D sideways, Space jumps off with a push away, pull up over the top, S at the bottom lets go; no attacking while climbing); third-person body driver `update_body` + `body_pose` (idle breathing, walk, run, jump/fall, crouch/crouch-walk, climb; the body turns to where you move, faces the wall when climbing; head follows the look pitch); new properties `body_name`, `crouch_multiplier`, `climb_speed`.
5. **Body model** (`Engine/src/Editor/FpsRigBuilder.cpp`, `.hpp`): `buildPlayerBody` - `PlayerBody` (child of the Player, local scale cancels the capsule's scale) with Hips/Spine/Head/ShoulderR,L/ElbowR,L/HipR,L/KneeR,L joints and primitive parts (pelvis, belt, belly, chest, backpack, neck, head, hair, eyes, nose, arms, gloves, thighs, shins, boots). The Player capsule is tagged "Empty" (collision shape only, not drawn). `FpsRigBuildResult::bodyName`.
6. **Demo arena**: a "Climb Wall" block with a "Ladder" (rails + rungs + `Ladder.Climb` box, climb_angle 270) and a "Rock Cliff" (climb_angle 180). `Game/Scenes/FPSDemo.gfprod` / `.gfai` regenerated.
7. **Add Script preset** "Climbable (climbable.lua)" (`main.cpp`).
8. **Tests** (`Engine/tests/TestMain.cpp`): `testSliderScriptProperty`, `testFpsDemoClimbing` (grab, climb, stick to the face, sideways, pull up and stand on top; a wall turned 90 deg; Space jumps off), `testFpsDemoPlayerBody` (joints, parented, hidden in first person / shown in third, boots on the ground, parts keep their size, walking swings legs, crouch lowers hips, jump tucks legs). New `DemoPlayerHarness` helper. 26 tests.
9. **Version** 0.86 -> 0.87.
* **Verified:** Debug build of all targets, zero new warnings on changed lines, 26/26 tests. Runtime screenshots (scratch copy with faked input): walking, running, crouch-walking, jumping, walking to the ladder and climbing it.
* **Found (existing engine limit, not changed):** colliders ignore rotation (position +/- scale boxes), so a TURNED climbable that is also a Collider can stop the player before its face. Documented in climbable.lua; the demo cliff is not turned.
* **Not done (not asked):** the third-person body doesn't hold the equipped weapon; no climb-direction arrow in the editor viewport; I had to close the user's running 0.86 editor to relink the new build.

## 1j. Session Log: 2026-09-24 (FPS Demo third-person strafe - still Alpha 0.86)

The user reported A/D inverted in the FPS Demo after pressing C (third person).
1. **Cause:** in third person the mouse turns only the camera (it orbits the player), but `fps_player.lua` moved relative to the player's body. Once the camera swung around, A/D (and W/S) went the wrong way on screen. With the camera straight behind it was already right.
2. **Fix** (`Game/Scripts/FPSDemo/fps_player.lua`, `update_movement`): in third person, forward/right come from `self.camera:getAim()` flattened to the ground, so movement follows the camera. First person unchanged.
3. **Test** (`Engine/tests/TestMain.cpp`): `testFpsDemoThirdPersonStrafe` - imports the kit, presses C, then checks through the real third-person camera that D goes screen-right, A screen-left and W away from the camera, at 3 body facings x 4 camera angles. It failed before the fix (camera at 90 deg) and passes now. 23 tests.
* **Not changed (not asked):** the small starter `third_person_controller.lua` has the same body-relative movement. No version bump (script-only fix; the user's editor was open, so the exe couldn't be relinked).

## 1i. Session Log: 2026-09-24 (default panel layout - Alpha 0.86)

The user picked option 2: put a default panel layout into the code (audit B20). Every change:
1. `Editor/src/main.cpp`: new `buildDefaultDockLayout()` (ImGui DockBuilder, `#include <imgui_internal.h>`). Same arrangement as the user's own layout file: Hierarchy over Project (left), Inspector over Toolbox (right), Viewport/Game/Cine Camera Preview tabs (middle), AI Forge/Animation/Console/Storyboard tabs (bottom).
2. Main loop: the dockspace id returned by `DockSpaceOverViewport` is kept; on the first frame, if the layout file has no docked layout, the default is built at the start of the next frame. **Edit > Reset Editor Layout** now builds the default too (it used to load an empty layout = every panel floating).
3. New `selectDockedTab()`: after building, the Viewport and AI Forge tabs are shown (otherwise the last panel created, Storyboard, kept focus and its tab stayed on top).
4. `gameforger::editor::ImGuiInputSource` qualified in `main()` - `imgui_internal.h` has its own `ImGuiInputSource` enum.
5. Version 0.85 -> 0.86.
* **Verified:** Debug build of all targets, zero new warnings on changed lines, 22/22 tests. Screenshot with no layout file: docked, Viewport + AI Forge tabs showing. With the user's layout file: the split sizes are unchanged (not replaced). Edit > Reset Editor Layout was NOT clicked by me (no remote mouse input) - the user should check it.
* **Not done (not asked):** no merge with `unity-parity-upgrade` (option 1).

## 1h. Session Log: 2026-09-24 (camera direction fixes - Alpha 0.85)

The user reported the cameras inverted (editor Viewport: up/down and left/right; also in game). Root causes found by checking every camera against glm::lookAt, the view the renderer really uses:
1. **Editor Viewport camera** (`Editor/src/main.cpp` `updateCamera`, also used by the editor's free Play camera): "right" was `cross(+Y, forward)` = screen-LEFT, and "up" was built from it = screen-DOWN. So A/D were swapped and middle-mouse / Shift+right-drag pan was inverted on both axes. This code was like this in the initial commit (a comment claims it "fixed" pan - same wrong flip as the old getRight). Now uses the new engine helper `cameraBasis(forward)` (`Engine/include/GameForger/Runtime/GameCamera.hpp`, `Engine/src/Runtime/GameCamera.cpp`): right = cross(forward, up), up = cross(right, forward).
2. **Third-person game camera** (`scriptedPlayCamera`, `GameCamera.cpp` - shared by the Editor Game view and GameForgerRuntime): the look pitch was ADDED to the orbit pitch, so mouse up raised the camera and the view tilted down. Now subtracted: mouse up looks up, same as first person.
3. **Checked and already correct (no change):** mouse-look left/right in the Viewport (orbit and fly), Viewport mouse-look up/down, first-person look in the Game view and Runtime, W/S/Q/E fly keys, `getRight()`.
4. **Tests** (`Engine/tests/TestMain.cpp`): `testCameraBasisMatchesScreen` (right/up land screen-right/screen-up at 15 yaw/pitch combos) and `testThirdPersonMouseUpLooksUp`. 22 tests.
5. **Version** 0.84 -> 0.85.
* **Verified:** Debug build of all targets, zero new warnings on changed lines, 22/22 tests, editor starts.
6. **Layout (no code change):** the user reported the layout broken. Cause: no default dock layout in code (plan B20); this worktree had no layout file, so panels floated. Copied the user's `GameForgerEditorLayout.ini` from `C:\GameForgerAI-Editor` into the worktree (gitignored file; the broken one is backed up in the session scratchpad).

## 1g. Session Log: 2026-09-24 (audit fixes B13, gizmo keys, A3, G1 - Alpha 0.84)

The user asked for everything in 1f's "Not done" list. Every change, in order:
1. **Correction:** B3 (rename orphans) and B9 (picking hidden objects) were already fixed in Alpha 0.80 (plan §2.5; `testRenameKeepsChildrenAttached`; `pickEntity` checks `isActiveInHierarchy`). 1f and plan §2.8 wrongly listed them as open. Their rows in plan §2.1 now say FIXED 0.80. No code change.
2. **B13** (`Editor/src/main.cpp`): removed the Ctrl+R save branch from `drawEditorPanels`. Save Scene is Ctrl+S only (menu label unchanged). `drawEditorPanels` no longer takes `currentScenePath`/`sceneDocument` (Ctrl+R was their only use).
3. **Gizmo keys** (`main.cpp`): W/E/R/T/Y over the Viewport switch the gizmo only when Ctrl is NOT held, so Ctrl+Y (redo) no longer also picks Universal and Ctrl+R no longer picks Scale.
4. **A3** (`main.cpp`): deleted the hard-coded `providers` array. New `loadProviderList()` reads id, displayName, endpoint, model, apiKeyEnvironmentVariable and `activeProvider` from `Game/AI/Providers.json`. `AISetupState` keeps the list plus `selectedProviderId`; the editor starts on the file's `activeProvider` (first entry if it isn't listed). Settings > AI Setup re-reads the file while open, shows a red note if the file has no providers, and disables Test until a listed provider is chosen. AI Forge / script generation use `selectedProviderId`.
5. **G1** (`main.cpp`, Inspector > Add Script presets): a preset whose `.lua` isn't in the project is still listed but greyed out with "(file missing: <path>)" and can't be picked - today that is the Catapult Controller (`Game/Scripts/catapult_controller.lua`). The file itself was not restored or written.
6. **Version** 0.83 -> 0.84 (`CMakeLists.txt`, `README.md`).
* **Verified:** Debug build of Engine, Tests, Editor, Runtime - zero new warnings on changed lines; `GameForgerTests` 20/20; the editor starts and runs.
* **Not done (not asked):** writing a new `catapult_controller.lua` (the plan leaves that to the user); choosing a provider in Settings doesn't write `activeProvider` back to the file.

## 1f. Session Log: 2026-09-24 (audit fixes B4, B1, B2, B5, A1, A2 - Alpha 0.83)

Every change, in the order the user asked for them:
1. **B4 script truncation** (`Editor/src/main.cpp`): `ScriptEditorState::buffer` (fixed 16 KB) -> `std::string text`; `ScriptCreatorState::previewBuffer` (fixed 8 KB) -> `std::string previewText`. New helper `inputTextMultilineString()` (ImGui `CallbackResize`, grows as you type). The Project-browser `.lua` open, the Inspector "Edit" button, the Edit Script modal Save and the AI preview "Save & Attach" all use the full string now.
2. **B1 double shortcuts** (`main.cpp`): removed the Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y branches from `drawMainMenu`. The one handler left is in `drawEditorPanels`: Ctrl+Z = undo, Ctrl+Shift+Z or Ctrl+Y = redo (it used to undo on Ctrl+Shift+Z too). Kept in `drawEditorPanels` rather than a new `handleEditorShortcuts()` function.
3. **B2 scene file** (`main.cpp`, `Engine/include/GameForger/Editor/SceneSerializer.hpp`, `Engine/src/Editor/SceneSerializer.cpp`):
   - New `serializeScene(entities)` = the exact JSON `saveScene` writes (`saveScene` now calls it).
   - New `SceneDocumentState` (scene text at the last New/Open/Save) + `sceneHasUnsavedChanges`, `saveSceneToPath`, `saveSceneAsDialog`, `saveCurrentScene`. During Play the edit-mode copy (`playMode.savedEntities`) is what is compared and saved.
   - New Scene clears `currentScenePath`. The editor also starts with an empty path (it used to start pointing at `Castle.gfprod` with an empty scene, so Ctrl+S overwrote Castle).
   - Save Scene (menu, Ctrl+S, Ctrl+R) with no file opens Save As.
   - New, Open and closing the window show "Unsaved Changes: Save / Don't Save / Cancel" when the scene differs from the last save. A cancelled Save As counts as Cancel.
   - File > Build Game is disabled (with a tooltip) until the scene has been saved to a file.
4. **B5 multi-select** (`main.cpp`): `deleteSelected` / `duplicateSelected` act on every id in `multiSelectedIds` (new helper `selectedEntityNames`). All commands run in one frame, so the undo snapshot groups them into one step. Duplicate selects all the copies.
5. **A1 Endpoint/Model** (`main.cpp`): removed the dead `endpoint`/`model` text boxes (and their arrays in `AISetupState`). Settings > AI Setup now shows the provider's `endpoint`/`model` read from `Game/AI/Providers.json` (read-only, re-read while the page is open) and an **Open Providers.json** button (opens it with the .json app, Notepad if none).
6. **A2 Test provider** (`main.cpp`): the probe runs on `AISetupState::testWorker` (same pattern as AI Forge); the button is disabled with "Testing..." while it runs; `pollProviderTest()` (called every frame from `drawSettingsWindow`) shows/logs the result; the thread is joined at shutdown.
7. **Version** 0.82 -> 0.83 (`CMakeLists.txt`, `README.md`).
* **Verified:** Debug build of Engine, Tests, Editor, Runtime - zero new warnings on changed lines; `GameForgerTests` 20/20; the editor starts and runs. The manual UI checks from Phase 1 (popup, multi-delete + one Ctrl+Z, Test with network off) are for the user.
* **Not done (not asked):** B13 (Ctrl+R as a second save key) is unchanged - Ctrl+R now just follows the same Save rules. Ctrl+Y / Ctrl+R over the Viewport still also switch the gizmo mode (Y = universal, R = scale) - separate item, not touched. A3 (provider list written twice) unchanged. No other Phase 1 items (B3, B9, G1).

## 1e. Session Log: 2026-09-24 (strafe fix + FPS Demo kit packaging - Alpha 0.82)

Every change, in order:
1. **Pushed** all 0.80/0.81 work to GitHub, branch `TattooAI/app-audit-workflow-reorg-cca6a1` (commit `5011473`).
2. **Strafe fix (commit `053770e`):** `Engine/src/Editor/ScriptRuntime.cpp` `luaEntityGetRight` now returns `cross(forward, up)` (screen-right for the game camera). It had returned `cross(up, forward)` = screen-LEFT since the 2026-08-13 audit item H-Script-1, so D strafed left in `fps_controller.lua`, `third_person_controller.lua` and `FPSController.lua`. `fps_player.lua` dropped its own workaround and uses `getRight()` again. New test `testGetRightMatchesScreenRight` checks it against `glm::lookAt` at 5 yaws.
3. **Renamed "FPS Opus" to "FPS Demo"** everywhere (code, `@preset` tags, Inspector/Add Script text, docs). "Opus" was a name I made up; the user said demo.
4. **Kit folders:** `git mv` the 7 demo scripts from `Game/Scripts/` to `Game/Scripts/FPSDemo/`; icons from `Game/Icons/Weapons/` to `Game/Icons/FPSDemo/`; added `Sword.png`, `Axe.png`, `Hammer.png`, `Item.png` (copies of icon-pack `SwordT2`/`AxeT1`/`HammerT1`/`Coin`) so the kit needs no other folder. `items.lua` default icon -> `Game/Icons/FPSDemo/Item.png`.
5. **Demo enemy:** new `Game/Scripts/FPSDemo/enemy.lua` = the enemy with melee attack + stun (what `enemy_ai.lua` had become in 0.81). `Game/Scripts/enemy_ai.lua` restored to its original small starter version.
6. **One place for kit paths:** new `Engine/include/GameForger/Runtime/FpsDemoKit.hpp` (`fpsdemo::` constants). Replaced the hard-coded paths in `GameplayLoop.hpp`, `GameplayHud.cpp`, `FpsRigBuilder.{hpp,cpp}`, `Runtime/src/main.cpp`, `Editor/src/main.cpp`, tests. The Game view's red "detected" icon also works for the demo `enemy.lua`.
7. **Import:** new `Engine/src/Runtime/FpsDemoKit.cpp` (`importFpsDemoKit`, `fpsDemoKitInstalled`, `findFpsDemoKitSource`) + **File > Import FPS Demo Kit into This Project** (copies `Game/Scripts/FPSDemo/` + `Game/Icons/FPSDemo/` from the engine folder the editor was built in; keeps existing files).
8. **Demo scene** `Game/Scenes/FPSDemo.gfprod`/`.gfai` regenerated with the new paths.
9. **Version** 0.81 -> 0.82 (`CMakeLists.txt`, `README.md`).
* **Verified:** Debug build of all targets, zero new warnings on changed lines; `GameForgerTests` 20/20 - the end-to-end test now builds its project ONLY through `importFpsDemoKit`, proving the kit is self-contained.
* **Not done (not asked):** no auto-import, no kit copy in `New-GameForgerAIProject.ps1`, no kit README.

## 1d. Session Log: 2026-09-23 (FPS Demo preset: magic, XP, runtime showcase - Alpha 0.81)

### Claude - completed & verified this session
* **New scripts:** `projectiles.lua`, `effects.lua`, `xp_system.lua`, `game_manager.lua`; `fps_player.lua` (Fire/Frost/Life Casters, XP bonuses, crits, charges), `health.lua` (heal, burn/frost, damage numbers, player mode/respawn), `enemy_ai.lua` (melee attack), `items.lua` (new weapon ids). Every preset script ends with `-- @preset FPS Demo | <role>`.
* **Engine:** `GameplayHud.{hpp,cpp}` (shared HUD layout over an abstract canvas), messaging/combat/HUD Lua API in `ScriptRuntime.cpp`, `@preset` + `image` property parsing, `scriptPropertyText`, `loadTextureImageTopDown`, one-frame particles, 3 casters + Game Manager + demo changes in `FpsRigBuilder.cpp`.
* **Runtime:** `RuntimeHud.{hpp,cpp}` (GL implementation of the HUD canvas + font fallback), Game Manager splash/title, save-to-startup-scene matching (`Game/Saves/save.meta.json`), inventory panel.
* **Editor:** preset grouping + "Link all" checkbox, FPS Demo group in Add Script, `image` property UI (Change Image...), GameObject > Game Manager, Game view uses the shared HUD.
* **Fixed:** splash screens and 2D icons upside down (global `stbi_set_flip_vertically_on_load` vs. old UV assumptions).
* **Verified:** Debug build of all targets, zero new warnings on changed lines; `GameForgerTests` 19/19 incl. end-to-end on the real scripts; runtime screenshots (splash, fire, frost, lightning) via a scratch copy of the project.

## 1c. Session Log: 2026-09-23 (Opus 5.5 audit + FPS player / weapons / items)

### Claude - completed & verified this session (Alpha 0.80)
* **Audit:** `opus5.5Audit.md` (repo root) - whole-app audit plus a phase-by-phase UI regrouping plan, not started yet (waiting on the user's go-ahead per phase).
* **FPS player + weapons + items (user request):** `Game/Scripts/fps_player.lua`, `items.lua`, `health.lua`; `Engine/src/Editor/FpsRigBuilder.cpp` (hands + 8 weapons rig, demo arena); inventory/pickup/effects moved into `GameplayLoop.cpp` (shared with Runtime); new Lua API in `ScriptRuntime.cpp`; per-object script properties (`SceneEntity::scriptProperties`, serialized); `icon`/`enum` property types with an Inspector icon picker ("Change Icon..."); hotbar/beam/flash/health-bar HUD and a new inventory grid in the Editor Game view.
* **Fixed along the way:** audit B3 (rename orphans children), B8 (script property edits discarded outside Play), B9 (Viewport picks hidden objects); ImGuizmo `GIT_SHALLOW` + commit-hash clone failure on fresh configures.
* **Found, not changed (needs the user's call):** `self.entity:getRight()` returns screen-LEFT (the game camera shows +X on the left when looking down +Z), so D strafes left in `fps_controller.lua`/`third_person_controller.lua`. `fps_player.lua` computes its own correct right vector.
* **Verified:** Debug build of Engine/Editor/Runtime/Tests clean with zero new warnings on changed lines; `GameForgerTests` 18/18 (9 new, incl. an end-to-end run of the real scripts through walking, pickups, all 8 weapons, reload, ADS, melee damage); runtime screenshots of the viewmodel for AK-47, pistol, sword, axe, war hammer, Storm Caster.

## 1b. Session Log: 2026-08-21 (Master Roadmap Consolidation)

**What happened:** the user asked for a single new master roadmap consolidating everything left to build — all phases, from the current gauntlet-audit work through Beta 1.0 — with tasks split between Claude and Gemini, verification delegated to fresh subagents on both sides, and nothing lost from any older planning document (several of which had accumulated real, never-promoted findings). Also asked that old plans become references rather than being deleted, renamed instead with a version suffix, and that a `docs/phases/` progress-log folder be set up for ongoing work.

**Archive research (delegated to a subagent, not done inline):** a background subagent read `audit.md`, `fix.md`, `NewAudit.md`, `audit-2026-08-13.md`, `PlanFixAuditResults.md`, and `IMPLEMENTATION_PLAN.md` in full and cross-checked every finding against `planFix_AGY.md`'s own ticket tables and traceability matrix. Found **13 small/medium findings** from `audit-2026-08-13.md` that existed only as unresolved `TBD-*` placeholders in `PlanFixAuditResults.md` and never became a real ticket anywhere (now `R-01` through `R-17` in the RoadMap's Phase 0.5), plus **several much larger ideas** unique to `PlanFixAuditResults.md` that never carried forward at all — most notably a whole ~30-person-day "GDevelop-parity behavior library" tier (20+ pre-built attachable behaviors + a variable system), which the subagent flagged as *"arguably the single largest surviving gap"* in the project's planning history. Also found two concrete unbuilt AI Forge goals in `IMPLEMENTATION_PLAN.md` (a guided full-game-creation wizard, a provider-agnostic AI adapter layer) that never made it into any engine-focused plan since they're product/AI-layer scope, not engine scope.

**What was created:**
- **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)** — the new active master plan. Phase 0 (gauntlet, in progress) through Phase 4 (platform packaging → Beta 1.0), plus a parallel Phase 5 (AI Forge expansion) and an explicitly-parked Someday/Post-1.0 backlog so large/speculative ideas (ProBuilder-style mesh editing, asset store, browser live preview, cloud save, voice chat) are visible but not scheduled. Every ticket keeps a Claude/Gemini owner, an effort estimate, and a prep checklist (skills to load, new dependencies to add via `FetchContent`, what to read in `Compared.md` first).
- **`docs/phases/README.md`** — the `phaseNN.letter.seq.md` naming convention for the ongoing per-ticket progress log, plus `docs/phases/phase00.a.md` (Claude's completed Phase 0 work, worked example) and `docs/phases/phase00.b.md` (Gemini's assigned Phase 0 work, stub for Gemini to fill in).

**Renames (old files kept, not deleted, each got a version suffix + a banner pointing to the new RoadMap):**
`planFix_AGY.md` → `planFix_AGY.v1.2026-08-18.md`, `audit-2026-08-13.md` → `audit-2026-08-13.v1.md`, `PlanFixAuditResults.md` → `PlanFixAuditResults.v1.md`, `audit.md` → `audit.v1.2026-08-01.md`, `fix.md` → `fix.v1.2026-08-01.md`, `NewAudit.md` → `NewAudit.v1.2026-08-02.md`. Every cross-reference to these six files in `AGENTS.md`, this changelog, `Gemini_todo_list.md`, `PROJECT_AUDIT_GAUNTLET_REPORT.md`, and `IMPLEMENTATION_PLAN.md` was updated to point at the new names/the new RoadMap — confirmed via grep that no markdown link to an old filename remains broken. `Compared.md`, `IMPLEMENTATION_PLAN.md`, `CHANGES.md`, and `README.md` were **not** renamed — they're either an active spec, active background reading, or logs (not superseded plans).

**Not renamed but updated in place:** `AGENTS.md` §1 now points `RoadMap2026-08-21.md` as the primary directive and lists the six archived files explicitly; its Agent Execution Loop diagram (§3) now includes a prep/tools-check step, a `docs/phases/` logging step, and a subagent-verification step that weren't there before.

**Verification:** this was a planning/documentation session, no C++ code changed — no build/CTest run needed. Every internal markdown cross-reference was checked with `grep` after the renames to confirm nothing links to a now-nonexistent filename.

---

## 2. Ticket Status & Progress Tracker

> **Legend:**  
> 🟢 Completed & Verified | 🟡 In Progress / Claimed | ⚪ Not Started / Scheduled

### Quick Wins (Target: Milestone 0.79)
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **Q-1** | Drop negation in capsule bottom hemisphere | 🟢 | Verified in `PrimitiveMeshes.cpp` & `testPrimitiveMeshes` (outward normals dot > 0.8) |
| **Q-2** | Primitive winding order fix | 🟢 | Verified in `PrimitiveMeshes.cpp` & `testPrimitiveMeshes` (CCW cross product outward alignment on Cube, Sphere, Cylinder, Cone, Capsule) |
| **Q-3** | Render Map Humanoid / Animation Library as disabled | 🟢 | Verified disabled menu items with explanatory tooltips in `Editor/src/main.cpp` |
| **Q-4** | Rename/clarify `isKeyPressed` vs `isKeyDown` | 🟢 | Renamed and documented in `InputSource.hpp`, `GlfwInputSource.cpp`, `main.cpp` |
| **Q-5** | Drop misleading `const` from `getScriptNumberField` | 🟢 | Fixed signature and return type in `ScriptRuntime.cpp` |
| **Q-6** | Bound suffix loop in `makeUniqueName` (cap at 10,000) | 🟢 | Capped iteration and prevented infinite loops in `EditorScene.cpp` |
| **Q-7** | Reject empty-string queries in `nameInUse` | 🟢 | Handled in `EditorScene.cpp` & verified in `testEditorScene` |
| **Q-8** | Hierarchy text search / filter bar | 🟢 | Implemented `##HierarchySearch` with recursive subtree expansion in `Editor/src/main.cpp` |
| **Q-9** | Viewport grid snapping toggle & increments | 🟢 | Implemented Local/Global space toggle and grid snapping passed to `ImGuizmo::Manipulate` in `Editor/src/main.cpp` |
| **Q-10** | Simulation Pause and Step Frame buttons | 🟢 | Implemented `Pause`/`Resume` and `Step` toolbar buttons, wired into `PlayModeState` and simulation loop in `Editor/src/main.cpp` |
| **Q-11** | Entity `activeSelf` checkbox in header | 🟢 | Added `bool active` to `SceneEntity`, serialized in `SceneSerializer.cpp`, checkbox in Hierarchy & Inspector, respected in Viewport, Script, Animation, and Projectile ticks |

### Tier 1 — Core Defect Remediation & Test Infrastructure
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T1-1** | Structured JSON provider parser | 🟢 | Verified JSON parser in `AIProviderClient.cpp` & `testJsonParser` |
| **T1-2** | AI Provider live calibration & UI override | 🟢 | Verified endpoint and key configurations in `AIProviderClient.cpp` |
| **T1-3** | Strict scene validation & format versioning | 🟢 | Implemented `GameForgerScene` format check, directory safety, and atomic file replacement on Windows in `SceneSerializer.cpp` & `testSceneSerialization` |
| **T1-4** | JSON parser hardening (RFC compliance, UTF-8) | 🟢 | Enforced end-of-document validation in `Json.cpp` & verified with `testJsonParser` |
| **T1-5** | Gameplay loop & runtime state isolation | 🟢 | Verified `GameplayLoop.cpp` parity between Editor and standalone Runtime |
| **T1-6** | Lua sandbox isolation & global leak prevention | 🟢 | Implemented dedicated per-script `_ENV` table with `__index = _G`, stripped unsafe globals (`dofile`, `loadfile`, `load`, `collectgarbage`) in `ScriptRuntime.cpp`, verified with `testScriptRuntimeSandboxing` |
| **T1-7** | Automated test suite & CI Pipeline | 🟢 | Created `Engine/tests/TestMain.cpp`, `.github/workflows/ci.yml`, registered with `enable_testing()` & `add_test()`, verified with `ctest` (100% tests passing) |

### Tier 2 — Architecture Modernization (Milestone 0.85 Completed 100%)
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T2-1** | Entity-Component model refactor (`GameObject` + `Component`) | 🟢 | Implemented `Component.hpp` (Transform, MeshFilter, MeshRenderer, Collider, Light, Camera, AudioSource, Script) and `GameObject.hpp` container; verified in `testGameObjectComponentModel` |
| **T2-2** | Inspector script parameter reflection | 🟢 | Implemented `-- @property` parser, runtime field accessors in `ScriptRuntime.cpp`, live Inspector controls in `Editor/src/main.cpp`; verified in `testScriptPropertyReflection` |
| **T2-3** | GUID-based Asset Database & `.meta` pipeline | 🟢 | Implemented `AssetDatabase.hpp` / `.cpp` with 128-bit GUID generation, two-way lookup, recursive scanning, and `.meta` sidecar files; verified in `testAssetDatabase` |
| **T2-4** | Standalone Material asset system (`.gfmat`) | 🟢 | Implemented `Material.hpp` / `.cpp` with JSON serialization/deserialization, blend modes, PBR maps, and format validation; verified in `testMaterialSerialization` |

### Tier 3 — Essential Engine Subsystems
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T3-1** | Multi-light shading & real-time shadow mapping | ⚪ | Target: `ViewportRenderer.cpp` |
| **T3-2** | Standard Cook-Torrance PBR shading pipeline | ⚪ | Target: `ViewportRenderer.cpp` |
| **T3-3** | Jolt / Bullet3 3D physics engine integration | ⚪ | Target: `PhysicsWorld.cpp` |
| **T3-4** | Spatial 3D audio subsystem (`miniaudio`) | ⚪ | Target: `AudioEngine.cpp` |
| **T3-5** | Placeable Camera component & multi-camera manager | ⚪ | Target: `CameraComponent.hpp` |

### Tier 4 — Editor Workflow & Viewport Parity
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T4-1** | Complete top-level menu bar (File/Edit/Assets/GameObject/Window) | ⚪ | Target: `main.cpp` |
| **T4-2** | Viewport tooling (Global/Local, Pivot/Center, Snapping, View Cube) | ⚪ | Target: `main.cpp` / `ViewportRenderer.cpp` |
| **T4-3** | Hierarchy enhancements (Sibling reorder, Eye visibility, Lock) | ⚪ | Target: `main.cpp` |
| **T4-4** | Game View aspect ratio constraints & simulation tools | ⚪ | Target: `main.cpp` |
| **T4-5** | Multi-object Inspector editing & component context actions | ⚪ | Target: `main.cpp` |
| **T4-6** | Prefab system (`.prefab.json`) | ⚪ | Target: `Prefab.cpp` / `main.cpp` |

### Tier 5 — Advanced Subsystems
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T5-1** | In-game 2D UI Canvas & `RectTransform` system | ⚪ | Target: `Engine/src/UI/` |
| **T5-2** | Shuriken-style particle system (`ParticleSystemComponent`) | ⚪ | Target: `Engine/src/VFX/` |
| **T5-3** | Recast/Detour NavMesh surface baking & pathfinding | ⚪ | Target: `Engine/src/Navigation/` |
| **T5-4** | Animator state machine & visual Bezier curve editor | ⚪ | Target: `Engine/src/Animation/` |
| **T5-5** | Terrain authoring enhancements (Unlimited layers, Foliage brush) | ⚪ | Target: `Terrain.cpp` |

### Tier 6 — Platform Packaging & Build Pipeline
| Ticket ID | Description | Status | Verification Notes |
|---|---|:---:|---|
| **T6-1** | Dedicated Build Settings window | ⚪ | Target: `BuildSettingsWindow.cpp` |
| **T6-2** | Player Settings configuration (Icons, Splash, Resolution) | ⚪ | Target: `main.cpp` |
| **T6-3** | Standalone executable & asset archive bundler (`.gfdata`) | ⚪ | Target: `Runtime/CMakeLists.txt` |

---

## 3. Next Session Instructions

When starting a new implementation session:
1. Review [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) — check "You Are Here" and pick the next open ticket owned by your agent.
2. Do the phase's prep checklist (tools/skills/dependencies) before writing code.
3. Follow the test-driven development workflow: implement unit/regression tests in `Engine/tests/` where applicable.
4. Log start/finish in `docs/phases/` (see `docs/phases/README.md`) and link it from the RoadMap's progress table.
5. Update this changelog file (`planFix_AGY_CHANGELOG.md`) by flipping ticket status markers (`⚪` -> `🟢`) and recording notes upon task completion, and update the RoadMap's own status column too.
