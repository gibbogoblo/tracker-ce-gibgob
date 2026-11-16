#ifndef GIBGOB_LANECONTROL_H_
#define GIBGOB_LANECONTROL_H_

#include "displayControls.h"
#include "gibgob/gibgobUi.h"
#include "mtSequencer.h"

namespace gibgob
{

struct LaneInfo
{
    char name[16]; // "1 Kick"
    bool melodic;
    bool muted;
    bool sounding;
};

// Everything the main sequencer screen needs to draw one frame.
struct LaneView
{
    const Sequencer::strPattern* pattern = nullptr;
    LaneInfo lanes[8];

    int firstTick = 0;
    int stepsVisible = 16;
    int patternTicks = 16 * 48;

    int cursorTrack = 0;
    int cursorTick = 0;
    int playheadTick = -1; // -1 = stopped
    bool shift = false;

    char title[56] = { 0 };
    char mode[12] = { 0 };
    uint32_t modeColor = 0;
    ParamBox params[4];
    int selectedParam = 0;
    BottomBar bar;
};

class LaneControl : public cDisplayControl
{
  public:
    LaneControl(strControlProperties* properties = nullptr);
    virtual ~LaneControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (LaneView*)data; }

  private:
    LaneView* view = nullptr;
};

} // namespace gibgob

#endif
