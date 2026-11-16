#ifndef GIBGOB_PIANOROLLCONTROL_H_
#define GIBGOB_PIANOROLLCONTROL_H_

#include "displayControls.h"
#include "mtSequencer.h"

namespace gibgob
{

// Everything the piano roll needs to draw one frame.
struct PianoRollView
{
    const Sequencer::strPattern* pattern = nullptr;
    int track = 0;

    int firstTick = 0;    // left edge of the view, in micro steps
    int ticksVisible = 16 * 48;
    int gridTicks = 48;   // grid unit (cursor step)
    int topPitch = 72;    // highest visible pitch
    int rowsVisible = 12;
    int patternTicks = 16 * 48;

    int cursorTick = 0;
    int cursorPitch = 60;
    int ghostLength = 48;
    int ghostNudge = 0;
    uint8_t ghostPitches[6] = { 60 };
    int ghostCount = 1;

    int playheadTick = -1; // -1 = stopped

    // info strip
    char info[96] = { 0 };
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

    int tickToX(int tick) const;
    int pitchToY(int pitch) const; // top of row
    int rowHeight() const;
};

} // namespace gibgob

#endif
