#ifndef GIBGOB_CHORDCANOPY_H_
#define GIBGOB_CHORDCANOPY_H_

// GibGob OS: the Chord Canopy, the chord page of the piano roll (Shift + FX2).
// One chord at a time: vines hang from a jungle canopy down to the keys of
// the chord; a fruit on each vine lights up while its note sounds.

#include "gibgob/gibgobUi.h"

#include <stdint.h>

namespace gibgob
{

struct CanopyView
{
    static constexpr int kMaxNotes = 8;
    static constexpr int kMaxFields = 8;

    char title[40] = { 0 };
    char chordName[24] = { 0 };
    int chordIndex = 0; // 0..7: Shift + top 1..8

    uint8_t notes[kMaxNotes] = { 0 }; // the chord's notes
    int noteCount = 0;
    uint8_t sounding[kMaxNotes] = { 0 }; // notes playing right now
    int soundingCount = 0;

    // the four ways of playing (Note / Instr / FX1 / FX2); `mode` is lit
    ParamBox params[4];
    int mode = 0;

    // the row of values: the chord, then the chosen way of playing
    struct Field
    {
        char label[10];
        char value[12];
        uint32_t color;   // accent while the cursor is on it
        int meter;        // > 0: draw `meter` of `meterMax` dots under the value
        int meterMax;
    } fields[kMaxFields];
    int fieldCount = 0;
    int cursor = 0;

    bool shift = false;
    BottomBar bar;

    // the chord list (Insert): ready-made progressions to load into 1-8
    static constexpr int kListRows = 8;
    struct ListRow
    {
        char name[20];
        char chords[64]; // "Dm9 G13 Cmaj9 ..."
        uint8_t style;   // ChordListStyle
    };
    bool listOpen = false;
    ListRow listRows[kListRows];
    int listShown = 0;   // rows filled
    int listCursor = 0;  // the highlighted row (of the shown ones)
    int listIndex = 0;   // its number in the whole list (0-based)
    int listCount = 0;
    char listKey[4] = { 0 };
};

// draws the whole page (inside a display update)
void drawChordCanopy(const CanopyView& v, int fontRegular, int fontBold);

// the lowest C shown and how many octaves (2 or 3), so all the notes fit
void canopyKeyRange(const uint8_t* notes, int count, int& lowC, int& octaves);

} // namespace gibgob

#endif
