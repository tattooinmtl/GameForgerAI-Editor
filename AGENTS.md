# CLAUDE 3D/C++ Hybrid Operational Guide

> ## Active Plan (2026-08-28 →)
>
> The single source of truth for all in-flight work is **[`integration_plan_allinone.md`](./integration_plan_allinone.md)**. It covers, in one execution order:
> 1. Blender MCP integration (Phase A)
> 2. Multi-provider AI framework + AI Cockpit (Phases B, C)
> 3. Game Manager Addon — formerly `GameManagerAddon.md`, now Phase D
>
> Any prior floating plan files (`GameManagerAddon.md`, `GameManagerAddon.md.bak`, `A-D-investigation.md`) are superseded. See **[`master_changeLog.md`](./master_changeLog.md)** for what was removed or moved and why. When in doubt: follow `integration_plan_allinone.md`; if the plan is silent on something, ask before improvising.

A balanced framework for senior software engineering in C++ 3D engines (Unity, Blender, Unreal Engine, custom engines). Merges CLAUDE.md's disciplined verification loop with AGENTS.md's project-specific workflows for C++20, CMake, component architecture, and 3D asset integrity.

---

## Core Operating Principle

**Verify-Driven Development for 3D Systems**

```
INSPECT → PLAN → APPROVE → IMPLEMENT → VERIFY → DOCUMENT
```

Every action on a 3D engine subsystem (transform hierarchy, physics, rendering, assets, scripting) must be independently verified before claiming completion. 3D systems are state-heavy, asset-coupled, and spatially complex; guessing is not an option.

---

## 1. The 3D Engine Development Workflow

### Phase 1: Reconnaissance & Architecture Understanding

Before touching code:

1. **Identify the target subsystem** (Physics, Rendering, Audio, Transform Hierarchy, Scripting, AssetDatabase, etc.)
2. **Inspect relevant architecture:**
   - Header files defining component/system interfaces
   - Build targets and CMake configuration
   - Asset pipeline (how data flows in and out)
   - Scene serialization format
3. **Read all applicable documentation:**
   - Engine design docs (e.g., `Compared.md` for Unity parity specs)
   - RoadMap and progress logs for the target ticket
   - Physics/rendering/scripting standards
4. **Search for existing patterns:**
   - Similar components or systems
   - Test fixtures or example scenes
   - Related transformations or conversions
5. **Identify dependencies:**
   - Direct library dependencies (Lua, GLM, PhysX, etc.)
   - Inter-component dependencies (e.g., does Transform depend on Physics?)
   - External tools (modelX exporters, shader compilers, audio processors)

**Do not assume** the engine works like Unity/Unreal/Blender just because you've used them. Inspect the actual implementation.

### Phase 2: 3D Math & Spatial Correctness Pre-Check

For any work involving:
- Transform matrices (position, rotation, scale)
- Physics colliders or constraints
- Camera matrices or projections
- Skeletal animation or bone transforms
- Audio spatialization
- Raycast or spatial queries

**Pre-verify:**
- What coordinate system is in use? (right-hand vs. left-hand, Y-up vs. Z-up)
- Are rotations stored as Euler angles, quaternions, or matrices? (Euler angles are prone to gimbal lock)
- What is the transform hierarchy order? (parent → child vs. child → parent)
- Are there any handedness conversions (e.g., when importing from Blender or FBX)?
- What is the expected precision? (float vs. double, and does the engine standardize?)

Get this wrong and the entire subsystem will produce subtle, difficult-to-debug spatial bugs.

### Phase 3: Plan & Approval

Update the **Progress Tracking** section of `integration_plan_allinone.md` (move items between DONE / TODO / BLOCKED). Do **not** create side plan files. If a piece of work does not fit the active plan, propose adding a phase to it before writing code.

Historical template retained for reference only — do NOT create a new `integration_plan_allinone.md`:

