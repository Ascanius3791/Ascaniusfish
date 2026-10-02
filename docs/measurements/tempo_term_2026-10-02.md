<!-- OWNERSHIP=Claude -->
# The tempo term (#70)

A = main at eb3e5da. B = the same tree with the patch below in
`src/basic_eval.cpp` (`tempo_eval()`: the side to move gets
`(T_o·M + T_e·(78−M))/78`, M = both sides' material in 39ths, 0..78), built
in a scratch copy because that file is Ascanius-owned. Every engine
`-DTT_EXPONENT=11`, `UseNNE=false`.

## Tempo bias, `tools/tempo_swing` (20d9235)

```
tools/tempo_swing per_bin=1500 jobs=4 [engine=<B binary>]
```

9000 positions of `nets/nne_d6_test.tsv` (seed 70; 2802 non-quiet skipped),
1500 per phase bin, one `go depth 8` each, about 4.5–5 min per run. T̂ = half
the odd/even swing of the root score in cp, the median per bin with its 95% CI.
Positive means the side that just moved gets too much.

**A (main):**

| material | d=2 | d=4 | d=6 |
|---|---|---|---|
| 0..13  | +6.2 [+5.8,+6.8] | +6.0 [+5.5,+6.2] | +4.5 [+4.2,+5.0] |
| 14..26 | +9.2 [+8.5,+10.0] | +8.8 [+8.2,+9.2] | +7.5 [+7.0,+8.0] |
| 27..39 | +11.0 [+9.8,+12.2] | +10.0 [+9.5,+11.0] | +9.2 [+8.8,+10.0] |
| 40..52 | +13.8 [+12.5,+15.0] | +12.5 [+11.8,+13.2] | +10.1 [+9.2,+11.0] |
| 53..65 | +15.2 [+13.8,+17.0] | +12.8 [+11.8,+13.5] | +9.8 [+9.0,+10.8] |
| 66..78 | +15.0 [+14.0,+16.5] | +12.5 [+11.5,+13.5] | +9.2 [+8.5,+9.8] |
| all (n≈5000–5650) | +10.2 | +9.8 | +8.0 |

Fit at d=2: T_o = +17.2, T_e = +5.8.

**B with T_o=17, T_e=6** (the d=2 fit as it stands): every cell is still +0.5 to
+5.2 (all: +3.2 / +3.0 / +2.5), the 53..65 bin at d=2 over the bar. The term
removed the same ~69% of the bias at every depth. A swing measured at d=2 is
therefore already diluted (quiescence and reductions mix leaf parity), and
T̂ = f(d)·(B − T) with f(2) ≈ 0.69. Dividing by that gives the per-leaf bias.
The fits at d=2, 4 and 6 all give T_o ≈ 24, T_e ≈ 9.

**B with T_o=24, T_e=9** (the patch):

| material | d=2 | d=4 | d=6 |
|---|---|---|---|
| 0..13  | −0.8 [−1.0,−0.2] | −1.0 [−1.5,−0.8] | −1.2 [−1.5,−0.8] |
| 14..26 | +1.2 [+0.8,+1.5] | +0.8 [+0.5,+1.2] | +0.8 [+0.2,+1.2] |
| 27..39 | +1.0 [+0.2,+1.8] | +1.2 [+0.5,+2.0] | +1.2 [+0.8,+1.8] |
| 40..52 | +1.5 [+0.5,+2.0] | +1.8 [+1.0,+2.5] | +1.0 [+0.5,+1.8] |
| 53..65 | +1.8 [+0.8,+2.5] | +0.9 [+0.0,+1.5] | +0.5 [+0.0,+1.2] |
| 66..78 | +0.0 [+0.0,+0.5] | +0.0 [+0.0,+0.5] | +0.0 [−0.2,+0.5] |
| all | +0.2 | +0.2 | +0.2 |

Every bin at every depth is within ±2 cp.

## Match, net off

```
tools/match main <B binary> tc=5+0.05 concurrency=6 pairs=100 openings=tools/openings.epd \
    optionsA=UseNNE=false optionsB=UseNNE=false
```

Run twice over the first 100 openings, 415 s + 435 s. All 400 games ended
`Termination "normal"`.

| Games | W / D / L (B) | Score | Pairs 0/½/1/1½/2 | Elo B−A (95% CI) |
|---|---|---|---|---|
| 200 (pass 1) | 75 / 62 / 63 | 53.0% | 11 / 20 / 32 / 20 / 17 | +20.9 ± 42.1 |
| 200 (pass 2) | 72 / 65 / 63 | 52.2% | 9 / 19 / 39 / 20 / 13 | +15.6 ± 38.5 |
| **400** | 147 / 127 / 126 | 52.6% | 20 / 39 / 71 / 40 / 30 | **+18.3** [−10.1, +46.8], LOS 90% |

No significant difference, leaning B. Bench: 55161 (A) → 54588 (B).

The net was trained on main's static eval, so with the term it corrects a
tempo it no longer needs to: retrain before measuring with the net
(`make nne-retrain`, `docs/NNE_RELABEL.md`).
