<!-- OWNERSHIP=Claude -->
# Root PV length with best-move-only TT entries (#82)

Commit: issue/82 on top of a42fd1b. "today" is that tree's `ascaniusfish_uci`;
"move only" is the same tree with `lib/tt_line.hpp` in `TT_entry` (the proposed
`lib/lookup_table.hpp` patch, applied in a scratch copy only), and "move + walk" is
that binary with `setoption name TTWalk value true` (`UCI_Engine::extend_pv_from_tt()`).
Command: `diagnostics/root_pv_length full=<today> compact=<move only> depths=8,9,10`,
the 130 positions (30 bench + 100 `tools/openings.epd`), `go depth 10` after
`ucinewgame`, engine defaults (net on), the PV of each `info depth 8/9/10` line.
81 s in all. 2–3 positions end before depth 8 (mate, or no legal move).

Entry size: `sizeof(TT_entry)` 184 B today → 32 B (`TT_Line` 16 B: packed move,
eval, depth, bound type, has-move; `initialized` moved next to the other flag).

Nodes, score and first move agree in all three on every line (0 mismatches),
and `make bench` gives the same nodes at depth 3 (36503) and 8 (1275625).

| depth | variant     | lines | mean len | identical to today | on today | differ | missing |
|-------|-------------|-------|----------|--------------------|----------|--------|---------|
| 8     | today       | 128   | 8.72     | 128                | 8.72     | 0.00   | 0.00    |
| 8     | move only   | 128   | 8.67     | 126                | 8.67     | 0.00   | 0.05    |
| 8     | move + walk | 128   | 8.70     | 127                | 8.70     | 0.00   | 0.02    |
| 9     | today       | 127   | 9.48     | 127                | 9.48     | 0.00   | 0.00    |
| 9     | move only   | 127   | 9.36     | 122                | 9.36     | 0.00   | 0.12    |
| 9     | move + walk | 127   | 9.46     | 125                | 9.46     | 0.00   | 0.02    |
| 10    | today       | 127   | 10.46    | 127                | 10.46    | 0.00   | 0.00    |
| 10    | move only   | 127   | 10.36    | 123                | 10.36    | 0.00   | 0.10    |
| 10    | move + walk | 127   | 10.50    | 124                | 10.46    | 0.04   | 0.00    |

Per line against today's: *on today* = plies before the first difference,
*differ* = plies after it, *missing* = today's plies after it.

- **Move only** is always a prefix of today's line (the search is the same; a line
  just ends where an exact TT hit used to hand back the stored rest). 2–5 lines
  per depth get shorter, by 1–6 plies; the worst is #48 at depth 10, 4 plies
  instead of 10.
- **Move + walk** gives back every missing ply but at #94 (depth 8: 8 of 11) and
  #38, #58 (depth 9: 9 of 10, 9 of 11). At depth 10 it runs 2 plies *past* today's
  end on 3 lines (#22, #87, #108): the TT has moves there from other parts of the
  tree, below the horizon. No walked move contradicts today's line.
- Every line is shorter than the 24 plies the time manager compares (`TM_PV_LEN`),
  so λ sees the whole line either way; the few shorter lines are what could change it.
