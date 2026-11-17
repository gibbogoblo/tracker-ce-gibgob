#!/bin/sh
# GibGob OS host tests: run the real note engine, chord builder and screen
# drawing code on a computer, with the hardware parts faked.
#   ./gibgob-tests/run.sh
set -e
cd "$(dirname "$0")"
CORE=../core
G=$CORE/interface/gibgob
FLAGS="-std=c++17 -Wall -g -fsanitize=address,undefined -I stubs -I $CORE/source -I $CORE/interface -I $G"
mkdir -p build
g++ $FLAGS engine_test.cpp $CORE/source/gibgob/MelodicTrack.cpp -o build/engine_test
g++ $FLAGS chord_test.cpp -o build/chord_test
g++ $FLAGS screen_test.cpp $G/LaneControl.cpp $G/PianoRollControl.cpp $G/TableControl.cpp $G/DrumSeqControl.cpp $G/gibgobUi.cpp \
    $CORE/source/gibgob/MelodicTrack.cpp -o build/screen_test
./build/engine_test
./build/chord_test
./build/screen_test
