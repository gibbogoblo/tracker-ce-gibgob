#ifndef GIBGOB_DRUMOVERVIEWCONTROL_H_
#define GIBGOB_DRUMOVERVIEWCONTROL_H_

// The drum overview (beach look): every drum track as a band from sunset sky
// down to sand, 8 steps on screen (one per top button). The selected track is
// a wide band with its step locks above and below each step.

#include "displayControls.h"
#include "gibgob/gibgobUi.h"

namespace gibgob
{

struct DrumOverviewTrack
{
    char name[12] = { 0 };
    bool muted = false;
    uint8_t hit = 0;      // bit per step on screen: a hit
    uint8_t exists = 0;   // bit per step on screen: inside the track's length
    uint8_t fillOnly = 0; // bit per step: plays only in fills
    uint8_t noFill = 0;   // bit per step: left out of fills
    bool sampleTag = false; // PERC mode: this track plays its sample ("smp")
};

struct DrumOverviewView
{
    static constexpr int kMaxTracks = 8;
    static constexpr int kSteps = 8;

    int count = 0; // drum tracks shown
    DrumOverviewTrack tracks[kMaxTracks];
    int sel = 0;     // selected band
    int cursor = 0;  // step on screen 0..7, -1 = on the track name
    int play = -1;   // playing step on screen, -1 = not on this page
    bool fillOn = false; // Insert held while playing
    bool perc = false;   // PERC mode: the drum synth (teal), else the samples (drum red)

    // the selected track: the parameter names and each step's locked values
    char labelTop[2][10] = { { 0 } };
    char labelBot[2][10] = { { 0 } };
    char lockTop[kSteps][2][8] = { { { 0 } } };
    char lockBot[kSteps][2][8] = { { { 0 } } };

    // the info strip: the 4 coloured-button values of the cursor step, then the length
    char cloudLabel[4][10] = { { 0 } };
    char cloudValue[4][10] = { { 0 } };
    int cloudLit = -1; // the held button's cloud
    char lengthText[12] = { 0 };
    char pageText[8] = { 0 }; // "2/4": this page of 8 steps of all
    int firstStep = 0;        // the first step on screen (0-based)

    // the selected track: steps that have effects (FX1 / FX2): a small mark under the hit
    uint8_t fxMark = 0;
    // Shift + FX1 / FX2 held: the effect list drops down from that box
    static constexpr int kFxListMax = 16;
    int fxList = -1;     // the box it drops from (2 = FX1, 3 = FX2), -1 = closed
    int fxCount = 0;
    int fxSel = 0;       // the effect the step has
    const char* fxNames[kFxListMax] = { nullptr };

    bool shift = false;
    BottomBar bar; // with Shift on: the CHIMP OUT effects (else the 8 steps on screen)
    char message[48] = { 0 }; // shown instead of the bands (no drum tracks)
    HelpView help;
};

class DrumOverviewControl : public cDisplayControl
{
  public:
    DrumOverviewControl(strControlProperties* properties = nullptr);
    virtual ~DrumOverviewControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (DrumOverviewView*)data; }

  private:
    DrumOverviewView* view = nullptr;
};

} // namespace gibgob

#endif
