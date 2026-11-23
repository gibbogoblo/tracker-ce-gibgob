#include "gibgob/VisionControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/MelodicTrack.h"

#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif
#ifndef __NOINIT
#define __NOINIT(bank)
#endif

namespace gibgob
{

// ---------------------------------------------------------------------------
// tile contents
// ---------------------------------------------------------------------------
namespace
{
constexpr int kScreenTicks = kVisionScreenSteps * kTicksPerStep;
constexpr int kQuarter = kTicksPerStep / 4; // ticks per 1/4 step (mark units)
constexpr int kRawMax = 160;

struct RawMark
{
    int x0, x1, y0, y1;
};

// merges marks on the same row that are closer than `gap`; returns the count
int mergeMarks(RawMark* m, int n, int gap)
{
    // sort by row, then start (insertion sort: at most kRawMax marks)
    for (int i = 1; i < n; i++)
    {
        RawMark v = m[i];
        int j = i - 1;
        while (j >= 0 && (m[j].y0 > v.y0 || (m[j].y0 == v.y0 && m[j].x0 > v.x0)))
        {
            m[j + 1] = m[j];
            j--;
        }
        m[j + 1] = v;
    }
    int out = 0;
    for (int i = 0; i < n; i++)
    {
        if (out > 0 && m[out - 1].y0 == m[i].y0 && m[i].x0 <= m[out - 1].x1 + gap)
        {
            if (m[i].x1 > m[out - 1].x1)
                m[out - 1].x1 = m[i].x1;
            if (m[i].y1 > m[out - 1].y1)
                m[out - 1].y1 = m[i].y1;
            continue;
        }
        m[out++] = m[i];
    }
    return out;
}
} // namespace

void buildVisionTile(const Sequencer::strPattern* p, int track, int startTick, VisionTile& out)
{
    out.count = 0;
    if (p == nullptr)
        return;
    const int steps = trackSteps(p, track);
    const int trackTicks = (steps > 0 ? steps : 16) * kTicksPerStep;
    const int notes = noteCount(p, track);
    const bool slice = isSliceTrack(p, track);
    const bool melodic = isMelodic(p, track) && !slice;
    const int slices = slice ? sliceCount(p, track) : 0;

    // pitch range of a melodic track (at least an octave, centred)
    int lo = 127, hi = 0;
    for (int i = 0; i < notes; i++)
    {
        const int pitch = getNote(p, track, i).pitch;
        lo = pitch < lo ? pitch : lo;
        hi = pitch > hi ? pitch : hi;
    }
    if (lo > hi)
        lo = hi = 60;
    while (hi - lo < 11)
    {
        if (lo > 0)
            lo--;
        if (hi - lo < 11 && hi < 127)
            hi++;
    }
    const int span = hi - lo + 1;

    __NOINIT(EXTERNAL_RAM) static RawMark raw[kRawMax]; // scratch, kept out of the fast memory
    int n = 0;
    int start = startTick % trackTicks;
    if (start < 0)
        start += trackTicks;
    for (int i = 0; i < notes && n < kRawMax; i++)
    {
        const Note note = getNote(p, track, i);
        // where the note first falls on screen, then every loop of the track after
        int x = (int)note.start - start;
        while (x < 0)
            x += trackTicks;
        for (; x < kScreenTicks && n < kRawMax; x += trackTicks)
        {
            RawMark m;
            m.x0 = x / kQuarter;
            if (melodic)
            {
                int len = note.length > 0 ? note.length : kTicksPerStep / 2;
                m.x1 = (x + len) / kQuarter;
                m.y0 = (hi - note.pitch) * 255 / span;
                m.y1 = m.y0 + 255 / span;
            }
            else if (slice)
            {
                int s = (int)note.pitch - kFirstSliceNote;
                int rows = slices > 0 ? slices : 8;
                s = s < 0 ? 0 : (s >= rows ? rows - 1 : s);
                m.x1 = m.x0 + 2;
                m.y0 = s * 255 / rows;
                m.y1 = m.y0 + (255 / rows > 4 ? 255 / rows : 4);
            }
            else
            {
                m.x1 = m.x0 + 2; // a hit: half a step wide
                m.y0 = 64;
                m.y1 = 191;
            }
            if (m.x1 <= m.x0)
                m.x1 = m.x0 + 1;
            if (m.x1 > kVisionScreenSteps * 4)
                m.x1 = kVisionScreenSteps * 4;
            if (m.y1 > 255)
                m.y1 = 255;
            raw[n++] = m;
        }
    }
    // too many to draw: join neighbours on a row, further and further apart
    int gap = 0;
    n = mergeMarks(raw, n, gap);
    while (n > VisionTile::kMaxMarks && gap < kVisionScreenSteps * 4)
    {
        gap = gap == 0 ? 2 : gap * 2;
        n = mergeMarks(raw, n, gap);
    }
    if (n > VisionTile::kMaxMarks)
        n = VisionTile::kMaxMarks;
    for (int i = 0; i < n; i++)
    {
        out.marks[i].x0 = (uint16_t)raw[i].x0;
        out.marks[i].x1 = (uint16_t)raw[i].x1;
        out.marks[i].y0 = (uint8_t)raw[i].y0;
        out.marks[i].y1 = (uint8_t)raw[i].y1;
    }
    out.count = n;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------
namespace
{
constexpr uint32_t kTileBg = 0x1C1C24;
constexpr uint32_t kBarLine = 0x3A3946;
constexpr uint32_t kMutedGrey = 0x585764;
constexpr uint32_t kPictureBg = 0x2E213B; // the frames' own background
constexpr int kPatBoxW = 84;
constexpr int kReserve = 230; // words kept for what is drawn after the notes
constexpr int kHeaderH = 20;

void cellRect(int cell, int& x1, int& y1, int& x2, int& y2)
{
    x1 = kVisionMargin + (cell % 3) * (kVisionTileW + kVisionGap);
    y1 = kVisionTop + (cell / 3) * (kVisionTileH + kVisionGap);
    x2 = x1 + kVisionTileW - 1;
    y2 = y1 + kVisionTileH - 1;
}
} // namespace

VisionControl::VisionControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobVision, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (VisionView*)properties->data;
    }
}

void VisionControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;

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

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, 479);

    // tile backgrounds and header bands
    draw::color(kTileBg);
    for (int t = 0; t < 8; t++)
    {
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        draw::rect(x1, y1, x2, y2);
    }
    for (int t = 0; t < 8; t++)
    {
        const VisionTile& tile = view->tiles[t];
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        draw::color(tile.muted ? 0x3C3C42 : tile.color);
        draw::rect(x1, y1, x2, y1 + kHeaderH - 1);
    }

    // the fixed parts before the notes, so the notes can never push them out
    drawBottomBar(view->bar, fBold);
    drawInfoStrip(kPatBoxW + 12, view->title, view->mode, view->modeColor, view->params, view->selectedParam, fRegular,
                  fBold);
    draw::color(color::kLine);
    draw::rect(4, kInfoY + 3, kPatBoxW, kInfoY + kInfoH - 4);
    draw::color(color::kText);
    draw::text((4 + kPatBoxW) / 2, kInfoY + kInfoH / 2, fBold, OPT_CENTERX | OPT_CENTERY, view->patText);

    // the picture (or where it goes)
    {
        int x1, y1, x2, y2;
        cellRect(4, x1, y1, x2, y2);
        if (view->pictureLoaded)
        {
            const int w = view->frameW, h = view->frameH;
            draw::color(color::kBg);
            draw::rect(x1, y1, x2, y2);
            draw::bitmap(view->frameAddress[view->frame & 1], w, h, x1 + (kVisionTileW - w) / 2,
                         y1 + (kVisionTileH - h) / 2);
        }
        else
        {
            draw::color(kPictureBg);
            draw::rect(x1, y1, x2, y2);
        }
    }

    // bar lines
    for (int t = 0; t < 8; t++)
    {
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        const int ax = x1 + 6, aw = kVisionTileW - 12;
        const int ay = y1 + kHeaderH + 5, ah = kVisionTileH - kHeaderH - 11;
        draw::color(kBarLine);
        for (int b = 1; b < 4; b++)
        {
            int x = ax + b * aw / 4;
            draw::rect(x, ay, x, ay + ah);
        }
    }

    // notes
    for (int t = 0; t < 8; t++)
    {
        const VisionTile& tile = view->tiles[t];
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        const int ax = x1 + 6, aw = kVisionTileW - 12;
        const int ay = y1 + kHeaderH + 5, ah = kVisionTileH - kHeaderH - 11;
        draw::color(tile.muted ? kMutedGrey : tile.color);
        for (int i = 0; i < tile.count; i++)
        {
            if (!draw::room(3 + kReserve))
                break; // leave room for the playheads, names, cursor and Shift border
            const VisionTile::Mark& m = tile.marks[i];
            int mx1 = ax + m.x0 * aw / (kVisionScreenSteps * 4);
            int mx2 = ax + m.x1 * aw / (kVisionScreenSteps * 4) - 1;
            if (mx2 < mx1)
                mx2 = mx1;
            int my1 = ay + m.y0 * ah / 255;
            int my2 = ay + m.y1 * ah / 255 - 1;
            if (my2 < my1 + 2)
                my2 = my1 + 2;
            draw::rect(mx1, my1, mx2, my2);
        }
    }

    for (int t = 0; t < 8; t++)
    {
        const VisionTile& tile = view->tiles[t];
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        const int ax = x1 + 6, aw = kVisionTileW - 12;
        const int ay = y1 + kHeaderH + 5, ah = kVisionTileH - kHeaderH - 11;
        if (tile.playX >= 0)
        {
            int x = ax + tile.playX * aw / (kVisionScreenSteps * 4);
            draw::color(color::kPlayhead);
            draw::rect(x, ay, x + 1, ay + ah);
        }
        if (tile.flash && !tile.muted)
        {
            draw::color(tile.color);
            draw::frame(x1, y1, x2, y2, 3);
        }
        if (t == view->cursor)
        {
            draw::color(color::kSelect);
            draw::frame(x1 - 3, y1 - 3, x2 + 3, y2 + 3, 3);
        }
    }
    if (view->shift)
        drawShiftBorder();

    // text last: names, lengths, mute / solo tags
    for (int t = 0; t < 8; t++)
    {
        const VisionTile& tile = view->tiles[t];
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        draw::color(tile.muted ? color::kTextDim : color::kTextDark);
        draw::text(x1 + 7, y1 + kHeaderH / 2, fBold, OPT_CENTERY, tile.name);
        draw::text(x2 - 7, y1 + kHeaderH / 2, fRegular, OPT_CENTERY | OPT_RIGHTX, tile.info);
    }
    for (int t = 0; t < 8; t++)
    {
        const VisionTile& tile = view->tiles[t];
        if (!tile.muted && !tile.soloed)
            continue;
        int x1, y1, x2, y2;
        cellRect(visionCellOfTrack(t), x1, y1, x2, y2);
        draw::color(tile.soloed ? color::kBox : color::kTextDim);
        draw::rect(x2 - 52, y2 - 20, x2 - 6, y2 - 5);
        draw::color(color::kTextDark);
        draw::text(x2 - 29, y2 - 12, fRegular, OPT_CENTER, tile.soloed ? "SOLO" : "MUTE");
    }
    if (!view->pictureLoaded && view->pictureHint != nullptr)
    {
        int x1, y1, x2, y2;
        cellRect(4, x1, y1, x2, y2);
        draw::color(color::kText);
        draw::text((x1 + x2) / 2, y1 + 34, fBold, OPT_CENTER, "GIBBON VISION");
        draw::color(color::kTextDim);
        draw::text((x1 + x2) / 2, y1 + 66, fRegular, OPT_CENTER, view->pictureHint);
    }

    draw::end();
}

} // namespace gibgob
