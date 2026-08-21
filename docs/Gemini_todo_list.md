# Gemini To-Do List — Gauntlet Audit Remediation (2026-08-21)

**Read [`AGENTS.md`](file:///C:/GameForgerAI-Editor/AGENTS.md) first if you haven't already** — it's the shared operating manual for every agent (Claude, Codex, Antigravity, you) working in this repo: source-of-truth docs, coding standards, the skills library at `C:/.skills/skills/`, and build/verify commands.

> **Note:** this file's tasks are also tracked as Phase 0 of the master **[`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md)** (which supersedes `planFix_AGY.v1.2026-08-18.md`, now archived). Once you finish DEF-04/06/08, also read the RoadMap for what's assigned to you next (Phase 0.5's recovered backlog and beyond) — this file only covers the first, already-scoped batch.

## Why this file exists

An external audit tool re-scanned this project (`PROJECT_AUDIT_GAUNTLET_REPORT.md`) and found 8 defects, now tracked as **DEF-01 through DEF-08** in `RoadMap2026-08-21.md` Phase 0. Claude did the three that needed the most judgment (build system, atomic file writes, path-traversal confinement — DEF-01/02/03, already done and verified as of Alpha 0.79) and kept two more that need care (DEF-05, DEF-07). **You own DEF-04, DEF-06, and DEF-08** — mechanical, repetitive, or low-risk-of-getting-wrong work that doesn't need much judgment, just following the recipe below precisely and verifying it compiles/passes tests before you call it done.

**Ground rules — read this before touching anything:**
1. This is not a git repo (`git status` will fail — don't try). There's no branch/PR workflow here; edit files directly.
2. Before claiming ANY task done, you must actually build and run the test suite (commands below) and see it pass — don't infer success from the edit looking right.
3. When you finish a task, update **four** places, not one: (a) check the box below, (b) append your own dated entry under a new `### Gemini` subsection in [`planFix_AGY_CHANGELOG.md`](file:///C:/GameForgerAI-Editor/docs/planFix_AGY_CHANGELOG.md) (follow the existing entry format — see the "2026-08-21" section Claude already wrote, right above the ticket tables), (c) flip the Status column for that DEF-ID from ⚪ to 🟢 in `RoadMap2026-08-21.md` Phase 0, and (d) create/update a file in `docs/phases/` per `docs/phases/README.md`'s convention, linked from the RoadMap. If you only do the checkbox here, the other agents working on this repo won't see that it's done.
4. If something in a recipe below doesn't match what you actually find in the file (a line moved, a signature differs), that's expected — this codebase changes between sessions. Use judgment to apply the *intent* of the fix, don't force the literal text if it no longer matches, but don't silently skip it either — note what changed in your changelog entry.
5. **Verification must come from a fresh subagent, not your own self-assessment** — after you finish an edit, spawn a subagent with no memory of *how* you made the change (just what the ticket asked for and the acceptance criteria) to run the build/test commands and confirm independently. This is the same discipline Claude is using on its side — see `RoadMap2026-08-21.md`'s "Verification Protocol" section for why (a fresh reviewer catches "looks right to the person who just wrote it" mistakes that self-review misses).

**Build & verify commands** (PowerShell, needs the MSVC dev environment first):
```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d C:\GameForgerAI-Editor && cmake --build out/build/windows-x64 --config Debug --target GameForgerEditor GameForgerRuntime GameForgerTests'
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d C:\GameForgerAI-Editor && ctest --test-dir out/build/windows-x64 -C Debug --output-on-failure'
```
Both must succeed (exit 0, "100% tests passed") before you mark a task done. Watch for **new** compiler warnings your own edit introduces — zero-warning is the bar this project holds itself to for these fixes specifically (that's the whole point of DEF-04).

**Skills to load before starting** (per `AGENTS.md` §4's situation matrix — these apply to everything below):
- [`cpp-coding`](file:///C:/.skills/skills/cpp-coding) — C++20 idioms, minimal-blast-radius edits, before you touch any `.cpp`/`.hpp`.
- [`verification-before-completion`](file:///C:/.skills/skills/verification-before-completion) — before you check any box below. This is the one that matters most: it exists specifically so you verify with evidence (a real build + test run) instead of assuming your edit is correct. Don't skip it just because these tasks look mechanical — mechanical edits at 70+ call sites are exactly where a copy-paste mistake hides.
- [`code-review`](file:///C:/.skills/skills/code-review) — after you finish each task, run a self-review pass against your own diff before marking it done. Look specifically for: did every call site actually get the fix, or did you miss one in a file you didn't grep thoroughly enough? Did you accidentally change behavior (e.g. now short-circuiting on failure when the original code intentionally kept going)?

---

## [ ] DEF-04 — Fix `[[nodiscard]]` C4834 warnings (~70+ call sites)

**Files:** `Engine/src/Editor/ScriptRuntime.cpp`, `Engine/src/Runtime/GameplayLoop.cpp`, `Editor/src/main.cpp`, `Runtime/src/main.cpp`

**The problem:** `AICommandBus::execute(...)` returns `[[nodiscard]] AICommandResult` (`Engine/include/GameForger/Editor/AICommandBus.hpp`):
```cpp
struct AICommandResult
{
    bool success = false;
    bool preview = false;
    std::string message;
};
```
Dozens of call sites do `commandBus.execute(SomeCommand{...});` and throw the result away, which MSVC flags as warning C4834 ("discarding return value of function with [[nodiscard]] attribute"). Find every one with:
```
grep -rn "commandBus.execute(\|commandBus().execute(" Engine/src/Editor/ScriptRuntime.cpp Engine/src/Runtime/GameplayLoop.cpp Editor/src/main.cpp Runtime/src/main.cpp
```
(or just rebuild and read the C4834 warning list — each one gives you the exact file:line.)

**The fix, per call site — capture the result and log on failure. Don't just add `(void)` to silence the warning** — that defeats the actual point of `[[nodiscard]]` here (surfacing command failures), and the audit explicitly calls this out as "Handle command execution results or log errors when commands fail," not "silence the warning."

Which logging call to use depends on what's already in scope at that call site — **check what's actually available before picking**, don't assume:

- **In `Editor/src/main.cpp`**: most functions in this file that mutate the scene already take (or can reach) a `ConsoleState& console` parameter, and the file already has a `logMessage(ConsoleState&, LogLevel, std::string)` helper (defined ~line 199, used e.g. at lines 884/898 — read those two for the exact pattern to copy). Prefer this pattern:
  ```cpp
  const AICommandResult result = commandBus.execute(SomeCommand{...});
  if (!result.success)
  {
      logMessage(console, LogLevel::Error, "SomeCommand failed: " + result.message);
  }
  ```
  If a specific call site genuinely has no `console` reachable (rare — check the enclosing function signature and its callers before concluding this), fall back to the `ScriptRuntime.cpp`/`GameplayLoop.cpp` pattern below instead of inventing a new logging path.

- **In `Engine/src/Editor/ScriptRuntime.cpp`**: the class already has a `void log(bool isError, const std::string& message) const` member (defined ~line 1160, routes through `logCallback_` — the same mechanism the rest of the file already uses for script errors). Where you're inside a `ScriptRuntime` member function or have a `ScriptRuntime*`/`runtime` pointer in scope, use:
  ```cpp
  const AICommandResult result = runtime->commandBus().execute(SetPropertyCommand{...}); // or commandBus().execute(...) if `this`
  if (!result.success)
  {
      runtime->log(true, "SetPropertyCommand failed: " + result.message); // or log(true, ...) if `this`
  }
  ```

- **In `Engine/src/Runtime/GameplayLoop.cpp` and `Runtime/src/main.cpp`**: these have no ImGui console and no `ScriptRuntime` log callback in scope at most of these call sites — `Runtime/src/main.cpp` already logs plainly via `std::fprintf(stderr, "...\n", ...)` (see lines 68/74/82/307 for the existing style). Match that:
  ```cpp
  const AICommandResult result = commandBus.execute(SetPropertyCommand{...});
  if (!result.success)
  {
      std::fprintf(stderr, "SetPropertyCommand failed: %s\n", result.message.c_str());
  }
  ```
  `GameplayLoop.cpp` doesn't currently `#include <cstdio>` — add it if you use `fprintf` there.

**A pragmatic note on message text**: don't spend time hand-crafting a unique message per call site. `"<CommandType> failed: " + result.message` (or the fprintf equivalent) is enough — `result.message` from the command bus itself already carries the specific reason. The goal is "failures are visible somewhere," not prose quality.

**Verify:** rebuild Debug — the C4834 warning list should be **empty** (grep the build output for `C4834`, confirm zero matches). Run CTest, confirm still 9/9 (or whatever the current count is) passing. Skim your own diff (`code-review` skill) for any call site where you changed control flow instead of just adding a check-and-log.

---

## [ ] DEF-06 — Add a Release build to CI

**File:** [`.github/workflows/ci.yml`](file:///C:/GameForgerAI-Editor/.github/workflows/ci.yml)

**The problem:** the workflow only builds and tests the Debug configuration:
```yaml
      - name: Build All Targets (Debug)
        run: |
          cmake --build out/build/windows-x64 --config Debug

      - name: Run Automated CTest Suite
        run: |
          ctest --test-dir out/build/windows-x64 -C Debug --output-on-failure
```
Release has never been exercised in CI, so a Release-only compile break (there's been at least one historically — the `all-release` preset failure DEF-01 just fixed) can land undetected.

**The fix:** turn `build-and-test` into a matrix job over both configs, OR add explicit Release build+test steps after the existing Debug ones — either is acceptable, but **matrix is preferred** since it's the idiomatic GitHub Actions way and keeps Debug/Release output cleanly separated in the Actions UI instead of interleaved in one job's log. Matrix form:
```yaml
jobs:
  build-and-test:
    name: Windows x64 MSVC Build & CTest (${{ matrix.config }})
    runs-on: windows-latest
    strategy:
      fail-fast: false
      matrix:
        config: [Debug, Release]

    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup MSVC developer command prompt
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: x64

      - name: Setup Ninja
        uses: ashley-taylor/setup-ninja@v1.0.0

      - name: Configure CMake
        run: |
          cmake --preset windows-x64

      - name: Build All Targets (${{ matrix.config }})
        run: |
          cmake --build out/build/windows-x64 --config ${{ matrix.config }}

      - name: Run Automated CTest Suite (${{ matrix.config }})
        run: |
          ctest --test-dir out/build/windows-x64 -C ${{ matrix.config }} --output-on-failure
```
This is a single-source `configure` shared by both matrix legs (Ninja Multi-Config supports building either config from one configure step, which is why the existing single `cmake --preset windows-x64` call works unchanged) — don't add a second configure step per leg, it's unnecessary and slower.

**Verify:** you can't literally run GitHub Actions locally, so verification here is: (1) the YAML is valid (no tab characters, consistent indentation — `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"` if Python's available, or just have a second look by eye against the existing file's indentation style), and (2) confirm the exact commands in the file (`cmake --build out/build/windows-x64 --config Release`, `ctest --test-dir out/build/windows-x64 -C Release --output-on-failure`) actually succeed when you run them yourself locally first — same commands as the "Build & verify" section above, just with `Release` instead of `Debug`.

---

## [ ] DEF-08 — Plaintext API keys in `Game/AI/Providers.local.json`

**File:** `Editor/src/AIProviderClient.cpp` (the `AIProviderClient` constructor, ~line 186)

**Context, read this before you start:** full encryption-at-rest for this file is explicitly **out of scope** for this pass — it would need a real design decision (what encrypts it? a user password? Windows DPAPI? where's the key stored?) that hasn't been made, and inventing one unilaterally is exactly the kind of judgment call this task was *not* assigned to you for. **Don't implement encryption.** This ticket is scoped narrowly to what's actually mechanical:

1. **Confirm the file is already gitignored** (it should be — check `.gitignore` for `Game/AI/Providers.local.json`, it's there as of this writing at line 12). If it's somehow not there when you check, add it. This is the check that actually matters most: the real risk isn't the file existing on the user's own disk, it's it accidentally landing in a git commit/push.
2. **Add a one-time startup warning** so a user opening the Editor is actually told this file holds an unencrypted secret, rather than discovering it by reading source. `AIProviderClient`'s constructor (`Editor/src/AIProviderClient.cpp` ~line 186-189) currently does nothing but store `projectRoot_`. Add a check there:
   ```cpp
   AIProviderClient::AIProviderClient(std::filesystem::path projectRoot)
       : projectRoot_(std::filesystem::absolute(std::move(projectRoot)))
   {
       if (std::filesystem::exists(projectRoot_ / "Game/AI/Providers.local.json"))
       {
           std::fprintf(
               stderr,
               "Note: Game/AI/Providers.local.json exists and is read in plaintext (API keys are not "
               "encrypted at rest). Keep this file out of version control and don't share it.\n");
       }
   }
   ```
   Check the top of the file for `#include <filesystem>` and `#include <cstdio>` — add whichever is missing.
3. **Add one sentence to README.md's "Known gaps" section** (grep for `## Known gaps` or similar heading — if the project doesn't have one, skip this sub-step and just note it in your changelog entry instead of inventing a new section) stating plainly that API keys in `Providers.local.json` are stored unencrypted on disk, gitignored but not otherwise protected, and that real encryption-at-rest is a deferred design decision, not yet scheduled.

**Verify:** rebuild Debug, confirm it still compiles clean (this is a tiny, additive change — if it doesn't compile clean, you've almost certainly got a missing include or a typo, not a real design problem). Manually confirm the warning path: temporarily create an empty `Game/AI/Providers.local.json` if one doesn't already exist in this dev environment, launch the Editor from a terminal that shows stderr, confirm the note prints once at startup, then remove the file again if you created it (or leave it if the user already had a real one there — don't delete a file you didn't create).

---

## When you're done with all three

Update the "Last Updated" line at the top of `planFix_AGY_CHANGELOG.md` to today's date and your name, same as Claude did for its own session. Leave a short note in your changelog entry about anything from these recipes that didn't match what you actually found in the code — that's useful signal for whichever agent reads this next, not a sign you did something wrong.
