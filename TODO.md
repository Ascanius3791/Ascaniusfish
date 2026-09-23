<!-- OWNERSHIP=Claude -->
# Performance TODO

Findings from investigating "Stockfish searches much deeper, much faster" (2026-09-22).
Ownership note: `move_generation.cpp`/`.hpp` and everything else these items touch are
`#OWNERSHIP=Ascanius`. Items 1 and 3 were implemented directly in those files with
Ascanius's explicit go-ahead in conversation (2026-09-22); items 2 and 4 have not been
touched or approved and remain proposals only.

## 1. Legality checked by "make the move, then rescan" — DONE (2026-09-22)

`all_moves()` (`src/move_generation.cpp`) used to generate pseudo-legal targets for
every piece, then for *each one* do `Base_BB` (a full board-struct copy) followed by a
call to `in_check()` to see if it's actually legal. That meant the cost of a full
king-safety scan was paid once per candidate move instead of once per node — roughly
a branching-factor-sized multiplier (~35x on average) on move generation.

Fix applied: compute checkers + pinned-piece masks once per node, then generate only
already-legal moves directly (king moves get an O(1) "danger squares" bitboard instead
of a per-destination in_check() call; pinned pieces get a precomputed allowed-ray mask;
check evasions get a precomputed capture/block mask). En passant stays the expensive
"make it, then verify" way even in the fast version — it's the one move type where
removing two pawns from the same rank can expose a check a simple pin mask can't
cover cheaply, and it's rare enough (≤2 per position) that it doesn't matter.

With Ascanius's explicit go-ahead, this is now integrated: the previous implementation
was renamed to `all_moves_old()` (kept as a reference/fallback), and the new
checkers/pin-mask implementation now lives under the name `all_moves()` in
`src/move_generation.cpp` — every caller in the codebase (minimax, sorting_moves,
opening book loading, PGN generation, etc.) picks it up automatically since they all
call `all_moves()` by name. Declarations for both are in `lib/move_generation.hpp`.

Verification (2026-09-22, re-run against the actual integrated `all_moves()`/
`all_moves_old()`): recursive move-list agreement checked across ~17,000 nodes total
from six standard perft positions (startpos + Kiwipete + CPW positions 3-6, chosen to
exercise pins/checks/en passant/castling/promotions) — 0 mismatches. Perft node counts
match known-correct values for startpos (depths 1-4: 20/400/8902/197281) and Kiwipete
(depths 1-3: 48/2039/97862), and match `all_moves_old()` exactly (old-vs-new
cross-check) at depth 3 for all six positions. See
[diagnostics/legal_movegen_perft_compare.cpp](diagnostics/legal_movegen_perft_compare.cpp)
(rerun it after any future move-generation change).

One real bug was caught and fixed during verification: the first draft nested the
double pawn push's legality check inside the single push's checkmask/pin gate, so a
single push failing checkmask (e.g. because the king was in check and the one-square
square didn't block it) silently suppressed an independently-legal double push too.
Caught by the recursive move-list diff, not by node counts alone — the six-position,
full-tree comparison approach is worth keeping for any future move-generation change,
node counts alone can mask exactly this kind of one-move discrepancy if it's rare.

Combined speed (with item 3 below, also landed in the same pass): `perft(startpos, 5)`
(4,865,609 nodes) went from **14.5M nodes/s → 42.2M nodes/s** (~3x), of which ~2.4x is
this item and the remaining portion is item 3's DEBUG_MODE gate (measured by also
timing `all_moves_old()` after item 3's fix alone: 34.4M nodes/s, vs. 14.5M nodes/s
before either fix).

**Second bug found via random-position stress testing (2026-09-22, see item 5):**
the checkers bitboard never considered the enemy king itself, only pawns/knights/
sliders. `in_check()` (src/checks.cpp) does treat "enemy king adjacent to my king" as
a check - real legal play can never reach that (a king can never move next to the
enemy king in the first place), but arbitrary positions can have it, and 6.7% of
20,000 random positions hit exactly this. Fixed by adding `checkers |= K_template[king_sq]
& enemy_king;` to the checkers computation. Re-verified after the fix: 0 mismatches
across the curated perft suite (unchanged) and across 20,000 random root positions +
162,607 recursively-compared nodes from a 500-position sample (was 1,338/20,000 and
3/500 before the fix) - see
[diagnostics/random_position_stress_test.cpp](diagnostics/random_position_stress_test.cpp).

## 2. Sliding-piece attacks — smaller win than first thought, correct as noted

