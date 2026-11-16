#ifndef GIBGOB_UI_H_
#define GIBGOB_UI_H_

// Shared look & feel for the GibGob OS screens.

#include <stdint.h>

namespace gibgob
{

// ---- latched Shift (press Shift once = on, again = off) ----
extern bool g_shiftLatched;
bool shiftActive(); // latched or physically held

// ---- palette ----
namespace color
{
constexpr uint32_t kBg = 0x101418;
constexpr uint32_t kPanel = 0x1C2329;
constexpr uint32_t kText = 0xE6E9EC;
constexpr uint32_t kTextDim = 0x8A949C;
constexpr uint32_t kTextDark = 0x101418;
constexpr uint32_t kShiftBorder = 0xFF2A3A;
constexpr uint32_t kSelect = 0xFFFFFF;

constexpr uint32_t kDrum = 0xFF8A3D;
constexpr uint32_t kMelodic = 0x3FB8AF;

// match the Note / Instr / FX1 / FX2 button colours
constexpr uint32_t kNoteBtn = 0x4CD964;  // green
constexpr uint32_t kInstrBtn = 0xFFD60A; // yellow
constexpr uint32_t kFx1Btn = 0xB46CFF;   // purple
constexpr uint32_t kFx2Btn = 0x5AC8FA;   // light blue
} // namespace color

uint32_t scaleColor(uint32_t c, int num, int den);

// ---- bottom bar: 8 boxes for the 8 top-row buttons ----
struct BottomBar
{
    const char* text[8] = { nullptr };
    uint32_t fill[8] = { 0 };     // 0 = default panel colour
    bool highlighted[8] = { false };
};

constexpr int kScreenW = 800;
constexpr int kBottomBarY = 426;
constexpr int kBottomBarH = 52;

// draw calls; must be used inside a control's update()
void drawBottomBarRects(const BottomBar& bar);
void drawBottomBarText(const BottomBar& bar, int fontHandle);
void drawShiftBorder(int x, int y, int w, int h);

} // namespace gibgob

#endif
