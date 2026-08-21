# GameForgerAI Editor vs. Unity 3D — Comprehensive Comparative Audit & Parity Specification

**Audit Date:** 2026-08-18  
**Target Document:** `Compared.md` (GameForgerAI-Editor Root)  
**Baseline Compared:** GameForgerAI Editor (`Editor/src/main.cpp`, `Engine/`, `Runtime/`) vs. Unity 3D (Unity LTS / Unity 6 Editor Specification)  
**Objective:** Complete comparative evaluation of all editor options, workflows, menus, components, and engine subsystems, detailing the exact gaps required to bring GameForgerAI Editor to functional and workflow parity with Unity 3D.

---

## 1. Executive Summary & Parity Scorecard

GameForgerAI Editor is an engine and authoring environment featuring a native ImGui-based dockable desktop application, an integrated AI Forge command planner, procedural animation generation, and Lua-based gameplay scripting. 

While GameForgerAI provides an integrated **AI-first workflow** (natural language scene planning, automated Lua script generation, and AI transform animation), its underlying **engine architecture, editor options, and subsystem depth remain in a pre-alpha/prototype state** when compared to the industry-standard Unity 3D editor.

### Feature Parity Overview

```
                               PARITY PROGRESSION
┌─────────────────────────────────────────────────────────────────────────────┐
│ Subsystem                        Parity Level   Status                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ Top-Level Menus & Navigation     [██░░░░░░░░]   20%    Major gaps           │
│ Project & Asset Management       [███░░░░░░░]   30%    No GUIDs / Prefabs   │
│ Hierarchy & Scene Graph          [█████░░░░░]   50%    Basic parenting only │
│ Scene View / Viewport Tooling    [████░░░░░░]   40%    Missing modes/snaps  │
│ Game View & Playmode Simulation  [████░░░░░░]   40%    No pause/stats/ratio │
│ Inspector & Component System     [██░░░░░░░░]   20%    Monolithic struct    │
│ Rendering & Material System      [██░░░░░░░░]   20%    No PBR / Shaders     │
│ Lighting & Shadowing Pipeline    [█░░░░░░░░░]   10%    No light entities    │
│ Camera Subsystem                 [███░░░░░░░]   30%    Rigid camera modes   │
│ Physics Subsystem (3D/2D)        [██░░░░░░░░]   20%    AABB-only box push   │
│ Audio Pipeline                   [░░░░░░░░░░]    0%    Non-existent         │
│ Animation & Rigging              [███░░░░░░░]   30%    No state machine/IK  │
│ Terrain System                   [█████░░░░░]   50%    Height/splat only    │
│ Scripting Engine & Lifecycle     [████░░░░░░]   40%    No exposed params    │
│ In-Game UI / Canvas System       [█░░░░░░░░░]   10%    No 2D canvas/anchors │
│ Particles & Visual Effects (VFX) [░░░░░░░░░░]    0%    Non-existent         │
│ Navigation & AI (NavMesh)        [█░░░░░░░░░]   10%    No NavMesh baker     │
│ Profiling & Diagnostic Tooling   [█░░░░░░░░░]   10%    Basic console only   │
│ AI-Assisted Authoring (AI Forge) [█████████░]   90%    Competitive edge     │
│ Build & Platform Packaging       [██░░░░░░░░]   20%    Single scene export  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Comprehensive Comparative Matrix

### 2.1 Top-Level Menus & Global Navigation

| Menu Category | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **File** | `New Scene`, `Open Scene`, `Save`, `Save As...`, `Save As Scene Template...`, `New Project...`, `Open Project...`, `Save Project`, `Build Settings...`, `Build and Run`, `Exit` | `New Scene`, `Open Scene...`, `Save Scene`, `Save Scene As...`, `Build Game...` | • No `New Project` / `Open Project` dialog in-editor<br>• No `Build Settings...` window (multi-scene selection, target platform switch)<br>• No `Build and Run` one-click workflow<br>• No `Save Project` (persisting global assets/settings independently of scene) |
| **Edit** | `Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Duplicate`, `Delete`, `Frame Selected`, `Find`, `Select All`, `Play / Pause / Step`, `Project Settings...`, `Preferences...`, `Shortcuts...`, `Clear All PlayerPrefs` | `Undo`, `Redo`, `Duplicate Selected`, `Delete Selected`, `Reset Editor Layout` | • Missing `Cut`, `Copy`, `Paste` for GameObjects & Components<br>• Missing `Select All`, `Invert Selection`<br>• Missing `Frame Selected` (`F`) in menu<br>• Missing `Project Settings...` modal/window with full engine configuration<br>• Missing `Preferences...` window (editor keybindings, colors, external tools/IDE)<br>• Missing `Shortcuts...` visual keybinding editor |
| **Assets** | `Create` (Folder, C# Script, Shader, Material, Prefab, Animation, Audio Mixer, etc.), `Show in Explorer`, `Open`, `Delete`, `Rename`, `Import New Asset...`, `Import Package`, `Export Package...`, `Find References in Scene`, `Select Dependencies`, `Refresh` (`Ctrl+R`), `Reimport`, `Reimport All` | *None* (No Assets menu exists) | • **Complete absence of `Assets` menu**<br>• No asset creation submenu<br>• No `Show in Explorer` shortcut<br>• No manual `Reimport` or `Refresh Asset Database`<br>• No asset dependency tracing |
| **GameObject** | `Create Empty`, `Create Empty Child`, `3D Object` (Cube, Sphere, Capsule, Cylinder, Plane, Quad, Terrain, Text - TextMeshPro, Ragdoll...), `2D Object` (Sprites, Tilemap, Physics), `Effects` (Particle System, Trail, Line), `Light` (Directional, Point, Spot, Area, Reflection Probe, Light Probe Group), `Audio` (Audio Source, Audio Reverb Zone), `Video` (Video Player), `UI` (Canvas, Text, Image, Button, Slider, etc.), `Camera`, `Set as first/last sibling`, `Move to View`, `Align with View`, `Align View to Selected` | `Cube`, `Sphere`, `Cylinder`, `Cone`, `Plane`, `Capsule` | • Missing `Create Empty` / `Create Empty Child`<br>• Missing `Light` instantiation menu<br>• Missing `Camera` instantiation menu (only CineCamera in Toolbox)<br>• Missing `Audio Source` / `Audio Listener`<br>• Missing `2D Objects` & `UI` creation submenus<br>• Missing `Move to View` (`Ctrl+Alt+F`) & `Align with View` (`Ctrl+Shift+F`) |
| **Component** | Searchable popup and categorized component list: `Mesh`, `Effects`, `Physics`, `Physics 2D`, `Navigation`, `Audio`, `Rendering`, `Miscellaneous`, `Scripts`, `New script...` | *None* (No Component menu exists) | • **Complete absence of `Component` menu**<br>• All properties are hardcoded into `SceneEntity` rather than being modular attachable components |
| **Window** | `Layouts` (Default, 2 by 3, 4 Split, Tall, Wide, Save/Delete Layout), `General` (Scene, Game, Hierarchy, Inspector, Project, Console), `Rendering` (Lighting, Occlusion Culling), `Animation` (Animation, Animator), `Audio` (Audio Mixer), `Analysis` (Profiler, Frame Debugger, Physics Debugger), `AI` (Navigation), `Package Manager` | *None* (Tabs are hardcoded in ImGui dockspace; only `Settings` button exists) | • Missing `Window` menu<br>• No dock layout preset saving/loading<br>• No toggles to reopen closed panels (if closed, requires Reset Layout)<br>• No diagnostic/profiling windows |
| **Help** | `About Unity...`, `Unity Manual`, `Scripting API Reference`, `Unity Connect`, `Report a Bug...`, `Check for Updates...` | *None* | • No `Help` menu, documentation link, API reference, or version info modal |

---

### 2.2 Project & Asset Management Architecture

```
Unity Asset Pipeline                   GameForgerAI Pipeline
┌─────────────────────────┐            ┌─────────────────────────┐
│ Asset File (.png/.fbx)  │            │ Asset File (.png/.obj)  │
└────────────┬────────────┘            └────────────┬────────────┘
             │                                      │ (Loose relative path)
             ▼                                      ▼
