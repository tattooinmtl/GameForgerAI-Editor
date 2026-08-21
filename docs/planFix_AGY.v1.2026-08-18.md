# GameForgerAI — Master Fix & Unity 3D Parity Plan (`planFix_AGY.md`)

> **⚠️ SUPERSEDED as of 2026-08-21.** This file is now a historical reference, kept (not deleted) for traceability. The active source of truth is **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)**, which folds in every ticket below plus items recovered from `audit-2026-08-13.md`/`PlanFixAuditResults.md`/`IMPLEMENTATION_PLAN.md` that never made it into this file. Read the RoadMap first — come back here only for the original design rationale behind a specific T-series ticket.

**Date:** 2026-08-18  
**Document Version:** 1.0.0 (Unified Master Plan)  
**Derived From:** `audit-2026-08-13.md` (47 findings), `Compared.md` (Unity 3D parity specification), and `PlanFixAuditResults.md`  
**Current Engine Baseline:** Alpha 0.78 (~23 kLOC C++20, ImGui, OpenGL 4.6, Lua 5.4, Assimp, GLM)  
**Primary Objective:** Deliver a bullet-proof, production-grade 3D game engine and editor that resolves all known defects and achieves architectural, functional, and workflow parity with Unity 3D while preserving GameForgerAI's native AI Forge advantage.

---

## Table of Contents

