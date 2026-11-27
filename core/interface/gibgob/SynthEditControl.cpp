#include "gibgob/SynthEditControl.h"

#include "FT812.h"
#include "display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{

using namespace synthui;

namespace
{
constexpr int kSlant = 8;
constexpr int kBudget = 1490;

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// the label, cut to fit `chars`
void fit(char* out, int outLen, const char* in, int chars)
{
    if (chars >= outLen)
        chars = outLen - 1;
    snprintf(out, (size_t)(chars + 1), "%s", in ? in : "");
}

// ---- the sharp looks: the synth editor's Y2K, and BONOBONGO's three (SynthView::look) ----
struct Look
{
    uint32_t bg, panel, panelEdge, row, rowSel, graphBg, grid, chromeHi, chromeLo, chromeText, text, dim, dark, off;
    uint32_t cyan, purple, pink, lime; // the cursor, the tab edges, the hot colour, the lit colour
    uint32_t note, instr, fx1, fx2;    // the coloured buttons
    uint32_t accent[4];                // the panels' colours
    uint32_t tabText;
};
const Look kLooks[kLookCount] = {
    // the Y2K synth editor
    { y2k::kBg, y2k::kPanel, y2k::kPanelEdge, y2k::kRow, y2k::kRowSel, y2k::kGraphBg, y2k::kGrid, y2k::kChromeHi,
      y2k::kChromeLo, y2k::kChromeText, y2k::kText, y2k::kDim, y2k::kDark, y2k::kOff, y2k::kCyan, y2k::kPurple,
      y2k::kPink, y2k::kLime, y2k::kNote, y2k::kInstr, y2k::kFx1, y2k::kFx2,
      { y2k::kAccent[0], y2k::kAccent[1], y2k::kAccent[2], y2k::kAccent[3] }, 0xCFC4FF },
    // BLAST: explosive Y2K edge, a burst of yellow, orange and hot red on soot
    { 0x0A0605, 0x1A0C08, 0xFF5A1F, 0x26110A, 0x4C1A08, 0x0E0604, 0x2C130A, 0xFFF6C2, 0xFF7A1A, 0x1A0500, 0xFFFFFF,
      0xFFB98A, 0x160600, 0x3C1B10, 0xFFE14D, 0xFF5A1F, 0xFF2D55, 0xC6FF00, 0x39FF88, 0xFFE14D, 0xB36BFF, 0x3DD6FF,
      { 0xFFE14D, 0xFF7A1A, 0xFF2D55, 0xC6FF00 }, 0xFFD3AE },
    // CLUB: a rainforest nightclub, jade and leaf under UV and magenta lasers
    { 0x040C09, 0x0A1912, 0x1FBF86, 0x0F241A, 0x2B0F40, 0x030907, 0x12302A, 0xE27BFF, 0x2FFFB4, 0x06120D, 0xEFFFF6,
      0x8FD9B8, 0x06110C, 0x163225, 0xFF4FD8, 0x24A870, 0xFF4FD8, 0xB6FF3B, 0x39FF88, 0xFFE14D, 0xB36BFF, 0x3DD6FF,
      { 0x39FFB0, 0xFF4FD8, 0xFFD23F, 0x7B8CFF }, 0xBFFFE0 },
    // RED: red and black, 90s cool
    { 0x0A0A0A, 0x151515, 0xD10A0A, 0x1E1E1E, 0x420707, 0x050505, 0x2A2A2A, 0xFF2A2A, 0x8E0000, 0xFFFFFF, 0xFFFFFF,
      0xBDBDBD, 0x000000, 0x303030, 0xFF2A2A, 0x7A0000, 0xFF2A2A, 0xFF2A2A, 0x39FF88, 0xFFE14D, 0xB36BFF, 0x3DD6FF,
      { 0xFF2A2A, 0xFFFFFF, 0xB0B0B0, 0xFF8080 }, 0xE6E6E6 },
};
const Look* L = &kLooks[0];

// BONOBONGO's looks: what lies behind the panels (shows in the gaps, around the header and the tabs)
void drawLookDecor(const SynthView& v)
{
    if (v.look == kLookBlast)
    {
        // an explosion between the page's title and the quick boxes: rays of yellow, orange and hot red
        static const int16_t kRay[12][2] = { { 0, -60 },  { 40, -44 },  { 62, -6 },  { 50, 34 },  { 18, 58 },  { -22, 56 },
                                             { -54, 30 }, { -64, -8 }, { -44, -46 }, { 28, -58 }, { 58, 18 }, { -10, 62 } };
        const int cx = 282, cy = 48;
        for (int k = 0; k < 12; k++)
        {
            // squashed to the strip between the header and the panels (y 30 .. 68)
            const int16_t xy[4] = { (int16_t)cx, (int16_t)cy, (int16_t)(cx + kRay[k][0] * 3 / 4), (int16_t)(cy + kRay[k][1] / 3) };
            draw::color(k % 3 == 0 ? 0xFFE14D : (k % 3 == 1 ? 0xFF7A1A : 0xFF2D55));
            draw::lineWidth(k % 2 ? 48 : 80);
            draw::lineStrip(xy, 2);
        }
        draw::color(0xFFF6C2);
        draw::roundRect(cx - 9, cy - 8, cx + 9, cy + 8, 8);
        draw::lineWidth(16);
        return;
    }
    if (v.look == kLookClub)
    {
        // spotlights from the ceiling (magenta, laser green, UV), leaves at the floor's corners
        static const int16_t kBeam[3][2] = { { 150, 0xFF }, { 640, 0x01 }, { 400, 0x02 } };
        static const uint32_t kBeamCol[3] = { 0xFF4FD8, 0x2FFFB4, 0x8A5CFF };
        draw::alpha(46);
        for (int k = 0; k < 3; k++)
        {
            const int x = kBeam[k][0];
            const int x1 = clampi(x - 170, 0, kScreenW - 1), x2 = clampi(x + 170, 0, kScreenW - 1);
            const int16_t xy[6] = { (int16_t)x1, 479, (int16_t)x, 30, (int16_t)x2, 479 };
            draw::color(kBeamCol[k]);
            draw::fillUnder(xy, 3, x1, 30, x2, 479);
        }
        draw::alpha(255);
        static const int16_t kLeaf[6][4] = { { 0, 479, 70, 400 },     { 30, 479, 120, 420 },   { 0, 440, 60, 360 },
                                             { 799, 479, 730, 400 }, { 770, 479, 680, 420 }, { 799, 440, 740, 360 } };
        for (int k = 0; k < 6; k++)
        {
            const int16_t xy[4] = { kLeaf[k][0], kLeaf[k][1], kLeaf[k][2], kLeaf[k][3] };
            draw::color(k % 3 == 1 ? 0x1E6B4A : 0x14503A);
            draw::lineWidth(k % 3 == 1 ? 200 : 260);
            draw::lineStrip(xy, 2);
        }
        draw::lineWidth(16);
        return;
    }
    if (v.look == kLookRed)
    {
        // a red rule under the header (the racing stripes go on the header itself: drawLookHeader)
        draw::color(0xD10A0A);
        draw::rect(0, 29, kScreenW - 1, 31);
    }
}

// on the header bar, under its text: the red look's racing stripes (white and black)
void drawLookHeader(const SynthView& v)
{
    if (v.look != kLookRed)
        return;
    for (int k = 0; k < 3; k++)
    {
        const int16_t xy[4] = { (int16_t)(352 + k * 22), 28, (int16_t)(376 + k * 22), 0 };
        draw::color(k == 1 ? 0x000000 : 0xFFFFFF);
        draw::lineWidth(k == 1 ? 56 : 72);
        draw::lineStrip(xy, 2);
    }
    draw::lineWidth(16);
}

uint32_t panelColor(const SynthView& v, int panel)
{
    const ViewLayout& l = v.layout;
    if (v.angelic)
    {
        const int g = l.panel[panel].group;
        return angelic::kAccent[(g < 0 ? panel : g) & 3];
    }
    if (v.jungle)
    {
        static const uint32_t kJ[4] = { jungle::kFirefly, 0x6FD6A0, 0x9FE0B5, 0x5AC8FA };
        const int g = l.panel[panel].group;
        return kJ[(g < 0 ? panel : g) & 3];
    }
    if (l.style == kStyleMain || l.style == kStylePatch)
        return panel == 0 ? L->cyan : L->pink;
    const int g = l.panel[panel].group;
    return L->accent[(g < 0 ? panel : g) & 3];
}

// one sample of a waveform, -1..1
float waveAt(float shape, float phase)
{
    phase -= floorf(phase);
    const float sine = sinf(phase * 6.2831853f);
    const float tri = phase < 0.25f ? phase * 4 : (phase < 0.75f ? 2 - phase * 4 : phase * 4 - 4);
    const float saw = phase < 0.5f ? phase * 2 : phase * 2 - 2;
    const float sq = phase < 0.5f ? 1.0f : -1.0f;
    if (shape >= 3.5f)
    {
        const int kind = (int)(shape + 0.5f);
        if (kind == 4)
            return 1 - phase * 2; // ramp down
        // random: steps (S&H) or a smooth line through the same values
        static const float kRnd[8] = { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f, -0.5f, 0.7f, -0.1f };
        const float pos = phase * 8;
        const int i = (int)pos & 7;
        if (kind == 5)
            return kRnd[i];
        const float t = pos - floorf(pos);
        const float k = t * t * (3 - 2 * t);
        return kRnd[i] + (kRnd[(i + 1) & 7] - kRnd[i]) * k;
    }
    shape = clampf(shape, 0, 3);
    const float shapes[4] = { sine, tri, saw, sq };
    const int i = (int)shape;
    if (i >= 3)
        return sq;
    const float t = shape - i;
    return shapes[i] + (shapes[i + 1] - shapes[i]) * t;
}
} // namespace