```
## [Subsystem Name] [Task Description]

### Findings
- [Architecture discovered]
- [Existing patterns/code reuse opportunities]
- [Dependencies identified]
- [3D math concerns / coordinate system notes]

### Plan
1. [Concrete implementation step 1]
2. [Concrete implementation step 2]
3. [Test/verification step]
4. [Documentation update]

### Files Affected
- [engine/src/systems/TransformSystem.cpp]
- [engine/include/systems/TransformSystem.h]
- [engine/tests/TransformTests.cpp]

### 3D-Specific Risks
- [Coordinate system mismatch risk]
- [Serialization format incompatibility]
- [Asset reference breakage]
- [Performance / memory layout concerns]

### Validation Strategy
- Unit tests for math transforms (test known inputs/outputs)
- Integration test in scene hierarchy (parent-child relationships)
- Visual verification (if UI/rendering is involved)
- Asset load/save round-trip test
- Performance profile (if subsystem is hot-path)
- Regression check against existing test suite

### Approval Boundary
**Explicitly ask: "Is this plan acceptable? I will not modify the codebase until you approve."**

---

### DONE
- [Nothing yet]

### TODO
- [Implement core logic]
- [Add tests]
- [Update docs]

### BLOCKED
- [None]
```

**Do not proceed until approval.**

### Phase 4: Test-Driven Development for 3D

Before writing engine code:

1. **Write the failing test** in `engine/tests/`:
   - Input: known transform values, scene hierarchy, physics state, etc.
   - Expected output: transformed vectors, resolved collisions, serialized format, etc.
   - Verify the test actually fails with the current code.

2. **Example (Transform Test):**
   ```cpp
   TEST(TransformTests, LocalToWorldConversionWithRotation) {
       TransformComponent parent{{0, 1, 0}, {0, 90, 0}, {1, 1, 1}};
       TransformComponent child{{1, 0, 0}, {0, 0, 0}, {1, 1, 1}};
       child.parent = &parent;
       
       glm::vec3 worldPos = child.LocalToWorldMatrix() * glm::vec4{1, 0, 0, 1};
       EXPECT_NEAR(worldPos.x, 1.0f, 0.001f);  // 90° rotation should move +X to +Z
       EXPECT_NEAR(worldPos.z, 1.0f, 0.001f);
   }
   ```

3. **Run the test, confirm it fails.**

4. **Implement the minimal fix** to make the test pass.

5. **Run all tests** to ensure no regressions.

### Phase 5: Implementation with Verification Checkpoints

For complex systems, break work into small, verifiable chunks:

```
Change A (e.g., add Transform component field)
  → Compile check (no new warnings)
  → Read back the change
  → Verify syntax/logic
  
Change B (e.g., implement LocalToWorld transformation)
  → Compile check
  → Unit test passes
  → Verify math with known test cases
  
Change C (e.g., integrate with rendering pipeline)
  → Compile check
  → Related subsystem tests pass
  → Visual verification in editor/viewport
  
Change D (e.g., add serialization/deserialization)
  → Compile check
  → Round-trip test (save scene → load scene → verify data matches)
  → Asset database consistency check
```

Do not chain 10 changes and hope they work together.

### Phase 6: Build & Runtime Verification

After implementing:

1. **Configure and build:**
   ```powershell
   cmake --preset editor-debug
   cmake --build --preset editor-debug
   ```

2. **Verify no new compiler warnings:**
   ```powershell
   # Check build output for warnings
   # Warnings today → errors tomorrow
   ```

3. **Run unit tests:**
   ```powershell
   ctest --preset editor-debug --output-on-failure
   ```

4. **For rendering/physics/spatial changes, do visual verification:**
   - Launch the editor
   - Create a test scene with known geometry
   - Verify transforms render correctly
   - Verify physics responds as expected
   - Check console for asset-load errors

5. **Asset integrity check (if applicable):**
   - Load a known scene file
   - Save it to a temporary location
   - Parse both with a schema validator
   - Verify no data loss
   - Check GUID references resolve correctly

### Phase 7: Documentation & Status Update

Update all three tracking documents:

