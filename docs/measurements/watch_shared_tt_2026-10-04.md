<!-- OWNERSHIP=Claude -->
# Fixed-depth Watch: one shared TT vs one per side (#91)

Engine: 97ae231 (Pure watch; search code as on `main`). Command:
`flock -w 3600 /tmp/ascaniusfish-heavy.lock ./tools/watch_tt depth=10 games=1 plies=10`
(UseNNE on, no tablebases, PTT not involved, TT 2^18 × 8).

`tools/watch_tt` plays a fixed-depth game and searches every position twice: on one
process that plays both sides (the GUI's Pure watch) and on that side's own process
(tournament mode), each with what the GUI's Watch sends (one `ucinewgame` per process per
game, `position … moves …`, `go depth D`). Time is wall time from `go` to `bestmove`.

## Result

Depth 10, opening 1 of `tools/openings.epd`, 10 plies:

| | shared | own process | shared / own |
|---|---|---|---|
| time per move | 204 ms | 338 ms | 0.603 |
| nodes per move | 142 452 | 235 888 | 0.604 |
| geometric mean per move, time | | | 0.609 (95% CI 0.467 – 0.794) |
| geometric mean per move, nodes | | | 0.593 (0.463 – 0.759) |

Shared was faster on 8 of plies 2–10 (ratios 0.34 – 0.86; ply 10 1.14); ply 1's row
was cut from the kept log, the totals include it. Both picked the same move on all 10.

**A small sample** (one game, 10 plies), kept deliberately small. The direction needs no
measurement: the shared process enters every search with the table of the previous ply's
search, which covered this position's subtree to about depth D−1, while a side's own
process last searched two plies ago and never saw the opponent's reply. The size, ~0.6×,
is this sample's; middlegame and endgame plies were not measured. A larger run is
`tools/watch_tt depth=10 games=10 plies=60` (~10 min of three engines).
