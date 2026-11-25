#include "gibgob/GameMenuControl.h"

#include "FT812.h"
#include "display.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{

namespace
{
// ---- the look (docs/design-ideas/menu/mockup.jpg) ----
constexpr uint32_t kSky = 0x76CBE8;
constexpr uint32_t kCloud = 0xFFFFFF;
constexpr uint32_t kFrond = 0xC6DCB4;    // pale palm leaves
constexpr uint32_t kFrondBand = 0x9EF5A6; // ... where the band lights them
constexpr uint32_t kTrunk = 0xB0686A;
constexpr uint32_t kBand = 0x6EFFFF;     // the item you stand on
constexpr uint32_t kItem = 0xE8455A;     // the items above and below
constexpr uint32_t kItemSel = 0xF2A55A;  // the item you stand on
constexpr uint32_t kShadow = 0x7B3FD6;   // purple drop shadow under the letters
constexpr uint32_t kBubble = 0xFFFFFF;
constexpr uint32_t kBubbleText = 0x3E9DD2;
constexpr uint32_t kInk = 0x14101A;      // the monkey's outlines
constexpr uint32_t kFur = 0xE0245E;
constexpr uint32_t kFace = 0xFAD9E8;
constexpr uint32_t kPhones = 0x9B5CF0;
constexpr uint32_t kPhonesDark = 0x6A3BC2;
constexpr uint32_t kDotOff = 0x2E8FB0;

constexpr int kBudget = 1440; // a full-screen popup over the screen's own controls (hidden)

constexpr int kBandY1 = 156, kBandY2 = 238;
constexpr int kItemY[3] = { 112, 197, 284 }; // above, the one you stand on, below

void circle(int cx, int cy, int r)
{
    draw::roundRect(cx - r, cy - r, cx + r, cy + r, r);
}

int16_t clampX(float x)
{
    return (int16_t)(x < 0.0f ? 0.0f : (x > 799.0f ? 799.0f : x));
}
int16_t clampY(float y)
{
    return (int16_t)(y < 0.0f ? 0.0f : (y > 479.0f ? 479.0f : y));
}

// a palm crown: fronds fanning out from (cx, cy), drooping at the tips; each
// a thick spine, then its leaflets as one zig-zag line hanging below it
void palm(int cx, int cy, int size, int seed)
{
    static const int16_t kAngles[8] = { -165, -135, -105, -78, -52, -24, 2, 28 }; // degrees, from the right
    constexpr int kSpine = 5, kLeaf = 15;
    for (int pass = 0; pass < 2; pass++)
    {
        draw::lineWidth(pass == 0 ? 16 * 7 : 16 * 3);
        for (int f = 0; f < 8; f++)
        {
            const float a = (float)(kAngles[f] + ((seed * 7 + f * 13) % 11) - 5) * 3.14159265f / 180.0f;
            const float len = (float)size * (0.82f + 0.18f * (float)(((seed + f) * 5) % 4) / 3.0f);
            const float ca = cosf(a), sa = sinf(a);
            auto at = [&](float t, float& x, float& y)
            {
                x = (float)cx + ca * len * t;
                y = (float)cy + sa * len * t + t * t * len * 0.38f; // the tips hang down
            };
            if (pass == 0)
            {
                int16_t pts[2 * kSpine];
                for (int k = 0; k < kSpine; k++)
                {
                    float x, y;
                    at((float)k / (kSpine - 1), x, y);
                    pts[2 * k] = clampX(x);
                    pts[2 * k + 1] = clampY(y);
                }
                draw::lineStrip(pts, kSpine);
            }
            else
            {
                // leaflets: from the spine down and outwards, shorter towards the tip
                int16_t pts[2 * kLeaf];
                for (int k = 0; k < kLeaf; k++)
                {
                    const float t = 0.12f + 0.88f * (float)(k / 2 * 2) / (float)(kLeaf - 1);
                    float x, y;
                    at(t, x, y);
                    if (k & 1)
                    {
                        const float leaf = len * 0.26f * (1.0f - 0.6f * t);
                        x += ca * leaf * 0.55f;
                        y += leaf;
                    }
                    pts[2 * k] = clampX(x);
                    pts[2 * k + 1] = clampY(y);
                }
                draw::lineStrip(pts, kLeaf);
            }
        }
    }
    draw::lineWidth(16);
}

void trunk(const int16_t* xy, int n)
{
    draw::color(kTrunk);
    draw::lineWidth(16 * 18);
    draw::lineStrip(xy, n);
}

void cloud(int cx, int base, int w, int h)
{
    const int16_t bumps[9] = { (int16_t)(cx - w / 4), (int16_t)(base - h * 4 / 10), (int16_t)(h * 4 / 10),
                               (int16_t)(cx + w / 10), (int16_t)(base - h * 6 / 10), (int16_t)(h * 6 / 10),
                               (int16_t)(cx + w * 3 / 8), (int16_t)(base - h * 3 / 10), (int16_t)(h * 3 / 10) };
    draw::color(kCloud);
    draw::dots(bumps, 3);
    draw::roundRect(cx - w / 2, base - h * 3 / 10, cx + w / 2, base, h * 3 / 20);
}

// the item's name in capitals with a purple drop shadow
void item(int y, int font, uint32_t col, const char* name)
{
    char up[32];
    int k = 0;
    for (; name[k] && k < (int)sizeof(up) - 1; k++)
        up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    draw::color(kShadow);
    draw::text(400 + 3, y + 4, font, OPT_CENTER, up);
    draw::color(col);
    draw::text(400, y, font, OPT_CENTER, up);
}

// the monkey in the bottom right corner: a red head in profile, a pink face,
// eyes closed, mouth open (talking), purple headphones
void monkey()
{
    // the headphones' band over the head
    int16_t band[2 * 7];
    for (int k = 0; k < 7; k++)
    {
        const float ang = (-118.0f + 15.0f * (float)k) * 3.14159265f / 180.0f; // over the top, to the ear
        band[2 * k] = clampX(712.0f + 140.0f * cosf(ang));
        band[2 * k + 1] = clampY(448.0f + 140.0f * sinf(ang));
    }
    draw::color(kInk);
    draw::lineWidth(16 * 20);
    draw::lineStrip(band, 7);
    draw::color(kPhones);
    draw::lineWidth(16 * 13);
    draw::lineStrip(band, 7);

    // the head
    draw::color(kInk);
    circle(712, 448, 132);
    draw::color(kFur);
    circle(712, 448, 126);

    // the face: brow and muzzle, outlined
    draw::color(kInk);
    circle(632, 392, 50);
    draw::roundRect(512, 384, 660, 476, 44);
    draw::color(kFace);
    circle(632, 392, 44);
    draw::roundRect(518, 390, 654, 470, 38);
    circle(600, 420, 40); // joins the brow and the muzzle

    // eye closed (a soft curve), the brow line, nostrils, the open mouth
    draw::color(kInk);
    draw::lineWidth(16 * 4);
    const int16_t eye[8] = { 608, 384, 620, 393, 634, 393, 646, 384 };
    draw::lineStrip(eye, 4);
    const int16_t brow[6] = { 596, 362, 614, 352, 636, 350 };
    draw::lineStrip(brow, 3);
    const int16_t nose1[4] = { 534, 402, 542, 412 };
    const int16_t nose2[4] = { 552, 398, 558, 408 };
    draw::lineStrip(nose1, 2);
    draw::lineStrip(nose2, 2);
    const int16_t mouth[8] = { 522, 438, 546, 446, 572, 444, 590, 436 };
    draw::lineWidth(16 * 6);
    draw::lineStrip(mouth, 4);
    // fur marks on the head
    draw::lineWidth(16 * 4);
    const int16_t mark1[6] = { 708, 350, 716, 340, 728, 344 };
    const int16_t mark2[4] = { 742, 388, 746, 402 };
    draw::lineStrip(mark1, 3);
    draw::lineStrip(mark2, 2);

    // the ear cup
    draw::color(kInk);
    draw::roundRect(752, 372, 812, 486, 30);
    draw::color(kPhones);
    draw::roundRect(758, 378, 812, 480, 26);
    draw::color(kPhonesDark);
    draw::roundRect(770, 396, 812, 466, 18);
    draw::lineWidth(16);
}

// the speech bubble with its tail towards the monkey's mouth
void bubble(const char* text, int fBold)
{
    draw::color(kBubble);
    draw::roundRect(14, 318, 476, 456, 66);
    // the tail: a narrowing wedge (lines of falling width)
    for (int k = 0; k < 4; k++)
    {
        const int16_t tail[4] = { (int16_t)(440 - k * 6), (int16_t)(404 + k * 3), (int16_t)(518 - k * 2), 432 };
        draw::lineWidth(16 * (10 - k * 2));
        draw::lineStrip(tail, 2);
    }
    draw::lineWidth(16);
    char lines[3][48];
    const int n = gameMenuWrap(text, 30, lines, 3);
    draw::color(kBubbleText);
    const int lineH = 30;
    const int y0 = 387 - (n - 1) * lineH / 2;
    for (int i = 0; i < n; i++)
        draw::text(245, y0 + i * lineH, fBold, OPT_CENTER, lines[i]);
}
} // namespace

