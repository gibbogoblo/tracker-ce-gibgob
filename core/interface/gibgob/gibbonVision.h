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

    // picture loading: 0 idle, 1 frame 1 asked, 2 frame 1 loading,
    // 3 frame 2 asked, 4 frame 2 loading, 5 done, 6 none
    int pictureState = 0;
    int frameW[2] = { 0, 0 }, frameH[2] = { 0, 0 };

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
