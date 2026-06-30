#!/bin/sh

# Verify that the riscv64 universal binary selects correct build
# and benches correctly under qemu-riscv64 with various vlens
#
# Usage: check_universal_riscv.sh STOCKFISH_EXE EXPECTED_BENCH

set -eu

if [ $# -ne 2 ]; then
    echo "Usage: $0 STOCKFISH_EXE EXPECTED_BENCH" >&2
    exit 2
fi

STOCKFISH_EXE=$1
EXPECTED_BENCH=$2
QEMU=${QEMU:-qemu-riscv64}
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/riscv64-linux-gnu}

EXTS=zba=true,zbb=true,zbs=true,zicond=true
PAIRS="
rv64:riscv64
rv64,v=true,$EXTS,vlen=128:riscv64-rva23
rv64,v=true,$EXTS,vlen=256:riscv64-rva23
rv64,v=true,$EXTS,vlen=512:riscv64-rva23
rv64,v=true,$EXTS,vlen=1024:riscv64-rva23
"

BINARY_SIZE=$(wc -c < "$STOCKFISH_EXE")
MAX_SIZE=$((150 * 1024 * 1024))
if [ "$BINARY_SIZE" -gt "$MAX_SIZE" ]; then
    printf 'check_universal_riscv.sh: binary size %d bytes exceeds 150 MB limit\n' "$BINARY_SIZE" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

idx=0
for pair in $PAIRS; do
    idx=$((idx + 1))
    cpu=${pair%%:*}
    (
        comp=$($QEMU -cpu "$cpu" "$STOCKFISH_EXE" compiler 2>&1 | awk -F: '/Compilation architecture/ {
            sub(/^[[:space:]]+/, "", $2); sub(/[[:space:]]+$/, "", $2); print $2; exit }')
        bench=$($QEMU -cpu "$cpu" "$STOCKFISH_EXE" bench 2>&1 | awk -F: '/Nodes searched/ {
            gsub(/[^0-9]/, "", $2); print $2; exit }')
        printf '%s|%s|%s\n' "$cpu" "$comp" "$bench" > "$tmp/$idx"
    ) &
done
wait

FAIL=0
idx=0
for pair in $PAIRS; do
    idx=$((idx + 1))
    expected=${pair##*:}
    IFS='|' read -r cpu comp bench < "$tmp/$idx"
    if [ "$comp" = "$expected" ] && [ "$bench" = "$EXPECTED_BENCH" ]; then
        printf 'CPU %-26s ok (%s, bench %s)\n' "$cpu" "$comp" "$bench" >&2
    else
        printf 'CPU %-26s FAIL: expected %s/%s, got %s/%s\n' \
            "$cpu" "$expected" "$EXPECTED_BENCH" "${comp:--}" "${bench:--}" >&2
        FAIL=1
    fi
done

if [ "$FAIL" != 0 ]; then
    echo "check_universal_riscv.sh: failed"
    exit 1
fi

echo "check_universal_riscv.sh: Good! Universal binary has correct hardware detection."
