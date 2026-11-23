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
// ---- the look (docs/design-ideas/drum_overview/v5_*.png) ----
constexpr uint32_t kSkyTop = 0xE8432C;
constexpr uint32_t kSkyLow = 0xF27A3C;
constexpr uint32_t kSunOuter = 0xF9A94F;
constexpr uint32_t kSunInner = 0xFBB862;
constexpr uint32_t kRay = 0xFFB35C;
constexpr uint32_t kWood = 0xB9683A;
constexpr uint32_t kWoodDark = 0x7A3E1E;
constexpr uint32_t kSkin = 0xF6E6CB;
constexpr uint32_t kCloud = 0xFCE9E6;
constexpr uint32_t kCloudLabel = 0xA0626A;
constexpr uint32_t kInk = 0x2B2238;
constexpr uint32_t kSand = 0xD9985F;
constexpr uint32_t kCursor = 0xFFD84A;
constexpr uint32_t kFillHit = 0xFFD84A;
constexpr uint32_t kCoconutRing = 0xF0462A;
constexpr uint32_t kShellOuter = 0x5E2E20;
constexpr uint32_t kShellInner = 0x8C4C34;
constexpr uint32_t kFlesh = 0xFFF8EC;
constexpr uint32_t kFleshOff = 0xCDBFAF;
// from the sky down to the sand: fewer tracks spread over these
constexpr uint32_t kBands[8] = { 0xA0569A, 0x7B5DA6, 0x5E63D6, 0x4F72E6, 0x79A3EC, 0xBAB9DF, 0xDCC6AE, 0xEBB98A };

constexpr int kTop = 62;  // the bands
constexpr int kBot = 404; // ... down to the sand
constexpr int kX0 = 168;  // step 1
constexpr int kStepX = 86;
constexpr int kBigBand = 172;
constexpr int kWavePts = 17;
constexpr int kBudget = 1440; // + the Shift look (always drawn) stays under 1500

bool isDark(uint32_t c)
{
    const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return (299 * r + 587 * g + 114 * b) / 1000 < 150;
}

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

// a cloud: flat bottom, fluffy top; each of the 5 has its own bumps
void cloud(int cx, int base, int w, int h, int which, uint32_t fill)
{
    static const int8_t kBump[5][5][2] = {
        // x across (-50..50 % of the width), radius (% of the height); 0 radius = none
        { { -30, 34 }, { -5, 52 }, { 24, 40 }, { 0, 0 }, { 0, 0 } },
        { { -28, 36 }, { 2, 46 }, { 28, 32 }, { 0, 0 }, { 0, 0 } },
        { { -34, 30 }, { -12, 44 }, { 12, 50 }, { 34, 30 }, { 0, 0 } },
        { { -26, 40 }, { 6, 54 }, { 30, 30 }, { 0, 0 }, { 0, 0 } },
        { { -36, 30 }, { -14, 46 }, { 8, 54 }, { 30, 40 }, { 0, 0 } },
    };
    int16_t xyr[3 * 5];
    int n = 0;
    for (int i = 0; i < 5; i++)
    {
        const int r = kBump[which][i][1] * h / 100;
        if (r == 0)
            continue;
        xyr[3 * n] = (int16_t)(cx + kBump[which][i][0] * w / 100);
        xyr[3 * n + 1] = (int16_t)(base - r); // the bottoms touch the base: a flat bottom
        xyr[3 * n + 2] = (int16_t)r;
        n++;
    }
    draw::color(fill);
    draw::dots(xyr, n);
    draw::roundRect(cx - w / 2, base - h * 35 / 100, cx + w / 2, base, h * 17 / 100);
}

