<!-- OWNERSHIP=Claude -->
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

"Ascaniusfish" is a from-scratch bitboard chess engine in C++ (no external chess libraries). It does full legal move generation, alpha-beta minimax search with a transposition table, a hand-written positional/material evaluator, Zobrist hashing, opening-book loading, and an optional Python/tkinter GUI (`display_board.py`) driven over a pipe/file handshake. There's also a self-play weight-tuning loop.

## File ownership rules

Every file in this repo declares ownership via a comment at the very top of the file, containing `OWNERSHIP=Ascanius` or `OWNERSHIP=Claude`, written with that file's own comment syntax (`// OWNERSHIP=Claude` in C++ `.cpp`/`.hpp` files — a literal leading `#` there is a preprocessor directive and fails to compile; `# OWNERSHIP=Claude` in the Makefile, Python, and shell files).

- **Never silently edit a file owned by Ascanius** (`#OWNERSHIP=Ascanius`) — treat it as off-limits by default, even when running with auto-edit / "always allow edits" privileges. A standing permission mode does not by itself count as authorization to edit these files.
- If a task requires changing an Ascanius-owned file, stop and propose the exact change to the user (e.g. as a patch) instead of editing it directly.
- Modifying an Ascanius-owned file is allowed once Ascanius has explicitly given permission for that specific change in the conversation. The point of this rule is that Ascanius always knows what changed and why — never that a change lands without them seeing and understanding it first, even if they've said yes.
- Files owned by Claude (`#OWNERSHIP=Claude`) may be edited freely.
- A file with no ownership comment has no declared owner — ask the user before editing it rather than assuming either ownership.
- Whenever Claude creates a new file, mark it as Claude-owned by adding `#OWNERSHIP=Claude` as a comment at the very top of that file.
- All Markdown files are Claude-owned (`<!-- OWNERSHIP=Claude -->`). `.claude/settings.json` is Claude-owned too; JSON can't hold the marker.

## Workflow

Development is GitHub-issue driven. Read `docs/WORKFLOW.md` before writing an issue or a commit.

**Every issue has its own git worktree.** When a task names an issue number, first run `tools/issue_worktree.sh N` and switch into the path it prints with `EnterWorktree` (`path`), before reading or editing anything else. Landing on `main` happens only when Ascanius says the issue is done (see "Issue worktrees" in `docs/WORKFLOW.md`).

## Build & run

Build system is a plain `Makefile` (`CXX=g++`, `-O3 -DNDEBUG` by default). This is a **header-driven single-TU build**: `ascaniusfish.cpp` is the only compiled source given to g++; every `lib/*.hpp` `#include`s its matching `src/*.cpp` directly (see e.g. `lib/Bitboards.hpp` including `../src/bit_operations.cpp`), so the whole engine is compiled as one translation unit. There is no separate object-file linking step — don't try to compile `src/*.cpp` files independently. The one exception is third-party C: the UCI engines also link `third_party/gaviota/libgtb.a` (see **Tablebases**).

```bash
make              # builds ./ascaniusfish and ./ascaniusfish_uci (equivalent: make all)
make run          # build + run (alias: make play)
make debug        # build with -O0 -g (no optimizations, for debugging)
make asm          # emit ascaniusfish.s (assembly output)
make tests        # builds hash_table_test, hash_game_test, pv_first_move_diagnostic
make perft        # movegen vs known perft counts (PERFT_DEPTH=4 for a quick check)
make bench        # fixed-depth search; prints "bench: nodes N ..." (the search signature)
make speed-compare A=main B=.   # nps A/B of two git refs, 95% CI ("." = working tree)
make tt-stats DEPTH=5 GAMES=4   # TT discards later re-requested, by depth (only build with -DTT_STATS)
make gaviota-test # Gaviota DTM probe vs Lichess and vs its children (#77, needs ~/gaviota)
make mate-suite   # 430 Lichess mate puzzles at 1 s each: hit rate per N, exact N (#78)
make ptt-seed     # time to stored depth + 1, seeded with the stored line vs cold (#79, ~10 min)
make walkback     # Analyse along the 3.Bc2+ study, walks off/on/fwd/mixed, ~40 s each: when each step shows its mate (#100)
make gui          # build + run the browser GUI server (GUI_PORT=8173 to pick the port)
make gui NARROW=1 # the same, driving ./ascaniusfish_uci_narrow (-DTT_BOUNDS_NEVER_NARROW=0, #65)
make nne-retrain  # after an eval() change: relabel the NNE dataset, retrain, check (docs/NNE_RELABEL.md; FROM=<branch> takes a cloud session's labels)
make rebuild      # clean + all
make clean        # remove build artifacts
```

