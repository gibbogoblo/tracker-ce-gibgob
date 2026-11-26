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
g++ $FLAGS engine_test.cpp $CORE/source/gibgob/MelodicTrack.cpp $CORE/source/gibgob/ChordFeel.cpp -o build/engine_test
g++ $FLAGS chord_test.cpp -o build/chord_test
g++ $FLAGS library_test.cpp $CORE/source/gibgob/Library.cpp -o build/library_test
g++ $FLAGS screen_test.cpp $G/PianoRollControl.cpp $G/TableControl.cpp $G/ChordCanopy.cpp $G/DrumSeqControl.cpp $G/PerformControl.cpp $G/VisionControl.cpp $G/DrumOverviewControl.cpp $G/GameMenuControl.cpp $G/SynthEditControl.cpp $G/gibgobUi.cpp $CORE/source/gibgob/SynthLayout.cpp \
    $CORE/source/gibgob/MelodicTrack.cpp $CORE/source/gibgob/ChordFeel.cpp $CORE/source/gibgob/PerformFx.cpp -o build/screen_test
g++ $FLAGS -std=c++20 perform_test.cpp $CORE/source/gibgob/PerformFx.cpp -o build/perform_test
g++ $FLAGS master_test.cpp $CORE/source/gibgob/MasterFx.cpp -o build/master_test
g++ $FLAGS -O1 whistle_test.cpp $CORE/source/gibgob/Whistle.cpp -o build/whistle_test
./build/engine_test
./build/chord_test
./build/library_test
./build/screen_test
./build/perform_test
./build/master_test
./build/whistle_test
