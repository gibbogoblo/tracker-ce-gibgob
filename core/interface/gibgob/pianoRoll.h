#ifndef GIBGOB_PIANOROLL_H_
#define GIBGOB_PIANOROLL_H_

#include "core/modulesBase.h"
#include "gibgob/PianoRollControl.h"

class cPianoRoll : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamPitch = 0,
        kParamVelocity,
        kParamNudge,
        kParamInversion,
        kParamCount
    };

    virtual void update();
    virtual void start(uint32_t options); // options = track index
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls();

    void setDefaultScreenFunct();
    void showDefaultScreen();
    void refreshView();      // recompute view + redraw grid
    void refreshLabels();

    // editing
    void moveCursor(int dTicks, int dPitch);
    void placeGhost();
    void deleteAtCursor();
    void toggleSelectAtCursor();
    void clearSelection();
    void changeParam(int delta);
    void changeLength(int deltaGrid);
    void setChord(int idx);
    void setZoom(int dir);
    void auditionGhost(bool on);

    int track = 0;
    int chordIdx = 0;
    int inversion = 0;
    int velocity = 100;
    int nudge = 0;
    int lengthTicks = 48;
    int zoomLevel = 1; // 0 = 8 steps, 1 = 16 steps, 2 = 32 steps
    uint8_t param = kParamPitch;
    bool autoAudition = false;
    uint32_t autoAuditionOffAt = 0;
    int lastPlayheadDrawn = -2;

    gibgob::PianoRollView view;

    hControl grid = -1;
    hControl label[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    hControl bgLabel = -1;
    char labelText[8][12];
    char titleText[40];
};

extern cPianoRoll pianoRoll;

#endif
