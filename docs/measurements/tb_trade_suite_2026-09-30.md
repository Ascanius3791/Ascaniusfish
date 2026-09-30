<!-- OWNERSHIP=Claude -->
# Trade suite: tables in the search (#39)

Base e47a6a4 plus the two Ascanius-owned patches of #39 (the probe in `minimax()`,
and `make_move()` restarting the halfmove clock), tables `~/syzygy-nr`.
Suite `tools/tb_trade_suite.epd`: 60 positions, 52 with 6 pieces and 8 with 7, each with a
winning and a losing capture (the losing one takes at least as much material).

```
make tb-trade-suite TB_TRADE_ARGS="depth=8 tables=off"   # ./tools/tb_trade_suite run ~/syzygy-nr ...
make tb-trade-suite TB_TRADE_ARGS="depth=8 tables=on"
```

| | solved | wrong trade | other move |
|---|---|---|---|
| tables off | 47/60 | 3 | 10 |
| tables on (SyzygyProbeLimit 5) | 57/60 | 0 | 3 |

"Other move" is a non-capture, which the tables cannot judge, so it is not counted as solved.
`make bench` nodes with the tables off: 52087 before and after.

Before `make_move()` restarted the clock (it never did: the field was `parent+1` on every move),
the same run with tables on solved 50/60, because the probe's "clock 0" test held only at the
child of a null move.