1. **integration_plan_allinone.md**
   - Move completed work to DONE section
   - Update TODO with any newly discovered items
   - Note any architecture decisions for future reference

2. **docs/phases/phaseNN.letter.seq.md** (if applicable)
   - Link from RoadMap's progress table
   - Summarize what was implemented
   - Link to relevant PRs/commits

3. **`integration_plan_allinone.md` → Part 6 (Progress Tracking)**
   - Move the finished phase item from TODO to DONE.
   - Note any newly-discovered follow-up work in TODO.
   - Move blocked items into BLOCKED with a one-line reason.
   - **Do not** create a separate changelog file — the plan is the log.

---

## 2. C++20 & Modern Engine Development Standards

### Language & Compiler

- **Standard:** C++20 (modern, move semantics, concepts, ranges, coroutines where appropriate)
- **Compiler:** MSVC x64 (target platform)
- **Build System:** CMake with Ninja Multi-Config
- **No new compiler warnings** — treat warnings as errors in your mental model

### Memory Safety & RAII

**Always:**
- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) for heap allocation
- Avoid naked `new`/`delete`
- Use stack allocation for small objects
- Follow RAII for resource cleanup (files, handles, GPU resources)

**Example (Wrong):**
```cpp
TransformComponent* transform = new TransformComponent();  // ❌ naked new
// ... later
delete transform;  // ❌ easy to forget
```

**Example (Right):**
```cpp
auto transform = std::make_unique<TransformComponent>();  // ✓ RAII
// Auto-cleanup when transform goes out of scope
```

### Component Architecture (No Monolithic Entities)

- **Do not** add ad-hoc union fields to a giant `SceneEntity` struct
- **Do** use decoupled, composable components:
  - `TransformComponent` (position, rotation, scale)
  - `MeshRendererComponent` (mesh asset, material)
  - `ColliderComponent` (shape, physics material)
  - `LightComponent` (type, intensity, color)
  - `AudioSourceComponent` (clip, volume, spatialization)
  - `ScriptComponent` (Lua script reference, parameter bindings)

**Why?** Sparse storage, cache locality, independent updating, serialization clarity, reuse.

### Command Bus & Undo/Redo

All scene mutations (create, delete, transform, reparent, animate) must route through `AICommandBus`:

```cpp
// ❌ Direct mutation (breaks undo)
entity.transform.position = {1, 2, 3};

// ✓ Command bus (preserves undo/redo)
bus.Execute(std::make_unique<SetTransformCommand>(entityID, {1, 2, 3}));
```

This ensures the editor can undo/redo user actions correctly.

---

## 3. 3D-Specific Best Practices

### Transform Hierarchies

- **Parent-child relationships must be explicit and testable.**
- Store parent reference, compute world transform on demand.
- Test transform hierarchy with known parent rotations and scales.
- Watch for gimbal lock with Euler angles — quaternions are safer.

**Example Test:**
```cpp
TEST(TransformHierarchy, RotatedParentAffectsChildWorldPosition) {
    auto parent = CreateEntity();
    auto child = CreateEntity();
    SetParent(child, parent);
    
    SetLocalPosition(child, {1, 0, 0});
    SetRotation(parent, {0, 90, 0});  // 90° around Y
    
    auto worldPos = GetWorldPosition(child);
    EXPECT_NEAR(worldPos.x, 0, 0.001f);
    EXPECT_NEAR(worldPos.z, 1, 0.001f);  // +X rotated 90° = +Z
}
```

### Asset Integrity & GUID-Based References

- **Never hardcode file paths for assets.**
- Use `AssetDatabase` with GUIDs and `.meta` files.
- Scenes save asset references by GUID, not path.
- Before serializing, verify GUIDs resolve.

**Example (Wrong):**
```cpp
meshAsset = LoadMesh("Assets/Models/Player.mesh");  // ❌ Path brittleness
```

**Example (Right):**
```cpp
meshAsset = assetDb.LoadByGUID("a1b2c3d4-e5f6-7890");  // ✓ GUID stability
```

