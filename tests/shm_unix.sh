#!/bin/bash

set -euo pipefail

TEST_BINARY=$(mktemp)
trap 'rm -f "$TEST_BINARY"' EXIT

"${COMPCXX:-g++}" \
  -std=c++17 \
  -O2 \
  -pthread \
  -fno-exceptions \
  -Wall \
  -Wextra \
  -Werror \
  ../tests/shm_unix.cpp \
  misc.cpp \
  -o "$TEST_BINARY"

"$TEST_BINARY"
