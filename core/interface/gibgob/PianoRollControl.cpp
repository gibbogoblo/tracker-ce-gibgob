#include "gibgob/PianoRollControl.h"

#include "FT812.h"
#include "display.h"
#include "mtStructs.h"
#include <stdio.h>

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
constexpr int kReserve = 120; // words kept for ghost, box, cursor, playhead, border

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

    if (drawHelp(view->help, fRegular, fBold))
    {
        draw::end();
        return;
    }
    draw::setLighten(view->shift);

    // ---- background ----
    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    // ---- rows: black keys darker, C rows tinted (drawn per colour) ----
    const bool slices = view->sliceCount > 0;
    const int lastSlice = kFirstSliceNote + view->sliceCount - 1;
    if (slices)
    {
        // every other slice a little darker; rows past the slices greyed out
        draw::color(kRowBlack);
        for (int r = 0; r < rows; r++)
        {
            int pitch = view->topPitch - r;
            if (pitch >= kFirstSliceNote && pitch <= lastSlice && ((pitch - kFirstSliceNote) & 1))
                draw::rect(kGridX1, gy1 + r * rh, kGridX2, gy1 + r * rh + rh - 1);
        }
        draw::color(kOutside);
        for (int r = 0; r < rows; r++)
        {
            int pitch = view->topPitch - r;
            if (pitch < kFirstSliceNote || pitch > lastSlice)
                draw::rect(kGridX1, gy1 + r * rh, kGridX2, gy1 + r * rh + rh - 1);
        }
    }
    else
    {
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

    const bool overlayOpen = view->menu.active || view->picker.active;

    // ---- grid lines ----
    if (cellW >= 8 && !overlayOpen)
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
    if (rh >= 8 && !overlayOpen)
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

    // ---- slice keys: "S1".."Sn", slices on a top button in the slice colour ----
    if (slices)
    {
        for (int r = 0; r < rows; r++)
        {
            int pitch = view->topPitch - r;
            if (pitch < kFirstSliceNote || pitch > lastSlice)
                continue;
            bool onButton = false;
            for (int b = 0; b < 8; b++)
                onButton = onButton || (kFirstSliceNote + view->sliceButton[b] == pitch);
            draw::color(onButton ? color::kSlice : kKeyWhite);
            draw::rect(2, gy1 + r * rh, kKeyW - 3, gy1 + r * rh + (rh > 2 ? rh - 2 : rh));
        }
        const bool few = rh < 14 || overlayOpen;
        draw::color(color::kTextDark);
        for (int r = 0; r < rows; r++)
        {
            int pitch = view->topPitch - r;
            if (pitch < kFirstSliceNote || pitch > lastSlice)
                continue;
            int s = pitch - kFirstSliceNote;
            if (few && (s % 4) != 0)
                continue;
            char t[6];
            snprintf(t, sizeof(t), "S%d", s + 1);
            draw::text(6, gy1 + r * rh + rh / 2, fRegular, OPT_CENTERY, t);
        }
    }
    else
    {
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
    const bool fewLabels = rh < 14 || overlayOpen;
    draw::color(color::kTextDark);
    for (int r = 0; r < rows; r++)
    {
        int pitch = view->topPitch - r;
        if (pitch < 0 || pitch > 127 || isBlackKey(pitch))
            continue;
        if (fewLabels && pitch % 12 != 0)
            continue;
        draw::text(6, gy1 + r * rh + rh / 2, fRegular, OPT_CENTERY, mtNotes[pitch]);
    }
    if (!fewLabels)
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
    } // piano keys

    // ---- fixed furniture before the notes, so notes can never push it out ----
    drawInfoStrip(10, view->title, view->mode, view->modeColor, view->params, view->selectedParam, fRegular, fBold);
    drawBottomBar(view->bar, fBold);

    const bool overlay = view->menu.active || view->picker.active;

    // ---- notes: selected rims first, then bodies in three brightness passes ----
    if (view->pattern && !overlay)
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
    if (view->showGhost && !overlay)
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

    // ---- menu / note picker on top ----
    if (view->picker.active)
    {
        const PickerView& pk = view->picker;
        const int x0 = 20, x1 = kScreenW - 20, y0 = 120, y1 = 330;
        draw::color(color::kPanel);
        draw::rect(x0 - 10, y0 - 50, x1 + 10, y1 + 40);
        int keyW = (x1 - x0) / pk.keys;
        for (int k = 0; k < pk.keys; k++)
        {
            int pitch = pk.firstPitch + k;
            if (pitch > 127)
                break;
            bool black = isBlackKey(pitch);
            bool picked = (pk.mask[pitch >> 3] >> (pitch & 7)) & 1;
            int kx = x0 + k * keyW;
            uint32_t col = picked ? color::kInstrBtn : (black ? kKeyBlack : kKeyWhite);
            draw::color(col);
            draw::rect(kx + 1, black ? y0 : y0 + 30, kx + keyW - 2, y1);
        }
        int ck = pk.cursor - pk.firstPitch;
        if (ck >= 0 && ck < pk.keys)
        {
            int kx = x0 + ck * keyW;
            draw::color(color::kRec);
            draw::frame(kx - 1, y0 - 4, kx + keyW, y1 + 4, 3);
        }
        draw::color(color::kText);
        draw::text(x0, y0 - 30, fBold, OPT_CENTERY, "Pick notes for the acid line");
        draw::text(x1, y0 - 30, fBold, OPT_CENTERY | OPT_RIGHTX, mtNotes[pk.cursor < 0 ? 0 : (pk.cursor > 127 ? 127 : pk.cursor)]);
        draw::color(color::kTextDark);
        for (int k = 0; k < pk.keys; k++)
        {
            int pitch = pk.firstPitch + k;
            if (pitch > 127 || pitch % 12 != 0)
                continue;
            draw::text(x0 + k * keyW + 2, y1 - 12, fRegular, OPT_CENTERY, mtNotes[pitch]);
        }
        draw::color(color::kTextDim);
        draw::text(x0, y1 + 22, fRegular, OPT_CENTERY,
                   "Left/Right: move   Up/Down: octave   Middle: pick / unpick   7: Apply   8: Back");
    }
    else if (view->menu.active)
    {
        const PianoMenuView& m = view->menu;
        const int x0 = 140, x1 = kScreenW - 140;
        const int rowH = 30;
        const int top = 96;
        int bodyRows = m.cols == 2 ? 2 : m.rows;
        int y1 = top + 40 + bodyRows * rowH + 34;
        draw::color(color::kPanel);
        draw::rect(x0, top, x1, y1);
        draw::color(m.accent);
        draw::rect(x0, top, x1, top + 3);

        if (m.cols == 2)
        {
            // one side or the other: the side not chosen is dimmed and its
            // amount is hidden, because Quantize and Humanize undo each other
            int colW = (x1 - x0 - 30) / 2;
            for (int c2 = 0; c2 < 2; c2++)
            {
                int cx = x0 + 10 + c2 * (colW + 10);
                bool colSel = (c2 == m.colCursor);
                int ty = top + 40;
                if (colSel)
                {
                    draw::color(m.cursor < 0 ? m.accent : scaleColor(m.accent, 45, 100));
                    draw::rect(cx, ty, cx + colW, ty + rowH - 4);
                    if (m.cursor >= 0)
                    {
                        draw::color(m.accent);
                        draw::rect(cx, ty + rowH, cx + colW, ty + 2 * rowH - 4);
                    }
                }
                else
                {
                    draw::color(color::kBg);
                    draw::rect(cx, ty, cx + colW, ty + rowH - 4);
                }
            }
            for (int c2 = 0; c2 < 2; c2++)
            {
                int cx = x0 + 10 + c2 * (colW + 10);
                bool colSel = (c2 == m.colCursor);
                int ty = top + 40;
                draw::color(!colSel ? color::kTextDim : (m.cursor < 0 ? color::kTextDark : color::kText));
                draw::text(cx + 8, ty + (rowH - 4) / 2, fBold, OPT_CENTERY, m.colTitle[c2]);
                if (!colSel)
                    continue;
                int vy = ty + rowH;
                draw::color(m.cursor >= 0 ? color::kTextDark : color::kText);
                draw::text(cx + 8, vy + (rowH - 4) / 2, fRegular, OPT_CENTERY, m.row[c2].label);
                draw::text(cx + colW - 8, vy + (rowH - 4) / 2, fBold, OPT_CENTERY | OPT_RIGHTX, m.row[c2].value);
            }
        }
        else
        {
            for (int r = 0; r < m.rows; r++)
            {
                if (r != m.cursor)
                    continue;
                int ry = top + 40 + r * rowH;
                draw::color(m.accent);
                draw::rect(x0 + 10, ry, x1 - 10, ry + rowH - 4);
            }
            for (int r = 0; r < m.rows; r++)
            {
                int ry = top + 40 + r * rowH + (rowH - 4) / 2;
                bool cur = (r == m.cursor);
                draw::color(cur ? color::kTextDark : (m.row[r].isAction ? m.accent : color::kText));
                draw::text(x0 + 20, ry, m.row[r].isAction || cur ? fBold : fRegular, OPT_CENTERY, m.row[r].label);
                draw::text(x1 - 20, ry, fBold, OPT_CENTERY | OPT_RIGHTX, m.row[r].value);
            }
        }
        draw::color(color::kText);
        draw::text(x0 + 12, top + 20, fBold, OPT_CENTERY, m.title);
        draw::color(color::kTextDim);
        draw::text(x0 + 12, y1 - 16, fRegular, OPT_CENTERY, m.hint);
    }

    if (view->shift)
        drawShiftBorder();

    draw::end();
}

} // namespace gibgob
