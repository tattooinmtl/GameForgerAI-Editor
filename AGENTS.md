# Agent Guidelines & Operational Blueprint for GameForgerAI

Welcome to the **GameForgerAI Editor** repository. All AI coding agents (Antigravity, Codex, Claude, etc.) operating in this workspace **must** adhere to the operational guidelines, architectural standards, prioritized roadmaps, and skills integration protocols defined below.

---

## 1. Master Plans & Source of Truth

When starting any coding, refactoring, debugging, or feature implementation task in this repository, **you must read and align with the following source documents**:

1. **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md) — Master Roadmap (PRIMARY DIRECTIVE, ACTIVE)**
   * **The single authoritative, prioritized plan.** Supersedes `planFix_AGY.v1.2026-08-18.md` (which is now a historical reference only, renamed and archived — do not treat it as current). Contains every phase from where the project stands today through Beta 1.0, task ownership between Claude and Gemini, target time windows, a "You Are Here" section, and the `docs/phases/` progress-log convention.
   * Also folds in everything recovered from the archived audit trail (`audit-2026-08-13.v1.md`, `PlanFixAuditResults.v1.md`, `audit.v1.2026-08-01.md`, `fix.v1.2026-08-01.md`, `IMPLEMENTATION_PLAN.md`) that never made it into any prior plan — see the RoadMap's "Recovered Backlog" and "Someday / Post-1.0 Backlog" sections.
   * **Rule:** All new development must directly correspond to a ticket in `RoadMap2026-08-21.md` (T-series tickets keep their original IDs from the archived master plan for traceability; recovered items use `R-NN` IDs; gauntlet-audit items use `DEF-NN` IDs).

