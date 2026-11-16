#ifndef GIBGOB_HOME_H_
#define GIBGOB_HOME_H_

#include "core/modulesBase.h"
#include "gibgob/TrackGridControl.h"

// GibGob OS main screen: grid of the 8 tracks.
class cGibGobHome : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options); // options = track to select
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refreshTiles();
    void refreshLabels();

    void moveCursor(int dCol, int dRow);
    void openTrack();
    void openTracker();
    void setType(bool melodic);
    void toggleMute();
    void changeSound(int dir);
    void clearTrack();

    int cursor = 0;
    int lastPlayStep = -2;

    gibgob::TrackGridView view;
    hControl grid = -1;
    char titleText[40];
};

extern cGibGobHome gibgobHome;

// display name of an instrument slot
void gibgobSoundName(uint8_t instrument, char* out, int outLen);

#endif
