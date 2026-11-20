#ifndef GIBGOB_UI_H_
#define GIBGOB_UI_H_

// Shared look & feel and drawing helpers for the GibGob OS screens.

#include <stdint.h>

class InputProcessor;
class ModuleBase;

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

// Rec button: a short press toggles live recording, a 1 s press toggles
// quantized recording (and arms Rec), Shift+Rec is undo.
enum class RecAction : uint8_t
{
    None,
    Toggled,  // short press: Rec on/off
    Undo,     // Shift+Rec
};
RecAction recButton(uint8_t state);
// call from update(): handles the long press and turns live recording off
// when playback stops. Returns true if the screen should be redrawn.
bool recPoll();
// "REC", "Q REC" or "" for the info strip
const char* recModeText();

// options for opening the main sequencer on a given track
uint32_t homeOptions(int track);

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

// Called by every GibGob screen when it opens: Shift starts off.
void screenOpened();

// Button 2 (Params): open the parameters of the sound on this track
// (sample parameters, or the synth's pages).
void openSoundParameters(ModuleBase* from, int track);
// the track whose sound the instrument editor is showing (for the chord
// palette's audition): the track that opened it, or the first track using
// the instrument being edited
int soundParametersTrack();

// Play pressed while stopped: with Rec armed, one bar of count-in first
void playFromStop();

// Big +/- on lengths: jump to the nearest of 16, 32, 48, 64, 128 steps, then
// step between those values.
int snapLength(int steps, int dir);

// ---- shortcut help (Shift + Menu) ----
struct HelpView
{
    bool active = false;
    const char* title = nullptr;
    const char* const* lines = nullptr;
    int count = 0;
};
// opens / closes the help window; while it is open only Menu works
void toggleHelp(HelpView& help, InputProcessor* ip);

// ---- welcome window of a new project: "What are we feeling today?" ----
struct WelcomeView
{
    bool active = false;
    int choice = 0; // 0 = 80, 1 = 140, 2 = 170 BPM
};
static constexpr int kWelcomeChoices = 3;
extern const int kWelcomeBpm[kWelcomeChoices];
extern const char* const kWelcomeGenre[kWelcomeChoices];
void requestWelcome(); // a new project was made: greet it on the main sequencer
bool takeWelcome();    // true once after requestWelcome()
// full-screen welcome; returns true if drawn (the caller then stops)
bool drawWelcome(const WelcomeView& w, int fontRegular, int fontBold);

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
constexpr uint32_t kSlice = 0xD9A23A; // slice tracks (beat-slice samples)

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
void setLighten(bool on); // Shift on: everything a little lighter
void text(int x, int y, int fontHandle, uint16_t options, const char* str);
// an RGB565 picture already in the display chip's memory, top left at x, y
void bitmap(uint32_t address, int w, int h, int x, int y);

// ---- lines and shapes (the Y2K synth editor) ----
void lineWidth(int sixteenths); // 16 = 1 pixel
// a line through n points (xy: x0, y0, x1, y1, ...)
void lineStrip(const int16_t* xy, int n);
// the area under a line through n points, down to the bottom of the clip box
void fillUnder(const int16_t* xy, int n, int clipX1, int clipY1, int clipX2, int clipY2);
// A slanted box: the top edge is moved right by `slant` pixels, the bottom
// edge starts `slant` pixels in from the left ( /____/ ). `under` is the
// colour around it (the corners are painted with it).
void slant(int x1, int y1, int x2, int y2, int slantPx, uint32_t fill, uint32_t under);
// the same with a colour fade from left (c0) to right (c1)
void slantFade(int x1, int y1, int x2, int y2, int slantPx, uint32_t c0, uint32_t c1, uint32_t under);
// paints the corner right of the line from (x2, y1) to (x1, y2) with `under`
void slantEnd(int x1, int y1, int x2, int y2, uint32_t under);
// outline of a slanted box
void slantOutline(int x1, int y1, int x2, int y2, int slantPx, uint32_t c, int sixteenths);
// a box with a fade (vertical: top to bottom, else left to right)
void fadeRect(int x1, int y1, int x2, int y2, uint32_t c0, uint32_t c1, bool vertical);
// a box with round corners of `radius` pixels (a circle when it is square
// with radius = half its size); the chip rounds rectangles by the line width
void roundRect(int x1, int y1, int x2, int y2, int radius);
// box with its top-left and bottom-right corners cut off by `cut` pixels
void cutBox(int x1, int y1, int x2, int y2, int cut, uint32_t fill, uint32_t under);
// display list words one fade costs (the chip's ramp bitmap, up to 27)
constexpr int kFadeWords = 27;
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
// full-screen list of shortcuts; returns true if drawn (the caller then stops)
bool drawHelp(const HelpView& help, int fontRegular, int fontBold);

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
void drawInfoStrip(int titleX,
                   const char* title,
                   const char* mode,
                   uint32_t modeColor,
                   const ParamBox* params,
                   int selectedParam,
                   int fontRegular,
                   int fontBold);

} // namespace gibgob

#endif