int gameMenuItemAt(const GameMenuView& v, int offset)
{
    if (v.count <= 0)
        return -1;
    return ((v.selected + offset) % v.count + v.count) % v.count;
}

int gameMenuWrap(const char* text, int width, char lines[][48], int maxLines)
{
    if (text == nullptr || width <= 0)
        return 0;
    width = width > 47 ? 47 : width;
    int n = 0;
    const char* p = text;
    while (*p && n < maxLines)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        // as many words as fit
        int len = (int)strlen(p);
        int cut = len;
        if (len > width)
        {
            cut = width;
            while (cut > 0 && p[cut] != ' ')
                --cut;
            if (cut == 0)
                cut = width; // one long word: cut it
        }
        memcpy(lines[n], p, (size_t)cut);
        lines[n][cut] = 0;
        // the last line that fits: what does not fit is cut with "..."
        if (n == maxLines - 1 && p[cut] != 0 && cut + 3 <= width)
            strcat(lines[n], "...");
        ++n;
        p += cut;
    }
    return n;
}

GameMenuControl::GameMenuControl(strControlProperties* properties) : cDisplayControl(controlIdGibGobGameMenu, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (GameMenuView*)properties->data;
    }
}

void GameMenuControl::update()
{
    if (view == nullptr)
        return;
    const GameMenuView& v = *view;
    const int fBold = fonts[3].handle;

    draw::begin(kBudget);

    // ---- the beach: sky, clouds, palms (behind the band and the items) ----
    draw::color(kSky);
    draw::rect(0, 0, kScreenW - 1, 479);
    cloud(292, 50, 78, 40);
    cloud(570, 72, 210, 76);

    const int16_t trunkL[6] = { 44, 480, 30, 360, 58, 238 };
    const int16_t trunkR[6] = { 790, 480, 774, 360, 752, 244 };
    trunk(trunkL, 3);
    trunk(trunkR, 3);
    draw::color(kFrond);
    palm(110, 86, 150, 1);
    palm(150, 232, 120, 2);
    palm(690, 80, 160, 3);
    palm(660, 232, 120, 4);
    draw::lineWidth(16);

    // ---- the band you stand on, lighting the leaves it crosses ----
    draw::alpha(215);
    draw::color(kBand);
    draw::rect(0, kBandY1, kScreenW - 1, kBandY2);
    draw::alpha(70);
    draw::color(kFrondBand);
    draw::rect(0, kBandY1, 250, kBandY2);
    draw::rect(560, kBandY1, kScreenW - 1, kBandY2);
    draw::alpha(255);

    // ---- three items: above, the one you stand on, below ----
    if (v.count > 0)
    {
        const int above = gameMenuItemAt(v, -1), here = gameMenuItemAt(v, 0), below = gameMenuItemAt(v, 1);
        if (v.count > 2 && v.items[above])
            item(kItemY[0], 30, kItem, v.items[above]);
        if (v.items[here])
        {
            // the longest names in the slightly smaller big font so they fit
            const int len = (int)strlen(v.items[here]);
            item(kItemY[1], len > 16 ? 30 : 31, kItemSel, v.items[here]);
        }
        if (v.count > 1 && v.items[below])
            item(kItemY[2], 30, kItem, v.items[below]);

        // the modes of the item you stand on (e.g. Master's pages): dots under it
        if (v.modeCount > 1)
        {
            const int gap = 18, x0 = 400 - (v.modeCount - 1) * gap / 2;
            int16_t on[3], off[3 * 8];
            int no = 0;
            for (int m = 0; m < v.modeCount && m < 8; m++)
            {
                int16_t* d = (m == v.modeCurrent) ? on : &off[3 * no++];
                d[0] = (int16_t)(x0 + m * gap);
                d[1] = (int16_t)(kBandY2 - 10);
                d[2] = (int16_t)(m == v.modeCurrent ? 5 : 4);
            }
            draw::color(kDotOff);
            draw::dots(off, no);
            draw::color(kItemSel);
            draw::dots(on, 1);
        }

        // ---- the monkey tells you what it is ----
        monkey();
        bubble(v.descriptions[here] ? v.descriptions[here] : "", fBold);
    }
    draw::end();
}

} // namespace gibgob
