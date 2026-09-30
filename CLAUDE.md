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
g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/<name> diagnostics/<name>.cpp
```

`tools/` holds the benchmarking tools behind `make perft`/`bench`/`speed-compare` (see `docs/BENCHMARKS.md`), and `issue_worktree.sh` (one worktree per issue, see `docs/WORKFLOW.md`).
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

The plan term (#43, `lib/plan_eval.hpp` / `src/plan_eval.cpp`, switch `USE_PLAN_EVAL` there) adds, per piece, the best db/(2+N) over the squares it can reach: db is the gain of moving just that piece in piece tables + activity + pawn structure, N the shortest path through empty squares not attacked by enemy pawns. `src/basic_eval.cpp` includes it and adds it in one line, both approved by Ascanius. db is computed incrementally but exactly; `diagnostics/plan_eval_probe.cpp` prints each piece's target and checks it against re-evaluating the board, and colour symmetry.

### Opening book
`lib/opening_book.hpp` / `src/opening_book.cpp` supports loading positions into the `lookup_table` from either PGN (`load_opening_book_from_pgn`) or Lichess JSON-lines evaluation dumps (`load_opening_book_from_lichess_json`), plus a full-book dedup+binary-cache path (`load_and_save_full_opening_book`/`load_opening_book_from_binary`) for the multi-GB Lichess eval database. All toggles live as `const bool`/`const std::string` constants at the top of `ascaniusfish.cpp` (`USE_OPENING_BOOK`, `USE_LICHESS_JSON`, `LOAD_FULL_OPENING_BOOK`, paths under `books/`). See `OPENING_BOOK_README.md`, `LICHESS_JSON_SUPPORT.md`, and `FULL_BOOK_LOADING.md` for format details and setup steps — these are living docs for that subsystem, keep them in sync with `src/opening_book.cpp` if you change the loaders.

### UCI
`./ascaniusfish_uci` (entry `ascaniusfish_uci.cpp`, protocol in `lib/uci.hpp` / `src/uci.cpp`) speaks UCI on stdin/stdout; the interactive play mode stays in `./ascaniusfish`. The search runs on its own thread; `lib/search_control.hpp` holds the stop flag/deadline that `minimax()`/`minimax_tactical()` poll via `poll_search_abort()`, which throws `search_aborted` to unwind an aborted search (the last completed iteration's move is played). On a clock (`go wtime … btime …`), `lib/time_manager.hpp` / `src/time_manager.cpp` set the hard limit (the abort deadline) and, after every iteration, a soft limit y + λ·u that decides whether the next iteration starts (λ from PV stability across iterations, see the header). `setoption name SyzygyPath value <dir>` loads the tablebases (#38): at a root with ≤5 pieces `UCI_Engine::tb_root_filter` (`src/uci.cpp`) keeps only the moves that preserve the tables' result, winning ones ranked by DTZ inside the 50-move rule, and `search_tb_root` searches just those (the root loop is ours, `minimax()` is untouched). `make tb-suite` (`tools/tb_suite`, suite `tools/tb_suite.epd`) plays 38 won and 22 drawn positions against the engine without tables. Inside the search (#39) `lib/tb_search.hpp`'s `tb_probe_score()` gives a node its table result (≤ `SyzygyProbeLimit` pieces, no castling, clock 0; a cursed win or blessed loss is a draw): a win is ±`TB_WIN_SCORE`, a band below the mate scores and independent of ply, shown by `uci_score()` as `cp ±10000`, never `mate`; `SyzygyPath` empty (also via `setoption`) releases the tables, and `info` carries `tbhits`. The probe call in `minimax()` and `make_move()` restarting the halfmove clock are Ascanius-owned patches (`make tb-trade-suite`, `tools/tb_trade_suite`, 60 positions with 6-7 pieces where the right trade decides; numbers in `docs/measurements/tb_trade_suite_2026-09-30.md`). Non-standard `go perft N` prints a perft divide for the current position — handy for checking FEN loading/movegen.

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
  keeps what the searches found; the note also keeps that search's PV (`Move_Note::pv`, capped
  at `MOVE_NOTE_PV`, in UCI **from the position the move was played in**, so `pv[0]` is the move
  itself) — that is what the engine-line box shows when you step back onto the move, and it
  belongs to the move rather than to a position because the `Analysis_Store` is keyed by
  position and a later analysis of the same one replaces it. It is not exported to PGN; a comment read back is collapsed to one line, or the newline
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
  self-play game is running or stepping, and which side is thinking; Analyse adds only the
  toggle, whether a search is running and the engine's error. What a search *found* is not in
  any of the three: `eval` is the one score and line the page draws, in every mode, and
  `eval_view()` picks it (#25). A search running now wins, since it is about the position on the
  board; otherwise the mode says what is being looked at — in Analyse the position, so the best
  result kept for it wins, and in Play and Watch the move you are on, so the search that chose it
  does. `source` is `live`/`stored`/`move`, or `none` for a position nothing has searched, which
  is what makes the bar show nothing rather than 0.00. The score is **in white's view** whichever
  side moved (a bar that flipped with the mover would swing a board width every move), and so is
  every other score on the page, the Play and Watch panels' thinking indicator included. `settings` carries the
  gear's two switches (the eval gauge, the engine-line box); they live in the `Session` like the
  board orientation, so a reload and a second tab agree, and they are never written to a file.
  `diagnostics/eval_view_test.cpp` covers the choosing. A session
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
- `gui/tablebase_view.hpp` — the server links `lib/syzygy.hpp` itself (#40), so a position with ≤ limit
  pieces shows its exact result at once, with the analysis off. `./gui/ascaniusfish_gui syzygy=<dir>`
  (`make gui SYZYGY=<dir>`) opens the tables once for every session (`tablebase_setup()`); without it,
  or with no tables in it, the gear's switch is disabled and `settings.tbReason` says why. Per session
  `tb_on`/`tb_limit` (3–5) live in the `Session` and go to every engine as `setoption SyzygyPath`
  (`<empty>` when off) and `SyzygyProbeLimit`, sent by `Engine_Link` (`Search_Request::options`, only
  what that process does not hold yet) before the next `position`/`go`. `eval_view()` puts the tables
  first: `source` is `tb`, the score `{kind:"tb", value: white's WDL -2..2, dtz}`. In Analyse the state
  also carries `tbMoves` (every legal move, best first, result for the mover; clicking one plays it).
  `diagnostics/eval_view_test.cpp` covers it (needs `~/syzygy-nr` or argv[1]).
- The plan view (#44, only on the plan-eval branches): the gear's `plans` switch makes the state carry
  `plans` (`Session::write_plans()`): the plan term's total, the static eval, and per piece its best
  square with N, db (owner's view), term (white's view), one shortest `path` (`plan_path()` in
  `lib/plan_eval.hpp`) and every square on some shortest path (`via`). The page draws the paths
  as arrows and lists the pieces; hovering a row or clicking a piece shows just that one.
- `gui/json.hpp` — a JSON writer that inserts the commas, plus a flat-object parser for request bodies.
- `gui/web/` — `index.html`/`app.js`/`style.css`/`sound.js` and the `logo.png`/`favicon.png` (scaled
  down from the root's `Ascaniusfish.png`) are ours; `gui/web/vendor/` holds chessground
  (GPL-3, see its `README.md`), upstream's own prebuilt ESM bundle plus CSS with the board and
  all piece images as `data:` URIs. Nothing is fetched from the network and no Node is involved.
  **The page is exactly the window and never scrolls**: the board must not move when a move is
  played, so `body` is `overflow: hidden`, the side column and the move list are each their own
  scroll box, and everything whose text grows while the engine thinks (the status line, the
  engine line, the analysis stats, the best line) is held to a fixed height. `fitBoard()` sizes
  the board from the width the row gives it *and* the height the column has left — nothing can
  be scrolled to, so anything that does not fit is simply gone. Don't use `scrollIntoView()`
  here: it scrolls every scrollable ancestor, the document among them (#22). Above and below the
  board sit two fixed-height `.player-strip`s, one per side: lichess's material diff (counted from
  the FEN, drawn with chessground's own piece images at a small size) and the clock chip.
  `fitBoard()` lines them up with the board rather than the column. Queued premoves are marked by
  chessground's `current-premove` square colour (`highlight.custom`), not by arrows.

**The server owns the position and the page owns nothing** — reloading the browser is just another
`GET /api/state`. All chess logic stays in C++; the JS is presentation only. Routes:
`GET /api/state`, `GET /api/events` (SSE),
`POST /api/{move,fen,reset,undo,resign,play,watch,mode,flip,analyse,line,settings}`, all taking
`id` (default `main`). Both Play and Watch play under a Clock or a fixed depth (#21), starting on a
1+1 clock (`Session::play_base_ms`/`watch_base_ms`), which is also what Custom opens with; a `kind` of
`depth` carries `value` (Watch: per side), and a `kind` of `clock` carries `baseMs`/`incMs` in
ms — presets and Custom base+increment are entirely a page-side concept (`gui/web/app.js`'s
`CLOCK_PRESETS`/`HYPERBULLET_PRESETS`), resolved to a plain base+increment before the request is
sent, so the server only ever sees one shape either way a clock was chosen. `POST /api/play`
carries the Play settings (`side` = white/black/random, plus `kind`/`value`, or `kind=clock` with
`baseMs`/`incMs` and, for an asymmetric Custom clock, `blackBaseMs`/`blackIncMs`); before the
first move that starts a new game (Random's colour is drawn afresh), but once a move is on the
board it is refused with 409 unless the game is **paused**, and then it applies in place and the
game goes on: a side change turns the board, a depth is just the new limit, and a clock gives the
side(s) named that much time again (`Session::reseed_clock()`). `POST /api/watch` carries both a
side's setting (`side` = white/black plus the same `kind` shape) and a run control (`action` =
start/pause/step); unlike Play's one call, a clock's two colours are set with two requests, one
per side, under the same paused-only rule. So both panels' settings picker is hidden once a game
is on and until it is paused (`gui/web/app.js`'s `settingsLocked()`). `POST /api/pause` (`on` =
true/false) is **Play's** pause: the engine's search is dropped (asked for again on Resume), your
moves are refused and the clock stops (`Session::paused`); Watch keeps Start/Pause. The state's top-level
`paused` is `Session::paused_now()`: Play's flag, or a Watch game not running with nothing in flight.
While paused **or over** the Engine panel's toggle works in Play and Watch too
(`Session::analysis_wanted()`, judged on the position, so a resigned or adjudicated game can still
be looked at); Resume/Start turn it off again. `POST /api/adjudicate` (`result` = white/black/draw)
ends a Play or Watch game like a resignation does, with the reason "adjudicated" (`Session::adjudicate()`);
taking a move back undoes it. Pause is deliberately **not** an
abort: the move being thought about is finished and played, which is what "after the current
move" means — and, since a clock counts through Pause for exactly that reason, `GET /api/state`'s
top-level `clock` object (`whiteMs`, `blackMs`, `running`) is live only while something is
actually ticking; the page interpolates it locally between pushes rather than being sent one every
tick (`Session::clock_sync()`/`clock_ticking()`, `gui/session.hpp`). The server itself notices a
flag falling — not just a clocked engine's own time management — on every poll iteration
(`Session::check_flag()`; `gui/http_server.hpp`'s `poll_timeout` shortens while any clock ticks),
ending the game the way a resignation does, with a `TimeControl` PGN tag alongside it.
`POST /api/settings` carries the gear's switches (`evalBar`, `engineLine`, `plans`), each applied only
when the body names it, so one can be flipped without saying anything about the other.
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
`go infinite` analysis of whatever is on the board; Play is a full game against
`./ascaniusfish_uci` (`engine=` picks a different binary); Watch is Ascaniusfish against itself,
two processes with a depth or movetime each, started/paused/stepped from the panel and reviewable
in the move tree afterwards with every move's eval and reached depth. The eval bar beside the
board and the Engine panel (the score, depth/nodes/nps and the best line in SAN) are the same two
things in **all three** modes (#25), drawn from `eval`; the gear in the header opens the settings
panel, whose two switches turn either of them off everywhere at once, and with both off Play and
Watch look exactly as they did before. The Engine panel also holds Analyse's on/off button, so
there it stays whatever the switches say. The line's moves are buttons only in Analyse, where
clicking one walks the board into it — in a game being played that would open a side line. It plays only from the **end of a line**,
like Play, so stepping back into the game pauses it. The analysis toggle starts
**off**: a `go infinite` search holds a core for as long as it runs, and this repo deliberately
leaves room for several engine processes at once. `main()` ignores `SIGPIPE` — an engine that died must be a message on the page,
not the end of the server — and takes `SIGINT`/`SIGTERM` as "leave through `main()`", which
aborts every search and quits the engines rather than orphaning them.

### Syzygy tablebases
`lib/syzygy.hpp` is our own prober (written from the file format, no Fathom) for 3-5 piece positions. API, all in namespace `Syzygy`: `init(dir)` maps every `*.rtbw`/`*.rtbz` file, `probe_wdl(pos, wdl)` gives the 5-valued WDL and `probe_dtz(pos, dtz, &rounded)` the DTZ in plies, both for the side to move and as if the halfmove clock were 0; they return false for castling rights, too many pieces or a missing table. WDL files leave captures as "don't care", so every probe searches the captures first (en passant included, it is not in the files); a DTZ file stores one side to move only, so the other side is a 1-ply search.

The files are `mmap`'d and their headers are parsed **lazily**: `init()` costs ~6 ms and ~90 kB RSS, the first probe of a table ~1 ms, later ones 4 us (WDL) / 14 us (DTZ). An eager background read is possible if wanted. Numbers: `docs/measurements/syzygy_probe_2026-09-30.md`.

The table set is `~/syzygy-nr` (default `SYZYGY_PATH`): the standard `.rtbw` files (symlinked from `~/syzygy`) plus the 3-4-5 **dtz-nr** ("no rounding") `.rtbz` files. The standard DTZ files store some distances in moves, so a probe can be one ply short; the nr files store plies and match all 1039 reference positions exactly. `make syzygy-test` checks the prober against `tools/syzygy_reference.txt` (Lichess-recorded, rebuilt with `tools/syzygy_reference`, needs curl) and against its own children on random positions (`SYZYGY_RANDOM=n` per table).

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
