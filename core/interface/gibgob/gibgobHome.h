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
        kParamLength = 0, // Note button  (green): track length
        kParamVelocity,   // Instr button (yellow)
        kParamNudge,      // FX1 button   (purple)
        kParamPitch,      // FX2 button   (light blue)
        kParamCount
    };

    // what +/- edit while Shift is on (until Shift is turned off)
    enum class ShiftParam : uint8_t
    {
        None,       // small +/- = sound, big +/- = zoom
        GlobalLen,  // Shift + Note: length of every track
        Tempo,      // Shift + FX1
        Swing,      // Shift + FX2
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
    void pressInsert();
    void changeParam(int delta, bool big);
    void changeLength(int delta);
    void changeZoom(int delta);
    void changeSound(int dir);
    void changeTempo(float delta);
    void changeSwing(int delta);
    void changeGlobalLength(int delta, bool big);
    void insertNote();
    void changePattern(int delta);
    void toggleMute(int track);
    void trackButton(int track, bool pressed);
    void openPianoRoll();
    void openTrackPage();
    void releaseAllButtons();
    void openWelcome();
    void welcomeMove(int d);
    void welcomeChoose();

    int cellNotes(int* out, int maxOut);

    int cursorTrack = 0;
    int cursorStep = 0; // -1 = on the track name column
    int stepsVisible = 16;
    uint8_t param = kParamVelocity;
    bool paramHeld = false; // a coloured button is held: +/- change `param`
    bool patternHeld = false; // button 1 is held: +/- change the pattern
    ShiftParam shiftParam = ShiftParam::None;
    bool lastShift = false;
    uint32_t blinkAt = 0;
    bool blinkOn = false;

    // defaults for new steps
    int velocity = 100;
    int nudge = 0;
    int lengthTicks = 48;

    bool held[8] = { false };
    bool perfHeld[8] = { false }; // Shift + top row: performance effect held
    uint8_t lastPerfActive = 0;
    bool heldRecording[8] = { false };
    uint8_t heldPitch[8] = { 0 };
    uint32_t auditionOffAt = 0;
    int lastPattern = -1;
    int lastPlayPos = -2;
    uint8_t lastSounding = 0;

    gibgob::LaneView view;
    hControl grid = -1;
    char titleText[40];
    char barText[8][14];
};

extern cGibGobHome gibgobHome;

// short display name of an instrument slot
void gibgobSoundName(uint8_t instrument, char* out, int outLen);

#endif
