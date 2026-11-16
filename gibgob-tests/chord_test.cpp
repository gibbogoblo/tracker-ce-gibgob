#include "gibgobChords.h"
#include <cstdio>
using namespace gibgob;
const char mtNotes[128][5] = {};
Sequencer sequencer; 
int main()
{
    int fail = 0;
    for (int type = 0; type < kChordTypeCount; type++)
        for (int inv = 0; inv < 6; inv++)
            for (int v = 0; v < kVoicingCount; v++)
            {
                uint8_t out[kMaxChordNotes];
                int n = buildChordNotes(type, 60, inv, v, out, kMaxChordNotes);
                int expect = chordType(type).count + (v == kVoicingBass ? 1 : 0);
                if (n != expect || n < 4) { printf("count type %d inv %d v %d: %d\n", type, inv, v, n); fail++; }
                for (int i = 1; i < n; i++)
                    if (out[i] <= out[i - 1]) { printf("order/dup type %d inv %d v %d\n", type, inv, v); fail++; }
                if (out[n - 1] - out[0] > 48) { printf("too wide type %d inv %d v %d\n", type, inv, v); fail++; }
            }
    // edges of the keyboard never crash or go out of range
    for (int root = 0; root < 128; root++)
        for (int type = 0; type < kChordTypeCount; type++)
        {
            uint8_t out[kMaxChordNotes];
            int n = buildChordNotes(type, root, 3, kVoicingBass, out, kMaxChordNotes);
            for (int i = 0; i < n; i++) if (out[i] > 127) fail++;
        }
    uint8_t out[6];
    int n = buildChordNotes(0, 60, 0, kVoicingClose, out, 6); // Cmaj7
    if (!(n == 4 && out[0] == 60 && out[1] == 64 && out[2] == 67 && out[3] == 71)) { printf("maj7 wrong\n"); fail++; }
    n = buildChordNotes(0, 60, 1, kVoicingClose, out, 6); // first inversion
    if (!(out[0] == 64 && out[3] == 72)) { printf("inv wrong\n"); fail++; }
    n = buildChordNotes(0, 60, 0, kVoicingDrop2, out, 6);
    if (!(out[0] == 55 && out[1] == 60)) { printf("drop2 wrong\n"); fail++; }
    printf(fail ? "CHORD TEST FAILED %d\n" : "ALL CHORD TESTS PASSED\n", fail);
    return fail;
}
