<!-- OWNERSHIP=Claude -->
# Gaviota DTM at tablebase roots (#77), 2026-10-03

Branch `issue/77` on top of `ed5cb72`. Tables: `~/gaviota` (145 `*.gtb.cp4`, all md5-checked,
from tablebase.lichess.ovh/tables/standard/Gaviota/), Syzygy `~/syzygy-nr`.

## Probe vs Lichess — `make gaviota-test`

```
init: up to 5 pieces from /home/ascanius/gaviota, RSS 5032 -> 16360 kB
1039 positions: 0 reference mismatches (0 without a Lichess DTM), 0 child mismatches, 0 not found
18757 probes in 5.28 s (281.6 us each, cold disk cache included), RSS 49340 kB
PASS
```

The positions are `tools/syzygy_reference.txt`'s (every 3-5 piece table, en passant included),
their DTM recorded from the Lichess API into `tools/gaviota_reference.txt`. "Child" checks a
position's DTM against 1 + the best of its children's.

## tb-suite — `make tb-suite` (GAVIOTA on by default)

| defender | won | drawn | mate N per move |
|---|---|---|---|
| engine without tables | 38/38 | 44/44 | shrinks every move, ends at 1 (by 2 where the defender errs) |
| `TB_SUITE_ARGS=opp_gaviota=on` (DTM-optimal) | 38/38 | 44/44 | shrinks by exactly 1, e.g. KBNvK `33 32 … 2 1` |

## GUI

First visit of a 5-piece position (DTM of it, its moves and the ≤64-ply line): ~35 ms
(`POST /api/fen`), then cached per position; a state request ~2 ms.
