#ifndef GIBGOB_PIANOROLLCONTROL_H_
#define GIBGOB_PIANOROLLCONTROL_H_

#include "displayControls.h"
#include "gibgob/gibgobUi.h"
#include "mtSequencer.h"

namespace gibgob
{

struct ParamBox
{
    char label[12];
    char value[12];
    uint32_t color;
};

// Everything the piano roll needs to draw one frame.
struct PianoRollView
{
    const Sequencer::strPattern* pattern = nullptr;
    int track = 0;

    int firstTick = 0;    // left edge of the view, in micro steps
    int stepsVisible = 16;
    int gridTicks = 48;   // grid unit (one square)
    int topPitch = 72;    // highest visible pitch
    int rowsVisible = 12;
    int patternTicks = 16 * 48;

    int cursorTick = 0;
    int cursorPitch = 60;
    int ghostLength = 48;
    int ghostNudge = 0;
    uint8_t ghostPitches[6] = { 60 };
    int ghostCount = 1;
    bool showGhost = true;

    // box selection (Insert)
    bool boxActive = false;
    int boxTick1 = 0, boxTick2 = 0; // inclusive start ticks of grid squares
    int boxPitch1 = 0, boxPitch2 = 0;

    int playheadTick = -1; // -1 = stopped
    bool shift = false;

    char title[48] = { 0 };  // left part of the info strip
    char mode[24] = { 0 };   // e.g. "MOVE 3 notes"
    ParamBox params[4];
    int selectedParam = 0;

    BottomBar bar;
};

class PianoRollControl : public cDisplayControl
{
  public:
    PianoRollControl(strControlProperties* properties = nullptr);
    virtual ~PianoRollControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (PianoRollView*)data; }

  private:
    PianoRollView* view = nullptr;

    int gridX1() const;
    int gridX2() const;
    int gridY1() const;
    int rowHeight() const;
    int ticksVisible() const;
    int tickToX(int tick) const;
    int pitchToY(int pitch) const; // top of row
};

} // namespace gibgob

#endif
