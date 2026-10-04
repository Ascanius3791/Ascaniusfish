<!-- OWNERSHIP=Claude -->
# Weight tuning plan (M6, #83)

M6: tuned weights beat `WEIGHTS_OG` in a match. The weights are fitted to positions from
engine games, sampled so that 1-0, ½-½ and 0-1 are equally likely, with the game result as
the only target. No Stockfish or other engine evaluations are used, not even the ones in PGN
comments. Inside M6 the weights are no longer frozen, and every weight set has a version
number.

## Method: Texel tuning

Every position gets the result of the game it comes from, r ∈ {1, ½, 0} from white's view.
The eval is mapped to an expected score E = σ(eval / K), and the weights minimise
Σ (E − r)² over the training positions. K is fitted once, on `WEIGHTS_OG`, and then held
fixed. The loop runs only the static eval, never a search: at about 1 µs per eval, one pass
over 1.5M positions takes seconds. Every position informs every weight it touches. That is
where the speed comes from compared with tuning by games, where a whole game yields one
result for all ~800 weights together.

Positions are **quiet**, by our own engine: not in check, and quiescence score equal to the
static eval. The fit compares the static eval with the result, and that comparison only
makes sense where no capture is pending.

The fit only says that the eval predicts results better. Whether it plays better is decided
once, at the end, by a match.

## Game source: CCRL 40/15

The whole archive (`CCRL-4040.[2458744].pgn.7z`, 453 MB, 2.7 GB unpacked, downloaded
2026-10-04 from computerchess.org.uk/ccrl/4040/games.html), headers only, by the two
engines' mean Elo:

| Mean Elo | Games | 1-0 | ½-½ | 0-1 | 0-1 games |
|---|---|---|---|---|---|
| 1250–1499 | 6,202 | 44.0% | 19.1% | 36.9% | 2,288 |
| 1500–1749 | 12,015 | 42.5% | 19.3% | 38.2% | 4,586 |
| 1750–1999 | 37,174 | 42.1% | 20.8% | 37.1% | 13,791 |
| 2000–2249 | 95,601 | 40.9% | 24.3% | 34.8% | 33,307 |
| 2250–2499 | 198,473 | 39.1% | 28.6% | 32.3% | 64,146 |
| 2500–2749 | 337,527 | 37.0% | 33.8% | 29.2% | 98,571 |
| 2750–2999 | 425,692 | 34.1% | 41.1% | 24.8% | 105,442 |
| 3000–3249 | 469,543 | 30.2% | 50.3% | 19.5% | 91,619 |
| 3250–3499 | 466,720 | 24.5% | 62.5% | 13.0% | 60,821 |
| 3500+ | 407,814 | 12.6% | 84.0% | 3.4% | 13,722 |
| all | 2,458,744 | 29.2% | 50.9% | 19.9% | 488,664 |

(1,983 games have no Elo tag.) Black wins are the scarcest result in every band, so they set
the size. Results also depend on the Elo gap (55.6% draws at a gap under 100, 13.5% at
400–499) and on length (93.7% draws under 40 plies: early agreed draws, which the filters
drop).

**Ascanius's rule: only very good engines, both rated 3000 or more.** Keeping only the games
where the *weaker* engine is ≥3000, with at least 40 plies:

| Weaker engine | Games | 1-0 | ½-½ | 0-1 |
|---|---|---|---|---|
| 3000–3249 | 482,384 | 143,816 | 247,351 | 91,217 |
| 3250–3499 | 446,849 | 103,265 | 293,711 | 49,873 |
| 3500+ | 343,508 | 36,052 | 299,593 | 7,863 |
| all (≥40 plies) | 1,263,583 | 282,932 | 831,747 | 148,904 |

The 149k black wins set the size: 3 × 149k ≈ **447k balanced games**. At two or three
positions per game, that is 0.9–1.3M positions for ~800 parameters, enough for Texel
tuning. If more is wanted, CCRL Blitz is the next source, under the same filter.

Candidates considered:

