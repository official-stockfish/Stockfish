#!/bin/sh

# Verify that the arm64 universal binary selects the correct per-arch build
# under qemu-aarch64 emulation for a range of target CPUs.
#
# Usage: check_universal_arm.sh STOCKFISH_EXE EXPECTED_BENCH

set -eu

if [ $# -ne 2 ]; then
    echo "Usage: $0 STOCKFISH_EXE EXPECTED_BENCH" >&2
    exit 2
fi

STOCKFISH_EXE=$1
EXPECTED_BENCH=$2

PAIRS="
cortex-a53:armv8
max:armv8-dotprod
"

BINARY_SIZE=$(wc -c < "$STOCKFISH_EXE")
MAX_SIZE=$((150 * 1024 * 1024))
if [ "$BINARY_SIZE" -gt "$MAX_SIZE" ]; then
    printf 'check_universal_arm.sh: binary size %d bytes exceeds 150 MB limit\n' "$BINARY_SIZE" >&2
    exit 1
fi

FAIL=0
for pair in $PAIRS; do
    cpu=${pair%%:*}
    expected_compiler=${pair##*:}
    compiler_out=$(qemu-aarch64 -cpu "$cpu" -- "$STOCKFISH_EXE" compiler 2>&1 || true)
    bench_out=$(qemu-aarch64 -cpu "$cpu" -- "$STOCKFISH_EXE" bench 2>&1 || true)
    actual_compiler=$(printf '%s\n' "$compiler_out" | awk -F: '/Compilation architecture/ {
        sub(/^[[:space:]]+/, "", $2); sub(/[[:space:]]+$/, "", $2); print $2; exit
    }')
    actual_bench=$(printf '%s\n' "$bench_out" | awk -F: '/Nodes searched/ {
        sub(/^[[:space:]]+/, "", $2); sub(/[[:space:]]+$/, "", $2); print $2; exit
    }')
    if [ "$actual_compiler" != "$expected_compiler" ] || [ "$actual_bench" != "$EXPECTED_BENCH" ]; then
        printf '===== CPU %s output (expected %s/%s, got %s/%s) =====\n' \
            "$cpu" "$expected_compiler" "$EXPECTED_BENCH" "${actual_compiler:--}" "$actual_bench" >&2
        printf 'Full "compiler" output: %s' "$compiler_out"
        printf 'Full "bench" output: %s' "$bench_out"
        FAIL=1
    else
        printf 'CPU %s ok\n' "$cpu" >&2
    fi
done

if [ "$FAIL" != 0 ]; then
    echo "check_universal_arm.sh: failed"
    exit 1
fi

echo "check_universal_arm.sh: Good! Universal binary has correct hardware detection."
