#include "gibgob/PianoRollControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/MelodicTrack.h"
#include "mtStructs.h"

namespace gibgob
{

namespace
{
constexpr uint32_t kRowBlack = 0x0B0E11;
constexpr uint32_t kRowC = 0x18222B;
constexpr uint32_t kGridStep = 0x20282F;
constexpr uint32_t kGridBeat = 0x3A4752;
constexpr uint32_t kOutside = 0x050607;
constexpr uint32_t kKeyWhite = 0xC8CDD2;
constexpr uint32_t kKeyBlack = 0x2A3036;
constexpr uint32_t kNote = 0x3FB8AF;
constexpr uint32_t kNoteSelected = 0xFFB347;
constexpr uint32_t kCrosshair = 0x26313A;
constexpr uint32_t kPlayhead = 0xFF4F5E;
constexpr uint32_t kBox = 0xFFB347;

constexpr int kKeyW = 50;
constexpr int kInfoY = 31;
constexpr int kInfoH = 34;
constexpr int kGridTop = kInfoY + kInfoH + 2;
constexpr int kGridBottom = kBottomBarY - 4;

inline bool isBlackKey(int pitch)
{
    int n = pitch % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

inline void rect(int x1, int y1, int x2, int y2)
{
    API_VERTEX2F(x1, y1);
    API_VERTEX2F(x2, y2);
}
} // namespace

PianoRollControl::PianoRollControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobPianoRoll, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (PianoRollView*)properties->data;
    }
}

int PianoRollControl::gridX1() const
{
    return kKeyW;
}
int PianoRollControl::gridX2() const
{
    return kScreenW - 6;
}
int PianoRollControl::gridY1() const
{
    return kGridTop;
}
int PianoRollControl::rowHeight() const
{
    int rows = view->rowsVisible > 0 ? view->rowsVisible : 12;
    return (kGridBottom - kGridTop) / rows;
}
int PianoRollControl::ticksVisible() const
{
    return view->stepsVisible * view->gridTicks;
}
int PianoRollControl::tickToX(int tick) const
{
    int w = gridX2() - gridX1();
    return gridX1() + ((tick - view->firstTick) * w) / ticksVisible();
}
int PianoRollControl::pitchToY(int pitch) const
{
    return gridY1() + (view->topPitch - pitch) * rowHeight();
}

