# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

"Ascaniusfish" is a from-scratch bitboard chess engine in C++ (no external chess libraries). It does full legal move generation, alpha-beta minimax search with a transposition table, a hand-written positional/material evaluator, Zobrist hashing, opening-book loading, and an optional Python/tkinter GUI (`display_board.py`) driven over a pipe/file handshake. There's also a self-play weight-tuning loop.

## File ownership rules

Every file in this repo declares ownership via a comment at the very top of the file, containing `OWNERSHIP=Ascanius` or `OWNERSHIP=Claude`, written with that file's own comment syntax (`// OWNERSHIP=Claude` in C++ `.cpp`/`.hpp` files — a literal leading `#` there is a preprocessor directive and fails to compile; `# OWNERSHIP=Claude` in the Makefile, Python, and shell files).

- **Never edit a file owned by Ascanius** (`#OWNERSHIP=Ascanius`), even when running with auto-edit / "always allow edits" privileges. This holds regardless of permission mode — auto-accept settings do not override this rule.
- Files owned by Claude (`#OWNERSHIP=Claude`) may be edited freely.
- A file with no ownership comment has no declared owner — ask the user before editing it rather than assuming either ownership.
- If a task requires changing an Ascanius-owned file, stop and propose the change to the user instead of editing it directly.
- Whenever Claude creates a new file, mark it as Claude-owned by adding `#OWNERSHIP=Claude` as a comment at the very top of that file.

## Build & run

Build system is a plain `Makefile` (`CXX=g++`, `-O3 -DNDEBUG` by default). This is a **header-driven single-TU build**: `ascaniusfish.cpp` is the only compiled source given to g++; every `lib/*.hpp` `#include`s its matching `src/*.cpp` directly (see e.g. `lib/Bitboards.hpp` including `../src/bit_operations.cpp`), so the whole engine is compiled as one translation unit. There is no separate object-file linking step — don't try to compile `src/*.cpp` files independently.

```bash
make              # builds ./ascaniusfish (equivalent: make all)
make run          # build + run (alias: make play)
make debug        # build with -O0 -g (no optimizations, for debugging)
make asm          # emit ascaniusfish.s (assembly output)
make tests        # builds hash_table_test, hash_game_test, pv_first_move_diagnostic
make rebuild      # clean + all
make clean        # remove build artifacts
```

Run a single test binary directly, e.g. `./hash_table_test` or `./hash_game_test` (built via `make tests`). There is no test framework/runner — `hash_table_test.cpp`, `hash_game_test.cpp`, and `pv_first_move_diagnostic.cpp` stay at the repo root (these are the `make tests` targets) and are each a standalone `main()` used as an ad-hoc check. One-off probes/diagnostics live in `diagnostics/`, and performance benchmarks live in `benchmarks/`; build any of them (root, `diagnostics/`, or `benchmarks/`) the same way, from the repo root so relative includes resolve, e.g.:
```bash
g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/<name> diagnostics/<name>.cpp
```

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

### Python GUI bridge
`lib/python_communication.hpp` / `src/python_communication.cpp` opens `display_board.py` as a subprocess via `popen` (piping UCI move strings to its stdin) so the C++ engine can drive a tkinter/pygame board with sound effects. Separately, `read_from_last_move()` (`ascaniusfish_2.hpp`) and `display_board.py`'s `write_to_last_move_file()` coordinate human-vs-engine play through the shared file `last_move.txt`, polling every 200ms; the color suffix (`ww`/`bb`) written after the move string is a same-color echo used to signal "no new move yet". Treat `last_move.txt` as ephemeral IPC state, not data to commit meaningfully.

### NNUE
`lib/NNUE.hpp` / `src/NNUE.cpp` exist but are **not wired into the build** — `lib/NNUE.hpp` includes a network implementation via a hardcoded path outside this repo (`../../../../Documents/Semester_7/ML/Semesterprojekt_2/...`) and nothing else in the codebase includes `NNUE.hpp`. Don't assume it compiles or is on the active eval path.

### Settings
`lib/Settings.hpp` are compile-time `constexpr bool`/`int` toggles (print verbosity, per-category timing display, `DEBUG_MODE` for consistency checks like `saefty_checks`, `MAX_PV_Lenght`, `max_mating_seq`). Flip these instead of adding new runtime flags for engine-internal debugging output.
