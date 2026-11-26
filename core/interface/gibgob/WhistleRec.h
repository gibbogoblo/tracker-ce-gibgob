#ifndef GIBGOB_WHISTLEREC_H_
#define GIBGOB_WHISTLEREC_H_

// GibGob OS: a whistle take. The Mini's mic records while the sequencer plays
// one bar of count-in and then the track once through; the pitch is followed
// while it records (gibgob/Whistle.h) and turned into notes at the end.
//
// Everything lives in the free end of the sample memory, after the project's
// samples: the readings, the working memory and the recording itself.

#include "gibgob/Whistle.h"

#include <stdint.h>

namespace gibgob
{
namespace whistlerec
{

enum class Phase : uint8_t
{
    Off,
    Arming,  // the mic switched on; the count-in starts in a moment (no pop)
    CountIn, // one bar of metronome
    Taking,  // the track plays once through: whistle!
    Done,    // the notes are found (take())
};

// starts a take on `track` (the sequencer is stopped first); false with why
bool start(int track, char* why, int whyLen);
// call often: starts the count-in, follows the pitch, ends the take
void poll();
Phase phase();
// stops recording, the input back to normal; the take is kept if Done
void cancel();
// back to Off once the caller has what it needs (after Done)
void finish();

int track();
// the pitch heard right now (MIDI, 0 = none) and the level (0..1), for the screen
float pitchNow();
float levelNow();
// how far the take is: 0..1 (Taking)
float progress();

// after Done
const whistle::Take& take();
// where tick 0 of the track is, and the tempo (readings per micro step)
int32_t anchorHop();
float hopsPerTick();
int trackTicks();

} // namespace whistlerec
} // namespace gibgob

#endif