Corrected from the initial read: `all_moves()` and `in_check()` already use magic
bitboards (`get_rook_attacks`/`get_bishop_attacks`, `src/magics.cpp`) for rook/bishop/
queen pseudo-legal targets and for detecting sliding checkers — this is *not* the
ray-stepping loop it first looked like. The ray-stepping (`for(int j=1;j<8;j++)`
stepping one square at a time) only remains in `one_move()` (the stalemate/checkmate
"is there at least one legal move" helper), which is called far less often than
`all_moves()`.

Update (2026-09-22): `one_move()` also never got item 1's legal-only rewrite - it still
tests every pseudo-legal candidate with make-move + `in_check()`, same as
`all_moves_old()` did. Wrote and stress-tested a drop-in replacement,
`one_move_fast()`, in
[diagnostics/one_move_fast_stress_test.cpp](diagnostics/one_move_fast_stress_test.cpp):
same checkers/checkmask + pinned-piece + king-danger-square bitboards as `all_moves()`,
computed once per node, short-circuiting `true` on the first legal destination found
per piece type instead of building a move list. Verified against `one_move()` with zero
mismatches across 50,000 random root positions, 4,383,407 recursively-compared random
nodes (3 plies), ~590,000 nodes from the CPW perft suite (startpos/kiwipete/position3-6,
3-4 plies), and three curated checkmate/stalemate positions (fool's mate, scholar's
mate, king+queen stalemate). Not yet wired in: `one_move()` lives in
`src/move_generation.cpp` and is called from `exception_eval()` in `src/eval.cpp`, both
Ascanius-owned, so landing it needs Ascanius's go-ahead on the diff.

Side finding from the stress test: `one_move()` does `BB* wfh = new BB;` on entry and
never `delete`s it - a 136-byte leak (`sizeof(BB)`) on every call. `exception_eval()`
calls `one_move()` at every leaf `eval()` reaches during search, so this leaks on every
leaf node of every real game, not just in the stress test (where hammering it millions
of times in a tight loop is what made the leak visible - RSS hit 1.5GB and got
OOM-killed at an earlier, deeper stress-test setting before the leak was identified and
the test scaled back). Worth fixing whenever `one_move()` is next touched, independent
of whether `one_move_fast()` replaces it.

## 3. `all_moves()` unconditionally recomputes every child's Zobrist hash — DONE (2026-09-22)

