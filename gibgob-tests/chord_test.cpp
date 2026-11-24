#include "gibgobChords.h"
#include "gibgobChordList.h"
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
    // the chord list: every progression in every key gives its 8 chords (the
    // right notes, whatever the inversion), in a warm range, moving smoothly
    {
        double totalMove = 0;
        int moves = 0, worst = 0;
        for (int e = 0; e < kChordListCount; e++)
            for (int key = 0; key < 12; key++)
            {
                PaletteChord pc[8];
                chordListChords(e, key, pc);
                uint8_t prev[kMaxChordNotes];
                int prevN = 0;
                for (int i = 0; i < 8; i++)
                {
                    uint8_t notes[kMaxChordNotes];
                    const int cn = buildPaletteChord(pc[i], notes, kMaxChordNotes);
                    if (cn != chordType(kChordList[e].type[i]).count) { printf("list %d key %d chord %d: %d notes\n", e, key, i, cn); fail++; }
                    if (pc[i].type != kChordList[e].type[i]) { printf("list %d: type changed\n", e); fail++; }
                    // the pitch classes are the chord's
                    const int rootPc = (key + kChordList[e].root[i] % 12 + 12) % 12;
                    if (pc[i].root % 12 != rootPc) { printf("list %d key %d chord %d: root %d\n", e, key, i, pc[i].root); fail++; }
                    for (int k = 0; k < cn; k++)
                    {
                        bool ok = false;
                        for (int q = 0; q < chordType(pc[i].type).count; q++)
                            ok = ok || (notes[k] % 12) == (rootPc + chordType(pc[i].type).intervals[q]) % 12;
                        if (!ok) { printf("list %d key %d chord %d: stray note %d\n", e, key, i, notes[k]); fail++; }
                    }
                    if (cn > 0 && (notes[0] < 36 || notes[cn - 1] > 84)) { printf("list %d key %d chord %d: range %d-%d\n", e, key, i, notes[0], notes[cn - 1]); fail++; }
                    if (prevN > 0)
                    {
                        int move = 0;
                        for (int k = 0; k < cn; k++)
                        {
                            int d = 128;
                            for (int q = 0; q < prevN; q++) d = abs(notes[k] - prev[q]) < d ? abs(notes[k] - prev[q]) : d;
                            move += d;
                        }
                        totalMove += (double)move / cn;
                        moves++;
                        worst = move / cn > worst ? move / cn : worst;
                    }
                    for (int k = 0; k < cn; k++) prev[k] = notes[k];
                    prevN = cn;
                }
            }
        const double avg = totalMove / moves;
        printf("  chord list: %d progressions, voices move %.2f semitones on average (worst %d)\n", kChordListCount, avg, worst);
        if (avg > 2.5 || worst > 7) { printf("chord list: the voices jump\n"); fail++; }
    }
    printf(fail ? "CHORD TEST FAILED %d\n" : "ALL CHORD TESTS PASSED\n", fail);
    return fail;
}
