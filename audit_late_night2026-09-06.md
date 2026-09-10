# GameForgerAI — Late-Night Session Audit

> **File:** `audit_late_night2026-09-06.md`
> **Date:** 2026-09-06 (overnight session, continuing from 2026-09-05)
> **Auditor:** Claude (Opus 5), this workspace
> **Predecessors:** [`audit-2026-09-05.v2.md`](./audit-2026-09-05.v2.md) (findings A-01..A-11, all closed or noted), [`DailyAudit2026-09-05.md`](./DailyAudit2026-09-05.md)
> **Method:** Every claim below was verified by building and running, not by reading. Debug **and** Release, both with the test suite, after each phase.

---

## 1. Executive summary

```
Debug   BUILD_EXIT=0   CTEST_EXIT=0   warnings 0   ->  24/24
Release BUILD_EXIT=0   CTEST_EXIT=0   warnings 0   ->  24/24
Editor launches, Runtime builds, existing scenes still parse.
```

Twenty commits on `phase-a-d-source`, **60 files, +13,040 / −617** against `main`.
Test suite grew **14 → 24**. Zero warnings at `/W4 /permissive-` throughout.

Three bodies of work: closing the 2026-09-05 audit findings, building the
Project/Audio/Timeline panels, and starting Phase D (Game Manager). Along the way CI
ran for the first time in this project's life, exposed four pre-existing breakages, and —
once those were fixed — **went green**.

**Everything is pushed.** `phase-a-d-source` is level with `origin/phase-a-d-source`,
0 unpushed. **CI passes on PR #1** (§4).

> **Revised 2026-09-06 after review.** The first version of this file was written before the
> final push and before CI finished, and stated the opposite of both. Those sections are
> corrected below. A review also surfaced seven gaps this audit missed — they are recorded in
> §7 rather than quietly folded in, because an audit that hides its own misses is worthless.

---

## 2. What was built

### Phase 1 — Typed project settings (complete)
`ProjectSettings` + `ProjectSettingsBus` in Engine: a typed view of `Project.json` and
`Settings.json` where the bus is the **only** writer. The inspector panel and the AI both
go through `validate() → execute()`, so neither can write a startup scene that does not
exist, a non-finite float, an fps outside 15–480, or an out-of-range index. A failed disk
write rolls back in memory, so the screen never disagrees with disk.

`BootStep` gives the engine its first notion of game flow — `tickBootSequence` runs before
scripts and holds player input until the sequence finishes, in **shared** `GameplayLoop`,
so Editor Play and standalone Runtime behave identically.

Five `project.*` AI tools route through the same bus rather than letting the model write
raw JSON. Only `project.get_settings` is auto-approvable.

### Phase 2 — Audio (complete)
miniaudio via FetchContent behind a pimpl. Device-init failure degrades to silent rather
than fatal — the editor must stay usable on a machine with no audio device, and CI has
none. `AudioSourceData` on entities, `AudioHook` on project settings, bound to events that
**already existed** in `GameplayLoop` rather than invented ones.

### Phase 3 — Timeline (complete)
Storyboard shots now persist in the scene file; they were session-only and lost on every
restart, which made the feature unusable for real work. `CineShot` moved to Engine and
gained `AudioCue`. The Timeline panel is a real track view — ruler, draggable playhead,
camera track, audio track, scrub/drag/delete — docked after Storyboard.

### Phase D1/D2 — Game Manager (complete)
`ScriptRuntime::Config` replaces `initialize()`'s nine positional parameters. Two
`BoolQueryCallback`s sat adjacent in that list, so a mis-ordered call site compiled and
silently misbehaved; named fields cannot.

**"Lock Cursor" is gone from every entity's Inspector.** It was session state modelled as a
per-entity authored flag. It is now `self.gameManager:setCursorLock()`, gated on something
being registered as actually driving the player — so a stale request from a torn-down scene
cannot leave the cursor captured with nothing controlling it.

New Lua surface on every script: `self.gameManager`, `self.managers`, `self.audio` (backed
by the real engine, not the stub the original doc specified). `on_end()` fires from
`stopScript` and from `shutdown` before `lua_close`. Prefabs `game_manager.lua` and
`audio_manager.lua`; presets 8 → 10; both controllers migrated.

`AudioHook` gained `loop`, which is what makes background music possible at all. Runtime
gained an `AudioEngine` — it had none, so hooks and `self.audio` were silent in the shipped
game while working in the editor.

---

## 3. Bugs found by running the code, not reading it

These are the ones that would have shipped. Every one was caught by build-and-run.

