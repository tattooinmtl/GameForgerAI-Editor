# GameForgerAI-Editor — Integration Plan (All-in-One)

> **Active planning doc, effective 2026-08-28.**
> Consolidates three previously separate streams into one execution order:
> 1. **Blender MCP integration** (external `MCP_Server_blender` repo → editor)
> 2. **Multi-provider AI framework + AI Cockpit** (agentic tool-use over scene + Blender)
> 3. **Game Manager Addon** (from `GameManagerAddon.md`, folded in as Phase D)
>
> This file supersedes `GameManagerAddon.md`, `GameManagerAddon.md.bak`, `A-D-investigation.md`, and any other floating plan file. See `master_changeLog.md` for what was moved or removed.
>
> All line numbers are cross-referenced against the live codebase at time of writing. Bump this note when the plan is re-verified.

---

## Part 1 — Vision

One editor. One conversation with an AI of the user's choosing. The AI can:

- Read and mutate the game scene (create/move/delete/property-set entities, attach scripts, save, play/stop).
- Read and mutate an open Blender session (model, texture, rig, render, export glTF).
- Bridge the two: sculpt a mesh in Blender → export → import into the scene → place → attach behaviour — all in one prompt.

The plumbing that makes this possible is unglamorous:

- The editor speaks JSON-RPC 2.0 over HTTP to Blender on `127.0.0.1:8765`.
- The Blender side is the upstream project [`tattooinmtl/MCP_Server_blender`](https://github.com/tattooinmtl/MCP_Server_blender) — installed unchanged, cloned by the installer.
- The AI is provider-agnostic (Anthropic, OpenAI, MiniMax, Moonshot, NVIDIA, Antigravity, OpenRouter, Agnes-AI, plus user-defined).
- Undo is doubled: the existing Edit → Undo / Ctrl-Z stack keeps working for scene mutations via `AICommandBus`. A new bounded AI-action ring buffer (last 5–10 actions) sits beside the AI prompt and rolls back both scene AND Blender operations in one click.

---

## Part 2 — Architecture

```
┌──────────────────────────── AI Cockpit Panel (ImGui) ────────────────────────────┐
│  Provider dropdown  |  Model dropdown  |  Reasoning-effort  |  Approval toggle   │
│  ─────────────────────────────────────────────────────────────────────────────── │
│  Chat log ..................... Tool trace ................. Undo (⏪ x5–10)     │
└─────────────────────────────────────────┬────────────────────────────────────────┘
                                          │
                              ┌───────────▼──────────┐
                              │   AgenticLoop        │ reuses AIProviderClient (HTTP)
                              │  · sends tools[]     │ supports:
                              │  · handles tool_use  │  - Anthropic native tool_use
                              │  · until stop_reason │  - OpenAI-compat function-call
                              └───┬──────────────┬───┘
                                  │              │
                        ┌─────────▼──┐      ┌────▼──────────────────┐
                        │ SceneTools │      │ BlenderTools          │
                        │ wraps      │      │ forwards to           │
                        │ AICommand  │      │ BlenderClient →       │
                        │ Bus        │      │ http://127.0.0.1:8765 │
                        │ (undoable) │      │ /mcp                  │
                        └─────────┬──┘      └────┬──────────────────┘
                                  │              │
                                  ▼              ▼
                            Scene entities   Blender bpy
                            (.gfprod files)  (in-process MCP addon)
```

External process boundary: `blender.exe` is launched by the editor via `CreateProcessW`; its addon (installed by the editor's installer) starts the HTTP server. Editor and Blender are two OS processes; the only channel between them is `127.0.0.1:8765`.

---

## Part 3 — Files Touched (Master Table)

| Area | File | Phase | Change |
|---|---|---|---|
| CMake | `CMakeLists.txt` (root) | A.0 | Add `cpp-httplib` via `FetchContent` |
| Blender HTTP client | `Editor/include/GameForger/Editor/BlenderClient.hpp` (NEW) | A.1 | JSON-RPC 2.0 client (Editor-only — Runtime never talks to Blender) |
| | `Editor/src/BlenderClient.cpp` (NEW) | A.1 | Impl. (WinHTTP, matches `AIProviderClient.cpp` layout) |
| Blender process launcher | `Editor/include/GameForger/Editor/BlenderLauncher.hpp` (NEW) | A.2 | Detect + spawn `blender.exe` |
| | `Editor/src/BlenderLauncher.cpp` (NEW) | A.2 | Impl. (Windows: `CreateProcessW`, registry, `%ProgramFiles%` glob) |
| Blender ImGui panel | `Editor/src/main.cpp` | A.3 | New top-menu `Blender`; new dockable panel |
| Installer | `installer/GameForgerAI-Editor.iss` (NEW) | A.4 | Inno Setup script |
| | `installer/blender_mcp.pin` (NEW) | A.4 | Pinned upstream MCP commit SHA |
| | `installer/post_install.ps1` (NEW) | A.4 | winget-Blender, git-clone MCP, run its `install_addon.ps1` |
| AI provider framework | `Editor/include/GameForger/Editor/AIProviderClient.hpp` | B.0 | Extend for presets + custom + capabilities |
| | `Editor/src/AIProviderClient.cpp` | B.0 | Impl. |
| | `Editor/include/GameForger/Editor/AIProviderPresets.hpp` (NEW) | B.1 | Hard-coded preset table (endpoints, schemas) |
| Provider Settings UI | `Editor/src/main.cpp` | B.4 | New `AI → Provider Settings…` window |
| Agentic loop | `Editor/include/GameForger/Editor/AgenticLoop.hpp` (NEW) | C.0 | Multi-turn tool_use driver |
| | `Editor/src/AgenticLoop.cpp` (NEW) | C.0 | Impl. (Anthropic + OpenAI-compat) |
| Scene tools | `Editor/include/GameForger/Editor/SceneTools.hpp` (NEW) | C.1 | Wraps `AICommandBus` |
| | `Editor/src/SceneTools.cpp` (NEW) | C.1 | Impl. |
| Blender tools proxy | `Editor/include/GameForger/Editor/BlenderTools.hpp` (NEW) | C.2 | Auto-fetch from MCP `tools/list` |
| | `Editor/src/BlenderTools.cpp` (NEW) | C.2 | Impl. |
| Bridge tool | (in `BlenderTools.cpp`) | C.3 | `bridge.export_from_blender_to_scene` |
| Cockpit panel | `Editor/src/main.cpp` | C.4 | New `AI → Open AI Cockpit…` |
| AI undo ring | `Editor/include/GameForger/Editor/AIActionRing.hpp` (NEW) | C.5 | Bounded ring (10 entries) |
| | `Editor/src/AIActionRing.cpp` (NEW) | C.5 | Impl. |
| ScriptRuntime refactor | `Engine/include/GameForger/Editor/ScriptRuntime.hpp` | D.0..D.2 | `ScriptRuntimeConfig`; new callbacks; `on_end`; `registeredManagers_` |
| | `Engine/src/Editor/ScriptRuntime.cpp` | D.0..D.2 | Impl. `self.gameManager`, `self.managers`, `self.audio`; `on_end` dispatch |
| Editor init call site | `Editor/src/main.cpp:1742–1771` | D.0 | Replace flat init with `ScriptRuntimeConfig` |
| Runtime init call site | `Runtime/src/main.cpp:442–472` | D.0 | Same |
| Prefab scripts | `Game/Scripts/game_manager.lua` (NEW) | D.1 | |
| | `Game/Scripts/audio_manager.lua` (NEW) | D.2 | |
| | `Game/Scripts/fps_controller.lua` | D.3 | Add `setCursorLock` + `managers:register/unregister` + `on_end` |
| | `Game/Scripts/third_person_controller.lua` | D.3 | Same |
| `lockCursor` removal | `Engine/include/GameForger/Editor/EditorScene.hpp:29–36` | D.4 | Remove field + block comment |
| | `Engine/src/Editor/EditorScene.cpp:590–597` | D.4 | Remove `SetPropertyCommand "Camera"/lockCursor` branch |
| | `Engine/src/Editor/SceneSerializer.cpp:248` | D.4 | Remove read |
| | `Engine/src/Editor/SceneSerializer.cpp:505` | D.4 | Remove write |
| | `Editor/src/main.cpp:5090–5099` | D.4 | Remove Inspector checkbox; add hint |
| | `Editor/src/main.cpp:2499–2501` | D.4 | Rewrite `wantsCursorLock` (manager registry + desired flag) |
| | `Runtime/src/main.cpp:556` | D.4 | Same rewrite |
| Presets | `Editor/src/main.cpp:5191, 5249, 5443–5457` | D.5 | 8 → 10 entries; fix Utility-preset Collider gating |
| ScriptGenerator | `Editor/src/ScriptGenerator.cpp:98–112` | D.5 | Full Lua API catalog in system prompt |
| Docs | `AGENTS.md` | (this doc) | Point at this plan; strip other plan references |
| | `master_changeLog.md` (NEW) | (this doc) | Removal trace |

---

## Part 4 — Phased Implementation

Phases are **independently landable** and verifiable. Order below is the recommended execution order; A and D are technically parallelisable but sharing PRs is safer.

---

### PHASE A — Blender MCP Infrastructure

Goal: editor can start Blender, install the addon if missing, and successfully round-trip `initialize` / `tools/list` / `tools/call` JSON-RPC over HTTP.

#### A.0 — HTTP stack decision: reuse existing WinHTTP

**Decision:** No new HTTP dependency. The project is Windows-only and `Editor/src/AIProviderClient.cpp` already speaks WinHTTP (`#pragma comment(lib, "winhttp.lib")`) with a working request loop. JSON is handled by the in-repo parser at `Engine/include/GameForger/Editor/Json.hpp` (`gameforger::editor::json::Value` — parse + tree traversal). Adding `cpp-httplib` + `nlohmann::json` would duplicate both.

**Rule:** Copy the WinHTTP pattern into `BlenderClient.cpp` directly. If a third HTTP client emerges later, extract a shared helper at that point (YAGNI until then).

**CMake:** No changes needed. `BlenderClient.cpp` and `BlenderLauncher.cpp` link `winhttp.lib` and `Advapi32.lib` (registry access) via `target_link_libraries(GameForgerEditor PRIVATE winhttp Advapi32)` in `Engine/CMakeLists.txt` — verify the target name during impl.

See `master_changeLog.md` (2026-08-28 entry "Phase A.0 rescope") for the removal trace.

#### A.1 — `BlenderClient` (JSON-RPC 2.0 over HTTP)

Header sketch:

```cpp
class BlenderClient {
public:
    struct Config { std::string host = "127.0.0.1"; int port = 8765; std::string bearerToken; std::chrono::milliseconds timeout{5000}; };
    struct ToolInfo { std::string name; std::string description; nlohmann::json inputSchema; };

    explicit BlenderClient(Config cfg);
    bool ping();                            // GET /mcp health
    std::vector<ToolInfo> listTools();      // JSON-RPC "tools/list"
    nlohmann::json callTool(const std::string& name, const nlohmann::json& args);  // "tools/call"
    // Non-blocking variants used by cockpit — dispatched on worker thread, result queued back to main.
    void callToolAsync(std::string name, nlohmann::json args, std::function<void(nlohmann::json, std::optional<std::string> err)> cb);
};
```

Impl. notes:
- All calls issued on a **worker thread**; results routed back to the main thread via a lock-free queue drained in the ImGui frame loop. Blocking HTTP would freeze the editor.
- JSON: reuse whatever JSON lib the editor already ships (`AIProviderClient.cpp` uses it — inherit the same include).
- Retries: `ping` retries up to 20× at 250 ms while waiting for Blender startup; other calls: no retry (surface error to caller).

#### A.2 — `BlenderLauncher`

Detection order for `blender.exe`:
1. Environment variable `GAMEFORGER_BLENDER_EXE` (test-friendly override).
2. Registry: `HKLM\SOFTWARE\Classes\blendfile\shell\open\command` default value → parse.
3. Glob: `%ProgramFiles%\Blender Foundation\Blender *\blender.exe`, sorted descending by version.
4. `%PATH%` search.

Launch:

```cpp
CreateProcessW(
    blenderExePath,
    L"blender.exe --python-expr \"import bpy; bpy.ops.preferences.addon_enable(module='blender_mcp_addon'); bpy.ops.blender_mcp.start_server()\"",
    ...);
```

If none found, expose `BlenderLauncher::isBlenderInstalled()` returning `false`; the panel uses this to gate the "Install Blender…" button.

Install offer (from the panel, on user click): shell out to `winget install BlenderFoundation.Blender`. Fallback: `ShellExecute` `https://www.blender.org/download/`.

#### A.3 — Blender panel + top-menu

In `Editor/src/main.cpp` main-menu-bar block (find `ImGui::BeginMainMenuBar` — pin exact line during impl.):

Top-level menu **Blender**:
- Start Blender  (grays out when running)
- Stop Blender  (grays out when not running)
- Start MCP Server  (sends `bpy.ops.blender_mcp.start_server` via `--python-expr` re-launch OR via already-running Blender's HTTP `execute_python`)
- Stop MCP Server
- Reinstall Addon…  (runs bundled `install_addon.ps1` again — for upgrades)
- ────────
- Open Blender Panel

Panel shows: state (Stopped/Starting/Running/Error), endpoint URL, ping latency, count of tools discovered, tool list (collapsible), scratchpad textbox to send `code.execute_python`. Panel is dockable next to Inspector.

#### A.4 — Installer

Create `installer/GameForgerAI-Editor.iss` (Inno Setup, Windows-only for v1):

- `[Files]` — editor binaries + `installer/blender_mcp.pin` + `installer/post_install.ps1`.
- `[Run]` — after install, execute `post_install.ps1` (PowerShell, elevated only if needed).

`installer/blender_mcp.pin` — one line, the commit SHA of `tattooinmtl/MCP_Server_blender` we're pinning to. Bumping this file is the ONLY change needed to consume a new MCP release.

`installer/post_install.ps1` sketch:
```powershell
param([string]$InstallDir)
$mcpPin = (Get-Content "$InstallDir\blender_mcp.pin").Trim()
$mcpDir = "$env:LOCALAPPDATA\GameForgerAI\blender_mcp"

# 1. Blender check
if (-not (Get-Command blender -ErrorAction SilentlyContinue) `
    -and -not (Test-Path "$env:ProgramFiles\Blender Foundation\Blender*\blender.exe")) {
  $ans = [System.Windows.Forms.MessageBox]::Show("Blender is not installed. Install now via winget?", "GameForgerAI", "YesNo")
  if ($ans -eq 'Yes') { winget install --id BlenderFoundation.Blender -e --accept-source-agreements --accept-package-agreements }
}

# 2. git check
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
}

# 3. Clone/update MCP repo at pinned SHA
if (-not (Test-Path $mcpDir)) { git clone https://github.com/tattooinmtl/MCP_Server_blender.git $mcpDir }
Push-Location $mcpDir
git fetch --depth=50 origin
git checkout $mcpPin
Pop-Location

# 4. Install addon into every discovered Blender version
& "$mcpDir\scripts\install_addon.ps1"

# 5. Optional overlay: if the editor ships MCP-side extensions, copy on top now.
if (Test-Path "$InstallDir\mcp_overlay") {
  Copy-Item "$InstallDir\mcp_overlay\*" -Destination "$mcpDir\addon\blender_mcp_addon" -Recurse -Force
}
```

Overlay folder is empty for v1 — reserved for the day we need `gameforger.export_glb` or similar first-class tools inside the MCP addon.

#### A.5 — Verification (V-A)

| # | Test | Pass |
|---|------|------|
| V-A1 | Build editor with `cpp-httplib` linked | `cmake --build --preset editor-debug` exit 0, no new warnings |
| V-A2 | Installer produces artifact | `iscc GameForgerAI-Editor.iss` succeeds; installer runs on a clean VM |
| V-A3 | winget-Blender flow | Uninstall Blender, run installer, accept prompt, verify Blender ends up installed |
| V-A4 | Addon copied to Blender addons dir | `%APPDATA%\Blender Foundation\Blender\*\scripts\addons\blender_mcp_addon\` exists |
| V-A5 | Round-trip `tools/list` | Panel shows non-empty tool list within 5 s of clicking Start Blender |
| V-A6 | Scratchpad `execute_python` | Sending `bpy.ops.mesh.primitive_cube_add()` creates a cube in Blender's viewport |

---

### PHASE B — Multi-Provider AI Framework

Goal: user can add/select AI providers via preset cards or a custom endpoint form, test the key, discover models, scan capabilities, and pick a model from a ranked dropdown.

#### B.0 — Extend `AIProviderClient`

Current `AIProviderClient` is single-request. Add:
- Provider record: `{ id, name, endpoint, apiKey, schema (anthropic|openai_compat|custom), supportsTools, supportsThinking, supportsReasoningEffort, modelListPath, modelListParser }`.
- Persist to `%APPDATA%\GameForgerAI\providers.json` (never inside a project dir — API keys must not leak into scenes).
- `testConnection(providerId) → { ok, error, latencyMs }` — sends a minimal "hello" completion.
- `discoverModels(providerId) → vector<ModelInfo>` — hits provider's `/models` or equivalent.
- `scanCapabilities(providerId, modelId) → { supportsTools, supportsThinking, supportsReasoningEffort, maxContext, provisional }` — sends probe requests to detect actual behaviour (some providers advertise tools support that doesn't work reliably).

#### B.1 — Preset table

`AIProviderPresets.hpp` ships hard-coded cards for:

| Preset | Endpoint | Schema | Notes |
|---|---|---|---|
| Anthropic Claude | `https://api.anthropic.com/v1` | `anthropic` | Native `tools`/`tool_use`; `thinking`; extended-context models flagged |
| OpenAI | `https://api.openai.com/v1` | `openai_compat` | Function-calling |
| MiniMax | `https://api.minimax.chat/v1` | `openai_compat` | Verify actual base URL during impl. |
| Moonshot (Kimi) | `https://api.moonshot.cn/v1` | `openai_compat` | |
| NVIDIA (NIM) | `https://integrate.api.nvidia.com/v1` | `openai_compat` | |
| Antigravity | *TBD — research during impl.* | *TBD* | |
| OpenRouter | `https://openrouter.ai/api/v1` | `openai_compat` | Exposes many upstream models |
| Agnes-AI | *TBD — research during impl.* | *TBD* | |

Presets are seed data; the user can edit any field after loading a preset.

#### B.2 — "Add Custom Provider" card

Blank card with fields: name, endpoint, API key, schema (dropdown of `anthropic` / `openai_compat` / `custom`), models-list URL (optional; defaults from schema).

**Test** button:
1. Save (in-memory only until test passes).
2. Call `testConnection` — show latency + inline error.
3. On success, `discoverModels` populates the model dropdown.
4. Only after a green test does the provider land in the persisted `providers.json`.

#### B.3 — Model discovery + capability scan

After discovery, each model gets a `scanCapabilities` pass (throttled, cached). Tool support is confirmed by sending a probe call with a trivial tool schema and checking the response shape. Thinking / reasoning-effort are inferred from provider schema; scan is best-effort.

#### B.4 — Provider Settings UI

New menu item **AI → Provider Settings…** opens a modal:

```
┌─ Providers ─────────────────────────────────────────────┐
│ [+ Add from preset ▾]  [+ Add custom]  [🗑 Remove]      │
│ ─────────────────────────────────────────────────────── │
│ ● Anthropic Claude   [key: sk-ant-…  ] [Test] ✓ 130ms   │
│ ○ OpenAI             [key: sk-…      ] [Test] ✓  94ms   │
│ ○ OpenRouter         [key: sk-or-…   ] [Test] – untested│
│ ...                                                     │
├─ Selected model ────────────────────────────────────────┤
│ Model:  [claude-opus-4-7                         ▾]     │
│   ✓ tools  ✓ thinking  ✓ reasoning-effort  200k ctx     │
│ Reasoning effort:  [ minimal | low | medium | high ]    │
└─────────────────────────────────────────────────────────┘
```

The model dropdown sorts using the relevance ranker (B.5).

#### B.5 — Model relevance ranking

Score = base rank from provider order + bonuses for keywords in model name/description/tags:
- `+3` for `tool` / `function` / `agent`
- `+2` for `vision` / `multimodal` (Blender viewport screenshots are useful)
- `+2` for `code` / `coder`
- `+1` for `reasoning` / `thinking`
- `−2` for `chat` / `instruct` without any of the above

Top-scored models bubble to the top of the dropdown. User can force-favourite a model to pin it.

#### B.6 — Verification (V-B)

| # | Test | Pass |
|---|------|------|
| V-B1 | Load Anthropic preset, paste key, Test | Green ✓ within 3 s; model list populates |
| V-B2 | Load OpenAI preset likewise | Same |
| V-B3 | Add custom provider (OpenAI-compat clone) | Green ✓; models list; capability scan produces sane flags |
| V-B4 | Persist across restart | `providers.json` reload restores providers and last-selected model |
| V-B5 | Bad key → red error, no persist | providers.json unchanged |
| V-B6 | Ranking sanity | For OpenRouter, `claude-*` / `gpt-4o-*` / models named `-coder` / `-tools` land above generic chat models |

---

### PHASE C — AI Cockpit (Agent Loop + Tool Router)

Goal: user opens **AI → Open AI Cockpit…**, sees a chat panel, types a prompt like *"Model a small treasure chest in Blender, export it as GLB, import it into the scene at (5, 0, 3), and give it a spin script."* — and the model does it, autonomously, respecting destructive-op approval.

#### C.0 — `AgenticLoop`

Handles the multi-turn tool_use loop. Two dialects for v1:

- **Anthropic native** (`schema: anthropic`): `messages` API with `tools: [...]`; response `content` contains `tool_use` blocks; we execute and reply with `tool_result` blocks.
- **OpenAI-compat** (`schema: openai_compat`): `chat/completions` with `tools`; response `tool_calls`; reply with `role: "tool"` messages.

Loop exit conditions: `stop_reason` = `end_turn` / `stop`, or the model produced no tool calls in the last turn, or a hard cap (default 40 tool calls per user prompt) to prevent runaway.

Streaming: v1 non-streaming (simpler); v2 add SSE for live token streaming.

#### C.1 — `SceneTools`

Wraps `AICommandBus` so every mutation is undoable. Tools exposed:

| Tool | Args | Returns |
|---|---|---|
| `scene.list_entities` | – | array of `{ id, name, tags, transform }` |
| `scene.get_entity` | `id` | full entity |
| `scene.select` | `id` | ok |
| `scene.create_entity` | `{ name, transform, components? }` | new `id` |
| `scene.delete_entity` | `id` | ok — **destructive, requires approval** |
| `scene.duplicate` | `id` | new `id` |
| `scene.set_transform` | `{ id, position?, rotation?, scale? }` | ok |
| `scene.set_property` | `{ id, group, key, value }` | ok |
| `scene.attach_script` | `{ id, path }` | ok |
| `scene.import_model` | `{ path, place_at?, name? }` | new `id` |
| `scene.save_scene` | `{ path? }` | absolute path saved |
| `scene.play` | – | ok |
| `scene.stop` | – | ok |

Non-destructive tools run freely. Destructive tools (`delete_entity`, `save_scene` overwriting an existing file, `stop` mid-play) show an inline **Approve / Reject** button in the tool trace and pause the loop until answered — UNLESS the user has toggled **Autonomous mode** and explicitly enabled destructive-op autonomy (two-click opt-in, never default).

#### C.2 — `BlenderTools`

On cockpit connect (or first prompt after Start Blender), fetch `tools/list` from the MCP and wrap each. The wrapper prepends `blender.` to every tool name.

Auto-discovery means when the upstream MCP adds a tool, the cockpit sees it after next reconnect — no editor rebuild.

Same destructive-gating heuristic applies: any tool whose name contains `delete`, `remove`, `clear`, `overwrite`, or is `code.execute_python` requires approval. `code.execute_python` always requires approval regardless of autonomous-mode setting (arbitrary bpy is too dangerous).

#### C.3 — Bridge tool

Registered inside `BlenderTools` for convenience:

- `bridge.export_from_blender_to_scene` — args: `{ blender_object_name, dest_path?, place_at? }`. Runs (a) `blender.code.execute_python` with a small glTF export script, (b) `scene.import_model` on the resulting file, (c) `scene.set_transform` if `place_at` set. Returns the new scene entity `id`.

This is the killer path. Model in Blender → one call → object in the game.

#### C.4 — Cockpit panel

New menu item **AI → Open AI Cockpit…**. Docked (default) or floating.

Layout:
```
┌ Cockpit ────────────────────────────────────────────────┐
│ Provider: [Anthropic ▾]  Model: [claude-opus-4-7 ▾]     │
│ Reasoning: [medium ▾]  Autonomous: [ ] Approve destr [x]│
│ ─────────────────────────────────────────────────────── │
│ [chat messages, scrollable]                             │
│                                                         │
│ [tool trace, scrollable, filterable]                    │
│                                                         │
│ ⏪ Undo last AI action  (ring: 3 of 10 available)       │
│ ─────────────────────────────────────────────────────── │
│ [prompt textbox            ] [Send] [Stop] [Clear]      │
└─────────────────────────────────────────────────────────┘
```

#### C.5 — `AIActionRing`

Bounded ring buffer, default capacity 10. Each entry:

```cpp
struct AIAction {
    std::string toolName;
    nlohmann::json args;
    std::variant<std::monostate,           // no-op (read-only tool)
                 AICommandBus::TokenId,    // scene op, undo via bus
                 BlenderUndoToken          // blender op, undo via bpy.ops.ed.undo (best-effort)
                > undoHandle;
    std::chrono::system_clock::time_point at;
};
```

Undo button pops the newest entry and dispatches:
- Scene → `commandBus.undo()` once (or N times if the tool bundled multiple commands — the wrapper records how many).
- Blender → `BlenderClient::callTool("code.execute_python", {"code": "bpy.ops.ed.undo()"})` N times (where N was captured by wrapping the tool call in `bpy.ops.ed.undo_push` grouping — see design note).

**Design note on Blender undo reliability:** Blender's undo stack is per-operator-invocation. If a tool call runs a single `bpy.ops.*`, one `bpy.ops.ed.undo()` reverses it. If a tool call runs arbitrary Python that mutates multiple objects, `bpy.ops.ed.undo` only pops one step. Mitigation: wrap every Blender tool call in an explicit undo grouping via `bpy.context.window_manager.event_timer_add` + `bpy.ops.ed.undo_push({'name': 'ai_op'})` bracket. Falls back to "best effort" for `code.execute_python` — the tool trace records the raw code so the user can manually revert.

Ring overflow: oldest entry drops silently. Ring is scene-lifetime, not session-lifetime (cleared on scene load/new).

#### C.6 — Destructive-op approval

Two-tier approval:
- Non-destructive: run automatically.
- Destructive: pause loop, show approve/reject in tool trace, resume on click. If autonomous mode is on AND the "Approve destructive automatically" checkbox is also on (two toggles), skip prompt for name-based destructive ops but STILL prompt for `code.execute_python`.

Approval decisions are logged so we can audit later.

#### C.7 — Verification (V-C)

| # | Test | Pass |
|---|------|------|
| V-C1 | Open cockpit with no provider configured | Panel shows "Configure a provider" nudge; Send disabled |
| V-C2 | Simple scene prompt ("list all entities") | Model calls `scene.list_entities`, replies with count |
| V-C3 | Simple Blender prompt ("add a cube") | Model calls `blender.objects.create_primitive` (or `code.execute_python` with approval); cube appears |
| V-C4 | Bridge prompt (export Blender selection → scene) | New entity present in scene; model file exists in `models/`; trace shows all three underlying tool calls |
| V-C5 | Destructive gating | Prompt "delete Player" pauses on approval; rejecting cancels; approving deletes |
| V-C6 | AI-side undo (scene) | Create three entities via prompt; click Undo 3× → scene empty; scene-Undo/Ctrl-Z stack also reflects this |
| V-C7 | AI-side undo (Blender) | Create three Blender objects; Undo 3× → objects gone; Blender's own Undo history matches |
| V-C8 | Runaway cap | Prompt an infinite-tool-call scenario; loop hits cap of 40 and surfaces a clean error, not a hang |

---

### PHASE D — Game Manager Addon (from former `GameManagerAddon.md`)

Goal: fix the "Lock Cursor" checkbox appearing on every entity by moving cursor lock ownership from a per-entity field to a script-driven Game Manager. Also add `on_end` lifecycle, `self.managers` registry, `self.audio` stub, and audio/game_manager prefab scripts.

**Change from the original plan:** The `luaEntityGetRight` "strafe fix" has been **removed from scope**. Verification against `Engine/src/Editor/ScriptRuntime.cpp:183–194` confirmed the code already computes `cross(+Y, forward)` correctly (comment at lines 188–190 documents the historical fix). The plan's Step 0.3 would have re-introduced the exact bug it claimed to fix. See `master_changeLog.md` for details.

#### D.0 — `ScriptRuntimeConfig` refactor

- Add `ScriptRuntimeConfig` struct in `ScriptRuntime.hpp` grouping all callbacks (existing + 3 new: `cursorLockSetCallback`, `managerRegistryChangedCallback`, `audioCommandCallback`).
- Replace flat `initialize()` 9-param signature with `initialize(scene, bus, input, ScriptRuntimeConfig)`.
- Update **both** call sites: `Editor/src/main.cpp:1742–1771` and `Runtime/src/main.cpp:442–472`.
- Add matching private members + clear in `shutdown()` (`ScriptRuntime.cpp:742–761`).

**No math changes to `luaEntityGetRight`.**

#### D.1 — `self.gameManager` Lua API + `game_manager.lua`

- Add `cursorLockDesired` to `PlayModeState` in `Editor/src/main.cpp`.
- Wire `cursorLockSetCallback` in the Editor call site to write it.
- Add `ScriptRuntime::setCursorLock(bool)`.
- Register `luaGameManagerSetCursorLock` and `pushGameManagerProxy`; attach `self.gameManager` in `attachScriptInstance`.
- Create `Game/Scripts/game_manager.lua` (see original plan Step 1.7 for content — unchanged).

#### D.2 — `on_end` + `self.managers` + `self.audio` + `audio_manager.lua`

- Extend `stopScript` (`ScriptRuntime.cpp:918–946`) with `on_end` dispatch + auto-`unregister` from manager registry.
- Fire `on_end` in `shutdown()` before `lua_close`.
- Add `registeredManagerNames` to `ScriptInstance`.
- Public methods: `registerManager`, `unregisterManager`, `hasManager`, `listManagers`.
- Lua proxies: `pushManagersProxy` (register/unregister/has/list), `pushAudioProxy` (play/stop/setMasterVolume/isPlaying — stub with one-warning-per-clip tracker).
- Create `Game/Scripts/audio_manager.lua`.

#### D.3 — FPS + third-person controller updates

Add to both `Game/Scripts/fps_controller.lua` and `third_person_controller.lua`:
```lua
function XxxController:on_start()
    -- existing body ...
    self.gameManager:setCursorLock(true)
    self.managers:register("fps_controller")   -- or "third_person_controller"
end

function XxxController:on_end()
    self.managers:unregister("fps_controller")
end
```

WASD math is already correct — no changes to movement logic. V-D2/V-D3 stay as regression tests.

#### D.4 — Remove `lockCursor` from all 7 consumers

Per the master table above. `wantsCursorLock` in both `Editor/src/main.cpp:2499–2501` and `Runtime/src/main.cpp:556` becomes:
```cpp
bool wantsCursorLock =
    playMode.cursorLockDesired &&
    !scriptRuntime.listManagers().empty();   // At least one controller/manager registered.
```

Exact formula pinned during implementation once we re-read the surrounding play/pause logic.

#### D.5 — Preset panel + ScriptGenerator catalog

- `Editor/src/main.cpp:5191` — `presets` 8 → 10 (append "Game Manager", "Audio Manager").
- `Editor/src/main.cpp:5249` — `presetSelected` size 8 → 10.
- `Editor/src/main.cpp:5443–5457` — split preset apply into `Movement` group (enables Collider) vs. `Utility` group (does NOT enable Collider). Game Manager + Audio Manager are Utility.
- `Editor/src/ScriptGenerator.cpp:98–112` — expand system prompt to catalog `self.entity`, `self.input`, `self.camera`, `self.physics`, `self.world`, `self.gameManager`, `self.managers`, `self.audio` with method signatures.

#### D.6 — Verification (V-D)

Reuse the V1–V20 matrix from the original `GameManagerAddon.md` (renumbered V-D1..V-D20). V-D2 and V-D3 (WASD Forward/Back and Strafe L/R) are kept as **regressions** — they should already pass before any code change in this phase.

---

## Part 5 — Verification Matrix (Consolidated)

| Group | Range | Focus |
|---|---|---|
| V-A | V-A1 .. V-A6 | Blender MCP infra: build, installer, addon install, HTTP round-trip |
| V-B | V-B1 .. V-B6 | Provider framework: presets, custom, persistence, ranking |
| V-C | V-C1 .. V-C8 | Cockpit: tool routing, bridge, destructive gating, undo ring, runaway cap |
| V-D | V-D1 .. V-D20 | Game Manager + `lockCursor` removal (per original doc, luaEntityGetRight fix REMOVED) |

Full build gate: `cmake --build --preset editor-debug` exit 0, zero new warnings; `ctest --preset editor-debug --output-on-failure` zero failures.

---

## Part 6 — Progress Tracking

> Last reconciled against the source tree on **2026-09-06**. Everything marked DONE
> below was verified by building both configurations and running the test suite, not
> by reading the code.
>
> ```
> Debug   BUILD_EXIT=0  CTEST_EXIT=0  warnings 0  ->  26/26
> Release BUILD_EXIT=0  CTEST_EXIT=0  warnings 0  ->  26/26
> ```
>
> CI builds this branch from a clean machine on every push and is green.

### DONE — Phase A (Blender MCP)

- **A.0** HTTP-stack decision recorded (WinHTTP + in-repo `json::Value`; no `cpp-httplib`).
- **A.1** `BlenderClient` — sync `ping`/`listTools`/`callTool` plus async variants with `pumpMainThread()`.
- **A.2** `BlenderLauncher` — 4-tier detection (env / registry / `%ProgramFiles%` glob / `PATH`), `CreateProcessW` with `--python-expr` boot.
- **A.3** Blender top-menu + dockable panel: detection/process/MCP state, endpoint, tools tree, `execute_python` scratchpad.
- **A.4** Installer files authored (`installer/`). Building it needs `iscc` on the target machine.
- **A.5 (security)** The MCP bearer token was declared but **never assigned**, so Blender ran with no auth and any local process could reach `code.execute_python`. Now generated from `BCryptGenRandom` at client construction and handed to the addon by the launcher.

### DONE — Phase B (Multi-provider AI)

- 8 providers with `protocol` + capability flags; AI Setup shows protocol chips and a reasoning-effort dropdown.
- `discoverModels()` and relevance ranking.
- Custom-provider card (emits a JSON snippet; built through `json::serialize` so free-text fields cannot produce invalid JSON).
- **`send()` was speaking OpenAI protocol to the default Anthropic endpoint**, so AI script generation, the command planner, animation generation and the Calibrate button all failed on a clean install. Now branches on `protocol` like `sendRaw()` always did, and the response parser understands Anthropic's `content[]` block array as well as OpenAI's `choices[]`.
- `Providers.json` keys that were read by nobody are now honoured: `timeoutSeconds` (was hardcoded 30s) and `localSecretsFile`.

### DONE — Phase C (AI Cockpit)

- Multi-turn agent loop, live `tools/list` catalog, tool_result turns, scene + Blender dispatch, bounded undo ring.
- **Approval gate rewritten.** It decided what to auto-run by substring-matching tool names against `delete`/`remove`/`clear`, which fails open — and fails open on names supplied by the MCP server, not by us. Replaced with an exhaustive allowlist of the editor's own non-destructive tools; everything else needs approval. `execute_python` is gated in every mode.
- Fixed a lost wakeup: `requestCockpitStop`/`joinCockpitWorker` mutated a condition-variable predicate without holding its mutex, which could hang the editor on exit.

### DONE — Phase D.0–D.2 (Game Manager)

- **D.0** `ScriptRuntime::Config` replaces `initialize()`'s nine positional parameters.
- **D.1** `self.gameManager` (`setCursorLock`), `self.managers` (register/unregister/has/list), `on_end()` lifecycle, `lockCursor` removed from all 11 sites including the per-entity Inspector checkbox. `game_manager.lua` prefab + preset.
- **D.2** `self.audio` backed by the real engine, `audio_manager.lua` prefab + preset, `AudioHook` gained `loop` (background music was impossible without it), and **Runtime gained an `AudioEngine`** — it had none, so hooks and `self.audio` were silent in the shipped game.
- **`luaEntityGetRight` deliberately untouched.** See `master_changeLog.md`; the plan's Step 0.3 would have re-introduced the strafe inversion it claimed to fix.

### DONE — work outside the original three streams

Added after this plan was written, in response to gaps found while building the above.

- **Project Settings panel + typed settings bus.** `Project.json`/`Settings.json` get a typed model where the bus is the only writer, so neither the panel nor the AI can store an invalid startup scene, a non-finite float or an out-of-range index. Five `project.*` AI tools route through it rather than letting the model write raw JSON.
- **Boot sequence** (`BootStep`, `tickBootSequence`) — the engine's first notion of game flow. Runs before scripts and holds player input until finished, in shared `GameplayLoop`, so Editor Play and standalone Runtime behave identically.
- **Audio** — miniaudio behind a pimpl, degrading to silent when there is no device. `AudioSourceData` on entities, `AudioHook` on project settings bound to events that already existed. Audio panel with clip list, preview, event bindings and **Import Sound from PC**.
- **Storyboard persistence** — shots were session-only and lost on every restart. `CineShot` moved into Engine, gained `AudioCue`, and now round-trips through the scene file.
- **Timeline panel** — ruler, draggable playhead, camera-keyframe track and audio-cue track on one clock; scrub, drag to retime, right-click to delete.
- **Performance panel + frame profiler** — per-frame cost split into named zones, rolling 600-frame window, the worst frames kept permanently with their full breakdown, and an exportable Markdown report. A plain FPS number says a dip happened but not what caused it.
- **Every shipped Lua script fixed.** Seven of nineteen could not run: they called APIs that never existed (`Input.`/`Entity.`/`Vector()`/`Raycast()`/`UI.`, an `Update()` entry point) or never returned a table. `testAllShippedScriptsLoad` now actually executes each one — a grep-based check had reported all nineteen clean and was wrong.
- **AI script-API catalog (D.5)** — both `ScriptGenerator` and `AICommandPlanner` advertised only `self.entity`/`self.input`/`self.camera`, which is how those seven broken scripts got written. Both now carry the complete surface and explicitly forbid the exact inventions that failed.
- **CI made to work at all.** It had never run: the branch had never been pushed. Four independent breakages, none of them the C++ — a deleted third-party action, ImGuizmo pinned to a commit SHA while being shallow-cloned (**a fresh clone of this repo could not configure**), glad's generator needing `jinja2`, and that package landing in a different Python than CMake chose.
- **Splash screen** was rendering upside down in both Editor and Runtime.

### TODO

- [ ] **A.5 verification** — live MCP connect (V-A5/V-A6) on the user's machine.
- [ ] **D.3** Key bindings: action layer over `InputSource`, defaults W/A/S/D, Space, Shift, C crouch, V cycle-view, E, I, M; Settings → Controls tab. Note this **changes existing behaviour** — the controllers currently use **C** to swap camera.
- [ ] **D.4** Scene Manager: ordered, numbered scene list; `self.scenes:load()`. Switching must happen between frames — scripts hold entity ids that a reload destroys.
- [ ] **D.5** Cutscene Manager: image sequence + audio track (no decoder dependency, by decision). Both between-scene and in-scene-without-unloading.
- [ ] **D.6** Missing import paths: `Game/Animations` (advertised in `Project.json` and unreachable), `Game/Characters`, `Game/Scripts`, `Game/Branding`.
- [ ] **Audio manager ownership** — per-entity "uses Audio Manager" flag surfacing the object in the Audio Manager list, with upload/link/effects per object, and an effects chain (reverb, echo, delay) with sliders and manual value entry.
- [ ] **`self.audio:isPlaying()`** currently always returns false; **`stop()`** stops every voice rather than the caller's.
- [ ] **Version bump** — still 0.79 in `CMakeLists.txt` and `README.md`.

### BLOCKED

*(none)*

---

## Part 7 — Open Questions / Deferred

- **AI-assisted UI editor.** Deferred by decision. Needs a UI data model *and* a data-driven runtime renderer — `Runtime/src/GameMenu.cpp` is hardcoded C++. Items 1–3 of that work have nothing to do with AI.
- **Video cutscenes.** Decided: image sequence + audio track, no decoder dependency. Real `.mp4` playback would mean FFmpeg — large, and LGPL/GPL implications for a closed project.
- **Antigravity + Agnes-AI preset details.** Endpoint + schema still unconfirmed.
- **Streaming responses in cockpit (SSE).** Deferred; v1 uses blocking `POST`.
- **`audioHooks` live in `Settings.json`, not `Project.json`.** Reader and writer agree so it round-trips, but the name suggests otherwise. Left alone — changing it would break saved projects for a cosmetic gain.
- **Repository size.** `.git` is 331 MB with no LFS. Models, scenes and audio are **deliberately not committed** — the website will host demo scenes for manual download once ready to publish.
- **Blender undo grouping reliability.** `bpy.ops.ed.undo_push` bracketing is best-effort for `code.execute_python`.

---

## Part 8 — Change History for This File

| Date | Change | Reason |
|---|---|---|
| 2026-08-28 | Initial version consolidating three streams | Per user request: single active plan doc |
| 2026-09-06 | Reconciled with the source tree and **committed to git** | This file is named by `AGENTS.md` as the single source of truth, but was untracked — it existed on one disk, in no commit, no push, no CI, and its progress table still listed Phases B, C and D as TODO after they had shipped. Recorded as finding 7.1 in `audit_late_night2026-09-06.md`. |
