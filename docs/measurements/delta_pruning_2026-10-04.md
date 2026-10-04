<!-- OWNERSHIP=Claude -->
# Delta pruning margin (#75)

`issue/75`, 2026-10-04. Delta pruning in `minimax_tactical()`: out of check and off the
forced-move path, a non-promotion capture is skipped when stand pat + the victim's
phase-blended value + M stays short of alpha (white) / beta (black); not below `game_phase()` 4.
Engine defaults (net on). Main is edd2050.

## Margin study

`diagnostics/delta_margin.cpp` (build with `-DDELTA_STATS`): the bench positions and both
`tools/openings*.epd` (353), searched to depth 6 with the pruning off. Every capture the rule
could prune was recorded (200000 of 1333744 kept at random) and searched with the window it had;
"wrong" = it would have raised alpha (lowered beta). The SEE rule replaces the victim's value
with the capture's SEE.

| M | pruned (victim value) | wrong %, net on | wrong %, net off | wrong %, SEE rule |
|---|---|---|---|---|
| 0 | 117532 | 16.0 | 19.2 | 31.7 |
| 200 | 71757 | 5.2 | 6.8 | 22.0 |
| 300 | 56489 | 2.4 | 2.9 | 19.6 |
| 400 | 44757 | 1.1 | 1.3 | 17.1 |
| 500 | 35278 | 0.6 | 0.8 | 13.9 |
| 600 | 28698 | 0.4 | 0.5 | 10.4 |
| 800 | 21328 | 0.24 | 0.19 | 7.7 |

A capture gains more than its victim: searched result − (stand pat + victim) has median +51 cp
(+77 net off), p99 +571. Taking a piece also removes its square, threat and king-attack terms.
SEE is no use here: it is below the victim's value, so it prunes more and is wrong far more often.

## Matches

`tools/match` against main, 5+0.05, `tools/openings_large.epd` (200 openings × 2 colours), tt=17.
200 cp ran alone at concurrency 6 and was stopped at 318 games; 400/500/800 ran side by side at
concurrency 2 each, engines prebuilt with `-DDELTA_MARGIN_CP=M`.

| M | games | W / D / L | Elo vs main | LOS |
|---|---|---|---|---|
| 200 | 318 | 103 / 92 / 123 | −21.9 ± 30.3 | – |
| 400 | 400 | 133 / 119 / 148 | −13.0 ± 27.8 | 17.8% |
| 500 | 400 | 155 / 115 / 130 | +21.7 ± 25.6 | 95.3% |
| 800 | 400 | 152 / 109 / 139 | +11.3 ± 25.9 | 80.4% |

500 cp is the default (Ascanius's call). It is the best of four margins, so its LOS is
somewhat optimistic; 400 below 200-and-500 is noise at ±26 Elo.

`make bench`: 36069 (last recorded on main) → 30974 nodes.