| Archive | Games | Draws | Strength | Access |
|---|---|---|---|---|
| CCRL 40/15 | 2.46M (2026-10) | 50.9% | wide, from weak hobby engines to 3650 | one 453 MB `.7z`, bare PGN with both Elos |
| CCRL Blitz (2'+1") | 2.13M | 46.4% | wide | one 416 MB `.7z` |
| TCEC (all seasons) | not counted (far fewer) | very high in the upper leagues | top engines only | GitHub releases, CC BY-SA 3.0 |

**Chosen: CCRL 40/15, both engines ≥3000.** Every CCRL game is engine against engine (no
humans), at a fixed time control, with an Elo tag on both sides, which the strength filter
and the balancing need. At 40/15 the games are cleaner than in Blitz. TCEC is all top
engines but too small and too drawish: balancing to one third draws would throw away most
of its games. Blitz is the reserve.

The PGNs are only read locally to make the dataset. They are not committed to the repo:
nothing under `data/` is.

## Balanced sampling

Ascanius's rule: 1-0, ½-½ and 0-1 equally likely. Since black wins are the rarest result
(11.8% of the games with both engines ≥3000), they set the size. Every black-win game that passes the filters is
used, and the 1-0 and ½-½ games are subsampled to the same count.

- **Balance within each Elo band** (mean Elo of the two engines, 250 wide), not only
  overall. Draws come mostly from strong engines and wins from weaker ones. Balancing only
  overall would let "looks like a strong-engine game" stand in for "draw", and a position's
  eval should not learn who was playing it.
- **A fixed number of positions per game** (a few, spread over the game), so long games
  don't outweigh short ones and the classes stay equal in positions as well as games.
- **Filters**: skip the first 24 plies (CCRL books are up to 12 moves), keep >5 pieces
  (below that the tablebases decide, not the eval), drop games with fewer than 40 plies.
  Optionally drop pairs with an Elo gap over 400.
- **Split by game** into train/valid/test (80/10/10), as `tools/nne_data` does, so no game
  has positions on both sides of the split.

## The weights today: what the tuner can move

`basic_eval()` = material + piece-square tables + king safety + pawn structure + mobility +
piece activity + tempo.

| Term | Where its numbers live | In the weights? | Shape |
|---|---|---|---|
| `material_eval()` | `W.piece_value[6]` | yes | **nonlinear**: (white − black) × √(2 − 281/material_left); ×1.40 with all material, down to ×1.27 in K+P vs K (kings count 350 each in material_left) |
| `piecetable()` | `W.piece_table_value_{opening,endgame}[7][64]` | yes | linear, phase-weighted by OW/39 and EW/39 |
| `pawn_struckture_eval_of_colour()` | 4 `W.punishment_*` / `pawn_supporting_value` | yes | linear |
| `king_safety_eval()` | ~30 `constexpr` in `src/king_safety.cpp` | **no** ("WEIGHTS is frozen") | shelter and storm linear; attack danger **nonlinear** (danger² / 5000) |
| mobility | `5 *` literal in `basic_eval()` | **no** | linear |
| `piece_activity_eval()` | ~17 literals (30, 15, 20, 40, …) | **no** | linear |
| `tempo_eval()` | `TEMPO_OPENING = 24`, `TEMPO_ENDGAME = 9` | **no** | linear |

*That was the eval at #83. Since #90 (eval version 4) every term above is in the weights,
and all but king danger are linear:*
- *One game phase, `game_phase()`, blends every opening/endgame pair: knight and bishop 1,
  rook 2, queen 4, both sides, pawns not counted, 24 = all pieces on.*
- *Material is `piece_value` (opening) and `piece_value_endgame` (no √).*
- *The mobility line and the activity "square" weights became per-piece mobility tables
  (`mobility_{knight,bishop,rook,queen}_{opening,endgame}`, by the attacked squares no own
  piece stands on) inside `piece_activity_eval()`, whose `/2` is gone.*
- *The queen has its own activity weights.*
- *Passers have `passed_pawn_value_opening` beside the endgame `passed_pawn_value`.*
- *`pawn_supporting_value` is gone (`activity_pawn_defend` counts the same thing).*
- *The king shelter is scaled by the enemy's pieces in phase units.*

**Rank-folded piece-square tables.** Black's non-pawn pieces read white's table *at the same
square, not the rank-flipped one* (`piecetable()`, also `gui/eval_split.hpp` and
`src/plan_eval.cpp`). The eval only stays colour-symmetric because every non-pawn table is
rank-symmetric: checked in #83, all 0 of 64 squares differ from their rank mirror in both
phases. A tuner held to that can't make the king prefer its own back rank, or value a knight
on the 6th above one on the 3rd. Reading black's tables rank-flipped (`sq ^ 56`) gives the
same eval with today's values, and frees the tables. The separate black-pawn row (index 6)
is today exactly the rank mirror of the white one, so it can go too.

**Unused fields**: `skip_depth_decrease_threshold`, `value_of_attacked_square`,
`check_value`, `offensive_value`, `defensive_value` and `king_safety_value` feed only the old
`king_safety_of_colour()` or nothing at all, not `basic_eval()`.
`value_of_king_safety_for_sorting` feeds only move ordering. None of them is tuned.

**Outside the eval**: `W.piece_value` also orders captures (`ascaniusfish.hpp:79`), and
`sorting_eval()` uses the material, the tables and piece activity. New weights therefore
change move ordering and `bench` as well as the eval.

**Degeneracy**: adding a constant to every square of a piece's table is the same as
changing its piece value. Pin one of the two (e.g. hold the pawn value at 100 and the mean
of each table at 0), or regularise toward the current values.

About 740 table entries (with pawns only on ranks 2–7) plus about 60 scalars, so ~800
parameters, nearly all linear.

**Missing terms**: there is no passed-pawn, bishop-pair or rook-on-open-file term. Tuning
can't create them, but once the tuner exists each one is a cheap addition: write the term
and let the fit set its value.

## Versioned weights

A weight set is a text file that names its version (and parent version, data set, tuner
commit, fit loss). `WEIGHTS_OG` becomes set 1, with today's values. The default set is
compiled in from a generated header, so a new version doesn't mean another edit to the
Ascanius-owned `src/Weights.cpp`. UCI `WeightsFile` loads another set, for matches and the
tuner. The provenance line and the `uci` answer carry `weights N`. Changing the default set
still bumps `EVAL_VERSION` (an existing rule, for the PTT): the eval version says scores
changed, and the weights version names the set that did it.

## The net

The NNE is trained on `label − static`. New weights change `static` and the search labels,
so a set that becomes the default needs `make nne-retrain` (docs/NNE_RELABEL.md,
~1.5–2 h). The tuner fits `basic_eval()` with the net off. The match is played twice: net off
(the weights alone), and net on with a net retrained on the new weights.

## Issues

1. #83: this plan.
2. #85 **Every eval number is a weight in one versioned set**: move the constants into
   `WEIGHTS`, read black's tables rank-flipped, versioned weight files, `WeightsFile`.
   `bench` unchanged.
3. #86 **A balanced position set from 3000+ engine games**: `tools/tune_data`, CCRL 40/15 to
   train/valid/test with one third per result.
4. #87 **The tuner fits the weights to game results**: `tools/tune`; K, linear coefficients,
   nonlinear terms, a new weight set with the next version.
5. #88 **Tuned weights beat WEIGHTS_OG in a match**: net off and net on (retrained). If they
   win, they become the default.
6. #89 **The eval knows passed pawns, the bishop pair and open files**, each fitted by the
   tuner and kept only if a match shows a gain. Comes after 5.

#85 and #86 are independent and can run in parallel; #87 needs both.

#4 (colour-symmetric eval) predates this plan. `diagnostics/king_safety_test.cpp` already
checks `basic_eval()` for mirror symmetry. Issue 2 keeps that test passing.
