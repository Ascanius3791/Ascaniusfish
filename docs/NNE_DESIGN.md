<!-- OWNERSHIP=Claude -->
# M9: the eval-correction net (NNE)

Agreed between Ascanius and Claude in #49 (2026-09-30). The idea is Ascanius's:
a small net looks at the board and predicts how far the static eval is off from
what a depth-d search finds, and the engine adds that correction at the leaves.
The issues that build it are listed at the end.

## Target

- The net predicts the **correction** `c = search_d(pos) − static(pos)`, where
  `static` is `eval()` and `search_d` is the root score of a depth-d search.
- **d = 6** to start. It can change later: the data keeps the positions, so a
  new d only means relabelling, not collecting new games.
- The label search runs on its own, with a fresh TT for every position. The
  games' own searches would give a deeper and history-dependent number instead.
  Depth 6 costs 6–38 ms per position here, and depth 10 costs 250–500 ms
  (`go depth`, startpos and two middlegames, commit 82ba3d1).
- **Quiet positions only**: not in check, and the quiescence score
  (`minimax_tactical`) equals the static eval. Then `c` holds no "a piece hangs"
  noise that the quiescence search would find anyway, and the data looks like the
  place the net is used (the quiet leaf, see "The net in the engine").
- Positions whose label is a mate or tablebase score are dropped.

## Inputs (780)

The board is always seen from the side to move. When black is to move, the ranks
are flipped and the colours swapped, and the net's output is negated back to
white's view (evals in the engine are white-positive). Positions with either side
to move are then one kind of data, and the side to move needs no input.

| Inputs | Count | Meaning, from the mover's side |
|---|---|---|
| piece × colour × square | 768 | 6 piece types × {own, opponent} × 64 squares |
| castling | 4 | own kingside, own queenside, opponent kingside, opponent queenside |
| en-passant file | 8 | the file of the en-passant square, if any (flipping the ranks keeps it) |

The feature code exists **once**, in the engine (`lib/nne.hpp`). The data tool
writes each position's active inputs into the dataset, so the trainer never
computes features itself and cannot drift from the engine.

## Architecture