Run a single test binary directly, e.g. `./hash_table_test` or `./hash_game_test` (built via `make tests`). There is no test framework/runner — `hash_table_test.cpp`, `hash_game_test.cpp`, and `pv_first_move_diagnostic.cpp` stay at the repo root (these are the `make tests` targets) and are each a standalone `main()` used as an ad-hoc check. One-off probes/diagnostics live in `diagnostics/`, and performance benchmarks live in `benchmarks/`; build any of them (root, `diagnostics/`, or `benchmarks/`) the same way, from the repo root so relative includes resolve, e.g.:
```bash
g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/<name> diagnostics/<name>.cpp
```

Every tool that touches movegen (a probe, a benchmark, a server) must run `Zobrist`/`initialize_rand()`/`init_magics()`/`init_sliders_attacks()` first in `main()`: without them sliding attacks are garbage and `in_check()` silently misses checks instead of crashing.

`tools/` holds the benchmarking tools behind `make perft`/`bench`/`speed-compare` (see `docs/BENCHMARKS.md`), and `issue_worktree.sh` (one worktree per issue, see `docs/WORKFLOW.md`).
`gui/` holds the browser GUI server behind `make gui` (see `docs/GUI.md`).

Recorded outputs of long measurement runs (e.g. `make tt-stats` games) live in `docs/measurements/`, each with a header naming the commit and command. Check there before rerunning one.

Profiling: `make profile-startpos` (builds `benchmarks/profile_startpos` with `-pg`, runs `PROFILE_ITERATIONS`/`PROFILE_DEPTH`-controlled iterations from inside `benchmarks/`, then runs `gprof` into `benchmarks/profile_startpos.gprof`).

`diagnostics/` (`attack_invariants_probe.cpp`, `debug_assign_depth.cpp`, `engine_move_diag.cpp`, `lookup_consistency_test.cpp`, `pawn_shift_probe.cpp`) holds one-off diagnostics written while debugging specific engine behaviors (assigned search depth, zobrist/lookup-table consistency, pawn bit-shift correctness, PV move ordering) — check an existing one for the pattern before writing a new probe. `benchmarks/` (`minimax_speed_test.cpp`, `profile_startpos.cpp`) holds performance-measurement programs. Files moved into these subfolders had their `#include "ascaniusfish.hpp"`-style includes rewritten to `../ascaniusfish.hpp` (relative to the subfolder) — keep that in mind when copying one as a template for a new probe/benchmark placed in the same folder.

## Architecture

This section is the map. Subsystems with a lot of detail keep it in their own doc, read when working on them: `docs/GUI.md` (browser GUI), `docs/UCI.md` (UCI layer and its options), `docs/TABLEBASES.md` (Syzygy, Gaviota), `docs/MATE_SEARCH.md` (mate verifier); also `docs/BENCHMARKS.md`, `docs/NNE_DESIGN.md`, `docs/WORKFLOW.md`. When a change alters what one of them describes, update that doc, not this file; add here only what every session needs.

