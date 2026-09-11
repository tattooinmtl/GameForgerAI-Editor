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

---

## 12. Reviewer's commentary — ai-agents-architect pass

*Audience: the next agent who picks this up. Read this before starting Phase 0.*

The plan is strong overall. Below are the pros I'd defend, the cons I'd flag, and the concrete changes I'd make before any code lands.

### Pros — keep these

- **Compile to Lua, reuse `ScriptRuntime`.** The single best decision in the plan. It directly attacks the Editor/Runtime seam that has already shipped four defects (per `MissingFunctions.md` §1b). A second execution engine would mean re-teaching every binding, every sandbox rule, and both hosts about it. Compiling means graphs inherit the sandbox, path confinement, hot reload, both hosts, and the existing test suite for free. **Do not waver on this under any pressure to "interpret the graph in C++ for performance" or to hide the generated Lua.**

- **Phase 0 exists for the right reason.** Dep-risk the imgui-node-editor dependency first because `feedback_imguizmo_tags` records a real gotcha with this exact kind of dependency. That is learned behavior, not ceremony. Keep it.

- **Trigger Zone lives in `GameplayLoop`, not `main.cpp`.** Specifying that the trait's enter/exit is computed inside Engine code called by both hosts, rather than in either host's main loop, is exactly the right move to prevent a fifth seam defect. Hold the line on this in code review.

- **"Open Generated Lua" + AI routed through `AICommandBus`.** These two choices are what keep pillars 2 ("never a dead end") and 5 ("AI assists, never owns") honest. Any agent work that bypasses the command bus or hides the generated Lua should be rejected at review.

### Cons — flag and address before Phase 0 closes

These are the things that will hurt later if left implicit.

1. **Pin the catalog shape before writing the compiler.** Phase 2's compiler depends on a stable, complete node catalog with exact Lua emission per node. If the catalog drifts mid-phase, the codegen golden tests churn and you lose the only safety net that proves the generated Lua is what `ScriptRuntime` will accept. Sequence: lock the 12 node types, their pins, their categories, their colours, and a stub emitter per type — *then* write `GraphCompiler.cpp`. Golden tests ride on top of the locked catalog.

2. **Pin id stability is under-specified.** The plan correctly notes that ids serialize by name (the enum lesson), but it does not say *where* ids are assigned, *who owns them* across a node's pin-list change, or what happens when a node type is deleted. Recommendation: every node type defines its pins as a static array with stable string ids; the data model resolves to int ids at load time; links reference resolved int ids. Add a dedicated unit test before Phase 2 closes that loads a `.gfgraph`, mutates one node's pin list, saves, reloads, and asserts that surviving links still resolve.

3. **`__gfNode(id)` breadcrumbs are a compiler concern, not a per-node emitter concern.** Live highlighting means every node function gets wrapped. That is a wrapper, designed once, applied uniformly — not twelve separate edit points that can drift out of sync. Implement it in `GraphCompiler.cpp` as part of the codegen pass; verify the wrapper is semantics-preserving by golden-test diff (run before and after the wrapper is added — diff must be zero apart from the wrapper lines).

4. **"The scene is the vocabulary" needs a literal-refresh contract.** The panel populates dropdowns from the live scene, but the plan doesn't say how those literals refresh when an entity is renamed, deleted, undone, or restored across save/load. Recommendation: store `EntityRef` / `AudioClip` / `Tag` literals as GUID + display-name pairs; on graph load and on every `AICommandBus` mutation event, re-resolve the display name and mark any unresolved literals as broken (visualised in the node, not silently empty). Spell this contract in `GraphData.hpp` comments before Phase 3 starts.

5. **"Author from a sentence" needs a wireframe before Phase 6 starts.** It is the only AI job that touches semantic graph correctness, and it routes through `AICommandBus` as a preview the user accepts or rejects — but the plan doesn't specify what "accept or reject" looks like in the UI. Recommendation: before any Phase 6 code, produce a single wireframe (hand-drawn is fine) showing the preview state, the diff vs. the current graph, and the accept / reject / edit-before-accept controls. The wireframe is the contract the AI assistant implementation must satisfy.

### How I would change the plan, in order

If I were driving this from here:

