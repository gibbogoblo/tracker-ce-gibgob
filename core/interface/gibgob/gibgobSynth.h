#ifndef GIBGOB_SYNTH_PAGE_H_
#define GIBGOB_SYNTH_PAGE_H_

#include "core/modulesBase.h"
#include "gibgob/SynthEditControl.h"
#include "gibgob/SynthLayout.h"

// GibGob OS synth editor (Y2K): the sound pages of a synth that has the
// GibGob layout (see SynthLayout.h). Opened instead of the stock synth pages.
//   top 1-8      : Main, Osc, Filter, Amp, LFO, Env, FX, Patch
//                  (the same button again: the page's next columns)
//   Shift + top  : Init pg, Ape pg, Copy pg, Paste pg, Compare, Undo, Latch, Save
//   colour btns  : jump to the 4 boxes at the top (again: back)
//   hold Middle  : play a note
class cGibGobSynth : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    // the instrument is a synth with the GibGob layout
    static bool handles(int instrument);

    void setDefaultScreenFunct();
    void rebuild();  // the model and the page, from the synth
    void refresh();  // the texts and pictures, then redraw

    void goToPage(int page);
    void moveCursor(int dx, int dy);
    void changeValue(int value);
    void setDefault();
    void colourButton(int k);
    void middle(bool pressed);
    void tool(int button);
    void say(const char* text);

    // the patch
    void loadPatch();
    void savePatch();
    void savePatchAs();
    void initPatch();
    void goApe(bool wholePatch);
    void applyEngine();

    void releaseNotes();
    void restoreCompare();

    int part = 0;
    int page = 0;
    hControl control = -1;
    char titleText[8];
};

extern cGibGobSynth gibgobSynth;

// Called from the interface loop: keeps the sequencer told which synth parts
// sing syllables (an engine with a "Syllable" parameter on CC 3, LILITRON).
void gibgobSynthPoll();

#endif
