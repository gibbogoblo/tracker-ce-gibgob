#ifndef GIBGOB_CHORDS_H_
#define GIBGOB_CHORDS_H_

#include <stdint.h>

namespace gibgob
{

struct ChordType
{
    const char* label;   // shown above the top-row button
    uint8_t count;       // number of notes
    int8_t intervals[6]; // semitones above the root
};

// The 8 top-row buttons. Slot 0 is "single note".
static constexpr int kChordButtons = 8;
static const ChordType kChordTypes[kChordButtons] = {
    { "Note", 1, { 0 } },
    { "Maj", 3, { 0, 4, 7 } },
    { "Min", 3, { 0, 3, 7 } },
    { "7", 4, { 0, 4, 7, 10 } },
    { "Maj7", 4, { 0, 4, 7, 11 } },
    { "Min7", 4, { 0, 3, 7, 10 } },
    { "Sus4", 3, { 0, 5, 7 } },
    { "Dim", 3, { 0, 3, 6 } },
};

// Build chord pitches from a root. inversion N moves the lowest N notes up an
// octave. Returns number of pitches written (pitches outside 0..127 dropped).
inline int buildChord(int chordIdx, int root, int inversion, uint8_t* out, int maxOut)
{
    if (chordIdx < 0 || chordIdx >= kChordButtons)
        chordIdx = 0;
    const ChordType& c = kChordTypes[chordIdx];
    int n = 0;
    int inv = c.count > 0 ? inversion % c.count : 0;
    for (int i = 0; i < c.count && n < maxOut; i++)
    {
        int p = root + c.intervals[i] + (i < inv ? 12 : 0);
        if (p < 0 || p > 127)
            continue;
        out[n++] = (uint8_t)p;
    }
    return n;
}

} // namespace gibgob

#endif
