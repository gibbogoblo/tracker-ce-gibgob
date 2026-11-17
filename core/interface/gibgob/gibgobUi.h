#ifndef GIBGOB_UI_H_
#define GIBGOB_UI_H_

// Shared look & feel and drawing helpers for the GibGob OS screens.

#include <stdint.h>

class InputProcessor;

namespace gibgob
{

// binds the buttons that jump to the stock Polyend screens (Fn keys, menu)
void bindModuleSwitchButtons(InputProcessor* ip, void (*switchFunct)(uint8_t));

// ---- global UI state ----
extern bool g_shiftLatched; // Shift is a toggle on GibGob screens
extern bool g_recArmed;     // Rec is a toggle: live recording while playing
bool shiftActive();         // Shift functions are on (the toggle)

// Feed every Shift button event here. A short press toggles Shift when it
// is released; holding Shift (e.g. for master volume with +/-) does not.
// Presses right after a toggle are ignored (the button can double-click).
// Returns true if the toggle changed.
bool shiftButton(uint8_t state);

// Rec toggle; also turns the live-record metronome on and off
void setRecArmed(bool on);

// The Mini's FX1 and FX2 buttons arrive swapped relative to their names in
// the stock code; these are the physical buttons.
uint8_t physFx1Button();
uint8_t physFx2Button();

// ---- undo ----
// Call before changing the pattern. Repeated presses of the same kind of edit
// within a moment share one undo step (holding +/- doesn't fill the history).
void beginEdit(int kind);
void undoEdit();

// display name of a track: its name if renamed, else its sound's name
void trackDisplayName(int track, char* out, int outLen);

// ---- palette ----
namespace color
{
constexpr uint32_t kBg = 0x121218;
constexpr uint32_t kPanel = 0x26262E;
constexpr uint32_t kLine = 0x585764;
constexpr uint32_t kText = 0xE6E9EC;
constexpr uint32_t kTextDim = 0x8A949C;
constexpr uint32_t kTextDark = 0x101418;
constexpr uint32_t kShiftBorder = 0xB85548;
constexpr uint32_t kRec = 0xE0604F;
constexpr uint32_t kSelect = 0xFFFFFF;
constexpr uint32_t kPlayhead = 0xFF4F5E;
constexpr uint32_t kBox = 0xFFB347;

constexpr uint32_t kDrum = 0xB85548;
constexpr uint32_t kMelodic = 0x5F53B8;

// match the Note / Instr / FX1 / FX2 button colours
constexpr uint32_t kNoteBtn = 0x53B886;  // green
constexpr uint32_t kInstrBtn = 0xB8AA53; // yellow
constexpr uint32_t kFx1Btn = 0x7B6FD6;   // purple (lifted so text on it stays readable)
constexpr uint32_t kFx2Btn = 0x5AC8FA;   // light blue
} // namespace color

uint32_t scaleColor(uint32_t c, int num, int den);

// ---- budgeted drawing ----
// The display chip can only hold about 2000 drawing commands for the whole
// screen. If a screen asks for more, it is dropped completely. These helpers
// count what is used and quietly skip drawing once the budget is spent, so a
// busy pattern loses some detail instead of the whole screen.
namespace draw
{
void begin(int budgetWords);
int used();
int skipped(); // drawing calls dropped for lack of room since begin()
bool room(int words);
void color(uint32_t c);
void alpha(int a);                              // 0..255
void rect(int x1, int y1, int x2, int y2);
void frame(int x1, int y1, int x2, int y2, int t); // outline of thickness t
void text(int x, int y, int fontHandle, uint16_t options, const char* str);
void end();
} // namespace draw

constexpr int kScreenW = 800;
constexpr int kScreenTop = 29;   // below the title bar
constexpr int kScreenH = 451;
constexpr int kBottomBarY = 426;
constexpr int kBottomBarH = 52;
constexpr int kDrawBudget = 1350;

// ---- bottom bar: 8 boxes for the 8 top-row buttons ----
struct BottomBar
{
    const char* text[8] = { nullptr };
    uint32_t fill[8] = { 0 }; // 0 = default panel colour
    bool highlighted[8] = { false };
};
void drawBottomBar(const BottomBar& bar, int fontHandle);
void drawShiftBorder();

// ---- four colour-coded value boxes (Note / Instr / FX1 / FX2) ----
struct ParamBox
{
    char label[10];
    char value[10];
    uint32_t color;
};
constexpr int kInfoY = 31;
constexpr int kInfoH = 34;
// draws the info strip: title on the left, optional mode tag, 4 value boxes
void drawInfoStrip(const char* title,
                   const char* mode,
                   uint32_t modeColor,
                   const ParamBox* params,
                   int selectedParam,
                   int fontRegular,
                   int fontBold);

} // namespace gibgob

#endif
