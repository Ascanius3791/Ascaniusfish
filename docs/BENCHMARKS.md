<!-- OWNERSHIP=Claude -->
# Benchmarks

Three commands for checking correctness and speed. They build their tools in
`tools/` and don't touch the engine binaries.

| Command | Checks | Time |
|---|---|---|
| `make perft` | move generation against known node counts | ~40–55 s (`PERFT_DEPTH=4`: 1 s) |
| `make bench` | search signature (total nodes) and nps | ~12–17 s |
| `make speed-compare A=<ref> B=<ref>` | whether B is faster than A | ~5 min |

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
  cost per node, not time to depth.

Keep the machine otherwise idle while it runs. The runs are single-threaded
and sequential.

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
