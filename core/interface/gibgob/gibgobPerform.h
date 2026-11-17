#ifndef GIBGOB_PERFORM_PAGE_H_
#define GIBGOB_PERFORM_PAGE_H_

#include "core/modulesBase.h"
#include "gibgob/PerformControl.h"

// GibGob OS Perform page: what each top-row button does as a performance
// effect (Shift + top row on the main sequencer, top row in song mode while
// playing). Hold a top button here to try it.
class cGibGobPerform : public ModuleBase
{
  public:
    enum Field : uint8_t
    {
        kFieldEffect = 0, // Note button  (green)
        kFieldParam1,     // Instr button (yellow)
        kFieldParam2,     // FX1 button   (purple)
        kFieldParam3,     // FX2 button   (light blue)
    };

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void setDefaultScreenFunct();
    void refresh();

    void moveCursor(int dCol, int dRow);
    void changeValue(int delta, bool big);
    void topButton(int button, bool pressed);
    void clearCell();
    void goBack();

    int cursor = 0;
    uint8_t field = kFieldEffect;
    bool changed = false;
    bool held[8] = { false };
    uint8_t lastActive = 0;

    gibgob::PerformView view;
    hControl control = -1;
    char titleText[32];
    char barText[8][16];
};

extern cGibGobPerform gibgobPerform;

#endif
