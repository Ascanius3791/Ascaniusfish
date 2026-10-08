<!-- OWNERSHIP=Claude -->
# Shelter cap removed; how the tuner's uncertainties are distributed (#107)

`issue/107` on 4eab1a9 (the cap removal, eval version 11) plus the `tools/tune.cpp` change of
the commit that adds this file, 2026-10-09. `data/tune2`, K 302.22 fixed, 4 threads, GN from
`weights/w8.txt`, `gauge=pin`, `se=1`. Sets, logs and spectra in `data_local/` (untracked).
Follow-up to `tune_pin3_2026-10-08.md`.

```
./tools/tune data=$D from=weights/w8.txt out=data_local/w9_cap_l0.txt k=302.22 jobs=4 lambda=0 spectrum=data_local/spec_cap_l0.txt
./tools/tune data=$D from=weights/w8.txt out=data_local/w9_cap_l9.txt k=302.22 jobs=4          spectrum=data_local/spec_cap_l9.txt
```

`tools/tune` now takes A's full eigendecomposition (Householder + implicit QL, 981 x 981 in
~2 s; residual of the reported vectors 6e-23 against eigenvalues >= 5e-13) instead of a
6-vector subspace iteration. The report names the 40 weakest directions with the SE along
each, sqrt(sigma^2 / (n eigenvalue)), `spectrum=` writes all of them, and every `# se` line
carries `fires`: train positions and sides where the weight's coefficient is nonzero.

## The rewrite is now exact

`basic_eval()` of w8 against w8 rewritten into the pinned gauge, all 3,064,881 positions:

| eval | differ on | mean | max | beyond 4 cp |
|---|---|---|---|---|
| capped (#106) | 1,294,545 | 0.481 | 109 | 161 |
| uncapped | 1,294,446 | 0.478 | 5 | 3 |

The 161 promotion outliers are gone; what is left is the tables' rounding, as in `pin104`.
`make bench` 31041 -> 31218.

## Fits: unchanged

| fit | train | valid | test |
|---|---|---|---|
| w8 | 0.113351 | 0.113442 | 0.113304 |
| capped, lambda 0 (#106) | 0.112896 | 0.112954 | 0.112867 |
| **uncapped, lambda 0** | 0.112897 | 0.112954 | 0.112867 |
| capped, lambda 1e-9 (#106) | 0.113243 | 0.113329 | 0.113194 |
| **uncapped, lambda 1e-9** | 0.113243 | 0.113329 | 0.113194 |

| set | opening P R N B Q | endgame P R N B Q | `ks_shelter` |
|---|---|---|---|
| uncapped, lambda 0 | 130 249 252 282 451 | 386 1970 1218 1291 3853 | 57 -68 -1 18 28 25 -8 -17 |
| uncapped, lambda 1e-9 | 130 453 396 434 1336 | 333 1655 1020 1083 2850 | 63 -68 -1 22 32 30 1 -34 |

Within 1 cp of #106 everywhere, as expected: the cap touched 5e-5 of the data. Piece value SEs at
lambda 0 too (R 4.9/6.5, N 3.5/4.7, B 3.6/4.3, Q 13.1/15.3 opening/endgame).

## Distribution at lambda 0

**Eigenvalues** (981, reduced coordinates), by decade:

```
1e-13    1
1e-12   21  ####
1e-11   80  ################
1e-10  274  ######################################################
1e-9   389  #############################################################################
1e-8   179  ###################################
1e-7    32  ######
1e-6     5  #
```

**There is no knee.** The log spectrum is one smooth hump; below k = 300 no two neighbours differ
by more than 1.34x, except the very weakest (2.6x, `mobility_queen_endgame[0]`). Direction SEs:
k 0 302 cp, k 10 122, k 20 72, k 40 40, k 100 22, k 200 12, k 500 4.8, k 900 1.0. 245
directions have SE > 10 cp, 106 > 20, 31 > 50, 12 > 100.

**Per-weight SEs** (993 tuned): median 6.3 cp, p75 12, p90 23, p95 38, p99 118, max 302. 311 above
10 cp, 119 above 20, 36 above 50.

**SE is set by how often a weight fires**, not by collinearity: SE ~ 1400 / sqrt(fires)
(median; p10-p90 of SE*sqrt(fires) 900-3100).

| fires | weights | median SE | max SE |
|---|---|---|---|
| 1e2-1e3 | 13 | 112 | 302 |
| 1e3-1e4 | 135 | 23 | 139 |
| 1e4-1e5 | 497 | 7.5 | 39 |
| 1e5-1e6 | 304 | 3.1 | 19 |
| >= 1e6 | 44 | 1.9 | 8.9 |

The SE > 20 cp weights: opening king table 37 (ranks 4-8: a king out of its corner with queens
on), endgame king 17, endgame queen table 12, queen mobility 15 (counts 0-2 and 22-27), knight and
bishop tables 24 (corners and far squares), the 7th-rank phalanx 4, `passed_unstoppable`,
`mobility_knight_endgame[0]`, one `threat_by_rook`. The top: `mobility_queen_endgame[0]` 302 cp
(233 fires), opening king h8/a8 190/162, `mobility_queen_opening[27]` 161, `pawn_phalanx_opening[6]`
151 (485 fires; endgame [6] 112, value 1394).

**Collinear rather than rare** (highest SE*sqrt(fires)): the opening king table (g1 8.9 cp at
1.7M fires; the shelter and attack terms see the same king square), `ks_no_queen` (9.5 cp at
963k, against the queen's values), `piece_value_endgame[4]`, and `activity_pawn_attack` (18.6
cp at 113k). The last is a new near-null direction, eigenvalue 2.6e-11 (SE 42):
`threat_by_pawn[1..4]` all -0.45 against `activity_pawn_attack` +0.44. Both count pawns attacking
enemy pieces; they differ only where a pawn attacks the king, a pawn, or a piece two pawns attack.

## Lambda 1e-9 is not a light ridge

The prior's sd is 6.8 cp; its precision 1e-9 sits at the **median** eigenvalue. 376 of 981
directions (eigenvalue < 1e-9) are held more by lambda than by the data, and the fit flags 808 of
993 weights as lambda-held (SE* shrinks by > 1.5 with 10 lambda; every SE is <= 7 cp). So the
lambda 1e-9 set is mostly w8 again, which is why its piece values stay near w8's and the two fits
are so far apart (queen 1336 against 451 in the opening). Lambda 1e-10 would hold about the
weakest 100 directions (SE > 22 cp), 1e-11 the weakest 22.

## Reading

- Removing the cap made the #106 pin exact and changed nothing else.
- The weak directions are a continuum of rarely seen weights, not a separable set. A cutoff for
  "inactive" is a choice (e.g. fires < 1e4: 148 weights; SE > 20: 119), not something the data
  marks.
- The piece values' gap between lambda 0 and 1e-9 lies along well-determined directions (queen SE
  13 cp against a gap of 885 cp): lambda 1e-9 holds most of the set at w8, not just the rare
  weights. Why the lambda 0 set plays worse (#104, -60 Elo) is still not explained by rare weights.
- All SEs are lower bounds: a game's positions share one result.