┌─────────────────────────┐            ┌─────────────────────────┐
│ .meta File (GUID)       │            │ Direct Hardcoded Path   │
└────────────┬────────────┘            │ (e.g. "Game/Models/...")│
             │                                      │
             ▼                                      ▼
┌─────────────────────────┐            ┌─────────────────────────┐
│ AssetDatabase Cache     │            │ No Cache / Reload on    │
│ (Import Settings/Bakes) │            │ Render Frame            │
└─────────────────────────┘            └─────────────────────────┘
```

| Capability | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Asset Addressing** | GUID-based meta files (`.meta`) track all assets, ensuring renames/moves never break references | Raw relative string paths (e.g., `Game/Textures/...`, `Game/Models/...`) | • Renaming or moving a file outside or inside breaks all scene references<br>• No persistent asset metadata or import setting files |
| **Project Browser UI** | Two-pane directory tree + file grid; Icon zoom slider; Breadcrumb bar; Asset preview thumbnails; Search filter by name, type (`t:Material`), tag (`l:Player`); Asset creation context menu | Single flat list with `.. (Up)` button; text-only listing; limited thumbnail display | • No folder tree sidebar<br>• No asset search / filter bar<br>• No icon size slider / grid view<br>• No file creation/deletion/renaming in Project window<br>• No drag-and-drop from Project into Viewport or Inspector |
| **Prefab Workflow** | Prefabs (`.prefab`), Nested Prefabs, Prefab Variants, Overrides inspection, Unpack Prefab | *None* | • No reusable template/prefab asset format<br>• Cannot instantiate synced entity instances across multiple scenes |
| **Package Management** | Unity Package Manager (UPM) for modular engine packages, git dependencies, Asset Store integrations | *None* | • No package or plugin extension system |
| **Import Settings** | Per-asset inspectors (Texture type, sRGB, Mipmaps, Compression, Model scale factor, Rig type: Generic/Humanoid, Animation split, Audio compression format) | Hardcoded imports (STB Image / Assimp defaults) | • No texture import settings (compression, wrap modes, filtering, sRGB flags)<br>• No model import inspector (scale unit conversion, axis conversion, mesh optimization) |

---

### 2.3 Hierarchy & Scene Graph

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Tree Structure** | Full n-depth tree; Multi-selection; Drag-and-drop reparenting and sibling reordering | Full n-depth tree with ImGuiTreeNode; Multi-selection; Drag-and-drop reparenting | • Missing sibling reordering (cannot reorder entities at the same depth)<br>• Missing parent collapse state persistence |
| **Search & Filtering** | Search bar filtering by Name, Component Type (`t:MeshRenderer`), Tag (`tag:Enemy`) | *None* | • No search bar or filter field in Hierarchy |
| **Entity Visibility & Isolation** | Eye icon toggle (Hide/Show in Viewport), Scene Visibility Isolation (Solo mode), Pickability lock (prevent accidental selection) | *None* | • Cannot hide objects in the viewport without deleting them<br>• Cannot lock objects from raycast picking in viewport |
| **Active State** | GameObject active checkbox (disables rendering, physics, and scripts recursively) | *None* | • No `activeSelf` / `activeInHierarchy` toggle on entities |
| **Context Menu** | Extensive right-click menu: Create Empty, 3D Object, 2D Object, Light, Audio, UI, Copy, Paste, Paste As Child, Rename, Duplicate, Delete | Context menu: Rename, Duplicate, Add Child (Primitives only) | • Missing Copy/Paste actions in Hierarchy<br>• Missing non-primitive child instantiation (Lights, Cameras, Empty nodes) |

---

### 2.4 Viewport & Scene View Tooling

```
Unity Scene View Toolbar
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬──────────────┬──────────────┐
│ Hand(Q) │ Move(W) │ Rotate(E)│ Scale(R)│ Rect(T) │ Trans(Y)│ Pivot/Center │ Global/Local │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┴──────────────┴──────────────┘
┌──────────────┬──────────────┬──────────────┬──────────────┬─────────────────────────────┐
│ Snapping     │ Render Mode  │ 2D/3D Toggle │ Light Toggle │ Stats Overlay (FPS/Batches) │
└──────────────┴──────────────┴──────────────┴──────────────┴─────────────────────────────┘
```

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Transform Gizmos** | Move (W), Rotate (E), Scale (R), Rect Tool (T), Transform Tool (Y), Custom Tools | ImGuizmo integration: Move (W), Rotate (E), Scale (R), Rect (T), Transform (Y) | • ImGuizmo covers the 5 standard tools<br>• Missing 2D Rect manipulator handles on 2D/UI elements |
| **Coordinate Space** | Toggle between `Pivot` vs `Center` and `Global` vs `Local` space | Hardcoded Local/World behavior (Gizmo operates in World coordinates) | • Missing `Global` / `Local` gizmo coordinate space toggle<br>• Missing `Pivot` vs `Center` bounds calculation toggle in viewport |
| **Snapping Controls** | Configurable Grid Snapping (Move step, Rotate angle step, Scale step); Vertex Snapping (hold `V`); Surface Snapping (`Ctrl+Shift`) | *None* (Continuous floating-point transforms only) | • No grid snapping toggle or increment config<br>• No vertex snapping tool (`V` key)<br>• No surface/terrain raycast drop snapping |
| **Orientation & Navigation Gizmo** | 3D Scene View Cube / Axis Gizmo (Click X/Y/Z for isometric ortho alignment, click center for Ortho/Perspective toggle) | Text overlay with mouse instructions | • No interactive View Cube / Orientation Gizmo widget<br>• Cannot snap camera to exact Top, Front, Right, Bottom orthographic views |
| **Camera Projection** | Perspective / Orthographic projection toggle; Customizable FOV and Clipping planes | Perspective only (hardcoded FOV and near/far planes) | • No Orthographic viewport mode<br>• No viewport camera speed slider<br>• No viewport FOV slider |
| **Render Modes** | Shaded, Wireframe, Shaded Wireframe, Shadow Cascades, Overdraw, Lighting Only, Normals, Depth | Shaded only | • Missing Wireframe / Shaded-Wireframe display mode<br>• Missing Overdraw and diagnostic debug rendering views |
| **Viewport Overlays & Stats** | Audio on/off, Skybox on/off, Fog on/off, Grid on/off, Statistics overlay (FPS, Frame time, Batches, SetPass calls, Triangles, Vertices) | *None* | • No Grid display toggle<br>• No Scene Statistics overlay (Draw calls, Triangles, Vertices, GPU time) |
| **Multi-View Layout** | 1-view, 2-view (vertical/horizontal), 4-view split screen | Single viewport only | • No multi-angle split viewport support |

---

### 2.5 Game View & Playmode Simulation

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Play Controls** | `Play` (Enter simulation), `Pause` (Freeze time), `Step` (Advance 1 single frame) | `Play` and `Stop` buttons | • Missing `Pause` button<br>• Missing `Step` (single frame advance) button<br>• No time-scale slider (slow-motion / fast-forward simulation) |
| **Playmode Tint** | Customizable editor GUI background color tint during Playmode to prevent accidental lost edits | Static text banner ("PLAYING - changes will not be saved") | • No visual editor window border/theme tinting during Playmode |
| **Aspect Ratio & Resolution** | Dropdown with standard resolutions (16:9, 16:10, 4:3, 21:9, Standalone 1920x1080, 4K, Custom, Device presets); Aspect ratio scaling/letterboxing | Stretches dynamically to fill ImGui Game window size | • No aspect ratio constraint options (causes aspect-ratio-dependent UI/camera distortion)<br>• No fixed-resolution preview mode |
| **Game View Tooling** | `Maximize on Play` toggle, `Mute Audio` toggle, `Stats` overlay, `Gizmos` overlay in Game View | Embedded camera lock / FPS capture | • Missing `Maximize on Play` toggle<br>• Missing `Mute Audio` toggle<br>• Missing Game View render stats |

---

### 2.6 Inspector & Component System Architecture

```
Unity Modular Component Architecture        GameForgerAI Monolithic Struct
┌───────────────────────────────────┐       ┌───────────────────────────────────┐
│ GameObject                        │       │ SceneEntity                       │
├───────────────────────────────────┤       ├───────────────────────────────────┤
│ [x] Active   Name: "Hero"         │       │ int id;                           │
│ Tag: "Player"  Layer: "Default"   │       │ string name;                      │
├───────────────────────────────────┤       │ vector<string> tags;              │
│ ▼ Transform (Pos, Rot, Scale)     │       │ vec3 position, rotation, scale;   │
├───────────────────────────────────┤       │ vec3 color, pivotOffset;          │
│ ▼ MeshFilter (Mesh: Hero_Body)    │       │ bool hasCollider;                 │
├───────────────────────────────────┤       │ bool isPickupItem, PickupItemData;│
│ ▼ MeshRenderer (Material: PBR_Mat)│       │ bool isCastle, CastleData;        │
├───────────────────────────────────┤       │ bool isCatapult, CatapultData;    │
│ ▼ Rigidbody (Mass, Gravity)       │       │ bool isTextMesh, TextMeshData;    │
├───────────────────────────────────┤       │ bool isCineCamera;                │
│ ▼ CapsuleCollider (Radius, Height)│       │ bool isTerrain, TerrainData;      │
├───────────────────────────────────┤       │ bool isImportedMesh;              │
│ ▼ AudioSource (Clip: Footsteps)   │       │ array<TerrainLayerData,3> mat;    │
├───────────────────────────────────┤       │ vector<string> scripts;           │
│ ▼ Script (HeroController.cs)      │       │ EntityAnimation animation;        │
│   - Speed: 8.5                    │       │ EntityCameraRig cameraRig;        │
├───────────────────────────────────┤       └───────────────────────────────────┘
│ [+ Add Component]                 │       (Rigid C++ struct; adding a feature
└───────────────────────────────────┘        requires recompiling the entire engine)
```

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Architecture** | Pure Entity-Component-System / Composition: GameObjects are empty containers; behaviors are attachable `Component` classes | Monolithic `SceneEntity` C++ struct containing 25+ hardcoded union-like fields | • Cannot add arbitrary multiples of a component (e.g. 2 colliders, 3 audio sources)<br>• Cannot create custom new component types without modifying engine C++ structs |
| **Add Component Workflow** | Searchable `Add Component` popup menu categorizing all built-in and user script components | Fixed sections in Inspector; only scripts have an "Add Script" popup | • Missing dynamic `Add Component` button and registry |
| **Component Header Controls** | Checkbox to enable/disable individual component; Context menu: `Reset`, `Remove Component`, `Move Up`, `Move Down`, `Copy Component`, `Paste Component Values`, `Preset` selector | Fixed ImGui sections; only custom checkboxes for certain hardcoded features | • Cannot disable a renderer, collider, or camera independently without modifying fields<br>• No component-level Copy/Paste values<br>• No component Preset saving/loading |
| **Multi-Object Editing** | Multi-selection shows shared fields in Inspector; editing changes all selected objects; displays "—" for mixed values | Header text notes "(+N more selected)", but fields only mutate primary selected entity | • Multi-object Inspector editing does not broadcast changes across all selected entities |
| **Exposed Script Variables** | Public and `[SerializeField]` variables (floats, ints, strings, vectors, asset references, curves) automatically render appropriate GUI controls in the Inspector | Scripts are listed as paths only; script internal variables cannot be viewed or edited from the Inspector | • **Critical Scripting Gap:** Game designers must edit raw Lua code to tweak speed, jump height, health, or weapon parameters |

---

## 3. Subsystem-by-Subsystem Deep Dive

### 3.1 Rendering, Materials & Shaders

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Material Asset Model** | First-class `.mat` asset files using PBR shaders; sharable across infinite meshes; material inspector | Material properties embedded directly per-entity in `SceneEntity::materialLayers` and `color` | • No reusable Material assets (`.mat`)<br>• Updating a texture or color on 10 objects requires editing all 10 individually |
| **PBR Shading Model** | Metallic-Roughness or Specular-Glossiness workflow; Albedo, Normal, Metallic, Smoothness, AO, Height, Emission maps; Detail maps | Basic Lambert/Blinn-Phong; Diffuse, Normal, Height slots with basic world-space triplanar sampling | • No Metallic / Roughness / Smoothness / Specular channels<br>• No Ambient Occlusion (AO) channel<br>• No Emission / Glow channel<br>• No Alpha Blending / Transparency render modes |
| **Shaders & Pipeline** | HLSL/ShaderLab, Universal Render Pipeline (URP), High Definition Render Pipeline (HDRP), Node-based Shader Graph | 4 hardcoded GLSL shader programs in `ViewportRenderer.cpp` (line, mesh, skinned mesh, terrain) | • No custom shader authoring<br>• No visual shader editor / node graph<br>• No compute shaders |
| **Skybox & Environment** | HDRI Skybox material, Procedural Skybox, Gradient background, Solid color background | Static dark clear color (`glClearColor`) | • No skybox rendering (cubemaps or equirectangular HDRIs)<br>• No background customization in scene/game view |
| **Post-Processing** | Post-Processing Stack / Volume framework: Bloom, Tonemapping (ACES/Neutral), Color Grading, Vignette, Motion Blur, Ambient Occlusion (SSAO), Depth of Field | *None* | • No post-processing effects pipeline |

---

### 3.2 Lighting & Shadows

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Light Types** | Directional Light, Point Light, Spot Light, Area Light (Baked/Realtime) | Single hardcoded directional light vector in GLSL fragment shader | • **No Light entities or components**<br>• Cannot place point lights (torches, lamps) or spotlights (flashlights, headlights)<br>• Cannot adjust light color, intensity, range, or angle in UI |
| **Shadows** | Real-time Shadow Maps (Hard/Soft PCF filtering), Shadow Cascades (1, 2, 4 cascades), Shadow Distance, Shadow Bias, Baked Lightmaps | *None* | • **No shadows rendered anywhere in the engine** (objects do not cast or receive shadows) |
| **Global Illumination & Probes** | Baked Lightmapping (Progressive CPU/GPU Lightmapper), Light Probe Groups, Reflection Probes | *None* | • No light baking or indirect lighting approximations |
| **Environment Lighting & Fog** | Linear, Exponential, Exponential Squared Fog; Fog color, start/end distance; Ambient sky/equator/ground gradient | Hardcoded shader constants | • No Fog component or environmental settings panel |

---

### 3.3 Cameras

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Camera Component** | Clear Flags, Background Color, Culling Mask, Projection (Perspective/Ortho), Field of View (FOV), Physical Camera lenses, Near/Far Clipping Planes, Viewport Rect, Depth, Target Texture | Hardcoded view matrix calculation; `EntityCameraRig` contains only FPS/3rd-Person offsets | • Cannot adjust camera FOV, Near Clip, or Far Clip in Inspector<br>• Cannot create multiple in-game cameras (minimaps, split-screen)<br>• No `RenderTexture` target support (cameras rendering to textures/security monitors) |
| **Cine / Cutscene Camera** | Cinemachine (Virtual Cameras, Dolly Tracks, Blend Lists, Freelook, Target Framing) | Custom `isCineCamera` + `Storyboard` panel (records transform keyframes into shots) | • Storyboard captures position keyframes only; no focal length blending, look-at targets, noise shakes, or easing transitions |

---

### 3.4 Physics (3D & 2D)

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Physics Engine Backend** | PhysX / Unity Physics / Havok integration | Custom 150-line Lua/C++ bounding-box push solver (`resolveBoxCollision` in `ScriptRuntime.cpp`) | • No production physics engine (PhysX/Jolt/Bullet)<br>• Objects cannot slide, tumble, roll, or bounce with true Newtonian mechanics |
| **Rigidbody Component** | Mass, Linear Drag, Angular Drag, Use Gravity, Is Kinematic, Interpolate, Collision Detection (Discrete/Continuous/Speculative), Constraints (Freeze Position X/Y/Z, Freeze Rotation X/Y/Z) | `rigidbody.lua` script (custom rudimentary Euler integration in Lua) | • No Rigidbody component in C++ engine<br>• No collision constraints (freeze rotation/position)<br>• No continuous collision detection (high-speed projectiles tunnel through colliders) |
| **Colliders** | Box Collider (Center, Size), Sphere Collider (Center, Radius), Capsule Collider (Center, Radius, Height, Direction), Mesh Collider (Convex, Mesh), Compound Colliders | `hasCollider` boolean (uses object's unrotated world-space AABB `scale` as `[-scale, +scale]`) | • Rotated objects have unrotated collision boxes<br>• No Sphere, Capsule, or Mesh colliders<br>• No Collider Center / Size offset independent of Transform |
| **Physic Materials & Triggers** | Dynamic Friction, Static Friction, Bounciness, Friction/Bounce Combine modes; `Is Trigger` toggle (`OnTriggerEnter`) | *None* (all colliders are solid push-out obstacles; no friction or restitution parameters) | • No Physic Materials<br>• No Trigger volumes (`OnTriggerEnter` / `OnTriggerExit` events) |
| **Joints & Constraints** | Fixed Joint, Hinge Joint, Spring Joint, Character Joint, Configurable Joint | *None* | • No physical joints or constraints |

---

### 3.5 Audio Pipeline

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Audio Architecture** | Full FMOD/Unity Audio pipeline; DSP graph; 3D spatialization; Doppler effect; Rolloff curves | *None* (`Game/Audio/` directory exists with files, but engine has zero audio code) | • **Complete lack of Audio playback** in Editor and Runtime<br>• No OpenAL / miniaudio / SoLoud integration |
| **Audio Components** | `AudioSource` (Clip, Volume, Pitch, Loop, Spatial Blend 2D/3D, Min/Max Distance, 3D Rolloff), `AudioListener` | *None* | • No AudioSource / AudioListener components |
| **Audio Mixing** | `AudioMixer` asset, Master/Sub-busses, Effects (Reverb, Lowpass, Highpass, Pitch), Snapshots | *None* | • No audio mixer or master volume controls |

---

### 3.6 Animation & Skeletal Rigging

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Animation Curves & Dope Sheet** | Dope Sheet and Curve Editor with Bezier tangent handles (Free, Flat, Clamped, Broken, Auto); Multi-property keyframing | Dope Sheet listing keyframe time points; numeric input; linear interpolation only | • No Curve Editor with visual tangent / Bezier curve manipulation<br>• Only animate Position, Rotation, Scale (cannot animate light intensity, colors, material floats) |
| **Animator State Machine** | Animator Controller graph; States, Transitions, Conditions, Parameters (Float, Int, Bool, Trigger), Any State, Sub-State Machines | *None* | • No Animation State Machine<br>• Cannot transition from Idle -> Walk -> Run -> Jump based on gameplay parameters |
| **Skeletal & Rigging Pipeline** | Humanoid Avatar retargeting; Muscle definitions; Generic rigs; Skinned Mesh Renderer; Root Motion; IK (Inverse Kinematics) | Assimp-loaded bone palettes (GPU skinned shader in `ViewportRenderer.cpp`); Map Humanoid menu item is inert | • "Map Humanoid Skeleton..." is an inert disabled menu item<br>• "Animation Library..." is an inert disabled menu item<br>• No animation clip blending, retargeting, or cross-fading |

---

### 3.7 Terrain System

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Heightmap Sculpting** | Sculpt tools: Raise/Lower, Paint Height (Target), Smooth, Flatten, Stamp; Custom alpha brush shapes & falloffs | Raise, Lower, Perlin Generate, Import Heightmap Image; single circular radius/strength brush | • No Smooth or Flatten brushes<br>• No custom alpha brush masks (stamps, noise textures, square brushes) |
| **Texture Painting** | Unlimited Terrain Layers; Diffuse, Normal, Mask, Specular maps; Tiling & Offset per layer; Brush size, opacity, target strength | Up to 3 hardcoded texture layers; RGB splat weight grid; single shared UV scale | • Strictly capped at 3 layers (cannot add a 4th texture for rock, mud, snow, etc.)<br>• No per-layer UV tiling/offset settings |
| **Foliage & Trees** | Paint Trees (tree prototypes, density, height variation, color variation, wind sway); Paint Details (billboard grass, mesh flowers, wind animation) | *None* | • **No Tree / Foliage placement tool**<br>• **No Grass / Detail mesh painting tool** |
| **Multi-Tile Terrains** | Create Neighbor Terrains, automatic seamless border stitching | Single isolated terrain entity | • No terrain tiling or streaming |

---

### 3.8 Scripting & Logic Architecture

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Language & Engine API** | C# with .NET runtime; High-performance engine binding; Full standard library; NuGet packages | Embedded Lua 5.4 restricted to base, math, string, table; custom `self.entity`, `self.physics`, `self.camera`, `self.input` bindings | • Restricted sandbox (no networking, file IO, or external modules)<br>• C++ runtime has hardcoded callback bindings |
| **Inspector Variable Exposure** | `[SerializeField]` / `public` variables auto-generate Inspector UI widgets | *None* (Scripts are opaque file paths; variables cannot be inspected or tuned) | • Cannot inspect or edit script variables from the Inspector UI |
| **Event Lifecycle** | `Awake()`, `OnEnable()`, `Start()`, `FixedUpdate()`, `Update()`, `LateUpdate()`, `OnTriggerEnter()`, `OnCollisionEnter()`, `OnDisable()`, `OnDestroy()` | `init()`, `update(dt)`, custom event queries (`heldItemQuery`, `catapultQuery`) | • No `FixedUpdate` (physics step desynced from render frame rate)<br>• No collision callback events (`onCollision(otherEntity)`) |
| **Visual Scripting** | Unity Visual Scripting (Graph-based State and Flow machines) | AI Script Generator (generates raw Lua code from natural language prompts) | • No interactive node-based visual scripting canvas |
| **Debugging** | Visual Studio / Rider / VS Code debugger integration with breakpoints, watches, callstacks | Console logs (`print()` routed to editor Console window) | • No step-through Lua debugging or breakpoint inspection |

---

### 3.9 In-Game UI / Canvas System

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Canvas & Layout Architecture** | Canvas (Screen Space Overlay, Screen Space Camera, World Space), Canvas Scaler, RectTransform with Anchors (Min/Max), Pivots, Layout Groups (Horizontal, Vertical, Grid) | *None* for in-game authoring (only 3D TextMesh entities and hardcoded ImGui debug overlays) | • **No in-game 2D UI system**<br>• Cannot author custom HUDs, menus, health bars, inventory slots, or dialog boxes visually |
| **UI Widgets** | TextMeshPro, Image, RawImage, Button, Toggle, Slider, Scrollbar, Dropdown, InputField, ScrollView | Hardcoded ImGui windows during Play (Inventory grid, Catapult HP bar, Detected icon) | • UI elements are hardcoded in C++ PlayMode state instead of being authored scene entities |

---

### 3.10 Particles & Visual Effects (VFX)

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Particle System** | Shuriken Particle System: Emission, Shape, Velocity, Color over Lifetime, Size over Lifetime, Rotation, Noise, Collision, Sub-emitters, Lights, Trails | *None* | • **Complete lack of a Particle System** (no fire, smoke, sparks, explosions, rain, or magical effects) |
| **VFX Graph** | GPU-accelerated node-based VFX Graph for millions of particles | *None* | • No GPU particle simulation |

---

### 3.11 Navigation & AI

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **NavMesh Generation** | NavMesh Surface baker (voxelizes scene geometry, generates walkable polygons, agent radius/slope/step settings) | *None* | • No NavMesh baking tool |
| **Pathfinding Agents** | `NavMeshAgent` component (A* pathfinding, obstacle avoidance, automatic steering, stopping distance) | `enemy_ai.lua` script (rudimentary straight-line distance vector push in Lua) | • No pathfinding algorithm (enemies walk straight into walls and get stuck)<br>• No dynamic obstacle avoidance |

---

### 3.12 Profiling & Diagnostics

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Profiler Window** | Multi-channel timeline: CPU Usage, GPU Usage, Memory allocations, Rendering statistics, Physics, Audio | *None* | • No performance profiler |
| **Frame Debugger** | Step-by-step draw call inspector (inspect render targets, depth buffers, shader uniforms, draw order) | *None* | • No frame debugging tool |
| **Console Window** | Log, Warning, Error filters; Stack traces with clickable links to IDE source files; "Clear on Play"; "Error Pause"; Regex search | Basic list with Info, Warning, Error checkboxes, Auto-scroll, and Clear button | • Console messages do not show source file or line number<br>• Clicking a log entry does not open the script editor at that line<br>• No search filter in console |

---

### 3.13 AI Integration (GameForgerAI Competitive Advantage)

| Feature | Unity 3D | GameForgerAI Editor | Parity Evaluation |
|---|---|---|---|
| **Scene Command Planner** | Unity Muse (Cloud subscription, web-based, limited scene manipulation) | **AI Forge:** Built-in multi-turn natural language command planner with command bus previews | **GameForgerAI Advantage:** Native local/remote AI planning tightly coupled to the editor command bus |
| **Script Generation** | Unity Muse Code (generates C# snippets via chat) | **AI Script Creator:** In-editor prompt-to-Lua generation with syntax verification | **GameForgerAI Parity/Lead:** Direct script creation and attachment to target entities |
| **Animation Generation** | *None* native | **AI Animation Generator:** Generates multi-keyframe transform animations from prompts | **GameForgerAI Advantage:** Direct text-to-keyframe synthesis |
| **Provider Agnosticism** | Proprietary OpenAI/Unity Cloud backends only | Configurable providers (NVIDIA, Agnes-AI, custom local LLMs via JSON) | **GameForgerAI Advantage:** Local/private model support |

---

### 3.14 Build & Platform Pipeline

| Feature | Unity 3D | GameForgerAI Editor | Parity Gap & Missing Options |
|---|---|---|---|
| **Build Settings Window** | Platform selection (Windows, Mac, Linux, Android, iOS, WebGL, Consoles); Scene inclusion list with reordering; Development Build / Autoconnect Profiler toggles | Single menu item: "Build Game..." (exports active scene to `.gfai` and updates `Project.json`) | • No Build Settings window<br>• Cannot choose which scenes are bundled in a build<br>• Cannot select target build architecture/platform |
| **Player Settings** | Company Name, Product Name, Version, App Icons (all sizes), Splash Screen, Resolution/Fullscreen modes, Color Space (Gamma/Linear), Quality Presets | `Project.json` has `title`, `version`, `startupScene`; no UI editor for these settings | • No Player Settings editor tab<br>• Cannot configure game executable icon, splash screen, or window display modes |
| **Standalone Distribution** | Builds a self-contained `.exe` + asset archive (`data.unity3d`) ready for distribution | Runtime requires raw source folders (`Game/`, `out/`) to locate assets | • No asset bundling / packaging into single-binary or encrypted archive formats |

---

## 4. Master Parity Gap Inventory

This inventory itemizes all missing options, widgets, menus, and systems required for Unity 3D parity.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       MASTER PARITY GAP INVENTORY                           │
└─────────────────────────────────────────────────────────────────────────────┘

[P0] ARCHITECTURAL PREREQUISITES
 ├── Modular Component Architecture (Migrate from monolithic SceneEntity struct)
 ├── Dynamic "Add Component" system with searchable component registry
 ├── GUID-based Asset Meta System (.meta files for all assets in Game/)
 ├── Script Inspector Serialization (Expose Lua public fields in Inspector)
 ├── True 3D Physics Backend (Integrate Jolt or PhysX; replace Lua box-pusher)
 └── Audio Engine Integration (Integrate miniaudio or OpenAL; AudioSource/Listener)

[P1] TOP-LEVEL MENUS & WORKFLOW WINDOWS
 ├── Assets Menu (Create Submenu, Show in Explorer, Reimport All, Refresh)
 ├── Component Menu (Categorized component attachment menu)
 ├── Window Menu (Panel toggles, Layout save/load presets)
 ├── Project Settings Window (Audio, Physics, Player, Quality, Tags/Layers)
 ├── Preferences Window (Editor colors, External script editor, Keybindings)
 └── Build Settings Window (Scene list, Platform switcher, Executable packager)

[P2] INSPECTOR & COMPONENT PARITY
 ├── GameObject Header (Active checkbox, Tag dropdown, Layer dropdown, Static flags)
 ├── Component Context Controls (Enable checkbox, Reset, Remove, Move Up/Down, Copy/Paste)
 ├── Multi-Object Property Broadcasting (Simultaneous multi-entity editing)
 ├── Transform Component (Local/World toggle, normalization, reset)
 ├── Mesh Renderer Component (Material slot picker, Cast/Receive shadows)
 ├── Material Asset System (.mat files, PBR Metallic/Roughness, Normal, AO, Emission)
 ├── Light Components (Directional, Point, Spot, Intensity, Range, Angle, Color, Shadows)
 ├── Camera Component (FOV, Near/Far clip, Clear flags, Projection Ortho/Perspective)
 ├── Rigidbody Component (Mass, Drag, Gravity, Kinematic, Constraints X/Y/Z)
 └── Collider Components (Box, Sphere, Capsule, Mesh with Center & Size offsets)

[P3] VIEWPORT, HIERARCHY & TOOLING
 ├── Viewport Snapping System (Grid increment snapping, Vertex snap 'V', Surface drop)
 ├── Viewport Orientation Gizmo (Interactive 3D View Cube)
 ├── Viewport Render Modes (Shaded, Wireframe, Overdraw)
 ├── Viewport Statistics Overlay (FPS, Frame time, Draw calls, Triangles, Vertices)
 ├── Hierarchy Search & Filters (Filter by name, type, tag)
 ├── Hierarchy Visibility & Lock Toggles (Eye icon, Lock icon)
 ├── Game View Controls (Pause, Step frame, Aspect ratio dropdown, Maximize on play)
 └── Console Enhancements (Clickable stack traces, jump-to-code, regex search)

[P4] ADVANCED ENGINE SUBSYSTEMS
 ├── Particle System / VFX Emitter Component
 ├── In-Game 2D UI Canvas & RectTransform System (Text, Image, Button, Slider)
 ├── Terrain Enhancements (Unlimited layers, Foliage/Tree brush, Detail grass, Smooth/Flatten)
 ├── Animation State Machine & Curve Editor (Dope sheet tangents, transitions, blend trees)
 └── NavMesh Navigation System (NavMesh surface baker, NavMeshAgent pathfinding)
```

