<!-- OWNERSHIP=Claude -->
# Fits and SPRTs on GitHub Actions (#116)

Two workflows run the heavy tuning work on GitHub-hosted runners (4 vCPU, 16 GB, up to
20 jobs at once on a free account, 6 h per job):

- `tune` (`.github/workflows/tune.yml`): one `tools/tune` fit per option set, all in
  parallel.
- `sprt` (`.github/workflows/sprt.yml`): one SPRT of two weight sets, its pairs split over
  parallel jobs.

Both start only by hand (`workflow_dispatch`), never on a push or a pull request. The
repository is public, so **logs, step summaries and artifacts are public**. A run shows
the option sets, the weight files it fitted or played and every game.

`gh workflow run` finds a workflow only once its file is on the default branch (`main`).
`--ref <branch>` then runs that branch's version of it, with that branch's code.

## The data: a game pack as a release asset

The fits read a **game pack** (`lib/game_pack.hpp` has the format), not the PGN pool. It
holds only the selected games of a `tools/tune_data` run: every move as its rank among the
legal moves (1 byte), and a mark (split, ply, line number) for every position of
`train/valid/test.tsv` and of `chain.tsv`. There is no PGN or FEN in it. `tools/tune` replays
the games and gets the same positions. The fit from it is the same set, to the bit, as
the fit from the TSV files with the same options and `jobs=`.

| tune2 | Size |
|---|---|
| PGN pool the run read (`data/tune2/pool.pgn`) | 4.5 GB |
| `train/valid/test.tsv` + `chain.tsv` | ~1.3 GB |
| `games.pack`: 1,083,300 games, 3.06M split marks, 15.5M chain marks | 295 MB |
| `tune2.pack.zst` (zstd -19), the release asset | 138 MB |

Making it from a data directory with the split files (and `chain.tsv`) and the PGN the run
read, ~11 min. Every mark's position is checked against its FEN line, every field a FEN sets:

```bash
flock -w 3600 /tmp/ascaniusfish-heavy.lock ./tools/tune_data pack data/tune2/pool.pgn out=data/tune2 jobs=4
```

```bash
zstd -19 -T4 data/tune2/games.pack -o data/tune2/tune2.pack.zst
```

The pack goes up once, as an asset of the release `tune-data`:

```bash
gh release create tune-data data/tune2/tune2.pack.zst --title "Tuning data" --notes "Game packs for tools/tune (docs/CLOUD_RUNS.md)"
```

A newer data set is a new asset of the same release, e.g. `tune3.pack.zst`
(`gh release upload tune-data ...`), passed as `-f data=tune-data/tune3.pack.zst`.

Locally, `data=data/tune2/games.pack` fits from the pack too. With `chain=auto` it uses the
pack's chain marks. `keep=auto` keeps the chain positions of the fitted games in memory
(~1 GB for tune2) when that is less than half of MemAvailable, and otherwise replays them
every round. Both give the same set.

## tune: a matrix of fits

```bash
gh workflow run tune -f sets='td=0.97; td=0.9; td=0.97 lambda=0' -f from=weights/w10.txt
```

- `sets`: option sets for `tools/tune`, separated by `;`, one job each. `-` (the default)
  is one fit with the defaults. `data=`, `out=`, `jobs=`, `from=`, `compare=` and
  `spectrum=` belong to the workflow and are refused.
- `from`: the weight set to fit from, a path in the repository (default `weights/w10.txt`).
- `data`: `<release tag>/<asset>` (default `tune-data/tune2.pack.zst`).

Each job builds `tools/tune`, fits with `jobs=4` and uploads the artifact `tune-<k>`:
`set-<k>.txt` (the new set, with its fit report as `#` lines at the end), `log-<k>.txt`
(the whole output) and `command-<k>.txt`. The run's page shows every set's fit report as a
step summary. A fit is the same as a local one with `jobs=4` and the same data.

```bash
gh run download <run id> -n tune-2 -D out/tune-2
```

Artifacts are kept 30 days. A set worth keeping goes into the repository as the next
`weights/wN.txt`, like a local fit.

## sprt: one test split over jobs

```bash
gh workflow run sprt -f b=weights/w11.txt
```

```bash
gh workflow run sprt -f b=run:<tune run id>:2 -f net=on
```

- `a`, `b`: the weight sets. A is the reference (default `weights/w10.txt`), B the
  candidate. Each is a path in the repository, `default` (the compiled-in set), or
  `run:<id>:<k>`, the set of job k of a `tune` run (artifact `tune-<k>`, still within
  its 30 days).
- `net`: `off` (default) or `on`, `UseNNE` for both sides.
- `tc` (default `5+0.05`), `bounds` (default `0,10`, α = β = 0.05).
- `pairs`: **5000 game pairs in all** (default, Ascanius's cap), split as evenly as
  possible over `jobs` (default 20, i.e. 250 pairs per job).

Every job builds one engine (`-DTT_EXPONENT=17`, as `tools/match` builds a ref) that plays
both sides, set by `WeightsFile`. It runs `tools/match ... slice=<k>/<n>`, with
`concurrency` = cores − 1 (3 games at once). Job k plays the openings k, k+n, k+2n, … of
`tools/openings_ccrl.epd` (10,681 positions), so no opening is played twice in the whole test.
A job's two engines share one machine, so a slower runner slows both. A slice never stops
at an SPRT bound: an LLR looked at in 20 places is not α = 0.05. The `combine` job sums the
jobs' pentanomial counts and judges the SPRT **once, on the total**:

```bash
./tools/match combine sprt=0,10 stats-*.txt
```

It prints W/D/L, the pair counts, Elo ± 95% CI, the LLR and `H1 accepted`, `H0 accepted` or
`no decision`. That is the step summary and the artifact `sprt-result` (`result.txt`, kept
90 days). `sprt-<k>` has job k's games (`games-<k>.pgn`), log and `stats-<k>.txt`. A job
writes its stats file after every pair, and `combine` runs even when a job failed or hit
its time budget (`time=300` min), so a partial run still gives the total of what was
played. `combine` works locally on downloaded stats files too:

```bash
gh run download <run id> -p 'sprt-*' -D out/sprt
```

5000 pairs give an Elo CI of a few Elo, so most tests at [0, 10] decide. `no decision`
means the total landed near 5, the middle of the bounds. Then the change is dropped or
Ascanius decides, as with a local SPRT (docs/BENCHMARKS.md).
