#!/bin/bash
# CS375 Lab 9 - Repeats each experiment over seeds 1-10 and prints the means,
# to show that a result is not an accident of one random workload.
# Author: Santiago Soto
# Contributions: Wrote this script to check the report's experiment results.
#
# Run from the repository root: make && scripts/seed_sweep.sh

SIM=./bin/memory_simulator_advanced
CONFIG=src/segments.txt

# Table columns are counted from the right because labels can contain spaces.
FAULTS='NF-7'; TLB='NF-4'; LATENCY='NF-3'

# Prints one column of the data rows whose label starts with $1, one per line.
column() {
    awk -v row="$1" "index(\$0, row) == 1 && NF >= 11 { print \$($2) }"
}

mean() {
    column "$1" "$2" | awk '{ sum += $1; n++ } END { printf "%.2f", sum / n }'
}

sweep() {
    for seed in 1 2 3 4 5 6 7 8 9 10; do
        $SIM --config $CONFIG --seed $seed --valid 1.0 "$@"
    done
}

echo "Policy, 12 frames, TLB 8: mean page faults over 10 seeds"
for locality in 0.8 0.0; do
    out=$(sweep --compare policy --frames 12 --tlb 8 --locality $locality)
    wins=$(paste <(echo "$out" | column 'LRU ' $FAULTS) <(echo "$out" | column 'FIFO ' $FAULTS) |
        awk '$1 < $2 { l++ } $1 > $2 { f++ } END { printf "LRU fewer on %d seeds, FIFO fewer on %d", l, f }')
    echo "  locality $locality: LRU $(echo "$out" | mean 'LRU ' $FAULTS), FIFO $(echo "$out" | mean 'FIFO ' $FAULTS) ($wins)"
done

echo "TLB size, 20 frames, locality 0.8: mean hit rate / mean latency"
out=$(sweep --compare tlb --frames 20)
for row in 'TLB off' 'TLB 4 ' 'TLB 16 '; do
    echo "  $row: $(echo "$out" | mean "$row" $TLB)% / $(echo "$out" | mean "$row" $LATENCY)"
done

echo "Page size, equal memory, TLB 8: mean faults / hit rate / latency"
for locality in 0.8 0.0; do
    out=$(sweep --compare pagesize --frames 20 --tlb 8 --locality $locality)
    for row in 'Page size 1000' 'Page size 4000'; do
        echo "  locality $locality, $row: $(echo "$out" | mean "$row" $FAULTS) / $(echo "$out" | mean "$row" $TLB)% / $(echo "$out" | mean "$row" $LATENCY)"
    done
done