// ---------------------------------------------------------------------------
int envelopePoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max, int16_t handles[8])
{
    // lengths: the parts take what they need of the width, sustain holds
    const float a = 0.04f + clampf(g.a, 0, 1) * 0.30f;
    const float d = 0.04f + clampf(g.d, 0, 1) * 0.30f;
    const float hold = 0.22f;
    const float r = 0.04f + clampf(g.r, 0, 1) * 0.34f;
    const float scale = (float)(w - 1) / (a + d + hold + r);
    const float s = clampf(g.s, 0, 1);
    const int bottom = y + h - 1;
    int n = 0;
    auto put = [&](float px, float level)
    {
        if (n >= max)
            return;
        xy[2 * n] = (int16_t)clampi((int)(x + px * scale + 0.5f), x, x + w - 1);
        xy[2 * n + 1] = (int16_t)clampi((int)(bottom - level * (h - 1) + 0.5f), y, bottom);
        n++;
    };
    const int steps = 6;
    for (int i = 0; i <= steps; i++) // attack: a little curved
    {
        const float t = (float)i / steps;
        put(a * t, 1 - (1 - t) * (1 - t));
    }
    for (int i = 1; i <= steps; i++) // decay: falls fast, then settles
    {
        const float t = (float)i / steps;
        put(a + d * t, s + (1 - s) * expf(-4.0f * t) * (1 - t));
    }
    put(a + d + hold, s); // sustain
    for (int i = 1; i <= steps; i++) // release
    {
        const float t = (float)i / steps;
        put(a + d + hold + r * t, s * expf(-4.0f * t) * (1 - t));
    }
    if (handles)
    {
        const float hx[4] = { a, a + d, a + d + hold, a + d + hold + r };
        const float hl[4] = { 1, s, s, 0 };
        for (int k = 0; k < 4; k++)
        {
            handles[2 * k] = (int16_t)clampi((int)(x + hx[k] * scale + 0.5f), x, x + w - 1);
            handles[2 * k + 1] = (int16_t)clampi((int)(bottom - hl[k] * (h - 1) + 0.5f), y, bottom);
        }
    }
    return n;
}

int wavePoints(float shape, float cycles, int x, int y, int w, int h, int16_t* xy, int max)
{
    // a few points are kept for the straight jumps of saw, square and steps
    const int n = max > 24 ? max - 12 : max;
    const float mid = y + (h - 1) * 0.5f;
    const float amp = (h - 1) * 0.45f;
    int k = 0;
    float prev = 0;
    for (int i = 0; i < n && k < max; i++)
    {
        const float t = (float)i / (n - 1);
        const float v = waveAt(shape, t * cycles);
        if (i > 0 && fabsf(prev - v) > 0.9f && k < max - 1)
        {
            xy[2 * k] = (int16_t)(x + t * (w - 1));
            xy[2 * k + 1] = (int16_t)(mid - prev * amp);
            k++;
        }
        xy[2 * k] = (int16_t)(x + t * (w - 1));
        xy[2 * k + 1] = (int16_t)(mid - v * amp);
        k++;
        prev = v;
    }
    return k;
}

