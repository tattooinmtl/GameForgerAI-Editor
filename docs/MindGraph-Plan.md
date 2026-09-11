# Mind Graph — visual node scripting for GameForgerAI

**Status:** planned, not started · **Branch it will land on:** `unity-parity-upgrade`
**Depends on:** lights/cameras/UI (done), lens layers (done), reusable script presets (done)

---

## 1. Context

The editor can now build a world — models, terrain, lights, cameras, HUD, weapons, doors,
keys, audio with real 3D falloff. What it cannot do is let someone **wire that world together
without writing Lua**. Today the only way to say "when the player walks into this room, play
this sound and open that door" is to attach a script and edit its fields by hand.

Mind Graph is the answer to that: a Blueprint-style node canvas where a designer drags out
`On Game Start → Zone Trigger → Play Audio → Open Door`, picks each object from a dropdown of
what is actually in the scene, and runs it.

**The three decisions already locked:**

1. **Canvas:** `thedmd/imgui-node-editor` via FetchContent (MIT).
2. **Execution:** a graph compiles to **Lua**, run by the existing `ScriptRuntime`.
3. **First slice:** Zone trigger → audio + door, because it proves every layer end to end.

---

## 2. Design pillars

The "player" of this feature is the person building the game. Every decision below is
measured against these:

1. **Readable at a glance.** Someone who did not author the graph should be able to trace
   what happens from the wires alone. Node colour = category; execution flow is one thick
   white wire; data is thin coloured wires.
2. **Never a dead end.** Any node can be opened as the Lua it generates. A graph is never a
   black box you get stuck inside — you can always drop to script and keep going.
3. **The scene is the vocabulary.** Dropdowns list the objects, tags, audio clips, scripts
   and cameras that actually exist in this scene. No typing names and hoping.
4. **It ships.** A graph that works on Play works in `GameForgerRuntime.exe`. This is
   non-negotiable — §1b of `MissingFunctions.md` records four defects from exactly this seam.
5. **AI assists, never owns.** The AI can lay out, wire, name and explain a graph. The graph
   file stays human-authored and human-editable; nothing is generated that you cannot see.

---

## 3. What already exists — do not rebuild

Checked directly before planning. This is most of the hard part:

| Need | Already there |
|---|---|
| **Audio distance fade** (the waterfall case) | `AudioEngine.cpp:442-543` — miniaudio spatialisation with `min/maxDistance` and a positioned listener. `AudioSourceData` already has `is3D`, `minDistance`, `maxDistance`, `fadeInSeconds`, `fadeOutSeconds`. **No new audio code at all.** |
| Sandboxed script execution, both hosts | `ScriptRuntime.cpp`, `resolveProjectFile` confinement |
| Proximity queries | `world:findNearestWithTag`, `findPositionByTag` |
| Show/hide, rotate, move another entity from script | `world:setEntityActive`, `setEntityRotation`, `entity:setPosition` |
| Doors, keys, keycodes, weapons, inventory | the four script presets just shipped |
| Cameras, cine mode, lens layers | just shipped |
| Undoable, validated mutation | `AICommandBus` |
| JSON read/write | `Json.cpp`, `SceneSerializer.cpp` conventions |

**What genuinely does not exist:** a node canvas, a graph data model, graph→Lua codegen, and
a Trigger Zone entity trait.

---

## 4. Architecture

```
  .gfgraph (JSON, authored)
        |
        |  GraphCompiler  (Engine/src/Editor/MindGraph/)
        v
  Game/Scripts/generated/<GraphName>.lua        <- readable, openable, NOT hand-edited
        |
        v
  ScriptRuntime  (identical in Editor Play and GameForgerRuntime)
```

**Why compile to Lua rather than interpret the graph in C++.** A second execution engine
would need every binding, every sandbox rule and every host taught about it twice — which is
precisely the Editor/Runtime seam that has already produced four defects here. Compiling
means the graph inherits the sandbox, the path confinement, hot reload, both hosts and the
existing test suite for free. It is also the same pipeline GFScript is committed to, so
graphs and hand-written scripts converge instead of forking the engine.

**The generated Lua is a first-class artefact.** It goes in `Game/Scripts/generated/`, is
gitignored from hand-editing by convention (a header comment says so), and the panel has an
"Open Generated Lua" button. Pillar 2 depends on this.

### Files

