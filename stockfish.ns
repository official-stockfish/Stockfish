#!/bin/sh

echo "- Getting latest Stockfish ..."

if [ -d Stockfish/src ]; then
    cd Stockfish/src
    make clean > /dev/null
    git pull
else
    git clone --depth 1 https://github.com/niklasf/Stockfish.git --branch fishnet
    cd Stockfish/src
fi

echo "- Determining CPU architecture ..."

ARCH=x86-64
EXE=stockfish-x86_64

if [ -f /proc/cpuinfo ]; then
    if grep "^flags" /proc/cpuinfo | grep -q popcnt ; then
        ARCH=x86-64-modern
        EXE=stockfish-x86_64-modern
    fi

    if grep "^flags" /proc/cpuinfo | grep bmi2 | grep -q popcnt ; then
        ARCH=x86-64-bmi2
        EXE=stockfish-x86_64-bmi2
    fi
fi

echo "- Building and profiling $EXE ... (patience advised)"
make profile-build ARCH=$ARCH EXE=../../$EXE > /dev/null

cd ../..
echo "- Done!"