### Scene Serialization (Atomic Writes)

- **Always write to temp file first, then atomic rename.**
- Prevents corruption on crash or IO failure.

**Example:**
```cpp
std::string tempPath = scenePath + ".tmp";
{
    std::ofstream file(tempPath, std::ios::binary);
    SerializeScene(file, scene);
    file.flush();  // Force to disk
}
std::filesystem::rename(tempPath, scenePath);  // Atomic
```

### Physics & Spatial Queries

- Test collider shapes in isolation (unit tests).
- Test physics constraints with known scenarios.
- Verify raycast results with visual debugging.
- Profile physics updates if high entity count.

### Rendering & Coordinate Systems

- **Clarify coordinate system once, document it, test against it.**
- Right-hand vs. left-hand, Y-up vs. Z-up.
- Conversion functions should be explicit (e.g., `BlenderToEngineRotation()`).
- Unit test coordinate conversions with known matrices.

**Example:**
```cpp
// Blender uses Z-up, engine uses Y-up
glm::quat BlenderToEngineRotation(const glm::quat& blenderRot) {
    // Convert Z-up rotation to Y-up rotation
    // Document the transformation exactly
}

TEST(CoordinateConversion, BlenderZUpToEngineYUp) {
    glm::quat blenderRot = glm::angleAxis(glm::radians(90.f), {0, 0, 1});
    glm::quat engineRot = BlenderToEngineRotation(blenderRot);
    // Verify against known expected value
}
```

### Lua Scripting Sandbox

- Keep Lua sandboxed: restrict standard libraries, use per-script `_ENV`.
- Instruction budget via `lua_sethook` to prevent infinite loops.
- Script parameters exposed via `-- @property` annotations parsed by inspector.

---

## 4. Verification After Every Action

### File Operations

**After creating a file:**
- Verify it exists
- Read it back
- Verify content is correct
- Verify references/includes work

**After editing a file:**
- Read the affected section back
- Verify the change is present
- Verify surrounding code wasn't accidentally altered
- Run compile/lint checks

**After renaming/moving a file:**
- Verify old path is gone
- Verify new path exists
- Search for all references/includes
- Update imports/includes if needed

### Build Verification

**After running cmake/build:**
- Check exit code (0 = success)
- Scan output for new warnings (even warnings are suspect)
- Verify expected artifacts exist (binaries, libraries)
- For executables, verify they start

### Test Verification

**After running tests:**
- Check exit code
- Count passed/failed/skipped tests
- Review any failure output
- Re-run failed tests in isolation
- Do NOT assume "command ran" = "tests passed"

### Visual Verification (3D/Rendering)

**After rendering or scene changes:**
- Launch the editor/viewport
- Create a minimal test scene
- Verify visual output matches expectation
- Check console for warnings/errors
- Test edge cases (empty scene, large counts, rotations, scales)

### Asset Verification

**After serialization changes:**
- Save a test scene
- Load it back
- Compare in-memory structures
- Verify no data loss
- Test with multiple asset types

---

## 5. Anti-Hallucination Rules for 3D Development

Never:

- **Invent 3D math.** If unsure about a matrix operation or quaternion conversion, compute it or derive it.
- **Guess coordinate system handedness.** Test it.
- **Assume asset references are valid.** Verify GUIDs resolve.
- **Claim visual correctness without running the engine.** Compilation ≠ correct rendering.
- **Skip physics validation.** Physics bugs are felt by users.
- **Ignore serialization edge cases.** Round-trip failures destroy user work.
- **Invent API contracts.** Read the actual header file.
- **Claim performance without profiling.** Optimization without measurement is guesswork.
- **Assume cross-platform compatibility.** Test on target platforms.

If uncertain about any of these, inspect or test before claiming success.

---

## 6. Priority Order for Conflicting Guidance

When instructions conflict:

1. **Explicit user requirements** (this task, this session)
2. **Safety & security** (memory safety, serialization integrity, asset safety)
3. **Repository-local instructions** (CLAUDE.md, AGENTS.md, ROADMAP, Compared.md)
4. **Architecture & existing patterns** (component model, command bus, asset DB)
5. **C++20 / CMake conventions** (RAII, smart pointers, build targets)
6. **3D engine best practices** (transform hierarchies, physics testing, rendering pipelines)
7. **Personal preference**

Never use "best practice" as an excuse to violate an explicit project requirement.

---

## 7. Definition of Done (3D Edition)

A 3D engine task is **done** only when:

- ✓ The approved scope is fully implemented
- ✓ All new C++ code follows C++20 standards, RAII, smart pointers
- ✓ Unit tests written for math/transforms/physics logic exist and pass
- ✓ Integration tests verify subsystem interactions pass
- ✓ If rendering/visual: editor verification (scenes load, render correctly, no visual artifacts)
- ✓ If assets: serialization round-trip verified (save → load → compare)
- ✓ If physics/transforms: coordinate system validated with tests
- ✓ If audio/spatial: spatialization math verified
- ✓ Build succeeds with zero new compiler warnings
- ✓ Full test suite passes (`ctest --preset editor-debug`)
- ✓ No asset references broken (GUID resolution checked)
- ✓ Performance profiling done if subsystem is hot-path
- ✓ Documentation updated (code comments, architecture docs, RoadMap progress)
- ✓ integration_plan_allinone.md updated (DONE/TODO/BLOCKED sections current)
- ✓ docs/phases/ file created/updated and linked from RoadMap
- ✓ `integration_plan_allinone.md` Progress Tracking updated (DONE/TODO/BLOCKED current)
- ✓ No silent blockers ignored

If any of these are incomplete, say so explicitly.

---

## 8. Workflow Checklist for a Typical 3D Feature

Use this checklist for subsystem features (Transform, Physics, Rendering, Audio, etc.):

```
[ ] 1. READ RoadMap2026-08-21.md and identify owned ticket
[ ] 2. READ Compared.md (if implementing Unity parity feature)
[ ] 3. INSPECT target subsystem architecture (headers, CMakeLists.txt)
[ ] 4. IDENTIFY 3D math concerns (coordinate system, handedness, precision)
[ ] 5. SEARCH for existing related patterns/tests
[ ] 6. UPDATE integration_plan_allinone.md with findings, architecture, risks, 3D specifics
[ ] 7. ASK FOR APPROVAL (explicit)
[ ] 8. WRITE failing test case(s) in engine/tests/
[ ] 9. VERIFY test fails with current code
[ ] 10. IMPLEMENT minimal fix in source
[ ] 11. VERIFY test passes
[ ] 12. RUN full test suite (ctest)
[ ] 13. VERIFY no new compiler warnings
[ ] 14. VERIFY serialization round-trip (if applicable)
[ ] 15. VISUAL verification in editor (if rendering/physics involved)
[ ] 16. UPDATE integration_plan_allinone.md DONE section
[ ] 17. CREATE/UPDATE docs/phases/ file
[ ] 18. UPDATE `integration_plan_allinone.md` Progress Tracking (DONE section)
[ ] 19. UPDATE RoadMap progress table for the phase
[ ] 20. REPORT completion with evidence
```

---

## 9. Tools & Commands Reference

### CMake & Build

```powershell
# Configure debug build
cmake --preset editor-debug

# Build
cmake --build --preset editor-debug

# Or one-liner
.\Build-Project.cmd
```

### Testing

```powershell
# Run all tests with output on failure
ctest --preset editor-debug --output-on-failure

# Run specific test
ctest --preset editor-debug -R TransformTests --output-on-failure
```

### Inspection

```bash
# Search for symbol/pattern in codebase
grep -r "TransformComponent" --include="*.h" --include="*.cpp"

# Find files by name
find . -name "*Transform*.cpp" -o -name "*Transform*.h"

# Inspect git history for a file
git log --oneline -- engine/src/systems/TransformSystem.cpp
```

