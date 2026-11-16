#include "gibgob/PianoRollControl.h"

#include "FT812.h"
#include "display.h"
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

constexpr int kKeyW = 50;
constexpr int kGridTop = kInfoY + kInfoH + 3;
constexpr int kGridBottom = kBottomBarY - 5;
constexpr int kGridX1 = kKeyW;
constexpr int kGridX2 = kScreenW - 6;
constexpr int kReserve = 70; // words kept for ghost, box, cursor, playhead, border

inline bool isBlackKey(int pitch)
{
    int n = pitch % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
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

void PianoRollControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;

    const int rows = view->rowsVisible > 0 ? view->rowsVisible : 12;
    const int steps = view->stepsVisible > 0 ? view->stepsVisible : 16;
    const int rh = (kGridBottom - kGridTop) / rows;
    const int gy1 = kGridTop;
    const int gy2 = gy1 + rh * rows - 1;
    const int gridW = kGridX2 - kGridX1;
    const int ticksVisible = steps * view->gridTicks;
    const int firstTick = view->firstTick;
    const int lastTick = firstTick + ticksVisible;
    const int bottomPitch = view->topPitch - rows + 1;
    const int cellW = gridW / steps;
    const bool tiny = rh < 12 || cellW < 12;
    const int pad = tiny ? 1 : 2;

    auto tickToX = [&](int tick) { return kGridX1 + ((tick - firstTick) * gridW) / ticksVisible; };
    auto pitchToY = [&](int pitch) { return gy1 + (view->topPitch - pitch) * rh; };

    draw::begin(kDrawBudget);

    // ---- background ----
    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    // ---- rows: black keys darker, C rows tinted (drawn per colour) ----
    draw::color(kRowBlack);
    for (int r = 0; r < rows; r++)
    {
        int pitch = view->topPitch - r;
        if (isBlackKey(pitch))
            draw::rect(kGridX1, gy1 + r * rh, kGridX2, gy1 + r * rh + rh - 1);
    }
    draw::color(kRowC);
    for (int r = 0; r < rows; r++)
    {
        int pitch = view->topPitch - r;
        if (pitch % 12 == 0)
            draw::rect(kGridX1, gy1 + r * rh, kGridX2, gy1 + r * rh + rh - 1);
    }

    // ---- crosshair on the cursor row and column (helps when zoomed out) ----
    draw::color(kCrosshair);
    if (view->cursorPitch <= view->topPitch && view->cursorPitch >= bottomPitch)
    {
        int y = pitchToY(view->cursorPitch);
        draw::rect(kGridX1, y, kGridX2, y + rh - 1);
    }
    if (view->cursorTick >= firstTick && view->cursorTick < lastTick)
    {
        draw::rect(tickToX(view->cursorTick), gy1, tickToX(view->cursorTick + view->gridTicks) - 1, gy2);
    }

    // ---- past the pattern end ----
    if (view->patternTicks < lastTick)
    {
        int x = tickToX(view->patternTicks > firstTick ? view->patternTicks : firstTick);
        draw::color(kOutside);
        draw::rect(x, gy1, kGridX2, gy2);
    }

    // ---- grid lines ----
    if (cellW >= 8)
    {
        draw::color(kGridStep);
        for (int s = firstTick / kTicksPerStep; s * kTicksPerStep <= lastTick; s++)
        {
            if (s % 4 == 0)
                continue;
            int x = tickToX(s * kTicksPerStep);
            if (x >= kGridX1 && x <= kGridX2)
                draw::rect(x, gy1, x, gy2);
        }
    }
    if (rh >= 8)
    {
        draw::color(kGridStep);
        for (int r = 1; r < rows; r++)
            draw::rect(kGridX1, gy1 + r * rh, kGridX2, gy1 + r * rh);
    }
    draw::color(kGridBeat);
    for (int s = firstTick / kTicksPerStep; s * kTicksPerStep <= lastTick; s++)
    {
        if (s % 4 != 0)
            continue;
        int x = tickToX(s * kTicksPerStep);
        if (x >= kGridX1 && x <= kGridX2)
            draw::rect(x, gy1, x, gy2);
    }

    // ---- piano keys ----
    draw::color(kKeyWhite);
    for (int r = 0; r < rows; r++)
    {
        if (!isBlackKey(view->topPitch - r))
            draw::rect(2, gy1 + r * rh, kKeyW - 3, gy1 + r * rh + (rh > 2 ? rh - 2 : rh));
    }
    draw::color(kKeyBlack);
    for (int r = 0; r < rows; r++)
    {
        if (isBlackKey(view->topPitch - r))
            draw::rect(2, gy1 + r * rh, kKeyW - 3, gy1 + r * rh + (rh > 2 ? rh - 2 : rh));
    }

    // key labels: when rows get small only the C notes are named
    draw::color(color::kTextDark);
    for (int r = 0; r < rows; r++)
    {
        int pitch = view->topPitch - r;
        if (pitch < 0 || pitch > 127 || isBlackKey(pitch))
            continue;
        if (rh < 14 && pitch % 12 != 0)
            continue;
        draw::text(6, gy1 + r * rh + rh / 2, fRegular, OPT_CENTERY, mtNotes[pitch]);
    }
    if (rh >= 14)
    {
        draw::color(color::kText);
        for (int r = 0; r < rows; r++)
        {
            int pitch = view->topPitch - r;
            if (pitch < 0 || pitch > 127 || !isBlackKey(pitch))
                continue;
            draw::text(6, gy1 + r * rh + rh / 2, fRegular, OPT_CENTERY, mtNotes[pitch]);
        }
    }

    // ---- fixed furniture before the notes, so notes can never push it out ----
    drawInfoStrip(view->title, view->mode, view->modeColor, view->params, view->selectedParam, fRegular, fBold);
    drawBottomBar(view->bar, fBold);

    // ---- notes: selected rims first, then bodies in three brightness passes ----
    if (view->pattern)
    {
        int count = noteCount(view->pattern, view->track);

        auto noteRect = [&](const Note& n, int& x1, int& y1, int& x2, int& y2) -> bool {
            if (n.pitch > view->topPitch || n.pitch < bottomPitch)
                return false;
            int s = n.start, e = n.start + n.length;
            if (e <= firstTick || s >= lastTick)
                return false;
            x1 = tickToX(s < firstTick ? firstTick : s);
            x2 = tickToX(e > lastTick ? lastTick : e) - pad;
            if (x2 < x1 + 2)
                x2 = x1 + 2;
            y1 = pitchToY(n.pitch) + pad;
            y2 = pitchToY(n.pitch) + rh - 1 - pad;
            if (y2 < y1 + 1)
                y2 = y1 + 1;
            return true;
        };

        draw::color(color::kSelect);
        for (int i = 0; i < count; i++)
        {
            Note n = getNote(view->pattern, view->track, i);
            if (!(n.flags & kNoteFlagSelected))
                continue;
            int x1, y1, x2, y2;
            if (noteRect(n, x1, y1, x2, y2) && draw::room(3 + kReserve))
                draw::rect(x1 - 1, y1 - 1, x2 + 1, y2 + 1);
        }

        for (int pass = 0; pass < 6; pass++)
        {
            bool selPass = pass >= 3;
            int level = pass % 3; // 0 = soft, 1 = medium, 2 = loud
            draw::color(scaleColor(selPass ? kNoteSelected : kNote, level == 2 ? 100 : (level == 1 ? 78 : 52), 100));
            for (int i = 0; i < count; i++)
            {
                Note n = getNote(view->pattern, view->track, i);
                bool sel = (n.flags & kNoteFlagSelected) != 0;
                if (sel != selPass)
                    continue;
                int v = n.velocity ? n.velocity : 100;
                int noteLevel = v >= 110 ? 2 : (v >= 70 ? 1 : 0);
                if (n.chance() < 100 && noteLevel > 0)
                    noteLevel--; // notes with a chance look fainter
                if (noteLevel != level)
                    continue;
                int x1, y1, x2, y2;
                if (noteRect(n, x1, y1, x2, y2) && draw::room(3 + kReserve))
                    draw::rect(x1, y1, x2, y2);
            }
        }
    }

    // ---- ghost notes ----
    int gs = view->cursorTick + view->ghostNudge;
    int ge = gs + view->ghostLength;
    if (view->showGhost)
    {
        draw::color(color::kSelect);
        draw::alpha(80);
        for (int i = 0; i < view->ghostCount; i++)
        {
            int p = view->ghostPitches[i];
            if (p > view->topPitch || p < bottomPitch)
                continue;
            int x1 = tickToX(gs < firstTick ? firstTick : gs);
            int x2 = tickToX(ge > lastTick ? lastTick : ge) - pad;
            if (x2 < x1 + 2)
                x2 = x1 + 2;
            int y1 = pitchToY(p) + pad;
            int y2 = pitchToY(p) + rh - 1 - pad;
            if (y2 < y1 + 1)
                y2 = y1 + 1;
            draw::rect(x1, y1, x2, y2);
        }
        draw::alpha(255);
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
        if (t1 < lastTick && t2 > firstTick && pHi >= pLo)
        {
            int x1 = tickToX(t1 < firstTick ? firstTick : t1);
            int x2 = tickToX(t2 > lastTick ? lastTick : t2);
            int y1 = pitchToY(pHi);
            int y2 = pitchToY(pLo) + rh - 1;
            draw::color(color::kBox);
            draw::alpha(45);
            draw::rect(x1, y1, x2, y2);
            draw::alpha(255);
            draw::frame(x1, y1, x2, y2, 2);
        }
    }

    // ---- playhead ----
    if (view->playheadTick >= firstTick && view->playheadTick < lastTick)
    {
        int x = tickToX(view->playheadTick);
        draw::color(color::kPlayhead);
        draw::rect(x, gy1, x + 1, gy2);
    }

    // ---- cursor outline (never smaller than 6 px so it stays findable) ----
    if (view->cursorPitch <= view->topPitch && view->cursorPitch >= bottomPitch &&
        view->cursorTick >= firstTick && view->cursorTick < lastTick)
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
        draw::color(color::kSelect);
        draw::frame(x1, y1, x2, y2, 2);
    }

    if (view->shift)
        drawShiftBorder();

    draw::end();
}

} // namespace gibgob
