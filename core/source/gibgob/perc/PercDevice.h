// SPDX-License-Identifier: GPL-3.0-only
#ifndef GIBGOB_PERC_DEVICE_H_
#define GIBGOB_PERC_DEVICE_H_

// The Mini side of PERC (Perc.h): the one engine, which drum tracks play it, the project's PERC file
// (perc.bin, next to the synth patches) and the audio.
//
// PERC mode is the project's: the drum overview shows either the sample drums or PERC (long press
// Rec there switches). In PERC mode a drum track plays its PERC voice (the track's sample is left
// alone, its steps stay) unless the track's Source is set to Sample (a one-shot track among the PERC
// ones). Slice tracks and melodic tracks never play PERC.

#include "gibgob/perc/Perc.h"

#include <stdint.h>

namespace gibgob
{
namespace perc
{

// once, when the UI starts (the hook into the note engine)
void init();

bool modeOn();
// switching on the first time in a project gives the drum tracks kick, snare, hat and cowbell
void setMode(bool on);
// a drum track's source in PERC mode: true = PERC (the default), false = its sample
bool trackSource(int track);
void setTrackSource(int track, bool perc);
// does this track play PERC now (mode on, a drum track, not a slice track, source PERC)
bool playsPerc(int track);

// the track's sound (UI thread; the audio interrupt is held off while it changes)
const Sound& sound(int track);
void setSound(int track, const Sound& s);
void setValue(int track, int param, int value);
void setHatOpen(int track, bool open);
// a whole kit onto the PERC tracks: the drum tracks in order take D1, SN, CH, D2, D3, OH, CY
void loadKit(int kit);
// a hit from the UI (pads, editor preview): 60 = the sound's own pitch
void play(int track, uint8_t velocity, int note = 60);
float level(int track);
bool sounding(int track);

// the audio node: adds PERC into the block (n: a multiple of 64)
void render(float* left, float* right, int n);

// the project's file
void loadWorkspace();                 // after a project was opened into the workspace
void saveWorkspace();                 // when the workspace project is saved
void newProject();                    // a new project: PERC off, the default sounds
void copyProjectFile(const char* fromFolder, const char* toFolder); // project <-> workspace
void markChanged();

} // namespace perc
} // namespace gibgob

#endif