int filterPoints(const SynthGraph& g, int x, int y, int w, int h, int16_t* xy, int max)
{
    const float fc = 0.03f + clampf(g.cutoff, 0, 1) * 0.94f; // position on the frequency axis
    const float q = 0.7f + clampf(g.reso, 0, 1) * 6.0f;
    const int bottom = y + h - 1;
    int n = 0;
    for (int i = 0; i < max; i++)
    {
        const float t = (float)i / (max - 1);
        // frequency ratio to the cutoff over about 3 decades
        const float ratio = powf(10.0f, (t - fc) * 3.0f);
        float mag;
        if (g.filterType == 3)
        {
            // comb: teeth getting closer with the cutoff
            const float teeth = 4 + fc * 10;
            mag = 0.55f + 0.45f * cosf(t * teeth * 6.2831853f) * (0.4f + 0.6f * clampf(g.reso, 0, 1));
            mag = mag * 1.4f;
        }
        else
        {
            const float x2 = ratio * ratio;
            const float re = 1 - x2, im = ratio / q;
            const float den = sqrtf(re * re + im * im) + 1e-6f;
            if (g.filterType == 1)
                mag = x2 / den;
            else if (g.filterType == 2)
                mag = (ratio / q) / den;
            else
                mag = 1 / den;
        }
        // to the box: 0 dB at 70 % height, down to -40 dB at the bottom
        float db = 20 * log10f(mag + 1e-5f);
        db = clampf(db, -40, 14);
        const float level = (db + 40) / 54.0f;
        if (n < max)
        {
            xy[2 * n] = (int16_t)(x + t * (w - 1));
            xy[2 * n + 1] = (int16_t)clampi((int)(bottom - level * (h - 1)), y, bottom);
            n++;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
SynthEditControl::SynthEditControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobSynth, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (SynthView*)properties->data;
    }
}

namespace
{
struct Fonts
{
    int label, bold, big, small;
};

// the sample across the box: its shape, each playhead, the grains as
// fireflies (jungle) or bubbles (any other look)
void drawSampleView(const SynthView& v, int x1, int y1, int x2, int y2, const Fonts& f)
{
    const SynthGrainView& gv = v.grains;
    const bool j = v.jungle;
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    // the river
    if (j)
        draw::fadeRect(x1, y1, x2, y2, jungle::kRiver, 0x06201C, true);
    else
    {
        draw::color(L->graphBg);
        draw::rect(x1, y1, x2, y2);
    }
    const int mid = y1 + h / 2;
    if (!gv.hasSample)
    {
        draw::color(j ? jungle::kSoft : L->dim);
        draw::text((x1 + x2) / 2, mid, f.small, OPT_CENTER, "No sample: Middle on Sample to pick one");
        return;
    }
    // the sample's shape: one bar per stretch
    const int bins = SynthGrainView::kBins;
    const float bw = (float)(w - 8) / bins;
    draw::color(j ? jungle::kWave : L->panelEdge);
    for (int b = 0; b < bins; b++)
    {
        const int bh = 1 + gv.peaks[b] * (h / 2 - 6) / 255;
        const int bx = x1 + 4 + (int)(b * bw);
        draw::rect(bx, mid - bh, bx + (int)bw - 2, mid + bh);
    }
    if (gv.recording)
    {
        // recording: the take so far (above), a red light, the time and the input level
        draw::color(0xFF3B30);
        draw::roundRect(x1 + 10, y1 + 8, x1 + 24, y1 + 22, 7);
        char t[32];
        snprintf(t, sizeof(t), "REC %.1f / %.0f s", (double)gv.recSeconds, (double)gv.recMaxSeconds);
        draw::color(0xFFFFFF);
        draw::text(x1 + 32, y1 + 15, f.bold, OPT_CENTERY, t);
        const int lx1 = x1 + 200, lx2 = x1 + 360, ly = y1 + 11;
        draw::color(j ? jungle::kRow : L->off);
        draw::rect(lx1, ly, lx2, ly + 8);
        const float lv = clampf(gv.recLevel, 0, 1);
        if (lv > 0.0f)
        {
            draw::color(lv > 0.9f ? 0xFF3B30 : (j ? jungle::kFirefly : L->lime));
            draw::rect(lx1, ly, lx1 + (int)((lx2 - lx1) * lv), ly + 8);
        }
        draw::color(j ? jungle::kSoft : L->dim);
        draw::text(x2 - 10, y2 - 12, f.small, OPT_CENTERY | OPT_RIGHTX, "Rec: stop and keep");
        return;
    }
    if (j)
    {
        // moonlight on the water
        draw::alpha(40);
        draw::color(jungle::kMoon);
        for (int k = 0; k < 5; k++)
            draw::rect(x1 + 40 + k * 150, mid - 1, x1 + 80 + k * 150 - k * 4, mid);
        draw::alpha(255);
    }
    // playheads
    draw::color(j ? jungle::kMoon : L->lime);
    for (int k = 0; k < gv.heads && k < SynthGrainView::kMaxHeads; k++)
    {
        const int px = x1 + 4 + (int)(clampf(gv.head[k], 0, 1) * (w - 9));
        draw::rect(px - 1, y1 + 6, px, y2 - 6);
        draw::roundRect(px - 4, y1 + 2, px + 4, y1 + 10, 4);
    }
    // the grains: a soft glow, then the light (louder = bigger)
    const int n = gv.grains < SynthGrainView::kMaxGrains ? gv.grains : SynthGrainView::kMaxGrains;
    if (n > 0)
    {
        int16_t glow[3 * SynthGrainView::kMaxGrains], light[3 * SynthGrainView::kMaxGrains];
        for (int i = 0; i < n; i++)
        {
            const int x = x1 + 4 + (int)(clampf(gv.pos[i], 0, 1) * (w - 9));
            const int y = mid + (int)(clampf(gv.pan[i], -1, 1) * (h / 2 - 14));
            const float lv = clampf(gv.level[i], 0, 1);
            glow[3 * i] = light[3 * i] = (int16_t)x;
            glow[3 * i + 1] = light[3 * i + 1] = (int16_t)y;
            glow[3 * i + 2] = (int16_t)(6 + lv * 8);
            light[3 * i + 2] = (int16_t)(2 + lv * 3);
        }
        draw::alpha(60);
        draw::color(j ? jungle::kFirefly : L->cyan);
        draw::dots(glow, n);
        draw::alpha(255);
        draw::color(j ? jungle::kFireflyHi : 0xC8FAFF);
        draw::dots(light, n);
    }
    char info[20];
    snprintf(info, sizeof(info), j ? "%d fireflies" : "%d grains", n);
    draw::color(j ? jungle::kSoft : L->dim);
    draw::text(x2 - 10, y2 - 12, f.small, OPT_CENTERY | OPT_RIGHTX, info);
}

void drawGraph(const SynthView& v, int pi, const Fonts& f)
{
    const PanelPos& p = v.layout.panel[pi];
    if (p.gw <= 0)
        return;
    const SynthGraph& g = v.graph[pi];
    const uint32_t col = panelColor(v, pi);
    const int x1 = p.gx, y1 = p.gy, x2 = p.gx + p.gw - 1, y2 = p.gy + p.gh - 1;
    if (p.graph == kGraphSample)
    {
        drawSampleView(v, x1, y1, x2, y2, f);
        return;
    }
    if (p.graph != kGraphAvatar)
    {
        draw::color(v.angelic ? angelic::kGraphBg : (v.jungle ? 0x07171A : L->graphBg));
        draw::rect(x1, y1, x2, y2);
        draw::color(v.angelic ? angelic::kRing : (v.jungle ? jungle::kRing : L->panelEdge));
        draw::frame(x1, y1, x2, y2, 1);
    }

    // inside the frame
    const int ix = x1 + 4, iy = y1 + 6, iw = p.gw - 8, ih = p.gh - 12;
    int16_t pts[2 * 64];
    int n = 0;
    switch (p.graph)
    {
        case kGraphEnv:
        {
            int16_t hp[8];
            n = envelopePoints(g, ix, iy, iw, ih, pts, 32, hp);
            draw::color(col);
            draw::alpha(56);
            draw::fillUnder(pts, n, ix, iy, ix + iw - 1, iy + ih - 1);
            draw::alpha(255);
            draw::lineWidth(p.gh > 120 ? 40 : 24);
            draw::lineStrip(pts, n);
            if (p.gh > 120)
            {
                // the four handles, in the colours of the buttons that pick them
                const uint32_t kHandle[4] = { L->note, L->instr, L->fx1, L->fx2 };
                static const char* const kName[4] = { "A", "D", "S", "R" };
                for (int k = 0; k < 4; k++)
                {
                    const int hx = clampi(hp[2 * k], ix + 9, ix + iw - 10);
                    const int hy = clampi(hp[2 * k + 1], iy + 9, iy + ih - 10);
                    const int s = (g.handle == k) ? 11 : 8;
                    draw::slant(hx - s, hy - s, hx + s, hy + s, s, kHandle[k], v.angelic ? angelic::kGraphBg : L->graphBg);
                    draw::color(L->dark);
                    draw::text(hx, hy, f.small, OPT_CENTER, kName[k]);
                }
            }
            break;
        }
        case kGraphOsc:
        case kGraphLfo:
        {
            const float cycles = p.graph == kGraphLfo ? (g.cycles > 0 ? g.cycles : 3) : 2;
            n = wavePoints(g.shape, cycles, ix, iy, iw, ih, pts, 44);
            draw::color(col);
            draw::lineWidth(24);
            draw::lineStrip(pts, n);
            break;
        }
        case kGraphFilter:
        {
            // a grid line at each decade
            draw::color(v.angelic ? angelic::kGrid : L->grid);
            for (int k = 1; k < 3; k++)
                draw::rect(ix + iw * k / 3, iy, ix + iw * k / 3, iy + ih - 1);
            n = filterPoints(g, ix, iy, iw, ih, pts, 48);
            draw::color(col);
            draw::alpha(56);
            draw::fillUnder(pts, n, ix, iy, ix + iw - 1, iy + ih - 1);
            draw::alpha(255);
            draw::lineWidth(32);
            draw::lineStrip(pts, n);
            // the cutoff
            const int cx = ix + (int)((0.03f + clampf(g.cutoff, 0, 1) * 0.94f) * (iw - 1));
            draw::color(v.angelic ? angelic::kAccent[0] : L->pink);
            draw::rect(cx, iy, cx, iy + ih - 1);
            break;
        }
        case kGraphDelay:
        {
            // echoes, each quieter by the feedback
            float level = 1.0f;
            const int bars = 6;
            const int bw = iw / (bars * 2);
            for (int k = 0; k < bars; k++)
            {
                const int bx = ix + 6 + k * (iw - 12) / bars;
                const int bh = (int)((ih - 4) * level);
                draw::color(k == 0 ? col : scaleColor(col, 3, 5));
                draw::rect(bx, iy + ih - 1 - bh, bx + bw, iy + ih - 1);
                level *= 0.15f + clampf(g.amount, 0, 1) * 0.8f;
            }
            break;
        }
        case kGraphAvatar:
        {
            // the 8 syllables, one tile per top button: the picked one outlined,
            // the one being sung lit
            const SynthAvatar& av = v.avatar;
            if (av.names == nullptr)
                break;
            const int cols = 4, gap = 10;
            const int tw = (p.gw - (cols - 1) * gap) / cols;
            const int th = (p.gh - gap) / 2;
            if (v.cute)
            {
                // round bubbles in pastels; the sung one bright, the picked one ringed
                for (int k = 0; k < 8; k++)
                {
                    const int x1 = p.gx + (k % cols) * (tw + gap), y1 = p.gy + (k / cols) * (th + gap);
                    const int x2 = x1 + tw - 1, y2 = y1 + th - 1;
                    const bool lit = av.sung == k;
                    const uint32_t bubble = cute::kBubble[k & 3];
                    if (av.picked == k)
                    {
                        draw::color(cute::kText);
                        draw::roundRect(x1 - 3, y1 - 3, x2 + 3, y2 + 3, 34);
                    }
                    draw::color(lit ? cute::kHot : cute::kRow);
                    draw::roundRect(x1, y1, x2, y2, 30);
                    if (!lit)
                    {
                        // a little shine, top left
                        draw::color(bubble);
                        draw::roundRect(x1 + 14, y1 + 12, x1 + 30, y1 + 22, 5);
                    }
                    char num[4];
                    snprintf(num, sizeof(num), "%d", k + 1);
                    draw::color(lit ? cute::kText : bubble);
                    draw::text(x2 - 18, y1 + 18, f.small, OPT_CENTER, num);
                    draw::text((x1 + x2) / 2, (y1 + y2) / 2 + 4, f.big, OPT_CENTER, av.names[k]);
                }
                break;
            }
            for (int k = 0; k < 8; k++)
            {
                const int x1 = p.gx + (k % cols) * (tw + gap), y1 = p.gy + (k / cols) * (th + gap);
                const int x2 = x1 + tw - 1, y2 = y1 + th - 1;
                const bool lit = av.sung == k;
                if (lit)
                    draw::slant(x1, y1, x2, y2, 12, L->lime, L->panel);
                else
                {
                    draw::color(L->row);
                    draw::rect(x1 + 6, y1, x2 - 6, y2);
                }
                if (av.picked == k)
                    draw::slantOutline(x1, y1, x2, y2, 12, L->cyan, 32);
                char num[4];
                snprintf(num, sizeof(num), "%d", k + 1);
                draw::color(lit ? L->dark : L->dim);
                draw::text(x1 + 18, y1 + 16, f.small, OPT_CENTERY, num);
                draw::color(lit ? L->dark : L->text);
                draw::text((x1 + x2) / 2, (y1 + y2) / 2 + 4, f.big, OPT_CENTER, av.names[k]);
            }
            break;
        }
        case kGraphReverb:
        {
            // a tail of reflections, longer with the decay
            static const float kNoise[16] = { 0.9f, -0.7f, 0.5f, -1.0f, 0.8f, -0.4f, 0.6f, -0.9f,
                                              0.3f, -0.6f, 1.0f, -0.5f, 0.7f, -0.8f, 0.4f, -0.3f };
            const float len = 0.15f + clampf(g.amount, 0, 1) * 0.85f;
            const int count = 40;
            const float mid = iy + (ih - 1) * 0.5f;
            for (int i = 0; i < count; i++)
            {
                const float t = (float)i / (count - 1);
                const float env = expf(-t / (len * 0.35f));
                pts[2 * i] = (int16_t)(ix + t * (iw - 1));
                pts[2 * i + 1] = (int16_t)(mid - kNoise[i & 15] * env * (ih - 2) * 0.5f);
            }
            draw::color(col);
            draw::lineWidth(20);
            draw::lineStrip(pts, count);
            break;
        }
        default: break;
    }
    draw::lineWidth(16);
}

void drawPanel(const SynthView& v, int pi, const Fonts& f)
{
    const PanelPos& p = v.layout.panel[pi];
    const int x2 = p.x + p.w - 1, y2 = p.y + p.h - 1;
    draw::cutBox(p.x, p.y, x2, y2, 12, L->panel, L->bg);
    const int16_t edge[14] = { (int16_t)(p.x + 12), (int16_t)p.y, (int16_t)x2, (int16_t)p.y, (int16_t)x2,
                               (int16_t)(y2 - 12), (int16_t)(x2 - 12), (int16_t)y2, (int16_t)p.x, (int16_t)y2,
                               (int16_t)p.x, (int16_t)(p.y + 12), (int16_t)(p.x + 12), (int16_t)p.y };
    draw::color(L->panelEdge);
    draw::lineWidth(16);
    draw::lineStrip(edge, 7);
    const uint32_t col = panelColor(v, pi);
    if (v.panelTitle[pi][0])
    {
        draw::color(col);
        draw::text(p.x + 16, p.y + 15, f.bold, OPT_CENTERY, v.panelTitle[pi]);
    }
    if (v.panelInfo[pi][0] && p.w >= 400)
    {
        draw::color(L->dim);
        draw::text(x2 - 14, p.y + 15, f.small, OPT_CENTERY | OPT_RIGHTX, v.panelInfo[pi]);
    }
    drawGraph(v, pi, f);
}

// a bar of light segments (lit ones first, then the rest: two colour changes)
void ledBar(int x1, int y, int x2, int fill, bool bipolar, uint32_t col)
{
    const int segs = 6;
    const int w = (x2 - x1 + 1) / segs;
    const int lit = (fill * segs + 50) / 100;
    bool on[segs];
    for (int k = 0; k < segs; k++)
    {
        if (bipolar)
        {
            // from the middle: right half for plus, left for minus
            const int pos = (fill - 50) * segs / 50;
            on[k] = pos >= 0 ? (k >= segs / 2 && k < segs / 2 + (pos + 1) / 2)
                             : (k < segs / 2 && k >= segs / 2 + (pos - 1) / 2);
        }
        else
        {
            on[k] = k < lit;
        }
    }
    for (int pass = 0; pass < 2; pass++)
    {
        draw::color(pass == 0 ? col : L->off);
        for (int k = 0; k < segs; k++)
            if (on[k] == (pass == 0))
                draw::rect(x1 + k * w, y, x1 + k * w + w - 4, y + 6);
    }
}

void drawCell(const SynthView& v, int i, const Fonts& f)
{
    const CellPos& c = v.layout.cell[i];
    const SynthCellText& t = v.cells[i];
    const bool sel = i == v.cursor;
    const uint32_t col = panelColor(v, c.panel);
    const int x2 = c.x + c.w - 1, y2 = c.y + c.h - 1;
    const uint32_t under = (v.layout.style == kStyleColumns || v.layout.style == kStylePatch) ? L->panel : L->bg;
    char label[20], value[16];

    switch (c.kind)
    {
        case kCellChip:
        {
            const uint32_t kChip[4] = { L->note, L->instr, L->fx1, L->fx2 };
            static const char* const kName[4] = { "A", "D", "S", "R" };
            const int k = clampi(c.role - kRoleAttack, 0, 3);
            draw::slant(c.x, c.y, x2, y2, kSlant, sel ? L->rowSel : L->row, under);
            if (sel)
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, L->cyan, 32);
            draw::color(kChip[k]);
            draw::text(c.x + kSlant + 6, c.y + 11, f.small, OPT_CENTERY, kName[k]);
            draw::color(L->text);
            fit(value, sizeof(value), t.value, c.w / 9);
            draw::text(x2 - 10, c.y + c.h - 13, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
            break;
        }
        case kCellGrid:
        {
            if (sel)
            {
                draw::slant(c.x, c.y, x2, y2, kSlant, L->rowSel, L->bg);
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, L->cyan, 32);
            }
            else
            {
                draw::color(L->row);
                draw::rect(c.x, c.y, x2, y2);
            }
            draw::color(sel ? L->cyan : L->dim);
            fit(label, sizeof(label), t.label, 10);
            draw::text(c.x + 12, c.y + 15, f.small, OPT_CENTERY, label);
            draw::color(L->text);
            fit(value, sizeof(value), t.value, 7);
            draw::text(x2 - 10, c.y + 15, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
            if (t.fill >= 0)
                ledBar(c.x + 12, c.y + c.h - 16, x2 - 8, t.fill, t.bipolar, L->accent[(i / 3) & 3]);
            break;
        }
        case kCellAction:
        {
            // lit: the engine in use (lime)
            if (t.lit)
                draw::slant(c.x, c.y, x2, y2, kSlant, L->lime, under);
            else if (sel)
                draw::slant(c.x, c.y, x2, y2, kSlant, L->rowSel, under);
            else
            {
                draw::color(L->row);
                draw::rect(c.x, c.y, x2, y2);
            }
            if (sel)
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, L->cyan, 32);
            draw::color(t.lit ? L->dark : (sel ? L->cyan : L->text));
            fit(label, sizeof(label), t.label, 18);
            draw::text(c.x + 14, c.y + c.h / 2, f.bold, OPT_CENTERY, label);
            draw::color(t.lit ? L->dark : L->dim);
            fit(value, sizeof(value), t.value, 15);
            draw::text(x2 - 12, c.y + c.h / 2, f.small, OPT_CENTERY | OPT_RIGHTX, value);
            break;
        }
        default:
        {
            if (sel)
            {
                draw::slant(c.x, c.y, x2, y2, kSlant, L->rowSel, under);
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, L->cyan, 32);
            }
            else
            {
                draw::color(L->row);
                draw::rect(c.x, c.y, x2, y2);
            }
            // name and value, sharing the width
            const int chars = c.w / 8;
            int lc = chars - (int)strlen(t.value) - 1;
            if (lc > 18)
                lc = 18;
            if (lc < 6)
                lc = 6;
            fit(label, sizeof(label), t.label, lc);
            fit(value, sizeof(value), t.value, chars - lc - 1 > 4 ? chars - lc - 1 : 4);
            draw::color(sel ? L->cyan : L->dim);
            draw::text(c.x + 12, c.y + c.h / 2, f.label, OPT_CENTERY, label);
            draw::color(L->text);
            draw::text(x2 - 10, c.y + c.h / 2, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
            (void)col;
            break;
        }
    }
}

void drawMainExtras(const SynthView& v, const Fonts& f)
{
    // two lines of status under the sound settings (voices free, memory)
    const PanelPos& p = v.layout.panel[1];
    char line[40];
    if (v.angelic)
        draw::color(v.warn ? angelic::kAccent[0] : angelic::kSoft);
    else
        draw::color(v.warn ? L->pink : L->dim);
    fit(line, sizeof(line), v.pathText, (p.w - 24) / 9);
    draw::text(p.x + 14, p.y + p.h - 52, f.small, OPT_CENTERY, line);
    fit(line, sizeof(line), v.hint, (p.w - 24) / 9);
    draw::text(p.x + 14, p.y + p.h - 28, f.small, OPT_CENTERY, line);
}

// the singer picture in the bottom right corner (kStyleSinger). Its background
// is meant to match the screen's (L->bg), so it is drawn at its true
// colours even while Shift lightens everything else, on a patch of unlightened
// background.
void drawSinger(const SynthView& v)
{
    const SynthAvatar& av = v.avatar;
    const int k = (av.cat ? SynthAvatar::kDuo : SynthAvatar::kSinger) + (av.singing ? 1 : 0);
    if (av.loaded[k])
    {
        const int w = av.w[k] < kSingerW ? av.w[k] : kSingerW;
        const int h = av.h[k] < kSingerH ? av.h[k] : kSingerH;
        const int x = kSingerRight - w + 1, y = kSingerBottom - h + 1;
        if (v.shift && !v.cute)
        {
            draw::setLighten(false);
            draw::color(L->bg);
            draw::rect(x, y, kSingerRight, kSingerBottom);
        }
        draw::bitmap(av.address[k], w, h, x, y);
        draw::setLighten(v.shift && !v.cute);
        return;
    }
    // no picture on the card: little stand-in faces
    auto face = [&](int cx, int cy, bool catFace)
    {
        const int r = 40;
        if (catFace)
        {
            draw::color(0xFFB347);
            draw::rect(cx - r, cy - r - 14, cx - r + 20, cy - r + 6);
            draw::rect(cx + r - 20, cy - r - 14, cx + r, cy - r + 6);
        }
        else
        {
            draw::color(L->purple);
            draw::rect(cx - r - 5, cy - r - 8, cx + r + 5, cy - r + 16);
        }
        draw::color(catFace ? 0xFFB347 : 0xFFD9EC);
        draw::rect(cx - r, cy - r, cx + r, cy + r);
        draw::color(L->dark);
        draw::rect(cx - 20, cy - 14, cx - 10, cy - 2);
        draw::rect(cx + 10, cy - 14, cx + 20, cy - 2);
        if (av.singing)
        {
            draw::color(0xC0304F);
            draw::rect(cx - 9, cy + 10, cx + 9, cy + 26);
        }
        else
            draw::rect(cx - 9, cy + 16, cx + 9, cy + 18);
    };
    const int cx = kSingerRight - kSingerW / 2, cy = kSingerBottom - 70;
    if (av.cat)
    {
        face(cx - 46, cy, false);
        face(cx + 50, cy - 20, true);
    }
    else
        face(cx, cy, false);
}

// ---- the round look: pink (LILITRON) or night jungle (GORILLAGRAIN) ----
struct RoundPalette
{
    uint32_t panel, ring, title, row, sel, selRing, selText, selLabel, soft, text, dark, lit, note, tabOn, tabOnShift;
    uint32_t bubble[4];
};
const RoundPalette kPink = { cute::kPanel, cute::kRing, cute::kRing, cute::kRow, cute::kHot, cute::kText, cute::kText,
                             cute::kText, cute::kSoft, cute::kText, cute::kDark, cute::kMint, cute::kLemon, cute::kHot,
                             cute::kMint, { cute::kBubble[0], cute::kBubble[1], cute::kBubble[2], cute::kBubble[3] } };
const RoundPalette kJungle = { jungle::kPanel, jungle::kRing, jungle::kTitle, jungle::kRow, jungle::kSelFill,
                               jungle::kFirefly, jungle::kText, jungle::kFireflyHi, jungle::kSoft, jungle::kText,
                               jungle::kDark, jungle::kLeaf, jungle::kFirefly, jungle::kTabOn, jungle::kFirefly,
                               { jungle::kButton[0], jungle::kButton[1], jungle::kButton[2], jungle::kButton[3] } };
const RoundPalette kAngelic = { angelic::kPanel, angelic::kRing, angelic::kTitle, angelic::kRow, angelic::kSel,
                                angelic::kGold, angelic::kDark, angelic::kDark, angelic::kSoft, angelic::kText,
                                angelic::kDark, angelic::kLit, angelic::kGold, angelic::kTabOn, angelic::kLit,
                                { angelic::kAccent[0], angelic::kAccent[1], angelic::kAccent[2], angelic::kAccent[3] } };
inline const RoundPalette& pal(const SynthView& v)
{
    return v.angelic ? kAngelic : (v.jungle ? kJungle : kPink);
}

void cutePill(int x1, int y1, int x2, int y2, uint32_t fill)
{
    draw::color(fill);
    draw::roundRect(x1, y1, x2, y2, (y2 - y1) / 2);
}

void cuteRingPill(int x1, int y1, int x2, int y2, uint32_t ring, uint32_t fill, int t)
{
    cutePill(x1 - t, y1 - t, x2 + t, y2 + t, ring);
    cutePill(x1, y1, x2, y2, fill);
}

void jungleHeader(const SynthView& v, const Fonts& f)
{
    // leaves at the top left, the moon at the top right
    draw::color(jungle::kLeafDark);
    static const int16_t kLeaves[6][3] = { { 26, 13, 26 }, { 60, 11, 22 }, { 18, 44, 18 }, { 700, 8, 16 }, { 672, 6, 12 }, { 785, 72, 14 } };
    for (const auto& l : kLeaves)
        draw::roundRect(l[0] - l[2], l[1] - l[2] / 2, l[0] + l[2], l[1] + l[2] / 2, l[2] / 2);
    draw::alpha(28);
    draw::color(jungle::kMoon);
    draw::roundRect(748 - 26, 14 - 26, 748 + 26, 14 + 26, 26);
    draw::alpha(255);
    draw::roundRect(748 - 11, 14 - 11, 748 + 11, 14 + 11, 11);
    draw::color(jungle::kSkyTop);
    draw::roundRect(754 - 10, 10 - 10, 754 + 10, 10 + 10, 10); // the shadow: a crescent
    draw::color(jungle::kText);
    draw::text(16, 15, f.big, OPT_CENTERY, v.synthName);
    char info[24];
    fit(info, sizeof(info), v.headerInfo, 20);
    draw::color(jungle::kSoft);
    draw::text(474, 14, f.small, OPT_CENTERY, info);
    if (v.pageText[0])
    {
        draw::color(jungle::kFirefly);
        draw::text(718, 14, f.bold, OPT_CENTERY | OPT_RIGHTX, v.pageText);
    }
}

// a gold halo: an open ring (an ellipse in two round rectangles)
void halo(int cx, int cy, int rx, int ry, uint32_t under)
{
    draw::color(angelic::kGold);
    draw::roundRect(cx - rx, cy - ry, cx + rx, cy + ry, ry);
    draw::color(under);
    draw::roundRect(cx - rx + 3, cy - ry + 2, cx + rx - 3, cy + ry - 2, ry - 2);
}

// the angel emblem: a gold halo over a little gold heart between two lilac
// wings (three feathers each, the top one longest)
void angelEmblem(int cx, int cy)
{
    static const int8_t kFeather[3][2] = { { 0, 18 }, { 5, 13 }, { 10, 8 } }; // dy, length
    draw::color(angelic::kWingEdge);
    for (const auto& fe : kFeather)
    {
        const int y = cy - 3 + fe[0];
        draw::roundRect(cx - 5 - fe[1], y - 2, cx - 5, y + 2, 2);
        draw::roundRect(cx + 5, y - 2, cx + 5 + fe[1], y + 2, 2);
    }
    halo(cx, cy - 8, 8, 3, angelic::kPanel);
    draw::color(angelic::kGold);
    draw::roundRect(cx - 4, cy - 1, cx + 4, cy + 7, 4);
}

// four-point sparkles in one colour (x, y, size each)
void sparkles(const int16_t (*s)[3], int n, uint32_t col)
{
    draw::color(col);
    for (int i = 0; i < n; i++)
    {
        draw::rect(s[i][0] - s[i][2], s[i][1], s[i][0] + s[i][2], s[i][1]);
        draw::rect(s[i][0], s[i][1] - s[i][2], s[i][0], s[i][1] + s[i][2]);
    }
}

void angelicHeader(const SynthView& v, const Fonts& f)
{
    // a pearl pill with a gloss, the name, the angel emblem at its end;
    // sparkles on the right
    cuteRingPill(5, 3, 459, 26, angelic::kRing, angelic::kPanel, 1);
    draw::color(0xF3EEFF);
    draw::roundRect(16, 5, 120, 9, 2); // the gloss
    draw::color(angelic::kText);
    draw::text(20, 15, f.big, OPT_CENTERY, v.synthName);
    angelEmblem(430, 15);
    char info[24];
    fit(info, sizeof(info), v.headerInfo, 20);
    draw::color(angelic::kSoft);
    draw::text(474, 14, f.small, OPT_CENTERY, info);
    static const int16_t kGoldSparks[2][3] = { { 708, 9, 5 }, { 756, 8, 6 } };
    static const int16_t kBlueSparks[2][3] = { { 732, 20, 3 }, { 782, 18, 4 } };
    sparkles(kGoldSparks, 2, angelic::kGoldDeep);
    sparkles(kBlueSparks, 2, angelic::kAccent[2]);
    if (v.pageText[0])
    {
        draw::color(angelic::kAccent[1]);
        draw::text(694, 14, f.bold, OPT_CENTERY | OPT_RIGHTX, v.pageText);
    }
}

void cuteHeader(const SynthView& v, const Fonts& f)
{
    // a pink pill with a light top, the synth's name; bubbles floating right
    cutePill(4, 2, 460, 27, cute::kHeader);
    draw::color(cute::kHeaderHi);
    draw::roundRect(14, 5, 70, 10, 2); // a little gloss
    draw::roundRect(76, 5, 86, 10, 2);
    draw::color(cute::kDark);
    draw::text(20, 15, f.big, OPT_CENTERY, v.synthName);
    char info[24];
    fit(info, sizeof(info), v.headerInfo, 20);
    draw::color(cute::kSoft);
    draw::text(474, 14, f.small, OPT_CENTERY, info);
    static const int16_t kBubbles[5][3] = { { 700, 10, 5 }, { 722, 19, 3 }, { 745, 9, 7 }, { 768, 20, 4 }, { 786, 11, 3 } };
    draw::alpha(150);
    for (int i = 0; i < 5; i++)
    {
        draw::color(cute::kBubble[i & 3]);
        draw::roundRect(kBubbles[i][0] - kBubbles[i][2], kBubbles[i][1] - kBubbles[i][2], kBubbles[i][0] + kBubbles[i][2],
                        kBubbles[i][1] + kBubbles[i][2], kBubbles[i][2]);
    }
    draw::alpha(255);
    if (v.pageText[0])
    {
        draw::color(cute::kHot);
        draw::text(690, 14, f.bold, OPT_CENTERY | OPT_RIGHTX, v.pageText);
    }
}

void cutePanel(const SynthView& v, int pi, const Fonts& f)
{
    const PanelPos& p = v.layout.panel[pi];
    const RoundPalette& P = pal(v);
    const int x2 = p.x + p.w - 1, y2 = p.y + p.h - 1;
    draw::color(P.ring);
    draw::roundRect(p.x, p.y, x2, y2, 20);
    draw::color(P.panel);
    draw::roundRect(p.x + 2, p.y + 2, x2 - 2, y2 - 2, 18);
    if (v.panelTitle[pi][0])
    {
        int tx = p.x + 20;
        if (v.angelic)
        {
            // a little halo before the title
            halo(p.x + 24, p.y + 16, 10, 5, P.panel);
            tx = p.x + 42;
        }
        else if (v.jungle)
        {
            // a little leaf before the title
            draw::color(jungle::kLeaf);
            draw::roundRect(p.x + 14, p.y + 12, p.x + 32, p.y + 20, 4);
            tx = p.x + 40;
        }
        draw::color(P.title);
        draw::text(tx, p.y + 16, f.bold, OPT_CENTERY, v.panelTitle[pi]);
    }
    if (v.panelInfo[pi][0] && p.w >= 400)
    {
        draw::color(P.soft);
        draw::text(x2 - 18, p.y + 16, f.small, OPT_CENTERY | OPT_RIGHTX, v.panelInfo[pi]);
    }
    drawGraph(v, pi, f);
}

void cuteCell(const SynthView& v, int i, const Fonts& f)
{
    const CellPos& c = v.layout.cell[i];
    const SynthCellText& t = v.cells[i];
    const RoundPalette& P = pal(v);
    const bool sel = i == v.cursor;
    const int x2 = c.x + c.w - 1, y2 = c.y + c.h - 1;
    char label[20], value[16];
    // the selected one: hot pink with a white ring (jungle: a firefly glow);
    // the engine in use: mint (jungle: a leaf)
    const uint32_t fill = sel ? P.sel : (t.lit ? P.lit : P.row);
    if (sel)
        cuteRingPill(c.x, c.y, x2, y2, P.selRing, fill, 2);
    else
        cutePill(c.x, c.y, x2, y2, fill);
    const bool light = !sel && t.lit;
    const int chars = c.w / 8;
    int lc = chars - (int)strlen(t.value) - 1;
    lc = lc > 18 ? 18 : (lc < 6 ? 6 : lc);
    fit(label, sizeof(label), t.label, c.kind == kCellChip ? 2 : lc);
    fit(value, sizeof(value), t.value, chars - lc - 1 > 4 ? chars - lc - 1 : 4);
    if (c.kind == kCellChip)
    {
        static const char* const kName[4] = { "A", "D", "S", "R" };
        draw::color(light ? P.dark : (sel ? P.selLabel : P.soft));
        draw::text(c.x + 16, c.y + c.h / 2, f.bold, OPT_CENTERY, kName[clampi(c.role - kRoleAttack, 0, 3)]);
    }
    else
    {
        draw::color(light ? P.dark : (sel ? P.selLabel : P.soft));
        draw::text(c.x + 18, c.y + c.h / 2, c.kind == kCellAction ? f.bold : f.label, OPT_CENTERY, label);
    }
    draw::color(light ? P.dark : (sel ? P.selText : P.text));
    draw::text(x2 - 16, c.y + c.h / 2, c.kind == kCellAction ? f.small : f.bold, OPT_CENTERY | OPT_RIGHTX, value);
    if (c.kind == kCellGrid && t.fill >= 0)
    {
        // a little progress pill instead of lights
        const int bx1 = c.x + 14, bx2 = x2 - 14, by = y2 - 12;
        cutePill(bx1, by, bx2, by + 6, P.panel);
        if (t.fill > 2)
            cutePill(bx1, by, bx1 + (bx2 - bx1) * t.fill / 100, by + 6, P.bubble[(i / 3) & 3]);
    }
}

void cuteQuickBoxes(const SynthView& v, const Fonts& f)
{
    for (int k = 0; k < 4; k++)
    {
        const int x1 = kQuickX0 + k * 116, x2 = x1 + kQuickW - 1;
        const int y1 = kQuickY, y2 = kQuickY + kQuickH - 1;
        const RoundPalette& P = pal(v);
        const uint32_t col = P.bubble[k];
        if (v.quick[k].label[0] == 0)
        {
            cutePill(x1, y1, x2, y2, P.panel);
            continue;
        }
        const bool on = v.quickActive[k];
        if (on)
            cutePill(x1, y1, x2, y2, col);
        else
            cuteRingPill(x1 + 2, y1 + 2, x2 - 2, y2 - 2, col, v.angelic ? angelic::kPanel : (v.jungle ? jungle::kPanel : cute::kBg), 2);
        char label[10], value[10];
        fit(value, sizeof(value), v.quick[k].value, 7);
        abbreviate(v.quick[k].label, label, clampi((86 - (int)strlen(value) * 8) * 2 / 13, 2, 8));
        draw::color(on ? P.dark : col);
        draw::text(x1 + 12, (y1 + y2) / 2, f.small, OPT_CENTERY, label);
        draw::color(on ? P.dark : P.text);
        draw::text(x2 - 10, (y1 + y2) / 2, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
    }
}

void cuteTabs(const SynthView& v, const Fonts& f)
{
    const int w = (kScreenW - 8 - 7 * 4) / 8;
    for (int i = 0; i < 8; i++)
    {
        const int x1 = 4 + i * (w + 4), x2 = x1 + w - 1;
        const int y1 = kTabsY + 2, y2 = kTabsY + kTabsH - 3;
        const bool on = v.tabOn[i];
        const RoundPalette& P = pal(v);
        // with Shift: the tools (a singer: the syllables, the one sung lights up)
        if (on && v.jungle && !v.shift)
            cuteRingPill(x1 + 2, y1 + 2, x2 - 2, y2 - 2, jungle::kFirefly, P.tabOn, 2);
        else if (on && v.angelic && !v.shift)
            cuteRingPill(x1 + 2, y1 + 2, x2 - 2, y2 - 2, angelic::kGold, P.tabOn, 2); // a halo
        else if (on)
            cutePill(x1, y1, x2, y2, v.shift ? P.tabOnShift : P.tabOn);
        else
            cuteRingPill(x1 + 2, y1 + 2, x2 - 2, y2 - 2, P.ring, P.panel, 2);
        if (v.tabText[i] && v.tabText[i][0])
        {
            draw::color(on ? (v.shift ? P.dark : (v.jungle ? jungle::kFireflyHi : P.text)) : P.soft);
            draw::text((x1 + x2) / 2, (y1 + y2) / 2, f.bold, OPT_CENTER, v.tabText[i]);
        }
    }
}

void drawQuickBoxes(const SynthView& v, const Fonts& f)
{
    const uint32_t kBtn[4] = { L->note, L->instr, L->fx1, L->fx2 };
    for (int k = 0; k < 4; k++)
    {
        const int x1 = kQuickX0 + k * 116, x2 = x1 + kQuickW - 1;
        const int y1 = kQuickY, y2 = kQuickY + kQuickH - 1;
        const bool on = v.quickActive[k];
        if (v.quick[k].label[0] == 0)
        {
            draw::slantOutline(x1, y1, x2, y2, kSlant, L->off, 16);
            continue;
        }
        if (on)
            draw::slant(x1, y1, x2, y2, kSlant, kBtn[k], L->bg);
        else
            draw::slantOutline(x1, y1, x2, y2, kSlant, kBtn[k], 24);
        char label[10], value[10];
        fit(value, sizeof(value), v.quick[k].value, 7);
        // the name gets what the value leaves (small font: about 6.5 pixels a letter)
        const int labelChars = clampi((86 - (int)strlen(value) * 8) * 2 / 13, 2, 8);
        abbreviate(v.quick[k].label, label, labelChars);
        draw::color(on ? L->dark : kBtn[k]);
        draw::text(x1 + kSlant + 6, (y1 + y2) / 2, f.small, OPT_CENTERY, label);
        draw::color(on ? L->dark : L->text);
        draw::text(x2 - kSlant - 2, (y1 + y2) / 2, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
    }
}

void drawTabs(const SynthView& v, const Fonts& f)
{
    const int w = (kScreenW - 8 - 7 * 4) / 8;
    for (int i = 0; i < 8; i++)
    {
        const int x1 = 4 + i * (w + 4), x2 = x1 + w - 1;
        const int y1 = kTabsY, y2 = kTabsY + kTabsH - 1;
        const bool on = v.tabOn[i];
        if (on && v.shift)
            draw::slant(x1, y1, x2, y2, 10, L->lime, L->bg); // a tool that is on
        else if (on)
            draw::slantFade(x1, y1, x2, y2, 10, L->cyan, L->pink, L->bg);
        else
            draw::slantOutline(x1, y1, x2, y2, 10, L->purple, 24);
        if (v.tabText[i] && v.tabText[i][0])
        {
            draw::color(on ? L->dark : L->tabText);
            draw::text((x1 + x2) / 2, (y1 + y2) / 2, f.bold, OPT_CENTER, v.tabText[i]);
        }
    }
}
} // namespace

void SynthEditControl::update()
{
    if (view == nullptr)
        return;
    const SynthView& v = *view;
    L = (!v.cute && v.look < kLookCount) ? &kLooks[v.look] : &kLooks[0];
    // the angelic look is light: its labels in the regular weight (the thin
    // one fades into the pearl), same width
    const Fonts f{ v.angelic ? fonts[2].handle : fonts[0].handle, fonts[2].handle, fonts[3].handle, fonts[4].handle };

    draw::begin(kBudget);
    if (drawHelp(v.help, fonts[1].handle, fonts[3].handle))
    {
        draw::color(color::kBg);
        draw::rect(0, 0, kScreenW - 1, kScreenTop - 1);
        draw::end();
        return;
    }
    // the pink look keeps its colours under Shift (only the red frame shows)
    draw::setLighten(v.shift && !v.cute);

    if (v.angelic)
        draw::fadeRect(0, 0, kScreenW - 1, 479, angelic::kSkyTop, angelic::kSkyLow, true);
    else if (v.jungle)
        draw::fadeRect(0, 0, kScreenW - 1, 479, jungle::kSkyTop, jungle::kSkyLow, true);
    else
    {
        draw::color(L->bg);
        draw::rect(0, 0, kScreenW - 1, 479);
        if (!v.cute)
            drawLookDecor(v);
    }

    if (v.cute)
    {
        const RoundPalette& P = pal(v);
        if (v.angelic)
            angelicHeader(v, f);
        else if (v.jungle)
            jungleHeader(v, f);
        else
            cuteHeader(v, f);
        draw::color(P.text);
        draw::text(12, kQuickY + kQuickH / 2, f.big, OPT_CENTERY, v.title);
        cuteQuickBoxes(v, f);
        cuteTabs(v, f);
        for (int p = 0; p < v.layout.panelCount; p++)
            cutePanel(v, p, f);
        if (v.layout.style == kStyleMain)
            drawMainExtras(v, f);
        for (int i = 0; i < v.layout.cellCount; i++)
            cuteCell(v, i, f);
        if (v.layout.style == kStyleSinger)
            drawSinger(v);
        if (v.hint[0] && v.layout.style != kStyleMain)
        {
            draw::color(v.warn ? (v.angelic ? angelic::kAccent[0] : (v.jungle ? jungle::kFirefly : cute::kHot)) : P.soft);
            draw::text(28, 352, f.small, OPT_CENTERY, v.hint);
        }
        if (v.message[0])
        {
            const int w = (int)strlen(v.message) * 8 + 32;
            cutePill(400 - w / 2, 394, 400 + w / 2, 422, P.note);
            draw::color(P.dark);
            draw::text(400, 408, f.bold, OPT_CENTER, v.message);
        }
        if (v.shift)
            drawShiftBorder(kScreenTop, !v.angelic); // last; a light screen: the frame only (the wash would bleach it)
        draw::end();
        return;
    }

    // header: a chrome bar with a slanted end
    draw::fadeRect(0, 0, 459, 28, L->chromeHi, L->chromeLo, true);
    draw::slantEnd(440, 0, 459, 28, L->bg);
    if (!v.cute)
        drawLookHeader(v);
    draw::color(L->chromeText);
    draw::text(10, 14, f.big, OPT_CENTERY, v.synthName);
    draw::color(L->cyan);
    {
        char info[24];
        fit(info, sizeof(info), v.headerInfo, 20);
        draw::text(470, 14, f.small, OPT_CENTERY, info);
    }
    if (v.pageText[0])
    {
        draw::color(L->pink);
        draw::text(790, 14, f.bold, OPT_CENTERY | OPT_RIGHTX, v.pageText);
    }

    if (!v.editing)
    {
        draw::color(L->text);
        draw::text(kScreenW / 2, 240, f.big, OPT_CENTER, v.message);
        drawTabs(v, f);
        if (v.shift)
            drawShiftBorder();
        draw::end();
        return;
    }

    draw::color(L->text);
    draw::text(12, kQuickY + kQuickH / 2, f.big, OPT_CENTERY, v.title);
    drawQuickBoxes(v, f);
    // the tabs before the content: if a busy page runs out of room, it loses
    // a detail, never these (the Shift look is drawn last and always)
    drawTabs(v, f);

    for (int p = 0; p < v.layout.panelCount; p++)
        drawPanel(v, p, f);
    if (v.layout.style == kStyleMain)
        drawMainExtras(v, f);
    for (int i = 0; i < v.layout.cellCount; i++)
        drawCell(v, i, f);
    if (v.layout.style == kStyleSinger)
        drawSinger(v);
    if (v.hint[0] && v.layout.style != kStyleMain)
    {
        draw::color(v.warn ? L->pink : L->dim);
        draw::text(24, 352, f.small, OPT_CENTERY, v.hint);
    }

    if (v.message[0])
    {
        // a short note over the tabs' top edge (copied, pasted ...)
        const int w = (int)strlen(v.message) * 8 + 24;
        draw::slant(400 - w / 2, 396, 400 + w / 2, 422, kSlant, L->lime, L->bg);
        draw::color(L->dark);
        draw::text(400, 409, f.bold, OPT_CENTER, v.message);
    }
    if (v.shift)
        drawShiftBorder(); // last: the wash lies over everything
    draw::end();
}

} // namespace gibgob
