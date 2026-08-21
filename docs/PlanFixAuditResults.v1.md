# GameForgerAI — Fix Plan from Audit Results

> **⚠️ ARCHIVED as of 2026-08-21.** Renamed from `PlanFixAuditResults.md` (kept, not deleted). Superseded by `planFix_AGY.v1.2026-08-18.md`, itself now superseded by **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)**. Several larger ideas unique to this file (visual scripting/behavior library, ProBuilder-style mesh editing, asset store, browser live preview, expression evaluator, animation event timeline, frame debugger, cloud save, voice chat) were never carried into `planFix_AGY.md` at all — they're now recovered as `R-NN` tickets or explicitly parked in the RoadMap's "Someday / Post-1.0 Backlog" so they aren't lost again. Read the RoadMap first.

**Source audit:** `audit-2026-08-13.md` (47 findings) + fresh independent re-check on 2026-08-18
**Project state at this plan:** Alpha 0.78 (single-developer, ~23 kLOC C++20 + ImGui + Lua 5.4 + Assimp)
**Goal of this file:** turn the audit findings into a concrete, prioritized build plan.

> Legend: ✅ fixed (verified) · 🟡 claimed fixed (not independently re-verified) · ❌ still open · ⚠️ partial / risky

---

## Table of contents

1. [How to use this plan](#how-to-use-this-plan)
2. [Quick wins (≤ 1 day each)](#quick-wins--1-day-each)
3. [Tier 1 — must-fix existing bugs](#tier-1--must-fix-existing-bugs)
4. [Tier 2 — core engine features](#tier-2--core-engine-features)
5. [Tier 3 — editor UX features](#tier-3--editor-ux-features)
6. [Tier 4 — bigger engine systems](#tier-4--bigger-engine-systems)
7. [Tier 5 — GDevelop-style authoring for non-coders](#tier-5--gdevelop-style-authoring-for-non-coders)
8. [Tier 6 — long-term / multiplayer / platform](#tier-6--long-term--multiplayer--platform)
9. [Suggested milestones](#suggested-milestones)
10. [Cross-reference: original audit tag → ticket](#cross-reference-original-audit-tag--ticket)

---

## How to use this plan

- Each ticket has a **severity**, **files**, **acceptance check**, and an **effort estimate**.
- Severity is `🔴 Critical` (build / data loss / silent wrong behavior) > `🟠 High` (user-visible regression) > `🟡 Medium` (edge case) > `🟢 Low` (nit / future-proofing).
- Effort is in **person-days for a single C++ developer who knows the codebase** (1 pd ≈ focused day of work, not calendar).
- Items already fixed are listed for context only — do not re-work them.

---

## Quick wins (≤ 1 day each)

These are tiny, isolated, and pay off immediately. Knock them out first.

| # | Ticket | File | Why | Effort |
|---|---|---|---|---|
| Q-1 | **H-Render-2** Drop the negation in capsule bottom-hemisphere generation | `Engine/src/Editor/PrimitiveMeshes.cpp:181` | One-line fix; every capsule primitive currently has inside-out lighting on the bottom. | 0.05 pd |
| Q-2 | **H-Render-3** Fix winding order on cube ±X faces, sphere, cylinder side+disks, cone slant+base, capsule body; flip `reverseWinding` on cylinder top disk | `Engine/src/Editor/PrimitiveMeshes.cpp` | Lets culling be enabled safely; right-hand rule matches declared normal. | 0.3 pd |
| Q-3 | **L13** Render `Map Humanoid Skeleton…` and `Animation Library…` menu items as disabled placeholders | `Editor/src/main.cpp:1661-1662` | `ImGui::MenuItem(..., false)`; communicates intent to the user. | 0.05 pd |
| Q-4 | **E-UI-3** Rename `isKeyPressed` to `wasKeyPressedThisFrame` in the header OR add an `isKeyDown` mapping; document the one-frame semantics | `Engine/include/GameForger/Editor/InputSource.hpp` | Lua authors writing `while self.input:isKeyPressed("W") do …` get one frame of truth — silent misbehavior. | 0.1 pd |
| Q-5 | **E-Script-2** Drop the misleading `const` from `getScriptNumberField` | `Engine/src/Editor/ScriptRuntime.cpp:839` | `lua_rawgeti` / `lua_getfield` / `lua_pop` mutate the VM; `const` hides side effects. | 0.02 pd |
| Q-6 | **L1** Bound the suffix loop in `makeUniqueName` (cap at 10 000) | `Engine/src/Editor/EditorScene.cpp:683-697` | Prevents degenerate-loop freezes. | 0.05 pd |
| Q-7 | **L2** Reject empty-string queries in `nameInUse` | `Engine/src/Editor/EditorScene.cpp:678-681` | Empty names silently pass. | 0.02 pd |
| Q-8 | **Hierarchy search/filter** (new feature, not in original audit) | `Editor/src/main.cpp:3570` (Hierarchy panel) | 30 lines of ImGui; the single highest-leverage UX win in the editor. | 0.3 pd |
| Q-9 | **Grid snap toggle** in the Viewport toolbar | `Editor/src/main.cpp:6489` (Viewport) | ImGuizmo already supports it; just expose the toggle. | 0.2 pd |
| Q-10 | **Editor theme: light mode** (currently dark only) | `Editor/src/main.cpp` ImGui style block | Two hours of CSS-class-equivalent work; modern teams expect it. | 0.3 pd |

**Subtotal:** ~1.4 pd. Do all of these in the first day of the new sprint.


---

## Tier 1 — must-fix existing bugs

> These are the items the 2026-08-13 audit called out that are still arguably open. Each ticket has been independently spot-checked on 2026-08-18. Some are listed as 🟡 "claimed fixed" in `CHANGES.md` but I could not verify the fix end-to-end from code alone — those need a smoke test.

### T1-1 — Replace string-search provider parser with the structured JSON parser [🔴 Critical, 🟡 partial fix?]

- **Files:** `Editor/src/AIProviderClient.cpp:37-58, 94-128, 148-152`
- **Problem:** `parseProvider` does `find("\"id\": \"" + providerId + "\"")` then extracts values between the next two `"`. Escaped `\"` in URLs terminates values early; model name is concatenated raw into the request body with no `escapeJson`. A `"` or `\n` in a model name produces an invalid body that gateways 400.
- **Fix:** Use `gameforger::editor::json::Value` (already in the Engine) to walk `providers[]` / `choices[0].message.content` / `error.message`.
- **Acceptance:**
  - [ ] `parseProvider` no longer calls `find` / `substr` on raw strings.
  - [ ] An endpoint URL containing `\"` round-trips correctly through provider selection.
  - [ ] A model name containing `"` produces a valid JSON body.
- **Effort:** 0.5 pd

### T1-2 — Capsule bottom hemisphere normals flipped [🔴 Critical, ❌ open]

- **File:** `Engine/src/Editor/PrimitiveMeshes.cpp:181`
- **Problem:** `generateUvSphereShell(out, 8, segments, -radius, -halfHeight)` produces a sphere of the correct *shape* but the unit-vector normals then point inward.
- **Fix:** Drop the negation.
- **Effort:** 0.05 pd (see Q-1)

### T1-3 — Winding-order inversion on most primitives [🔴 Critical, ❌ open]

- **Files:** `Engine/src/Editor/PrimitiveMeshes.cpp`
- **Problem:** `(b-a) × (c-a)` points opposite to the declared `normal` attribute. Latent because `GL_CULL_FACE` is disabled, but the moment culling is enabled most geometry disappears.
- **Fix:** Swap vertex order so right-hand rule matches the declared normal; flip `reverseWinding` on the cylinder top disk.
- **Effort:** 0.3 pd (see Q-2)

### T1-4 — AI Setup "Calibrate / Test provider" button is a no-op [🟠 High, 🟡 partial fix?]

- **Files:** `Editor/src/main.cpp:1739-1742` (and follow-on wiring)
- **Problem:** The button flips two UI flags and does nothing else. The endpoint/model edits in this panel are also ignored by `AIProviderClient`, which re-reads `Providers.json` on every call.
- **Fix:** Send a lightweight GET request and surface status; plumb override settings through to the client.
- **Acceptance:**
  - [ ] Clicking the button produces a visible test result.
  - [ ] Editing endpoint/model in the panel actually overrides `Providers.json` for the next call.
- **Effort:** 1 pd

### T1-5 — `loadScene` ignores `format` and `version` [🟡 Medium, ❌ open]

- **Files:** `Engine/src/Editor/SceneSerializer.cpp:566-585`
- **Problem:** Writer embeds `"format": "GameForgerScene"`, `"version": 8`. Loader skips both. Any JSON file with an `entities` array (e.g. `Project.json`) loads as a scene with default-fallback fields and silently overwrites the current scene.
- **Fix:** Validate `format == "GameForgerScene"`; reject/upgrade on version mismatch.
- **Effort:** 0.3 pd

### T1-6 — JSON parser accepts invalid input (6 distinct bugs) [🟡 Medium, ❌ open]

- **File:** `Engine/src/Editor/Json.cpp` (whole file)
- **Bugs:**
  1. `parseDocument` doesn't require end-of-input → `{"foo":1}garbage` parses.
  2. `parseRawString` accepts a trailing `\` as a literal backslash.
  3. Raw control characters U+0000–U+001F accepted inside strings.
  4. `\u` escapes: no hex validation, no surrogate pair handling, lone surrogates emitted as invalid UTF-8.
  5. `appendUtf8` has no 4-byte branch.
  6. `parseNumber` accepts leading `+`, structurally lax loop accepts `1.2.3`, `--5`, etc.
- **Fix:** Six small per-function fixes; pair each with a unit test (see T1-12).
- **Effort:** 0.5 pd

### T1-7 — `escapeJson` silently drops `\r` and other control bytes [🟡 Medium, ❌ open]

- **Files:** `Engine/src/Editor/SceneSerializer.cpp:306-323`, `Editor/src/AIProviderClient.cpp:72-92`
- **Problem:** User-pasted CRLF strings lose CR on round-trip; tabs/form-feeds produce invalid JSON.
- **Fix:** Centralize a single `escapeJson` that handles all 7 control characters + surrogate pairs.
- **Effort:** 0.2 pd

### T1-8 — Runtime bugs (6 items in `Runtime/src/main.cpp`) [🟡 Medium, ❌ open]

- **File:** `Runtime/src/main.cpp`
- **Bugs:**
  - **E-Run-2** `deltaTime` is `clampDeltaTime` for the gravity path only; `tickProjectiles` non-gravity path also needs clamping.
  - **E-Run-3** Fast projectiles (22 u/s × 0.033 s ≈ 0.7 u/step) tunnel through 1-unit castle colliders. No swept test, no sub-iteration.
  - **E-Run-4** `followedEntity` is a dangling pointer if a script deletes the entity mid-frame.
  - **E-Run-5** Inventory save/load is asymmetric — write emits only `itemName`/`iconPath`/`count`; read default-initializes the other 5 fields. Pickup-then-save-then-reload visually loses item identity.
  - **E-Run-6** `Load` doesn't reset `heldItemEntityName` / `playerOperatingCatapult` / `gameOverMessage` — stale session state survives a load.
  - **E-Run-7** `Load` doesn't reset `gameCameraLookYawDegrees` / `gameCameraLookPitchDegrees` — camera starts looking sharply up-and-right.
- **Fix:** Reset session state on load; swept sphere-vs-AABB for projectiles; round-trip all 8 InventoryItem fields; re-find `followedEntity` after `tickScripts`.
- **Effort:** 1.5 pd

### T1-9 — `SetPropertyCommand` accepts NaN/Inf and wrong types silently [🟡 Medium, ⚠️ partial]

- **Files:** `Engine/src/Editor/EditorScene.cpp:337-613`, `Engine/src/Editor/AICommandBus.cpp:112-121`
- **Problem:** Vec3 fields accept NaN/Inf — `glm::translate` propagates NaN into the model matrix. `SetPropertyCommand` validator doesn't finiteness-check vec3s the way `SetPositionCommand` does.
- **Fix:** Add `std::isfinite` checks to the variant dispatch in `AICommandBus`; reject the command at validation, not at execution.
- **Effort:** 0.3 pd

### T1-10 — Lua chunk partial-failure leaks globals to `_G` [🟡 Medium, ❌ open]

- **File:** `Engine/src/Editor/ScriptRuntime.cpp:721-726`
- **Problem:** On `lua_pcall` error, any top-level `SOME_CONSTANT = 1` defined before the error remains in `_G`. Future scripts sharing those names inherit broken state.
- **Fix:** Run each script's chunk in a fresh sandboxed env (set `_ENV` at chunk load), or wrap in `pcall` and roll back any globals set before the error.
- **Effort:** 0.5 pd

### T1-11 — `CreateScriptCommand` silently overwrites existing scripts [🟡 Medium, ⚠️ partial]

- **File:** `Engine/src/Editor/EditorScene.cpp:108-113`
- **Problem:** `std::ofstream(..., std::ios::trunc)` overwrites without warning. `CHANGES.md` says an `overwrite` flag was added; verify the executor refuses when `overwrite == false`.
- **Acceptance:**
  - [ ] Re-attaching a script to an entity that already has the same path fails unless the user confirms.
  - [ ] If Play is running, the file is locked, not truncated.
- **Effort:** 0.3 pd (mostly verification)

### T1-12 — Add a unit test framework + smoke tests [🟠 High, ❌ missing entirely]

- **Why:** The audit found 47 bugs; many were the same class of bug re-introduced across releases. No test exists today (acknowledged in `audit-2026-08-13.md` M8/M9). Without tests, the next 50 alphas will keep regressing.
- **Plan:**
  - Pick a test framework: Catch2 (header-only, MIT) or doctest (single header). Recommend Catch2 v3.
  - Add an `Engine/tests/` target.
  - For each Tier 1 ticket, add at least one regression test.
  - For the JSON parser, add a fixture covering all 6 E-Json bugs.
  - For the script runtime, add a Lua fixture with a `while true` infinite-loop test (proves `lua_sethook` is in place).
  - For scene save, add an atomic-save test that injects a kill mid-write.
- **Effort:** 1 pd setup + 0.1 pd per fixture
- **Acceptance:**
  - [ ] `ctest` runs in < 30 s.
  - [ ] CI runs on every push.
  - [ ] At least 1 test per Tier 1 ticket above.

### T1-13 — Add a CI pipeline [🟠 High, ❌ missing entirely]

- **Why:** M9 from the original audit. Without CI, "did the build break" is a human-polled question.
- **Plan:** GitHub Actions on push + PR: configure (CMake), build (Release), run tests, upload artifacts.
- **Effort:** 0.3 pd

**Tier 1 subtotal:** ~7 pd (~1.5 weeks).


---

## Tier 2 — core engine features

> These are the features where absence is felt by *every* user, every session.

### T2-1 — Multi-light lighting model [🔴 Critical visual gap]

- **Files:** `Engine/src/Editor/ViewportRenderer.cpp` (5 hardcoded light sites at lines 82-83, 195, 219, 294, 313), `Engine/include/GameForger/Editor/EditorScene.hpp`
- **Problem:** Single hardcoded `vec3(0.4, 0.85, 0.35)` directional light. No point lights, no spot lights, no area lights.
- **Fix:**
  - Add `Light { type, color, intensity, position/rotation, range, castsShadow }` to `SceneEntity` (or as a separate `LightData` flagged on).
  - Shader changes: loop over up to 8 lights per draw call, pass uniform array.
  - Inspector section for `Light`.
- **Effort:** 2-3 pd
- **Acceptance:**
  - [ ] At least 4 dynamic lights can be on simultaneously.
  - [ ] Spot light cone visible.
  - [ ] Inspector has "Add Light" / "Light" component.

### T2-2 — Real-time shadows [🔴 Critical visual gap]

- **Plan:** Start with a single shadow map for the directional light. Cascaded later.
- **Files:** `Engine/src/Editor/ViewportRenderer.cpp`, `Engine/src/Editor/PrimitiveMeshes.cpp`
- **Effort:** 5-7 pd
- **Acceptance:**
  - [ ] A cube sitting on a plane casts a visible shadow.
  - [ ] Shadow map resolution configurable in Quality settings.
  - [ ] Self-shadow acne mitigated (slope-scaled bias).

### T2-3 — PBR-style material [🟠 High visual gap]

- **Files:** `Engine/src/Editor/ViewportRenderer.cpp`, `Engine/include/GameForger/Editor/EditorScene.hpp`
- **Plan:** Extend the existing `materialLayers` to include metallic / smoothness / AO / emission / normal maps. Use Filament-style or LearnOpenGL PBR for the shader.
- **Effort:** 5-7 pd

### T2-4 — Tonemapping + sRGB output + MSAA [🟠 High visual gap]

- **Files:** `Engine/src/Editor/ViewportRenderer.cpp`
- **Plan:** Single post-process FBO with a tonemap pass (ACES), gamma-correct sRGB output, 4× MSAA.
- **Effort:** 1 pd
- **Acceptance:** Editor and Runtime both render through the same post-FBO.

### T2-5 — Real physics (rotation-aware AABB + sweep tests) [🔴 Critical gameplay gap]

- **Plan:** Either integrate Bullet3 (free, MIT) for production use, or hand-roll a proper physics system with:
  - OBB (rotation-aware AABB) for primitive colliders
  - Sphere, capsule, and box collider types
  - Swept tests (swept-AABB, sphere-sweep) so fast-moving objects don't tunnel
  - Triggers (no-collision, event-only volumes)
  - Joints (fixed, hinge, ball, spring) — Tier 4
  - Physics materials (friction, bounciness) — Tier 4
- **Files:** `Engine/src/Editor/ScriptRuntime.cpp` (`self.physics:resolve` becomes one of many physics bindings), `Engine/src/Runtime/GameplayLoop.cpp` (new `tickPhysics` step)
- **Effort:** 10-15 pd
- **Acceptance:**
  - [ ] A cube dropped onto a rotated platform still rests on it.
  - [ ] `self.physics:onTriggerEnter(other)` fires when entering a trigger volume.
  - [ ] Fast projectile no longer tunnels thin castle walls.

### T2-6 — Audio engine [🔴 Critical feature gap]

- **Plan:** Integrate `miniaudio` (single-file, public domain, ~30 KB). Drop-in:
  - `AudioSource { clipPath, volume, pitch, spatial, loop, playOnStart }`
  - `AudioListener { entity }` (one per scene)
  - `.wav` and `.ogg` import via stb_vorbis (one more header)
  - 3D positional audio
  - Play/stop from Lua via `self.audio:play(clipPath)`
- **Files:** New `Engine/src/Audio/`, new `Editor/src/Audio/`, `Engine/include/GameForger/Editor/EditorScene.hpp` (add `AudioSourceData` / `AudioListenerData`)
- **Effort:** 5-7 pd
- **Acceptance:**
  - [ ] A sound plays in 3D space at a placeholder.
  - [ ] `Game/Audio/.keep` is no longer empty.
  - [ ] Inspector has "Audio Source" / "Audio Listener" components.

### T2-7 — Placeable Camera component [🟠 High]

- **Files:** `Engine/include/GameForger/Editor/EditorScene.hpp`, `Editor/src/main.cpp:2374` (Game view panel)
- **Problem:** Acknowledged in README "Known gaps". Game view always uses a fixed default camera, not a selected one.
- **Plan:** Add `CameraData { fov, near, far, priority, isOrthographic, orthoSize }` to `SceneEntity`. Game view renders from the highest-priority active camera.
- **Effort:** 1 pd

### T2-8 — Trigger volumes (`OnTriggerEnter/Exit` for scripts) [🟠 High]

- **Files:** `Engine/src/Editor/ScriptRuntime.cpp` (add `self.physics:onTriggerEnter(other)` etc.)
- **Plan:** Extend physics tick to detect and dispatch trigger events. Tiny on top of T2-5.
- **Effort:** 1 pd

### T2-9 — Gamepad / controller support [🟠 High]

- **Files:** `Engine/include/GameForger/Editor/InputSource.hpp`, `Engine/src/Editor/GlfwInputSource.cpp`, `Editor/src/ImGuiInputSource.cpp`
- **Plan:** Add `isButtonDown(gamepadButton)`, `getAxis(gamepadAxis)`, plus a binding to the new Input System (T3-7).
- **Effort:** 2-3 pd

**Tier 2 subtotal:** ~35-50 pd (~7-10 weeks).


---

## Tier 3 — editor UX features

> These turn the editor from "developer tool I built" to "tool someone else can use."

### T3-1 — Prefabs [🔴 Most-missing-feature-ever]

- **Plan:** "Create Prefab" action snapshots a `SceneEntity` to `.prefab.json`; "Spawn Prefab" instantiates from that file. Drag-into-Hierarchy. Edit Prefab in isolation. Override per-instance.
- **Files:** New `Engine/src/Editor/Prefab.{hpp,cpp}`; new `Editor/src/Editor/PrefabPanel.{hpp,cpp}`
- **Effort:** 5-7 pd
- **Acceptance:**
  - [ ] Round-trip a 5-entity prefab through save → load → instantiate.
  - [ ] Per-instance overrides are preserved on re-instantiate.

### T3-2 — Visual scripting / event sheets [🔴 #1 unlock]

- **Plan:** A GDevelop-style "Behaviors" or "Events" panel. Per-entity list of `WHEN <condition> DO <action>` rows. Condition/action types defined in a registry. Initial set:
  - **Conditions:** KeyDown, KeyPressed, MouseDown, CompareVariable, DistanceToObject, OnTriggerEnter
  - **Actions:** Move, Rotate, Scale, SetActive, PlayAnimation, SpawnPrefab, FireProjectile, PlaySound, SetVariable
- **Files:** New `Engine/src/Editor/Behavior.{hpp,cpp}`, new `Editor/src/Editor/BehaviorPanel.{hpp,cpp}`
- **Effort:** 15-20 pd
- **Acceptance:**
  - [ ] A non-coder can build a "follow the player" enemy without touching Lua.
  - [ ] Behaviors are saved with the scene.

### T3-3 — Scene gizmo + camera speed slider [🟠 High polish]

- **Files:** `Engine/src/Editor/ViewportRenderer.cpp` or the Viewport ImGui panel
- **Plan:** Top-right axis widget (perspective/ortho toggle), bottom-right camera speed slider, "Bookmarks" with Ctrl+Shift+1..9.
- **Effort:** 1 pd

### T3-4 — Multi-edit Inspector [🟠 High UX]

- **Files:** `Editor/src/main.cpp:3842` (Inspector)
- **Plan:** When 2+ entities selected, show only fields they share; mixed values rendered with a "—" placeholder; setting writes to all.
- **Effort:** 3-5 pd

### T3-5 — Asset previews in the Project panel [🟡 Medium]

- **Files:** `Editor/src/main.cpp:2087` (Project panel)
- **Plan:** 3D model viewer (use the existing ViewportRenderer on a small framebuffer), material sphere, texture thumbnail.
- **Effort:** 3-5 pd

### T3-6 — Build target selection in "Build Game…" [🟠 High]

- **Files:** `Editor/src/main.cpp:1474` (Build Game menu), `New-GameForgerAIProject.ps1`, `Build-Project.cmd`
- **Plan:** At minimum: Windows x64 + x86. Bundle the .dll list. Smoke-test by spawning the Runtime on the built artifact.
- **Effort:** 3-5 pd
- **Acceptance:**
  - [ ] Build Game produces a runnable .exe in a separate folder.
  - [ ] Built .exe runs the Castle scene end-to-end.

### T3-7 — Input Action Map (rebindable controls) [🟡 Medium]

- **Files:** `Engine/include/GameForger/Editor/InputSource.hpp`, `Editor/src/main.cpp` (Settings window)
- **Plan:** A `Game/Input/ActionMap.json` that maps logical actions ("MoveForward") to physical inputs ("W", "GamepadLeftStickY+", "UpArrow"). Scripts read the logical name.
- **Effort:** 2-3 pd

### T3-8 — Command palette / search everywhere [🟠 High]

- **Files:** New `Editor/src/CommandPalette.{hpp,cpp}`
- **Plan:** Ctrl+P opens a text input that fuzzy-matches every menu item, every command, every entity, every asset. Enter executes.
- **Effort:** 1 pd
- **Acceptance:** Ctrl+P → "terrain" finds "GameObject > Terrain", "Toolbox > Create Terrain", and the entity named "Terrain".

### T3-9 — Project Settings expansion [🟡 Medium]

- **Files:** `Editor/src/main.cpp:1994` (Settings window)
- **Plan:** Add **Tags & Layers**, **Physics**, **Quality**, **Player** (splash, default scene, company name, version, icon), **Input Action Map** (T3-7).
- **Effort:** 2-3 pd

### T3-10 — Reset-to-default on every Inspector section [🟢 Low]

- **Files:** `Editor/src/main.cpp:3842`
- **Plan:** Add a "⋮" menu at the top of each section with "Reset to defaults."
- **Effort:** 0.5 pd

### T3-11 — Help tooltips on every field [🟢 Low]

- **Files:** everywhere there's an ImGui widget in the Inspector
- **Plan:** 1 line of `if (ImGui::IsItemHovered()) ImGui::SetTooltip("...")` per field, ~200 fields.
- **Effort:** 0.5 pd (mostly typing)

### T3-12 — Diffable scene format [🟡 Medium]

- **Files:** `Engine/src/Editor/SceneSerializer.cpp`
- **Plan:** Sort entity arrays by stable id; canonicalize JSON key order; one-entity-per-line for diffability. Optionally a YAML export for version control.
- **Effort:** 2 pd
- **Acceptance:** Two scenes differing only in entity positions produce a small, clean git diff.

### T3-13 — Editor themes (light + dark + custom accent) [🟢 Low]

- **Files:** `Editor/src/main.cpp` ImGui style block
- **Plan:** Three presets, switchable from Settings > Appearance.
- **Effort:** 0.5 pd (see Q-10)

### T3-14 — Hierarchy search/filter (already in Q-8) [🟡 Medium]

- **Effort:** 0.3 pd

**Tier 3 subtotal:** ~45-65 pd (~9-13 weeks).


---

## Tier 4 — bigger engine systems

> These are the ones a "real game" needs but that take weeks each.

### T4-1 — Particle system [🟠 High]

- **Plan:** A `ParticleEmitter { shape, rate, lifetime, velocity, color, size, texture }` component. Editor panel with curve editors for color/size/velocity over lifetime.
- **Files:** New `Engine/src/Runtime/Particles.{hpp,cpp}`, new `Editor/src/Editor/ParticleEditor.{hpp,cpp}`
- **Effort:** 10-15 pd

### T4-2 — NavMesh + pathfinding [🟠 High]

- **Plan:** Integrate Recast + Detour (both Apache-2.0). Build navmesh from terrain. `NavMeshAgent` component. `self.navmesh:findPath(to) -> list of waypoints`.
- **Files:** New `Engine/third_party/recast/`, new `Engine/src/Runtime/NavMesh.{hpp,cpp}`
- **Effort:** 10-15 pd

### T4-3 — Skeletal animation with bone UI, blend trees, IK [🟠 High]

- **Files:** `Engine/src/Editor/Animation.cpp`, `Engine/include/GameForger/Animation/`, new bone-hierarchy panel
- **Plan:** Bone list with select + pose. Animation clip selection (currently hard-coded to first imported clip). Blend trees (idle ↔ walk ↔ run). Inverse kinematics for foot placement.
- **Effort:** 30-40 pd

### T4-4 — Animation events + timeline with audio/activation tracks [🟡 Medium]

- **Files:** `Engine/src/Editor/Animation.cpp`, new `Editor/src/Editor/Timeline.{hpp,cpp}`
- **Plan:** Extend the existing Storyboard to a full director-style timeline: animation track + audio track + activation track + signal/event track. Footstep events triggered on bone-position crossings.
- **Effort:** 20-25 pd

### T4-5 — In-game UI canvas + UI events [🟠 High]

- **Files:** New `Engine/src/Runtime/UI/`, new `Editor/src/Editor/UIEditor.{hpp,cpp}`
- **Plan:** ImGui overlay during Play, or a 2D quad tree. Buttons, sliders, text, panels. `onClick`, `onHover` events. Inventory grid moves from a hard-coded panel to a real UI authoring system.
- **Effort:** 15-20 pd

### T4-6 — Frame Debugger + Profiler [🟡 Medium]

- **Files:** New `Engine/src/Runtime/Profiler.{hpp,cpp}`
- **Plan:** Record per-frame timings, draw call counts, GPU memory, draw as overlay.
- **Effort:** 10-15 pd

### T4-7 — ScriptableObject-equivalent (data assets) [🟡 Medium]

- **Files:** New `Engine/src/Editor/DataAsset.{hpp,cpp}`
- **Plan:** A `.data.json` file type that holds a named bundle of fields. Reusable across entities (e.g. one `SwordItem` data asset referenced by many `PickupItem` entities).
- **Effort:** 5-7 pd

### T4-8 — ProBuilder (in-editor mesh authoring) [🟡 Medium]

- **Files:** New `Engine/src/Editor/ProBuilder.{hpp,cpp}`
- **Plan:** Vertex / edge / face selection, extrude, inset, bevel, loop cut. Save as `.mesh.json`.
- **Effort:** 30+ pd (this is a feature on its own)

### T4-9 — Sprite / 2D pipeline [🟠 High — alt-engine scope]

- **Plan:** Sprite component, sprite animation, 9-patch, Tiled import. Sprite + 2D colliders + 2D physics.
- **Effort:** 30-60 pd (this is an entire alternate engine)

### T4-10 — Asset Store / package manager [🟠 High — community-scale]

- **Plan:** A `.gfpkg` format. A central registry. CLI to install/uninstall.
- **Effort:** 20+ pd of work + ongoing community

**Tier 4 subtotal:** ~180-260 pd (~9-13 months).

---

## Tier 5 — GDevelop-style authoring for non-coders

> T3-2 already does the minimum. This tier is the full GDevelop parity push.

### T5-1 — Variable system with types and authoring UI [🟠 High]

- **Plan:** `Variable { name, type: number|string|bool|structure, scope: global|scene|object, value }`. Authoring panel in the Settings window.
- **Files:** `Engine/include/GameForger/Editor/EditorScene.hpp`, `Editor/src/main.cpp` (Settings window)
- **Effort:** 5-7 pd

### T5-2 — Built-in behavior library [🟠 High]

- **Plan:** Ship 20+ pre-built behaviors: PlatformerCharacter, TopDownController, FollowTarget, Patrol, ShootAtTarget, LookAtCamera, FadeInOut, DestroyOnContact, TimedSelfDestruct, Health, Damage, Knockback, Pickup, DialogTrigger, DayNightCycle, WeatherController, AudioLoop, etc.
- **Files:** New `Game/Behaviors/`, integration with T3-2
- **Effort:** 1 pd per behavior × 20 = 20 pd

### T5-3 — Expression evaluator with autocomplete [🟡 Medium]

- **Plan:** Parse `1 + 2 * sin(self.x)`, evaluate at runtime, autocomplete in the field-editor.
- **Effort:** 5-7 pd

### T5-4 — Live preview in browser [🟡 Medium]

- **Plan:** WebGL2 backend for the renderer. Optional Emscripten build.
- **Effort:** 30+ pd (substantial)

**Tier 5 subtotal:** ~60-80 pd (~3-4 months).

---

## Tier 6 — long-term / multiplayer / platform

### T6-1 — Mobile / web export [🟡 Medium]

- **Plan:** Web (Emscripten + WebGL2), Android (via custom toolchain or a third-party), iOS (Xcode project generation).
- **Effort:** 30-60 pd per platform

### T6-2 — Multiplayer (P2P with relay, lobby, replication) [🟠 High]

- **Plan:** Integrate a netcode library (e.g. GameNetworkingSockets from Valve, MIT) or hand-roll. Server-authoritative state. RPC + replicated properties.
- **Effort:** 60-120 pd

### T6-3 — Cloud save [🟢 Low]

- **Plan:** A `self.save:cloud(slot)` Lua binding. Backend TBD.
- **Effort:** 5-10 pd

### T6-4 — Voice chat [🟢 Low]

- **Plan:** Integrate Vivox or LiveKit.
- **Effort:** 10-20 pd

**Tier 6 subtotal:** ~100-200 pd (~5-10 months).


---

## Suggested milestones

Each milestone should be a tagged release with a public "what's new" note. Don't ship a milestone without (a) all its tickets closed AND (b) the Tier 1 tests passing.

### Milestone 0.79 — "Solid foundation" (1.5 weeks)

- All 10 quick wins (Q-1 … Q-10)
- All of Tier 1 (T1-1 … T1-13)
- 1 test framework + 10 regression tests
- 1 CI pipeline
- README "Known gaps" updated to reflect what's now closed

### Milestone 0.85 — "Looks like a game" (4 weeks after 0.79)

- T2-1 multi-light
- T2-2 directional shadow
- T2-4 tonemap + sRGB + MSAA
- T2-7 placeable Camera
- T3-3 scene gizmo + camera speed
- T3-8 command palette
- T3-9 Project Settings: Tags & Layers, Quality

### Milestone 0.90 — "Plays like a game" (5 weeks after 0.85)

- T2-3 PBR material
- T2-5 rotation-aware physics (swept + triggers; save joints for next milestone)
- T2-6 audio
- T2-8 trigger volumes
- T2-9 gamepad support
- T3-4 multi-edit Inspector
- T3-6 build target selection

### Milestone 0.95 — "Authored like a game" (6 weeks after 0.90)

- T3-1 prefabs
- T3-2 visual scripting (initial 6 conditions × 6 actions)
- T4-1 particle system
- T5-1 variable system
- T5-2 first 5 built-in behaviors
- T3-12 diffable scenes

### Milestone 1.0 (Beta) — "Ship it" (8 weeks after 0.95)

- T3-5 asset previews
- T3-7 input action map
- T4-2 NavMesh
- T4-3 skeletal animation bone UI
- T5-3 expression evaluator
- All 20 built-in behaviors
- First-party "Starter" project template (FPS, Platformer, Top-down)
- Tutorial / first-run experience
- Public documentation site
- 100+ tests, CI green

### Post 1.0 — Indie game viability

- 2D pipeline (T4-9)
- Asset Store (T4-10)
- Web export (T5-4)
- Multiplayer (T6-2)

---

## Cross-reference: original audit tag → ticket

This table maps every finding in `audit-2026-08-13.md` to the ticket(s) in this plan that address it. Use it to make sure nothing is dropped.

| Audit tag | Plan ticket | Status |
|---|---|---|
| C1 Runtime won't build | (no ticket — claimed fixed in `CHANGES.md`) | 🟡 Verify |
| C2 Non-atomic save | (no ticket — claimed fixed) | 🟡 Verify |
| C3 WinHTTP 120s blocking | TBD-1 (add to Tier 1 if not fixed) | 🟡 Verify |
| C4 Provider parser string-search | T1-1 | 🟡 Partial fix |
| H1 Runtime doesn't run | T1-8 | ⚠️ Partial |
| H2 Script path traversal | T1-10 | ⚠️ Partial |
| H3 AI Settings not applied | T1-4 | 🟡 Partial fix |
| H4 Provider parser | T1-1 | 🟡 Partial fix |
| H5 Non-atomic save | T1-7 + (no ticket — claimed fixed) | 🟡 Verify |
| H-Script-1 getRight returns left | (no ticket — claimed fixed) | ✅ Verified fixed |
| H-Play-1 animation vs parent | (no ticket — claimed fixed) | 🟡 Verify in Castle scene |
| H-Render-1 all textures flipped | (no ticket — claimed fixed via `StbImageImpl`) | ✅ Verified fixed |
| H-Edit-1 AI anim bypasses undo | (no ticket — claimed fixed via `SetAnimationCommand`) | 🟡 Verify |
| H-Edit-2 Save Ctrl+R dead | (no ticket — claimed fixed) | ✅ Verified fixed |
| H-Edit-3 Undo/Redo menu inert | (no ticket — claimed fixed) | ✅ Verified fixed |
| H-Edit-4 Calibrate no-op | T1-4 | 🟡 Partial fix |
| H-Edit-5 Rename/Delete break parent link | TBD-2 | ❌ Open |
| H-Edit-6 AI worker exception strands flag | TBD-3 | ❌ Open |
| H-Render-2 capsule lighting | Q-1 / T1-2 | ❌ Open |
| H-Render-3 winding order | Q-2 / T1-3 | ❌ Open |
| H-Script-2 no Lua budget | (no ticket — claimed fixed via `lua_sethook`) | ✅ Verified fixed |
| M1 inert menu commands | (no ticket — claimed fixed) | 🟡 Verify |
| M2 Project.json decorative | (no ticket — claimed fixed) | 🟡 Verify |
| M3 JSON parser lax | T1-6 | ❌ Open |
| M4 Scene validation weak | T1-5 | ❌ Open |
| M5 Asset confinement incomplete | TBD-4 | ❌ Open |
| M6 JSON escaping incomplete | T1-7 | ❌ Open |
| M7 Versions disagree | TBD-5 | ❌ Open |
| M8 No automated tests | T1-12 | ❌ Open |
| M9 No CI | T1-13 | ❌ Open |
| M10 Shutdown blocks on AI | TBD-1 (same as C3) | ❌ Open |
| L4 Save bound to Ctrl+R | (no ticket — claimed fixed) | 🟡 Verify |
| N1 Skinned model root inverse | (no ticket — claimed fixed) | ✅ Verified fixed |
| N2 Bind-pose fallback | (no ticket — claimed fixed) | ✅ Verified fixed |
| E-Render-1 material cache leak | (no ticket — claimed fixed) | 🟡 Verify |
| E-Render-2 TextMesh hole wall normals | (no ticket — claimed fixed) | 🟡 Verify |
| E-Render-3 cone normal magic 0.5F | (no ticket — claimed fixed) | 🟡 Verify |
| E-Scene-1 loadScene no format check | T1-5 | ❌ Open |
| E-Json-1..6 | T1-6 | ❌ Open |
| E-Scene-2 escapeJson drops \r | T1-7 | ❌ Open |
| E-Script-1 startScript double-register | (no ticket — claimed fixed) | 🟡 Verify |
| E-Script-2 getScriptNumberField const | Q-5 | ❌ Open |
| E-Script-3 CreateScriptCommand overwrites | T1-11 | ⚠️ Partial |
| E-Scene-3 DuplicateEntityCommand no children | TBD-6 | ❌ Open |
| E-Validate-1 NaN/Inf accepted | T1-9 | ❌ Open |
| E-Validate-2 default branch silent approve | T1-9 (related) | ❌ Open |
| E-Validate-3 AttachScript vs CreateScript .. guard | (no ticket — claimed fixed) | 🟡 Verify |
| E-Scene-4 replaceEntities no id advance | (no ticket — claimed fixed) | 🟡 Verify |
| E-Animation-1 Euler lerp | (no ticket — claimed fixed) | 🟡 Verify |
| E-Animation-2 boundary `<=` | (no ticket — claimed fixed) | 🟡 Verify |
| E-Animation-3 imported AnimationClip looping | TBD-7 | ❌ Open |
| E-Run-1 win/lose banner missing in Runtime | T1-8 (related) | ❌ Open |
| E-Run-2 unclamped deltaTime | T1-8 | 🟡 Partial fix |
| E-Run-3 fast projectiles tunnel | T1-8 / T2-5 | ❌ Open |
| E-Run-4 followedEntity dangling | T1-8 | ❌ Open |
| E-Run-5 inventory round-trip | T1-8 | ❌ Open |
| E-Run-6 Load doesn't reset session | T1-8 | ❌ Open |
| E-Run-7 camera look not reset | T1-8 | ❌ Open |
| E-AI-1 unescaped model name | T1-1 (same root cause) | ❌ Open |
| E-AI-2 WinHTTP header char count | (no ticket — claimed fixed) | 🟡 Verify |
| E-UI-1 ScriptCreatorState 8 KiB hard cap | TBD-8 | ❌ Open |
| E-UI-2 GL 4.6 context not verified | (no ticket — claimed fixed) | 🟡 Verify |
| E-UI-3 isKeyPressed semantics | Q-4 | ❌ Open |
| E-UI-4 escapeJson in AI provider | T1-7 (same root cause) | ❌ Open |
| E-Script-4 luaEntitySetPosition silent miss | TBD-9 | ❌ Open |
| E-Script-5 Lua partial fail leaks globals | T1-10 | ❌ Open |
| E-Import-1 skinned import drops non-skinned | TBD-10 | ❌ Open |
| E-Import-2 InverseBindMatrix may be identity | TBD-11 | ❌ Open |
| E-Import-3 no recursion depth cap | TBD-12 | ❌ Open |
| E-Path-1 weakly_canonical no symlink resolve | TBD-13 | ❌ Open |
| E-Path-2 toLowerAscii ASCII-only | TBD-14 | ❌ Open |
| L1 makeUniqueName no bound | Q-6 | ❌ Open |
| L2 nameInUse empty | Q-7 | ❌ Open |
| L3 Skeleton::validate parent < -1 | TBD-15 | ❌ Open |
| L4 resolvePivotPreset assumes cube | TBD-16 | ❌ Open |
| L5 hardcoded XYZ Euler order | TBD-17 (doc-only) | ❌ Open |
| L6 boneMatrices not zeroed | TBD-18 | ❌ Open |
| L7 skinned normal `(M·S)⁻ᵀ` | TBD-19 | ❌ Open |
| L8 WinHTTP wstring size bytes | (no ticket — claimed fixed) | ✅ Verified fixed |
| L9 InputSource no mouse button | (no ticket — claimed fixed) | ✅ Verified fixed |
| L10 AIAnimationGenerator keyframe times | TBD-20 | ❌ Open |
| L11 cursorLockSuppressed in shared struct | TBD-21 | ❌ Open |
| L12 GameMenu phantom click | TBD-22 | ❌ Open |
| L13 Character menu items inert | Q-3 | ❌ Open |

**Open TBD tickets (24 items):** Add these to a Tier 1.5 batch — each is small (≤ 0.3 pd). Total ~5 pd.

---

## Quick reference: file-to-ticket map

| File | Tickets |
|---|---|
| `Engine/src/Editor/PrimitiveMeshes.cpp` | Q-1, Q-2, T1-2, T1-3 |
| `Engine/src/Editor/Json.cpp` | T1-6 |
| `Engine/src/Editor/SceneSerializer.cpp` | T1-5, T1-7, T3-12 |
| `Engine/src/Editor/EditorScene.cpp` | T1-11, T3-1, T4-7 |
| `Engine/src/Editor/ScriptRuntime.cpp` | T1-9, T1-10, T2-5, T2-8 |
| `Engine/src/Editor/Animation.cpp` | T4-3 |
| `Engine/src/Editor/ViewportRenderer.cpp` | T2-1, T2-2, T2-3, T2-4, T4-6 |
| `Engine/src/Editor/ModelImport.cpp` | TBD-10, TBD-11, TBD-12 |
| `Engine/src/Runtime/GameplayLoop.cpp` | T1-8, T2-5 |
| `Engine/include/GameForger/Editor/EditorScene.hpp` | T2-1, T2-6, T2-7, T5-1 |
| `Engine/include/GameForger/Editor/InputSource.hpp` | Q-4, T2-9, T3-7 |
| `Editor/src/AIProviderClient.cpp` | T1-1, T1-7 |
| `Editor/src/main.cpp` | Q-3, Q-8, Q-9, Q-10, T1-4, T1-8, T1-11, T2-6, T2-7, T3-3, T3-4, T3-5, T3-6, T3-8, T3-9, T3-10, T3-11, T3-13, T4-1, T4-5, T4-7 |
| `Runtime/src/main.cpp` | T1-8 |
| `Build-Project.cmd` / `New-GameForgerAIProject.ps1` | T3-6 |

---

## Closing note

This plan turns a 47-item audit into roughly 70 tickets across 6 tiers, with the highest-impact 13 of them forming Milestone 0.79 (≈ 1.5 weeks of focused work). The single biggest unlock for end users is **T3-2 visual scripting / event sheets** — it turns the editor from "tool a coder uses" into "tool a designer uses" and is the moat neither Unity nor GDevelop currently hold against an AI-native tool.

If a single thing should be cut from the plan to ship faster: cut **T4-9 (2D pipeline)** entirely. GameForgerAI's DNA is 3D-first; the 2D pipeline is a separate product.
