<!-- OWNERSHIP=Claude -->
# NNE relabel: a Claude cloud session labels exactly like this machine (#56)

Commit 9d2a1be (`tools/nne_data relabel`, built by `make tools/nne_data`, TT_EXPONENT=13,
the same flags on both machines, no `-march=native`), labels with the net off.

```
tools/nne_probe/cloud_check.sh              # cloud: downloads the WDL tables first
tools/nne_probe/cloud_check.sh ~/syzygy-nr  # here
```

The script relabels `tools/nne_probe/sample.tsv`, the 10040 positions of the
#50 dataset whose game index is a multiple of 53, with one worker per CPU, and
compares every column with the dataset's.

| machine | workers | wall | label time, summed | ms per position | equal |
|---|---:|---:|---:|---:|---:|
| i7-1065G7 (4 cores / 8 threads, WSL) | 4 | 118 s | 459 s | 45.7 | 10040 of 10040 |
| i7-1065G7 | 8 | 96 s | 741 s | 73.8 | 10040 of 10040 |
| cloud: Xeon @ 2.10GHz, 4 vCPU | 4 | 44 s | 167 s | 16.6 | 10040 of 10040 |

A cloud vCPU labels 2.7 times as fast as one of 4 local workers. Here 8 workers
are only 23% faster than 4, since 4 of them are hyperthreads.

The cloud session: Ubuntu 24.04, g++ 13.3.0, 4 vCPU, 15 GB RAM, network set to
a Custom allowlist of `tablebase.lichess.ovh` plus the default package managers.
Downloading the 3-4-5 WDL tables (145 files, 379 MB) took 19 s, and the whole
script well under 10 minutes. DTZ files are not needed: every labelled root has
6 or more pieces.

## The whole dataset (532,902 positions), projected

- Cloud: 532,902 × 16.6 ms = 2.5 CPU-hours, so about 37 min on one 4-vCPU
  session: two chunks of `minutes=25`.
- Here: 85 positions/s on 4 workers, 105 on 8, so 85–105 min.

## Since then

3978e48 shortened relabel's part lines to index, status and scores (26 bytes a
position, 14 MB for the whole dataset); the merge computes the features. Rechecked
here with 8 workers (82 s wall, 637 s summed): 10040 of 10040 equal. A trial of
the cloud route on this machine (`tools/nne_cloud_relabel.sh` in a clone, the
sample stopped after 0.4 min and resumed, then `tools/nne_retrain.sh from=`) merged
all 10040 equal to the sample. Its spot check of 1045 positions passed, and so
did the inference check of the net trained on them.
