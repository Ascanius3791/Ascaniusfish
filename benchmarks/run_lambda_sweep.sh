# OWNERSHIP=Claude
#!/usr/bin/env bash
# Compiles and runs benchmarks/lambda_victim_benchmark.cpp once per lambda
# value (each a separate binary, since LOOKUP_TABLE_LAMBDA is a compile-time
# macro override for value_for_victim_index()'s lambda - see
# src/lookup_table.cpp), then prints the fill-state and TOTAL summary lines per
# value so they're easy to diff against each other. Run from the repo root.
#
# warmup_plies default (18) was picked by watching
# lookup_table::get_number_of_entrys() after every ply of unmeasured depth-5
# self-play from the start position: with TT_EXPONENT_FOR_SIZE=15/
# TT_BUCKET_SIZE=8 (262144 slots), every bucket had at least one entry by ply 17
# and the table sat at ~95% average fill (7.64/8 entries per bucket) by ply 18 -
# i.e. find_victim_index() is actually exercising eviction by then, which it
# isn't at all in the first several plies (buckets just have free slots).
set -euo pipefail
cd "$(dirname "$0")/.."

COUNTED_PLIES="${1:-10}"
DEPTH="${2:-5}"
WARMUP_PLIES="${3:-18}"
LAMBDAS=(0 0.5 1 2 3)

for lambda in "${LAMBDAS[@]}"; do
    bin="benchmarks/lambda_victim_benchmark_l${lambda}"
    g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable \
        -DNDEBUG -DLOOKUP_TABLE_LAMBDA="${lambda}" \
        -o "${bin}" benchmarks/lambda_victim_benchmark.cpp
    "${bin}" "${COUNTED_PLIES}" "${DEPTH}" "${WARMUP_PLIES}" | grep -E '^(lambda=|after|TOTAL)'
    rm -f "${bin}"
done
