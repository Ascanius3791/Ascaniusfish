#!/usr/bin/env bash
# OWNERSHIP=Claude
#
# tools/nne_probe/cloud_check.sh [table_dir]: can a machine label NNE data
# exactly like ours (#56)? Relabels the 10040 positions of sample.tsv (every
# dataset position whose game index is a multiple of 53, labelled at the
# dataset's commit) with `tools/nne_data relabel`, one worker per CPU, and
# compares every column with the sample. Run from the repo root.
#
# Without table_dir the 3-4-5 WDL tables (145 files, 379 MB) are downloaded
# from tablebase.lichess.ovh into ~/syzygy-wdl; DTZ is not needed, since every
# labelled root has 6+ pieces. On this repo's machine: 118 s wall with 4
# workers, all 10040 equal.
set -euo pipefail

tables=${1:-$HOME/syzygy-wdl}
url=https://tablebase.lichess.ovh/tables/standard/3-4-5-wdl
out=${OUT:-/tmp/nne-probe}

if [ $# -eq 0 ] && [ "$(ls "$tables"/*.rtbw 2>/dev/null | wc -l)" -ne 145 ]; then
    mkdir -p "$tables"
    echo "downloading the WDL tables into $tables ..."
    start=$(date +%s)
    curl -fsS "$url/" | grep -o 'href="[^"]*\.rtbw"' | sed 's/href="//; s/"$//' \
        | xargs -P 8 -I{} curl -fsS -o "$tables/{}" "$url/{}"
    echo "tables: $(ls "$tables"/*.rtbw | wc -l) files, $(du -sh "$tables" | cut -f1), $(( $(date +%s) - start )) s"
fi

make tools/nne_data >/dev/null
jobs=$(nproc)
echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//'), $jobs threads"

rm -rf "$out"
./tools/nne_data relabel tools/nne_probe/sample.tsv jobs="$jobs" out="$out" syzygy="$tables" 2>&1 \
    | grep -E 'positions to relabel|workers done|label search time|no tables'

for s in train valid test; do tail -n +2 "$out/$s.tsv"; done | sort > "$out/got.tsv"
tail -n +2 tools/nne_probe/sample.tsv | sort > "$out/expected.tsv"
equal=$(comm -12 "$out/expected.tsv" "$out/got.tsv" | wc -l)
echo "equal: $equal of $(wc -l < "$out/expected.tsv") positions (all columns)"
if [ "$equal" -ne "$(wc -l < "$out/expected.tsv")" ]; then
    echo "first differences (expected, then got):"
    diff <(cut -f2,4-7 "$out/expected.tsv" | sort -n) <(cut -f2,4-7 "$out/got.tsv" | sort -n) | head -10
    exit 1
fi
