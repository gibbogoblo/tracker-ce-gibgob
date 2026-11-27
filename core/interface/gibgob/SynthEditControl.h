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

// the singer pictures (LILITRON): JPEGs decoded into the display's memory
struct SynthAvatar
{
    // the singer alone, and with the cat (one picture each, mouth shut / open)
    enum Pic : uint8_t
    {
        kSinger = 0,
        kSingerSing,
        kDuo,
        kDuoSing,
        kPics
    };
    bool loaded[kPics];
    uint32_t address[kPics];
    int16_t w[kPics], h[kPics];
    bool singing;  // the mouth is open (a note sounds)
    bool cat;      // the cat is on
    char word[8];  // what is sung
    // the syllable tiles: names (8), the one picked (top buttons), the one sung
    const char* const* names;
    int8_t picked;
    int8_t sung; // -1: quiet
};

// the sample view on top of a page with a sample slot (GORILLAGRAIN): the
// sample's shape, each sounding note's playhead, the grains sounding now
struct SynthGrainView
{
    static constexpr int kBins = 80;
    static constexpr int kMaxHeads = 4;
    static constexpr int kMaxGrains = 48;
    bool hasSample;
    uint8_t peaks[kBins]; // 0..255
    uint8_t heads;
    float head[kMaxHeads]; // 0..1
    uint8_t grains;
    float pos[kMaxGrains];   // 0..1
    float level[kMaxGrains]; // 0..1
    float pan[kMaxGrains];   // -1..1
    // a diagnosis build of the engine: its measurements (kept after the note)
    bool hasDiag;
    float diag[12];
    float partPeak;     // what the Mini's synth stage got from this part (since the note began)
    uint32_t overloads; // times all synths were muted for running late
    uint32_t lateUs;    // the longest synth update (2100 us is the limit)
    float postPeak;     // after the part's volume
    float gainDb;       // the part's volume as used
    float panL, panR;   // the pan stage before the mixer
    // recording into the sample (Rec): the picture is the take so far
    bool recording;
    float recSeconds, recMaxSeconds, recLevel;
};

struct SynthView
{
    synthui::ViewLayout layout;
    SynthCellText cells[synthui::kMaxCells];
    char panelTitle[synthui::kMaxPanels][18];
    char panelInfo[synthui::kMaxPanels][20];
    SynthGraph graph[synthui::kMaxPanels];
    SynthAvatar avatar;
    SynthGrainView grains;

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
    bool cute = false;   // the round look (pink for LILITRON); Shift does not lighten it
    bool jungle = false; // with cute: the night jungle colours (GORILLAGRAIN)
    bool angelic = false; // with cute: the Y2K cyber angelic look (CHILLAMANG): pearl, halos, wings
    uint8_t look = 0;     // not cute: 0 the Y2K look, BONOBONGO's kLookBlast / kLookClub / kLookRed
    char message[64];
    HelpView help;
};

