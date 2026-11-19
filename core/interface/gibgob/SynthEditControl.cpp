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

uint32_t panelColor(const SynthView& v, int panel)
{
    const ViewLayout& l = v.layout;
    if (l.style == kStyleMain || l.style == kStylePatch)
        return panel == 0 ? y2k::kCyan : y2k::kPink;
    const int g = l.panel[panel].group;
    return y2k::kAccent[(g < 0 ? panel : g) & 3];
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

void drawGraph(const SynthView& v, int pi, const Fonts& f)
{
    const PanelPos& p = v.layout.panel[pi];
    if (p.gw <= 0)
        return;
    const SynthGraph& g = v.graph[pi];
    const uint32_t col = panelColor(v, pi);
    const int x1 = p.gx, y1 = p.gy, x2 = p.gx + p.gw - 1, y2 = p.gy + p.gh - 1;
    draw::color(y2k::kGraphBg);
    draw::rect(x1, y1, x2, y2);
    draw::color(y2k::kPanelEdge);
    draw::frame(x1, y1, x2, y2, 1);

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
                static const uint32_t kHandle[4] = { y2k::kNote, y2k::kInstr, y2k::kFx1, y2k::kFx2 };
                static const char* const kName[4] = { "A", "D", "S", "R" };
                for (int k = 0; k < 4; k++)
                {
                    const int hx = clampi(hp[2 * k], ix + 9, ix + iw - 10);
                    const int hy = clampi(hp[2 * k + 1], iy + 9, iy + ih - 10);
                    const int s = (g.handle == k) ? 11 : 8;
                    draw::slant(hx - s, hy - s, hx + s, hy + s, s, kHandle[k], y2k::kGraphBg);
                    draw::color(y2k::kDark);
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
            draw::color(y2k::kGrid);
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
            draw::color(y2k::kPink);
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
    draw::cutBox(p.x, p.y, x2, y2, 12, y2k::kPanel, y2k::kBg);
    const int16_t edge[14] = { (int16_t)(p.x + 12), (int16_t)p.y, (int16_t)x2, (int16_t)p.y, (int16_t)x2,
                               (int16_t)(y2 - 12), (int16_t)(x2 - 12), (int16_t)y2, (int16_t)p.x, (int16_t)y2,
                               (int16_t)p.x, (int16_t)(p.y + 12), (int16_t)(p.x + 12), (int16_t)p.y };
    draw::color(y2k::kPanelEdge);
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
        draw::color(y2k::kDim);
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
        draw::color(pass == 0 ? col : y2k::kOff);
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
    const uint32_t under = (v.layout.style == kStyleColumns || v.layout.style == kStylePatch) ? y2k::kPanel : y2k::kBg;
    char label[20], value[16];

    switch (c.kind)
    {
        case kCellChip:
        {
            static const uint32_t kChip[4] = { y2k::kNote, y2k::kInstr, y2k::kFx1, y2k::kFx2 };
            static const char* const kName[4] = { "A", "D", "S", "R" };
            const int k = clampi(c.role - kRoleAttack, 0, 3);
            draw::slant(c.x, c.y, x2, y2, kSlant, sel ? y2k::kRowSel : y2k::kRow, under);
            if (sel)
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, y2k::kCyan, 32);
            draw::color(kChip[k]);
            draw::text(c.x + kSlant + 6, c.y + 11, f.small, OPT_CENTERY, kName[k]);
            draw::color(y2k::kText);
            fit(value, sizeof(value), t.value, c.w / 9);
            draw::text(x2 - 10, c.y + c.h - 13, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
            break;
        }
        case kCellGrid:
        {
            if (sel)
            {
                draw::slant(c.x, c.y, x2, y2, kSlant, y2k::kRowSel, y2k::kBg);
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, y2k::kCyan, 32);
            }
            else
            {
                draw::color(y2k::kRow);
                draw::rect(c.x, c.y, x2, y2);
            }
            draw::color(sel ? y2k::kCyan : y2k::kDim);
            fit(label, sizeof(label), t.label, 10);
            draw::text(c.x + 12, c.y + 15, f.small, OPT_CENTERY, label);
            draw::color(y2k::kText);
            fit(value, sizeof(value), t.value, 7);
            draw::text(x2 - 10, c.y + 15, f.bold, OPT_CENTERY | OPT_RIGHTX, value);
            if (t.fill >= 0)
                ledBar(c.x + 12, c.y + c.h - 16, x2 - 8, t.fill, t.bipolar, y2k::kAccent[(i / 3) & 3]);
            break;
        }
        case kCellAction:
        {
            // lit: the engine in use (lime)
            if (t.lit)
                draw::slant(c.x, c.y, x2, y2, kSlant, y2k::kLime, under);
            else if (sel)
                draw::slant(c.x, c.y, x2, y2, kSlant, y2k::kRowSel, under);
            else
            {
                draw::color(y2k::kRow);
                draw::rect(c.x, c.y, x2, y2);
            }
            if (sel)
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, y2k::kCyan, 32);
            draw::color(t.lit ? y2k::kDark : (sel ? y2k::kCyan : y2k::kText));
            fit(label, sizeof(label), t.label, 18);
            draw::text(c.x + 14, c.y + c.h / 2, f.bold, OPT_CENTERY, label);
            draw::color(t.lit ? y2k::kDark : y2k::kDim);
            fit(value, sizeof(value), t.value, 15);
            draw::text(x2 - 12, c.y + c.h / 2, f.small, OPT_CENTERY | OPT_RIGHTX, value);
            break;
        }
        default:
        {
            if (sel)
            {
                draw::slant(c.x, c.y, x2, y2, kSlant, y2k::kRowSel, under);
                draw::slantOutline(c.x, c.y, x2, y2, kSlant, y2k::kCyan, 32);
            }
            else
            {
                draw::color(y2k::kRow);
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
            draw::color(sel ? y2k::kCyan : y2k::kDim);
            draw::text(c.x + 12, c.y + c.h / 2, f.label, OPT_CENTERY, label);
            draw::color(y2k::kText);
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
    draw::color(v.warn ? y2k::kPink : y2k::kDim);
    fit(line, sizeof(line), v.pathText, (p.w - 24) / 9);
    draw::text(p.x + 14, p.y + p.h - 52, f.small, OPT_CENTERY, line);
    fit(line, sizeof(line), v.hint, (p.w - 24) / 9);
    draw::text(p.x + 14, p.y + p.h - 28, f.small, OPT_CENTERY, line);
}

void drawQuickBoxes(const SynthView& v, const Fonts& f)
{
    static const uint32_t kBtn[4] = { y2k::kNote, y2k::kInstr, y2k::kFx1, y2k::kFx2 };
    for (int k = 0; k < 4; k++)
    {
        const int x1 = kQuickX0 + k * 116, x2 = x1 + kQuickW - 1;
        const int y1 = kQuickY, y2 = kQuickY + kQuickH - 1;
        const bool on = v.quickActive[k];
        if (v.quick[k].label[0] == 0)
        {
            draw::slantOutline(x1, y1, x2, y2, kSlant, y2k::kOff, 16);
            continue;
        }
        if (on)
            draw::slant(x1, y1, x2, y2, kSlant, kBtn[k], y2k::kBg);
        else
            draw::slantOutline(x1, y1, x2, y2, kSlant, kBtn[k], 24);
        char label[10], value[10];
        fit(value, sizeof(value), v.quick[k].value, 7);
        // the name gets what the value leaves (small font: about 6.5 pixels a letter)
        const int labelChars = clampi((86 - (int)strlen(value) * 8) * 2 / 13, 2, 8);
        abbreviate(v.quick[k].label, label, labelChars);
        draw::color(on ? y2k::kDark : kBtn[k]);
        draw::text(x1 + kSlant + 6, (y1 + y2) / 2, f.small, OPT_CENTERY, label);
        draw::color(on ? y2k::kDark : y2k::kText);
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
            draw::slant(x1, y1, x2, y2, 10, y2k::kLime, y2k::kBg); // a tool that is on
        else if (on)
            draw::slantFade(x1, y1, x2, y2, 10, y2k::kCyan, y2k::kPink, y2k::kBg);
        else
            draw::slantOutline(x1, y1, x2, y2, 10, y2k::kPurple, 24);
        if (v.tabText[i] && v.tabText[i][0])
        {
            draw::color(on ? y2k::kDark : 0xCFC4FF);
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
    const Fonts f{ fonts[0].handle, fonts[2].handle, fonts[3].handle, fonts[4].handle };

    draw::begin(kBudget);
    if (drawHelp(v.help, fonts[1].handle, fonts[3].handle))
    {
        draw::color(color::kBg);
        draw::rect(0, 0, kScreenW - 1, kScreenTop - 1);
        draw::end();
        return;
    }
    draw::setLighten(v.shift);

    draw::color(y2k::kBg);
    draw::rect(0, 0, kScreenW - 1, 479);

    // header: a chrome bar with a slanted end
    draw::fadeRect(0, 0, 459, 28, y2k::kChromeHi, y2k::kChromeLo, true);
    draw::slantEnd(440, 0, 459, 28, y2k::kBg);
    draw::color(y2k::kChromeText);
    draw::text(10, 14, f.big, OPT_CENTERY, v.synthName);
    draw::color(y2k::kCyan);
    {
        char info[24];
        fit(info, sizeof(info), v.headerInfo, 20);
        draw::text(470, 14, f.small, OPT_CENTERY, info);
    }
    if (v.pageText[0])
    {
        draw::color(y2k::kPink);
        draw::text(790, 14, f.bold, OPT_CENTERY | OPT_RIGHTX, v.pageText);
    }

    if (!v.editing)
    {
        draw::color(y2k::kText);
        draw::text(kScreenW / 2, 240, f.big, OPT_CENTER, v.message);
        drawTabs(v, f);
        if (v.shift)
            drawShiftBorder();
        draw::end();
        return;
    }

    draw::color(y2k::kText);
    draw::text(12, kQuickY + kQuickH / 2, f.big, OPT_CENTERY, v.title);
    drawQuickBoxes(v, f);
    // the tabs and the Shift frame before the content: if a busy page runs
    // out of room, it loses a detail, never these
    drawTabs(v, f);
    if (v.shift)
        drawShiftBorder();

    for (int p = 0; p < v.layout.panelCount; p++)
        drawPanel(v, p, f);
    if (v.layout.style == kStyleMain)
        drawMainExtras(v, f);
    for (int i = 0; i < v.layout.cellCount; i++)
        drawCell(v, i, f);
    if (v.hint[0] && v.layout.style != kStyleMain)
    {
        draw::color(v.warn ? y2k::kPink : y2k::kDim);
        draw::text(24, 352, f.small, OPT_CENTERY, v.hint);
    }

    if (v.message[0])
    {
        // a short note over the tabs' top edge (copied, pasted ...)
        const int w = (int)strlen(v.message) * 8 + 24;
        draw::slant(400 - w / 2, 396, 400 + w / 2, 422, kSlant, y2k::kLime, y2k::kBg);
        draw::color(y2k::kDark);
        draw::text(400, 409, f.bold, OPT_CENTER, v.message);
    }
    draw::end();
}

} // namespace gibgob
