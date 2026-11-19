#ifndef GIBGOB_SYNTHEDITCONTROL_H_
#define GIBGOB_SYNTHEDITCONTROL_H_

// GibGob OS synth editor screen, Y2K style: chrome header, slanted boxes,
// light-segment bars, a colour-fade page tab.

#include "displayControls.h"
#include "gibgob/SynthLayout.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

struct SynthCellText
{
    char label[20];
    char value[16];
    int8_t fill; // 0..100 how full the bar is, -1: no bar
    bool bipolar;  // the bar grows from the middle
    bool lit;      // shown lit (the engine in use)
};

// what a panel's picture shows (all 0..1 unless noted)
struct SynthGraph
{
    // envelope: attack, decay, release lengths and the sustain level
    float a, d, s, r;
    int8_t handle; // the A / D / S / R point under the cursor (-1: none)
    // waveform / LFO: 0 sine, 1 triangle, 2 saw, 3 square (a morph in between),
    // 4 ramp down, 5 sample & hold, 6 smooth random
    float shape;
    float cycles; // LFO: waves across the box
    // filter: cutoff position, resonance; type 0 low pass, 1 high pass,
    // 2 band pass, 3 comb
    float cutoff, reso;
    uint8_t filterType;
    // delay: feedback; reverb: decay length
    float amount;
};

struct SynthView
{
    synthui::ViewLayout layout;
    SynthCellText cells[synthui::kMaxCells];
    char panelTitle[synthui::kMaxPanels][18];
    char panelInfo[synthui::kMaxPanels][20];
    SynthGraph graph[synthui::kMaxPanels];

    char synthName[20];   // header, on the chrome
    char headerInfo[40];  // "// TRK 05 // HAIRY LEAD"
    char pageText[20];    // "01/08 MAIN"
    char title[28];       // left of the quick boxes
    char pathText[48];    // Main page: the first status line
    char hint[64];        // Main page: the second status line; others: a line of help
    bool warn = false;    // the status / hint is a warning (pink)
    ParamBox quick[4];
    bool quickActive[4];

    int cursor = 0;
    const char* tabText[8] = { nullptr };
    bool tabOn[8] = { false }; // the current page, or a tool that is on
    bool shift = false;
    bool editing = true; // false: the synth has no GibGob layout (message only)
    char message[64];
    HelpView help;
};

class SynthEditControl : public cDisplayControl
{
  public:
    SynthEditControl(strControlProperties* properties = nullptr);
    virtual ~SynthEditControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (SynthView*)data; }

  private:
    SynthView* view = nullptr;
};

namespace y2k
{
constexpr uint32_t kBg = 0x0B0420;
constexpr uint32_t kGrid = 0x1A0B3A;
constexpr uint32_t kPanel = 0x150632;
constexpr uint32_t kPanelEdge = 0x4A2390;
constexpr uint32_t kRow = 0x1E0A46;
constexpr uint32_t kRowSel = 0x2C1166;
constexpr uint32_t kGraphBg = 0x07021A;
constexpr uint32_t kChromeHi = 0xF4F7FF;
constexpr uint32_t kChromeLo = 0x8A96BC;
constexpr uint32_t kChromeText = 0x120530;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kDim = 0xA898FF;
constexpr uint32_t kDark = 0x0B0420;
constexpr uint32_t kOff = 0x2D1660; // unlit bar segment
constexpr uint32_t kCyan = 0x00F0FF;
constexpr uint32_t kPurple = 0xC04DFF;
constexpr uint32_t kPink = 0xFF3DB4;
constexpr uint32_t kLime = 0xC6FF00;
// the coloured buttons: Note, Instr, FX1, FX2 (neon versions)
constexpr uint32_t kNote = 0x39FF88;
constexpr uint32_t kInstr = 0xFFE14D;
constexpr uint32_t kFx1 = 0xB36BFF;
constexpr uint32_t kFx2 = 0x3DD6FF;
constexpr uint32_t kAccent[4] = { kCyan, kPurple, kPink, kLime };
} // namespace y2k

// points of a picture, for the drawing and the tests (returns how many)
int envelopePoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max, int16_t handles[8]);
int wavePoints(float shape, float cycles, int x, int y, int w, int h, int16_t* xy, int max);
int filterPoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max);

} // namespace gibgob

#endif