// the sharp looks (SynthView::look)
enum : uint8_t
{
    kLookY2k = 0,
    kLookBlast, // explosive Y2K edge
    kLookClub,  // rainforest nightclub
    kLookRed,   // red and black, 90s cool
    kLookCount
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
// the background: exactly what a JPEG painted #0C0624 shows as on the Mini
// (16-bit colour), so a singer picture with that background blends in
constexpr uint32_t kBg = 0x080421;
constexpr uint32_t kPictureBg = 0x0C0624; // the colour to paint picture backgrounds
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

// the bubbly pink look (LILITRON): round shapes, pastel pinks on the same
// night background as the y2k look (the singer pictures blend into it)
namespace cute
{
constexpr uint32_t kBg = y2k::kBg;
constexpr uint32_t kPanel = 0x2A0E33;
constexpr uint32_t kRing = 0xFF8FC8;
constexpr uint32_t kRow = 0x41164A;
constexpr uint32_t kHot = 0xFF6FB5;   // the selected thing
constexpr uint32_t kSoft = 0xFFC2E2;  // labels
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kDark = 0x3A0A2E;  // text on light fills
constexpr uint32_t kHeader = 0xFF9ACD;
constexpr uint32_t kHeaderHi = 0xFFD3EA;
constexpr uint32_t kMint = 0x9CF2D8;
constexpr uint32_t kLemon = 0xFFF1A8;
// pastel bubbles: pink, lilac, peach, mint
constexpr uint32_t kBubble[4] = { 0xFF9ACD, 0xC9A7FF, 0xFFC4A3, 0x9CF2D8 };
} // namespace cute

// the night jungle (GORILLAGRAIN): the same round shapes as the pink look,
// moonlit greens; the grains are fireflies
namespace jungle
{
constexpr uint32_t kSkyTop = 0x0A1F26;
constexpr uint32_t kSkyLow = 0x040C0A;
constexpr uint32_t kPanel = 0x0B1E17;
constexpr uint32_t kRing = 0x1E4A34;    // panel and tab edges
constexpr uint32_t kTitle = 0x9FE0B5;   // panel titles
constexpr uint32_t kRow = 0x10281E;
constexpr uint32_t kSoft = 0x7FBF9F;    // labels
constexpr uint32_t kText = 0xE8F7D0;    // values (moonlight)
constexpr uint32_t kDark = 0x06140F;    // text on light fills
constexpr uint32_t kFirefly = 0xD7F25A; // the cursor, the grains
constexpr uint32_t kFireflyHi = 0xF2FF9E;
constexpr uint32_t kSelFill = 0x2C3B17;
constexpr uint32_t kLeafDark = 0x0F2D1E;
constexpr uint32_t kLeaf = 0x3FA06A;
constexpr uint32_t kTabOn = 0x2E7D4F;
constexpr uint32_t kRiver = 0x0E3A3A;
constexpr uint32_t kWave = 0x1E6B5A;
constexpr uint32_t kMoon = 0xE8F7D0;
// the coloured buttons (the Mini's own colours)
constexpr uint32_t kButton[4] = { 0x53B886, 0xB8AA53, 0x7B6FD6, 0x5AC8FA };
} // namespace jungle

// the Y2K cyber angelic look (CHILLAMANG): the same round shapes on a pearl
// sky (lavender to baby blue), iridescent pastels, gold halos, little wings
namespace angelic
{
constexpr uint32_t kSkyTop = 0xFBF7FF;
constexpr uint32_t kSkyLow = 0xE6F4FF;
constexpr uint32_t kPanel = 0xFFFFFF;
constexpr uint32_t kRing = 0xB7A4F2;   // panel and tab edges (lilac)
constexpr uint32_t kTitle = 0x2B1F66;  // panel titles
constexpr uint32_t kRow = 0xF1ECFF;
constexpr uint32_t kSoft = 0x45397E;   // labels (dark enough for thin fonts on pearl)
constexpr uint32_t kText = 0x1C1248;   // values
constexpr uint32_t kDark = 0x140C38;   // text on light fills
constexpr uint32_t kSel = 0xFFC6EA;    // the selected thing: iridescent pink
constexpr uint32_t kGold = 0xF5C542;   // halos: the cursor's ring, the page tab's ring
constexpr uint32_t kGoldDeep = 0xA67B0C;
constexpr uint32_t kLit = 0xBDEBFF;    // the engine in use: baby blue
constexpr uint32_t kTabOn = 0xE4DAFF;
constexpr uint32_t kWing = 0xFFFFFF;
constexpr uint32_t kWingEdge = 0xD6C8FF;
constexpr uint32_t kGraphBg = 0xF6F2FF;
constexpr uint32_t kGrid = 0xE4DAFF;
// deeper iridescent colours, readable on white: rose, lavender, sky, gold
constexpr uint32_t kAccent[4] = { 0xB8287F, 0x5B3CC4, 0x1A6E9E, 0x8A6408 };
} // namespace angelic

// points of a picture, for the drawing and the tests (returns how many)
int envelopePoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max, int16_t handles[8]);
int wavePoints(float shape, float cycles, int x, int y, int w, int h, int16_t* xy, int max);
int filterPoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max);

} // namespace gibgob

#endif