| # | Bug | How it would have hurt |
|---|---|---|
| 1 | **Trailing comma in the scene writer.** Removing `lockCursor` left `thirdPersonYawOffsetDegrees` with a `,` before `}` | **Every saved scene would have failed to load** — this project's parser rejects trailing commas outright |
| 2 | **`IM_ASSERT(ReorderRequestTabId == 0)` crash on launch.** Two panels queued a tab reorder into the same node on one frame | Hard `abort()` dialog on startup — the editor would not open |
| 3 | **`DockBuilderDockWindow` cannot dock a fresh window.** It only re-seats a window that already has a `DockId` | Timeline floated forever; the placement code had never actually worked for a new panel |
| 4 | **ImGuizmo shallow-clone.** Pinned to a raw commit SHA with `GIT_SHALLOW TRUE`; a depth-1 clone can only reach a branch or tag | **A fresh clone of this repo could not configure at all.** Invisible locally because `out/_deps` was already populated |
| 5 | **`AudioHook` had no `loop`** | Background music was impossible despite `AudioEngine::play` always accepting a loop argument |
| 6 | **Audio panel had no import button** | The panel's entire purpose is managing audio, and there was no way to add any |

Nos. 1–3 were introduced during this session and caught before commit. Nos. 4–6 were
pre-existing.

---

## 4. CI: first real run in this project's history

The branch had never been pushed, so `.github/workflows/ci.yml` had never executed. It was
broken in four independent ways, none related to the C++:

