<!-- OWNERSHIP=Claude -->
# Plan term: what could make it gain Elo at a clock (#44)

The plan term (#43, `lib/plan_eval.hpp`) won **+58±42 Elo at fixed depth 5**, but
it drops nps from 488k to 63k. This page lists what could change that, with
reasons, estimates and a way to measure each. The numbers come from
`diagnostics/plan_eval_stats.cpp` (11716 positions: the probe's playouts plus
one random capture from each, since quiescence leaves are capture-heavy), at
commit 5d52dbf. Timings are ±5%.

## Where the time goes

| | us/call |
|---|---|
| `basic_eval` without the plan term | 0.4 |
| plan term | 15.5 |
| … setup (pawn attacks, piece tables, roles) | 0.4 |
| … BFS over all pieces | 0.75 |
| … db of every reached square | **14.4** |

- About 265 squares get a db per call: N 81, K 47, R 42, Q 42, B 31, P 23.
- One db costs about 54 ns. Most of that is recomputing an average of 3.7 other
  pieces' activity roles, the ones whose mask holds the from- or to-square.
- `eval()` runs at every stand-pat of `minimax_tactical`, so there is about one
  call per node.

**The bar.** A node cost about 2 us before the term. At about 80–100 Elo per
doubling of speed, +58 Elo pays for a slowdown of about 1.5–1.65x. So the term
has to cost **≤ ~1.3 us per call, 12x less than now**, if the +58 holds (its CI
is ±42). No single item below gets there. The plan is to stack exact speedups
with approximations that keep the fixed-depth gain.

## Where the value goes

| type | share of \|term\| | mean \|term\| (cp) | no target | N=1 | N=2 | N=3 | N≥4 |
|---|---|---|---|---|---|---|---|
| P | 27% | 5.9 | 45% | 77% | 15% | 6% | 1% |
| N | 22% | 19.0 | 7% | 45% | 33% | 15% | 7% |
| B | 16% | 13.2 | 18% | 64% | 27% | 6% | 2% |
| Q | 13% | 24.3 | 3% | 42% | 37% | 15% | 6% |
| R | 13% | 9.5 | 23% | 51% | 31% | 11% | 8% |
| K | 10% | 13.3 | 3% | 28% | 31% | 25% | 16% |

- |plan_eval| has mean 56 cp, p95 136, p99 177 and max 287.
- **The term swings ~100 cp with the side to move.** The same position with
  the other side to move differs by 98 cp on average. Along a game it changes
  by 96 cp per ply but only 31 cp over two plies. The cause is the 1+N vs 2+N
  divisor: it acts like a side-to-move bonus of about 100 cp. That bonus goes
  straight into stand-pat cutoffs and aspiration windows.

## Candidates

"Gain" is a guess until the match in the last column is run.

### Speed, exact (the term does not change)

| # | Idea | Reasoning | Estimate | Measure |
|---|---|---|---|---|
| S1 | **Cheaper touched-role update** | 92% of the time is db, and most of db is recomputing 3.7 other roles per square. For a leaper or pawn role, moving a piece s→t changes only whether s and t are occupied, so the change is a few bit tests. Only sliders whose ray passes through s or t need their attacks again. | 2–3x on db | probe: fast == reference; `make speed-compare`; bench unchanged |
| S2 | **Vacate once per piece** | Every target of a piece shares the "s is empty now" part. Compute it once per piece (the piece table at s, roles that see s, own roles). Then each target adds only "t is occupied", correcting for sliders that see both squares. | 1.5–2x on top of S1 | as S1 |
| S3 | **Plan cache by Zobrist key** | Inside `src/plan_eval.cpp`, so no Ascanius-owned file changes. Quiescence leaves repeat across iterative-deepening iterations and move orders. 2^14 entries of 8 bytes = 128 kB, in line with the small-memory rule. | gain = hit rate; measure it first (counter build over `make bench`) | hit rate; nps |

**Ruled out.** An exact early stop on an upper bound for db is exact but
**10% slower**: the safe bound is 230–320 cp, so it almost never stops.
Incremental updates from the parent could skip at most ~27% of the per-piece
work: only 29% of unmoved pieces keep the same reached set and db values.

### Speed, approximate (the term changes; the match decides)

| # | Idea | Reasoning | Estimate | Measure |
|---|---|---|---|---|
| A1 | **Cap N at 2** | Most chosen targets are at N ≤ 2, and deep targets are divided by 4+ anyway. | **2x** (8.2 us); correlation 0.990, mean \|diff\| 7.4 cp | fixed-depth 5 match vs the uncapped term: ≥ 0 within CI |
| A2 | Bound 0.5 × the largest db seen, as an early stop | Slightly more accurate than a cap at N=3 at the same speed. | 1.4x (11.1 us); correlation 0.999, 1.4 cp | as A1 |
| A3 | **Lazy plan term at stand-pat** | If the eval without the term is more than a margin outside [alpha, beta], the cutoff does not depend on the term. The margin is ~180 cp (p99) or 290 cp (max); tighter for fewer pieces. | depends on how many stand-pats are that far outside the window: measure first. It is the one idea that skips calls rather than making them cheaper. | count of skippable calls; nps; depth match |
| — | Cheap db (piece table + own roles only) | 3.1x, but correlation 0.84 and 50% of the targets change. It is a different term, not a faster one. | not recommended | — |

A3 needs `alpha`/`beta` at the eval call, in `minimax_tactical`
(`ascaniusfish_2.hpp`) and `basic_eval` (`src/basic_eval.cpp`). Both are
Ascanius-owned, so it goes to Ascanius as a patch.

### Quality (a better term for the same cost, or cheaper)

| # | Idea | Reasoning | Estimate | Measure |
|---|---|---|---|---|
| Q1 | **Remove the tempo swing** | A 100 cp side-to-move oscillation is noise that the search has to fight through (stand-pat, aspiration). Options: 2+N for both sides, or a fixed tempo bonus in place of 1+N for the side to move. | possibly the biggest quality gain here; the numbers give no estimate | depth match; the swing measured by the stats diagnostic falls to ~30 cp |
| Q2 | **One piece per square** | In the Yugoslav (probe position 2) Nc3, Bf1 and Qd2 all aim at d5 and are all credited, though only one can stand there. Assign squares greedily by term. | cheap post-processing; removes systematic over-counting | depth match |
| Q3 | **Kings stay home in the middlegame** | db leaves king safety out, so Ke1-f2-g3 gets db +60 (term +20) in the Yugoslav. Scale king db by the endgame weight, or drop kings while queens are on. Kings are 18% of the targets for 10% of the value. | also ~15% faster with queens on | depth match |
| Q4 | Avoid squares attacked by lower-valued enemy pieces | The same logic as the pawn rule, extended: a rook does not route via a square a knight hits. | **20% faster** (fewer targets), 6.7% of targets change, correlation 0.983 | depth match |
| Q5 | Scale factor on the whole term (×0.5, ×0.75, ×1.5) | The cheapest experiment. It also says whether +58 is the term or noise: a real effect should vary smoothly with the scale. | none | three depth matches |
| Q6 | Pawn plans N ≤ 1, or no pawns | Pawns carry 27% of the value, but a push is irreversible and "pawn could go to a3" is a weak plan. | speed small (pawns are 9% of targets) | depth match |

## Measuring

- **Exactness** (S1–S3): `diagnostics/plan_eval_probe`. It must keep reporting
  fast == reference, correct paths and mirror symmetry, and `make bench` must
  keep the same node count.
- **Accuracy of an approximation**: `diagnostics/plan_eval_stats` prints
  correlation and mean |diff| against the exact term.
- **Speed**: `make speed-compare A=issue/43 B=.`, the nps of the whole engine,
  which is what the clock sees.
- **Quality**: `tools/match <prev> . depth=5`, against the previous tip of this
  line, not `main`. This isolates the one change.
- **The verdict**: `tools/match main . tc=10+0.1`. Only this says whether the
  line can land.

## Order

1. **S1 + S2**: exact, so no match is needed. Then **Q1**, because its
   side-to-move noise distorts every other measurement.
2. **A1**, then Q3 + Q4 (both quality and speed), then Q2 and Q5.
3. S3 and A3 once the per-call cost is down: their value is the fraction of
   calls saved, and that fraction is worth more when calls are cheap.
4. A tc match after each step that changes speed by more than 1.5x.

Filed as follow-up issues: #45 (S1+S2), #46 (Q1), #47 (Q2–Q5, A1), #48
(A3, S3, and the tc verdict).
