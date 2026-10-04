<!-- OWNERSHIP=Claude -->
# SPRT: deeper stored bounds cut, without narrowing (#93)

One binary for both sides: issue/93 at 750df69 (main c7a7259 + #93), built by
`tools/match` from the working tree with `-DTT_EXPONENT=17`. B sets
`TTDeeperCuts=true`: a lower or upper bound stored by a search at least as deep
as the node cuts off; it never narrows alpha and beta. A keeps the main rule (a
bound cuts only at exactly its own depth). Net on for both (the default).

```
tools/match . . sprt=0,10 tc=5+0.05 time=10 optionsB=TTDeeperCuts=true
```

7 games in parallel, `tools/openings_ccrl.epd`. 364 games in 616 s, all
`Termination "normal"`.

| Games | W / D / L (B) | Score | Pairs 0/½/1/1½/2 | Elo B−A (95% CI) | LLR |
|---|---|---|---|---|---|
| 364 | 141 / 113 / 110 | 54.3% | 17 / 39 / 52 / 44 / 30 | **+29.7** [−0.9, +60.7], LOS 97.1% | +1.00 |

**No decision** within the 10 min budget (LLR bounds ±2.94). The point estimate
was positive the whole run (+33 at pair 147, +30 at the end).

Bench at depth 3: 30974 nodes with the option off (= main), 29770 with
`tools/bench deeper=1` (−3.9%).
