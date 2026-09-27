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

## Build & run

Build system is a plain `Makefile` (`CXX=g++`, `-O3 -DNDEBUG` by default). This is a **header-driven single-TU build**: `ascaniusfish.cpp` is the only compiled source given to g++; every `lib/*.hpp` `#include`s its matching `src/*.cpp` directly (see e.g. `lib/Bitboards.hpp` including `../src/bit_operations.cpp`), so the whole engine is compiled as one translation unit. There is no separate object-file linking step — don't try to compile `src/*.cpp` files independently.

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
make gui          # build + run the browser GUI server (GUI_PORT=8173 to pick the port)
make rebuild      # clean + all
make clean        # remove build artifacts
```

Run a single test binary directly, e.g. `./hash_table_test` or `./hash_game_test` (built via `make tests`). There is no test framework/runner — `hash_table_test.cpp`, `hash_game_test.cpp`, and `pv_first_move_diagnostic.cpp` stay at the repo root (these are the `make tests` targets) and are each a standalone `main()` used as an ad-hoc check. One-off probes/diagnostics live in `diagnostics/`, and performance benchmarks live in `benchmarks/`; build any of them (root, `diagnostics/`, or `benchmarks/`) the same way, from the repo root so relative includes resolve, e.g.:
```bash
g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/<name> diagnostics/<name>.cpp
```

`tools/` holds the benchmarking tools behind `make perft`/`bench`/`speed-compare` (see `docs/BENCHMARKS.md`).
`gui/` holds the browser GUI server behind `make gui` (see **Browser GUI** below).

Recorded outputs of long measurement runs (e.g. `make tt-stats` games) live in `docs/measurements/`, each with a header naming the commit and command. Check there before rerunning one.

Profiling: `make profile-startpos` (builds `benchmarks/profile_startpos` with `-pg`, runs `PROFILE_ITERATIONS`/`PROFILE_DEPTH`-controlled iterations from inside `benchmarks/`, then runs `gprof` into `benchmarks/profile_startpos.gprof`).

`diagnostics/` (`attack_invariants_probe.cpp`, `debug_assign_depth.cpp`, `engine_move_diag.cpp`, `lookup_consistency_test.cpp`, `pawn_shift_probe.cpp`) holds one-off diagnostics written while debugging specific engine behaviors (assigned search depth, zobrist/lookup-table consistency, pawn bit-shift correctness, PV move ordering) — check an existing one for the pattern before writing a new probe. `benchmarks/` (`minimax_speed_test.cpp`, `profile_startpos.cpp`) holds performance-measurement programs. Files moved into these subfolders had their `#include "ascaniusfish.hpp"`-style includes rewritten to `../ascaniusfish.hpp` (relative to the subfolder) — keep that in mind when copying one as a template for a new probe/benchmark placed in the same folder.

## Architecture

### Header/source split and include order matters
Everything funnels through two giant umbrella headers that must be included in this order (see `ascaniusfish.cpp`):
1. `ascaniusfish.hpp` — pulls in `lib/Bitboards.hpp`, `lib/Bitboard_initialisations.hpp`, `Settings`, `Weights`, `templates`, `timers`, `printing`, `checks`, `python_communication`, `lookup_table`, `move_generation`, `basic_eval`, `saefty_checks`, `eval` (via their `src/*.cpp`). Defines `BB` history globals, `CBE` (cached-eval board wrapper, currently unused by the main search), depth-assignment heuristics (`assign_depth`), PGN/UCI move-string formatting (`get_move`, `get_PGN`).
2. `ascaniusfish_2.hpp` — the actual game/search driver: `minimax` (alpha-beta with TT probing/storing and move ordering via `sorting_moves`), `PP` (play parameters struct: depth, colors, weights, python-pipe config, opening-book/game-count settings), `Play` (the class whose `nicely_written_play()` runs a full game loop, printing/piping board state to `display_board.py` when configured), and `PRESENT`/self-play tournament machinery for weight tuning.
3. `main()` in `ascaniusfish.cpp` wires a `PP` struct, optionally loads an opening book into `lookup_table`, then calls `Play::nicely_written_play()`.

