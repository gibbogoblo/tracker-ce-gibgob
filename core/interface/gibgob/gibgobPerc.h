// SPDX-License-Identifier: GPL-3.0-only
#ifndef GIBGOB_PERC_PAGE_H_
#define GIBGOB_PERC_PAGE_H_

#include "core/modulesBase.h"

// GibGob OS PERC editor: one PERC track's drum synth sound, on the synth editor's Y2K screen.
// Opened with Middle on a PERC track's name in the drum overview (options: the track).
//   top 1-8      : Main, Osc, Filter, Amp, LFO, LFO 2, FX, Patch
//   Shift + top  : Init, Go Ape, -, -, Compare, Undo, -, Back (to the drum overview)
//   colour btns  : jump to the 4 boxes at the top (again: back)
//   Middle       : play it (Main: the voice type under the cursor; Patch: the action)
//   pads         : play it at the pad's velocity
//   Delete       : the value back to the voice type's own
class cGibGobPerc : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options); // options: the track (0..7)
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void rebuild(); // the pages for the voice type
    void refresh();

    void goToPage(int page);
    void moveCursor(int dx, int dy);
    void changeValue(int value);
    void setDefault();
    void colourButton(int k);
    void middle(bool pressed);
    void tool(int button);
    void say(const char* text);
    void back();

    int track = 0;
    int page = 0;
    hControl control = -1;
};

extern cGibGobPerc gibgobPerc;

#endif
