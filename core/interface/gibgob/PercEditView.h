// SPDX-License-Identifier: GPL-3.0-only
#ifndef GIBGOB_PERCEDITVIEW_H_
#define GIBGOB_PERCEDITVIEW_H_

// GibGob OS PERC editor: its pages on the synth editor's screen (SynthEditControl), from a PERC
// sound. No hardware here (gibgob-tests/screen_test.cpp draws every page).
//
// The pages: Main (the voice types | Source, Volume, Pan, Tune, Decay or the hat's Open/Closed),
// Osc, Filter, Amp (+ the pitch envelope), LFO 1, LFO 2 (on Env's button), FX (click, drive and
// rate, velocity), Patch (Preset, Kit, Revert | Init, Go Ape, its amount). The Main and Patch
// pages' special rows are the synth editor's own (synthui::PatchItem), named for PERC here.

#include "gibgob/SynthEditControl.h"
#include "gibgob/SynthLayout.h"
#include "gibgob/perc/Perc.h"

namespace gibgob
{
namespace percui
{

void buildModel(synthui::SynthModel& m, int voiceType);

// a Main page row's value (-1: not one, e.g. the hat's Open / Closed)
int mainParam(int item, int voiceType);

struct Info
{
    int track = 0;
    bool sourcePerc = true;
    const char* presetName = "";
    const char* kitName = "";
    int apeAmount = 30;
    bool comparing = false;
    bool shift = false;
    const char* message = "";
    char (*tabNames)[10] = nullptr; // where the tab names are kept (8)
    int look = kLookBlast;          // BONOBONGO's look (SynthEditControl.h)
};

// lays the page out and fills every text and picture; the cursor is kept inside the page
// (v.cursor is where it ended up)
void fillView(SynthView& v, const synthui::SynthModel& m, int page, int view, int cursor, const perc::Sound& s,
              const Info& info);

extern const char* const kTools[8];

} // namespace percui
} // namespace gibgob

#endif
