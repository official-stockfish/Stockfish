#!/bin/sh

### TODO: What is the Windows path for this? (msys2)

if [ ! -f /proc/sys/abi/sve_default_vector_length ]; then
    return 1
fi

vl=$(cat /proc/sys/abi/sve_default_vector_length)
echo "$vl * 8" | bc
