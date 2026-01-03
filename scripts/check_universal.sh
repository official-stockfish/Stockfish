#!/bin/sh

# Verify that the universal binary selects the correct per-arch build under
# Intel SDE emulation for a range of target CPUs
#
# Usage: check_universal.sh STOCKFISH_EXE SDE_EXE

set -eu

if [ $# -ne 2 ]; then
    echo "Usage: $0 STOCKFISH_EXE SDE_EXE" >&2
    exit 2
fi

STOCKFISH_EXE=$1
SDE_EXE=$2

PAIRS="
p4p:x86-64
nhm:x86-64-sse41-popcnt
snb:x86-64-sse41-popcnt
ivb:x86-64-sse41-popcnt
hsw:x86-64-bmi2
skl:x86-64-bmi2
skx:x86-64-avx512
clx:x86-64-vnni512
icl:x86-64-avx512icl
adl:x86-64-avxvnni
"

results=$(
    i=0
    for pair in $PAIRS; do
        i=$((i + 1))
        (
            cpu=${pair%%:*}
            expected=${pair##*:}
            out=$("$SDE_EXE" "-$cpu" -- "$STOCKFISH_EXE" compiler 2>&1 || true)
            actual=$(printf '%s\n' "$out" | awk -F: '/Compilation architecture/ {
                sub(/^[[:space:]]+/, "", $2); sub(/[[:space:]]+$/, "", $2); print $2; exit
            }')
            if [ "$actual" != "$expected" ]; then
                printf '===== -%s output (expected %s, got %s) =====\n%s\n' \
                    "$cpu" "$expected" "${actual:--}" "$out" >&2
            fi
            printf '%d %s %s %s\n' "$i" "$cpu" "$expected" "${actual:--}"
        ) &
    done
    wait
)

printf '%s\n' "$results" | sort -n | awk '
    { if ($4 == $3) printf "Testing -%-4s (expect %s) ... OK\n", $2, $3
      else { printf "Testing -%-4s (expect %s) ... FAIL (got %s)\n", $2, $3, $4; fail++ } }
    END {
        if (fail) {
            print "check_universal.sh: " fail " architecture(s) selected wrongly" > "/dev/stderr"
            exit 1
        }
        print "check_universal.sh: Good! Universal binary has correct hardware detection."
    }
'
