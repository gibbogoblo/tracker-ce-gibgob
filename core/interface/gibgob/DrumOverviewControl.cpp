#include "gibgob/DrumOverviewControl.h"

#include "FT812.h"
#include "display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{

namespace
{
// ---- the look: the GibGob colours (gibgobUi.h), the drum tracks as waves ----
// the bands: GibGob greys, the selected drum in the drum colour
constexpr uint32_t kBand[2] = { 0x26262E, 0x2E2E38 };
constexpr uint32_t kBandSel = 0x6E3730; // the drum red, deep enough for white on it
constexpr uint32_t kBandMuted = 0x1A1A20;
constexpr uint32_t kHit = 0xFFFFFF;
constexpr uint32_t kHitMuted = 0x8A8A94;
constexpr uint32_t kFillHit = color::kBox;   // plays only in fills
constexpr uint32_t kEmpty = color::kLine;
constexpr uint32_t kCursor = color::kSelect;
constexpr uint32_t kNoFillTag = 0x3C3C46;
constexpr uint32_t kFxMark = color::kFx1Btn; // a step with effects

constexpr int kTop = kInfoY + kInfoH + 6; // the bands (below the info strip)
constexpr int kBot = kBottomBarY - 8;      // ... down to the button bar
constexpr int kX0 = 168;                   // step 1
constexpr int kStepX = 86;
constexpr int kBigBand = 166;
constexpr int kWavePts = 17;
constexpr int kBudget = 1350; // with the title bar: the usual budget (+ the Shift look under 1500)

void wave(int y, float amp, int phase, int16_t* xy)
{
    for (int i = 0; i < kWavePts; i++)
    {
        const int x = i * 50;
        const float a = (float)(x + phase) * 6.2831853f / 230.0f;
        xy[2 * i] = (int16_t)x;
        xy[2 * i + 1] = (int16_t)(y + amp * sinf(a) + amp * 0.3f * sinf((float)(x + phase * 3) / 37.0f));
    }
}
} // namespace

DrumOverviewControl::DrumOverviewControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobTrackGrid, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (DrumOverviewView*)properties->data;
    }
}