void bongos(int x, int y)
{
    // two little bongos on the sun: wood bodies, a dark band, pale heads
    draw::color(kWood);
    draw::roundRect(x, y + 4, x + 30, y + 26, 5);
    draw::roundRect(x + 34, y, x + 58, y + 20, 5);
    draw::color(kWoodDark);
    draw::rect(x + 1, y + 13, x + 29, y + 16);
    draw::rect(x + 35, y + 9, x + 57, y + 12);
    draw::rect(x + 28, y + 12, x + 36, y + 15);
    draw::color(kSkin);
    draw::roundRect(x - 2, y, x + 32, y + 7, 3);
    draw::roundRect(x + 32, y - 4, x + 60, y + 3, 3);
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

    // no title bar on this screen (it draws its own sky), so more room than most
    draw::begin(kBudget);
    if (drawHelp(v.help, fRegular, fBold))
    {
        draw::end();
        return;
    }

    // ---- sky, sun, bongos, the page ----
    draw::fadeRect(0, 0, kScreenW - 1, kTop + 8, kSkyTop, kSkyLow, true);
    draw::color(kRay);
    draw::lineWidth(64);
    for (int i = 0; i < 5; i++)
    {
        const float a = (-10.0f + 15.0f * (float)i) * 3.14159265f / 180.0f;
        const float r1 = 64.0f, r2 = (i & 1) ? 98.0f : 84.0f;
        const int16_t xy[4] = { (int16_t)(66 + r1 * cosf(a)), (int16_t)(26 + r1 * sinf(a)), (int16_t)(66 + r2 * cosf(a)),
                                (int16_t)(26 + r2 * sinf(a)) };
        draw::lineStrip(xy, 2);
    }
    draw::lineWidth(16);
    {
        const int16_t outer[3] = { 66, 26, 56 }, inner[3] = { 66, 26, 46 };
        draw::color(kSunOuter);
        draw::dots(outer, 1);
        draw::color(kSunInner);
        draw::dots(inner, 1);
    }
    bongos(18, 20);

    if (v.count <= 0 || v.message[0])
    {
        draw::color(0x2E2240);
        draw::rect(0, kTop, kScreenW - 1, 479);
        draw::color(color::kText);
        draw::text(kScreenW / 2, 240, fBold, OPT_CENTER, v.message);
        if (v.shift)
            drawShiftBorder(0);
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
    const int lo = n >= 7 ? 0 : 1;
    uint32_t col[DrumOverviewView::kMaxTracks];
    for (int i = 0; i < n; i++)
        col[i] = n > 1 ? kBands[lo + (i * (7 - lo) + (n - 1) / 2) / (n - 1)] : kBands[3];
    float amp = n > 1 ? (float)small * 0.14f : 7.0f;
    amp = amp > 7.0f ? 7.0f : amp;

    int16_t xy[2 * kWavePts];
    for (int i = 0; i < n; i++)
    {
        wave(edge[i] - (i == 0 ? 4 : 0), i == 0 ? 3.0f : amp, i * 90, xy);
        draw::color(v.tracks[i].muted ? 0x4A4452 : col[i]);
        draw::fillUnder(xy, kWavePts, 0, edge[i] - 12, kScreenW - 1, kBot + 10);
    }
    // the sand under the coconuts
    wave(kBot + 4, 4.0f, 0, xy);
    draw::color(kSand);
    draw::fillUnder(xy, kWavePts, 0, kBot - 8, kScreenW - 1, 479);

    // playhead: a soft light down the step being played
    if (v.play >= 0)
    {
        draw::alpha(40);
        draw::color(0xFFFFFF);
        draw::roundRect(kX0 + v.play * kStepX - 22, kTop + 4, kX0 + v.play * kStepX + 22, kBot - 6, 18);
        draw::alpha(255);
    }

    // ---- names (the selected one big, with its parameter names) ----
    for (int i = 0; i < n; i++)
    {
        const DrumOverviewTrack& t = v.tracks[i];
        const int mid = (edge[i] + edge[i + 1]) / 2;
        const bool sel = i == v.sel;
        const uint32_t ink = t.muted ? 0x9C96A6 : (isDark(col[i]) ? 0xFFFFFF : kInk);
        draw::color(ink);
        if (sel)
        {
            // the chip's own big fonts: 30, or 29 for a long name (it must end before step 1)
            draw::text(16, mid, strlen(t.name) <= 6 ? 30 : 29, OPT_CENTERY, t.name);
            const uint32_t soft = isDark(col[i]) ? 0xD9DCFF : 0x6A5A6E;
            draw::color(soft);
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
            draw::text(150, mid, fSmall, OPT_CENTERY | OPT_RIGHTX, "mute");
    }

    // ---- steps: hits (fill-only gold, left-out hollow), empty steps as small dots ----
    for (int i = 0; i < n; i++)
    {
        const DrumOverviewTrack& t = v.tracks[i];
        const bool sel = i == v.sel;
        const int mid = (edge[i] + edge[i + 1]) / 2;
        const int h = edge[i + 1] - edge[i];
        int R = sel ? 17 : (h * 32 / 100 < 10 ? h * 32 / 100 : 10);
        R = R < 3 ? 3 : R;
        if (sel)
        {
            // the cursor: a yellow ring
            const int16_t ring[3] = { (int16_t)(kX0 + v.cursor * kStepX), (int16_t)mid, (int16_t)(R + 9) };
            const int16_t hole[3] = { (int16_t)(kX0 + v.cursor * kStepX), (int16_t)mid, (int16_t)(R + 5) };
            draw::color(kCursor);
            draw::dots(ring, 1);
            draw::color(t.muted ? 0x4A4452 : col[i]);
            draw::dots(hole, 1);
        }
        int16_t hits[3 * 8], gold[3 * 8], empty[3 * 8], shade[3 * 8];
        int nh = 0, ng = 0, ne = 0;
        for (int s = 0; s < DrumOverviewView::kSteps; s++)
        {
            if (!(t.exists & (1 << s)))
                continue;
            const int16_t x = (int16_t)(kX0 + s * kStepX);
            if (t.hit & (1 << s))
            {
                shade[3 * (nh + ng)] = x;
                shade[3 * (nh + ng) + 1] = (int16_t)(mid + 2);
                shade[3 * (nh + ng) + 2] = (int16_t)R;
                if (t.fillOnly & (1 << s))
                {
                    gold[3 * ng] = x;
                    gold[3 * ng + 1] = (int16_t)mid;
                    gold[3 * ng + 2] = (int16_t)R;
                    ng++;
                }
                else
                {
                    hits[3 * nh] = x;
                    hits[3 * nh + 1] = (int16_t)mid;
                    hits[3 * nh + 2] = (int16_t)R;
                    nh++;
                }
            }
            else
            {
                empty[3 * ne] = x;
                empty[3 * ne + 1] = (int16_t)mid;
                empty[3 * ne + 2] = (int16_t)(sel ? 4 : 3);
                ne++;
            }
        }
        if (sel && nh + ng > 0) // soft shadows: the selected band only (cheap enough with 8 bands)
        {
            draw::alpha(36);
            draw::color(0x000000);
            draw::dots(shade, nh + ng);
            draw::alpha(255);
        }
        draw::color(t.muted ? 0xD7D7E0 : 0xFFFFFF);
        draw::dots(hits, nh);
        draw::color(kFillHit);
        draw::dots(gold, ng);
        if (ne > 0)
        {
            draw::alpha(140);
            draw::color(0xFFFFFF);
            draw::dots(empty, ne);
            draw::alpha(255);
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
            draw::color(t.muted ? 0x4A4452 : col[i]);
            draw::dots(holes, nl);
        }
    }

    // ---- clouds: the cursor step's 4 values, then the track's length ----
    static const int kCx[4] = { 300, 396, 492, 590 };
    static const int kCw[4] = { 82, 74, 86, 78 };
    static const uint32_t kLit[4] = { color::kNoteBtn, color::kInstrBtn, color::kFx1Btn, color::kFx2Btn };
    for (int k = 0; k < 4; k++)
        cloud(kCx[k], 55, kCw[k], 48, k, v.cloudLit == k ? kLit[k] : kCloud);
    cloud(722, 55, 116, 50, 4, kCloud);
    for (int k = 0; k < 4; k++)
    {
        draw::color(v.cloudLit == k ? kInk : kCloudLabel);
        draw::text(kCx[k], 31, fSmall, OPT_CENTER, v.cloudLabel[k]);
        draw::color(kInk);
        draw::text(kCx[k], 45, fBold, OPT_CENTER, v.cloudValue[k]);
    }
    draw::color(kCloudLabel);
    draw::text(722, 31, fSmall, OPT_CENTER, "length");
    draw::color(kInk);
    draw::text(722, 45, fBold, OPT_CENTER, v.lengthText);
    draw::color(0x5A2A16);
    draw::text(96, 50, fBold, OPT_CENTER, v.pageText);
    if (v.fillOn)
    {
        draw::color(kFillHit);
        draw::roundRect(136, 22, 210, 46, 12);
        draw::color(kInk);
        draw::text(173, 34, fBold, OPT_CENTER, "FILL");
    }

    // ---- the buttons: coconuts (Shift: the CHIMP OUT effects) ----
    const DrumOverviewTrack& selT = v.tracks[v.sel];
    if (v.shift)
    {
        drawBottomBar(v.bar, fBold);
    }
    else
    {
        int16_t ring[3 * 8], outer[3 * 8], inner[3 * 8], flesh[3 * 8], off[3 * 8];
        int nr = 0, nf = 0, no = 0;
        for (int s = 0; s < 8; s++)
        {
            const int16_t x = (int16_t)(53 + s * 98 + s / 2);
            outer[3 * s] = inner[3 * s] = x;
            outer[3 * s + 1] = inner[3 * s + 1] = 443;
            outer[3 * s + 2] = 31;
            inner[3 * s + 2] = 25;
            int16_t* f = (selT.exists & (1 << s)) ? &flesh[3 * nf++] : &off[3 * no++];
            f[0] = x;
            f[1] = 443;
            f[2] = 20;
            if (selT.exists & selT.hit & (1 << s))
            {
                ring[3 * nr] = x;
                ring[3 * nr + 1] = 443;
                ring[3 * nr + 2] = 37;
                nr++;
            }
        }
        draw::color(kCoconutRing);
        draw::dots(ring, nr);
        draw::color(kShellOuter);
        draw::dots(outer, 8);
        draw::color(kShellInner);
        draw::dots(inner, 8);
        draw::color(kFlesh);
        draw::dots(flesh, nf);
        draw::color(kFleshOff);
        draw::dots(off, no);
        draw::color(kInk);
        for (int s = 0; s < 8; s++)
        {
            char num[4];
            snprintf(num, sizeof(num), "%d", s + 1);
            draw::text(53 + s * 98 + s / 2, 444, fBold, OPT_CENTER, num);
        }
    }

    // ---- the selected track's locks and fill tags (details: these go first when room runs out) ----
    {
        const int i = v.sel;
        const int mid = (edge[i] + edge[i + 1]) / 2;
        const uint32_t ink = isDark(col[i]) ? 0xFFFFFF : kInk;
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
                    draw::color(only ? kFillHit : kInk);
                    draw::roundRect(tx, mid - 33, tx + w, mid - 19, 7);
                    draw::color(only ? kInk : 0xFFFFFF);
                    draw::text(tx + w / 2, mid - 26, fSmall, OPT_CENTER, only ? "FILL" : "NO FILL");
                }
            }
            draw::color(ink);
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
        drawShiftBorder(0); // last: the wash lies over everything (this screen has no title bar)
    draw::end();
}

} // namespace gibgob
