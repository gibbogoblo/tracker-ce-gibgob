#ifndef GIBGOB_GIBBONVISION_H_
#define GIBGOB_GIBBONVISION_H_

#include "core/modulesBase.h"
#include "gibgob/VisionControl.h"

// Gibbon vision: the performance view (hold 1 + big + on the main sequencer).
// The top row plays the CHIMP OUT effects without Shift; Shift + top row
// plays the tracks. Your two pictures from /GibGob/Vision/ dance on the beat.
class cGibbonVision : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options); // options: the track to start on
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    void moveCursor(int dCol, int dRow);
    void topButton(int button, bool pressed);
    void toggleMute(int track);
    void toggleSolo(int track);
    void clearTrack(int track);
    void openEditor();
    void goBack();
    void changePattern(int delta);
    void releaseAllButtons();

    int cursor = 0;
    bool patternHeld = false;  // button 1 held
    bool patternUsed = false;  // ... and +/- pressed with it (no going back on release)
    bool perfHeld[8] = { false };
    bool trackHeld[8] = { false };
    int soloTrack = -1;
    uint8_t muteBeforeSolo[8] = { 0 };

    // picture loading: slot 0 = frame 1, 1 = frame 2, 2 = logo, 3 = done
    int loadSlot = 3;
    bool loadBusy = false;
    int loadedW[3] = { 0, 0, 0 }, loadedH[3] = { 0, 0, 0 };
    void pictureDone(bool ok);

    int lastPlayX = -2;
    uint8_t lastSounding = 0;
    uint8_t lastPerfActive = 0;
    int lastFrame = -1;

    gibgob::VisionView* view = nullptr; // in the external memory
    hControl control = -1;
    char titleText[32];
    char barText[8][16];
};

extern cGibbonVision gibbonVision;

#endif
