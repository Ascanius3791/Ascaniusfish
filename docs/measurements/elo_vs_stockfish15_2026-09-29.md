<!-- OWNERSHIP=Claude -->
# Elo estimate against weakened Stockfish 15 (2026-09-29)

Engine: `main` at bdfb868, built by `tools/match` with the default TT (`tt=15`).
Opponent: Stockfish 15 with `UCI_LimitStrength=true` and `UCI_Elo=N`. That rating is
calibrated at 60+0.6 and anchored to CCRL 40/4 (Stockfish's README), so every run is at
`tc=60+0.6`, and Ascaniusfish's rating ≈ N + (Elo B−A).
Machine: WSL2, 8 cores, 4 games in parallel, nothing else heavy running.

| `UCI_Elo` | Games | Ascaniusfish W/D/L | Elo B−A (95% CI) | Implied rating |
|---|---|---|---|---|
| 1500 | 40 (20 pairs) | 35 / 0 / 5 | +338 [+220, +603] | ~1840 (1720–2100) |
| 1800 | 8 (stopped after 4 of 10 pairs) | 1 / 0 / 7 | ~−340, CI ±1100 | ~1460, meaningless alone |

**Estimate: roughly 1550–1800 CCRL.** The two levels point to different
numbers. Part of that is the tiny 1800 sample, and part is typical of weakened Stockfish: it plays strong moves with random
mistakes mixed in, so its score curve against a real engine is steep, and the anchor is only
approximate. Every game ended in mate except two Stockfish time losses in positions it was
already losing (−9 and −11); Ascaniusfish never lost on time.

PGNs (outside the repo): `~/stockfish15/games/sf1500.pgn`, `sf1800.pgn` (Ascaniusfish is "B").

## Rerunning

Stockfish 15 is built natively for Linux at `~/stockfish15/src/stockfish`, from the
source that comes with the Windows download at
`C:\Users\Acer\Documents\Ascanius\Nicht Uni\Chess\stockfish_15_win_x64_avx2\`. The net
`nn-6877cd24400e.nnue` is downloaded from `tests.stockfishchess.org/api/nn/` into `src/`, then
`make -j4 build ARCH=x86-64-avx2`. Don't use the Windows `.exe` through WSL interop: pipe
latency distorts the clock.

```bash
./tools/match ~/stockfish15/src/stockfish main tc=60+0.6 pairs=50 concurrency=4 tt=15 \
    optionsA="UCI_LimitStrength=true,UCI_Elo=1650" pgn=sf1650.pgn
```

- `tt=15` rates the engine with its real table size. Without it, `tools/match` builds refs
  with `tt=11`.
- Keep `concurrency=4`. Each game pair needs ~200 MB, and the machine's memory budget is tight.
- 20 pairs take ~25 min, and 50 pairs ~1.5 h.
- Next step: 50 pairs at `UCI_Elo=1650`, where the score should be near 50% and the CI
  tightest. Rerun it after strength improvements land, and add the result as a new row above.
  Keep the same Stockfish, TC and openings so the rows compare.
- If a run is interrupted, `/tmp/ascaniusfish_match_*` checkouts are left behind. Remove them with
  `git worktree remove --force` and delete the binaries.
