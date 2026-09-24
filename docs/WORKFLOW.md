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
- One issue = one session's work. Split anything bigger.
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

- `area` ∈ `search`, `eval`, `movegen`, `tt`, `book`, `gui`, `tools`, `build`, `docs`.
- One logical change per commit. The build (`make`) must succeed.
- If the change affects strength or speed, add the measurement to the body, e.g. `nps 1.21M → 1.34M`.
- Don't mix Ascanius-approved edits to Ascanius-owned files with other changes.
- Commit to `main`. Use a branch only for experiments that may be reverted (e.g. `exp/lmr`); merge it if the issue is met, else delete it.

## Context budget

A hook (`.claude/hooks/context_on_commit.py`) reports the context size after every commit:
- **OK** (<100k): continue.
- **WARN** (100k–160k): finish the current issue, then wrap up.
- **STOP** (≥160k): stop cleanly (tree committed or deliberately left dirty, stated). Then either:
  1. recommend `/compact` if this session's knowledge is still needed for the next step, or
  2. write a startup prompt for a new session:
     ```
     Read docs/WORKFLOW.md, then work on issue #N.
     State: <1–3 lines not captured in the issue or git log>.
     ```
     Then recommend a model and effort level:
     - Sonnet 5 / medium: mechanical work (refactors, tooling, docs).
     - Opus 5.5 / high: normal features and bugfixes.
     - Opus 5.5 / xhigh: subtle search or eval bugs, correctness hunts.
