<!-- OWNERSHIP=Claude -->
# UCI engine

What `./ascaniusfish_uci` (`ascaniusfish_uci.cpp`, `lib/uci.hpp` / `src/uci.cpp`) does beyond plain UCI. Read before working on the UCI layer, its options, or anything the GUI sends it. Keep it in sync with `src/uci.cpp`.

## Overview
`./ascaniusfish_uci` (entry `ascaniusfish_uci.cpp`, protocol in `lib/uci.hpp` / `src/uci.cpp`) speaks UCI on stdin/stdout; the interactive play mode stays in `./ascaniusfish`. The search runs on its own thread; `lib/search_control.hpp` holds the stop flag/deadline that `minimax()`/`minimax_tactical()` poll via `poll_search_abort()`, which throws `search_aborted` to unwind an aborted search (the last completed iteration's move is played).

## Time management
On a clock (`go wtime … btime …`), `lib/time_manager.hpp` / `src/time_manager.cpp` set the hard limit (the abort deadline) and, after every iteration, a soft limit y + λ·u that decides whether the next iteration starts (λ from PV stability across iterations, see the header).

## Tablebases at the root (#38)
`setoption name SyzygyPath value <dir>` loads the tablebases (#38): at a root with ≤5 pieces `UCI_Engine::tb_root_filter` (`src/uci.cpp`) keeps only the moves that preserve the tables' result, winning ones ranked by DTZ inside the 50-move rule, and `search_tb_root` searches just those (the root loop is ours, `minimax()` is untouched). Its score is the tables' (`cp 0`/`±10000`, or a mate the search has seen), never the eval below a kept move; when every kept move's position is itself probed, nothing is searched and the eval picks among them, a mate first. `make tb-suite` (`tools/tb_suite`, suite `tools/tb_suite.epd`) plays 38 won and 22 drawn positions against the engine without tables.

## Tablebases inside the search (#39)
Inside the search (#39) `lib/tb_search.hpp`'s `tb_probe_score()` gives a node its table result (≤ `SyzygyProbeLimit` pieces, no castling, **any halfmove clock**: a running clock may already have made a win a 50-move draw, accepted so every such node scores from the tables; a cursed win or blessed loss is a draw): a win is `TB_WIN_SCORE = INT_MAX - max_mating_seq` (a loss `TB_LOSS_SCORE = INT_MIN + max_mating_seq`), the edge of the mate band: every proven-mate test counts it, the per-ply mate shift leaves it alone, and it is independent of ply; shown by `uci_score()` as `cp ±10000`, never `mate`; `SyzygyPath` empty (also via `setoption`) releases the tables, and `info` carries `tbhits`. The probe calls in `minimax()` and in `minimax_tactical()`'s capture loop, and `make_move()` restarting the halfmove clock, are Ascanius-owned patches (`make tb-trade-suite`, `tools/tb_trade_suite`, 60 positions with 6-7 pieces where the right trade decides; numbers in `docs/measurements/tb_trade_suite_2026-09-30.md`).

## MultiPV (#69)
`setoption name MultiPV value K` (#69) gives K `info depth d multipv i …` lines per depth, best first: `multipv_search()` (`src/uci.cpp`) runs pass k with the first moves of lines 1..k-1 passed to `minimax()` as excluded root moves (an Ascanius-owned patch: such a root neither probes nor stores the TT), sorts the lines and prints them together; an aborted depth keeps the previous set, and a tablebase root ignores K. K=1 is the plain search (`make bench` unchanged). `diagnostics/multipv_agreement.cpp` measures how often line 1 at K>1 still matches K=1 (`docs/measurements/multipv_agreement_2026-10-02.md`).

## Best-move-only TT entries and TTWalk (#82)
A TT entry keeps only its best move (#82, `TT_Result` in `lib/tt_result.hpp`, 32 bytes; `-DTT_FULL_PV=1` stores whole lines again), so a line ends where an exact TT hit returned it; `TTWalk` (check, default on, `extend_pv_from_tt()`) extends each iteration's lines along the TT's moves, stopping at a miss, a bound (its move only failed high or low), an illegal move or a repetition (`docs/measurements/root_pv_length_2026-10-03.md`).

## Non-standard commands: perft, hint (#79)
Non-standard `go perft N` prints a perft divide for the current position — handy for checking FEN loading/movegen. Non-standard `hint depth D pv <moves>` (#79), after `position` and dropped by the next one, seeds the TT before the next `go` (`seed_hint()`): the position i plies down the line gets its move at depth D−i as a **vacuous lower bound** (eval `INT_MIN`, `bound_type -1`), which never cuts or narrows but stays the TT move until the search stores something as deep, so an old or wrong line costs time, never a score; `minimax()` is untouched. Only the GUI sends it (from the PTT); `tools/match` never does.

## Path refresh for analysis (#100)
An analysis search (`go infinite`, or any `go` with the standard `UCI_AnalyseMode` on, #100) first **refreshes its path** (`UCI_Table::refresh_path()`, `lib/uci.hpp`, `info string refresh N entries demoted`): the root's exact entry and every ancestor's entry within its own depth of the root become depth-0 vacuous bounds with their move kept (never a proof or a book entry), so an older entry cannot cut before what was found below it since; a mate `verify_mate()` confirms is written over the root's entry however deep (`store_proven()`), and a hint never seeds over a proof. Non-standard `route <moves>` (after `position`, dropped by the next one, one per line) names another route from the start to a position on the path, a transposition, whose entries are refreshed the same way, distances counted through that position (`refresh_routes()`); only the GUI sends it, with the Engine panel's `Transp.` box on. Game searches are untouched by the refresh (`docs/plans/tt_knowledge_reuse.md`; `make walkback`, `docs/measurements/tt_knowledge_reuse_2026-10-07.md`).

The correspondence mode (#101, `docs/GUI.md`) has two more non-standard commands, each stopping the search first. `ttmark mark M depth D score cp|mate N [move m] fen <fen>` stores an exact entry for that position with the user's mark M (1–32767, in `TT_Result`'s former padding; `UCI_Table::import_marked()`); the GUI marks with the generation (#100). `ttdemote` turns every unmarked entry into a depth-0 vacuous bound with its move kept, a proof excepted (`demote_unmarked()`). Marks belong to the position, and a marked entry's result gives way only to an improvement, `tt_improves_marked()` (`lib/tt_result.hpp`): a shorter mate (an exact one over a claim of as many plies), a proof over no proof, or a deeper exact result, never over a proof; a deeper bound is none, since its move only failed high or low. `insert()` applies it to a search result (an Ascanius-owned patch, #100) and `store_proven()` to a verified root mate; `import_marked()` takes a save that improves so, or one with a higher mark over no proof (an unmarked entry counts as mark 0). The mark becomes the higher of the two either way. Once a table holds a mark, marked entries rank above all others as victims, by mark, then depth, and the rest rank as without marks (`UCI_Table::value_for_victim_index()`). A marked entry is never evicted: an unmarked one ranks below it, and `ttmark` for a new position whose bucket is full of marked entries stores nothing (`not stored, its bucket holds only marked entries`); the answer names the entry's mark, depth, bound and eval. An analysis search at a marked root sets the root's result aside (`UCI_Table::set_aside()`, `info string marked root: …`), since it would cut every iteration up to its depth, and puts it back afterwards unless the search improved on it (`put_back()`); a marked mate stays. Only `ucinewgame` and a change of net or weight set clear the marks with the rest of the table. Under `TT_FULL_PV` there is no room for a mark, and every entry is unmarked. `diagnostics/tt_mark_test.cpp` checks these rules on one bucket.

## Weight sets and provenance (#85)
`setoption name WeightsFile value <file>` (#85) searches with another weight set (`<empty>` = the compiled-in default; a file that does not read keeps the set in use and says why; a change clears the TT). Every search starts with `info string provenance …` (eval version, weight set version, net hash, tables, commit; `ENGINE_COMMIT` from the Makefile), and the answer to `uci` carries `info string evalversion N weights W commit C`.

## Narrowing on stored bounds (#65)
`TT_BOUNDS_NEVER_NARROW` (default 1) keeps #64's rule: in `minimax()` a stored bound only
cuts, it never narrows alpha and beta. Built with `-DTT_BOUNDS_NEVER_NARROW=0`
(`make ascaniusfish_uci_narrow`), the UCI check options `TTNarrowing` (a same-depth bound
narrows, the pre-#64 rule) and `TTNarrowingDeeper` (a deeper one too; only with the former)
set `tt_narrowing` (`lib/search_control.hpp`); `tools/bench narrow=0|1|2` does the same.
The GUI asks its engine once at startup which build it is (`engine_build()`,
`gui/engine_link.hpp`) and shows it, with both switches, in the gear.

Separately, a bound cuts only when it was stored at exactly the node's depth. The UCI check
option `TTDeeperCuts` (#93, default on, every build; `tt_deeper_cuts`, `tools/bench deeper=0` for off)
lets a bound from a deeper search cut too, still without narrowing.
