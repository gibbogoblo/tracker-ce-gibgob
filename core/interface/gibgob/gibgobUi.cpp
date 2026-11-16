#include "gibgob/gibgobUi.h"

#include "FT812.h"
#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/modulesBase.h"

#include <string.h>

namespace gibgob
{

bool g_shiftLatched = false;
bool g_recArmed = false;

bool shiftActive()
{
    return g_shiftLatched ||
           mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
}

void bindModuleSwitchButtons(InputProcessor* ip, void (*switchFunct)(uint8_t))
{
    static const uint8_t kButtons[] = {
        interfaceButtonPerformance, interfaceButtonParams,     interfaceButtonFile,
        interfaceButtonConfig,      interfaceButtonMaster,     interfaceButtonSamplePlay,
        interfaceButtonSampleEdit,  interfaceButtonSampleRec,  interfaceButtonSampleLoad,
        interfaceButtonSong,
    };
    for (uint8_t b : kButtons)
        ip->setButtonObj(b, buttonPress, switchFunct);
}

uint32_t scaleColor(uint32_t c, int num, int den)
{
    uint32_t r = ((c >> 16) & 0xFF) * (uint32_t)num / (uint32_t)den;
    uint32_t g = ((c >> 8) & 0xFF) * (uint32_t)num / (uint32_t)den;
    uint32_t b = (c & 0xFF) * (uint32_t)num / (uint32_t)den;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
namespace draw
{
namespace
{
int s_budget = 0;
int s_used = 0;
bool s_inRects = false;
bool s_haveColor = false;
uint32_t s_color = 0;
int s_alpha = 255;
} // namespace

void begin(int budgetWords)
{
    s_budget = budgetWords;
    s_used = 2;
    s_inRects = false;
    s_haveColor = false;
    s_alpha = 255;
    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
}

int used()
{
    return s_used;
}

bool room(int words)
{
    return s_used + words <= s_budget;
}

void color(uint32_t c)
{
    c &= 0xFFFFFF;
    if (s_haveColor && c == s_color)
        return;
    if (!room(1))
        return;
    API_COLOR(c);
    s_color = c;
    s_haveColor = true;
    s_used += 1;
}

void alpha(int a)
{
    if (a == s_alpha || !room(1))
        return;
    API_COLOR_A((uint8_t)a);
    s_alpha = a;
    s_used += 1;
}

void rect(int x1, int y1, int x2, int y2)
{
    if (!room(3))
        return;
    if (x2 < x1)
    {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (!s_inRects)
    {
        API_BEGIN(RECTS);
        s_inRects = true;
        s_used += 1;
    }
    API_VERTEX2F((int16_t)x1, (int16_t)y1);
    API_VERTEX2F((int16_t)x2, (int16_t)y2);
    s_used += 2;
}

void frame(int x1, int y1, int x2, int y2, int t)
{
    rect(x1, y1, x2, y1 + t - 1);
    rect(x1, y2 - t + 1, x2, y2);
    rect(x1, y1, x1 + t - 1, y2);
    rect(x2 - t + 1, y1, x2, y2);
}

void text(int x, int y, int fontHandle, uint16_t options, const char* str)
{
    if (str == nullptr || str[0] == 0)
        return;
    int len = (int)strlen(str);

    // glyphs right of x = 512 cost two commands each; estimate generously
    int startX = x;
    if (options & OPT_CENTERX)
        startX -= len * 4;
    if (options & OPT_RIGHTX)
        startX -= len * 8;
    int cost = 5;
    for (int i = 0; i < len; i++)
        cost += (startX + (i + 1) * 9 >= 512) ? 2 : 1;
    if (!room(cost))
        return;

    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
    API_CMD_TEXT((int16_t)x, (int16_t)y, (int16_t)fontHandle, options, str);
    s_used += cost;
}

void end()
{
    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
    if (s_alpha != 255)
    {
        API_COLOR_A(255);
        s_alpha = 255;
        s_used += 1;
    }
}
} // namespace draw

// ---------------------------------------------------------------------------
static void barBox(int i, int& x1, int& x2)
{
    const int w = kScreenW / 8;
    x1 = i * w + 3;
    x2 = (i + 1) * w - 4;
}

void drawBottomBar(const BottomBar& bar, int fontHandle)
{
    draw::color(color::kBg);
    draw::rect(0, kBottomBarY - 3, kScreenW - 1, kBottomBarY + kBottomBarH + 1);

    for (int i = 0; i < 8; i++)
    {
        int x1, x2;
        barBox(i, x1, x2);
        if (bar.highlighted[i])
        {
            draw::color(color::kSelect);
            draw::rect(x1 - 2, kBottomBarY - 2, x2 + 2, kBottomBarY + kBottomBarH + 1);
        }
        uint32_t fill = bar.fill[i] ? bar.fill[i] : color::kPanel;
        if (!bar.highlighted[i] && bar.fill[i])
            fill = scaleColor(fill, 45, 100); // unselected coloured boxes are dimmed
        draw::color(fill);
        draw::rect(x1, kBottomBarY, x2, kBottomBarY + kBottomBarH - 1);
    }

    for (int i = 0; i < 8; i++)
    {
        if (bar.text[i] == nullptr || bar.text[i][0] == 0)
            continue;
        int x1, x2;
        barBox(i, x1, x2);
        bool darkText = bar.highlighted[i] && bar.fill[i];
        draw::color(darkText ? color::kTextDark : color::kText);
        draw::text((x1 + x2) / 2, kBottomBarY + kBottomBarH / 2, fontHandle, OPT_CENTERX | OPT_CENTERY,
                   bar.text[i]);
    }
}

void drawShiftBorder()
{
    draw::color(color::kShiftBorder);
    draw::frame(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1, 4);
}

void drawInfoStrip(const char* title,
                   const char* mode,
                   uint32_t modeColor,
                   const ParamBox* params,
                   int selectedParam,
                   int fontRegular,
                   int fontBold)
{
    const int boxW = 112;
    const int boxX0 = kScreenW - 4 * (boxW + 4) - 2;
    const int cy = kInfoY + kInfoH / 2;

    draw::color(color::kPanel);
    draw::rect(2, kInfoY, kScreenW - 3, kInfoY + kInfoH - 1);

    if (params != nullptr)
    {
        for (int i = 0; i < 4; i++)
        {
            int x1 = boxX0 + i * (boxW + 4);
            bool sel = (i == selectedParam);
            draw::color(sel ? params[i].color : scaleColor(params[i].color, 30, 100));
            draw::rect(x1, kInfoY + 3, x1 + boxW, kInfoY + kInfoH - 4);
        }
    }

    draw::color(color::kText);
    draw::text(10, cy, fontBold, OPT_CENTERY, title);
    if (mode != nullptr && mode[0])
    {
        draw::color(modeColor);
        draw::text((params ? boxX0 : kScreenW) - 8, cy, fontBold, OPT_CENTERY | OPT_RIGHTX, mode);
    }

    if (params != nullptr)
    {
        for (int i = 0; i < 4; i++)
        {
            int x1 = boxX0 + i * (boxW + 4);
            bool sel = (i == selectedParam);
            draw::color(sel ? color::kTextDark : params[i].color);
            draw::text(x1 + 5, cy, fontRegular, OPT_CENTERY, params[i].label);
            draw::text(x1 + boxW - 5, cy, fontBold, OPT_CENTERY | OPT_RIGHTX, params[i].value);
        }
    }
}

} // namespace gibgob
