<!-- OWNERSHIP=Claude -->
# Elo of narrowing on stored bounds (#65)

A = main at d067455 (a stored bound only cuts, #64), built by `tools/match` from
the ref with `-DTT_EXPONENT=11`. B = issue/65 at 7fbb721 built with the same
flags plus `-DTT_BOUNDS_NEVER_NARROW=0`, both options on: a bound narrows alpha
and beta when it comes from a search of the same depth (the pre-#64 rule) or of a
deeper one. Both with the net. Run from the worktree root:

```
tools/match main <uci_narrow> tc=5+0.05 concurrency=6 openings=tools/openings.epd \
    optionsA="NNEFile=$N,UseNNE=true" \
    optionsB="NNEFile=$N,UseNNE=true,TTNarrowing=true,TTNarrowingDeeper=true"
```

Run twice over the 100 openings, 413 s + 420 s. All 400 games ended
`Termination "normal"`.

| Games | W / D / L (B) | Score | Pairs 0/½/1/1½/2 | Elo B−A (95% CI) |
|---|---|---|---|---|
| 200 (pass 1) | 69 / 57 / 74 | 48.8% | 12 / 26 / 33 / 13 / 16 | −8.7 ± 42.0 |
| 200 (pass 2) | 85 / 56 / 59 | 56.5% | 11 / 14 / 31 / 26 / 18 | +45.4 ± 42.5 |
| **400** | 154 / 113 / 133 | 52.6% | 23 / 40 / 64 / 39 / 34 | **+18.3** [−11.5, +48.3], LOS 89% |

No significant difference. The two passes disagree by 54 Elo, within the noise of
200 games each. B reaches more depth in the same time: 8.05 plies per move
against 7.70 (~26.1k moves each, 0.092 s per move for both). That fits the bench:
44853 nodes with both options against 46981 for main.

Bench at its default depth 3 (`tools/bench narrow=n`, the `-DTT_BOUNDS_NEVER_NARROW=0`
build): 0 → 46981 (= main), 1 → 48039 (= df62ea6, the pre-#64 main), 2 → 44853.
