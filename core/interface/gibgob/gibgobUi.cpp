#include "gibgob/gibgobUi.h"

#include "FT812.h"
#include "core/interface.h"
#include "core/interfaceDefs.h"

namespace gibgob
{

bool g_shiftLatched = false;

bool shiftActive()
{
    return g_shiftLatched ||
           mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
}

uint32_t scaleColor(uint32_t c, int num, int den)
{
    uint32_t r = ((c >> 16) & 0xFF) * num / den;
    uint32_t g = ((c >> 8) & 0xFF) * num / den;
    uint32_t b = (c & 0xFF) * num / den;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (r << 16) | (g << 8) | b;
}

static void boxRect(int i, int& x1, int& x2)
{
    const int w = kScreenW / 8;
    x1 = i * w + 3;
    x2 = (i + 1) * w - 4;
}

void drawBottomBarRects(const BottomBar& bar)
{
    // call between API_BEGIN(RECTS) / API_END()
    API_COLOR(color::kBg);
    API_VERTEX2F(0, kBottomBarY - 2);
    API_VERTEX2F(kScreenW - 1, kBottomBarY + kBottomBarH + 1);

    for (int i = 0; i < 8; i++)
    {
        int x1, x2;
        boxRect(i, x1, x2);
        if (bar.highlighted[i])
        {
            API_COLOR(color::kSelect);
            API_VERTEX2F(x1 - 2, kBottomBarY - 2);
            API_VERTEX2F(x2 + 2, kBottomBarY + kBottomBarH + 1);
        }
        uint32_t fill = bar.fill[i] ? bar.fill[i] : color::kPanel;
        if (!bar.highlighted[i] && bar.fill[i])
            fill = scaleColor(fill, 45, 100); // unselected coloured boxes are dimmed
        API_COLOR(fill);
        API_VERTEX2F(x1, kBottomBarY);
        API_VERTEX2F(x2, kBottomBarY + kBottomBarH - 1);
    }
}

void drawBottomBarText(const BottomBar& bar, int fontHandle)
{
    for (int i = 0; i < 8; i++)
    {
        if (bar.text[i] == nullptr || bar.text[i][0] == 0)
            continue;
        int x1, x2;
        boxRect(i, x1, x2);
        bool darkText = bar.highlighted[i] && bar.fill[i];
        API_COLOR(darkText ? color::kTextDark : color::kText);
        API_CMD_TEXT((x1 + x2) / 2,
                     kBottomBarY + kBottomBarH / 2,
                     fontHandle,
                     OPT_CENTERX | OPT_CENTERY,
                     bar.text[i]);
    }
}

void drawShiftBorder(int x, int y, int w, int h)
{
    // call between API_BEGIN(RECTS) / API_END()
    constexpr int t = 4;
    API_COLOR(color::kShiftBorder);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w - 1, y + t - 1);
    API_VERTEX2F(x, y + h - t);
    API_VERTEX2F(x + w - 1, y + h - 1);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + t - 1, y + h - 1);
    API_VERTEX2F(x + w - t, y);
    API_VERTEX2F(x + w - 1, y + h - 1);
}

} // namespace gibgob