1. [Architectural North Star: Unity 3D Parity](#1-architectural-north-star-unity-3d-parity)
2. [Quick Wins (Immediate Fixes & Low-Hanging Fruit)](#2-quick-wins-immediate-fixes--low-hanging-fruit)
2a. [Gauntlet Audit Remediation — 2026-08-21](#2a-gauntlet-audit-remediation--2026-08-21)
3. [Tier 1 — Core Defect Remediation & Test Infrastructure](#3-tier-1--core-defect-remediation--test-infrastructure)
4. [Tier 2 — Architecture Modernization (The Component Model & Asset Database)](#4-tier-2--architecture-modernization-the-component-model--asset-database)
5. [Tier 3 — Essential Engine Subsystems (Physics, Audio, Lighting, Materials)](#5-tier-3--essential-engine-subsystems-physics-audio-lighting-materials)
6. [Tier 4 — Editor Workflow & Viewport Parity](#6-tier-4--editor-workflow--viewport-parity)
7. [Tier 5 — Advanced Subsystems (UI Canvas, VFX, NavMesh, Animation Curves)](#7-tier-5--advanced-subsystems-ui-canvas-vfx-navmesh-animation-curves)
8. [Tier 6 — Platform Packaging & Build Pipeline](#8-tier-6--platform-packaging--build-pipeline)
9. [Release Milestones & Delivery Roadmap](#9-release-milestones--delivery-roadmap)
10. [Comprehensive Audit & Gap Traceability Matrix](#10-comprehensive-audit--gap-traceability-matrix)

---

## 1. Architectural North Star: Unity 3D Parity

To prevent technical debt and recurring regressions, GameForgerAI must transition from a hardcoded monolithic entity model to a modern, decoupled engine architecture:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      MODERN MODULAR ENGINE ARCHITECTURE                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                            GameObject / Entity                        │  │
│  │   • ID (uint32_t)       • Name (string)        • ActiveSelf (bool)    │  │
│  │   • Tags (vector)       • Layer (uint8_t)      • StaticFlags (uint8)  │  │
│  └───────────────────────────────────┬───────────────────────────────────┘  │
│                                      │ (Holds 1..N attached components)     │
│                                      ▼                                      │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                     Polymorphic Component Hierarchy                   │  │
│  ├───────────────────┬───────────────────┬───────────────────────────────┤  │
│  │ TransformComponent│ MeshRendererComp  │ LightComponent (Dir/Point/Spot│  │
│  ├───────────────────┼───────────────────┼───────────────────────────────┤  │
│  │ RigidbodyComponent│ ColliderComponent │ AudioSource / AudioListener   │  │
│  ├───────────────────┼───────────────────┼───────────────────────────────┤  │
│  │ CameraComponent   │ ScriptComponent   │ ParticleSystemComponent       │  │
│  └───────────────────┴───────────────────┴───────────────────────────────┘  │
│                                      │                                      │
│                                      ▼                                      │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                  Asset Database & GUID Resolver System                │  │
│  │   • .meta files for all assets (.png, .obj, .glb, .gfmat, .lua, .wav) │  │
│  │   • References stored as 128-bit UUIDs (resilient to moves & renames) │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Quick Wins (Immediate Fixes & Low-Hanging Fruit)

*Target Completion: Sprint Day 1 (Subtotal: ~1.8 person-days)*

| ID | Title / Task | Target File(s) | Description / Root Cause | Effort |
|---|---|---|---|---|
| **Q-1** | **Capsule Bottom Normals** | `Engine/src/Editor/PrimitiveMeshes.cpp:181` | Drop negation in `generateUvSphereShell` bottom-hemisphere call; fixes inside-out lighting. | 0.05 pd |
| **Q-2** | **Primitive Winding Order** | `Engine/src/Editor/PrimitiveMeshes.cpp` | Ensure right-hand vertex order matches outward normals on cube, sphere, cylinder, and cone. | 0.3 pd |
| **Q-3** | **Menu Placeholder Labels** | `Editor/src/main.cpp:1661-1662` | Mark `Map Humanoid Skeleton...` and `Animation Library...` explicitly disabled with tooltips. | 0.05 pd |
| **Q-4** | **Input Key Semantic Clarification** | `Engine/include/GameForger/Editor/InputSource.hpp` | Rename/alias `isKeyPressed` vs `isKeyDown` to distinguish single-frame triggers from continuous state. | 0.1 pd |
| **Q-5** | **Const Correctness in Lua Helper** | `Engine/src/Editor/ScriptRuntime.cpp:839` | Remove `const` from `getScriptNumberField` because Lua stack manipulation mutates VM state. | 0.02 pd |
| **Q-6** | **Unique Name Suffix Bound** | `Engine/src/Editor/EditorScene.cpp:683-697` | Cap suffix search at 10,000 to prevent degenerate infinite loops. | 0.05 pd |
| **Q-7** | **Reject Empty Entity Query** | `Engine/src/Editor/EditorScene.cpp:678-681` | Return `false` immediately for empty name queries in `nameInUse`. | 0.02 pd |
| **Q-8** | **Hierarchy Search Bar** | `Editor/src/main.cpp:3570` | Add real-time text filter bar to Hierarchy panel filtering entities by name and tag. | 0.3 pd |
| **Q-9** | **Viewport Grid Snapping Toggle** | `Editor/src/main.cpp:6489` | Expose ImGuizmo translation (1.0u), rotation (15°), and scale (0.1u) snapping toggles. | 0.2 pd |
| **Q-10** | **Simulation Pause & Step Controls** | `Editor/src/main.cpp:1680` | Add `Pause` and `Step 1 Frame` buttons next to `Play` / `Stop` in the main toolbar. | 0.4 pd |
| **Q-11** | **Entity Active Checkbox** | `Editor/src/main.cpp:3855`, `Engine/include/GameForger/Editor/EditorScene.hpp` | Add `bool activeSelf = true` to entity header to enable/disable rendering, scripts, and physics. | 0.3 pd |

---

## 2a. Gauntlet Audit Remediation — 2026-08-21

*Source: [`PROJECT_AUDIT_GAUNTLET_REPORT.md`](file:///C:/GameForgerAI-Editor/docs/PROJECT_AUDIT_GAUNTLET_REPORT.md) (external "Antigravity AI Code Auditor" gauntlet run against Alpha 0.78). Task split and full instructions for the mechanical items: [`Gemini_todo_list.md`](file:///C:/GameForgerAI-Editor/docs/Gemini_todo_list.md). Session changelog: [`planFix_AGY_CHANGELOG.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY_CHANGELOG.md) (2026-08-21 entries).*

| ID | Title | Target File(s) | Owner | Status |
|---|---|---|---|---|
| **DEF-01** | `all-release` build broken by upstream `imguizmo` CMake target | `CMakeLists.txt` | Claude | 🟢 Done |
| **DEF-02** | `Project.json` truncated on write (no atomic staging) | `Editor/src/main.cpp` | Claude | 🟢 Done |
| **DEF-03** | Model/font/texture paths bypass `resolveProjectFile` (traversal) | `Engine/src/Editor/ViewportRenderer.cpp` | Claude | 🟢 Done |
| **DEF-04** | `[[nodiscard]]` C4834 warnings on discarded `AICommandResult` (~70+ sites) | `ScriptRuntime.cpp`, `GameplayLoop.cpp`, `Editor/src/main.cpp`, `Runtime/src/main.cpp` | Gemini | ⚪ Assigned |
| **DEF-05** | JSON parser: stray `+` prefix, unverified `strtod` consumption, no UTF-16 surrogate pairs | `Engine/src/Editor/Json.cpp` | Claude | ⚪ Not started |
| **DEF-06** | CI only builds Debug, no Release matrix | `.github/workflows/ci.yml` | Gemini | ⚪ Assigned |
| **DEF-07** | Lua state has no memory-budgeted allocator | `Engine/src/Editor/ScriptRuntime.cpp` | Claude | ⚪ Not started |
| **DEF-08** | `Game/AI/Providers.local.json` stores plaintext API keys | `.gitignore`, `Editor/src/main.cpp` (load path) | Gemini | ⚪ Assigned |

---

## 3. Tier 1 — Core Defect Remediation & Test Infrastructure

*Target Completion: Milestone 0.79 (~7.5 person-days)*

### T1-1 — Replace String-Search AI Provider Parser with Structured JSON
* **Files:** `Editor/src/AIProviderClient.cpp:37-58, 94-128`, `Editor/src/AIChatResponse.cpp`
* **Defect:** `parseProvider` searches raw JSON text with `find("\"id\": \"" + providerId + "\"")`, misparsing descriptions containing "endpoint" and breaking on escaped quotes.
* **Fix:** Parse structurally via `gameforger::editor::json::Value`; walk `providers[]`, `choices[0].message.content`, and `error.message`.
* **Acceptance Criteria:**
  * [ ] URLs with escaped quotes and descriptions containing keywords parse without error.
  * [ ] Provider test requests send properly formatted JSON request bodies.
* **Effort:** 0.5 pd

### T1-2 — AI Provider Live Calibration & UI Override Plumbing
* **Files:** `Editor/src/main.cpp:1771-1850`, `Editor/src/AIProviderClient.cpp`
* **Defect:** "Calibrate / Test provider" button previously flipped local UI flags without verifying endpoint connectivity. Edits to endpoint/model in UI were ignored by the client.
* **Fix:** Send a lightweight ping request; plumb UI overrides through to `AIProviderClient`.
* **Acceptance Criteria:**
  * [x] Clicking Calibrate sends an HTTP test and reports true status code and response time.
  * [x] Session overrides for endpoint and model are applied immediately.
* **Effort:** 0.8 pd

### T1-3 — Strict Scene Validation & Format Versioning
* **Files:** `Engine/src/Editor/SceneSerializer.cpp:566-585`
* **Defect:** `loadScene` does not check `"format": "GameForgerScene"` or `"version"`. Passing arbitrary JSON files (e.g. `Project.json`) corrupts active scene state.
* **Fix:** Verify format identifier and version header before mutating scene; reject invalid schemas gracefully.
* **Acceptance Criteria:**
  * [x] Loading non-scene JSON displays an error in Console without altering current scene.
* **Effort:** 0.3 pd

### T1-4 — JSON Parser Hardening & UTF-8 Escapes
* **Files:** `Engine/src/Editor/Json.cpp`, `Engine/src/Editor/SceneSerializer.cpp:306-323`
* **Defect:** Parser accepts trailing garbage after documents, allows raw control chars (U+0000–U+001F), lacks 4-byte UTF-8 surrogate decoding, and drops `\r`.
* **Fix:** Full JSON specification compliance: strict EOF check, complete escape table (`\b`, `\f`, `\n`, `\r`, `\t`, `\"`, `\\`), and UTF-8 surrogate pair reconstruction.
* **Acceptance Criteria:**
  * [x] All JSON conformance tests pass.
* **Effort:** 0.6 pd

### T1-5 — Gameplay Loop & Runtime State Isolation
* **Files:** `Runtime/src/main.cpp`, `Engine/src/Runtime/GameplayLoop.cpp`
* **Defect:** Fast projectiles tunnel through colliders; deleting entities mid-frame leaves dangling `followedEntity` pointers; `Load` does not reset camera yaw/pitch or session flags.
* **Fix:** Implement swept sphere-vs-box tests; re-resolve entity pointers after script ticks; reset all session state on scene load.
* **Acceptance Criteria:**
  * [x] Projectiles at 30+ units/sec reliably collide with 1-unit walls.
  * [x] Reloading a save cleanly resets camera orientation and interaction state.
* **Effort:** 1.5 pd

### T1-6 — Lua Sandbox Isolation & Global Leak Prevention
* **Files:** `Engine/src/Editor/ScriptRuntime.cpp:721-726`
* **Defect:** When a script encounters a runtime error during chunk loading, top-level globals set prior to the error leak into `_G` and pollute other scripts.
* **Fix:** Isolate each script instance within its own unique environment table (`_ENV`) with a metatable pointing to approved safe globals.
* **Acceptance Criteria:**
  * [x] Errored scripts cannot contaminate `_G` or other running script instances.
* **Effort:** 0.5 pd

### T1-7 — Automated Test Framework & CI Pipeline
* **Files:** `CMakeLists.txt`, new `Engine/tests/`, new `.github/workflows/ci.yml`
* **Implementation:** Integrate Catch2 v3; register CTest test suite; build automated GitHub Actions workflow for Debug/Release on Windows.
* **Acceptance Criteria:**
  * [x] `ctest` runs locally and on CI on every push with zero test failures.
  * [x] Includes regression tests for JSON parsing, primitive normals, winding, and script sandboxing.
* **Effort:** 1.5 pd

---

## 4. Tier 2 — Architecture Modernization (The Component Model & Asset Database)

*Target Completion: Milestone 0.85 (~15 person-days)*

### T2-1 — Entity-Component Architecture Refactor
* **Files:** `Engine/include/GameForger/Core/Component.hpp`, `Engine/include/GameForger/Core/GameObject.hpp`, `Engine/include/GameForger/Editor/EditorScene.hpp`, `Editor/src/main.cpp`
* **Design:**
  * Replace monolithic union struct in `SceneEntity` with `GameObject` containing `std::vector<std::unique_ptr<Component>>`.
  * Standard components: `TransformComponent`, `MeshFilterComponent`, `MeshRendererComponent`, `ColliderComponent`, `ScriptComponent`, `CameraComponent`, `LightComponent`, `AudioSourceComponent`.
  * Support multiple components of the same type (e.g. multiple colliders or audio sources).
* **Acceptance Criteria:**
  * [x] Inspector renders components dynamically from the entity's component list.
  * [x] Searchable "Add Component" popup menu allows attaching any registered component.
* **Effort:** 6.0 pd

### T2-2 — Inspector Script Parameter Reflection (`[SerializeField]` Equivalent)
* **Files:** `Engine/src/Editor/ScriptRuntime.cpp`, `Editor/src/main.cpp`
* **Design:**
  * Parse structured doc-comments or Lua schema blocks in attached scripts:
    ```lua
    -- @property speed: number = 8.5 [1.0, 20.0] "Movement speed"
    -- @property jumpForce: number = 12.0 "Initial jump velocity"
    -- @property isInvulnerable: boolean = false "God mode toggle"
    -- @property soundEffect: string = "Game/Audio/jump.wav"
    ```
  * Inspector automatically generates draggable sliders, input fields, checkboxes, and asset drop zones for these properties without altering C++ code.
* **Acceptance Criteria:**
  * [x] Editing a script property in the Inspector instantly updates the Lua instance variable at runtime.
* **Effort:** 3.0 pd

### T2-3 — GUID-Based Asset Database & `.meta` File Pipeline
* **Files:** `Engine/src/Core/AssetDatabase.hpp`, `Engine/src/Core/AssetDatabase.cpp`
* **Design:**
  * Every asset file under `Game/` receives an associated `.meta` file containing a 128-bit UUID and import settings.
  * Scenes and components store asset references by UUID rather than raw relative paths.
  * Renaming or moving assets inside the project updates paths seamlessly without broken references.
* **Acceptance Criteria:**
  * [x] Moving a texture or model between folders does not break scene references.
* **Effort:** 4.0 pd

### T2-4 — Standalone Material Asset System (`.gfmat`)
* **Files:** `Engine/include/GameForger/Core/Material.hpp`, `Engine/src/Core/Material.cpp`
* **Design:**
  * Material properties decoupled from entities into reusable `.gfmat` JSON files.
  * Supports Albedo, Normal, Metallic, Roughness, Ambient Occlusion, Emission, and UV Tiling/Offset.
  * Multiple entities can reference the same `.gfmat`; edits in Inspector update all instances.
* **Acceptance Criteria:**
  * [x] Creating a material asset and assigning it to 5 objects allows one-click shared color/roughness edits.
* **Effort:** 2.0 pd

---

## 5. Tier 3 — Essential Engine Subsystems (Physics, Audio, Lighting, Materials)

*Target Completion: Milestone 0.90 (~20 person-days)*

### T3-1 — Multi-Light Shading & Real-Time Shadow Mapping
* **Files:** `Engine/src/Editor/ViewportRenderer.cpp`, `Engine/include/GameForger/Editor/LightComponent.hpp`
* **Design:**
  * Implement `LightComponent` supporting Directional, Point, and Spot light types with customizable Color, Intensity, Range, and Spot Angle.
  * Render directional shadow map (2048x2048 FBO depth texture) with percentage-closer filtering (PCF) and slope-scaled depth bias.
  * Upgrade forward GLSL shader to loop over up to 8 active dynamic lights.
* **Acceptance Criteria:**
  * [ ] Objects cast real-time shadows onto terrain and other scene objects.
  * [ ] Placing point lights creates localized illumination falloff.
* **Effort:** 5.0 pd

### T3-2 — Standard PBR Shading Pipeline
* **Files:** `Engine/src/Editor/ViewportRenderer.cpp`
* **Design:**
  * Replace custom Lambert/Blinn-Phong with standard Cook-Torrance GGX microfacet PBR model.
  * Inputs: Albedo, Normal (tangent space), Metallic, Roughness, Ambient Occlusion, Emission.
  * Includes Reinhard/ACES tonemapping pass and gamma-correct sRGB framebuffer output.
* **Acceptance Criteria:**
  * [ ] Metallic and dielectric surfaces render with physically accurate reflections and specular highlights.
* **Effort:** 4.0 pd

### T3-3 — True 3D Physics Engine Integration (Jolt / Bullet3)
* **Files:** `Engine/src/Physics/PhysicsWorld.cpp`, `Engine/include/GameForger/Physics/`
* **Design:**
  * Embed Jolt Physics (MIT) or Bullet3 for robust Newtonian mechanics.
  * Components: `RigidbodyComponent` (Mass, Drag, Gravity, Kinematic, Constraints X/Y/Z), `BoxColliderComponent`, `SphereColliderComponent`, `CapsuleColliderComponent`, `MeshColliderComponent`.
  * Physics materials: Static Friction, Dynamic Friction, Restitution (Bounciness).
  * Trigger volumes with script callbacks: `onTriggerEnter(other)`, `onCollisionEnter(other)`.
* **Acceptance Criteria:**
  * [ ] Rotated boxes and complex colliders collide accurately without AABB axial drift.
  * [ ] Rigidbodies tumble, bounce, and settle realistically on terrain.
* **Effort:** 7.0 pd

### T3-4 — Spatial 3D Audio Subsystem (`miniaudio`)
* **Files:** `Engine/src/Audio/AudioEngine.cpp`, `Engine/include/GameForger/Audio/`
* **Design:**
  * Embed `miniaudio` (single-file library) for multi-channel cross-platform audio playback.
  * Components: `AudioSourceComponent` (Clip, Volume, Pitch, Loop, Spatial 3D Blend, Min/Max Distance), `AudioListenerComponent`.
  * Script API: `self.audio:play("Game/Audio/sfx.wav")`, `self.audio:setVolume(0.8)`.
* **Acceptance Criteria:**
  * [ ] 3D sounds attenuate smoothly based on listener distance and pan stereo channels based on angle.
* **Effort:** 3.0 pd

### T3-5 — Placeable Camera Component & Multi-Camera Management
* **Files:** `Engine/include/GameForger/Camera/CameraComponent.hpp`, `Editor/src/main.cpp`
* **Design:**
  * `CameraComponent` supporting FOV (10°–120°), Near/Far clip planes, Clear flags (Skybox/Solid Color), Perspective vs Orthographic toggle, and Priority.
  * Game View renders from the highest-priority active camera component in the scene.
* **Acceptance Criteria:**
  * [ ] Modifying camera FOV or projection in Inspector updates Game View perspective in real time.
* **Effort:** 1.0 pd

---

## 6. Tier 4 — Editor Workflow & Viewport Parity

*Target Completion: Milestone 0.95 (~16 person-days)*

### T4-1 — Complete Top-Level Menu Bar Structure
* **Files:** `Editor/src/main.cpp`
* **Implementation:**
  * **File:** New Scene, Open Scene, Save Scene (`Ctrl+S`), Save Scene As, Build Settings (`Ctrl+Shift+B`), Exit.
  * **Edit:** Undo (`Ctrl+Z`), Redo (`Ctrl+Y`), Cut (`Ctrl+X`), Copy (`Ctrl+C`), Paste (`Ctrl+V`), Duplicate (`Ctrl+D`), Delete (`Del`), Frame Selected (`F`), Select All (`Ctrl+A`), Project Settings, Preferences.
  * **Assets:** Create (Folder, Script, Material, Scene), Show in Explorer, Reimport All, Refresh (`Ctrl+R`).
  * **GameObject:** Create Empty, Create Empty Child, 3D Objects (Cube, Sphere, Capsule, Cylinder, Plane, Terrain, TextMesh), Lights (Directional, Point, Spot), Audio (Audio Source, Audio Listener), Camera, Align with View (`Ctrl+Shift+F`), Move to View (`Ctrl+Alt+F`).
  * **Component:** Categorized popup for attaching components to selected entity.
  * **Window:** Panels toggle checklist (Hierarchy, Inspector, Project, Viewport, Game, Console, Animation, AI Forge), Layout Presets (Default, 2 by 3, 4 Split, Reset).
  * **Help:** Documentation links, About GameForgerAI modal.
* **Effort:** 2.5 pd

### T4-2 — Viewport Tooling & Navigation Gizmos
* **Files:** `Editor/src/main.cpp`, `Engine/src/Editor/ViewportRenderer.cpp`
* **Features:**
  * **Coordinate Space Toggle:** Switch between `Global` (World) and `Local` transform gizmos.
  * **Pivot vs. Center Toggle:** Switch between object geometric center and authored pivot point.
  * **Snapping Suite:** Grid snapping increments + Vertex snapping (hold `V` key to snap vertices).
  * **3D Orientation View Cube:** Interactive orientation axis widget in top-right corner (click X/Y/Z for isometric orthographic alignment).
  * **Render Modes Dropdown:** Shaded, Wireframe, Shaded Wireframe, Overdraw, Normals.
  * **Scene Statistics Overlay:** On-screen HUD displaying FPS, Frame time, Draw calls, Triangles, Vertices.
* **Effort:** 3.5 pd

### T4-3 — Hierarchy UX Enhancements
* **Files:** `Editor/src/main.cpp:3561-3825`
* **Features:**
  * Sibling reordering via drag-and-drop within the same hierarchy branch.
  * Active/Inactive checkbox next to entity names in tree.
  * Scene Visibility (Eye icon) to hide/unhide objects in Viewport without destroying them.
  * Selection lock (Padlock icon) to prevent accidental clicking in Viewport.
* **Effort:** 2.0 pd

### T4-4 — Game View Aspect Ratio Constraints & Simulation Tooling
* **Files:** `Editor/src/main.cpp:2364-2900`
* **Features:**
  * Aspect Ratio Dropdown: Free Aspect, 16:9 (1920x1080), 16:10, 4:3, 21:9 Ultra-Wide, Standalone Windowed.
  * Fixed resolution rendering with letterbox/pillarbox bars.
  * `Maximize on Play` toggle.
  * `Mute Audio` toggle in Game View header.
* **Effort:** 2.0 pd

### T4-5 — Multi-Object Inspector Editing & Component Context Actions
* **Files:** `Editor/src/main.cpp:3842`
* **Features:**
  * Multi-selection property broadcasting: modifying a field applies changes to all selected entities simultaneously; displays "—" for mixed values.
  * Component Header Context Menu: `Reset to Defaults`, `Remove Component`, `Move Up`, `Move Down`, `Copy Component`, `Paste Component Values`.
* **Effort:** 3.0 pd

### T4-6 — Prefab System (`.prefab.json`)
* **Files:** `Engine/src/Editor/Prefab.cpp`, `Editor/src/main.cpp`
* **Features:**
  * Dragging an entity from Hierarchy into Project panel creates a `.prefab.json` asset.
  * Dragging a Prefab asset into Viewport/Hierarchy spawns linked instances.
  * Prefab updates propagate to instances while maintaining local property overrides.
* **Effort:** 3.0 pd

---

## 7. Tier 5 — Advanced Subsystems (UI Canvas, VFX, NavMesh, Animation Curves)

*Target Completion: Milestone 1.0 (~35 person-days)*

### T5-1 — In-Game 2D UI Canvas & RectTransform System
* **Files:** `Engine/src/UI/Canvas.cpp`, `Engine/include/GameForger/UI/`
* **Features:**
  * `CanvasComponent` (Screen Space Overlay, Screen Space Camera, World Space).
  * `RectTransformComponent` with Anchors (Min/Max), Pivots, and Pixel Offsets.
  * UI Components: `UIImageComponent`, `UITextMeshProComponent`, `UIButtonComponent`, `UISliderComponent`, `UIPanelComponent`.
  * Visual interactive authoring in Scene View using the Rect Tool (`T`).
* **Effort:** 8.0 pd

### T5-2 — Shuriken-Style Particle System (`ParticleSystemComponent`)
* **Files:** `Engine/src/VFX/ParticleSystem.cpp`, `Editor/src/VFX/ParticleEditor.cpp`
* **Features:**
  * Modules: Main (Duration, Start Lifetime, Speed, Size, Color, Gravity), Emission (Rate over time, Bursts), Shape (Sphere, Cone, Box, Circle), Velocity over Lifetime, Color over Lifetime, Size over Lifetime, Texture Sheet Animation, Renderer (Billboard / Mesh).
* **Effort:** 7.0 pd

### T5-3 — NavMesh Surface Baking & `NavMeshAgent` Pathfinding
* **Files:** `Engine/src/Navigation/NavMesh.cpp`, `Engine/include/GameForger/Navigation/`
* **Features:**
  * Integrate Recast & Detour (Apache-2.0).
  * Navigation window to bake walkable NavMesh from scene terrain and static colliders.
  * `NavMeshAgentComponent` (Speed, Angular Speed, Stopping Distance, Obstacle Avoidance).
  * Lua API: `self.agent:setDestination(targetPos)`, `self.agent:isPathStale()`.
* **Effort:** 7.0 pd

### T5-4 — Animator State Machine & Visual Curve Editor
* **Files:** `Engine/src/Animation/Animator.cpp`, `Editor/src/Animation/CurveEditor.cpp`
* **Features:**
  * Animator Controller graph: States, Transitions, Conditions (Floats, Bools, Triggers), Blend Trees (Idle ↔ Walk ↔ Run).
  * Dope Sheet and Bezier Curve Editor with tangible tangent handles for smooth easing curves.
* **Effort:** 8.0 pd

### T5-5 — Terrain Authoring Enhancements
* **Files:** `Engine/src/Editor/Terrain.cpp`, `Editor/src/main.cpp`
* **Features:**
  * Unlimited terrain texture layers (decoupling from 3-channel limit).
  * Smooth and Flatten brush modes.
  * Foliage & Tree placement brush with random scale/rotation jitter.
  * Detail mesh / procedural grass painting with wind sway shader.
* **Effort:** 5.0 pd

---

## 8. Tier 6 — Platform Packaging & Build Pipeline

*Target Completion: Milestone 1.0 (~8 person-days)*

### T6-1 — Dedicated Build Settings Window
* **Files:** `Editor/src/Build/BuildSettingsWindow.cpp`, `Editor/src/main.cpp`
* **Features:**
  * Multi-scene build inclusion list with drag-to-reorder for startup sequence.
  * Platform selector: Windows x64 Standalone (Phase 1), Linux x64 (Phase 2), WebGL / WebAssembly (Phase 3).
  * Toggles: `Development Build`, `Autoconnect Profiler`, `Script Debugging`.
* **Effort:** 3.0 pd

### T6-2 — Player Settings Configuration
* **Files:** `Editor/src/main.cpp` (Settings Window > Player Tab)
* **Features:**
  * Configurable Company Name, Product Name, Version String.
  * Custom Application Icon selector (auto-generates .ico sizes).
  * Splash screen image & background color settings.
  * Default Window Resolution (Fullscreen, Borderless Window, Windowed).
* **Effort:** 2.0 pd

### T6-3 — Standalone Executable & Asset Archive Bundler
* **Files:** `Runtime/CMakeLists.txt`, `New-GameForgerAIProject.ps1`
* **Features:**
  * Compiles assets into a packed, optimized binary archive (`game.gfdata`).
  * Produces a clean distribution folder containing only `MyGame.exe`, necessary `.dll`s, and `game.gfdata`.
* **Effort:** 3.0 pd

---

## 9. Release Milestones & Delivery Roadmap

```
                               DELIVERY TIMELINE
┌─────────────────────────────────────────────────────────────────────────────┐
│ Milestone 0.79 — "Solid Foundation" (Sprint 1: ~1.5 Weeks)                  │
│   • Quick Wins Q-1 through Q-11                                             │
│   • Tier 1 Bug Fixes T1-1 through T1-6                                      │
│   • Catch2 Test Suite & GitHub Actions CI (T1-7)                            │
├─────────────────────────────────────────────────────────────────────────────┤
│ Milestone 0.85 — "Modern Architecture" (Sprint 2: ~3 Weeks)                 │
│   • Modular Entity-Component Model Refactor (T2-1)                          │
│   • Inspector Script Parameter Reflection (T2-2)                            │
│   • GUID-Based Asset Database & .meta Pipeline (T2-3)                        │
│   • Reusable Material Assets .gfmat (T2-4)                                  │
│   • Placeable Camera Component (T3-5)                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│ Milestone 0.90 — "Engine Core Power" (Sprint 3: ~4 Weeks)                   │
│   • Multi-Light & Real-Time Shadow Mapping (T3-1)                           │
│   • Standard PBR Shading & ACES Tonemapping (T3-2)                          │
│   • Jolt / Bullet3 3D Physics Engine Integration (T3-3)                    │
│   • miniaudio 3D Spatial Audio Subsystem (T3-4)                             │
│   • Top-Level Menus & Viewport Snapping / Orientation Cube (T4-1, T4-2)     │
├─────────────────────────────────────────────────────────────────────────────┤
│ Milestone 0.95 — "Workflow & Authoring" (Sprint 4: ~3.5 Weeks)              │
│   • Prefab System .prefab.json (T4-6)                                       │
│   • Hierarchy Visibility, Lock, & Reordering (T4-3)                         │
│   • Game View Aspect Ratio Constraints & Simulation Tools (T4-4)            │
│   • Multi-Object Inspector Editing (T4-5)                                   │
│   • Terrain Smooth/Flatten & Foliage Painting (T5-5)                        │
├─────────────────────────────────────────────────────────────────────────────┤
│ Milestone 1.0 (Beta) — "Production Ready" (Sprint 5: ~4 Weeks)              │
│   • In-Game 2D UI Canvas & RectTransform (T5-1)                             │
│   • Shuriken Particle System (T5-2)                                         │
│   • Recast/Detour NavMesh & Pathfinding (T5-3)                              │
│   • Animator State Machine & Bezier Curve Editor (T5-4)                     │
│   • Build Settings & Standalone Distribution Packager (T6-1, T6-2, T6-3)    │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 10. Comprehensive Audit & Gap Traceability Matrix

This matrix maps every historical defect from `audit-2026-08-13.md`, `PlanFixAuditResults.md`, and `Compared.md` to the resolving ticket in this master plan.

| Source ID | Finding Description | Resolving Ticket in `planFix_AGY.md` | Target Milestone |
|---|---|---|---|
| **C1** | Runtime header mismatch | Fixed in codebase / Verified in T1-7 | M0.79 |
| **C2 / H5** | Non-atomic scene file saves | T1-3 (Atomic file replace + Schema validation) | M0.79 |
| **C3 / M10**| WinHTTP 120s blocking UI thread | T1-1, T1-2 (Cancellable worker handles + 15s timeout) | M0.79 |
| **C4 / H4** | Provider parser substring search | **T1-1** (Structured JSON parsing) | M0.79 |
| **H-Render-1**| STB textures vertically inverted | Verified fixed / Unit tested in T1-7 | M0.79 |
| **H-Render-2**| Capsule bottom hemisphere normals | **Q-1** (Drop negation) | M0.79 |
| **H-Render-3**| Primitive winding order inverted | **Q-2** (Right-hand vertex order match) | M0.79 |
| **H-Script-1**| `getRight` returns left vector | Verified fixed / Unit tested in T1-7 | M0.79 |
| **H-Script-2**| No Lua instruction budget / timeout | Verified fixed (`lua_sethook`) / Tested in T1-7 | M0.79 |
| **H1 / E-Run**| Runtime state / projectile tunneling | **T1-5** (Swept sphere tests + State reset) | M0.79 |
| **H2 / T1-10**| Script traversal / Lua global leak | **T1-6** (Sandboxed per-script `_ENV`) | M0.79 |
| **H3 / T1-4** | AI settings calibration no-op | **T1-2** (Live HTTP ping + Session override) | M0.79 |
| **DEF-01** | `all-release` broken (upstream `imguizmo` target) | **DEF-01** (`EXCLUDE_FROM_ALL`, see §2a) | M0.79 |
| **DEF-02** | `Project.json` truncated on write | **DEF-02** (Atomic tmp+`.bak`+rename, see §2a) | M0.79 |
| **DEF-03** | Model/font/texture path traversal | **DEF-03** (`resolveProjectFile` confinement, see §2a) | M0.79 |
| **DEF-04** | `[[nodiscard]]` C4834 warnings | **DEF-04** (see §2a, assigned to Gemini) | M0.79 |
| **DEF-05** | JSON parser number/surrogate-pair gaps | **DEF-05** (see §2a) | M0.79 |
| **DEF-06** | CI missing Release build matrix | **DEF-06** (see §2a, assigned to Gemini) | M0.79 |
| **DEF-07** | Lua state has no memory-budgeted allocator | **DEF-07** (see §2a) | M0.85 |
| **DEF-08** | Plaintext API keys in `Providers.local.json` | **DEF-08** (see §2a, assigned to Gemini) | M0.79 |
| **M1** | Inert menu items | **Q-3**, **T4-1** (Complete menu wire-up) | M0.79 / M0.90 |
| **M3 / E-Json**| JSON parser lax / escape flaws | **T1-4** (RFC compliant parser hardening) | M0.79 |
| **M4 / E-Scene**| Scene validation weak | **T1-3** (Strict versioning & schema check) | M0.79 |
| **M8 / M9** | Missing automated tests & CI | **T1-7** (Catch2 + GitHub Actions CI) | M0.79 |
| **GAP-ARCH-1**| Monolithic `SceneEntity` struct | **T2-1** (Modular Component Architecture) | M0.85 |
| **GAP-ARCH-2**| Raw string asset paths | **T2-3** (GUID Asset Database & `.meta` files) | M0.85 |
| **GAP-ARCH-3**| No script Inspector parameters | **T2-2** (Inspector Script Reflection) | M0.85 |
| **GAP-REND-1**| No PBR materials / `.mat` assets | **T2-4**, **T3-2** (PBR Shader & `.gfmat` assets) | M0.85 / M0.90 |
| **GAP-LGT-1** | Single hardcoded light / No shadows | **T3-1** (Multi-Light & PCF Shadow Maps) | M0.90 |
| **GAP-PHYS-1**| AABB box pusher / No real physics | **T3-3** (Jolt/Bullet3 Physics & Rigidbodies) | M0.90 |
| **GAP-AUD-1** | No audio engine | **T3-4** (`miniaudio` 3D Spatial Audio) | M0.90 |
| **GAP-CAM-1** | Fixed camera modes only | **T3-5** (Placeable Camera Components) | M0.85 |
| **GAP-UI-1**  | Incomplete Top Menu Bar | **T4-1** (Standard File/Edit/Assets/GameObject/Window) | M0.90 |
| **GAP-VIEW-1**| Missing Viewport Snaps/Cube/Modes | **T4-2** (Snapping, View Cube, Render Modes) | M0.90 |
| **GAP-GAME-1**| Game View missing Pause/Aspect Ratio| **Q-10**, **T4-4** (Pause/Step & Aspect Ratio Selector) | M0.79 / M0.95 |
| **GAP-HIER-1**| Hierarchy missing Search/Active/Order| **Q-8**, **Q-11**, **T4-3** (Search, Active, Sibling Sort)| M0.79 / M0.95 |
| **GAP-PREF-1**| No Prefabs | **T4-6** (Prefab `.prefab.json` System) | M0.95 |
| **GAP-2DUI-1**| No in-game 2D UI Canvas | **T5-1** (Canvas, RectTransform, UI Widgets) | M1.0 |
| **GAP-VFX-1** | No Particle System | **T5-2** (Shuriken-style Particle Emitter) | M1.0 |
| **GAP-NAV-1** | No NavMesh / Pathfinding | **T5-3** (Recast/Detour NavMesh & Agents) | M1.0 |
| **GAP-ANIM-1**| No State Machine / Curve Editor | **T5-4** (Animator Controller & Bezier Curves) | M1.0 |
| **GAP-BLD-1** | No Build Settings / Dist Packager | **T6-1**, **T6-2**, **T6-3** (Standalone Packager) | M1.0 |

---

*End of Master Plan (`planFix_AGY.md`).*
