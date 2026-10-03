<!-- OWNERSHIP=Claude -->
# Benchmarks

Four commands for checking correctness, speed and strength. They build their
tools in `tools/` and don't touch the engine binaries.

| Command | Checks | Time |
|---|---|---|
| `make perft` | move generation against known node counts | ~40–55 s (`PERFT_DEPTH=4`: 1 s) |
| `make bench` | search signature (total nodes) and nps | ~12–17 s |
| `make speed-compare A=<ref> B=<ref>` | whether B is faster than A | ~5 min |
| `make match A=<ref> B=<ref>` | whether B is stronger than A, in Elo | ~27 min (depth 3) |
| `make gui-match A=<bin> B=<bin>` | one watchable game, depth per move | ~20 min (10 s/move) |

## `make perft`

Runs the six standard positions from
[chessprogramming.org/Perft_Results](https://www.chessprogramming.org/Perft_Results)
(startpos, Kiwipete, positions 3–6) to depth 5 or 6 and compares them with the
published counts. Prints time and Mnps per position and exits with code 1 on a mismatch.
Run it after every movegen change. `PERFT_DEPTH=4` caps the depth for a quick check.

For a single position, `./ascaniusfish_uci` also accepts
`position fen … ` and then `go perft N`, which prints the node count per root move.

## `make bench`

Searches 30 fixed positions (openings, tactical middlegames, endgames) with
iterative deepening 1..3, like UCI `go depth 3`. The transposition table is
cleared before each position. The last line is

```
bench: nodes <N> time_ms <T> nps <X>
```

`nodes` is deterministic. It changes only when search behaviour changes
(pruning, ordering, eval, TT), never with machine load. It works as a
signature: a refactor that is meant to be behaviour-neutral must keep it
unchanged. Commits touching search or eval put `bench: <N>` in the body.
`BENCH_DEPTH=n` changes the depth (then the signature is different too).

"Nodes" counts `minimax()` and `minimax_tactical()` calls. Move ordering
evaluates every child with the full evaluator, so this is only a fraction of
the actual evaluation work (see the baseline below).

## `make speed-compare A=<ref> B=<ref>`

Answers "is B faster than A, beyond noise?"

- Any git ref works (`main`, `HEAD~2`, a hash). `.` means the current working
  tree including uncommitted changes, e.g. `make speed-compare A=HEAD B=.`
- Each ref is checked out in a `git worktree` under `/tmp` and built with the
  *current* `tools/bench.cpp`, so both sides run the same workload. Refs older
  than the UCI front end (1a1f4c8) can't build it.
- `ROUNDS` (default 10) bench runs per side, alternating AB, BA, AB, … so
  load and thermal drift cancel out.
- The verdict comes from the per-round ratio B/A (paired), mean ± 95%
  t-interval. "Significant" means the interval excludes 0.
- If the node signatures differ, the search itself changed. nps then compares
  cost per node, not time to depth, so a second verdict follows: the paired
  ratio of time to depth (each run's nodes/nps), which is the one that counts.

Keep the machine otherwise idle while it runs. The runs are single-threaded
and sequential.

## `make match A=<ref> B=<ref>`

Answers "is B stronger than A, and by how much?"

- A and B are git refs (built like in `speed-compare`, `.` = working tree)
  or paths to UCI binaries. A ref is built as `ascaniusfish_uci` with
  `-DTT_EXPONENT=17` (`tt=`) so that many engines fit in memory. With
  best-move-only entries (#82, `TT_entry` 32 B) that is a 32 MB TT instead of the
  default 64 MB; refs on the `PV_CHUNK` layout (~184 B per entry) take ~184 MB. Refs older
  than 55c44ce ignore the flag entirely, and any ref still on the flat
  `Move[MAX_PV_Lenght]` `PV_Line` (~2.2 KB per entry) need far more; pass a
  smaller `tt=` for those. Do not
  raise `TT_EXPONENT` to close that gap; see the sizing note in
  `lib/Settings.hpp`.
- Every position of `tools/openings.epd` is played twice with colours
  swapped (a game pair): 100 openings, 200 games.
- `DEPTH=n` (default 3) fixes the search depth. `TC=10+0.1` plays with a
  clock instead (seconds + increment, sent as `go wtime … btime …`).
  `tools/match` also takes `depthA=`/`depthB=` for different depths per side.
- `CONCURRENCY=n` games run in parallel (default: cores−1, capped by free
  memory). `PAIRS=n` uses only the first n openings.
- `OPTIONS_A=`/`OPTIONS_B=` (`optionsA=`/`optionsB=`) send UCI options to an
  engine as `Name=Value,Name=Value`, again on every restart. With Stockfish,
  `OPTIONS_A=UCI_LimitStrength=true,UCI_Elo=1800` gives an opponent of a known
  rating: Stockfish's `UCI_Elo` is calibrated at 60+0.6 and anchored to CCRL
  40/4, so play `TC=60+0.6` and add B's Elo to it for an estimate of B's rating.
  Pick a level where B scores 25–75%, or the CI is wide.
- The runner applies the rules itself, with the engine's move generator:
  mate, stalemate, threefold repetition, 50-move rule, insufficient material.
  An illegal move, a hung engine or a flag fall loses the game.
- Output is from B's point of view: W/D/L, score, and Elo ± 95% CI. The CI
  comes from the pentanomial distribution of the game-pair results (0, ½, 1,
  1½, 2 points per pair). The two games of a pair share an opening and are not
  independent. Treating them as single games gets the CI wrong (usually too wide,
  because the colour swap cancels most of the opening's bias).
- All games go to `match.pgn` (`pgn=` in `tools/match`), with the engine's
  score and depth as a comment after each move, and with `TC=` also the
  time the move took: `{+0.35/3 1.21s}`.

At a fixed depth the engine is deterministic, so identical engines play
identical games from both colours. Every pair then scores exactly 1 point
and the result is 0 ± 0. A real change makes the games diverge. For a CI that
includes timing noise, use `TC=`.

### Opening suite

`tools/openings.epd` holds 100 positions after 8–16 plies of named
openings from [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings),
spread over ECO A–E. At most one position per variation and three per opening family.
Each one has a Lichess cloud eval of |cp| ≤ 30 at depth ≥ 29 (`ce`, from the side
to move; `acd` = depth). `tools/make_openings.cpp` builds it:
`./tools/make_openings tools/openings.epd 100 a.tsv b.tsv c.tsv d.tsv e.tsv`
(needs curl and network access, ~5 min due to the API's rate limit).

## `make gui-match A=<white> B=<black>`

One game between two UCI binaries at `MOVETIME` ms per move (default 10000),
shown live in `display_board.py` (moves and the mover's PV). Run from the repo
root. The terminal gets a line per move (depth, nodes, nps, time, score) and at
the end each engine's mean/median depth and depth histogram; the game goes to
`gui_match.pgn`. For a profile, pass wrapper scripts as the engines, e.g.

```sh
#!/bin/sh
exec /usr/lib/linux-tools/5.15.0-194-generic/perf record -F 999 --call-graph fp -o a.data -- ./a_uci
```

with the engine built with `-g -fno-omit-frame-pointer`. `quit_wait=` (default
60 s) is how long the driver waits for perf to write its data. Only one game
from one side: it shows depth and profile, not strength (use `make match`).

## Baseline

Recorded 2026-09-25 at b138d31 plus this tooling, g++ -O3, WSL2, 8 cores.

**perft:** all six positions correct.

| position | depth | nodes |
|---|---|---|
| startpos | 6 | 119,060,324 |
| kiwipete | 5 | 193,690,690 |
| pos3 | 6 | 11,030,083 |
| pos4 | 5 | 15,833,292 |
| pos5 | 5 | 89,941,194 |
| pos6 | 5 | 164,075,551 |

593.6M nodes. Speed was 10.8–16.5 Mnps (bulk-counted leaves), depending on host load.

**bench** (depth 3): `bench: nodes 130296`. nps 7.8k–11.1k.
The node count was identical in every run. nps varies by up to ~30% from run
to run on this WSL2 host, which is why a single `make bench` can't show a
speedup. Use `speed-compare` for that.

**Noise** (`make speed-compare A=HEAD B=HEAD`, the same code on both
sides): `+0.40% ± 9.14%` with 10 rounds, correctly not
significant. Most rounds agreed within ±5%, but two rounds had one side ~20%
slow (host hiccups), and those widen the interval. So 10 rounds resolve
differences of roughly ≥10%. For smaller effects use `ROUNDS=30`; the
interval shrinks with √rounds.

Profile of the bench (gprof, depth 3): 130k search nodes, but about 3.1M full
evaluations. `piece_activity_eval`, `king_safety_of_colour`, `attacks_by_col`,
`piecetable` and `sorting_eval` take ~70% of the time. They are called from
`sorting_moves()` for every child of every node. This is the main reason
search nps (~10k) is far below perft speed (~16M/s).

**match** (`make match A=main B=main`, 55c44ce, depth 3, 7 in parallel,
98 MB per engine pair): 200 games in 27 min, `W 77 D 46 L 77`, all 100
pairs 1–1, Elo 0.0 ± 0.0, as expected for a deterministic engine (see above).
Endings: 154 mates, 26 threefold repetitions, 12 fifty-move rule, 4 stalemates,
4 insufficient material.

**match, TC=10+0.1** (`make match A=main B=main TC=10+0.1`, 8e88e4a): a real
clock breaks the determinism above, so this is the CI baseline for noise. 200
games in 694 s. `B vs A: W 84 D 25 L 91`, score 48.2%, pairs
14/11/55/8/12 (0/0.5/1/1.5/2 points), Elo −12.2 ± 37.9 (95% CI
[−50.2, +25.6]) — not significant, as expected for identical engines.
Endings: 175 mates, 8 threefold repetitions, 3 fifty-move rule, 3 stalemates,
11 insufficient material.

**match, depth 3 vs 2** (`./tools/match main main depthB=2`, 8e88e4a): sanity
check that the runner detects a real strength gap. 200 games in 535 s.
`B vs A: W 12 D 14 L 174`, score 9.5%, pairs 74/14/12/0/0, Elo −391.6 ± 71.1
(95% CI [−474.3, −332.1]) — B (depth 2) is significantly weaker, as expected.
Endings: 186 mates, 10 threefold repetitions, 2 fifty-move rule, 1 stalemate,
1 insufficient material.
