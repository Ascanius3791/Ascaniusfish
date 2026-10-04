<!-- OWNERSHIP=Claude -->
# Passed-pawn term vs main (#84)

B = `issue/84` at 628db65 (passed-pawn bonus, eval version 2), A = `main` at ea9cf06.
Both net off, tc 5+0.05, `tools/match` defaults (tt=17).

```
./tools/match main . tc=5+0.05 openings=tools/endgames.epd optionsA=UseNNE=false optionsB=UseNNE=false
./tools/match main . tc=5+0.05 openings=<tools/openings.epd twice> optionsA=UseNNE=false optionsB=UseNNE=false
```

The openings suite has 100 positions, so it was played twice to reach 400 games.

| suite | games | W | D | L | score | Elo B−A | LOS |
|---|---|---|---|---|---|---|---|
| tools/endgames.epd | 400 | 70 | 280 | 50 | 52.5% | +17.4 ± 14.9 | 98.9% |
| tools/openings.epd ×2 | 400 | 171 | 94 | 135 | 54.5% | +31.4 ± 27.9 | 98.7% |

Endgame pairs (B points 0/0.5/1/1.5/2): 0 28 125 46 1.
Opening pairs: 17 31 86 31 35.
