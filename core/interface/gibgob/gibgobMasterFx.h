#ifndef GIBGOB_MASTERFX_PAGE_H_
#define GIBGOB_MASTERFX_PAGE_H_

#include "core/modulesBase.h"
#include "gibgob/MasterFx.h"
#include "gibgob/TableControl.h"

// GibGob OS Master FX page (the 4th page of the Master menu): the kick duck,
// the glue compressor and the two colour slots. Settings are the song's;
// top 2 edits the current pattern's own instead.
class cGibGobMasterFx : public ModuleBase
{
  public:
    enum Column : uint8_t
    {
        kColDuck = 0,
        kColGlue,
        kColSlot1,
        kColSlot2,
        kColCount
    };

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}
    virtual uint8_t getCurrentModeIdx() { return 3; } // the 4th Master page

    void setDefaultScreenFunct();
    void refresh();
    int rowsIn(int column) const;

    void moveCursor(int dCol, int dRow);
    void changeValue(int delta, bool big);
    void topButton(int button);
    void goToMaster();

    int col = 0;
    int row = 0;
    bool editPattern = false; // false = the song's settings
    float lastGr = -1.0f;

    gibgob::TableView* view = nullptr; // in the external memory
    hControl table = -1;
    char titleText[40];
    char barText[8][16];
};

extern cGibGobMasterFx gibgobMasterFx;

#endif
