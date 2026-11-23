#ifndef GIBGOB_HOME_H_
#define GIBGOB_HOME_H_

#include "core/modulesBase.h"
#include "gibgob/VisionControl.h"

// GibGob OS main screen: the eight tracks as clips around your picture
// (the Gibbon vision layout), with the main sequencer's controls. Notes are
// edited in the drum overview / piano roll (Middle).
class cGibGobHome : public ModuleBase
{
  public:
    // what a held coloured button changes with +/-
    enum Param : uint8_t
    {
        kParamNone = 0,
        kParamLength,   // Note: the track's length
        kParamVelocity, // Instr: how hard the top row plays / records
        kParamSound,    // FX1: the track's sound
        kParamPitch,    // FX2: the note the top row plays
        kParamAllLength, // Shift + Note: every track's length
        kParamTempo,     // Shift + FX1
        kParamSwing,     // Shift + FX2
    };

    virtual void update();
    virtual void start(uint32_t options); // options: homeOptions(track)
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    // actions
    void moveCursor(int dCol, int dRow);
    void openEditor();
    void openTrackPage();
    void trackButton(int track, bool pressed);
    void releaseAllButtons();
    void toggleMute(int track);
    void toggleSolo(int track);
    void duplicate();
    void clearTrack();
    void changeParam(int delta, bool big);
    void changePattern(int delta);
    void openWelcome();
    void welcomeMove(int d);
    void welcomeChoose();

    int cursor = 0; // the selected track
    Param param = kParamNone;
    bool patternHeld = false; // button 1 held: small +/- change the pattern
    bool insertDown = false;  // Insert: tap = mute, hold = solo
    bool insertUsed = false;
    uint32_t insertAt = 0;
    int soloTrack = -1;
    uint8_t muteBeforeSolo[8] = { 0 };
    int velocity = 100; // how hard the top row plays

    bool held[8] = { false };
    bool heldRecording[8] = { false };
    uint8_t heldPitch[8] = { 0 };
    bool perfHeld[8] = { false };

    // picture loading: slot 0 = frame 1, 1 = frame 2, 2 = done
    int loadSlot = 2;
    bool loadBusy = false;
    int loadedW[2] = { 0, 0 }, loadedH[2] = { 0, 0 };
    void pictureDone(bool ok);

    int lastPattern = -1;
    int lastPlayX = -2;
    int lastFrame = -1;
    uint8_t lastSounding = 0;
    uint8_t lastPerfActive = 0;
    bool lastShift = false;

    gibgob::VisionView* view = nullptr; // in the external memory
    hControl control = -1;
    char barText[8][16];
    char titleText[16];
};

extern cGibGobHome gibgobHome;

// short display name of an instrument slot
void gibgobSoundName(uint8_t instrument, char* out, int outLen);

#endif