| File | Role |
|---|---|
| `Engine/include/GameForger/Editor/MindGraph/GraphData.hpp` | `GraphNode`, `GraphPin`, `GraphLink`, `MindGraph` — the data model |
| `Engine/src/Editor/MindGraph/GraphSerializer.cpp` | `.gfgraph` load/save, same hand-rolled JSON style as `SceneSerializer` |
| `Engine/src/Editor/MindGraph/NodeCatalog.cpp` | every node type: pins, category, colour, and its Lua emitter |
| `Engine/src/Editor/MindGraph/GraphCompiler.cpp` | topological walk → Lua source; cycle and missing-input diagnostics |
| `Editor/src/MindGraphPanel.cpp` | the canvas panel, dropdowns populated from the live scene |
| `Engine/tests/TestMain.cpp` | round-trip, codegen golden output, cycle rejection |

### Data model sketch

```cpp
struct GraphPin { int id; std::string name; PinKind kind; PinType type; };
// PinKind: Exec | Data.  PinType: Flow, Bool, Float, Vec3, EntityRef, AudioClip, Tag, String
struct GraphNode { int id; std::string type; glm::vec2 canvasPos;
                   std::vector<GraphPin> inputs, outputs;
                   std::map<std::string, std::string> literals; }; // inline dropdown choices
struct GraphLink { int id; int fromPin; int toPin; };
struct MindGraph { std::string name; std::vector<GraphNode> nodes; std::vector<GraphLink> links; };
```

Pin ids are graph-unique and stable across saves, so a link never silently rebinds when a
node's pin list changes — the same reason enums here serialise by name, not ordinal.

---

## 5. Node catalogue

**First slice (Phase 3) — the twelve nodes that make the demo work:**

| Category | Node | Emits |
|---|---|---|
| Events | `On Game Start` | the generated script's `on_start` |
| Events | `On Update` | `on_update(dt)` |
| Events | `On Zone Enter` / `On Zone Exit` | a proximity test against a Trigger Zone entity |
| Events | `On Interact (E)` | range check + `input:isKeyPressed("E")` |
| Flow | `Branch (if)` | `if cond then … else … end` |
| Flow | `Sequence` | ordered exec outputs |
| Flow | `Delay` | coroutine-style timer on the script's own clock |
| Audio | `Play Audio` | `audio:play(clip, entity)` — 3D falloff comes free from the source |
| Audio | `Stop Audio` | `audio:stop(...)` |
| World | `Set Entity Active` | `world:setEntityActive(name, bool)` |
| World | `Open / Close Door` | sets the door script's state |
| World | `Set Light` | intensity / colour on a Light entity |

**Later phases:** `Play Cine Shot`, `Set Camera Effects` (the lens layers), `Move To`,
`Spawn`, `Give Item`, `Has Item`, `Set Variable` / `Get Variable`, `Random`, `Compare`,
`Call Script Function` (wraps any function in any `Game/Scripts/*.lua` — the escape hatch
that makes pillar 2 real), `On Win` / `On Lose`, `Load Scene`.

---

## 6. New engine data: Trigger Zone

The one genuinely new entity trait. Additive, same shape as `hasCollider`/`isLight`:

```cpp
enum class TriggerShape { Sphere, Box, EntityProximity };
struct TriggerZoneData {
    TriggerShape shape = TriggerShape::Sphere;
    float radius = 4.0F;          // Sphere
    glm::vec3 halfExtents{2,2,2}; // Box
    std::string proximityTag;     // EntityProximity: "near anything tagged X"
    std::string watchTag = "Player";
    bool triggerOnce = false;
    bool active = true;
};
```

Renders as a wireframe gizmo through the `isGizmoOnlyEntity` path already built, so it costs
nothing in the mesh pass and cannot index past the primitive array. Enter/exit edges are
computed once per frame in `GameplayLoop` — **in Engine, called by both hosts**, not in
`main.cpp`, so it cannot become a fifth seam defect.

A **waterfall** is then: a mesh + an `AudioSource` (`is3D`, loop, min 3 / max 25) + nothing
else. The distance fade is the audio engine's, already working. A graph is only needed if you
want it to *start* on approach rather than play from scene load.

---

## 7. The panel

Docks **centre, tabbed beside Viewport and Game** — the user asked for this explicitly and
they are right: a graph needs the big pane, not a side strip. `EditorLayout.cpp` gets a third
tab in that dock node.

Layout: node canvas centre; left a **Palette** (searchable, grouped by category); right a
**Details** strip for the selected node's literals; bottom a **Compile** bar with
Compile / Open Generated Lua / error list. Double-clicking an error selects the offending node.

**Live feedback while playing.** During Play, the currently-executing node pulses. This is
the single biggest reason Blueprint is teachable, and it is cheap here: the generated Lua
emits `__gfNode(id)` breadcrumbs that the panel reads.