When editing engine internals, `lib/*.hpp` is the declaration/interface layer and `src/*.cpp` is the implementation — always check both; the `.hpp` files also carry `#include` chains that make the single-TU build work, so don't casually reorder or remove includes.

### Core data model
- `BB` (`lib/Bitboards.hpp` / `src/Bitboards.cpp`) is the board representation: `uint64_t Board[12]` (one bitboard per piece type × color, white pieces at indices 0-5, black at 6-11), castling rights, en-passant square, Zobrist hash, move counters, repetition count. Most engine functions take `const BB* const`.
- `Move` / `PV_Line` (`lib/move_generation.hpp`) — `PV_Line` carries a principal-variation move array (`MAX_PV_Lenght`, `lib/Settings.hpp`), eval, search depth, and `bound_type` (0=exact, -1=lower, 1=upper, used for TT alpha/beta cutoffs).
- `WEIGHTS` (`lib/Weights.hpp`) holds all tunable eval parameters (piece values, piece-square tables for opening/endgame, king safety, pawn structure penalties) with file I/O (`read_values_from_file`/`print_values_to_file`) used by the self-play tuning loop in `ascaniusfish_2.hpp`. `WEIGHTS_OG` is the default/baseline instance.

### Search
`minimax()` in `ascaniusfish_2.hpp` is alpha-beta with:
- Transposition table probing/storing via `lookup_table` (`lib/lookup_table.hpp`), keyed by `Zobrist::compute_Zobrist_Hash` (`lib/zobrist.hpp`) truncated to `exponent_for_size` bits, with bucketed replacement (`find_victim_index`) and bound-type-aware alpha/beta tightening.
- Move ordering via `sorting_moves()`, which scores moves with `sorting_eval` and promotes the PV move from `PV_Line` to the front.
- Dynamic per-move depth allocation via `assign_depth()` (`ascaniusfish.hpp`) — captures of more valuable pieces get `INT_MAX` (searched at full remaining depth), other moves get a fraction of `free_depth` weighted by `tactical_potential`.

### Evaluation
`eval()` (`lib/eval.hpp` / `src/eval.cpp`) composes `basic_eval` (material + piece-square tables) with king safety, pawn structure penalties, and mobility, all parameterized by a `WEIGHTS` instance so both sides can play with different weights (used in self-play tuning). `exception_eval()` detects checkmate/stalemate. Mate scores are encoded near `INT_MIN`/`INT_MAX` (see `interpret_eval()` in `ascaniusfish.hpp` for the encoding: distance-to-mate is `eval - INT_MIN` or `INT_MAX - eval`).

### Opening book
`lib/opening_book.hpp` / `src/opening_book.cpp` supports loading positions into the `lookup_table` from either PGN (`load_opening_book_from_pgn`) or Lichess JSON-lines evaluation dumps (`load_opening_book_from_lichess_json`), plus a full-book dedup+binary-cache path (`load_and_save_full_opening_book`/`load_opening_book_from_binary`) for the multi-GB Lichess eval database. All toggles live as `const bool`/`const std::string` constants at the top of `ascaniusfish.cpp` (`USE_OPENING_BOOK`, `USE_LICHESS_JSON`, `LOAD_FULL_OPENING_BOOK`, paths under `books/`). See `OPENING_BOOK_README.md`, `LICHESS_JSON_SUPPORT.md`, and `FULL_BOOK_LOADING.md` for format details and setup steps — these are living docs for that subsystem, keep them in sync with `src/opening_book.cpp` if you change the loaders.

