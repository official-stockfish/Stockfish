#!/bin/bash

# Script for running Travis-CI tests naturally, instead of abusing `if`,
# `TRAVIS_OS_NAME` and chaining `&&` in the config file.

# Exit with error if a command fails.
set -e

#
# Obtain bench reference from git log
#
git log HEAD | grep "\b[Bb]ench[ :]\+[0-9]\{7\}" | head -n 1 | sed "s/[^0-9]*\([0-9]*\).*/\1/g" > git_sig
export benchref=$(cat git_sig)
echo "Reference bench:" $benchref

#
# Verify bench number against various builds
#

# Debug build (64-bit)
cmake --config Debug -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_CXX_FLAGS="-Werror -D_GLIBCXX_DEBUG" \
    > /dev/null
cmake --build build --config Debug -j 2 > /dev/null
./tests/signature.sh $benchref
cmake --build build --target clean

# Release build (64-bit)
cmake --config Release -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DCMAKE_CXX_FLAGS="-Werror" \
    -DENABLE_OPTIMIZE=ON \
    > /dev/null
cmake --build build --config Release -j 2 > /dev/null
./tests/signature.sh $benchref
cmake --build build --target clean

if [[ "$TRAVIS_OS_NAME" == "linux" ]]
then
    # Debug build (32-bit)
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DCMAKE_CXX_FLAGS="-Werror -D_GLIBCXX_DEBUG" \
        -DCMAKE_SIZEOF_VOID_P=4 \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    ./tests/signature.sh $benchref
    cmake --build build --target clean

    # Release build (32-bit)
    cmake --config Release -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DCMAKE_SIZEOF_VOID_P=4 \
        -DCMAKE_CXX_FLAGS="-Werror" \
        -DENABLE_OPTIMIZE=ON \
        > /dev/null
    cmake --build build --config Release -j 2 > /dev/null
    ./tests/signature.sh $benchref
    cmake --build build --target clean
fi

#
# Advanced cases (may not run on Travis)
#
if [[ "$TRAVIS_OS_NAME" == "linux" ]]
    # BMI2
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DUSE_PEXT=ON \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    cmake --build build --target clean

    # AVX2
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DUSE_AVX2=ON \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    cmake --build build --target clean

    # AVX512 (needs gcc 10 to compile)
    if [[ "$CXX" != "g++-8" ]]
    then
        cmake --config Debug -S . -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=${CC} \
            -DCMAKE_CXX_COMPILER=${CXX} \
            -DUSE_AVX512=ON \
            > /dev/null
        cmake --build build --config Debug -j 2 > /dev/null
        cmake --build build --target clean
    fi
fi

#
# Check perft and reproducible search
#
cmake --config Release -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=${CC} \
    -DCMAKE_CXX_COMPILER=${CXX} \
    -DENABLE_OPTIMIZE=ON > /dev/null
cmake --build build --config Release -j 2 > /dev/null
./tests/perft.sh
./tests/reprosearch.sh
cmake --build build --target clean

#
# Valgrind
#
export CXXFLAGS="-O1 -fno-inline"
if [ -x "$(command -v valgrind )" ]
then
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DCMAKE_CXX_FLAGS="-O1 -fno-inline" > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    ./tests/instrumented.sh --valgrind
    ./tests/instrumented.sh --valgrind-thread
    cmake --build build --target clean > /dev/null
fi

#
# Sanitizer
#
if [[ "$TRAVIS_OS_NAME" == "linux" ]]
then
    # UndefinedSanitizer
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DENABLE_SANITIZE=undefined \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    ./tests/instrumented.sh --sanitizer-undefined
    cmake --build build --target clean

    # ThreadSanitizer
    cmake --config Debug -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DENABLE_SANITIZE=thread \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    ./tests/instrumented.sh --sanitizer-thread
    cmake --build build --target clean
fi

#
# Link-time optimization
#
if [[ "$TRAVIS_OS_NAME" == "linux" && "$CC" == "gcc" ]]
then
    cmake --config Release -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DENABLE_LTO=ON \
        > /dev/null
    cmake --build build --config Release -j 2 > /dev/null
    cmake --build build --target clean
fi

#
# Profile-guided optimization
#
if [[ "$TRAVIS_OS_NAME" == "linux" && "$CC" == "gcc" ]]
then
    # Generate profiling
    cmake --config Release -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DENABLE_PROFILE=generate \
        > /dev/null
    cmake --build build --config Release -j 2 > /dev/null
    ./tests/signature.sh $benchref
    cmake --build build --target clean

    # Use profiling
    cmake --config Release -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=${CC} \
        -DCMAKE_CXX_COMPILER=${CXX} \
        -DENABLE_PROFILE=use \
        > /dev/null
    cmake --build build --config Debug -j 2 > /dev/null
    ./tests/signature.sh $benchref
    cmake --build build --target clean
fi
