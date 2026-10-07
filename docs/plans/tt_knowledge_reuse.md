<!-- OWNERSHIP=Claude -->
# What the analysis found shows wherever it bears: plan

Written 2026-10-07 on `issue/100` at c739eb9 (branch point 0411135, `make bench` 29770; main is
at 31041 since a43d78a). Line numbers are this branch's; grep again after a rebase. For
Ascanius to decide, and for the agent that implements it, who should not need to redo the
reasoning in §1–§3.

**Goal.** In Analyse, whatever the engine has already found anywhere in the move tree shows up
quickly at every position it bears on, in any visiting order (forward, back, side lines,
transpositions, revisits). Only knowledge a position no longer needs may be thrown away.

**Recommendation in short**
1. Engine, Claude-owned files only, analysis searches (`go infinite`) only. Before the search,
   **refresh the path**: demote the root's exact entry, and every ancestor's entry that lies
   within its own depth of the root. The move is kept and proofs are never touched. After an
   iteration whose mate the mate search verified, **write that proof over the root's entry**,
   however deep the entry is.
2. GUI: a mate beats a kept score however deep, and a fresh result of equal depth replaces the
   kept one. The PTT also prefers a mate.
3. Remove #100's `forget at`, `forget_through` and the GUI's recency stamps.
4. Optional, needs Ascanius: in `insert()`, a proof replaces an unproven entry however deep
   (#98's store rule).

Steps 1–3 leave `minimax()`, `insert()`, `make bench` and every game search as they are, so
they need no SPRT and no `EVAL_VERSION` bump.

## 1. Diagnosis

### 1.1 Why an older, deeper entry beats a newer, more decisive one
Three rules, each sensible on its own:

- **Store: deeper wins.** `insert()` replaces a position's entry only with a deeper one, or
  with an exact one at the same depth (`src/lookup_table.cpp:115-123`). A newer result at a
  lower depth is dropped, whatever it says, a mate included. The search id (age) is used only
  for eviction (`value_for_victim_index`, `src/lookup_table.cpp:19-27`), never for
  replacing the same position and never when probing.
- **Probe: a deep enough entry cuts at every node, the root included.** With stored depth ≥
  requested depth, an exact entry is returned at once (`ascaniusfish_2.hpp:410-413`). A bound
  cuts when it decides the window, at its own depth or, with `TTDeeperCuts`, any deeper one
  (`:414-432`). The root is no exception as long as the entry has a move (#76, `:410`). Each
  iteration calls `minimax()` at the root with a full window (`src/uci.cpp:976-984`, `:908`).
  So a root holding exact +27 @18 returns that entry in iterations 1–18 without looking at a
  single child. That is ply 9's "+27.16, the old root entry".
- **A proof cuts at any depth, but only at its own node** (`ascaniusfish_2.hpp:407-410`). A
  parent is probed before its children, so the parent's unproven entry cuts first and the
  proof below is never reached. The same happens inside the grandparent's search: ply 8's
  search stops at ply 9's +27 @18.

Quiescence adds nothing new: a depth-0 probe takes any exact entry (`:214-216`).

### 1.2 When the TT holds an inconsistent pair
Take an entry E at position P (search s, depth d). E is stale when a later search s' > s
changes the value of a position D in P's subtree that lies within P's horizon
(dist(P, D) ≤ d), and nothing has searched P since.

- **Within one search** this cannot happen: every iteration writes a parent after its
  children.
- **In game play** it hardly matters: each new root is a descendant of the old one, and no one
  probes the ancestors again.
- **In Analyse** it is the normal case: every step back, jump or sideways move lands at or
  above an earlier search's root.

A proof below makes the staleness obvious: a mate makes every unproven ancestor stale,
whatever its depth. But a deeper score does the same, e.g. a child analysed to depth 30 under
a parent stored at depth 18.

### 1.3 What the measurements say
- **The on walk** (`docs/measurements/walkback_2026-10-07.md`, engine on at 1 s per position):
  no step back shows a mate. Each step shows its own old root entry (the probe rule above).
- **An earlier run of the #100 session** (scratch, not committed). The walk had only the M9 as
  a key position and used 10 s per forward step and a 60 s cap. Ply 9 took 5.4 s with the
  engine off, more than 60 s with it on, and 38.7 s with only the path's roots forgotten. The
  38.7 s came from entries in the 5...Kg4 side line, where no search had yet found 6.Qh4+.
  That is not stale knowledge, it is **missing** knowledge, and `forget_through` only hid it
  by throwing away everything older. In today's walk 6.Qh4+ is a key position, so the
  knowledge exists, and a path refresh reaches it (§3).

## 2. Candidate rules

| rule | back | side / jump | correctness | `make bench` | play strength | files |
|---|---|---|---|---|---|---|
| **A** A proof replaces an unproven entry however deep (`insert()`) | not on its own: the parent's own entry still cuts at the root | keeps a proof found under a deeper unproven entry | sound: 0 refuted root mate claims in 463 (`mate_suite_2026-10-03.md`) | may change (rare at depth 3) | game search changes: SPRT | Ascanius (1 line) |
| **B** The root ignores its own exact hit (`minimax()`) | one step only: `insert()` drops the new, shallower result, so the entry is stale again for the next step back | no | sound | unchanged (a fresh TT never holds a deeper root entry) | games change: SPRT | Ascanius |
| **R** Path refresh: demote the root's exact entry, and ancestors within their own depth, before every analysis search (`src/uci.cpp`) | yes | yes, for every ancestor on a searched path | sound: it only removes cuts, and keeps the move | unchanged (bench never calls the UCI search) | unchanged (analysis only) | Claude |
| **W** Write a verified root mate over the root's entry however deep | needed where the root held a deeper bound | same | sound (the mate search never prunes the defender) | unchanged | unchanged | Claude |
| **C** Use an entry only if it is not older than its best child's | partly: misses a non-best child of a max node that became a mate | partly | sound | changes | one `make_move` + probe per TT cut: slower, SPRT | Ascanius (probe and `TT_readout`) |
| **G** Generation test: newer replaces older regardless of depth, or old entries don't cut | replacing alone does not stop old cuts; "old entries don't cut" is `forget_through` made lazy | – | sound | changes | replacing by newer loses deep bounds; SPRT | Ascanius |
| **P** Propagate a found mate up the path | attacker parent only, stops at the first defender | jumps over attacker parents | sound if verified | unchanged | unchanged | Claude |
| **F** #100: GUI recency + `forget at` + `forget_through` | yes | yes, also off the path | sound | unchanged | unchanged | Claude |

The **forward walk** needs none of the rules. Within one process a mate shown already stays
shown one move later (#98's note). R keeps that: it never demotes a proof, and a demoted root
rebuilds from its children within the same iterations.

### Recommended: R + W, analysis searches only
**R, precisely.** At the start of a `go infinite` search, take the path `game[0..n]` (root =
`game[n]`) and the distance `i = n - j`:

- **Root (i = 0):** demote its entry if it is exact. A bound never cuts at the root, whose
  window is full. If a parent's null-window search left it, it is exactly what that parent
  needs back, and its move is the refutation.
- **Ancestors (i ≥ 1):** demote the entry, exact or bound, if `i ≤ its depth`. An entry of
  depth d < i never looked as far as the root (check extensions aside), so nothing found
  there can contradict it.
  Without this horizon test, a long walk back would have to rebuild the whole game through a
  chain of demoted entries.
- `demote()` (`lib/uci.hpp:79`) already refuses proven results, book entries and entries that
  were demoted before. It keeps the move.

**Why demoting, not B.** After a demotion the root's new results are stored (depth 1 > 0, 2 > 1,
…). So the TT then holds what this search found, and the next search one level up finds it.
With B, the old entry would stay in the TT.

**Why it is cheap.** A demoted root rebuilds from its children. Their entries are as deep as
the old root entry minus one, so every iteration up to the old depth is a pass of TT cuts. The
first child is exact; the others carry bounds made against the same value.

**W.** A root that kept a deep bound (not demoted) would drop its own new proof in `insert()`.
So after an iteration whose mate `verify_mate()` confirmed (`mate_check==1`, MultiPV 1), the
proof is written over the root's entry, keeping the deeper of the two depths so it is not the
first entry evicted.

**Seed guard.** `seed_hint()` inserts vacuous bounds at depth D−i, and a deeper vacuous entry
replaces a shallower proof. So the seed skips a position whose entry is proven.

**R subsumes P.** A demoted attacker parent proves itself the moment any search reaches it:
its proven child cuts at depth 1.

**Why analysis only.** In a game, the root's own exact hit saves the first iterations, and the
ancestors are never probed again. R would only add rebuild time and change the iteration
values the time manager reads.

### Why the alternatives lose
- **A** is right, but it does not stop the parent's unproven entry from cutting (the parent is
  probed first). It touches `insert()` and the game search. It stays an optional follow-up
  (§7), aimed at #98.
- **B** fixes one step back, then leaves the stale entry in the TT for the step after. It also
  touches `minimax()` and the game search.
- **C** costs a move and a probe at every TT cut, and still misses a mate in a non-best move
  (10 main's 6.Kf5 if its TT move is another move).
- **G**: replacing by generation alone does not stop old cuts. "Old entries don't cut" is
  `forget_through`. Newer-replaces-deeper loses the deep bounds a parent's null-window search
  needs.
- **F** works, but every step back after a deeper child demotes about 1M entries (everything as
  old as the parent's search or older), and the GUI carries about 150 lines of recency
  bookkeeping.

### Is #100's GUI forget still needed?
No. R does engine-side, on every analysis `go`, what the GUI's `forget at` did for the path.
The GUI's recency stamps also dropped kept (store/PTT) results. Two small store rules replace
that (step 4): a live mate beats a kept score, and the live rebuild reaches the kept depth at
once anyway, so it takes over at equal depth. Remove the forget (step 3).

**Known gap: off-path transpositions.** Take a position X reached by another move order, whose
own entry predates a proof two or more plies below it. X lies on no searched path, so R does
not refresh it. One level works anyway, since X as a root is refreshed. The 3.Bc2+ study has
no such case. Handle it (the GUI would send the other paths) only if it shows up.

## 3. The defender node (ply 9 needs 5...Kg4 6.Qh4+)
A defender node is proven only when every one of its moves is refuted. A single child's proof
says nothing about it (min over all moves), so no rule can push a proof up through a defender.

**What can be fast.** Every refutation that some search has already proven, as long as nothing
unproven between the defender and the proof cuts first. With R, at ply 9:

- **5...Nxd8+**: ply 10 was an ancestor of the M9 key search, so it was refreshed then and
  re-proves itself at depth 1.
- **5...Kg4**: ply 10' was an ancestor of the 6.Qh4+ key search; the same applies.
- **The other defences** are short (6.Qh4# against most). The engine-off walk refutes all of
  them in 0.0–0.1 s.

So ply 9 should take about as long as in the engine-off walk. Ply 8 (the attacker above it)
follows at depth 1.

**What cannot be fast.** A defence whose refutation no search has found yet: 5...Kg4 before
6.Qh4+ was analysed (5.4 s cold, 38.7 s through entries that missed it), or 4...Kg4 at ply 7
(5.Qf6, not found in 60 s). Nothing can propagate a proof that does not exist. The old entries
in such a line are unproven "no mate within d" claims, and nothing known contradicts them.
Discarding them can let a fresh search find the refutation sooner, at the cost of the whole
table. The user's way out is the key-position move: analyse that defence, and from then on
its proof propagates.

A possible later GUI aid, not in this plan: mark which defences are already refuted, as
`tbMoves` does for tablebase moves.

## 4. Implementation steps
Run every heavy command as `flock -w 3600 /tmp/ascaniusfish-heavy.lock <command>`. Each walk
takes about 30–40 s. Commit messages follow `docs/WORKFLOW.md` (area, ≤60 chars, `Refs #100`
or the new issue's number). Step 6 needs Ascanius's approval before it is applied.

### Step 0: set up
`tools/issue_worktree.sh 100` (or the new issue's, §6), `EnterWorktree`, `git rebase main` on
a clean tree. Then `make`, and `make bench` should give 31041 (main's since a43d78a). If it
does not, stop and report.

### Step 1: `tools/walkback` gets the forward and mixed walks (`tools/walkback.cpp`)
- `Step` gets two new fields:
  - `long long engine_mate_ms = -1`: the first `info depth` line with `score mate`, whatever the
    page shows. Set it in `analyse()` from the parsed `Search_Info` before `set_analysis()`,
    and also in the stop's tail loop.
  - `int refreshed = -1`: parsed from `info string refresh %d entries demoted`.
- `print_step()` adds `engine X.X s` (or `-`) and, when ≥ 0, `refresh N`.
- Add the `fwd` and `mixed` walks exactly as in §5. Add the two move arrays `MATE_LINE` and
  `SWAP_LINE`. Each walk ends with a summary line `<walk>: N of M steps showed a mate`.
- `walks=` also accepts `fwd,mixed`. Keep the default `off,on,forget` until step 3.
- In the Makefile, change the comment `(~25 min)` to `(~40 s per walk)`.
- **Baseline:** `./tools/walkback walks=off,on,forget,fwd,mixed`, about 3 min, on the rebased
  branch as it is. Record it in `docs/measurements/tt_knowledge_reuse_<date>.md` (header: commit
  and command).
- Commit: `tools: walkback walks forward and in mixed order`.

### Step 2: the engine refreshes its path (lib/tt_result.hpp, lib/uci.hpp, src/uci.cpp)
(a) `lib/tt_result.hpp`. Add `#include <climits>` and `#include "Settings.hpp"` (for
`max_mating_seq`), and after the `#endif` of the `TT_FULL_PV` block:
```cpp
// An exact mate or table score (#78, #39): it holds at every depth, which is why
// minimax() cuts on it at any depth. Works for both TT_Result layouts.
inline bool tt_proven(const TT_Result& r)
{
    return r.bound_type==0 && (r.eval<=INT_MIN+max_mating_seq || r.eval>=INT_MAX-max_mating_seq);
}
```
(b) `lib/uci.hpp`, `struct UCI_Table`. Add three public members. In `demote()`, use
`tt_proven(r)` for `proven`.
```cpp
    // The entry of a position, or nullptr.
    TT_slot* find_slot(uint64_t zobrist_hash)
    {
        TT_bucket& bucket = table[get_hash(zobrist_hash)];
        for(int i=0;i<bucket.fill_count();i++)
        if(bucket.holds(i, zobrist_hash))
        return &bucket.slot[i];
        return nullptr;
    }

    // Before an analysis search: what was stored before this search learns
    // something below it must not cut. Demotes (demote(): the move stays) the
    // root's entry if it is exact, and every ancestor's entry, exact or bound,
    // that is i plies above the root with i <= its depth. A bound never cuts at
    // the root (full window) and is what the parent's null-window search needs
    // back; an entry shallower than its distance never saw the root. Proofs and
    // book entries stay. Returns the entries demoted.
    int refresh_path(const std::vector<BB>& path)
    {
        int demoted = 0;
        const int root = (int)path.size()-1;
        for(int j=0;j<=root;j++)
        {
            TT_slot* s = find_slot(path[j].zobrist_hash);
            const int i = root-j;
            if(s && (i==0 ? s->pv_line.bound_type==0 : i<=s->pv_line.depth))
            demoted += demote(*s);
        }
        return demoted;
    }

    // A verified mate for the root holds at every depth; insert() would keep an
    // older, deeper bound over it. Written over that entry, at the deeper depth
    // of the two so it is not the first one evicted.
    void store_proven(uint64_t zobrist_hash, const PV_Line& pv)
    {
        TT_entry entry;
        entry.zobrist_hash = zobrist_hash;
        entry.initialized = true;
        entry.pv_line = pv;
        entry.pv_line.bound_type = 0;
        TT_slot* s = find_slot(zobrist_hash);
        if(!s)
        {
            insert(entry);
            return;
        }
        entry.pv_line.depth = std::max(entry.pv_line.depth, s->pv_line.depth);
        entry.search_id = current_search_id;
        *s = TT_slot(entry);
    }
```
(c) `src/uci.cpp`, `UCI_Engine::search()`. Right after `send("info string " + provenance());`
(`:931`):
```cpp
    // An analysis walks the move tree in any order: refresh the path first
    // (UCI_Table::refresh_path()). A game search only moves forward and is left as it was.
    if(table && limits.infinite)
    send("info string refresh " + std::to_string(table->refresh_path(game)) + " entries demoted");
```
In the iteration loop, right after the `verify_mate()` call (`:1000`):
```cpp
        // A verified mate goes over the root's entry however deep (UCI_Table::store_proven()).
        if(table && limits.infinite && mate_check==1 && lines_wanted==1)
        table->store_proven(root.zobrist_hash, pv);
```
(d) `src/uci.cpp`, `seed_hint()` (`:325`). Replace `table->insert(entry);` with:
```cpp
        // A proof there stays: a deeper vacuous bound would replace it.
        const TT_slot* held = table->find_slot(pos.zobrist_hash);
        if(!held || !tt_proven(held->pv_line))
        table->insert(entry);
```
Leave `forget`/`forget_through` in place; step 3 removes them.

**Measure:** `make`; `make bench` must still give 31041 (it never calls the UCI search; a
different number means something leaked into `minimax()`, so stop). Then
`./tools/walkback walks=off,on,fwd,mixed`. Pass criteria are in §5.

**Decision point.** If on-walk ply 9 or mixed-walk ply 8 shows no engine mate within 1 s in a
run and in its one rerun, go to step 7 and do not continue. The forget is the fallback, and it
is still there.

Commit: `tt: an analysis search refreshes its path first`, with the walk numbers and
`bench: 31041` in the body.

### Step 3: remove #100's forget
- **Engine** (commit `tt: drop forget, the path refresh replaces it`):
  - `lib/uci.hpp`: remove `forget()`, `forget_through()`, `root_search`, `clear()`, the
    `handle_forget` declaration, and the forget lines of the header comment (`:26-27`).
    Rewrite the `UCI_Table` comment for `refresh_path`/`store_proven`.
  - `src/uci.cpp`: remove `handle_forget()`, the `forget` command branch, and the
    `root_search` line in `search()`. Change the three `table->clear()` calls back to
    `table->reset()`.
  - Bench unchanged.
- **GUI** (commit `gui: drop the recency stamps and the forget plan`):
  `git checkout main -- gui/session.hpp gui/analysis_store.hpp gui/engine_link.hpp gui/gui_server.cpp diagnostics/analysis_store_test.cpp`.
  On this branch only 4322550 touched these files, so this restores exactly main's versions.
  Afterwards `git diff main -- gui diagnostics/analysis_store_test.cpp` must be empty. Build
  `make gui/ascaniusfish_gui`.
- **Tool** (commit `tools: walkback without forget`): remove the `forget` walk, `Forget_Plan`/
  `forgot()` and `analyse()`'s `forget` argument. The default becomes `off,on,fwd,mixed`.
- **Measure:** bench 31041, the walks as in step 2 (same results within noise), and
  `diagnostics/analysis_store_test` passes.

### Step 4: the GUI prefers a mate (gui/analysis_store.hpp, gui/session.hpp, gui/ptt.hpp)
- `Analysis_Store`:
```cpp
    // A mate is a proof: it holds at any depth.
    static bool proven(const Search_Info& i) { return i.score_kind=="mate"; }

    // Whether `kept` tells at least what `fresh` does: as many lines, and a mate
    // where the fresh one has only a score, or else strictly deeper. At equal
    // depth the fresh result wins: it was made with whatever was found since.
    static bool covers(const Search_Info& kept, const Search_Info& fresh)
    {
        if(kept.more.size()<fresh.more.size())
        return false;
        if(proven(kept)!=proven(fresh))
        return proven(kept);
        return kept.depth>fresh.depth;
    }
```
- `Session::set_analysis()` (`:603`): the condition becomes
  `analysis_valid && analysis_stored && !analysis_older && Analysis_Store::covers(analysis, info)`.
  Drop `info.depth<analysis.depth`, which `covers()` now contains.
- `Session::recall_analysis()` (`:1592`): `kept->info.depth>found.depth` becomes
  `!Analysis_Store::covers(found, kept->info)`.
- `PTT_Store::better()` (`gui/ptt.hpp:148`): before the last line, add
  `if((fresh.info.score_kind=="mate")!=(kept.info.score_kind=="mate")) return fresh.info.score_kind=="mate";`.
- **Tests.** `diagnostics/analysis_store_test.cpp`:
  - a live mate at depth 3 replaces a kept score at depth 18 at once;
  - a live score at depth 20 does not replace a kept mate;
  - a fresh result of equal depth replaces the kept one in the store.

  `diagnostics/ptt_test.cpp`: a mate beats a deeper score of the same eval. The existing
  "reaching that depth, the search takes over" case must still pass.
- **Measure:** both tests pass. In the walks, at every step where the engine has a mate, the
  shown mate comes no more than 0.1 s after `engine`.
- Commit: `gui: a mate beats a kept score however deep`.

### Step 5: documentation
- Finish `docs/measurements/tt_knowledge_reuse_<date>.md`: the baseline, steps 2–4, and a
  short reading.
- `CLAUDE.md`:
  - UCI: replace the `forget at` sentence with the path refresh and `store_proven`.
  - Analysis_Store: "a mate beats a score; otherwise deeper wins, and a fresh result of equal
    depth replaces the kept one"; remove #100's recency paragraph.
  - PTT: a mate beats a score.
  - The `make walkback` line: "walks off/on/fwd/mixed, ~40 s each".
- Commit: `docs: ...`.

### Step 6 (optional): Patch A, only with Ascanius's approval of the hunk in §7
Apply it in a commit of its own. Then:
- Run `make bench`. If the number changes, bump `EVAL_VERSION` and run the SPRT:
  `tools/match main . sprt=0,10 tc=5+0.05 time=10`. Record it in `docs/measurements/`.
  Ascanius decides on an undecided result.
- Run the walks. Expect them equal or better, since the key searches' inner proofs now
  survive.

### Step 7 (only if step 2's decision point fails): diagnose, then ask
- Add a UCI debug command `ttprobe` to `src/uci.cpp`. For every legal root move it prints
  `info string ttprobe <uci> depth D bound B eval E search S proven P`, from `find_slot()` on
  the child.
- `walkback` sends it before the failing step's `go` (`probe=1`).
- Report to Ascanius the child whose entry is unproven although the engine-off walk refutes it
  in a fraction of a second, with its search id.
- Change nothing in `minimax()`/`insert()`. The candidates are Patch A (a proof dropped under
  a deeper entry) or a narrowly scoped `forget_through` (old unproven entries in a defence's
  subtree), and both are Ascanius's call.

## 5. Walk specifications and pass criteria
All walks use the 3.Bc2+ study (`8/3P3k/n2K3p/2p3n1/1b4N1/2p1p1P1/8/3B4 w - - 0 1`). They run
with no PTT and no tablebases, and each starts a fresh engine process. Analysis steps are
`back_ms` = 1 s; key positions run until they show a mate, capped at `key_ms` = 10 s. A key
position that misses its cap voids its walk: rerun that walk once. "Mate" means `engine` (the
engine's first mate line); from step 4 on, the shown mate as well.

**off and on (exist today).**
- **off:** after = baseline, and every step reports `refresh 0`. #100's "nothing forgotten on
  the engine-off walk" holds.
- **on:**
  - side step at ply 10 (after 5...Nxd8+, revisited once the M9 is known): mate ≤ 0.2 s
    (baseline: expected none, since its own +27 root entry cuts);
  - back steps at plies 10' (after 5...Kg4), 9 and 8: mate ≤ 1 s each, predicted 0.0–0.4 s
    (baseline: none);
  - ply 7: none;
  - forward steps: depth reached within 1 of the baseline.

**fwd (new): forward along a known mate, then a transposition.**
1. Engine off, play `STUDY_LINE` (11 plies), then key position ply 11 (the M9).
2. `MATE_LINE`, playing one move and analysing 1 s per position:

| ply | move |
|---|---|
| 12 | `e3e2` (6...e2) |
| 13 | `c2e4` (7.Be4) |
| 14 | `e2e1n` (7...e1=N) |
| 15 | `e4d5` (8.Bd5) |
| 16 | `c3c2` (8...c2) |
| 17 | `d5c4` (9.Bc4) |
| 18 | `c2c1n` (9...c1=N) |
| 19 | `c4b5` (10.Bb5) |
| 20 | `a6c7` (10...Nc7) |
| 21 | `b5a4` (11.Ba4) |

3. Go `BACK` 8 times without analysing, to ply 13. Then `SWAP_LINE` (side line), 1 s each:
   `c3c2` (7...c2), `e4d5` (8.Bd5), `e2e1n` (8...e1=N), `d5c4` (9.Bc4). The last position is
   ply 17's position again, halfmove clock included.

Expected, baseline and after: a mate at every step, predicted ≤ 0.5 s. **Pass:** every step
that showed a mate in the baseline still does, at most 0.2 s later.

**mixed (new): jumps, a side line, revisits.**

| # | how | position | baseline | after (pass) |
|---|---|---|---|---|
| 1 | engine on, 1 s per position, plies 0–10 along `STUDY_LINE`, play `e6f5` | – | – | – |
| 2 | key position | ply 11 (M9) | mate | mate |
| 3 | `BACK`×2, no analysis in between, 1 s | ply 9 (after 5.Ke6) | no mate | no mate (5...Kg4 is not refuted yet) |
| 4 | play `h5g4`, 1 s | ply 10' | no mate | no mate |
| 5 | play `d8h4`, key position | ply 11' | mate | mate |
| 6 | `BACK`×3, no analysis in between, 1 s | ply 8 (after 4...Nf7+) | no mate | mate ≤ 1 s (predicted ≤ 0.5 s) |
| 7 | `FORWARD` (to ply 9), play `f7g5`, 1 s | ply 10'' (after 5...Ng5+) | record | mate ≤ max(1 s, baseline + 0.2 s) |
| 8 | `BACK`, 1 s | ply 9 | no mate | mate ≤ 0.2 s |
| 9 | `FORWARD` (main line, 5...Nxd8+), 1 s | ply 10 | no mate | mate ≤ 0.2 s |
| 10 | `START`, 1 s | ply 0 | no mate | no mate; record the live depth at 1 s and what was shown |

Row 10 shows the cost of the horizon rule: the plies 0–10 all lie within their depth of the
key positions, so ply 0 rebuilds through demoted entries. The store's kept result stays on
show meanwhile. If its live depth at 1 s is far below the baseline's, that is open question 5.

## 6. What to keep and what to revert from #100

**Keep**
- 1252d70 (TTWalk follows exact entries only). Demoted entries keep their move, so a line must
  not walk on them.
- The `UCI_Table` subclass and its `demote()`.
- `tools/walkback` with its make target and `.gitignore` line.
- `docs/measurements/walkback_2026-10-07.md`, as history.

**Remove**
- cf9b4b5's `forget at` command (`handle_forget`, the loop branch, `UCI_Table::forget`).
- f97b28a's `forget_through`, `root_search` and `clear()`.
- All of 4322550 (GUI recency, `Search_Request::forget`, `Analysis_Store::erase` and `put()`'s
  bool, the test additions, its CLAUDE.md text).
- The walkback's `forget` walk.

**If this becomes a new issue instead:** branch from main and cherry-pick 1252d70 and the
walkback commits 912155c, 3000ccc and c739eb9, adapting `analyse()` to the version without
forget. Also cherry-pick this plan's commit. Take nothing of cf9b4b5, f97b28a or 4322550.
Step 3 then shrinks to the walkback change.

## 7. Patch A for Ascanius (optional, not applied without approval)
A proof replaces an unproven entry of the same position however deep. `tt_proven()` comes
from step 2 (`lib/tt_result.hpp`, Claude-owned), so the Ascanius-owned change is this one
line:

```diff
--- a/src/lookup_table.cpp
+++ b/src/lookup_table.cpp
@@ -112,7 +112,7 @@
                     if(bucket.holds(i, new_entry.zobrist_hash))//if they are equal
                     {
                         is_already_in_table=1;
-                        if(new_entry.pv_line.depth>old_entry.pv_line.depth)
+                        if(new_entry.pv_line.depth>old_entry.pv_line.depth || (tt_proven(new_entry.pv_line) && !tt_proven(old_entry.pv_line)))//a proof holds at every depth
                         {
                             old_entry=new_entry;
                         }
```

- **What it changes.** A mate that a search proves where the position already holds a deeper
  unproven entry is kept instead of dropped (a key search's inner proofs; #98).
- **What it doesn't change.** A deeper unproven entry can still replace a proof. That case is
  unreachable in practice, because a proof cuts every probe of its position, so that position
  is never searched again.
- **Cost.** `make bench` may change. If it does, step 6's SPRT and an `EVAL_VERSION` bump
  follow.

## 8. Open questions for Ascanius
1. A new issue, or this replaces #100's approach on `issue/100`? If it replaces it: either
   removal commits on top (the history shows add, then remove), or rebuild the branch from
   main as in §6 (a history rewrite, your call).
2. Is `go infinite` the right switch for "analysis", or should it be the standard UCI option
   `UCI_AnalyseMode`, which the GUI would set per search kind?
3. Patch A: now, with #98, or not at all?
4. `EVAL_VERSION`: I propose no bump, since game searches are unchanged and only the reuse
   between analysis searches changes. Agreed?
5. The horizon test (ancestor i plies up refreshed only if i ≤ its depth). Is it acceptable
   that a far jump back rebuilds from depth 1 while the kept result stays on show (mixed row
   10)? The alternative is refreshing only on a proof, which would leave deeper scores stale.
6. Off-path transpositions stay a known gap (§2). Handle them only if you run into one?
7. #95's `forget path` overlaps with the path refresh. When #95 is rebased, drop it in favour
   of this?

**Coding agent:** Opus 5.5 at high effort for steps 0–6. The steps are concrete, but the
engine change is about TT semantics, and the walks need judgement at the decision point. Use
xhigh only if step 7 is reached.