| Attempt | Died at | Cause | Fixed by |
|---|---|---|---|
| 1 | 3s, setup | `ashley-taylor/setup-ninja` **deleted from GitHub** | `7b7e35d` — resolved without any third-party action, since the obvious replacement is archived |
| 2 | configure | ImGuizmo shallow clone (§3 #4) | `bef1b3a` |
| 3 | build | glad's generator needs Python `jinja2` | `b935c51` |
| 4 | build | jinja2 installed into Python 3.12; CMake picked 3.14 | `e1898a0` |

### Then it went green

After those four fixes, CI **passed**:

| Run | Result | Duration |
|---|---|---|
| `34012258322` | **success** | ~11m 15s |
| `34017184934` | **success** | ~11m 15s |

Both **MSVC x64 Debug and Release** configured from scratch on a GitHub runner — cold
FetchContent clone of glfw, glm, glad, imgui, ImGuizmo, Lua, assimp and miniaudio — built
every target, and ran **all 24 unit tests green**.

This answers the question the 2026-09-05 audit opened with. A fresh clone on a clean machine
**does** build this project, and the tests pass there. That had never been demonstrated
before: the branch had never been pushed, so nothing outside this one Windows box had ever
compiled it. It also proves the two things I could only reason about locally — that
miniaudio degrades gracefully on a runner with **no audio device**, and that the ImGuizmo
cold-clone fix actually works.

---

## 5. Documentation correction

`master_changeLog.md:66` had the two `luaEntityGetRight` cross-product expressions
**transposed**. It described the live code as `cross(+Y, forward)` giving `+X`, and named
`cross(forward, +Y)` as the change to reject. Verified against the source and
`testGetRightMatchesFpsCamera`: the code is `cross(forward, +Y)` giving **−X** at yaw 0,
and that is the correct one.

The note's **conclusion was always right** and it did its job — Phase D left the function
alone because of it. But read literally it told the reader to "restore" the code into the
actual inverted-strafe bug. Rewritten (`e28fb79`) as a keep-vs-reject table with the yaw-0
sign as the deciding fact, both guards named, and a dated correction preserving the
original intent. The same transposition appeared in `2e41385`'s message; corrected here
rather than by rewriting history.

---

## 6. Open items

**Immediate:**
- **PR #1 is open, `MERGEABLE`, and CI is green.** Nothing blocks merging it but review.
- **Version still 0.79** in `CMakeLists.txt` and `README.md`, across everything above.

**Remaining plan phases.** These belong in `integration_plan_allinone.md` (the project's
declared source of truth, currently untracked — see §7.1), not in a path on one machine:
- **D3 — key bindings.** Action layer over `InputSource`; defaults specified
  (WASD / Space / Shift / C crouch / V cycle-view / E / I / M). Note this **changes existing
  behaviour**: the controllers currently use **C** to swap camera, which becomes **V**.
- **D6 — four missing import paths**: `Game/Animations` (advertised in `Project.json` and
  completely unreachable), `Game/Characters`, `Game/Scripts`, `Game/Branding`.
- **D4 — Scene Manager.** Ordered, numbered scene list; `self.scenes:load()`. Sharpest edge
  in the whole plan: switching scenes destroys entities while scripts hold their ids, so it
  must happen strictly between frames.
- **D5 — Cutscene Manager.** Image sequence + audio track (no decoder dependency, by
  decision). Both between-scene and in-scene-without-unloading.
- **UI editor** — deferred by decision; needs a UI data model plus a data-driven runtime
  renderer, since `Runtime/src/GameMenu.cpp` is hardcoded C++.

**Noted, not acted on:**
- `audioHooks` are stored in `Settings.json`, not `Project.json`. Reader and writer agree so
  it round-trips correctly, but the name suggests otherwise. Left alone — changing it would
  break saved projects for a cosmetic gain.
- `.claude/hooks/rewrite-build-command.sh` auto-allows build commands and filters their
  output. It bit twice this session: a compile failure left the test runner re-executing a
  **stale binary and reporting 100% pass**, and the hook mangled an unrelated command that
  merely mentioned a build tool. **Always read `BUILD_EXIT` before `CTEST_EXIT`.**
- `.git` is 331 MB with no LFS; `Game/` is 459 MB. Cutscene frame sequences (D5) will make
  this worse — `Game/Cutscenes/` should be gitignored from the start.

---

## 7. Gaps this audit missed (found by review, all verified)

Recorded as its own section rather than folded into §6, because the pattern matters: every
one of these is something the first pass looked past. Each was verified individually before
being written down.

### 7.1 `integration_plan_allinone.md` is untracked and stale — **highest impact**
`AGENTS.md` names it the strict single source of truth for all in-flight work. It is
**untracked in git** — it exists on exactly one disk, in no commit, no push, no CI. Its
Progress Tracking table was never updated with any of last night's work. Worse, §6 of this
audit pointed the reader at `~/.claude/plans/woolly-tinkering-puppy.md`, a path on my machine
that nobody else can open, instead of the project's own plan. That is precisely the A-01
failure from the previous audit — work living only on one disk — repeated on the document
that governs the work.

### 7.2 The documented test command does not exist
`CMakePresets.json` defines configure and build presets and **zero `testPresets`**. Six
references across `AGENTS.md` (×4), `docs/AGENTS.md` and `integration_plan_allinone.md` tell
the reader to run the preset form, which fails with *"No such test preset"*. The form that
works, and the one `ci.yml:75` actually uses, is the explicit `--test-dir out/build/windows-x64 -C Debug --output-on-failure`.

*Correction to the review on this point: this audit never contained the preset command. The
finding is real and belongs here, but the bad instruction lives in `AGENTS.md`, not in this
file.*

### 7.3 ~36 MB of untracked binaries sit one `git add .` away from the packfile
§6 warned about *future* cutscene bloat and missed what is already on disk:

| File | Size |
|---|---|
| `Game/Branding/gameforgerai-neon-system.mp4` | 15.5 MB |
| `Game/Models/CastleHighTides.glb` | 13.5 MB |
| `Game/Audio/Digital+Prison+Remix (2).mp3` | 6.1 MB |
| `Game/Models/rp_claudia_rigged_002_u3d.fbx` | 1.0 MB |
| `bugapp.png` | 20 KB |

Into a `.git` already at 331 MB with no LFS. My own first measurement reported only 29 MB
because the scan mis-parsed git's quoted path for the filename containing spaces and
parentheses — the review's ~36 MB figure is the correct one.

### 7.4 `ScriptGenerator.cpp` still advertises only three proxies
Plan step D.5 called for expanding the AI script-generation prompt. Lines 98–112 list only
`self.entity`, `self.input`, `self.camera`. Missing: `self.gameManager`, `self.managers`,
`self.audio`, `self.physics`, `self.world`, and `on_end`. **Every script the AI generates
in-editor is unaware of the APIs built last night** — including the manager registration that
cursor lock now depends on. D.5 was not done; §6 should have said so and did not.

### 7.5 `self.audio:isPlaying()` is hardcoded `false`
`play`, `stop` and `setMasterVolume` are wired to the real engine; `isPlaying` returns
`lua_pushboolean(L, 0)` unconditionally. It is commented as deliberate — `AudioEngine` has no
query channel yet — but any Lua branching on it silently takes the false path. Either give
`AudioEngine` a real query or remove the method; a predicate that is always false is worse
than no predicate.

### 7.6 `self.audio:stop()` is all-or-nothing
It calls `AudioEngine::stopAll()`. A script stopping its own music also kills every other
voice, including sounds another manager started. Needs a per-clip or per-voice handle.

### 7.7 Legacy controllers do not register as managers, so cursor lock silently fails
Only `fps_controller.lua` and `third_person_controller.lua` were migrated. These were not:
`FPSController.lua`, `PlayerFPS.lua`, `player_fps.lua`, `player_controller.lua`,
`controller.lua`.

`wantsCursorLock` now requires at least one registered manager, so **an entity using any of
those five scripts gets no cursor lock at all**. A working setup silently stops working, with
nothing in the UI explaining why — because the Inspector checkbox that used to control it was
removed in the same change. This is the most user-visible regression risk in D1, and the
audit missed it entirely. Either migrate all five, or have the Console say why the cursor is
not locking.

### 7.8 Superseded docs still loose in the root
`GameManagerAddon.md`, `GameManagerAddon.md.bak`, `A-D-investigation.md`,
`DailyAudit2026-09-05.md`, `Grok_todos.md`, `audit2026-09-01.md`, `auditKimi20260902.md`,
`final_audit-2026-09-01.md` — all untracked, none archived. `AGENTS.md` declares them
superseded, which only helps a reader who starts at `AGENTS.md`.

---

## 8. Process notes for whoever picks this up

- **Build both configurations.** A Release-only build left a stale Debug exe that silently
  ran hours-old code during a live UI check.
- **Kill the editor before rebuilding.** A running instance holds the exe and the link fails
  with `LNK1168` after every source file has already compiled.
- **Verify UI changes by launching and screenshotting**, not by reasoning about the code.
  Bugs 2 and 3 in §3 were both invisible to review and obvious on screen.
- The window-only `PrintWindow` capture (by PID, not window title) is the safe way to do
  that — it captures nothing outside the app's own window.
