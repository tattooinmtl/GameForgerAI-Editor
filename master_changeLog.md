# Master Change Log — Plan & Doc Removals

Records every deletion, supersession, or plan-scope removal so nothing disappears silently. Only removals go here — additions live in the plan file itself.

Newest entries on top.

---

## 2026-09-01 — Drop installer / winget from in-flight work

**Trigger:** User instruction: people already know they must install Blender to use the MCP addon. Stop planning an editor-driven installer.

### Removed from scope
- Phase A.5 verification items **V-A2** (Inno Setup artifact) and **V-A3** (winget-Blender flow).
- Treating `installer/GameForgerAI-Editor.iss` / `post_install.ps1` / `blender_mcp.pin` as required delivery for MCP connect.
- Winget “Install Blender…” as a planned product path.

### Why
MCP connect only needs a running Blender with the addon’s HTTP server on `127.0.0.1:8765`. Shipping an installer does not unblock the AI tool loop.

### Replaced with
- In-editor **Connect** to whatever is already listening on the MCP port (editor-launched *or* user-launched).
- AI Forge / Cockpit sending the live `tools/list` catalog so the prompt can call Blender tools.

### Retained
- Files under `installer/` stay on disk as optional packaging notes; they are not a TODO.
- **Start Blender (with MCP server)** remains for machines that already have `blender.exe` — that is a launch helper, not an installer.

---

## 2026-08-28 — Phase A.0 rescope

**Trigger:** Pre-implementation inspection of the existing code before writing the CMake change.

### Removed from scope
- **`cpp-httplib` FetchContent block in root `CMakeLists.txt`** (originally spec'd in `integration_plan_allinone.md` Phase A.0).
- **`nlohmann::json` implied dependency** (never explicitly added, but the plan's `BlenderClient` sketch used `nlohmann::json`).

### Why
- The project is Windows-only and `Editor/src/AIProviderClient.cpp` already uses **WinHTTP** for outbound HTTP (with `#pragma comment(lib, "winhttp.lib")` and a working request loop).
- The project already has a first-party JSON parser at `Engine/include/GameForger/Editor/Json.hpp` (`gameforger::editor::json::Value` with `parse()` + tree traversal).
- Adding `cpp-httplib` + `nlohmann::json` would duplicate both. That contradicts the project's demonstrated "no unnecessary deps" pattern (custom JSON parser, custom Lua build, etc.).

### Replaced with
- New `BlenderClient.cpp` will use WinHTTP directly, matching `AIProviderClient.cpp`'s pattern. Only difference: HTTP not HTTPS, port 8765, optional `Authorization: Bearer <token>`.
- JSON built and parsed with the in-repo `json::Value` / `json::parse`.
- If a third HTTP consumer emerges later, extract a shared helper THEN — YAGNI until then.

### Retained
- Sample `BlenderClient` API sketch in the plan (method signatures) is unchanged; only the implementation layer differs.

---

## 2026-08-28 — Plan consolidation

**Trigger:** User instruction to unify planning into a single active document (`integration_plan_allinone.md`) and update `AGENTS.md` to point at it.

### Superseded

- **`GameManagerAddon.md`** — full content folded into `integration_plan_allinone.md` Phase D. The original file is not deleted yet; it stays as a reference until Phase D lands. Once Phase D is fully verified (V-D1..V-D20 pass), the file may be removed.
- **`GameManagerAddon.md.bak`** — earlier draft of the same plan. Safe to delete once Phase D lands; no unique content vs. the superseded `.md`.
- **`A-D-investigation.md`** — presumed superseded by the audit findings now captured under Phase D's "Change from the original plan" note. Not read line-by-line during this consolidation — flag to re-audit if any Phase-D discrepancy surfaces.

### Removed from scope (with reason)

- **`GameManagerAddon.md` § 1.2 "Inverted A/D Strafe Movement" and Step 0.3's `luaEntityGetRight` fix.**
  - **Why:** The strafe inversion was already fixed in the engine long before Phase D. Re-applying Step 0.3 would undo that fix.

    **The single fact that settles it — `getRight()` at yaw 0 must be `−X`.** At yaw 0 the entity looks `+Z`, and GLM's `lookAtRH` screen-right is `−X`. That is the direction `fps_controller.lua` strafes on **D**, so it must match the Game view exactly.

    | | Expression | `getRight().x` at yaw 0 | Verdict |
    |---|---|---|---|
    | **In the code now — keep it** | `glm::cross(forward, glm::vec3(0, 1, 0))` | **−X** | **CORRECT** |
    | What Step 0.3 proposed — reject it | `glm::cross(glm::vec3(0, 1, 0), forward)` | +X | **WRONG — this is the inverted-strafe bug** |

    Guarded in two places, both of which must keep agreeing with the table above:
    - The `DO NOT CHANGE` comment directly above `luaEntityGetRight` in `Engine/src/Editor/ScriptRuntime.cpp`.
    - `testGetRightMatchesFpsCamera` (`Engine/tests/TestMain.cpp`), which asserts `rightX < -0.5F`. If a change makes that test fail, the change is the bug — not the test.

    > **Correction, 2026-09-06.** The original wording of this entry had the two expressions transposed: it described the live code as `cross(+Y, forward)` giving `+X`, and named `cross(forward, +Y)` as the change to avoid. That is backwards on both counts — verified against the source and the test. The *conclusion* was always right (do not touch this function); only the reasoning named the wrong side. A reader following the old text literally would have "restored" the code into the actual bug.
  - **Kept:** V-D2 and V-D3 (WASD Forward/Back and Strafe L/R) survive as **regression tests** — they should already pass before Phase D touches anything and must continue to pass afterward.

### Updated

- **`AGENTS.md`** — added an "Active Plan" pointer at the top directing all work to `integration_plan_allinone.md`. Left the operational guidance (verification workflow, C++20 standards, 3D best practices, etc.) intact because that is process, not plan.

### Removed from `AGENTS.md`

- All references to `PROJECT_PLAN.md` (was a template placeholder in the original AGENTS.md, never a real file in this repo — replaced with pointers to `integration_plan_allinone.md`).
- All four references to `docs/planFix_AGY_CHANGELOG.md` as a progress-tracking destination — replaced with pointers to `integration_plan_allinone.md` Part 6 (Progress Tracking). Rationale: the plan file now IS the progress log; a separate implementation changelog would duplicate it and drift out of sync. If we later need a per-commit engineering log, it can be added back and referenced from a single place.
- The instruction "Create or update `PROJECT_PLAN.md`" during Phase 3 (Plan & Approval). Replaced with the rule: **update the active plan's Progress Tracking; do not create side plan files**.
