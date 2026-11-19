#ifndef GIBGOB_MASTER_H_
#define GIBGOB_MASTER_H_

// GibGob OS master effects on the device: where the settings live, and the
// per-block work the audio stage calls.
//
// Settings are saved in spare bytes the stock file formats already reserve:
// the song's in the project (the MIDI instrument slots' reserved bytes), a
// pattern's own in that pattern's reserved bytes. No new files.

#include "gibgob/MasterFx.h"
#include "mtSequencer.h"

namespace gibgob
{
namespace master
{

Engine& engine();
// the effects' memory (once, when the UI starts)
void init();

// the song's settings (every pattern without its own)
Settings songSettings();
void setSongSettings(const Settings& s);
// a pattern's own settings
bool patternHasOwn(const Sequencer::strPattern* p);
Settings patternSettings(const Sequencer::strPattern* p); // own, or the song's
void setPatternSettings(Sequencer::strPattern* p, const Settings& s); // makes them its own
void clearPatternSettings(Sequencer::strPattern* p);                  // back to the song's
// what plays now (the current pattern's own, else the song's)
Settings activeSettings();

// audio stage, once per block, after CHIMP OUT: settings, duck, effects
void audioBlock(float* l, float* r, int n);

} // namespace master
} // namespace gibgob

#endif
