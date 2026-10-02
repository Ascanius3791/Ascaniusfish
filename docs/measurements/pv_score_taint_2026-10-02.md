<!-- OWNERSHIP=Claude -->
# Can a cutoff line carry a wrong score? (#64)

Commit: main `df62ea6` (issue/64 branch, search code unchanged). Probe: `diagnostics/pv_score_taint_probe.cpp`.
`CHECK=1` showed that the probe's copy of `minimax()` matches main's: same score, move and node count on every iteration checked.

```
./diagnostics/pv_score_taint_probe tools/openings.epd 30 20 10 0           # openings: 30 games x 20 plies, ID 1..10
./diagnostics/pv_score_taint_probe tools/endgames.epd 20 20 12 0           # endgames: 20 games x 20 plies, ID 1..12
NO_REP=1 ./diagnostics/pv_score_taint_probe tools/endgames.epd 20 20 12 0  # same, repetition draws off
```

A **masquerade** is a node whose window a same-depth TT bound narrowed, and which returns a bound
that lies inside the window its parent gave it, so the parent reads the bound as exact.
**Consistent** means the returned value equals the TT bound (two bounds meet, so the value is right).
The **gap** is |returned value - TT bound| when they differ. A root score is **tainted** if a
masquerade sits on the chain of exact values it came from, or if it was read from a stored exact
entry that was tainted. "cut-only" is the same iteration re-run in a fork(), from the same table,
with bounds that only cut and never narrow.

Root iterations that only handed back the root's own exact entry (1 node) are left out.

| | openings d10 | endgames d12 | endgames d12, no rep |
|---|---|---|---|
| root iterations searched | 1239 | 781 | 649 |
| root score tainted | 705 (57%) | 462 (59%) | 400 (62%) |
| tainted, gap > 0 | 11 (0.9%) | 25 (3.2%) | 21 (3.2%) |
| root gaps > 0 (cp) | 1-9 | 1-16, plus 95 and 166 | 1-12 |
| masquerades, all nodes (move loop / null move) | 9034 / 4 | 7458 / 66 | 5590 / 45 |
| of those inconsistent | 82 / 0 | 197 / 32 | 140 / 15 |
| tainted roots with a null-move masquerade | 0 | 18 | 18 |
| tainted roots, masquerade at the root itself | 26 | 15 | 7 |
| tainted roots via a stored exact entry | 88 | 56 | 105 |
| \|main - cut-only\| untainted: mean, >=50 cp, other move | 0.9, 0, 14 | 1.7, 2, 5 | 1.9, 4, 2 |
| \|main - cut-only\| tainted: mean, >=50 cp, other move | 3.0, 2, 99 | 4.8, 10, 49 | 2.8, 4, 28 |

The two large gaps (endgames, repetition draws on): game 9 `2R5/8/8/5p2/5P1r/5k2/8/6K1 w`, ply 16,
depth 12: main -54, cut-only -30, gap 166. Game 8 `2R5/4BK1k/r5p1/8/8/8/8/8 w`, ply 12, depth 9: main 668
g3d3, cut-only 736 f6c3, gap 95. With repetition draws off, no root gap exceeds 12 cp.

After #64 main uses the cut-only rule: `CHECK=1` finds 0 mismatches between main's `minimax()`
and the probe's cut-only copy (2 opening games x 8 plies to depth 9, 2 endgames x 10 plies to depth 11).