At the end of what's now `all_moves_old()` (`src/move_generation.cpp`), there's a loop
over every generated move slot that calls `Zobrist::compute_Zobrist_Hash(wfh[k])` from
scratch and compares it to the incrementally-updated hash, printing and blocking on
`std::cin.get()` on a mismatch. This loop was **not gated by `DEBUG_MODE`**
(`lib/Settings.hpp`'s existing flag for exactly this kind of consistency check) — it
ran unconditionally, on every node, for every child position, in the normal release
build. That's a full from-scratch hash computation (proportional to piece count) for
every single move generated, on top of the incremental update that already happens
during move generation.

Fix applied, with Ascanius's explicit go-ahead: the whole verification loop is now
wrapped in `if constexpr(DEBUG_MODE)`, same as other consistency checks in the
codebase. Confirmed this alone was a large cost: `perft(startpos, 5)` with
`all_moves_old()` went from 14.5M nodes/s to 34.4M nodes/s just from this gate (see
item 1's combined numbers above; the new `all_moves()` reaches 42.2M nodes/s with both
item 1 and this fix applied). The new `all_moves()` never had this loop at all.

## 4. No quiescence search / null-move pruning / LMR

`ascaniusfish_2.hpp`'s `minimax()` has no quiescence search (the code's own comment
says it's still pending), no null-move pruning, and no late-move reductions. Without
these, the effective branching factor stays close to full width at every ply, which is
usually the dominant reason a full-width alpha-beta engine plateaus at shallow depth
compared to something like Stockfish that prunes/reduces away most of the tree. Not
started — biggest expected win, but the largest, riskiest piece of work (touches search
correctness, not just speed), and belongs in `ascaniusfish_2.hpp` (Ascanius-owned).

## 5. `initialize_FEN_to::random_position()` had a real infinite-loop bug — DONE (2026-09-22)

Unrelated to search speed, but found and fixed while using this function to
stress-test item 1's move generator. Two bugs in `ascaniusfish.hpp`:
- If every piece-type flag (`pawn`/`rook`/`knight`/`bishop`/`queen`) was `false` but
  more than 2 pieces were requested, the per-piece placement loop had nothing it was
  allowed to place and spun forever. Reproduced directly (`random_position(Board,
  INT_MAX, 10, 0,0,0,0,0)` hangs on the unpatched version). Fixed by clamping
  `num_of_pieces` to 2 (kings only) when all five flags are disabled.
- The outer rejection-sampling loop (retrying until `eval(...)` is within
  `max_eval_diff` of 0) had no attempt cap, so a tight `max_eval_diff` combined with
  an unlucky material draw could run near-indefinitely. Fixed with a 5000-attempt cap
  that falls back to the closest attempt seen rather than hanging.

`random_position()` only fills a raw `Board[12]`, not a full `BB` - castling rights
need inferring separately. `castling_rights()` (`src/Bitboards.cpp`) does this
correctly (checks both king *and* rook home-square placement, unlike
`castling_right_rook_correction_for_col()`/`check_castling_rights()` which only check
the rook and assume the king invariant already holds from incremental play) - see
`build_random_BB()` in
[diagnostics/random_position_stress_test.cpp](diagnostics/random_position_stress_test.cpp)
for the pattern: build the `BB`, set `en_passant=0`, call `castling_rights(&pos)`,
then recompute `zobrist_hash`.

This is also the harness that found item 1's enemy-king-checker bug above -
20,000 random positions + a 500-position/162k-node recursive sample, 0 mismatches
after both fixes.

## 6. TT entries that resolve to forced mate should be accepted regardless of stored depth — DONE (2026-09-23)

Normal TT probing only trusts a stored entry if its stored search depth is >= the depth
currently needed (a shallower search is less reliable than what's being asked for now).
But if a stored entry's score is a mate score (encoded near `INT_MIN`/`INT_MAX`, see
`interpret_eval()` in `ascaniusfish.hpp`), a deeper re-search can't produce a more useful
answer - the position is a forced win/loss regardless of how much further you look, so
the depth check is pointless there and just wastes a re-search.

Why this is sound: mate scores only ever originate from an actually-reached checkmate
(`exception_state==2` in `minimax()`), never from a heuristic `eval()` extrapolation, and
the distance-to-mate is position-relative (adjusted by exactly 1 per ply on the way back
up the tree, in `minimax()`'s own eval++/eval-- lines), not root-relative - so a
`bound_type==0` entry with a mate-range eval is a completed proof about that exact board
state, independent of the depth budget that found it. `is_retrivable_eval()`
(`src/lookup_table.cpp`) does a full `are_equal()` board comparison, not just a hash
match, so there's no added hash-collision exposure. `are_equal()` doesn't compare halfmove
clock/repetition history, so in principle a stored mate could be reached via a path where
the 50-move rule intervenes first - a pre-existing gap shared by every exact TT entry, not
new here, and low-risk given mate sequences found this deep are short.

Implemented with Ascanius's explicit go-ahead in `ascaniusfish_2.hpp`'s TT probe path in
`minimax()`: added one OR'd condition (`is_proven_mate`, requiring `bound_type==0` and an
eval in the mate range) to the existing outer depth gate. The bound-tightening branch
(`else if(depth==readout.pv_line.depth)` - alpha/beta only updated on an exact depth
match) was deliberately left untouched, since `is_proven_mate` requires `bound_type==0`
and that branch only runs when `bound_type!=0`.

## 7. `minimax_tactical()` only extends into good captures - other tactical moves are dropped

`minimax_tactical()` (`ascaniusfish_2.hpp`, invoked from `minimax()` at `depth==0` as the
quiescence-style leaf search) builds `tactical_order` from `is_good_capture()` alone - any
move that isn't a capture, however tactically forcing (checks, especially discovered/
double checks; promotions, including underpromotions; possibly capture-threats that
`is_good_capture()`'s SEE-style filter scores as "not good" but that still matter, e.g.
because they fork or open a discovered attack), is treated as quiet and excluded from
further search, same as a genuinely quiet move. This risks the classic quiescence-search
horizon problem: a position that looks quiet by "no good captures available" but has a
forcing check or promotion right at the search horizon gets evaluated statically instead
of resolved, which can misjudge tactics - including missing forced mates that start with
a non-capturing check. Not started - touches `ascaniusfish_2.hpp` (Ascanius-owned), needs
Ascanius's go-ahead on both the design (which move classes to add, and how to keep
`max_non_king_pieces`'s termination-bound argument valid once non-capturing moves - which
don't remove a piece from the board - can also recurse) and the diff before landing.