---

## 5. Actionable Roadmap to Parity

### Phase 1: Architectural Foundation (Engine Core & Component System)
1. **Component Model Refactor:** Decompose `SceneEntity` into an entity container with an attached vector of polymorphic `Component` instances (`TransformComponent`, `MeshFilterComponent`, `MeshRendererComponent`, `ColliderComponent`, `ScriptComponent`, etc.).
2. **Add Component UI:** Build a searchable `Add Component` popup in the Inspector that scans the component registry.
3. **Inspector Script Serialization:** Parse Lua script header annotations (e.g. `--@field speed: float = 5.0`) to automatically generate editable numeric, string, boolean, and asset-reference fields in the Inspector.
4. **Asset GUID & Meta Pipeline:** Generate `.meta` files with UUIDs for all files under `Game/`; resolve asset references by UUID rather than raw file path strings.

### Phase 2: Core Subsystems (Audio, Physics, Lighting & Materials)
1. **Audio Integration:** Embed `miniaudio` into the Engine; implement `AudioSource` and `AudioListener` components with 3D spatial attenuation and volume/pitch controls.
2. **Real Physics Integration:** Integrate `Jolt Physics` (or PhysX); implement true `Rigidbody`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`, and `MeshCollider` components with friction/bounciness physic materials.
3. **PBR Material Asset System:** Introduce `.gfmat` JSON material files supporting Albedo, Normal, Metallic, Roughness, and Ambient Occlusion textures; upgrade `ViewportRenderer` to a standard PBR shader.
4. **Light Entities & Shadow Maps:** Create `LightComponent` (Directional, Point, Spot) and implement shadow mapping (depth framebuffers + PCF filtering).

### Phase 3: Editor Workflow & UX (Menus, Viewport & Diagnostics)
1. **Complete Menu Bar:** Implement `Assets`, `Component`, `Window`, and `Help` menus with all standard options.
2. **Viewport Snapping & Gizmos:** Add configurable grid snapping, vertex snapping (`V`), Global/Local space toggle, and an interactive 3D Orientation View Cube.
3. **Game View Controls:** Add `Pause`, `Step Frame`, fixed Aspect Ratio dropdowns (16:9, 4:3, etc.), and `Maximize on Play`.
4. **Project Settings & Preferences:** Build a multi-tab Project Settings window (Tags & Layers, Physics Gravity/Timestep, Audio Master, Quality) and an Editor Preferences window.
5. **Console Enhancement:** Add source file location links to log entries that open the built-in script editor or external IDE at the exact line.

### Phase 4: Advanced Systems (UI Canvas, VFX, Animation & Navigation)
1. **In-Game 2D UI Canvas:** Implement a 2D rendering pass with `Canvas`, `RectTransform`, `UIImage`, `UIText`, and `UIButton` components.
2. **Particle System:** Implement a CPU/GPU particle emitter component with lifetime, velocity, color-over-lifetime, and texture sheet animation.
3. **Animation State Machine & Curves:** Build an Animator State Machine editor with visual nodes, transitions, and Bezier curve editor.
4. **NavMesh & Pathfinding:** Implement Recast/Detour NavMesh generation and a `NavMeshAgent` component.
5. **Build & Standalone Packager:** Build a dedicated `Build Settings` window that compiles scenes and assets into a standalone distribution package.

---

*End of Comparative Audit Document (`Compared.md`).*
