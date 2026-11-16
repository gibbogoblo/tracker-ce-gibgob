#ifndef GIBGOB_CHORDS_H_
#define GIBGOB_CHORDS_H_

#include "gibgob/MelodicTrack.h"
#include "mtStructs.h"

#include <stdint.h>
#include <stdio.h>

namespace gibgob
{

struct ChordType
{
    const char* label;
    uint8_t count;       // number of notes (4 minimum)
    int8_t intervals[5]; // semitones above the root
};

// The pool of chord types a palette slot can use. Triads double the root an
// octave up so that every chord has at least four notes.
static const ChordType kChordPool[kChordTypeCount] = {
    { "Maj7", 4, { 0, 4, 7, 11 } },       // 0
    { "Min7", 4, { 0, 3, 7, 10 } },       // 1
    { "Dom7", 4, { 0, 4, 7, 10 } },       // 2
    { "m7b5", 4, { 0, 3, 6, 10 } },       // 3
    { "Dim7", 4, { 0, 3, 6, 9 } },        // 4
    { "Maj6", 4, { 0, 4, 7, 9 } },        // 5
    { "Min6", 4, { 0, 3, 7, 9 } },        // 6
    { "7sus4", 4, { 0, 5, 7, 10 } },      // 7
    { "Maj9", 5, { 0, 4, 7, 11, 14 } },   // 8
    { "Min9", 5, { 0, 3, 7, 10, 14 } },   // 9
    { "Dom9", 5, { 0, 4, 7, 10, 14 } },   // 10
    { "add9", 4, { 0, 4, 7, 14 } },       // 11
    { "m add9", 4, { 0, 3, 7, 14 } },     // 12
    { "6/9", 5, { 0, 4, 7, 9, 14 } },     // 13
    { "mMaj7", 4, { 0, 3, 7, 11 } },      // 14
    { "Min11", 5, { 0, 3, 7, 10, 17 } },  // 15
    { "Maj", 4, { 0, 4, 7, 12 } },        // 16
    { "Min", 4, { 0, 3, 7, 12 } },        // 17
    { "Sus2", 4, { 0, 2, 7, 12 } },       // 18
    { "Sus4", 4, { 0, 5, 7, 12 } },       // 19
    { "Dim", 4, { 0, 3, 6, 12 } },        // 20
    { "Aug", 4, { 0, 4, 8, 12 } },        // 21
};

enum Voicing : uint8_t
{
    kVoicingClose = 0,
    kVoicingOpen,  // every second note up an octave
    kVoicingDrop2, // second note from the top down an octave
    kVoicingBass,  // adds the root an octave below
};
static const char* const kVoicingNames[kVoicingCount] = { "Close", "Open", "Drop 2", "Bass +" };

inline const ChordType& chordType(int type)
{
    return kChordPool[(type >= 0 && type < kChordTypeCount) ? type : 0];
}

// Builds the notes of a chord, lowest first. Returns the number written.
inline int buildChordNotes(int type, int root, int inversion, int voicing, uint8_t* out, int maxOut)
{
    const ChordType& c = chordType(type);
    int notes[kMaxChordNotes];
    int n = c.count;

    for (int i = 0; i < n; i++)
        notes[i] = root + c.intervals[i];

    // inversion: move the lowest notes up an octave
    int inv = n > 0 ? inversion % n : 0;
    for (int i = 0; i < inv; i++)
        notes[i] += 12;

    auto sortNotes = [&]() {
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++)
                if (notes[b] < notes[a])
                {
                    int t = notes[a];
                    notes[a] = notes[b];
                    notes[b] = t;
                }
    };
    sortNotes();

    switch (voicing)
    {
        case kVoicingOpen:
            for (int i = 1; i < n; i += 2)
                notes[i] += 12;
            break;
        case kVoicingDrop2:
            if (n >= 3)
                notes[n - 2] -= 12;
            break;
        case kVoicingBass:
            if (n < kMaxChordNotes)
                notes[n++] = root - 12;
            break;
        default: break;
    }
    sortNotes();

    // an inversion of a doubled root can land two notes on the same pitch:
    // lift the copy an octave so the chord keeps all its notes
    for (int pass = 0; pass < kMaxChordNotes; pass++)
    {
        bool changed = false;
        for (int i = 1; i < n; i++)
        {
            if (notes[i] == notes[i - 1])
            {
                notes[i] += 12;
                changed = true;
            }
        }
        if (!changed)
            break;
        sortNotes();
    }

    int written = 0;
    for (int i = 0; i < n && written < maxOut; i++)
    {
        if (notes[i] < 0 || notes[i] > 127)
            continue;
        out[written++] = (uint8_t)notes[i];
    }
    return written;
}

inline int buildPaletteChord(const PaletteChord& c, uint8_t* out, int maxOut)
{
    return buildChordNotes(c.type, c.root, c.inversion, c.voicing, out, maxOut);
}

// e.g. "C4 Maj7"
inline void paletteChordName(const PaletteChord& c, char* out, int outLen)
{
    snprintf(out, (size_t)outLen, "%s %s", mtNotes[c.root > 127 ? 127 : c.root], chordType(c.type).label);
}

} // namespace gibgob

#endif
