<!-- OWNERSHIP=Claude -->
# The net retrained on the tempo eval (#56 route, #70)

The dataset's 532,902 positions were relabelled at 5e22541 (main with the tempo
term) in a Claude cloud session (`tools/nne_cloud_relabel.sh`, two chunks; the
container restarted in the first, the second carried on from the parts on disk).
Static, qsearch and label are all computed with the net off. 4,091 were dropped
(3,979 no longer quiet, 74 mate, 38 table labels), 528,811 kept. Then
`make nne-retrain FROM=claude/hopeful-feynman-0nmb1s`: the spot check relabelled
1,036 positions here, and all 1,026 kept ones equalled the imported labels; training took about
4 min on the GPU, and the `nne-test` check passed.

## Offline, the same test positions (`nne_train epochs=0` scores a net)

New labels (52,792 test positions), error |static + c − label| in cp:

| | median | clipped mean | p90 | E error |
|---|---|---|---|---|
| static | 67.0 | 145.3 | 379 | 0.0610 |
| static + old net | 62.3 | 138.5 | 364 | 0.0582 |
| static + new net | **58.3** | **136.1** | 361 | **0.0566** |

Per position the new net is closer than the old one in 53.7%, the old one
in 44.5% (mean gain +2.6 cp). Compared with static alone the new net is
closer by more than 50 cp in 16.1% and farther in 6.6% (old net: 14.9 / 7.3).

On the old labels (the #51 test set, 53,176 positions, in no training set of
either net) the two are equal: median 59.3 vs 59.4, closer 49.6% vs 48.5%.

Mean correction in the mover's view: old net +17.0 cp, new +8.1. The 9 cp it
dropped is the tempo the static eval now gives itself.

## Matches

One binary from 5e22541 (`ascaniusfish_uci.cpp`, the match flags,
`-DTT_EXPONENT=11`), `tools/match tc=5+0.05 concurrency=6
openings=tools/openings.epd`, nets given as absolute `NNEFile`:

| B vs A | Games | W / D / L (B) | Score | Elo B−A (95% CI) |
|---|---|---|---|---|
| new net vs old net | 200 | 89 / 50 / 61 | 57.0% | **+49.0 ± 42.5** [+7.2, +92.2], LOS 98.9% |
| new net vs net off | 200 | 99 / 51 / 50 | 62.3% | **+86.9 ± 42.0** [+46.1, +130.2] |

All 400 games ended `Termination "normal"`. A second pass of new vs old net
broke off at pair 84 (+8 ± 49 there) when an engine failed to restart, while
`nne_train` was scoring nets next to it; most likely the shared memory cap.
It is not counted.
