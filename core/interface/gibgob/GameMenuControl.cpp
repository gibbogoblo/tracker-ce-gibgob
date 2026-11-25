#include "gibgob/GameMenuControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/gibgobMenuArt.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{

namespace
{
// ---- the look: the GibGob artwork (docs/design-ideas/menu/background.jpg) ----
constexpr uint32_t kBand = 0x6EFFFF;    // the item you stand on
constexpr uint32_t kItem = 0xE8455A;    // the items above and below
constexpr uint32_t kItemSel = 0xF2A55A; // the item you stand on
constexpr uint32_t kOutline = 0x3A1670; // the letters' dark purple outline
constexpr uint32_t kBubbleText = 0x2F8FC6;
constexpr uint32_t kDotOff = 0x2E8FB0;

constexpr int kBudget = 1440; // the menu is drawn alone (display.exclusiveControl)

constexpr int kBandY1 = 156, kBandY2 = 238;
constexpr int kItemY[3] = { 112, 197, 278 }; // above, the one you stand on, below

// where the artwork lives in the display chip's memory: after the icons
// (116128), before the pictures screens load (350000)
constexpr uint32_t kTopAddress = 120000;
constexpr uint32_t kTopPaletteAddress = kTopAddress + sizeof(menuart::kTopPixels);
constexpr uint32_t kBotAddress = kTopPaletteAddress + sizeof(menuart::kTopPalette);
constexpr uint32_t kBotPaletteAddress = kBotAddress + sizeof(menuart::kBotPixels);
constexpr uint32_t kArtEnd = kBotPaletteAddress + sizeof(menuart::kBotPalette);
static_assert(kArtEnd <= 350000, "the menu art runs into the pictures' memory");
static_assert(kTopPaletteAddress % 4 == 0 && kBotAddress % 4 == 0 && kBotPaletteAddress % 4 == 0, "aligned");

bool s_artSent = false;
uint32_t s_artScratches = 0;

// a paletted bitmap at (x, y): `w` x `h` on the screen, from the source
// `stride` bytes a row, scaled by `scale` (1 or 2)
void paletted(uint32_t source, uint32_t palette, int stride, int srcH, int x, int y, int w, int h, int scale)
{
    API_PALETTE_SOURCE(palette);
    API_BITMAP_SOURCE(source);
    API_BITMAP_LAYOUT(PALETTED565, (uint16_t)stride, (uint16_t)srcH);
    API_BITMAP_TRANSFORM_A(256 / scale);
    API_BITMAP_TRANSFORM_E(256 / scale);
    API_BITMAP_SIZE(NEAREST, BORDER, BORDER, (uint16_t)w, (uint16_t)h);
    API_BEGIN(BITMAPS);
    API_VERTEX2F((int16_t)x, (int16_t)y);
    API_END();
}

// the artwork, as two halves each (a bitmap is drawn at most 511 wide)
void artwork()
{
    draw::detail::endRectsNow();
    API_COLOR_A(255);
    API_COLOR(0xFFFFFF); // no tint
    API_BITMAP_HANDLE(0); // the fonts use 1 and up (as the Shift frame)
    for (int half = 0; half < 2; half++)
    {
        // the top: half size, drawn 2x
        paletted(kTopAddress + (uint32_t)(half * menuart::kTopW / 2), kTopPaletteAddress, menuart::kTopW,
                 menuart::kTopH, half * 400, 0, 400, menuart::kSplitY, 2);
        // the bottom strip (bubble, monkey): full size
        paletted(kBotAddress + (uint32_t)(half * menuart::kBotW / 2), kBotPaletteAddress, menuart::kBotW,
                 menuart::kBotH, half * 400, menuart::kSplitY, 400, menuart::kBotH, 1);
    }
    API_BITMAP_TRANSFORM_A(256); // back to 1:1 for whatever draws next
    API_BITMAP_TRANSFORM_E(256);
    draw::detail::forgetState(2 * 2 * 9 + 6);
}

// the item's name in capitals: thick (the chip's fonts have one weight, so the
// letters are drawn a few times a pixel apart) inside a dark outline
void item(int y, int font, uint32_t col, const char* name)
{
    char up[32];
    int k = 0;
    for (; name[k] && k < (int)sizeof(up) - 1; k++)
        up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    static const int8_t kAround[8][2] = { { -2, -2 }, { 0, -3 }, { 3, -2 }, { -3, 0 },
                                          { 4, 0 },   { -2, 3 }, { 1, 4 },  { 3, 3 } };
    static const int8_t kBold[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    draw::color(kOutline);
    for (const auto& o : kAround)
        draw::text(400 + o[0], y + o[1], font, OPT_CENTER, up);
    draw::color(col);
    for (const auto& b : kBold)
        draw::text(400 + b[0], y + b[1], font, OPT_CENTER, up);
}

// the monkey's words in the speech bubble of the artwork
// (the chip's font 29, drawn twice a pixel apart so it reads bold)
void words(const char* text)
{
    char lines[3][48];
    const int n = gameMenuWrap(text, 30, lines, 3);
    draw::color(kBubbleText);
    const int lineH = 30;
    const int y0 = 388 - (n - 1) * lineH / 2;
    for (int i = 0; i < n; i++)
    {
        draw::text(244, y0 + i * lineH, 29, OPT_CENTER, lines[i]);
        draw::text(245, y0 + i * lineH, 29, OPT_CENTER, lines[i]);
    }
}
} // namespace

// the artwork into the display chip: once, and again after a screenshot
void ensureMenuArt()
{
    if (s_artSent && s_artScratches == display.ramGScratches)
        return;
    display.writeRamG(menuart::kTopPixels, sizeof(menuart::kTopPixels), kTopAddress);
    display.writeRamG(menuart::kTopPalette, sizeof(menuart::kTopPalette), kTopPaletteAddress);
    display.writeRamG(menuart::kBotPixels, sizeof(menuart::kBotPixels), kBotAddress);
    display.writeRamG(menuart::kBotPalette, sizeof(menuart::kBotPalette), kBotPaletteAddress);
    s_artSent = true;
    s_artScratches = display.ramGScratches;
}

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
    draw::begin(kBudget);

    // ---- the beach, the speech bubble and the monkey: the artwork ----
    artwork();

    // ---- the band you stand on ----
    draw::alpha(200);
    draw::color(kBand);
    draw::rect(0, kBandY1, kScreenW - 1, kBandY2);
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
        words(v.descriptions[here] ? v.descriptions[here] : "");
    }
    draw::end();
}

} // namespace gibgob
