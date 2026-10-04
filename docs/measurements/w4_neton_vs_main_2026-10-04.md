<!-- OWNERSHIP=Claude -->
# Set 4 with the old net against main (#90)

`issue/90` at be08e38 (set 4 the default, eval version 3) against `main` at 50da7e8 (set 1),
2026-10-04. Both engines with the net on (the default), the same `nets/nne_d6.bin`, which was
trained on set 1's scores. The first 200 openings of `tools/openings_large.epd`.

```
./tools/match main . tc=10+0.1 openings=<openings_large.epd, first 200>
```

400 games, 7 in parallel. B = set 4, A = main.

```
B vs A: W 215  D 84  L 101   score 64.2%
pairs (B points 0/0.5/1/1.5/2): 14 25 58 39 64
Elo B-A: +101.8 ± 32.8  (95% CI [+69.9, +135.5], LOS 100.0%)
endings: 3-fold repetition 38, fifty move rule 35, insufficient material 11, mates 316
```

So set 4 is better than set 1 with the old net too. A net retrained on set 4 is still to come.
