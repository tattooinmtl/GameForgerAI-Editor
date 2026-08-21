# `docs/phases/` — Per-Ticket Progress Log

This folder is the live, file-per-effort progress log for [`RoadMap2026-08-21.md`](file:///C:/GameForgerAI-Editor/docs/RoadMap2026-08-21.md). The RoadMap says *what* to do and *who* owns it; files here record *what actually happened* when an agent picked a ticket up — so any agent (or the user) can see what's in flight or done without re-reading a full session transcript.

## Naming convention

```
phases/phaseNN.letter.md           — first (and often only) file for a ticket
phases/phaseNN.letter.seq.md       — a later revision/attempt/follow-up on the same ticket
```

- **`NN`** — the phase number from the RoadMap (`00` = Phase 0 Gauntlet Remediation, `00.5` isn't valid as a folder-safe token so Phase 0.5 uses `phase00b`, see below, `01` = Phase 1, etc.).
- **`letter`** — one ticket/sub-task within that phase, assigned in the order work starts (`a`, `b`, `c`, ...). Look up which letter belongs to which ticket ID (e.g. `DEF-04`, `T3-3`, `R-16`) in the RoadMap's per-phase progress table — the RoadMap is the index, this folder is the content.
- **`seq`** (optional, numeric, zero-padded to 3 digits: `001`, `002`, ...) — only add this when you're revisiting a ticket that already has a file (a bug found after "done," a scope change, a second attempt after a failed verification). The first file for a ticket does **not** need a `.001` suffix — add one only from the second file onward for that same ticket, so `phase01.c.md` (first attempt) is followed by `phase01.c.002.md` (the fix), not `phase01.c.001.md` + `phase01.c.002.md`.

Examples: `phase00.a.md` (first file for Phase 0's first ticket), `phase03.d.002.md` (second file for Phase 3's fourth ticket).

Phase 0.5 (the recovered small-fix backlog) uses `phase00b` as its `NN` token (e.g. `phase00b.a.md`) — dots-only numbering can't express "0.5" in a sortable filename, and `00b` sorts immediately after `00` in a plain directory listing, which is the property that matters.

## What goes in a file

No fixed template — write what's useful for the next reader — but always include, near the top:
- **Ticket ID(s)** this covers (from the RoadMap, e.g. `T3-3`, `DEF-06`, `R-04`)
- **Agent** (Claude or Gemini) and **date**
- **Status**: `starting` / `in progress` / `blocked` (say on what) / `done — verified` (say how, e.g. "Debug+Release build clean, CTest 9/9, verified by subagent X")
- What changed (files touched, one line each is fine)
- Anything the next person picking up related work should know (a gotcha, a deferred edge case, a design call you made without asking)

## The rule that keeps this useful

**Every time you start or finish a ticket, do three things, not one:**
1. Create or update the `docs/phases/` file for it.
2. Link it from the RoadMap's progress table for that phase (a one-line row: ticket ID, agent, status, link to this file).
3. Update the ticket's status in the RoadMap itself AND in `planFix_AGY_CHANGELOG.md`.

Skipping steps 2/3 is the single most common way this kind of log convention silently stops being trustworthy — a file existing here that nothing points to is as good as it not existing, and a status flip in one place that isn't mirrored in the other two is how two agents end up duplicating or contradicting each other's work.

## Do not delete phase files

Same rule as the rest of this project's planning docs: if a phase file needs to be superseded (a redone attempt, a corrected approach), add a new `.seq` file — don't overwrite or delete the old one. The history of "what was tried" is worth keeping even after it's superseded.
