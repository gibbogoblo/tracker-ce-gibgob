#ifndef GIBGOB_DRUMSEQ_H_
#define GIBGOB_DRUMSEQ_H_

#include "core/modulesBase.h"
#include "gibgob/DrumSeqControl.h"

// GibGob OS drum track sequencer: rows of 8 steps, the top row toggles the
// steps of the current row; Left/Right pick a step to edit its values.
class cDrumSeq : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamProb = 0, // Note button  (green)
        kParamVelocity, // Instr button (yellow)
        kParamNudge,    // FX1 button   (purple)
        kParamPitch,    // FX2 button   (light blue)
    };

    virtual void update();
    virtual void start(uint32_t options); // options = track
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    int stepNotes(int step, int* out, int maxOut);
    void toggleStep(int step);
    void deleteStep(int step);
    void moveCursor(int dStep);
    void changeParam(int delta);
    void changeLength(int delta);

    int track = 0;
    int cursorStep = 0;
    uint8_t param = kParamVelocity;
    int velocity = 100;
    int chance = 100;
    int nudge = 0;
    int lengthTicks = 48;
    int lastPlayStep = -2;

    gibgob::DrumSeqView view;
    hControl grid = -1;
    char titleText[40];
    char barText[8][8];
};

extern cDrumSeq drumSeq;

#endif
