<!-- OWNERSHIP=Claude -->
# One static eval for the search (#71)

`static_eval()` (`lib/pruning.hpp`) is eval() plus, with `UseNNE` on, the net's
correction: the scale the quiet leaf of `minimax_tactical()` already returned.
Two changes, each its own commit and match, both sides with the net:

1. **Stand pat**: `minimax_tactical()` stands pat on `static_eval()` instead of
   the raw eval(), so it compares like with like with the leaves below it.
2. **Null-move guard**: `minimax()` tries the null move only when `static_eval()`
   is already ≥ beta (white) / ≤ alpha (black).

Engines built from the working tree with `tools/match`'s flags and
`-DTT_EXPONENT=11`: main = eb3e5da, p1 = main + change 1,
p2 = p1 + change 2. Run from the worktree root, two passes (or more) over the 100
openings, 5 s + 0.05 s per game, 6 games in parallel:

```
tools/match <A> <B> tc=5+0.05 concurrency=6 openings=tools/openings.epd \
    optionsA="NNEFile=$N,UseNNE=true" optionsB="NNEFile=$N,UseNNE=true"
```

All games ended `Termination "normal"`. The matches and the cost table below
were run on eb3e5da, before #70's tempo term (8157b2c) landed; the commits were
rebased onto it afterwards and their `bench:` re-measured (54588 for both, with
the net 58889 → 54807 for change 1).

## Change 1: stand pat on the corrected eval (A = main, B = p1)

| Games | W / D / L (B) | Score | Pairs 0/½/1/1½/2 | Elo B−A (95% CI) |
|---|---|---|---|---|
| 200 (pass 1) | 71 / 56 / 73 | 49.5% | 15 / 24 / 26 / 18 / 17 | −3.5 ± 44.6 |
| 200 (pass 2) | 87 / 57 / 56 | 57.8% | 7 / 13 / 36 / 30 / 14 | +54.3 ± 38.0 |
| **400** | 158 / 113 / 129 | 53.6% | 22 / 37 / 62 / 48 / 31 | **+25.2** [−3.9, +54.8], LOS 95.5% |

It does not measurably lose, so it lands (the issue's rule for this change).

## Change 2: null move only where it can cut (A = p1, B = p2)

| Games | W / D / L (B) | Score | Pairs 0/½/1/1½/2 | Elo B−A (95% CI) |
|---|---|---|---|---|
| 200 (pass 1) | 77 / 50 / 73 | 51.0% | 9 / 18 / 43 / 20 / 10 | +6.9 ± 36.5 |
| 200 (pass 2) | 81 / 55 / 64 | 54.2% | 8 / 18 / 39 / 19 / 16 | +29.6 ± 39.3 |
| **400** | 158 / 105 / 137 | 52.6% | 17 / 36 / 82 / 39 / 26 | **+18.3** [−8.4, +45.1], LOS 91.0% |

Below the issue's LOS 95%. Ascanius accepted it at 400 games and stopped the
games beyond them (a third pass was cut off unfinished).

## Cost

`tools/bench 6` (30 positions, depth 6), nodes and `perf stat -e instructions:u
taskset -c 0`, minus `bench 1` for the startup:

| | nodes, no net | instr/node | nodes, net | instr/node | search instr, net |
|---|---|---|---|---|---|
| main | 721,660 | 4564 | 747,964 | 4791 | 3.58G |
| p1 | 721,660 | 4554 | 728,932 | 5275 | 3.84G |
| p2 | 804,489 | 4539 | 667,505 | 5305 | 3.54G |

Without the net change 1 is the same search (signature unchanged). With it, every
stand pat pays one correction: +10% instructions per node, 2.5% fewer nodes,
+7% in all. Change 2 spends one more static eval per node that reaches the null
move's other tests, and saves 8% of the nodes. Without the net it searches 11%
more nodes: the raw eval's tempo bias (#70) skews the guard, as the issue expects.
