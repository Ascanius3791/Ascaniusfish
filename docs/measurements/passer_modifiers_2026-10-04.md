<!-- OWNERSHIP=Claude -->
# Passed-pawn endgame modifiers, stacked (#90)

`issue/90` at d35aab0 (before its rebase onto 50da7e8, no engine change in between), 2026-10-04,
04:49–07:13. Net off, tc 5+0.05, `tools/match` defaults (tt=17, 7 games in parallel).

The five modifiers were tried one after another, each on top of the ones kept so far, in the
issue's order. Stage k: A = the set kept so far, B = A + modifier k at its first-guess value.
Stage 1's A is `main` (50da7e8, set 1), so the code's cost counts. From stage 2 on both engines
are the working tree and differ only in `WeightsFile`. A modifier is kept if the endgames match
gives LOS ≥ 90% and then the openings match is not significantly worse (95% CI not entirely
below 0). The openings match is skipped when the endgames match fails.

```
./tools/match <A> . tc=5+0.05 openings=tools/endgames.epd \
    optionsA=UseNNE=false[,WeightsFile=<set kept so far>] optionsB=UseNNE=false,WeightsFile=<stage set>
./tools/match <A> . tc=5+0.05 openings=<tools/openings.epd twice> <same options>
```

First-guess values (cp, by relative rank 1–6 where 8 numbers; all but the last are scaled to 0
with all material on):

| modifier | weights |
|---|---|
| path | `passed_free_path 0 0 1 3 6 12 20 0` |
| king | `passed_king_enemy 0 0 0 3 6 9 12 0`, `passed_king_own 0 0 0 1 2 3 4 0` |
| support | `passed_supported 0 0 2 4 7 12 18 0` |
| rook | `passed_rook_behind 15` |
| square | `passed_unstoppable 300` |

## Result

| stage | A | suite | W | D | L | score | Elo B−A | 95% CI | LOS | pairs 0/½/1/1½/2 | |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 path | main | endgames | 53 | 298 | 49 | 50.5% | +3.5 ± 14.2 | [−10.8, +17.7] | 68.4% | 0 31 136 31 2 | dropped |
| 2 king | set 1 | endgames | 61 | 304 | 35 | 53.2% | +22.6 ± 14.0 | [+8.7, +36.6] | 99.9% | 0 19 139 39 3 | |
| | | openings ×2 | 157 | 114 | 129 | 53.5% | +24.4 ± 27.6 | [−3.1, +52.2] | 95.9% | 18 33 83 35 31 | **kept** |
| 3 support | king | endgames | 57 | 300 | 43 | 51.7% | +12.2 ± 11.4 | [+0.7, +23.6] | 98.2% | 0 15 157 27 1 | |
| | | openings ×2 | 148 | 115 | 137 | 51.4% | +9.6 ± 24.4 | [−14.8, +34.0] | 77.9% | 13 41 85 44 17 | **kept** |
| 4 rook | king+support | endgames | 49 | 310 | 41 | 51.0% | +6.9 ± 11.5 | [−4.6, +18.5] | 88.2% | 0 19 154 27 0 | dropped |
| 5 square | king+support | endgames | 55 | 312 | 33 | 52.8% | +19.1 ± 12.5 | [+6.6, +31.7] | 99.9% | 0 14 153 30 3 | |
| | | openings ×2 | 155 | 111 | 134 | 52.6% | +18.3 ± 29.5 | [−11.1, +47.9] | 88.9% | 21 44 60 43 32 | **kept** |

Kept: king distances, supported, unstoppable, now `weights/w3.txt` (version 3, parent 1). Set 1
stays the default. The three stage gains on the endgames suite add up to about +54 Elo over set 1,
but each was measured on top of the last and with 400 games, so treat that sum as a rough guide only.

Rook behind missed the bar by little (88.2%). Both dropped modifiers (free path, rook behind) stay in the code at 0 in every set for now.

Endings, endgames suite (stage 1 / 2 / 3 / 4 / 5): 3-fold 146/142/135/142/145, 50-move
64/49/63/60/64, insufficient material 87/112/99/105/100, mates 102/96/100/90/88, stalemate 1/1/3/3/3.
Openings suite: mates 286/285/289 of 400. Each endgames match took 436–468 s, each openings match 706–717 s.