`780 → 128 → 16 → 1`, clipped ReLU after both hidden layers, output in
centipawns (mover's view), about 100k parameters. A hidden size of 256 is the
fallback if 128 underfits.

## Loss

The net is trained in two runs (#54):

1. `log(|static + c_pred − label| + ε)` with **ε = 50 cp**, all layers. Near
   zero that is roughly linear in the error; beyond 50 cp it grows
   logarithmically, so a few large errors cannot dominate. (A tiny ε would make
   the gradient `1/(|r|+ε)` explode at small errors, so the net would fit a few
   points exactly.) The reason first given here, king-safety evals of up to
   ±300000 cp, has been gone since #42 (|static| p99 2290 cp, max 8477); the log
   loss stays because it plays better, not because of that tail.
2. Only the **last layer refitted** under Stockfish's expected-score loss
   `|E(static + c) − E(label)|^2.5`, with
   `E(x) = ½·(1 + σ((x−o)/s) − σ((−x−o)/s))` fitted to 400 of our own games at
   5+0.05 by maximum likelihood over W/D/L (`tools/wdl_fit`): **o = 219.8 cp,
   s = 267.7 cp**. This gains +22.6 ± 18.7 Elo over step 1 alone (1000 games).

Trained from scratch, the expected-score loss is no better (s: +16.5 ± 30.4,
2s: −13.0, s/2: −18.3, 400 games each): it weights the few positions where E
moves a lot and leaves the corrections of clearly won or lost positions, where E
is flat, nearly untrained (test median error 71–76 cp against 58). Numbers:
`docs/measurements/nne_wdl_loss_2026-10-01.md`.

## Data

- **Source: Lichess rated games**, one monthly dump from
  `database.lichess.org` (June 2014: 182 MB `.pgn.zst`; June 2016: 1.1 GB if
  more is needed). A C++ tool reads it through `zstdcat`; nothing is unpacked.
- **Game filter**: both players rated 1500 or more, no bullet, at least 50 plies.
- **One position per game**: a random ply after the book moves, then the
  first quiet position from there on. Positions of one game are never in the data
  twice. A second month is taken if one month is not enough, never more positions
  per game.
- Duplicate positions across the whole set are removed.
- **Volume**: 500k positions first. The trainer's learning curve (25/50/100% of
  the training set) shows whether more would help.
- Each record: FEN, the active inputs, static eval, quiescence score, label,
  and the source game.
- Moves are read with the SAN matching of `gui/move_tree.hpp` (every legal move
  named with our `san()` and compared), so no second SAN parser is written.
- The dataset and the dump are not committed.

The data is human positions, not the positions our search visits. Search
leaves are full of odd, unbalanced positions too, so the variety should help; if
the net underperforms, adding self-play positions is one of the things to try.

## Train / validation / test

**80 / 10 / 10 by game.** With one position per game, a game is a position,
so nothing near-identical crosses the split. Validation decides early stopping;
test is used only for the reported numbers.

## Training toolchain

- **Pure C++ against libtorch**, the one inside the installed torch wheel
  (2.7.1+cu118, `_GLIBCXX_USE_CXX11_ABI=1`), trained on the GPU (MX350, 2 GB).
  No Python.
- Checked with a small trainer (780→128→16→1, Adam, the loss above): it compiles
  in 28 s with a **1.25 GB** memory peak, so it must build alone, never next to an
  engine build; it runs in 490 MB.
- Positions are kept as lists of active inputs (about 40 bytes each) and expanded
  per batch, so 500k positions take about 20 MB.
- The trainer exports the weights to a flat binary file (magic, version, layer
  sizes, float32 little-endian), about 400 kB, which is committed. It also writes
  its predictions on the test set, so the engine can be checked against it.
- The trainer is `tools/nne_train` (#51, `make tools/nne_train`). The weights
  are `nets/nne_d6.bin`, the test predictions `nets/nne_d6_test.tsv` (game, FEN,
  static and label in white's view, the net's correction in the mover's view).
  The file's layout, all little-endian: `"NNE1"`, uint32 version 1, uint32 layer
  count 3, uint32 sizes `780 128 16 1`, then per layer float32 weights
  `[in][out]` (one contiguous row of 128 per active input in the first layer)
  and float32 bias `[out]`. The last layer is already scaled to centipawns, so
  `c = b3 + clamp(b2 + clamp(b1 + Σ W1[active], 0, 1)·W2, 0, 1)·W3`.
- Training: AdamW with weight decay 1 (best on the validation set among
  0–3), batch 1024, learning rate 1e-3 halved after 3 epochs without a better
  validation loss, stopped after 8. Numbers:
  `docs/measurements/nne_train_2026-10-01.md`. The loss's second step:
  `tools/nne_train loss=wdl init=<step-1 net> train=last` (see "Loss").

## The net in the engine

- The engine **never links libtorch** (1.6 GB of libraries per process would
  break the several-engines-at-once budget). Inference is hand-written in
  `lib/nne.hpp` / `src/nne.cpp` (Claude-owned) and reads the exported file.
- **Integers, quantized at load** (#55; #52's float32 version cost the search
  about 16% per node, `docs/measurements/nne_nps_2026-10-01.md`). The file stays
  float32, and `nne::load()` picks every scale from the weights it reads, so
  any net of this shape works without retuning. Layer 1 is int16 with one
  scale per unit, the largest that no position can overflow; layer 2 is int16
  × int16 into int32 (`pmaddwd`), again with the largest safe scale; b2, layer
  2's clipped ReLU and layer 3 are float. Integer sums are exact, so AVX2 (chosen
  at run time, the build flags stay SSE2) and the generic code give the same
  bits on every machine, and the first layer can be **updated rather than
  recomputed**: each thread keeps the last position's sums per side to move, the
  inputs as a 13-word bitset, and a leaf adds and subtracts only the rows whose
  bits differ (4.8 rows on average in `make bench`, 3.7% of leaves from scratch).
  Nothing is added to `BB`, `make_move` or the search (all Ascanius-owned), and a
  per-ply stack fed by the search would save about one more row per leaf.
  Cost and exactness: `docs/measurements/nne_int_2026-10-01.md`. `make nne-test`
  checks the engine against the trainer's test-set predictions (≤ 1 cp), the
  updated layer against one from scratch and AVX2 against the generic code;
  `NNE=nets/nne_d6.bin` on the bench target searches with the net on.
- **Where**: the quiet leaf of `minimax_tactical` (`ascaniusfish_2.hpp`, the
  `eval(original, W, 0)` after the TT probe) returns `static + correction`. Stand
  pat keeps the raw static eval, since non-quiet positions are not in the data.
  This is a patch to an Ascanius-owned file, applied with Ascanius's approval
  (#52). The sum is clamped so it can never reach the mate or tablebase bands.
- **UCI options** `NNEFile` (path) and `UseNNE`, so one binary plays both sides
  of a match. `UseNNE` defaults to true since #53 showed the gain; a match against
  the net-less engine passes `UseNNE=false` to that side. The default is applied
  at the first `isready`/`go` unless a `setoption` came first. `make bench` runs
  without the net unless given `NNE=`, so its signature is unchanged. The GUI's
  gear has a switch for it (on by default).
- **CPU, not GPU.** Alpha-beta asks for one eval at a time, and each depends on
  the cutoffs before it. A GPU call costs microseconds of launch and PCIe transfer
  against about 100 ns on the CPU, and a CUDA context costs hundreds of MB per
  engine process. The GPU pays off for large batches, which is training.

## How success is measured

- **Offline**, on the test set: the corrected eval `static + c_pred` beats the
  baseline `static + mean correction` (the training set's mean, a constant) on
  the median `|label − prediction|` and on the mean of that error clipped at
  1000 cp.
- **Engine**: the engine's correction equals the trainer's on every test position
  within 0.01 cp, and the nps cost with the net on is measured.
- **Strength, the milestone's point**: `tools/match`, the same binary with
  `UseNNE` off (A) and on (B), `tc=5+0.05`, `tools/openings.epd`. Done when
  Elo B−A is above 0 with its 95% interval. Both sides have the same clock, so any
  nps cost counts against the net.

## Variants tried (#53)

| Variant | Games | Elo B−A (95% CI) |
|---|---:|---|
| d = 6, hidden 128, 426k Lichess positions, correction at the quiet leaf only | 200 | **+79.5 ± 45.8** [+35.1, +126.7] |

The first variant already gains, so the fallbacks (the correction at stand pat
too, hidden size 256, another d, self-play data) were not tried. Numbers:
`docs/measurements/nne_elo_2026-10-01.md`.

The loss (#54), against the log-loss net above, 5+0.05:

| Variant | Games | Elo B−A (95% CI) |
|---|---:|---|
| expected-score loss from scratch, s = 267.7 | 400 | +16.5 ± 30.4 |
| … 2s | 400 | −13.0 ± 30.0 |
| … s/2 | 400 | −18.3 ± 29.1 |
| **log-loss net, last layer refitted with the expected-score loss at s** | 1000 | **+22.6 ± 18.7** [+4.0, +41.4] |

The last is `nets/nne_d6.bin` since #54.

## Issues

- #50: 500k labelled quiet positions from Lichess games (data tool, labels, split).
- #51: a net predicts the depth-6 correction on unseen games (trainer, export).
- #52: the engine evaluates quiet leaves with the net (inference, UCI options,
  the patch to `ascaniusfish_2.hpp`).
- #53: the net gains Elo at 5+0.05 (the match, and what to try if it doesn't).
- #54: the loss chosen by Elo (Stockfish's WDL curve fitted to our games,
  the last layer refitted under it).
