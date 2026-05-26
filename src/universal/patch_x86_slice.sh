#!/bin/sh
# This finds the gUniversalNNUEOffset/gUniversalNNUEEnd symbols in the x86 slice
# and patches them to be an offset+size into the full executable.
#
# Usage: patch_x86_slice.sh <fat_binary> <x86_64_thin_slice> <nnue_file>

set -eu

FAT="$1"
X86="$2"
NET="$3"

# Must match volatile initializers in nnue_embed.cpp; byte order
# is reversed to match how it appears in the binary xxd
OFFSET_MAGIC=e7f50fe7f50ffeca
SIZE_MAGIC=2e51feca2e51feca

die() { echo "patch_x86_slice: $*" >&2; exit 1; }

# Find hex pattern in file, return byte offset
# Optional $3 = bytes to skip, $4 = max bytes to scan
# TODO: can we use grep for this? BSD grep seems weird
find_bytes() {
    file=$1; needle=$2; skip=${3:-0}; window=${4:-0}
    [ "$window" -gt 0 ] && win="-l $window" || win=""
    # Disassemble as hex, strip \n and search for the pattern
    pos=$(xxd -s "$skip" $win -p "$file" | tr -d '\n' \
            | awk -v n="$needle" '{ p = index($0, n); if (p) { print p; exit } }')
    [ -n "$pos" ] || return 1
    echo $(( skip + (pos - 1) / 2 ))
}

# Overwrite 8 bytes at a file offset
write_u64_le() {
    f="$1"; off="$2"; v="$3"; bytes=""; i=0
    while [ "$i" -lt 8 ]; do
        bytes="$bytes$(printf '\\%03o' "$(( v & 255 ))")"
        v=$(( v >> 8 )); i=$(( i + 1 ))
    done
    printf "$bytes" | dd of="$f" bs=1 seek="$off" conv=notrunc 2>/dev/null
}

needle=$(xxd -p -l 64 "$NET" | tr -d '\n')  # take first bytes of network as needle
net_size=$(wc -c < "$NET" | tr -d ' ')

# The arm64 slice always follows the x86-64 slice in
# the fat binary, so scan from ~after the x86-64 slice.
x86_size=$(wc -c < "$X86" | tr -d ' ')
net_off=$(find_bytes "$FAT" "$needle" "$x86_size" 8000000) || die "network not found"

off_pos=$(find_bytes "$X86" "$OFFSET_MAGIC") || die "offset sentinel not found"
size_pos=$(find_bytes "$X86" "$SIZE_MAGIC")  || die "size sentinel not found"

write_u64_le "$X86" "$off_pos"  "$net_off"
write_u64_le "$X86" "$size_pos" "$net_size"

echo "patch_x86_slice.sh: network at offset $net_off + size $net_size "
