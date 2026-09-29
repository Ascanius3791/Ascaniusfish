<!-- OWNERSHIP=Claude -->
# Workflow

Issue-driven development. Claude writes the issues and commits, and Ascanius approves them. Keep everything short.

## Issues

**Title:** the outcome, not the technique. ≤60 chars.
Good: `Reach depth 8 from startpos in under 5 s`. Bad: `Implement null-move pruning`.
If Ascanius wants a specific technique, put it under Constraints.

**Body** (≤25 lines):

```
## Goal
1–3 sentences: what is true when this is done, and why it matters.

## Done when
- [ ] Checkable criterion (a number, a test, or an observable behaviour)

## Constraints            (optional, set by Ascanius)
- e.g. must not change lib/eval.hpp; must use technique X

## Implementation notes   (non-binding, Claude → future Claude)
- Suggested approach, relevant files, pitfalls
```

**Rules**
- *Goal*, *Done when* and *Constraints* are fixed. Only Ascanius changes them.
- *Implementation notes* are suggestions. Discard them if a better route appears, and say so when closing.
- Size: a meaningful deliverable (a feature, a tool, a measured change), usually 1–2 sessions. Don't split work into steps that only make sense together. Test in proportion to the goal.
- Prefer C++ for all code; other languages only where unavoidable (e.g. browser UI).
- If an issue's work touches an Ascanius-owned file, the issue says which file.
- Labels: `bug`, `feature`, `perf`, `tooling`. Milestones group issues.
- To close an issue, add a one-line comment: `Done in <hash>: <result>`, e.g. `Done in 3f2a1c0: +40±25 Elo, 300 games`.

## Commits

```
<area>: <imperative summary, ≤60 chars>

<optional: why, not what. ≤4 lines, wrapped at 72>

Refs #12        (or: Closes #12)
Co-Authored-By: ...
```

- `area` ∈ `search`, `eval`, `movegen`, `tt`, `tb`, `book`, `gui`, `tools`, `build`, `docs`.
- One logical change per commit. The build (`make`) must succeed.
- If the change affects strength or speed, add the measurement to the body, e.g. `nps 1.21M → 1.34M`.
- Commits touching search, eval or move ordering put `bench: <nodes>` (from `make bench`) in the body; commits touching move generation must pass `make perft`. See `docs/BENCHMARKS.md`.
- Don't mix Ascanius-approved edits to Ascanius-owned files with other changes.
- Commit to the issue's branch `issue/N`, in its worktree (see below). It lands on `main` only when Ascanius says the issue is done; an experiment that does not meet the issue is deleted instead.

## Issue worktrees

Every issue gets its own git worktree, so sessions on different issues never share files, builds or a checked-out branch. Issue N lives in `.claude/worktrees/issue-N` on branch `issue/N`. `tools/issue_worktree.sh N` finds it, or creates it from the local `main`. `tools/issue_worktree.sh list` shows them all, with ahead/behind `main` and uncommitted changes.

**Start or resume.** Any task that names an issue number begins with this, before reading or editing anything else:
1. `tools/issue_worktree.sh N`: prints the worktree's path, creating it if needed.
2. `EnterWorktree` with that `path`. Every edit, build, benchmark and commit happens there from now on.
3. If it reports `behind` > 0, `git rebase main` once the tree is clean. Small conflicts now are cheaper than one big one at the end.

**While working**
- The main folder stays on `main` and belongs to Ascanius. Never edit files there and never `git checkout`/`switch` there.
- Heavy jobs already take turns across all sessions (`.claude/hooks/heavy_lock.sh`). A second `make gui` needs its own `GUI_PORT`.
- Searching from the main folder also goes into `.claude/worktrees/`. Search from inside the worktree, or exclude it.

**Landing**, only when Ascanius says the issue is done:
1. In the worktree: `git rebase main`, resolving conflicts there. Show Ascanius any resolution that changes an Ascanius-owned file before continuing.
2. Re-verify on the new base: `make`, plus `make perft`/`make bench` as the commit rules require. If the tip commit's `bench:` no longer matches, amend it.
3. In the main folder: `git merge --ff-only issue/N`. If it refuses (another issue landed first, or Ascanius has uncommitted edits in the same files), say so. Never force it.
4. `git worktree remove .claude/worktrees/issue-N`, `git branch -d issue/N`, then close the issue (`Done in <hash>: …`).

Work without an issue number (a quick fix Ascanius asks for directly) stays in the main folder on `main`, as before.

## Context budget

A hook (`.claude/hooks/context_on_commit.py`) reports the context size after every commit. Claude also runs it with `--now` at natural breakpoints:
- **OK** (<100k): continue.
- **WARN** (100k–160k): finish the current issue, then wrap up.
- **STOP** (≥160k): stop cleanly (tree committed or deliberately left dirty, stated). Then either:
  1. recommend `/compact` if this session's knowledge is still needed for the next step, or
  2. write a startup prompt for a new session:
     ```
     Read docs/WORKFLOW.md, then work on issue #N.
     Issue: <1-line title> — <1–2 sentence plain-language summary of Goal and Done when,
     so Ascanius doesn't have to open GitHub to know what's being asked>.
     State: <1–3 lines not captured in the issue or git log>.
     ```
     This applies to any "work on issue #N" starter message Claude hands Ascanius, not
     only at a STOP breakpoint.
     Then recommend a model and effort level:
     - Sonnet 5 / medium: mechanical work (refactors, tooling, docs).
     - Opus 5.5 / high: normal features and bugfixes.
     - Opus 5.5 / xhigh: subtle search or eval bugs, correctness hunts.