void DrumOverviewControl::update()
{
    if (view == nullptr)
        return;
    const DrumOverviewView& v = *view;
    const int fSmall = fonts[4].handle, fRegular = fonts[1].handle, fBold = fonts[3].handle;

    draw::begin(kBudget);
    if (drawHelp(v.help, fRegular, fBold))
    {
        draw::end();
        return;
    }

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, 479);

    // ---- the info strip: the track, the page and length, FILL, the cursor step's 4 values ----
    ParamBox boxes[4];
    static const uint32_t kBoxColor[4] = { color::kNoteBtn, color::kInstrBtn, color::kFx1Btn, color::kFx2Btn };
    for (int k = 0; k < 4; k++)
    {
        snprintf(boxes[k].label, sizeof(boxes[k].label), "%s", v.cloudLabel[k]);
        snprintf(boxes[k].value, sizeof(boxes[k].value), "%s", v.cloudValue[k]);
        boxes[k].color = kBoxColor[k];
    }
    char title[80];
    const bool onName = v.count > 0 && !v.message[0] && v.cursor < 0;
    if (onName)
        snprintf(title, sizeof(title), "%.10s   Length %s   +/-: length   Middle: settings", v.tracks[v.sel].name,
                 v.lengthText);
    else if (v.count > 0 && !v.message[0])
        snprintf(title, sizeof(title), "%.10s  %s  %s", v.tracks[v.sel].name, v.pageText, v.lengthText);
    else
        snprintf(title, sizeof(title), "Drums");
    // on the name: the track's length, no step values
    drawInfoStrip(12, title, v.fillOn ? "FILL" : nullptr, color::kBox, (v.count > 0 && !onName) ? boxes : nullptr,
                  v.cloudLit, fRegular, fBold);

    if (v.count <= 0 || v.message[0])
    {
        draw::color(color::kText);
        draw::text(kScreenW / 2, 240, fBold, OPT_CENTER, v.message);
        if (v.shift)
            drawShiftBorder();
        draw::end();
        return;
    }

    // ---- the bands: the selected one wide, the others share the rest ----
    const int n = v.count;
    const int big = n > 2 ? kBigBand : (n == 2 ? (kBot - kTop) * 6 / 10 : kBot - kTop);
    const int small = n > 1 ? (kBot - kTop - big) / (n - 1) : 0;
    int edge[DrumOverviewView::kMaxTracks + 1];
    edge[0] = kTop;
    for (int i = 0; i < n; i++)
        edge[i + 1] = edge[i] + (i == v.sel ? big : small);
    edge[n] = kBot;
    uint32_t col[DrumOverviewView::kMaxTracks];
    for (int i = 0; i < n; i++)
        col[i] = v.tracks[i].muted ? kBandMuted : (i == v.sel ? kBandSel : kBand[i & 1]);
    float amp = n > 1 ? (float)small * 0.14f : 7.0f;
    amp = amp > 7.0f ? 7.0f : amp;

    int16_t xy[2 * kWavePts];
    for (int i = 0; i < n; i++)
    {
        wave(edge[i] - (i == 0 ? 4 : 0), i == 0 ? 3.0f : amp, i * 90, xy);
        draw::color(col[i]);
        draw::fillUnder(xy, kWavePts, 0, edge[i] - 12 < kTop - 4 ? kTop - 4 : edge[i] - 12, kScreenW - 1, kBot);
    }
    // the selected band's edge in the drum colour
    wave(edge[v.sel] - (v.sel == 0 ? 4 : 0), v.sel == 0 ? 3.0f : amp, v.sel * 90, xy);
    draw::color(color::kDrum);
    draw::lineWidth(40);
    draw::lineStrip(xy, kWavePts);
    draw::lineWidth(16);

    // playhead: the step being played, in the playhead red
    if (v.play >= 0)
    {
        draw::alpha(70);
        draw::color(color::kPlayhead);
        draw::roundRect(kX0 + v.play * kStepX - 22, kTop + 4, kX0 + v.play * kStepX + 22, kBot - 6, 18);
        draw::alpha(255);
    }

    // ---- names (the selected one big, with its parameter names) ----
    for (int i = 0; i < n; i++)
    {
        const DrumOverviewTrack& t = v.tracks[i];
        const int mid = (edge[i] + edge[i + 1]) / 2;
        const bool sel = i == v.sel;
        draw::color(t.muted ? color::kTextDim : color::kText);
        if (sel)
        {
            // the chip's own big fonts: 30, or 29 for a long name (it must end before step 1)
            draw::text(16, mid, strlen(t.name) <= 6 ? 30 : 29, OPT_CENTERY, t.name);
            draw::color(0xE8B4AA); // a light drum red
            for (int k = 0; k < 2; k++)
            {
                draw::text(18, edge[i] + 24 + k * 20, fSmall, OPT_CENTERY, v.labelTop[k]);
                draw::text(18, edge[i + 1] - 44 + k * 20, fSmall, OPT_CENTERY, v.labelBot[k]);
            }
        }
        else
        {
            const int h = edge[i + 1] - edge[i];
            draw::text(18, mid, h >= 30 ? fBold : fSmall, OPT_CENTERY, t.name);
        }
        if (t.muted)
        {
            draw::color(color::kTextDim);
            draw::text(150, mid, fSmall, OPT_CENTERY | OPT_RIGHTX, "mute");
        }
        if (sel && v.cursor < 0)
        {
            // the cursor on the name (Middle: the track's settings): a white bar under it
            draw::color(kCursor);
            draw::rect(16, mid + 20, 140, mid + 23);
        }
    }

    // ---- steps: hits (fill-only orange, left-out hollow), empty steps as small dots ----
    for (int i = 0; i < n; i++)
    {
        const DrumOverviewTrack& t = v.tracks[i];
        const bool sel = i == v.sel;
        const int mid = (edge[i] + edge[i + 1]) / 2;
        const int h = edge[i + 1] - edge[i];
        int R = sel ? 17 : (h * 32 / 100 < 10 ? h * 32 / 100 : 10);
        R = R < 3 ? 3 : R;
        if (sel && v.cursor >= 0)
        {
            // the cursor: a white ring
            const int16_t ring[3] = { (int16_t)(kX0 + v.cursor * kStepX), (int16_t)mid, (int16_t)(R + 8) };
            const int16_t hole[3] = { (int16_t)(kX0 + v.cursor * kStepX), (int16_t)mid, (int16_t)(R + 5) };
            draw::color(kCursor);
            draw::dots(ring, 1);
            draw::color(col[i]);
            draw::dots(hole, 1);
        }
        int16_t hits[3 * 8], gold[3 * 8], empty[3 * 8];
        int nh = 0, ng = 0, ne = 0;
        for (int s = 0; s < DrumOverviewView::kSteps; s++)
        {
            if (!(t.exists & (1 << s)))
                continue;
            const int16_t x = (int16_t)(kX0 + s * kStepX);
            if (t.hit & (1 << s))
            {
                int16_t* d = (t.fillOnly & (1 << s)) ? &gold[3 * ng++] : &hits[3 * nh++];
                d[0] = x;
                d[1] = (int16_t)mid;
                d[2] = (int16_t)R;
            }
            else
            {
                empty[3 * ne] = x;
                empty[3 * ne + 1] = (int16_t)mid;
                empty[3 * ne + 2] = (int16_t)(sel ? 4 : 3);
                ne++;
            }
        }
        draw::color(t.muted ? kHitMuted : kHit);
        draw::dots(hits, nh);
        draw::color(kFillHit);
        draw::dots(gold, ng);
        if (ne > 0)
        {
            draw::color(kEmpty);
            draw::dots(empty, ne);
        }
        // left out of fills: a hollow hit
        int16_t holes[3 * 8];
        int nl = 0;
        for (int s = 0; s < DrumOverviewView::kSteps; s++)
            if ((t.exists & t.hit & t.noFill) & (1 << s))
            {
                holes[3 * nl] = (int16_t)(kX0 + s * kStepX);
                holes[3 * nl + 1] = (int16_t)mid;
                holes[3 * nl + 2] = (int16_t)(R * 55 / 100);
                nl++;
            }
        if (nl > 0)
        {
            draw::color(col[i]);
            draw::dots(holes, nl);
        }
        // the selected track's steps with effects: a small dot under the hit
        if (sel && v.fxMark)
        {
            int16_t fx[3 * 8];
            int nf = 0;
            for (int s = 0; s < DrumOverviewView::kSteps; s++)
                if ((t.exists & t.hit & v.fxMark) & (1 << s))
                {
                    fx[3 * nf] = (int16_t)(kX0 + s * kStepX);
                    fx[3 * nf + 1] = (int16_t)(mid + R + 13);
                    fx[3 * nf + 2] = 4;
                    nf++;
                }
            draw::color(kFxMark);
            draw::dots(fx, nf);
        }
    }

    // ---- the button bar: the 8 steps on screen (Shift: the CHIMP OUT effects) ----
    const DrumOverviewTrack& selT = v.tracks[v.sel];
    if (v.shift)
    {
        drawBottomBar(v.bar, fBold);
    }
    else
    {
        BottomBar steps;
        char num[8][6];
        for (int s = 0; s < 8; s++)
        {
            const bool exists = selT.exists & (1 << s);
            snprintf(num[s], sizeof(num[s]), "%d", v.firstStep + s + 1);
            steps.text[s] = exists ? num[s] : "";
            steps.fill[s] = (selT.exists & selT.hit & (1 << s)) ? ((selT.fillOnly & (1 << s)) ? color::kBox : color::kDrum) : 0;
            steps.highlighted[s] = v.cursor == s;
        }
        drawBottomBar(steps, fBold);
    }

    // ---- Shift + FX1 / FX2 held: the effect list under its box ----
    if (v.fxList >= 2 && v.fxList <= 3 && v.fxCount > 0)
    {
        constexpr int kRows = 7, kRowH = 24, boxW = 112;
        const int x1 = kScreenW - 4 * (boxW + 4) - 2 + v.fxList * (boxW + 4);
        const int y0 = kInfoY + kInfoH + 2;
        const uint32_t accent = v.fxList == 2 ? color::kFx1Btn : color::kFx2Btn;
        int first = v.fxSel - kRows / 2;
        first = first < 0 ? 0 : (first > v.fxCount - kRows ? (v.fxCount - kRows < 0 ? 0 : v.fxCount - kRows) : first);
        const int rows = v.fxCount < kRows ? v.fxCount : kRows;
        draw::color(accent);
        draw::rect(x1 - 2, y0 - 2, x1 + boxW + 2, y0 + rows * kRowH + 1);
        draw::color(color::kPanel);
        draw::rect(x1, y0, x1 + boxW, y0 + rows * kRowH - 1);
        for (int r = 0; r < rows; r++)
        {
            const int k = first + r;
            const int y = y0 + r * kRowH;
            if (k == v.fxSel)
            {
                draw::color(accent);
                draw::rect(x1, y, x1 + boxW, y + kRowH - 1);
            }
            draw::color(k == v.fxSel ? color::kTextDark : color::kText);
            draw::text(x1 + 8, y + kRowH / 2, k == v.fxSel ? fBold : fRegular, OPT_CENTERY, v.fxNames[k] ? v.fxNames[k] : "");
        }
        // more above / below
        draw::color(color::kTextDim);
        if (first > 0)
            draw::text(x1 + boxW - 8, y0 + kRowH / 2, fSmall, OPT_CENTERY | OPT_RIGHTX, "^");
        if (first + rows < v.fxCount)
            draw::text(x1 + boxW - 8, y0 + (rows - 1) * kRowH + kRowH / 2, fSmall, OPT_CENTERY | OPT_RIGHTX, "v");
    }

    // ---- the selected track's locks and fill tags (details: these go first when room runs out;
    // not while the effect list is open: it needs the room) ----
    if (v.fxList < 0)
    {
        const int i = v.sel;
        const int mid = (edge[i] + edge[i + 1]) / 2;
        for (int s = 0; s < DrumOverviewView::kSteps; s++)
        {
            const int x = kX0 + s * kStepX;
            if ((selT.exists & selT.hit & (selT.fillOnly | selT.noFill)) & (1 << s))
            {
                const bool only = selT.fillOnly & (1 << s);
                if (draw::room(20 + 40))
                {
                    const int w = only ? 36 : 54;
                    const int tx = x + 10 + w > kScreenW - 4 ? kScreenW - 4 - w : x + 10; // the last steps: kept on screen
                    draw::color(only ? kFillHit : kNoFillTag);
                    draw::rect(tx, mid - 33, tx + w, mid - 19);
                    draw::color(only ? color::kTextDark : color::kText);
                    draw::text(tx + w / 2, mid - 26, fSmall, OPT_CENTER, only ? "FILL" : "NO FILL");
                }
            }
            draw::color(color::kText);
            for (int k = 0; k < 2; k++)
            {
                if (v.lockTop[s][k][0] && draw::room(12 + 40))
                    draw::text(x, edge[i] + 24 + k * 20, fBold, OPT_CENTER, v.lockTop[s][k]);
                if (v.lockBot[s][k][0] && draw::room(12 + 40))
                    draw::text(x, edge[i + 1] - 44 + k * 20, fBold, OPT_CENTER, v.lockBot[s][k]);
            }
        }
    }

    if (v.shift)
        drawShiftBorder(); // last: the wash lies over everything
    draw::end();
}

} // namespace gibgob