### Header/source split and include order matters
Everything funnels through two giant umbrella headers that must be included in this order (see `ascaniusfish.cpp`):
1. `ascaniusfish.hpp` — pulls in `lib/Bitboards.hpp`, `lib/Bitboard_initialisations.hpp`, `Settings`, `Weights`, `templates`, `timers`, `printing`, `checks`, `python_communication`, `lookup_table`, `move_generation`, `basic_eval`, `saefty_checks`, `eval` (via their `src/*.cpp`). Defines `BB` history globals, `CBE` (cached-eval board wrapper, currently unused by the main search), depth-assignment heuristics (`assign_depth`), PGN/UCI move-string formatting (`get_move`, `get_PGN`).
2. `ascaniusfish_2.hpp` — the actual game/search driver: `minimax` (alpha-beta with TT probing/storing and move ordering via `sorting_moves`), `PP` (play parameters struct: depth, colors, weights, python-pipe config, opening-book/game-count settings), `Play` (the class whose `nicely_written_play()` runs a full game loop, printing/piping board state to `display_board.py` when configured), and `PRESENT`/self-play tournament machinery for weight tuning.
3. `main()` in `ascaniusfish.cpp` wires a `PP` struct, optionally loads an opening book into `lookup_table`, then calls `Play::nicely_written_play()`.

When editing engine internals, `lib/*.hpp` is the declaration/interface layer and `src/*.cpp` is the implementation — always check both; the `.hpp` files also carry `#include` chains that make the single-TU build work, so don't casually reorder or remove includes.

### Core data model
- `BB` (`lib/Bitboards.hpp` / `src/Bitboards.cpp`) is the board representation: `uint64_t Board[12]` (one bitboard per piece type × color, white pieces at indices 0-5, black at 6-11), castling rights, en-passant square, Zobrist hash, move counters, repetition count. Most engine functions take `const BB* const`.
- `Move` / `PV_Line` (`lib/move_generation.hpp`) — `PV_Line` carries a principal-variation move array (`MAX_PV_Lenght`, `lib/Settings.hpp`), eval, search depth, and `bound_type` (0=exact, -1=lower, 1=upper, used for TT alpha/beta cutoffs).
- `WEIGHTS` (`lib/Weights.hpp`) holds all tunable eval parameters (piece values, piece-square tables for opening/endgame, king safety, pawn structure penalties) with file I/O (`read_values_from_file`/`print_values_to_file`) used by the self-play tuning loop in `ascaniusfish_2.hpp`. `WEIGHTS_OG` is the default/baseline instance. Since #85 every number `basic_eval()` uses is a `WEIGHTS` field, and the fields it uses form a **weight set**: a versioned text file (`lib/weight_set.hpp` has the format, `read_weight_set()`/`write_weight_set()`). Set 1 is `weights/w1.txt`; the default set (set 8, `weights/w8.txt`, since #102) is compiled in from `lib/weights_default.hpp`, which the Makefile generates from the file named by `WEIGHTS_DEFAULT` (committed, so a plain g++ line builds), and the `WEIGHTS()` constructor reads it, so a new default is a new `weights/wN.txt`, not an edit of `src/Weights.cpp`. Piece-square tables are white's view and black reads them rank-flipped (`sq^56`). `diagnostics/weight_set_test.cpp` checks the file round trip, the generated header, error handling and colour symmetry with scrambled tables. A new eval number goes into `WEIGHTS`, the set's field list (`weight_fields()` in `src/weight_set.cpp`) and the current `weights/wN.txt`.

