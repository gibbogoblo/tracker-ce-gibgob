#ifndef GIBGOB_HOME_H_
#define GIBGOB_HOME_H_

#include "core/modulesBase.h"
#include "gibgob/LaneControl.h"

// GibGob OS main screen: the main sequencer, 8 horizontal track lanes.
class cGibGobHome : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamProb = 0, // Note button  (green)
        kParamVelocity, // Instr button (yellow)
        kParamNudge,    // FX1 button   (purple)
        kParamPitch,    // FX2 button   (light blue)
        kParamCount
    };

    virtual void update();
    virtual void start(uint32_t options); // options = track to select
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    // actions
    void moveCursor(int dStep, int dTrack);
    void pressMiddle();
    void pressDelete();
    void changeParam(int delta);
    void changeLength(int delta);
    void changeZoom(int delta);
    void changeSound(int dir);
    void changePattern(int delta);
    void toggleMute(int track);
    void trackButton(int track, bool pressed);
    void openPianoRoll();
    void openTrackPage();
    void releaseAllButtons();

    int cellNotes(int* out, int maxOut);

    int cursorTrack = 0;
    int cursorStep = 0;
    int stepsVisible = 16;
    uint8_t param = kParamProb;

    // defaults for new steps
    int velocity = 100;
    int chance = 100;
    int nudge = 0;
    int lengthTicks = 48;

    bool held[8] = { false };
    bool heldRecording[8] = { false };
    uint8_t heldPitch[8] = { 0 };
    uint32_t auditionOffAt = 0;
    int lastPattern = -1;
    int lastPlayPos = -2;
    uint8_t lastSounding = 0;

    gibgob::LaneView view;
    hControl grid = -1;
    char titleText[40];
    char barText[8][10];
};

extern cGibGobHome gibgobHome;

// short display name of an instrument slot
void gibgobSoundName(uint8_t instrument, char* out, int outLen);

#endif