### Verification

```bash
# Check for uncommitted changes
git status

# Review diff before committing
git diff engine/src/

# Inspect recent commits
git log --oneline -10
```

---

## 10. Anti-Patterns & What Not to Do

### ❌ Monolithic Entities
```cpp
struct SceneEntity {
    Transform transform;
    Mesh mesh;
    Collider collider;
    Light light;
    Audio audio;
    // ... 50 more fields ...
    // Some entities use only 2 fields, wasting memory
};
```

### ✓ Component Architecture
```cpp
struct TransformComponent { /* position, rotation, scale */ };
struct MeshRendererComponent { /* mesh, material */ };
struct ColliderComponent { /* shape, physics material */ };
// Entities are sparse collections of components
```

---

### ❌ Direct Path References
```cpp
Mesh mesh = LoadMesh("Assets/Models/Player.mesh");  // Brittle
```

### ✓ GUID-Based Asset References
```cpp
Mesh mesh = assetDb.LoadByGUID("{guid}");  // Stable
```

---

### ❌ Hardcoded Quaternion Conversions
```cpp
// Guessing how Blender's Z-up converts to engine's Y-up
glm::quat q = glm::quat(x, y, z, w);  // Probably wrong
```

### ✓ Explicit, Tested Conversions
```cpp
glm::quat BlenderToEngineRotation(const glm::quat& blender) { /* ... */ }
TEST(CoordinateConversion, BlenderRotation) { /* verify with known value */ }
```

---

### ❌ Direct Mutation, Breaks Undo
```cpp
entity.transform.position = {1, 2, 3};  // Editor undo broken
```

### ✓ Command Bus, Preserves Undo
```cpp
bus.Execute(std::make_unique<SetTransformCommand>(entityID, {1, 2, 3}));
```

---

### ❌ Guessing Serialization
```cpp
// Save some fields, hope others aren't needed later
file.write(position);  // Incomplete
```

### ✓ Round-Trip Verified
```cpp
// Save all fields, load, compare original vs. loaded
file.write(position, rotation, scale, metadata);
auto loaded = LoadScene(file);
ASSERT_EQ(loaded.entities.size(), original.entities.size());
```

---

## 11. Integration with Existing RoadMap & Skills

Before every major task:

1. **Read RoadMap2026-08-21.md** — find your owned ticket, understand its phase
2. **Load relevant skills** from `C:/.skills/skills/` (configured via `.agents/skills.json`):
   - `supercoder` — Elite AI Programmer & Architect for senior-level engineering
   - `superpowers` / `using-superpowers` — Structured process workflow & quality gates
   - `cpp-coding` — C++20 idioms and memory safety
   - `gamedev` — 3D math, physics, audio, rendering
   - `systematic-debugging` — root-cause analysis before fixing
   - `test-driven-development` — write tests first
   - `verification-before-completion` — don't self-certify, use a subagent
3. **Update integration_plan_allinone.md** and ask for approval before coding
4. **Track progress** in `integration_plan_allinone.md` (Progress Tracking section). Record only plan/scope REMOVALS in `master_changeLog.md`.
5. **Delegate verification** to a fresh subagent if the task is complex

---

## Summary

This hybrid model combines:
- **CLAUDE.md's engineering rigor:** plan before code, verify after every action, no hallucination
- **AGENTS.md's project discipline:** RoadMap alignment, skills integration, documented workflows
- **3D engine best practices:** spatial correctness, asset integrity, serialization safety, physics/rendering validation
- **C++20 standards:** RAII, smart pointers, modern idioms, component architecture

**The core rule:** For 3D systems, verification is not optional. Verify spatial correctness with tests, asset integrity with round-trips, rendering with the viewport, physics with known scenarios. Document everything. Track progress. Ask before you code.

**Use this guide** when you're working on GameForgerAI or any C++/3D engine project. It will keep you safe from spatial bugs, asset corruption, unserialization failures, and silent regressions.
