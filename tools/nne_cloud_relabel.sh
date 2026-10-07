#!/usr/bin/env bash
# OWNERSHIP=Claude
#
# tools/nne_cloud_relabel.sh: one chunk of an NNE relabel in a Claude cloud
# session (#56, docs/NNE_RELABEL.md). Run from the repo root, on a branch whose
# code is the commit to label with. Each call:
#   1. gets what is missing: the 3-4-5 WDL tables (tablebase.lichess.ovh, into
#      ~/syzygy-wdl), zstd, and data/nne_fens.tsv.zst (branch nne-fens);
#   2. labels for at most MINUTES (default 25) into nne-labels/, 4 workers;
#   3. commits nne-labels/parts and pushes the branch, so a reclaimed VM loses
#      nothing: a new session on the pushed branch carries on from it.
# Prints "relabel: done" when every position is labelled, else "relabel:
# stopped" (exit 3): run it again. The merged dataset it also writes is not
# committed; the local side merges the parts itself (tools/nne_retrain.sh from=).
set -euo pipefail

minutes=${MINUTES:-25}
jobs=4                  # fixed: the parts of a run are tied to its worker count
tables=${TABLES:-$HOME/syzygy-wdl}
url=https://tablebase.lichess.ovh/tables/standard/3-4-5-wdl
fens=${FENS:-data/nne_fens.tsv.zst}
out=nne-labels
push=${PUSH:-1}         # PUSH=0 only commits (a local trial run)

if [ "$(ls "$tables"/*.rtbw 2>/dev/null | wc -l)" -ne 145 ]; then
    mkdir -p "$tables"
    echo "downloading the WDL tables into $tables ..."
    curl -fsS "$url/" | grep -o 'href="[^"]*\.rtbw"' | sed 's/href="//; s/"$//' \
        | xargs -P 8 -I{} curl -fsS -o "$tables/{}" "$url/{}"
    echo "tables: $(ls "$tables"/*.rtbw | wc -l) files, $(du -sh "$tables" | cut -f1)"
fi
if ! command -v zstdcat >/dev/null; then
    # A fresh container may have no package lists yet.
    { apt-get update -q && apt-get install -y zstd; } >/dev/null 2>&1 \
        || { sudo apt-get update -q && sudo apt-get install -y zstd; } >/dev/null
fi
if [ ! -s "$fens" ]; then
    mkdir -p data
    git fetch -q origin nne-fens
    git show FETCH_HEAD:nne_fens.tsv.zst > "$fens"
fi

# The code commit is fixed once the first chunk starts: later chunks must be
# labelled by the same engine, whatever has been committed on top since.
mkdir -p "$out"
if [ ! -s "$out/commit.txt" ]; then
    git rev-parse HEAD > "$out/commit.txt"
    printf '/*.tsv\n/summary.txt\n' > "$out/.gitignore"
fi
commit=$(cat "$out/commit.txt")
if ! git diff --quiet "$commit" -- . ":(exclude)$out"; then
    echo "the code differs from $commit, which labelled $out/parts" >&2
    exit 1
fi

make tools/nne_data >/dev/null
echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//'), $(nproc) threads; code $commit"
status=0
./tools/nne_data relabel "$fens" out="$out" jobs="$jobs" minutes="$minutes" syzygy="$tables" 2>&1 \
    | grep -vE '^worker 0:' || status=${PIPESTATUS[0]}
if [ "$status" -ne 0 ] && [ "$status" -ne 3 ]; then
    exit "$status"
fi

done_count=$(cat "$out"/parts/part-*.tsv | wc -l)
git add "$out/parts" "$out/commit.txt" "$out/.gitignore"
git commit -q -m "nne labels: $done_count positions by $commit" || true
[ "$push" = 0 ] || git push -q -u origin HEAD
if [ "$status" -eq 3 ]; then
    echo "relabel: stopped at $done_count positions, committed; run again to carry on"
    exit 3
fi
echo "relabel: done, $done_count positions, committed to $(git rev-parse --abbrev-ref HEAD)"
