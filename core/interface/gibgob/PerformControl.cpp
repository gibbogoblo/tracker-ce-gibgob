#include "gibgob/PerformControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/PerformFx.h"

#include <stdio.h>

namespace gibgob
{

uint32_t performFxColor(int fx)
{
    using namespace perform;
    switch (fx)
    {
        case kFxRoll: return 0xE07B39;
        case kFxStutter: return 0xD9A23A;
        case kFxFilter: return 0x7B6FD6;
        case kFxComb: return 0xA266C9;
        case kFxCut: return 0xB85548;
        case kFxDelayCut: return 0x3FA7B5;
        case kFxReverb: return 0x5AC8FA;
        case kFxReverse: return 0xD6639E;
        case kFxTalkbox: return 0x53B886;
        case kFxVinyl: return 0xB8AA53;
        case kFxGate: return 0xC7CCD1;
        default: return 0x3C3C42;
    }
}

namespace
{
constexpr int kTop = kInfoY + kInfoH + 8;
constexpr int kBottom = kBottomBarY - 8;
constexpr int kMargin = 10;
constexpr int kGap = 8;
} // namespace

PerformControl::PerformControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobPerform, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (PerformView*)properties->data;
    }
}

void PerformControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;

    draw::begin(kDrawBudget);
    if (drawHelp(view->help, fRegular, fBold))
    {
        draw::end();
        return;
    }
    draw::setLighten(view->shift);

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    const int cellW = (kScreenW - 2 * kMargin - 3 * kGap) / 4;
    const int cellH = (kBottom - kTop - kGap) / 2;

    // cell backgrounds and frames first, text after (fewer state changes)
    for (int i = 0; i < 8; i++)
    {
        const PerformView::CellView& c = view->cells[i];
        const int x1 = kMargin + (i % 4) * (cellW + kGap);
        const int y1 = kTop + (i / 4) * (cellH + kGap);
        const int x2 = x1 + cellW - 1;
        const int y2 = y1 + cellH - 1;
        uint32_t fill = c.off ? color::kPanel : scaleColor(c.color, c.active ? 5 : 2, c.active ? 5 : 5);
        draw::color(fill);
        draw::rect(x1, y1, x2, y2);
        if (i == view->cursor)
        {
            draw::color(color::kSelect);
            draw::frame(x1 - 3, y1 - 3, x2 + 3, y2 + 3, 3);
        }
    }

    drawInfoStrip(10, view->title, nullptr, 0, view->params, view->selectedParam, fRegular, fBold);

    for (int i = 0; i < 8; i++)
    {
        const PerformView::CellView& c = view->cells[i];
        const int x1 = kMargin + (i % 4) * (cellW + kGap);
        const int y1 = kTop + (i / 4) * (cellH + kGap);
        const bool dark = c.active && !c.off;
        draw::color(dark ? color::kTextDark : color::kText);
        draw::text(x1 + cellW / 2, y1 + 50, fBold, OPT_CENTER, c.name);
        draw::color(dark ? color::kTextDark : color::kTextDim);
        for (int l = 0; l < 3; l++)
        {
            if (c.line[l][0] == 0)
                continue;
            draw::text(x1 + cellW / 2, y1 + 86 + l * 24, fRegular, OPT_CENTER, c.line[l]);
        }
    }

    drawBottomBar(view->bar, fBold);
    if (view->shift)
        drawShiftBorder();
    draw::end();
}

} // namespace gibgob