1. **Add §13 — Catalog Pin Contract.** New subsection under §4 (Architecture) stating that node types define pins as static arrays with stable string ids, resolved to int ids at load. Cite the enum-name lesson explicitly.
2. **Add §14 — Literal Refresh Contract.** New subsection under §7 (Panel) specifying GUID + display-name storage for scene-bound literals and the `AICommandBus` event subscription that re-resolves them.
3. **Add §15 — Breadcrumb Wrapper Contract.** New subsection under §5 (Node Catalogue) stating that `__gfNode(id)` wrapping is a compiler-level concern, not a per-node emitter concern, and that semantics-preserving wrappers are verified by golden-test diff before Phase 5 lands.
4. **Reorder Phase 2 internally** to: lock catalog → write codegen for each node → wire compiler → golden tests. Do not let the compiler and the catalog evolve in parallel.
5. **Add an optional Phase 0.5 spike:** a 30-minute test that loads a real `.gfgraph` with a renamed entity in the scene, and proves the literal-refresh contract holds. If it can't be proven in 30 minutes, the contract is wrong and you find out before Phase 3, not after.
6. **Add a Phase 6 prep task:** the accept/reject wireframe, before any AI authoring code.

**If you only have time to do one of these, do #1.** Pin id stability is the load-bearing invariant for everything that comes after — serialization, link resolution, undo/redo across graph edits, and AI authoring.

### One thing I would not change

The compile-to-Lua architecture. Any pressure to "interpret the graph in C++ for performance" or "skip the generated file so users don't see it" should be rejected — the visibility into the generated Lua is exactly what makes the system debugable and what keeps pillars 2 and 5 honest. If a graph ever becomes a black box, you have lost both and you cannot get them back.

---

## 13. Catalog & pin contract

*Addresses reviewer con #1 and #2. This is the load-bearing invariant — everything after it
depends on links surviving edits.*

**Node types own their pins, as static data.** Each node type declares its pins once, in
`NodeCatalog.cpp`, as a static array. Every pin carries a **stable string id** (`"exec_in"`,
`"target"`, `"clip"`) that is part of the node type's contract and never changes once
shipped. Display names may change freely; string ids may not.

**Links serialize by name, never by index.** A link is written as
`{fromNode, "out_exec", toNode, "exec_in"}` — a node id plus a pin *string* id at each end.
Integer pin handles exist only in memory, assigned at load for the canvas to hit-test
against.

> This is the sharpening the reviewer's wording needs. "Resolve to int ids at load, links
> reference resolved int ids" is correct in memory and **wrong on disk**: if the file stores
> ints and ints are assigned at load, adding a pin to a node type shifts every later index and
> silently rebinds links — precisely the failure the contract exists to prevent. Same lesson
> as enums-by-name in `SceneSerializer`, one level deeper.

**Consequences that must hold:**

- Adding, removing or reordering a node type's pins must not rebind any surviving link.
- A link whose pin id no longer exists loads as **broken and visible**, not dropped. Dropping
  it silently destroys authored work on the first load after an engine update.
- A graph referencing an **unknown node type** loads with that node preserved verbatim and
  flagged. Saving must round-trip it untouched — otherwise opening a graph from a newer
  editor and saving it quietly deletes work.

**Test before Phase 2 closes:** load a `.gfgraph`, mutate one node type's pin list (add one,
reorder two), save, reload, assert every surviving link still resolves to the same logical
endpoints.

**Phase 2 internal order — do not parallelise these:**
`lock the 12 node types (pins, ids, category, colour) → stub emitter per type → golden tests
against the stubs → write GraphCompiler.cpp → fill in real emitters`. The catalog and the
compiler evolving together is what makes golden tests churn into noise.

---

## 14. Literal refresh contract

*Addresses reviewer con #4 — with a correction. The reviewer's intent is right; the
mechanism it proposes does not fit this engine.*

**The correction.** The review recommends storing scene-bound literals as *GUID +
display-name* pairs. **This codebase has no entity GUID and cannot cheaply acquire one.**
`EditorScene.hpp:440` states it outright: *"the save format identifies entities by name, not
id, so loaded entities never carry one."* Entity identity here **is the name** — that is how
`parentName`, script tags, catapult arm references and every other cross-entity reference
already work. Introducing GUIDs would mean overhauling the scene format's identity model, a
large change smuggled in under a node-editor feature.

