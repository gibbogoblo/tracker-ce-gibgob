#ifndef GIBGOB_PIANOROLLCONTROL_H_
#define GIBGOB_PIANOROLLCONTROL_H_

#include "displayControls.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobUi.h"
#include "mtSequencer.h"

namespace gibgob
{

// a settings menu drawn over the piano roll (Quantize / Acid / Arp)
struct MenuRow
{
    char label[20];
    char value[20];
    bool isAction; // a row you press Middle on (e.g. "Apply", "Go Ape")
};

struct PianoMenuView
{
    static constexpr int kMaxRows = 9;
    bool active = false;
    char title[48] = { 0 };
    int cols = 1;            // 2 = two columns side by side (Quantize | Humanize)
    char colTitle[2][14] = { { 0 }, { 0 } };
    int colCursor = 0;
    int rows = 0;            // rows per column
    MenuRow row[kMaxRows];   // two columns: row[0] = left value, row[1] = right value
    int cursor = 0;          // -1 = column titles (two-column menus)
    char hint[64] = { 0 };
    uint32_t accent = 0xFFFFFF;
};

// a horizontal keyboard for picking notes
struct PickerView
{
    bool active = false;
    int firstPitch = 36;
    int keys = 36;
    int cursor = 48;
    uint8_t mask[16] = { 0 }; // picked pitches, one bit each
};

// Everything the piano roll needs to draw one frame.
struct PianoRollView
{
    const Sequencer::strPattern* pattern = nullptr;
    int track = 0;

    int firstTick = 0; // left edge of the view, in micro steps
    int stepsVisible = 16;
    int gridTicks = 48; // one square
    int topPitch = 72;  // highest visible pitch
    int rowsVisible = 12;
    int patternTicks = 16 * 48;

    int cursorTick = 0;
    int cursorPitch = 60;
    int ghostLength = 48;
    int ghostNudge = 0;
    uint8_t ghostPitches[kMaxChordNotes] = { 60 };
    int ghostCount = 1;
    bool showGhost = true;

    // box selection (Insert)
    bool boxActive = false;
    int boxTick1 = 0, boxTick2 = 0; // start ticks of the corner squares
    int boxPitch1 = 0, boxPitch2 = 0;

    int playheadTick = -1; // -1 = stopped
    bool shift = false;

    char title[56] = { 0 };
    char mode[12] = { 0 };
    uint32_t modeColor = 0;
    ParamBox params[4];
    int selectedParam = 0;

    BottomBar bar;
    HelpView help;
    PianoMenuView menu;
    PickerView picker;
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
};

} // namespace gibgob

#endif
