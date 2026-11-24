#ifndef GIBGOB_CHORDLIST_H_
#define GIBGOB_CHORDLIST_H_

// GibGob OS: the Chord Canopy's chord list (Insert). Ready-made progressions
// of 8 chords - neo-soul, jazz, city pop and Japanese jazz - loaded into the
// Canopy's chords 1-8 in any key, voice-led so each chord moves smoothly into
// the next (the inversion and voicing nearest the chord before).

#include "gibgob/gibgobChords.h"

#include <stdint.h>
#include <stdlib.h>

namespace gibgob
{

enum ChordListStyle : uint8_t
{
    kStyleNeoSoul = 0,
    kStyleJazz,
    kStyleCityPop,
    kStyleJpJazz,
    kChordListStyleCount
};
static const char* const kChordListStyleNames[kChordListStyleCount] = { "Neo-soul", "Jazz", "City pop", "J-jazz" };

// chord type indices (kChordPool in gibgobChords.h)
namespace ct
{
constexpr uint8_t Maj7 = 0, Min7 = 1, Dom7 = 2, m7b5 = 3, Maj6 = 5, Min6 = 6, Maj9 = 8, Min9 = 9, Dom9 = 10,
                  SixNine = 13, Min11 = 15, Dom13 = 22, Sharp9 = 23, Flat9 = 24, NineSus4 = 25, Maj7s11 = 26,
                  Min69 = 27;
}

struct ChordListEntry
{
    const char* name;
    uint8_t style;
    int8_t root[8]; // semitones above the key (I = 0)
    uint8_t type[8];
};

// written in C: I = 0, ii = 2, iii = 4, IV = 5, V = 7, vi = 9, vii = 11
static const ChordListEntry kChordList[] = {
    // ---- neo-soul ----
    { "Soul ii-V-I", kStyleNeoSoul, { 2, 7, 0, 9, 2, 7, 0, 0 },
      { ct::Min9, ct::Dom13, ct::Maj9, ct::Flat9, ct::Min9, ct::NineSus4, ct::Maj7, ct::SixNine } },
    { "Velvet drift", kStyleNeoSoul, { 0, 5, 4, 9, 2, 7, 4, 9 },
      { ct::Maj9, ct::Maj9, ct::Min9, ct::Min9, ct::Min9, ct::NineSus4, ct::Min7, ct::Sharp9 } },
    { "Gospel lift", kStyleNeoSoul, { 5, 4, 9, 7, 0, 5, 5, 0 },
      { ct::Maj7, ct::Sharp9, ct::Min9, ct::Min9, ct::Dom13, ct::Maj9, ct::Min6, ct::Maj9 } },
    { "Minor soul", kStyleNeoSoul, { 9, 2, 7, 0, 5, 11, 4, 9 },
      { ct::Min9, ct::Min9, ct::Dom13, ct::Maj9, ct::Maj7s11, ct::m7b5, ct::Flat9, ct::Min9 } },
    // ---- jazz ----
    { "Jazz ii-V-I", kStyleJazz, { 2, 7, 0, 0, 4, 9, 2, 7 },
      { ct::Min7, ct::Dom7, ct::Maj7, ct::Maj6, ct::Min7, ct::Dom7, ct::Min7, ct::Dom7 } },
    { "Minor ii-V-i", kStyleJazz, { 11, 4, 9, 9, 2, 7, 0, 4 },
      { ct::m7b5, ct::Flat9, ct::Min9, ct::Min6, ct::Min9, ct::Dom13, ct::Maj7, ct::Sharp9 } },
    { "Turnaround", kStyleJazz, { 0, 9, 2, 7, 4, 9, 2, 7 },
      { ct::Maj7, ct::Min7, ct::Min7, ct::Dom7, ct::Min7, ct::Dom7, ct::Min7, ct::Dom9 } },
    { "Giant steps", kStyleJazz, { 0, 3, 8, 11, 4, 7, 0, 0 },
      { ct::Maj7, ct::Dom7, ct::Maj7, ct::Dom7, ct::Maj7, ct::Dom7, ct::Maj7, ct::Maj9 } },
    { "Modal", kStyleJazz, { 2, 4, 5, 7, 2, 3, 2, 7 },
      { ct::Min11, ct::Min11, ct::Maj7s11, ct::NineSus4, ct::Min11, ct::Min11, ct::Min11, ct::NineSus4 } },
    // ---- city pop ----
    { "Royal road", kStyleCityPop, { 5, 7, 4, 9, 5, 7, 0, 0 },
      { ct::Maj9, ct::Dom13, ct::Min9, ct::Min9, ct::Maj9, ct::NineSus4, ct::Maj9, ct::SixNine } },
    { "Two of us", kStyleCityPop, { 5, 4, 9, 7, 0, 5, 4, 9 },
      { ct::Maj7, ct::Sharp9, ct::Min9, ct::Min9, ct::Dom13, ct::Maj7, ct::Flat9, ct::Min9 } },
    { "Night drive", kStyleCityPop, { 8, 10, 0, 9, 2, 7, 4, 9 },
      { ct::Maj9, ct::Dom13, ct::Maj9, ct::Min9, ct::Min9, ct::Dom13, ct::Min9, ct::Flat9 } },
    { "Anime outro", kStyleCityPop, { 5, 7, 4, 9, 2, 4, 5, 7 },
      { ct::Maj7, ct::Dom7, ct::Min7, ct::Min7, ct::Min7, ct::Min7, ct::Maj7, ct::NineSus4 } },
    // ---- Japanese jazz ----
    { "Kissa ii-V", kStyleJpJazz, { 2, 1, 0, 11, 4, 3, 2, 7 },
      { ct::Min9, ct::Dom9, ct::Maj9, ct::Sharp9, ct::Min9, ct::Dom13, ct::Min9, ct::NineSus4 } },
    { "Tokyo bossa", kStyleJpJazz, { 0, 9, 2, 7, 4, 9, 2, 1 },
      { ct::Maj9, ct::Flat9, ct::Min9, ct::Dom13, ct::Min7, ct::Sharp9, ct::Min9, ct::Dom9 } },
    { "Kyoto rain", kStyleJpJazz, { 5, 4, 2, 0, 5, 4, 9, 7 },
      { ct::Maj9, ct::Min9, ct::Min9, ct::Maj9, ct::Maj7s11, ct::Min7, ct::Min69, ct::NineSus4 } },
};
static constexpr int kChordListCount = (int)(sizeof(kChordList) / sizeof(kChordList[0]));

// a short chord name for the list: "Dm9", "G13", "Cmaj7#11"
inline void chordListShortName(int pitchClass, int type, char* out, int outLen)
{
    static const char* const kPc[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* const kShort[kChordTypeCount] = { "maj7", "m7",  "7",     "m7b5", "dim7", "6",     "m6",
                                                         "7sus4", "maj9", "m9",   "9",    "add9", "madd9", "6/9",
                                                         "mMaj7", "m11", "",      "m",    "sus2", "sus4",  "dim",
                                                         "aug",  "13",   "7#9",   "7b9",  "9sus4", "maj7#11", "m6/9" };
    const int t = (type >= 0 && type < kChordTypeCount) ? type : 0;
    snprintf(out, (size_t)outLen, "%s%s", kPc[((pitchClass % 12) + 12) % 12], kShort[t]);
}

// the chords of entry `idx` in `key` (0 = C .. 11 = B), voice-led: the first
// in close position around C3, each next one the inversion / voicing whose
// notes sit nearest the one before (and not too low or too high)
inline void chordListChords(int idx, int key, PaletteChord out[8])
{
    const ChordListEntry& e = kChordList[(idx % kChordListCount + kChordListCount) % kChordListCount];
    key = ((key % 12) + 12) % 12;
    uint8_t prev[kMaxChordNotes];
    int prevN = 0;
    for (int i = 0; i < 8; i++)
    {
        const int pc = (key + e.root[i] % 12 + 12) % 12;
        int baseRoot = 48 + pc; // C3 .. B3
        if (baseRoot > 54)
            baseRoot -= 12; // F#2 .. F#3: the roots stay in one octave
        PaletteChord best{ (uint8_t)baseRoot, e.type[i], 0, kVoicingClose };
        if (prevN > 0)
        {
            int bestCost = 1 << 30;
            const int count = chordType(e.type[i]).count;
            for (int oct = -12; oct <= 12; oct += 12)
                for (int inv = 0; inv < count; inv++)
                    for (int voicing = 0; voicing < 2; voicing++) // close or drop 2
                    {
                        PaletteChord c{ (uint8_t)(baseRoot + oct), e.type[i], (uint8_t)inv,
                                        (uint8_t)(voicing ? kVoicingDrop2 : kVoicingClose) };
                        uint8_t notes[kMaxChordNotes];
                        const int n = buildPaletteChord(c, notes, kMaxChordNotes);
                        if (n <= 0)
                            continue;
                        // keep it in a warm middle register
                        if (notes[0] < 40 || notes[n - 1] > 79)
                            continue;
                        int cost = 0;
                        for (int a = 0; a < n; a++)
                        {
                            int d = 128;
                            for (int b = 0; b < prevN; b++)
                                d = abs((int)notes[a] - (int)prev[b]) < d ? abs((int)notes[a] - (int)prev[b]) : d;
                            cost += d * d;
                        }
                        cost += inv * 2 + voicing * 3; // simpler shapes win a tie
                        if (cost < bestCost)
                        {
                            bestCost = cost;
                            best = c;
                        }
                    }
        }
        out[i] = best;
        prevN = buildPaletteChord(best, prev, kMaxChordNotes);
    }
}

} // namespace gibgob

#endif
