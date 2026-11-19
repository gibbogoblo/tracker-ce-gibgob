#ifndef GIBGOB_VISIONCONTROL_H_
#define GIBGOB_VISIONCONTROL_H_

// Gibbon vision: the performance view. A 3 x 3 grid: the eight tracks around
// a two-frame picture from the SD card, each tile showing the 4 bars that
// are playing now.

#include "displayControls.h"
#include "gibgob/gibgobUi.h"
#include "mtSequencer.h"

namespace gibgob
{

// grid geometry (shared by the drawing and the picture size check)
constexpr int kVisionMargin = 10;
constexpr int kVisionGap = 8;
// the view has the whole screen: its own header (no title bar), then the grid
constexpr int kVisionTop = 69;
constexpr int kVisionBottom = 418;
constexpr int kVisionLogoX = 267, kVisionLogoY = 7; // logo panel
constexpr int kVisionLogoW = 264, kVisionLogoH = 56;
constexpr int kVisionTileW = (kScreenW - 2 * kVisionMargin - 2 * kVisionGap) / 3;
constexpr int kVisionTileH = (kVisionBottom - kVisionTop - 2 * kVisionGap) / 3;
constexpr int kVisionScreenSteps = 64; // 4 bars

// one tile: what to draw for a track
struct VisionTile
{
    static constexpr int kMaxMarks = 40;
    struct Mark
    {
        uint16_t x0, x1; // in 1/4 steps over the 4 bars (0..256)
        uint8_t y0, y1;  // 0 = top of the note area, 255 = bottom
    };
    char name[16] = { 0 };
    char info[20] = { 0 };
    uint32_t color = 0;
    bool muted = false;
    bool soloed = false;
    bool flash = false;
    int playX = -1; // playhead in 1/4 steps, -1 = stopped
    int count = 0;
    Mark marks[kMaxMarks];
};

// Fills the notes of a tile: the 4 bars that start at track tick
// `startTick` (the track loops when it is shorter). Merges nearby notes when
// there are more than fit.
void buildVisionTile(const Sequencer::strPattern* p, int track, int startTick, VisionTile& out);

struct VisionView
{
    VisionTile tiles[8];
    int cursor = 0; // track
    bool shift = false;

    // the picture: two frames in the display chip's memory
    bool pictureLoaded = false;
    uint32_t frameAddress[2] = { 0, 0 };
    int frameW = 0, frameH = 0;
    int frame = 0;
    const char* pictureHint = nullptr; // shown when there is no picture
    // the logo in the header (/GibGob/Vision/logo.jpg), else red text
    bool logoLoaded = false;
    uint32_t logoAddress = 0;
    int logoW = 0, logoH = 0;

    char patternText[16] = { 0 };
    char barText[16] = { 0 };
    char swingText[8] = { 0 };
    char bpmText[8] = { 0 };
    BottomBar bar;
    HelpView help;
};

// tile cell of track t in the 3 x 3 grid (the centre is the picture)
inline int visionCellOfTrack(int t)
{
    return t < 4 ? t : t + 1;
}

class VisionControl : public cDisplayControl
{
  public:
    VisionControl(strControlProperties* properties = nullptr);
    virtual ~VisionControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (VisionView*)data; }

  private:
    VisionView* view = nullptr;
};

} // namespace gibgob

#endif
