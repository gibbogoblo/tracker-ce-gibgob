#ifndef GIBGOB_PERFORM_H_
#define GIBGOB_PERFORM_H_

// Device side of the GibGob performance effects: the one engine instance,
// its memory, the sequencer clock and the saved button setup.

#include "gibgob/PerformFx.h"

namespace gibgob
{
namespace perform
{

Engine& engine();
// the buttons saved on the SD card, and the effects' memory (once, when the
// UI starts; until then the effects stage passes audio through untouched)
void loadSaved();

// holding the top-row buttons
void press(int cell);
void release(int cell);
void releaseAll();
bool isHeld(int cell);
bool anyHeld();
// + / Up (dir > 0) or - / Down (dir < 0) while a button is held: changes the
// latest held button's effect until it is let go. Returns false if none held.
bool twist(int dir);
// call from the screens' update(): keeps Drum cut / Melodic cut on the right
// voices while they are held (chords borrow voices of other tracks)
void uiTick();

// the eight buttons (saved to the SD card by save())
Cell getCell(int i);
Cell getLiveCell(int i); // what a held button plays right now (with its twists)
void setCell(int i, const Cell& c);
void save();

// sequencer clock (called from the sequencer interrupt)
void clockTick(uint32_t globalPos, uint8_t uStep);
void clockStop();
Clock currentClock();

} // namespace perform
} // namespace gibgob

#endif