(A GUID concept *does* exist in `Core/AssetDatabase.hpp` — but it is dead code, reached only
by `TestMain.cpp` per audit §8.2, and it identifies *assets by file path*, not entities.
Reviving it is a deliberate decision, not a Phase 3 detail.)

**What we do instead:**

| Literal type | Stored as | Resolved against |
|---|---|---|
| `EntityRef` | entity **name** | `scene.findEntity(name)` |
| `Tag` | tag string | the scene's live tag set |
| `AudioClip`, script path | project-relative path | `resolveProjectFile` — same confinement every other asset path gets |

**Re-resolution happens at two moments:** on graph load, and on every `AICommandBus`
mutation event. An unresolved literal is rendered **visibly broken on the node** — a red pin
and a "missing: Vault_Door" label — never a silently empty dropdown. That is the reviewer's
real point and it is kept verbatim.

**Known limitation, stated rather than hidden:** renaming an entity breaks graph references
to it, exactly as it already breaks `parentName` and tag references. The graph behaves like
the rest of the editor instead of being uniquely fragile *or* uniquely robust. Real
rename-survival needs scene-wide entity identity and belongs in its own plan.

---

## 15. Breadcrumb wrapper contract

*Addresses reviewer con #3.*

Live node highlighting (§7) requires every node's emitted code to announce itself. That
wrapping is **a single pass in `GraphCompiler.cpp`**, applied uniformly — never a line each
of the twelve emitters is individually responsible for remembering. Twelve edit points that
must stay in sync is twelve chances to drift, and the drift would be invisible until a node
mysteriously never lights up.

**Verification:** compile a reference graph with wrapping off, then on, and diff. The diff
must contain **only** wrapper lines. Anything else means the wrapper changed program
semantics, which is the one thing it must never do.

**Cost when nobody is watching:** `__gfNode(id)` resolves to an empty Lua function outside
the editor, so a shipped game pays a call per node and nothing more. If that ever shows up in
a profile, the compiler can omit the wrapper entirely for a Runtime build — but measure
before adding a second codegen mode.

---

## 16. Amended phase list

Supersedes §9. Changes are marked.

| # | Phase | Done when |
|---|---|---|
| 0 | **De-risk the dependency.** Pin an exact imgui-node-editor commit, verify its real file layout before writing CMake paths, build a throwaway window with two nodes and a wire. | A node canvas renders and a link can be dragged. Nothing else. |
| **0.5** | **NEW — literal-refresh spike.** Hand-write a `.gfgraph` referencing an entity, rename that entity in the scene, load the graph. Prove the reference shows as visibly broken rather than silently empty. Timebox: 30 minutes. | The contract in §14 is proven, or it is wrong and we find out now rather than in Phase 3. |
| 1 | **Graph model + serializer.** `GraphData.hpp`, `.gfgraph` read/write, round-trip test. No UI. | A hand-written `.gfgraph` round-trips byte-identical, **and §13's pin-mutation test passes**. |
| 2 | **Compiler + catalog.** **Internal order is fixed by §13** — lock catalog, stub emitters, golden tests, then compiler, then real emitters. | A hand-written graph compiles to Lua that `testAllShippedScriptsLoad` accepts. |
| 3 | **Panel.** Canvas, palette, details, compile bar, docked centre. Literals follow §14. | The demo graph is authorable by mouse alone. |
| 4 | **Trigger Zone.** Entity trait, gizmo, Inspector, enter/exit in `GameplayLoop`, both hosts. | Walking into a zone fires in Play **and** in the Runtime. |
| 5 | **Live node highlighting.** Wrapper per §15, golden-diff verified. | The executing node pulses, and the wrapper diff is wrapper-only. |
| **6a** | **NEW — accept/reject wireframe.** One wireframe showing the preview state, its diff against the current graph, and accept / reject / edit-before-accept. No code. | The wireframe exists and is agreed. It is the contract Phase 6b must satisfy. |
| 6b | **AI: lay out, describe, author-with-preview.** | A sentence produces a graph the user can accept or reject, matching 6a. |

**If only one thing from the review survives contact with reality, make it §13.** Pin id
stability is what serialization, link resolution, undo across graph edits, and AI authoring
all stand on.
