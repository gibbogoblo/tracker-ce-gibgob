#include "gibgob/LaneControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/MelodicTrack.h"

namespace gibgob
{

namespace
{
constexpr uint32_t kLaneBg = 0x151B21;
constexpr uint32_t kLaneBgCursor = 0x1F2932;
constexpr uint32_t kOutside = 0x050607;
constexpr uint32_t kGridStep = 0x232C34;
constexpr uint32_t kGridBeat = 0x3A4752;
constexpr uint32_t kColumn = 0x26313A;
// muted tracks: near-black monochrome
constexpr uint32_t kMutedHead = 0x2A2A2E;
constexpr uint32_t kMutedLane = 0x0C0C0E;
constexpr uint32_t kMutedNote = 0x3C3C42;
constexpr int kPatBoxW = 84;

constexpr int kLanesTop = kInfoY + kInfoH + 3;
constexpr int kLanesBottom = kBottomBarY - 5;
constexpr int kHeadW = 92;
constexpr int kGridX1 = kHeadW + 4;
constexpr int kGridX2 = kScreenW - 6;
constexpr int kMaxNotesPerLane = 48;
constexpr int kReserve = 120; // words kept for cursor, lights, playheads and border
} // namespace

LaneControl::LaneControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobTrackGrid, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (LaneView*)properties->data;
    }
}

void LaneControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;

    const int laneH = (kLanesBottom - kLanesTop) / 8;
    const int steps = view->stepsVisible > 0 ? view->stepsVisible : 16;
    const int ticksVisible = steps * kTicksPerStep;
    const int firstTick = view->firstTick;
    const int lastTick = firstTick + ticksVisible;
    const int gridW = kGridX2 - kGridX1;
    const int cellW = gridW / steps;

    auto tickToX = [&](int tick) { return kGridX1 + ((tick - firstTick) * gridW) / ticksVisible; };

    draw::begin(kDrawBudget);

    if (drawWelcome(view->welcome, fRegular, fBold))
    {
        draw::end();
        return;
    }
    if (drawHelp(view->help, fRegular, fBold))
    {
        draw::end();
        return;
    }
    draw::setLighten(view->shift);

    // ---- background ----
    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    // ---- lanes: header block coloured by type, grid background ----
    for (int t = 0; t < 8; t++)
    {
        const LaneInfo& lane = view->lanes[t];
        int y1 = kLanesTop + t * laneH;
        int y2 = y1 + laneH - 2;
        bool cur = (t == view->cursorTrack);

        uint32_t base = lane.slice ? color::kSlice : (lane.melodic ? color::kMelodic : color::kDrum);
        draw::color(lane.muted ? (cur ? 0x45454B : kMutedHead) : scaleColor(base, cur ? 100 : 55, 100));
        draw::rect(2, y1, kHeadW, y2);

        draw::color(lane.muted ? kMutedLane : (cur ? kLaneBgCursor : kLaneBg));
        draw::rect(kGridX1, y1, kGridX2, y2);
    }

    // ---- past each track's end ----
    draw::color(kOutside);
    for (int t = 0; t < 8; t++)
    {
        int endTick = view->lanes[t].steps * kTicksPerStep;
        if (endTick >= lastTick)
            continue;
        int x = tickToX(endTick > firstTick ? endTick : firstTick);
        int y1 = kLanesTop + t * laneH;
        draw::rect(x, y1, kGridX2, y1 + laneH - 2);
    }

    // ---- cursor column ----
    if (!view->nameCursor && view->cursorTick >= firstTick && view->cursorTick < lastTick)
    {
        draw::color(kColumn);
        draw::rect(tickToX(view->cursorTick), kLanesTop, tickToX(view->cursorTick + kTicksPerStep) - 1,
                   kLanesTop + 8 * laneH - 2);
    }

    // ---- grid lines: beats always, single steps when there is room ----
    int lastPatternTick = view->patternTicks < lastTick ? view->patternTicks : lastTick;
    draw::color(kGridStep);
    if (cellW >= 14)
    {
        for (int s = firstTick / kTicksPerStep; s * kTicksPerStep <= lastPatternTick; s++)
        {
            if (s % 4 == 0)
                continue;
            int x = tickToX(s * kTicksPerStep);
            draw::rect(x, kLanesTop, x, kLanesTop + 8 * laneH - 2);
        }
    }
    draw::color(kGridBeat);
    for (int s = firstTick / kTicksPerStep; s * kTicksPerStep <= lastPatternTick; s++)
    {
        if (s % 4 != 0)
            continue;
        int x = tickToX(s * kTicksPerStep);
        draw::rect(x, kLanesTop, x, kLanesTop + 8 * laneH - 2);
    }
    // gaps between lanes
    draw::color(color::kBg);
    for (int t = 1; t < 8; t++)
    {
        int y = kLanesTop + t * laneH - 1;
        draw::rect(kGridX1, y, kGridX2, y);
    }

    // ---- text and fixed furniture first, so notes can never push it out ----
    for (int t = 0; t < 8; t++)
    {
        const LaneInfo& lane = view->lanes[t];
        int y1 = kLanesTop + t * laneH;
        bool cur = (t == view->cursorTrack);
        draw::color(lane.muted ? color::kTextDim : (cur ? color::kTextDark : color::kText));
        draw::text(7, y1 + laneH / 2 - 1, cur ? fBold : fRegular, OPT_CENTERY, lane.name);
    }

    drawInfoStrip(kPatBoxW + 12, view->title, view->mode, view->modeColor, view->params, view->selectedParam,
                  fRegular, fBold);
    // pattern box: highlighted (blinking) while Shift is on
    draw::color(view->patHighlight ? color::kSelect : color::kLine);
    draw::rect(4, kInfoY + 3, kPatBoxW, kInfoY + kInfoH - 4);
    draw::color(view->patHighlight ? color::kTextDark : color::kText);
    draw::text((4 + kPatBoxW) / 2, kInfoY + kInfoH / 2, fBold, OPT_CENTERX | OPT_CENTERY, view->patText);
    drawBottomBar(view->bar, fBold);

    // ---- notes ----
    if (view->pattern)
    {
        for (int t = 0; t < 8; t++)
        {
            const LaneInfo& lane = view->lanes[t];
            int count = noteCount(view->pattern, t);
            if (count == 0)
                continue;
            int y1 = kLanesTop + t * laneH + 4;
            int y2 = kLanesTop + (t + 1) * laneH - 6;
            uint32_t base = lane.slice ? color::kSlice : (lane.melodic ? color::kMelodic : color::kDrum);
            if (lane.muted)
                base = kMutedNote;

            // melodic lanes show pitch as height inside the lane
            int lo = 127, hi = 0;
            if (lane.melodic)
            {
                for (int i = 0; i < count; i++)
                {
                    Note n = getNote(view->pattern, t, i);
                    if (n.pitch < lo)
                        lo = n.pitch;
                    if (n.pitch > hi)
                        hi = n.pitch;
                }
                if (hi - lo < 6)
                    hi = lo + 6;
            }

            int drawn = 0;
            int laneEnd = lane.steps * kTicksPerStep;
            for (int i = 0; i < count && drawn < kMaxNotesPerLane; i++)
            {
                if (!draw::room(3 + kReserve))
                    break;
                Note n = getNote(view->pattern, t, i);
                int s = n.start, e = n.start + n.length;
                if (s >= laneEnd)
                    continue;
                if (e > laneEnd)
                    e = laneEnd;
                if (e <= firstTick || s >= lastTick)
                    continue;
                int x1 = tickToX(s < firstTick ? firstTick : s) + 1;
                int x2 = tickToX(e > lastTick ? lastTick : e) - 2;
                if (x2 < x1 + 3)
                    x2 = x1 + 3;
                if (x2 > kGridX2)
                    x2 = kGridX2;

                // three brightness steps for velocity keep colour changes cheap
                int v = n.velocity ? n.velocity : 100;
                int bright = v >= 110 ? 100 : (v >= 70 ? 80 : 55);
                if (n.chance() < 100)
                    bright = bright * 70 / 100; // notes with a chance look fainter
                draw::color(scaleColor(base, bright, 100));

                if (lane.melodic)
                {
                    int y = y2 - 3 - ((n.pitch - lo) * (y2 - y1 - 3)) / (hi - lo);
                    draw::rect(x1, y, x2, y + 3);
                }
                else
                {
                    draw::rect(x1, y1, x2, y2);
                }
                drawn++;
            }
        }
    }

    // ---- cursor ----
    if (view->nameCursor)
    {
        int y1 = kLanesTop + view->cursorTrack * laneH;
        draw::color(color::kSelect);
        draw::frame(1, y1 - 1, kHeadW + 1, y1 + laneH - 1, 2);
    }
    else if (view->cursorTick >= firstTick && view->cursorTick < lastTick)
    {
        int x1 = tickToX(view->cursorTick);
        int x2 = tickToX(view->cursorTick + kTicksPerStep) - 1;
        if (x2 < x1 + 6)
            x2 = x1 + 6;
        int y1 = kLanesTop + view->cursorTrack * laneH;
        draw::color(color::kSelect);
        draw::frame(x1, y1, x2, y1 + laneH - 2, 2);
    }

    // ---- activity lights ----
    draw::color(color::kPlayhead);
    for (int t = 0; t < 8; t++)
    {
        if (!view->lanes[t].sounding || view->lanes[t].muted)
            continue;
        int y1 = kLanesTop + t * laneH;
        draw::rect(kHeadW - 10, y1 + 4, kHeadW - 4, y1 + 10);
    }

    // ---- playheads: one per track, each loops at its own length ----
    draw::color(color::kPlayhead);
    for (int t = 0; t < 8; t++)
    {
        int ph = view->lanes[t].playhead;
        if (ph < firstTick || ph >= lastTick)
            continue;
        int x = tickToX(ph);
        int y1 = kLanesTop + t * laneH;
        draw::rect(x, y1, x + 1, y1 + laneH - 2);
    }

    if (view->shift)
        drawShiftBorder();

    draw::end();
}

} // namespace gibgob
