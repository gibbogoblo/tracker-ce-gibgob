#ifndef GIBGOB_DRUMSEQCONTROL_H_
#define GIBGOB_DRUMSEQCONTROL_H_

#include "displayControls.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

struct DrumCell
{
    bool exists = false; // inside the track length
    bool on = false;
    uint8_t velocity = 100;
    uint8_t chance = 100;
    int8_t nudge = 0;    // micro steps off the grid
    char pitch[6] = { 0 }; // shown when it differs from the track's base note
};

// Everything the drum step sequencer needs to draw one frame.
struct DrumSeqView
{
    static constexpr int kMaxRows = 8; // zoom: 2, 4 or 8 rows of 8 steps
    static constexpr int kCols = 8;

    int rows = 4;
    int firstRow = 0;
    int cursorStep = 0;
    int playStep = -1;
    int steps = 16;
    DrumCell cell[kMaxRows * kCols];
    bool shift = false;

    char title[56] = { 0 };
    char mode[12] = { 0 };
    uint32_t modeColor = 0;
    ParamBox params[4];
    int selectedParam = 0;
    BottomBar bar;
    HelpView help;
};

class DrumSeqControl : public cDisplayControl
{
  public:
    DrumSeqControl(strControlProperties* properties = nullptr);
    virtual ~DrumSeqControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (DrumSeqView*)data; }

  private:
    DrumSeqView* view = nullptr;
};

} // namespace gibgob

#endif
