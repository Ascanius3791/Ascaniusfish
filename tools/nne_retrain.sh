#!/usr/bin/env bash
# OWNERSHIP=Claude
#
# tools/nne_retrain.sh (make nne-retrain): after a change to eval(), relabel the
# NNE dataset's positions with the current engine, retrain the net on them and
# check the engine's inference against the trainer (#56, docs/NNE_RELABEL.md).
#
#   tools/nne_retrain.sh [from=<branch>] [jobs=6] [minutes=M] [syzygy=~/syzygy-nr]
#                        [fens=data/nne_fens.tsv.zst] [out=data/nne-relabel]
#                        [net=nets/nne_d6.bin]
#
# 1. relabel: tools/nne_data relabel of the FEN list into out/. With minutes=
#    it stops after M minutes (exit 3); the same command carries on. With
#    from=, the labels are a cloud session's (tools/nne_cloud_relabel.sh): its
#    pushed branch's nne-labels/parts are fetched into out/parts, merged here
#    (game, ply, FEN and features come from the FEN list) and spot-checked
#    against ~1000 positions labelled locally, all columns equal.
# 2. tools/nne_train data=out/ with the log loss, then its last layer refitted
#    with the WDL loss (#54) into net (the old net is kept as *.prev.*).
# 3. make nne-test's check, on that net and its test predictions.
# Labels are searched with the net off, as nne_data never loads one.
set -euo pipefail

from=""; jobs=6; minutes=""; syzygy=$HOME/syzygy-nr
fens=data/nne_fens.tsv.zst; out=data/nne-relabel; net=nets/nne_d6.bin
for a in "$@"; do
    case "$a" in
        from=*) from=${a#from=} ;;   jobs=*) jobs=${a#jobs=} ;;
        minutes=*) minutes=${a#minutes=} ;;   syzygy=*) syzygy=${a#syzygy=} ;;
        fens=*) fens=${a#fens=} ;;   out=*) out=${a#out=} ;;   net=*) net=${a#net=} ;;
        *) echo "unknown argument $a" >&2; exit 2 ;;
    esac
done
preds=${net%.bin}_test.tsv
check_every=${CHECK_EVERY:-509}  # the spot check of imported labels: index % this == 0
remote=${NNE_REMOTE:-origin}     # where from= is fetched (a path, for a local trial)

# The FEN list: from the #50 dataset when it is here, else from branch nne-fens.
if [ ! -s "$fens" ]; then
    mkdir -p "$(dirname "$fens")"
    if [ -s data/nne/train.tsv ]; then
        { printf '#game\tindex\tply\tfen\n'
          tail -q -n +2 data/nne/train.tsv data/nne/valid.tsv data/nne/test.tsv | cut -f1-4 | sort -t$'\t' -k2,2n
        } | zstd -19 -q -o "$fens"
    else
        git fetch -q origin nne-fens
        git show FETCH_HEAD:nne_fens.tsv.zst > "$fens"
    fi
fi

if [ -n "$from" ]; then
    git fetch -q "$remote" "$from"
    commit=$(git show FETCH_HEAD:nne-labels/commit.txt)
    rm -rf "$out/parts"
    mkdir -p "$out"
    git archive FETCH_HEAD nne-labels/parts | tar -x --strip-components=1 -C "$out"
    echo "labels of $from: $(cat "$out"/parts/part-*.tsv | wc -l) positions, labelled by $commit"
    if git diff --quiet "$commit" -- lib src ascaniusfish.hpp ascaniusfish_2.hpp; then
        echo "the engine code here is the same as $commit's"
    else
        echo "the engine code here differs from $commit's; the spot check decides"
    fi
fi
# A run's parts are tied to its worker count (cloud runs use 4).
if [ -f "$out/parts/config.txt" ]; then
    jobs=$(sed -n 's/^relabel jobs=\([0-9]*\) .*/\1/p' "$out/parts/config.txt")
fi

make tools/nne_data >/dev/null
status=0
./tools/nne_data relabel "$fens" out="$out" jobs="$jobs" syzygy="$syzygy" ${minutes:+minutes=$minutes} || status=$?
if [ "$status" -eq 3 ]; then
    echo "relabel stopped after $minutes minutes; run the same command again to carry on"
    exit 3
fi
[ "$status" -eq 0 ] || exit "$status"

if [ -n "$from" ]; then
    check=$out/check
    rm -rf "$check"
    mkdir -p "$check"
    zstdcat -f "$fens" | awk -F'\t' -v m=$check_every 'NR==1 || $2 % m == 0' > "$check/positions.tsv"
    ./tools/nne_data relabel "$check/positions.tsv" out="$check" jobs=6 syzygy="$syzygy" >/dev/null
    for s in train valid test; do tail -n +2 "$check/$s.tsv"; done | sort > "$check/local.tsv"
    for s in train valid test; do tail -n +2 "$out/$s.tsv"; done \
        | awk -F'\t' -v m=$check_every '$2 % m == 0' | sort > "$check/imported.tsv"
    if ! cmp -s "$check/local.tsv" "$check/imported.tsv"; then
        echo "spot check failed: the imported labels differ from local ones (diff $check/local.tsv $check/imported.tsv)" >&2
        exit 1
    fi
    echo "spot check: $(wc -l < "$check/local.tsv") positions labelled here equal the imported ones"
fi

for f in "$net" "$preds"; do
    [ -f "$f" ] && cp "$f" "${f%.*}.prev.${f##*.}"
done
make tools/nne_train >/dev/null
log_net="${net%.*}.log.${net##*.}"
./tools/nne_train data="$out" out="$log_net" preds=-
./tools/nne_train data="$out" loss=wdl init="$log_net" train=last out="$net" preds="$preds"
make diagnostics/nne_inference_test >/dev/null
./diagnostics/nne_inference_test "$net" "$preds"