### UCI
`./ascaniusfish_uci` (entry `ascaniusfish_uci.cpp`, protocol in `lib/uci.hpp` / `src/uci.cpp`) speaks UCI on stdin/stdout; the interactive play mode stays in `./ascaniusfish`. The search runs on its own thread; `lib/search_control.hpp` holds the stop flag/deadline that `minimax()`/`minimax_tactical()` poll via `poll_search_abort()`, which throws `search_aborted` to unwind an aborted search (the last completed iteration's move is played). On a clock (`go wtime … btime …`), `lib/time_manager.hpp` / `src/time_manager.cpp` set the hard limit (the abort deadline) and, after every iteration, a soft limit y + λ·u that decides whether the next iteration starts (λ from PV stability across iterations, see the header). Non-standard `go perft N` prints a perft divide for the current position — handy for checking FEN loading/movegen.

### Browser GUI
`./gui/ascaniusfish_gui` (`make gui`, entry `gui/gui_server.cpp`) serves a chess board on
`http://localhost:8173` and is a **separate executable** — it does not use `./ascaniusfish`,
`nicely_written_play()` or `display_board.py`. Options are `key=value`: `port=`, `root=`, `fen=`.

- `gui/http_server.hpp` — the slice of HTTP this needs on plain sockets (no third-party library):
  GET static files, POST JSON, and SSE streams held open across one `poll()` loop, so the server
  can push a new position to every page watching a session. Binds `127.0.0.1` only.
- `gui/move_tree.hpp` — the game as a tree with a cursor, and PGN both ways. The linear `Game`
  in `tools/game_rules.hpp` stays as it is (`tools/match.cpp` and `tools/gui_match.cpp` use it);
  this is the shape a game being *looked at* needs. A `Tree_Node` holds the move, its SAN, the
  position after it, the repetition key, the halfmove clock along *this path*, a `Move_Note`
  eval slot and a PGN comment; `children[0]` is the main line and the rest are side lines.
  Node ids index one pool and are **never reused** — a deleted node stays in it marked dead, so
  an id the browser drew before a deletion is refused rather than meaning another move. `Nav`
  is the cursor's six moves (back/forward/start/end, and prev/next between siblings).
  `load_pgn()` reads a move by naming every legal move with the same `san()` the exporter uses
  and comparing, so import and export cannot drift apart; it handles nested variations, NAGs,
  `{}`/`;` comments and a `FEN`/`SetUp` tag. An engine move's `Move_Note` is exported as the
  comment `{+0.35/7 10.00s}`, the same shape `tools/gui_match.cpp` writes, so a downloaded game
  keeps what the searches found; a comment read back is collapsed to one line, or the newline
  the exporter's wrapping put inside it would re-wrap the next export differently. `diagnostics/move_tree_pgn_test.cpp` covers all of
  it, including a Lichess-shaped study PGN.
- `gui/session.hpp` — a `Session` is one game (a `Move_Tree`), a `Mode` and a board orientation;
  `Sessions` maps ids to them, so two browser tabs can hold two independent games.
  **"The position" is the cursor, not the end of the game**: legal moves, FEN, the result, the
  repetition count and the moves handed to the engine all follow it, which is what makes the
  analysis search follow it too. `state_json()` is the one thing the page renders from: FEN,
  `dests` per square, `promotions`, legal moves with SAN, the whole tree flat (every live node
  with its parent, children, SAN, move number and engine note) plus `cursor` and the
  `can{Back,Forward,Prev,Next,Promote}` flags, the game as PGN, `Outcome`, and the Play, Watch
  and Analysis panels' state. Play adds the resolved human colour, the `Go_Limits`,
  a resignation and whether a search is running; Watch adds a `Go_Limits` per side, whether the
  self-play game is running or stepping, and which side is thinking; Analyse adds the toggle and
  the running search's depth/nodes/nps, its score **in white's view** (`uci_score()` is the
  mover's, so `write_analysis()` negates it for black) and its PV in both UCI and SAN. A session
  runs at most one search — in Watch mode the two sides think in turn, never together — and
  `Search_Kind` says how to read its answer: a `PLAY` or `WATCH` search ends in a move on the
  board, an `ANALYSIS` search only in a line to look at. Every position change
  calls `clear_analysis()` *before* the page is told, so no frame can carry the previous
  position's eval; that call also files what the analysis reached in the
  `Analysis_Store` and puts the new position's kept result up, so `analysis` is either the
  live search's or a kept one and never nothing that has been looked at. `analysis_stored`
  says which, and `analysis_live_depth` how far a search still behind a deeper kept result
  has got (both go to the page as `stored`/`liveDepth`). `game_serial()` counts the games the session has held — it changes on a
  reset, a FEN or a loaded PGN and never on a move — which is what decides whether an engine
  needs a `ucinewgame` before it is asked anything.
- `gui/analysis_store.hpp` — what the analysis has already found in this game, so coming back
  to a position shows its eval, depth and line at once instead of an empty bar (#24). Fixed
  1024 slots, direct-mapped and **keyed by the position** (`Position_Key`, not a node id), so a
  transposition or the same position down a side line is the same entry and an entry can only
  ever be handed back for the position it was made in. Deeper wins for one position, and any
  other position simply takes the slot; a new game, a FEN or a loaded PGN clears the lot
  (`Session::forget_analysis()`). Memory only. `diagnostics/analysis_store_test.cpp` covers it.
- `gui/engine_link.hpp` — the UCI client of Play, Watch and Analyse mode: a `Go_Limits` (depth,
  movetime, or `Go_Limits::analysis()` = `go infinite` for Analyse;
  `wtime`/`btime`/`winc`/`binc` fields already there for M4), a `Search_Request`, and an
  `Engine_Link` that drives one `./ascaniusfish_uci` (`tools/uci_engine.hpp`) on a worker
  thread. The worker touches no `Session` and no socket: it calls `on_update()`, which only
  does `Http_Server::wake()` (a self-pipe), and the poll loop's `on_tick` picks the answer up.
  So a search never blocks a request — page loads, `flip`, `undo` and mode switches are all
  answered while the engine thinks, and every iteration's depth/score is pushed over SSE.
  `gui_server.cpp` keeps up to **three** links per session (`Engine_Set`): one for Play and
  Analyse, one per side for Watch, each started on the first search it is asked for. A link the
  session's mode cannot use is let go on the next tick, and so is every link of a session no
  page has been watching for 5 s — the self-play game is paused and the analysis toggle goes
  off first. Each process holds ~140 MB of tables, so leaving an idle pair around would eat the
  room this repo keeps for running several engines at once.
- `gui/json.hpp` — a JSON writer that inserts the commas, plus a flat-object parser for request bodies.
- `gui/web/` — `index.html`/`app.js`/`style.css` are ours; `gui/web/vendor/` holds chessground
  (GPL-3, see its `README.md`), upstream's own prebuilt ESM bundle plus CSS with the board and
  all piece images as `data:` URIs. Nothing is fetched from the network and no Node is involved.
  **The page is exactly the window and never scrolls**: the board must not move when a move is
  played, so `body` is `overflow: hidden`, the side column and the move list are each their own
  scroll box, and everything whose text grows while the engine thinks (the status line, the
  engine line, the analysis stats, the best line) is held to a fixed height. `fitBoard()` sizes
  the board from the width the row gives it *and* the height the column has left — nothing can
  be scrolled to, so anything that does not fit is simply gone. Don't use `scrollIntoView()`
  here: it scrolls every scrollable ancestor, the document among them (#22).

**The server owns the position and the page owns nothing** — reloading the browser is just another
`GET /api/state`. All chess logic stays in C++; the JS is presentation only. Routes:
`GET /api/state`, `GET /api/events` (SSE),
`POST /api/{move,fen,reset,undo,resign,play,watch,mode,flip,analyse,line}`, all taking `id`
(default `main`). `POST /api/play` carries the Play settings (`side` = white/black/random,
`kind` = depth/movetime, `value`); applying them starts a new game, since a colour cannot change
mid-game. `POST /api/watch` carries both a side's setting (`side` = white/black plus
`kind`/`value`, which applies from the next move and does *not* stop the game) and a run control
(`action` = start/pause/step). Pause is deliberately **not** an abort: the move being thought
about is finished and played, which is what "after the current move" means.
`POST /api/analyse` is the engine on/off toggle; `POST /api/line` walks the board along a
space-separated list of UCI `moves` (all of them or none), which is what clicking a move in the
analysis line does — the page sends the moves it drew rather than an index, so a deeper iteration
arriving between the draw and the click cannot play a different move. Anything that changes the
position under a running search aborts it and drops its `bestmove` by token.

Moving around the tree: `POST /api/nav` (`where` = back/forward/start/end/prev/next, what the
arrow keys and Home/End send), `POST /api/goto` (`node` = a node id, what clicking a move sends),
`POST /api/promote` (the line through the cursor becomes the main line — the position does not
change, so a running analysis is deliberately left alone), `POST /api/delete` (the move at the
cursor and everything after it), `POST /api/pgn` (`pgn` = a game to load) and `GET /api/pgn`
(the game as a file). "Take back" (`/api/undo`) is the one destructive step among these: it
removes the move at the cursor, and in Play mode keeps going until it is your turn again. In
Play mode the engine only answers at the **end of a line**, so stepping back into the game to
look around does not set it thinking; playing a different move there makes a new end, and then
it does answer.

Like every tool that touches movegen, `main()` must run `Zobrist`/`initialize_rand()`/`init_magics()`/
`init_sliders_attacks()` first — without them sliding attacks are garbage and `in_check()` silently
misses checks instead of crashing.

The mode selector shows Analyse / Play / Watch. Analyse is free play, FEN setup and a live
`go infinite` analysis of whatever is on the board — an eval bar beside the board, the score,
depth/nodes/nps and the best line in SAN; Play is a full game against `./ascaniusfish_uci`
(`engine=` picks a different binary); Watch is Ascaniusfish against itself, two processes with a
depth or movetime each, started/paused/stepped from the panel and reviewable in the move tree
afterwards with every move's eval and reached depth. It plays only from the **end of a line**,
like Play, so stepping back into the game pauses it. The analysis toggle starts
**off**: a `go infinite` search holds a core for as long as it runs, and this repo deliberately
leaves room for several engine processes at once. `main()` ignores `SIGPIPE` — an engine that died must be a message on the page,
not the end of the server — and takes `SIGINT`/`SIGTERM` as "leave through `main()`", which
aborts every search and quits the engines rather than orphaning them.

### Python GUI bridge
`lib/python_communication.hpp` / `src/python_communication.cpp` opens `display_board.py` as a subprocess via `popen` (piping UCI move strings to its stdin) so the C++ engine can drive a tkinter/pygame board with sound effects. Separately, `read_from_last_move()` (`ascaniusfish_2.hpp`) and `display_board.py`'s `write_to_last_move_file()` coordinate human-vs-engine play through the shared file `last_move.txt`, polling every 200ms; the color suffix (`ww`/`bb`) written after the move string is a same-color echo used to signal "no new move yet". Treat `last_move.txt` as ephemeral IPC state, not data to commit meaningfully.

### NNUE
`lib/NNUE.hpp` / `src/NNUE.cpp` exist but are **not wired into the build** — `lib/NNUE.hpp` includes a network implementation via a hardcoded path outside this repo (`../../../../Documents/Semester_7/ML/Semesterprojekt_2/...`) and nothing else in the codebase includes `NNUE.hpp`. Don't assume it compiles or is on the active eval path.

### Settings
`lib/Settings.hpp` are compile-time `constexpr bool`/`int` toggles (print verbosity, per-category timing display, `DEBUG_MODE` for consistency checks like `saefty_checks`, `MAX_PV_Lenght`, `max_mating_seq`). Flip these instead of adding new runtime flags for engine-internal debugging output.

#### Transposition table size: do not raise it
`TT_EXPONENT` (default 15) and `TT_BUCKET_SIZE` (8) in `lib/Settings.hpp` give the
regular TT `2^15 * 8 = 262144` entries; `PTT_EXPONENT_FOR_SIZE`/`PTT_BUCKET_SIZE`
(16/8) give the PTT `524288`. The exponent is both the array dimension and the
hash mask width (`lookup_table_base::get_hash`), so it can only be a power of two.

**Do not increase these just because the memory budget allows it.** Since the
`PV_CHUNK` layout shrank `TT_entry` to ~184 bytes, the TT is only ~46MB and the
PTT ~92MB, so it is tempting to raise the exponent "for free" — don't. The small
footprint is deliberate: it is what lets several engine processes run at once,
which is the thing being optimized for here. Debugging often means a binary and a
probe/benchmark side by side, and two agents working in this repo simultaneously
may each want to play a game. A bigger table trades that away for a marginal
search gain, so raising it is a decision for Ascanius, not a free win an agent
should take on its own. Raise it only if Ascanius explicitly asks.

If you need a *smaller* table, that is fine and already supported: build with
`-DTT_EXPONENT=n` (`tools/match` does this, defaulting to `tt=11`, so many match
engines fit in RAM; `make tt-stats TT_EXPONENT=n` likewise).
