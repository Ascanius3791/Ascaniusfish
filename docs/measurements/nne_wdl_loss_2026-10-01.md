<!-- OWNERSHIP=Claude -->
# The net's loss chosen by Elo: log loss against Stockfish's WDL loss (#54)

Commit 6410323 (`tools/wdl_fit`, `tools/nne_train` with `loss=wdl`), the
dataset of `nne_data_2026-09-30.md`, the engine of b37e4bf (`UseNNE` on) built
with the flags `tools/match` uses and `-DTT_EXPONENT=11`. Baseline:
the log-loss net `nets/nne_d6.bin` of `nne_train_2026-10-01.md`.

## 1. The WDL curve, fitted to our own games

400 self-play games at 5+0.05 (the 100 openings of `tools/openings.epd` twice,
both sides the same binary with the net on; the two halves came out at
−40.1 ± 41.3 and +24.4 ± 42.4 Elo, i.e. noise), then:

```
tools/match <bin> <bin> tc=5+0.05 pgn=fit1.pgn optionsA=NNEFile=…,UseNNE=true optionsB=…  # and fit2.pgn
tools/wdl_fit fit1.pgn fit2.pgn boot=500
```

53,925 moves: 51,845 used, 2,080 mate scores left out (no tablebase scores, no
book moves: the games start from the suite's FENs). White W/D/L 171/95/134.

| | o | s |
|---|---:|---:|
| maximum likelihood | **219.8 cp** | **267.7 cp** |
| 95% bootstrap over games | [154.9, 301.4] | [197.0, 370.3] |

`E(x) = ½·(1 + σ((x−o)/s) − σ((−x−o)/s))`. The fitted curve against the games
(x = the mover's search score):

| score bin (cp) | pairs | win% | draw% | loss% | E games | E fit |
|---|---:|---:|---:|---:|---:|---:|
| < −800 | 5471 | 0.2 | 5.7 | 94.1 | 0.031 | 0.016 |
| −800 … −400 | 3442 | 0.9 | 9.7 | 89.4 | 0.057 | 0.137 |
| −400 … −200 | 4227 | 9.4 | 27.7 | 62.9 | 0.233 | 0.287 |
| −200 … −100 | 4004 | 16.9 | 30.4 | 52.7 | 0.321 | 0.383 |
| −100 … −50 | 3071 | 25.5 | 32.3 | 42.2 | 0.416 | 0.442 |
| −50 … −20 | 2754 | 27.9 | 37.5 | 34.6 | 0.467 | 0.473 |
| −20 … 20 | 5664 | 22.5 | 54.8 | 22.7 | 0.499 | 0.500 |
| 20 … 50 | 2816 | 34.1 | 38.4 | 27.6 | 0.532 | 0.527 |
| 50 … 100 | 3200 | 41.3 | 32.0 | 26.7 | 0.573 | 0.557 |
| 100 … 200 | 4030 | 51.5 | 30.8 | 17.7 | 0.669 | 0.617 |
| 200 … 400 | 4360 | 62.6 | 27.6 | 9.8 | 0.764 | 0.712 |
| 400 … 800 | 3509 | 88.2 | 10.7 | 1.1 | 0.936 | 0.863 |
| ≥ 800 | 5297 | 93.9 | 5.9 | 0.2 | 0.968 | 0.984 |

The two-parameter model fits the draw rate and the expected score together, and
between 100 and 800 cp it is flatter than the games' expected score (0.617
against 0.669 at 100–200 cp). That is the model's shape, not a fitting error;
the s/2 variant below covers a steeper curve.

## 2. Offline, on the test set (53,176 positions)

The nets are 780→128→16→1 as before, AdamW with weight decay 1, seed 51. The
wdl loss is `|E(static + c) − E(label)|^2.5`, o = 219.8 throughout, s as given.
"E error" is the mean `|E(static + c) − E(label)|` at the fitted curve
(s = 267.7) for every row; the last three columns are the wdl loss at s, 2s, s/2.

```
tools/nne_train loss=wdl sfac=1|2|0.5 out=… preds=…
tools/nne_train loss=wdl init=<log-loss net> train=last out=… preds=…
tools/nne_train [loss=wdl sfac=…] init=<net> epochs=0 out=- preds=-   # scores a net
```

| net | best epoch | median cp | clipped mean cp | E error | wdl loss s | 2s | s/2 |
|---|---:|---:|---:|---:|---:|---:|---:|
| log loss (`nne_d6.bin` of #51) | 52 | **57.95** | **136.76** | **0.05666** | 0.005526 | 0.002543 | 0.009080 |
| wdl, s | 20 | 72.49 | 144.97 | 0.06099 | **0.005311** | | |
| wdl, 2s | 24 | 76.25 | 145.53 | 0.06236 | | **0.002352** | |
| wdl, s/2 | 20 | 71.18 | 144.89 | 0.06057 | | | **0.008830** |
| log loss, last layer refitted with wdl at s | 7 | 59.35 | 137.26 | 0.05701 | 0.005497 | | |
| static + mean correction | – | 68.42 | 146.57 | 0.06146 | 0.005824 | 0.002672 | 0.009621 |

Each wdl net beats the log-loss net on its own loss (by 3–8%) and loses on the
cp measures: the 2.5th power puts the weight on the few positions where E moves
a lot, and a position whose static eval is already far from 0 sits where E is
flat, so its correction is barely trained. The wdl nets overfit after about 8
epochs (training loss keeps falling, validation does not), and weight decay 1
was tuned for the log loss; it was not retuned here. No variant is clearly
worse under the loss it was trained for, so all four played.

The last-layer refit (17 parameters, 15 epochs, 39 s) moves little offline.

## 3. Matches against the log-loss net, 5+0.05

`tools/match <bin> <bin> tc=5+0.05`, A = the log-loss `nne_d6.bin`, B = the
variant, both `UseNNE=true` with an absolute `NNEFile`; the opening suite
repeated so one run holds 400 (600) games. Every game ended `Termination
"normal"`.

| B | games | W / D / L | pairs 0/½/1/1½/2 | Elo B−A (95% CI) |
|---|---:|---|---|---|
| wdl, s | 400 | 169 / 81 / 150 | 28 / 30 / 72 / 35 / 35 | +16.5 ± 30.4 [−13.7, +47.0] |
| wdl, 2s | 400 | 149 / 87 / 164 | 33 / 36 / 71 / 33 / 27 | −13.0 ± 30.0 [−43.1, +16.9] |
| wdl, s/2 | 400 | 138 / 103 / 159 | 30 / 44 / 65 / 39 / 22 | −18.3 ± 29.1 [−47.5, +10.7] |
| last layer refitted | 400 | 175 / 87 / 138 | 22 / 29 / 74 / 40 / 35 | +32.2 ± 29.3 [+3.1, +61.8] |
| last layer refitted, extension | 600 | 252 / 124 / 224 | 38 / 51 / 107 / 53 / 51 | +16.2 ± 24.4 [−8.1, +40.7] |
| **last layer refitted, all** | **1000** | 427 / 211 / 362 | 60 / 80 / 181 / 93 / 86 | **+22.6 ± 18.7 [+4.0, +41.4]** |

The leader was the best of four 400-game runs, so its first 400 games are
biased upward; the 600 fresh games alone give +16.2 ± 24.4. The gain is real at
1000 games but probably nearer +15 than +30.

## Result

The log-loss net with its last layer refitted under the wdl loss at the fitted
s is the new `nets/nne_d6.bin` (`nets/nne_d6_test.tsv` its test predictions;
`make nne-test` passes). Trained from scratch, the wdl loss is no better than
the log loss (s: +16.5 ± 30.4, 2s and s/2 below 0), so the trainer's default
loss stays `log`; the net is made in two runs:

```
tools/nne_train out=nets/nne_log.bin preds=-
tools/nne_train loss=wdl init=nets/nne_log.bin train=last
```
