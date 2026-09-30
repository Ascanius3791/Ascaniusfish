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

`log(|static + c_pred − label| + ε)` with **ε = 50 cp**. Near zero that is
roughly linear in the error; beyond 50 cp it grows logarithmically, so the huge
king-safety evals (up to ±300000 cp) cannot dominate training. (A tiny ε would
make the gradient `1/(|r|+ε)` explode at small errors, so the net would fit a
few points exactly.)

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
  `docs/measurements/nne_train_2026-10-01.md`.

## The net in the engine

- The engine **never links libtorch** (1.6 GB of libraries per process would
  break the several-engines-at-once budget). Inference is hand-written in
  `lib/nne.hpp` / `src/nne.cpp` (Claude-owned) and reads the exported file.
- **Float32**. The first layer is recomputed at every eval: at most 32 + 5 active
  inputs × 128 ≈ 5k additions. Estimated at about 100 ns; measured (#52) at
  about 1.7k cycles, mostly waiting on the W1 rows from L2, which costs the
  search about 16% nps (`docs/measurements/nne_nps_2026-10-01.md`). Nothing is
  added to `BB` or `make_move` (both Ascanius-owned; `BB` is copied every ply).
  An incrementally updated first layer comes later, only if #53 shows the cost
  matters. `make nne-test` checks the engine against the trainer's test-set
  predictions; `NNE=nets/nne_d6.bin` on the bench target searches with the net on.
- **Where**: the quiet leaf of `minimax_tactical` (`ascaniusfish_2.hpp`, the
  `eval(original, W, 0)` after the TT probe) returns `static + correction`. Stand
  pat keeps the raw static eval, since non-quiet positions are not in the data.
  This is a patch to an Ascanius-owned file, applied with Ascanius's approval
  (#52). The sum is clamped so it can never reach the mate or tablebase bands.
- **UCI options** `NNEFile` (path) and `UseNNE` (default false), so one binary
  plays both sides of a match. With `UseNNE` off, `make bench` is unchanged.
- **CPU, not GPU.** Alpha-beta asks for one eval at a time, and each depends on
  the cutoffs before it. A GPU call costs microseconds of launch and PCIe transfer
  against about 100 ns on the CPU, and a CUDA context costs hundreds of MB per
  engine process. The GPU pays off for large batches, which is training.
- `lib/NNUE.hpp` / `src/NNUE.cpp` (Ascanius-owned, not wired in) stay untouched.

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

## Issues

- #50: 500k labelled quiet positions from Lichess games (data tool, labels, split).
- #51: a net predicts the depth-6 correction on unseen games (trainer, export).
- #52: the engine evaluates quiet leaves with the net (inference, UCI options,
  the patch to `ascaniusfish_2.hpp`).
- #53: the net gains Elo at 5+0.05 (the match, and what to try if it doesn't).