2. **[`docs/phases/`](file:///C:/GameForgerAI-Editor/docs/phases/) — Per-Phase Progress Log**
   * Read `docs/phases/README.md` for the naming convention (`phaseNN.letter.seq.md`). Whenever you start or finish a ticket from the RoadMap, create or update the corresponding file here and link it back into the RoadMap's progress table for that phase — this is how any agent (or the user) can see what's actually in flight without re-reading full session transcripts.

3. **[`planFix_AGY_CHANGELOG.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY_CHANGELOG.md) — Session Changelog & Progress Tracker**
   * Living record of what has been implemented, modified, tested, and verified across sessions, now tracked against `RoadMap2026-08-21.md`.
   * **Rule:** Whenever you complete or modify a ticket, update its status in `planFix_AGY_CHANGELOG.md`.

4. **[`Compared.md`](file:///C:/GameForgerAI-Editor/docs/Compared.md) — Unity 3D Comparative Audit & Parity Specification**
   * Detailed 20-category evaluation of GameForgerAI vs. Unity 3D LTS / Unity 6. Still active — use it for the exact expected behavior, menus, inspector properties, and UX standards when implementing a RoadMap ticket for engine subsystems (Physics, Audio, PBR, Lighting, Cameras, Prefabs, Canvas, NavMesh, VFX).

5. **[`IMPLEMENTATION_PLAN.md`](file:///C:/GameForgerAI-Editor/docs/IMPLEMENTATION_PLAN.md) — High-Level Product Plan**
   * Historical architectural foundation for the native AI Forge copilot integration. Still active as background reading — two of its goals (the guided full-game-creation wizard, the provider-agnostic AI adapter layer) are still unbuilt and now tracked as `R-28`/`R-29` in the RoadMap.

6. **[`PROJECT_AUDIT_GAUNTLET_REPORT.md`](file:///C:/GameForgerAI-Editor/docs/PROJECT_AUDIT_GAUNTLET_REPORT.md) — External Gauntlet Audit**
   * Periodic external re-audit (build/test/security/data-integrity/Unity-parity) with file:line citations. Its findings are tracked as `DEF-NN` tickets in `RoadMap2026-08-21.md`. Citations can drift from current line numbers on re-reads — grep to confirm before trusting one, but treat the underlying defect claims as reliable.

7. **[`Gemini_todo_list.md`](file:///C:/GameForgerAI-Editor/docs/Gemini_todo_list.md) — Cross-Agent Task Delegation**
   * When a session splits work across agents (e.g. Claude on architecture-sensitive fixes, Gemini on mechanical/repetitive ones), the delegating agent writes the split here: exactly what the other agent owns, the skill(s) to load, the verification command to run before marking a task done, and where to log the result. **Rule:** whichever agent picks up a ticket from this file must update its own status checkbox here AND add its own dated entry to `planFix_AGY_CHANGELOG.md` AND its `docs/phases/` file — never mark a ticket done in only one place.

8. **Archived / historical reference only** (renamed with a version suffix so nothing is lost, but do not treat as current): `planFix_AGY.v1.2026-08-18.md`, `audit-2026-08-13.v1.md`, `PlanFixAuditResults.v1.md`, `audit.v1.2026-08-01.md`, `fix.v1.2026-08-01.md`, `NewAudit.v1.2026-08-02.md`. Each has a banner at the top pointing to `RoadMap2026-08-21.md`.

---

## 2. Architectural Principles & Coding Standards

* **Language Standard:** C++20.
* **Build System:** CMake (Presets in `CMakePresets.json`, Ninja Multi-Config generator, MSVC x64).
* **Component Architecture (M0.85+):** 
  * Refactor away from monolithic `SceneEntity` structs.
  * Use modular, decoupled components (`TransformComponent`, `MeshRendererComponent`, `ColliderComponent`, `LightComponent`, `AudioSourceComponent`, `ScriptComponent`).
  * Never add ad-hoc union fields to `SceneEntity`.
* **Asset Safety & Integrity:**
  * Scene saves must always be atomic (write to temporary file, flush, close, atomic rename).
  * Asset references must resolve through the `AssetDatabase` via GUIDs / `.meta` files.
* **Scripting & Security:**
  * Lua 5.4 runtime must remain strictly sandboxed (restricted standard libraries, per-script `_ENV` isolation, instruction budget via `lua_sethook`).
  * Script parameters must be exposed to the Inspector via `-- @property` annotations.
* **Command Bus Discipline:**
  * All scene mutations (create, delete, transform, reparent, animate) must route through `AICommandBus` to guarantee Undo/Redo consistency.

---

## 3. Workflow for AI Agent Sessions

```
                       AGENT EXECUTION LOOP
┌─────────────────────────────────────────────────────────────────┐
│ 1. READ docs/RoadMap2026-08-21.md & docs/planFix_AGY_CHANGELOG.md│
│    Find "You Are Here", identify your owned phase/ticket.       │
├─────────────────────────────────────────────────────────────────┤
│ 2. PREPARE: CHECK TOOLS/SKILLS/DEPS FOR THE TICKET               │
│    Read the RoadMap's per-phase prep checklist. Load skill(s)   │
│    from C:/.skills/skills/ per the matrix below. If a new        │
│    FetchContent dependency is needed, confirm it before coding. │
├─────────────────────────────────────────────────────────────────┤
│ 3. LOG START IN docs/phases/ (see docs/phases/README.md)        │
│    Create/update phaseNN.letter.seq.md, link it from the        │
│    RoadMap's progress table for that phase.                     │
├─────────────────────────────────────────────────────────────────┤
│ 4. INSPECT TARGET CODE & WRITE TESTS FIRST                      │
│    Review relevant headers/sources. Add regression test in      │
│    Engine/tests/ before modifying implementation.               │
├─────────────────────────────────────────────────────────────────┤
│ 5. EXECUTE MINIMAL, SURGICAL EDITS                              │
│    Implement the fix or feature following C++20 conventions.    │
├─────────────────────────────────────────────────────────────────┤
│ 6. VERIFY WITH A FRESH SUBAGENT, NOT YOURSELF                   │
│    Build-Project.cmd + ctest must actually run; spawn a         │
│    verification subagent (see RoadMap "Verification Protocol")  │
│    rather than self-certifying. Zero new compiler warnings.     │
├─────────────────────────────────────────────────────────────────┤
│ 7. UPDATE docs/phases/ FILE, docs/planFix_AGY_CHANGELOG.md, AND  │
│    THE ROADMAP'S STATUS COLUMN — all three, not just one.       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. Skills Integration & Situation Mapping (`C:/.skills/skills/`)

The workspace environment is backed by a repository of 117+ specialized agent skills located in **[`C:/.skills/skills/`](file:///C:/.skills/skills/)** (indexed in [`INDEX.md`](file:///C:/.skills/skills/INDEX.md)).

Before starting any task, identify the situation in the matrix below, load the skill's instructions by viewing its `SKILL.md` (e.g. `view_file` on `C:/.skills/skills/<skill-name>/SKILL.md`), and adhere to its protocol:

### Situation-to-Skill Mapping Matrix

| Situation / Task Type | Recommended Skills (`C:/.skills/skills/`) | Primary Objective / Usage Guidelines |
|---|---|---|
| **C++ Engine & Editor Coding** | [`cpp-coding`](file:///C:/.skills/skills/cpp-coding)<br>[`coder-ai-senior-developer`](file:///C:/.skills/skills/coder-ai-senior-developer)<br>[`coder-prime`](file:///C:/Users/ThePa/.gemini/config/skills/coder-prime/SKILL.md) | C++20 idioms, memory safety, RAII, pointer hygiene, CMake target configuration, and minimal blast-radius edits. |
| **Game Engine Subsystems & 3D Math** | [`gamedev`](file:///C:/.skills/skills/gamedev)<br>[`game-designer`](file:///C:/.skills/skills/game-designer)<br>[`game-ui-design`](file:///C:/.skills/skills/game-ui-design)<br>[`threejs-3d-model-editor`](file:///C:/.skills/skills/threejs-3d-model-editor) | 3D transform math, physics integration, audio spatialization, PBR rendering, camera matrices, and gizmo interaction. |
| **Bug Fixing & Defect Remediation** | [`systematic-debugging`](file:///C:/.skills/skills/systematic-debugging)<br>[`test-driven-development`](file:///C:/.skills/skills/test-driven-development) | Root-cause analysis before proposing fixes; reproducing issues with failing test cases before modifying implementation. |
| **Feature Brainstorming & Design** | [`brainstorming`](file:///C:/.skills/skills/brainstorming)<br>[`writing-plans`](file:///C:/.skills/skills/writing-plans) | Exploring requirements and UI/UX design trade-offs before entering code implementation. |
| **Plan Execution & Step Tracking** | [`executing-plans`](file:///C:/.skills/skills/executing-plans)<br>[`subagent-driven-development`](file:///C:/.skills/skills/subagent-driven-development) | Step-by-step ticket execution from `RoadMap2026-08-21.md` with explicit verification checkpoints. |
| **Verification & Quality Gate** | [`verification-before-completion`](file:///C:/.skills/skills/verification-before-completion)<br>[`requesting-code-review`](file:///C:/.skills/skills/requesting-code-review)<br>[`code-review`](file:///C:/.skills/skills/code-review) | Evidence-first validation (running `Build-Project.cmd` and `ctest`) before claiming any ticket or milestone complete. **Delegate this to a fresh subagent per the RoadMap's Verification Protocol — don't self-certify your own edit.** |
| **Parallel Tasks & Subagents** | [`dispatching-parallel-agents`](file:///C:/.skills/skills/dispatching-parallel-agents)<br>[`agent-orchestration`](file:///C:/.skills/skills/agent-orchestration) | Orchestrating concurrent subagents for independent research, audit, verification, or multi-file refactoring tasks. |
| **Branching & Worktree Isolation** | [`using-git-worktrees`](file:///C:/.skills/skills/using-git-worktrees)<br>[`finishing-a-development-branch`](file:///C:/.skills/skills/finishing-a-development-branch) | Workspace isolation for large architectural refactors (e.g. M0.85 Component Model migration). |

---

## 5. Build & Run Verification Commands

* **Configure & Build Editor (Debug):**
  ```powershell
  .\Build-Project.cmd
  ```
  *(Or via CMake CLI: `cmake --preset editor-debug && cmake --build --preset editor-debug`)*

* **Run Automated Tests:**
  ```powershell
  ctest --preset editor-debug --output-on-failure
  ```