### Search
`minimax()` in `ascaniusfish_2.hpp` is alpha-beta with:
- Transposition table probing/storing via `lookup_table` (`lib/lookup_table.hpp`), keyed by `Zobrist::compute_Zobrist_Hash` (`lib/zobrist.hpp`) truncated to `exponent_for_size` bits, with bucketed replacement (`find_victim_index`) and bound-type-aware alpha/beta tightening. For one position `insert()` keeps the deeper entry, except that a proof (`tt_proven()`: an exact mate or table score, or a mate claim, i.e. a bound that the mating side does at least that well; never a "no faster mate" bound) replaces an unproven entry however deep and only a deeper proof replaces it (#100's Patch A); a probe cuts on a proof at any depth.
- Move ordering via `sorting_moves()`, which scores moves with `sorting_eval` and promotes the PV move from `PV_Line` to the front.
- Dynamic per-move depth allocation via `assign_depth()` (`ascaniusfish.hpp`) — captures of more valuable pieces get `INT_MAX` (searched at full remaining depth), other moves get a fraction of `free_depth` weighted by `tactical_potential`.

### Evaluation
`eval()` (`lib/eval.hpp` / `src/eval.cpp`) composes `basic_eval` (material + piece-square tables) with king safety, pawn structure penalties, and mobility, all parameterized by a `WEIGHTS` instance so both sides can play with different weights (used in self-play tuning). `exception_eval()` detects checkmate/stalemate. Mate scores are encoded near `INT_MIN`/`INT_MAX` (see `interpret_eval()` in `ascaniusfish.hpp` for the encoding: distance-to-mate is `eval - INT_MIN` or `INT_MAX - eval`).

**When you change the eval, change `gui/eval_split.hpp` with it.** `basic_eval()` stays one undivided function for the search; the GUI's "Advanced debugging" breakdown (#66) is a *copy* of its arithmetic, term by term and part by part, and `src/plan_eval.cpp` (the dreamer) keeps its own incremental copy of `piece_activity_eval()`'s role terms. `eval_self_check()` compares every rebuilt term with the function `basic_eval()` calls for it, and their sum with `basic_eval()`, on 30 positions (~1-2 ms). The page runs it whenever a switch goes on. Run it after an eval change with `diagnostics/eval_split_test.cpp` (exit code 1 on a mismatch, naming the term). The dreamer (`lib/plan_eval.hpp`, the #43-#47 plan term) is GUI-only on `main`: nothing in the engine includes it.

### Opening book
`lib/opening_book.hpp` / `src/opening_book.cpp` supports loading positions into the `lookup_table` from either PGN (`load_opening_book_from_pgn`) or Lichess JSON-lines evaluation dumps (`load_opening_book_from_lichess_json`), plus a full-book dedup+binary-cache path (`load_and_save_full_opening_book`/`load_opening_book_from_binary`) for the multi-GB Lichess eval database. All toggles live as `const bool`/`const std::string` constants at the top of `ascaniusfish.cpp` (`USE_OPENING_BOOK`, `USE_LICHESS_JSON`, `LOAD_FULL_OPENING_BOOK`, paths under `books/`). See `OPENING_BOOK_README.md`, `LICHESS_JSON_SUPPORT.md`, and `FULL_BOOK_LOADING.md` for format details and setup steps — these are living docs for that subsystem, keep them in sync with `src/opening_book.cpp` if you change the loaders.

### UCI
`./ascaniusfish_uci` (entry `ascaniusfish_uci.cpp`, protocol in `lib/uci.hpp` / `src/uci.cpp`) speaks UCI on stdin/stdout; the interactive play mode stays in `./ascaniusfish`. The search runs on its own thread and is aborted through `lib/search_control.hpp` (`poll_search_abort()` throws `search_aborted`); clocks go through `lib/time_manager.hpp`. On top of plain UCI it has tablebases at the root and in the search, MultiPV, best-move-only TT entries with `TTWalk`, the non-standard `go perft`, `hint` and `route` commands, the analysis path refresh, `WeightsFile` and a provenance line, and the TT narrowing switches. Several of these are Ascanius-owned patches to `minimax()`. **Read `docs/UCI.md` before working on any of it.**

### Browser GUI
`./gui/ascaniusfish_gui` (`make gui`, entry `gui/gui_server.cpp`) serves a chess board on `http://localhost:8173`. It is a **separate executable**, a UCI client of `./ascaniusfish_uci` (it does not use `./ascaniusfish` or `display_board.py`), on its own small HTTP/SSE server with chessground in `gui/web/`. The server owns the position and the page owns nothing: all chess logic stays in C++, the JS is presentation only. Modes are Analyse, Play, Watch and Correspondence. **Read `docs/GUI.md` before working in `gui/`**: it covers each file (`session.hpp`, `move_tree.hpp`, `engine_link.hpp`, `ptt.hpp`, `analysis_store.hpp`, `eval_split.hpp`, `tablebase_view.hpp`, the web page), every `/api/` route, and the layout rules (the page never scrolls).

### Tablebases
`lib/syzygy.hpp` is our own Syzygy prober (3-5 pieces, WDL and DTZ; tables in `~/syzygy-nr`, `make syzygy-test`). Gaviota DTM (`~/gaviota`, #77) goes through `lib/gaviota.hpp` to the third-party C prober in `third_party/gaviota` (MIT, vendored as published: no ownership markers, never edited here), linked only into the UCI engines and `diagnostics/gaviota_test` via `$(WITH_GTB)`; without `-DWITH_GAVIOTA` it is stubs, so the plain g++ line still builds every other tool. Details, numbers and tests: `docs/TABLEBASES.md`.

### Mate search (#78)
`lib/mate_search.hpp` / `src/mate_search.cpp`: `minimax_checkmate_only()`, a separate search that only proves mates (no eval, no pruning of the defender), used as a **verifier only** by `UCI_Engine::verify_mate()` for every mate line 1 claims. `make mate-suite` measures it. Details: `docs/MATE_SEARCH.md`.

### Python GUI bridge
`lib/python_communication.hpp` / `src/python_communication.cpp` opens `display_board.py` as a subprocess via `popen` (piping UCI move strings to its stdin) so the C++ engine can drive a tkinter/pygame board with sound effects. Separately, `read_from_last_move()` (`ascaniusfish_2.hpp`) and `display_board.py`'s `write_to_last_move_file()` coordinate human-vs-engine play through the shared file `last_move.txt`, polling every 200ms; the color suffix (`ww`/`bb`) written after the move string is a same-color echo used to signal "no new move yet". Treat `last_move.txt` as ephemeral IPC state, not data to commit meaningfully.

### The net (NNE)
`lib/nne.hpp` / `src/nne.cpp` is the eval-correction net (`nets/nne_d6.bin`, UCI `UseNNE`/`NNEFile`; design in `docs/NNE_DESIGN.md`). Ascanius may call it "the net", "NNE" or "NNUE"; all mean this one.

**Retraining it.** When Ascanius asks to retrain or relabel the net, "locally" or "in the cloud", read `docs/NNE_RELABEL.md` and tell Ascanius the steps for that route; don't start the run yourself. Locally it is `make nne-retrain` (~1.5–2 h, needs `data/nne/` and `tools/nne_train` built once, alone). In the cloud Ascanius pastes the prompt from that doc into a Claude cloud session, and afterwards runs `make nne-retrain FROM=<the session's branch>` here.

### Settings
`lib/Settings.hpp` are compile-time `constexpr bool`/`int` toggles (print verbosity, per-category timing display, `DEBUG_MODE` for consistency checks like `saefty_checks`, `MAX_PV_Lenght`, `max_mating_seq`). Flip these instead of adding new runtime flags for engine-internal debugging output.

#### Narrowing on stored bounds (#65)
By default (`TT_BOUNDS_NEVER_NARROW` = 1) a stored bound only cuts in `minimax()`, it never narrows alpha and beta. The narrowing builds and the `TTNarrowing`/`TTNarrowingDeeper`/`TTDeeperCuts` options are in `docs/UCI.md`.

#### Transposition table size: do not raise it
`TT_EXPONENT` (default 18) and `TT_BUCKET_SIZE` (8) in `lib/Settings.hpp` give the
regular TT `2^18 * 8 = 2097152` entries of 32 bytes, 64MB. The exponent is both the array dimension and the
hash mask width (`lookup_table_base::get_hash`), so it can only be a power of two.

**Do not increase these just because the memory budget allows it.** When the
best-move-only entries (#82) shrank `TT_entry` from ~184 to 32 bytes, Ascanius raised every
exponent by 3 (15 → 18, ~46MB → 64MB) to spend the room gained; that was their call, and the
next raise is theirs too. The small
footprint is deliberate: it is what lets several engine processes run at once,
which is the thing being optimized for here. Debugging often means a binary and a
probe/benchmark side by side, and two agents working in this repo simultaneously
may each want to play a game. A bigger table trades that away for a marginal
search gain, so raising it is a decision for Ascanius, not a free win an agent
should take on its own. Raise it only if Ascanius explicitly asks.

If you need a *smaller* table, that is fine and already supported: build with
`-DTT_EXPONENT=n` (`tools/match` does this, defaulting to `tt=17`, 32MB, so 6 parallel games
(12 engines) fit in RAM with a cushion; `tools/tempo_swing` likewise, `tools/nne_data` uses
`NNE_TT=16`, and `make tt-stats TT_EXPONENT=n`).
