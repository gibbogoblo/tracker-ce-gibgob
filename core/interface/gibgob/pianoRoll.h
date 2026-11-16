#ifndef GIBGOB_PIANOROLL_H_
#define GIBGOB_PIANOROLL_H_

#include "core/modulesBase.h"
#include "gibgob/PianoRollControl.h"

class cPianoRoll : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamNote = 0, // Note button  (green)
        kParamVelocity, // Instr button (yellow)
        kParamNudge,    // FX1 button   (purple)
        kParamInversion, // FX2 button  (light blue)
        kParamCount
    };

    enum class Mode : uint8_t
    {
        Idle,    // d-pad moves the ghost cursor
        BoxDrag, // Insert pressed once: dragging a selection box
        Move,    // notes selected: d-pad moves them
    };

    virtual void update();
    virtual void start(uint32_t options); // options = track index
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refreshView();

    // actions
    void pressUp(bool repeat);
    void pressDown(bool repeat);
    void pressLeft();
    void pressRight();
    void pressMiddle();
    void pressInsert();
    void pressDelete();
    void toggleShift();
    void changeParam(int delta);
    void changeLength(int deltaGrid);
    void changeZoom(int delta);
    void pressChordButton(int button);
    void auditionGhost(bool on);

    // helpers
    void moveCursor(int dTicks, int dPitch);
    void moveSelection(int dTicks, int dPitch);
    void placeGhost();
    int selectUnderCursor();
    int selectInBox();
    void clearSelection();
    int selectionCount();

    int track = 0;
    int chordId = -1; // -1 = single note
    int inversion = 0;
    int velocity = 100;
    int nudge = 0;
    int lengthTicks = 48;
    int zoom = 0;
    uint8_t param = kParamNote;
    Mode mode = Mode::Idle;
    bool autoAudition = false;
    uint32_t auditionOffAt = 0;
    int lastPlayheadDrawn = -2;

    gibgob::PianoRollView view;
    hControl grid = -1;
    char titleText[40];
    char barText[8][12];
};

extern cPianoRoll pianoRoll;

#endif