void PianoRollControl::update()
{
    if (view == nullptr)
        return;

    const int gx1 = gridX1();
    const int gx2 = gridX2();
    const int gy1 = gridY1();
    const int rh = rowHeight();
    const int gy2 = gy1 + rh * view->rowsVisible - 1;
    const int bottomPitch = view->topPitch - view->rowsVisible + 1;
    const int firstTick = view->firstTick;
    const int lastTick = firstTick + ticksVisible();
    const int cellW = (gx2 - gx1) / (view->stepsVisible > 0 ? view->stepsVisible : 1);
    const bool tiny = rh < 12 || cellW < 12; // zoomed far out

    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    API_BEGIN(RECTS);

    // ---- background ----
    API_COLOR(color::kBg);
    rect(0, posY, kScreenW - 1, posY + height - 1);

    // ---- rows ----
    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        int y = gy1 + r * rh;
        if (isBlackKey(pitch))
        {
            API_COLOR(kRowBlack);
            rect(gx1, y, gx2, y + rh - 1);
        }
        else if (pitch % 12 == 0)
        {
            API_COLOR(kRowC);
            rect(gx1, y, gx2, y + rh - 1);
        }
    }

    // ---- crosshair: highlight cursor row and column (helps when zoomed out) ----
    if (view->cursorPitch <= view->topPitch && view->cursorPitch >= bottomPitch)
    {
        API_COLOR(kCrosshair);
        int y = pitchToY(view->cursorPitch);
        rect(gx1, y, gx2, y + rh - 1);
    }
    if (view->cursorTick >= firstTick && view->cursorTick < lastTick)
    {
        API_COLOR(kCrosshair);
        rect(tickToX(view->cursorTick), gy1, tickToX(view->cursorTick + view->gridTicks) - 1, gy2);
    }

    // ---- past pattern end ----
    if (view->patternTicks < lastTick)
    {
        int x = tickToX(view->patternTicks > firstTick ? view->patternTicks : firstTick);
        API_COLOR(kOutside);
        rect(x, gy1, gx2, gy2);
    }

    // ---- vertical grid lines (skip single steps when squares get tiny) ----
    for (int s = firstTick / 48; s * 48 <= lastTick; s++)
    {
        bool beat = (s % 4 == 0);
        if (!beat && cellW < 8)
            continue;
        int x = tickToX(s * 48);
        if (x < gx1 || x > gx2)
            continue;
        API_COLOR(beat ? kGridBeat : kGridStep);
        rect(x, gy1, x, gy2);
    }
    if (rh >= 8)
    {
        API_COLOR(kGridStep);
        for (int r = 1; r < view->rowsVisible; r++)
        {
            int y = gy1 + r * rh;
            rect(gx1, y, gx2, y);
        }
    }

    // ---- notes ----
    const int pad = tiny ? 1 : 2;
    if (view->pattern)
    {
        int count = noteCount(view->pattern, view->track);
        for (int i = 0; i < count; i++)
        {
            Note n = getNote(view->pattern, view->track, i);
            if (n.pitch > view->topPitch || n.pitch < bottomPitch)
                continue;
            int s = n.start, e = n.start + n.length;
            if (e <= firstTick || s >= lastTick)
                continue;
            int x1 = tickToX(s < firstTick ? firstTick : s);
            int x2 = tickToX(e > lastTick ? lastTick : e) - pad;
            if (x2 < x1 + 2)
                x2 = x1 + 2;
            int y1 = pitchToY(n.pitch) + pad;
            int y2 = pitchToY(n.pitch) + rh - 1 - pad;
            if (y2 < y1 + 1)
                y2 = y1 + 1;
            bool sel = n.flags & kNoteFlagSelected;
            if (sel)
            {
                // white rim makes selected notes visible even when tiny
                API_COLOR(color::kSelect);
                rect(x1 - 1, y1 - 1, x2 + 1, y2 + 1);
            }
            int v = n.velocity ? n.velocity : 100;
            API_COLOR(scaleColor(sel ? kNoteSelected : kNote, 40 + (60 * v) / 127, 100));
            rect(x1, y1, x2, y2);
        }
    }

    // ---- ghost notes ----
    int gs = view->cursorTick + view->ghostNudge;
    int ge = gs + view->ghostLength;
    if (view->showGhost)
    {
        API_COLOR(color::kSelect);
        API_COLOR_A(80);
        for (int i = 0; i < view->ghostCount; i++)
        {
            int p = view->ghostPitches[i];
            if (p > view->topPitch || p < bottomPitch)
                continue;
            int x1 = tickToX(gs < firstTick ? firstTick : gs);
            int x2 = tickToX(ge > lastTick ? lastTick : ge) - pad;
            int y1 = pitchToY(p) + pad;
            rect(x1, y1, x2, y1 + rh - 1 - 2 * pad);
        }
        API_COLOR_A(255);
    }

    // ---- box selection ----
    if (view->boxActive)
    {
        int t1 = view->boxTick1 < view->boxTick2 ? view->boxTick1 : view->boxTick2;
        int t2 = (view->boxTick1 < view->boxTick2 ? view->boxTick2 : view->boxTick1) + view->gridTicks;
        int pHi = view->boxPitch1 > view->boxPitch2 ? view->boxPitch1 : view->boxPitch2;
        int pLo = view->boxPitch1 > view->boxPitch2 ? view->boxPitch2 : view->boxPitch1;
        if (pHi > view->topPitch)
            pHi = view->topPitch;
        if (pLo < bottomPitch)
            pLo = bottomPitch;
        int x1 = tickToX(t1 < firstTick ? firstTick : t1);
        int x2 = tickToX(t2 > lastTick ? lastTick : t2);
        int y1 = pitchToY(pHi);
        int y2 = pitchToY(pLo) + rh - 1;
        API_COLOR(kBox);
        API_COLOR_A(45);
        rect(x1, y1, x2, y2);
        API_COLOR_A(255);
        rect(x1, y1, x2, y1 + 1);
        rect(x1, y2 - 1, x2, y2);
        rect(x1, y1, x1 + 1, y2);
        rect(x2 - 1, y1, x2, y2);
    }

    // ---- playhead ----
    if (view->playheadTick >= firstTick && view->playheadTick < lastTick)
    {
        int x = tickToX(view->playheadTick);
        API_COLOR(kPlayhead);
        rect(x, gy1, x + 1, gy2);
    }

    // ---- cursor outline (always at least 6 px so it stays findable) ----
    if (view->cursorPitch <= view->topPitch && view->cursorPitch >= bottomPitch)
    {
        int x1 = tickToX(view->cursorTick);
        int x2 = tickToX(view->cursorTick + view->gridTicks) - 1;
        if (view->showGhost)
            x2 = tickToX(ge > lastTick ? lastTick : ge) - 1;
        if (x2 < x1 + 6)
            x2 = x1 + 6;
        int y1 = pitchToY(view->cursorPitch);
        int y2 = y1 + rh - 1;
        if (y2 < y1 + 6)
            y2 = y1 + 6;
        API_COLOR(color::kSelect);
        rect(x1, y1, x2, y1 + 1);
        rect(x1, y2 - 1, x2, y2);
        rect(x1, y1, x1 + 1, y2);
        rect(x2 - 1, y1, x2, y2);
    }

    // ---- piano keys ----
    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        int y = gy1 + r * rh;
        API_COLOR(isBlackKey(pitch) ? kKeyBlack : kKeyWhite);
        rect(2, y, kKeyW - 3, y + (rh > 2 ? rh - 2 : rh));
    }

    // ---- info strip with colour-coded value boxes ----
    API_COLOR(color::kPanel);
    rect(2, kInfoY, kScreenW - 3, kInfoY + kInfoH - 1);
    const int boxW = 118;
    const int boxX0 = kScreenW - 4 * (boxW + 4) - 2;
    for (int i = 0; i < 4; i++)
    {
        int x1 = boxX0 + i * (boxW + 4);
        bool sel = (i == view->selectedParam);
        API_COLOR(sel ? view->params[i].color : scaleColor(view->params[i].color, 30, 100));
        rect(x1, kInfoY + 3, x1 + boxW, kInfoY + kInfoH - 4);
    }

    // ---- bottom bar ----
    drawBottomBarRects(view->bar);

    if (view->shift)
        drawShiftBorder(0, posY, kScreenW, height);

    API_END();

    // ---- text ----
    const int fSmall = fonts[1].handle; // 14 regular
    const int fBold = fonts[3].handle;  // 14 bold

    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        if (pitch < 0 || pitch > 127)
            continue;
        // when rows get small, only label the C notes
        if (rh < 14 && pitch % 12 != 0)
            continue;
        API_COLOR(isBlackKey(pitch) ? color::kText : color::kTextDark);
        API_CMD_TEXT(6, gy1 + r * rh + rh / 2, fSmall, OPT_CENTERY, mtNotes[pitch]);
    }

    API_COLOR(color::kText);
    API_CMD_TEXT(10, kInfoY + kInfoH / 2, fBold, OPT_CENTERY, view->title);
    if (view->mode[0])
    {
        API_COLOR(kBox);
        API_CMD_TEXT(boxX0 - 8, kInfoY + kInfoH / 2, fBold, OPT_CENTERY | OPT_RIGHTX, view->mode);
    }

    for (int i = 0; i < 4; i++)
    {
        int x1 = boxX0 + i * (boxW + 4);
        bool sel = (i == view->selectedParam);
        API_COLOR(sel ? color::kTextDark : view->params[i].color);
        API_CMD_TEXT(x1 + 6, kInfoY + kInfoH / 2, fSmall, OPT_CENTERY, view->params[i].label);
        API_CMD_TEXT(x1 + boxW - 6, kInfoY + kInfoH / 2, fBold, OPT_CENTERY | OPT_RIGHTX, view->params[i].value);
    }

    drawBottomBarText(view->bar, fBold);
}

} // namespace gibgob
