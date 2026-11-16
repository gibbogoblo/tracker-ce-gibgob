#ifndef GIBGOB_CHORDS_H_
#define GIBGOB_CHORDS_H_

#include <stdint.h>

namespace gibgob
{

struct ChordType
{
    const char* label;   // shown in the bottom bar box
    uint8_t count;       // number of notes (4 minimum)
    int8_t intervals[6]; // semitones above the root
};

static constexpr int kChordButtons = 8;
static constexpr int kChordBanks = 2;
static constexpr int kChordNone = -1; // single note

// bank 0 = normal, bank 1 = with Shift
static const ChordType kChordTypes[kChordBanks][kChordButtons] = {
    {
      { "Maj7", 4, { 0, 4, 7, 11 } },
      { "Min7", 4, { 0, 3, 7, 10 } },
      { "Dom7", 4, { 0, 4, 7, 10 } },
      { "m7b5", 4, { 0, 3, 6, 10 } },
      { "Dim7", 4, { 0, 3, 6, 9 } },
      { "Maj6", 4, { 0, 4, 7, 9 } },
      { "Min6", 4, { 0, 3, 7, 9 } },
      { "7sus4", 4, { 0, 5, 7, 10 } },
    },
    {
      { "Maj9", 5, { 0, 4, 7, 11, 14 } },
      { "Min9", 5, { 0, 3, 7, 10, 14 } },
      { "Dom9", 5, { 0, 4, 7, 10, 14 } },
      { "add9", 4, { 0, 4, 7, 14 } },
      { "m add9", 4, { 0, 3, 7, 14 } },
      { "6/9", 5, { 0, 4, 7, 9, 14 } },
      { "mMaj7", 4, { 0, 3, 7, 11 } },
      { "Min11", 5, { 0, 3, 7, 10, 17 } },
    },
};

// chordId: -1 = single note, otherwise bank * 8 + button
inline const ChordType* chordById(int chordId)
{
    if (chordId < 0 || chordId >= kChordBanks * kChordButtons)
        return nullptr;
    return &kChordTypes[chordId / kChordButtons][chordId % kChordButtons];
}

inline const char* chordName(int chordId)
{
    const ChordType* c = chordById(chordId);
    return c ? c->label : "Note";
}

inline int chordNoteCount(int chordId)
{
    const ChordType* c = chordById(chordId);
    return c ? c->count : 1;
}

// inversion N moves the lowest N notes up an octave.
inline int buildChord(int chordId, int root, int inversion, uint8_t* out, int maxOut)
{
    const ChordType* c = chordById(chordId);
    if (c == nullptr)
    {
        if (root < 0 || root > 127 || maxOut < 1)
            return 0;
        out[0] = (uint8_t)root;
        return 1;
    }
    int n = 0;
    int inv = inversion % c->count;
    for (int i = 0; i < c->count && n < maxOut; i++)
    {
        int p = root + c->intervals[i] + (i < inv ? 12 : 0);
        if (p < 0 || p > 127)
            continue;
        out[n++] = (uint8_t)p;
    }
    return n;
}

} // namespace gibgob

#endif