---

## 8. AI assistance

Three concrete, bounded jobs — not "AI writes your game":

1. **Lay out / tidy.** Re-flow a messy graph: left-to-right by execution order, aligned rows,
   no crossing wires where avoidable. Pure geometry, no semantics, zero risk.
2. **Describe.** "What does this graph do?" — walks the graph and explains it in prose. Also
   the honesty check on pillar 1.
3. **Author from a sentence.** "When the player enters the vault, dim the lights and play the
   alarm" → proposed nodes and links, **staged as a preview the user accepts or rejects**,
   never applied silently. Routed through the existing `AICommandBus` approval path.

The node catalogue and the live scene contents are what the model is given — the same
`describeCurrentScene()` grounding `AICommandPlanner` already uses. This is also the first
real consumer of the OKF knowledge base from Phase 5 of the main plan: a `mind-graph` skill
describing the node vocabulary.

---

## 9. Phases

| # | Phase | Done when |
|---|---|---|
| 0 | **De-risk the dependency.** Pin an exact imgui-node-editor commit, verify its actual file layout before writing CMake paths, build a throwaway window with two nodes and a wire. | A node canvas renders and a link can be dragged. Nothing else. |
| 1 | **Graph model + serializer.** `GraphData.hpp`, `.gfgraph` read/write, round-trip test. No UI. | A hand-written `.gfgraph` loads, saves, and comes back byte-identical. |
| 2 | **Compiler + catalog.** Twelve nodes, topological walk, Lua emit, cycle/missing-input errors. | A hand-written graph compiles to Lua that `testAllShippedScriptsLoad` accepts. |
| 3 | **Panel.** Canvas, palette, details, compile bar, docked centre. | The demo graph is authorable by mouse alone. |
| 4 | **Trigger Zone.** Entity trait, gizmo, Inspector, enter/exit in `GameplayLoop`, both hosts. | Walking into a zone fires in Play **and** in the Runtime. |
| 5 | **Live node highlighting** during Play. | The executing node pulses. |
| 6 | **AI: lay out, describe, author-with-preview.** | A sentence produces a graph the user can accept or reject. |

**Why this order:** phases 1 and 2 have no UI at all and are fully testable, so the risky,
untestable canvas work in phase 3 lands on a model already proven correct. Phase 0 exists
because `feedback_imguizmo_tags` records a real gotcha with exactly this kind of dependency.

---

## 10. Verification

Per phase: Debug + Release + `all-release` clean; full CTest plus that phase's own tests;
`GameForgerEditor.exe` launches with empty stderr.

New tests:
- `.gfgraph` round-trip, including stable pin ids across a node's pin list changing.
- Codegen golden output for each node type — the generated Lua must be exact.
- Cycle detection rejects a loop instead of hanging the compiler.
- **Trigger-zone parity:** enter/exit fires identically in the Editor tick and the Runtime
  tick. This is the test §1b says should have existed all along.

**End to end:** in the FPS demo, author `On Zone Enter (vault) → Play Audio (alarm) →
Set Light (red)` entirely in the panel, press Play, walk in, hear it and see it — then build
and run the same scene in `GameForgerRuntime.exe` and get the same result.

---

## 11. Gauntlet prompt

Per the `gauntlet-loop` skill. Run one phase at a time.

```
Work Phase N of docs/MindGraph-Plan.md on branch unity-parity-upgrade in
C:\GameForgerAI-Editor. Stay in C++20, native Win32, existing code style.

The bar is Unreal Engine Blueprint. Open the real thing, or its official docs and node
reference, and compare against it directly rather than against a description of it. The
proof is a designer authoring "when the player enters the vault, play the alarm and turn
the lights red" entirely with the mouse, pressing Play and having it work - then building
that scene and getting the same result in GameForgerRuntime.exe.

Break the phase into the smallest pieces that can be judged on their own. For each piece,
fan out a builder and a separate critic with fresh context. The critic drives the actual
running editor, puts our node canvas next to Blueprint blind, says which one a designer
would understand faster, and names the single biggest remaining gap. Then it goes back to
the builder. Every piece must also hold: Debug + Release + all-release clean, full CTest
green, and no frame-time regression on the reference scene.

The critic should be a harsh critic. Praise is not useful. If ours does not win, it keeps
going.

/loop on each piece until the critic picks ours blind. Do not stop before that.

Keep a live progress page updating as the work evolves so I can watch it.

Fan out subagents and ultracode.
```
