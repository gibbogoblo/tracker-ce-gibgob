#ifndef GIBGOB_DRUMOVERVIEW_H_
#define GIBGOB_DRUMOVERVIEW_H_

#include "core/modulesBase.h"
#include "gibgob/DrumOverviewControl.h"

// GibGob OS drum overview: every drum track at once, 8 steps on screen (one
// per top button). Opened with Middle on a drum track on the main screen.
// Shortcuts: gibgobHelp.h (kHelpDrumOverview), Shift + Menu.
class cDrumOverview : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kParamNone = 0,
        kParamProb,     // Note
        kParamVelocity, // Instr
        kParamNudge,    // FX1
        kParamPitch,    // FX2
        kParamFx1,      // Shift + FX1: the step's first effect (Up/Down: which, +/-: how much)
        kParamFx2,      // Shift + FX2: the step's second effect
        kParamRatchet,  // Shift + Note: small +/- speed, big +/- length
        kParamSample,   // Shift + Instr: the step's own sample
    };

    virtual void update();
    virtual void start(uint32_t options); // options: homeOptions(track)
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    int track() const; // the selected drum track (-1: none)
    int stepNotes(int track, int step, int* out, int maxOut);
    void selectTrack(int delta);
    void moveCursor(int delta);
    void toggleStep(int step);
    void deleteStep();
    void clearTrack();
    void changeParam(int dir, bool big);
    void changeLength(int dir, bool fine = false);
    void changeDrumStep(int track, const int* idx, int n, int dir, bool big); // ratchet / sample
    void changeStepFx(int dir, bool big); // the held FX: its amount
    void pickStepFx(int dir);             // the held FX: Up/Down, which effect
    void markFill(int fill);
    void clearFillMark();
    void copyPage();
    void pastePage();
    void openTrackPage();
    void goHome();
    void topButton(int button, bool pressed);
    void releaseAll();

    int drums[8] = { 0 }; // the drum tracks shown, in order
    int drumCount = 0;
    int selTrack = 0;     // the selected track (by track number)
    int cursor = 0;       // step (0 .. length - 1)
    bool onName = false;  // the cursor is on the track name (left of step 1)
    Param param = kParamNone;
    bool insertDown = false;
    bool insertUsed = false;
    bool perfHeld[8] = { false };
    uint32_t auditionOffAt = 0;
    int lastPlayStep = -2;
    bool lastShift = false;

    gibgob::DrumOverviewView* view = nullptr; // in the external memory
    hControl control = -1;
    char barText[8][16];
};

extern cDrumOverview drumOverview;

#endif
