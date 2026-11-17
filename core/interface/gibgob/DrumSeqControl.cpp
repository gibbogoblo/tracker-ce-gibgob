#include "gibgob/DrumSeqControl.h"

#include "FT812.h"
#include "display.h"

#include <stdio.h>

namespace gibgob
{

namespace
{
constexpr int kTop = kInfoY + kInfoH + 8;
constexpr int kBottom = kBottomBarY - 8;
constexpr int kLabelW = 56;
constexpr uint32_t kCellOff = 0x22222A;
} // namespace

DrumSeqControl::DrumSeqControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobDrumSeq, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (DrumSeqView*)properties->data;
    }
}

void DrumSeqControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;
    const int rows = DrumSeqView::kRows;
    const int cols = DrumSeqView::kCols;
    const int rowH = (kBottom - kTop) / rows;
    const int cellW = (kScreenW - kLabelW - 8) / cols;

    draw::begin(kDrawBudget);
    if (drawHelp(view->help, fRegular, fBold))
    {
        draw::end();
        return;
    }
    draw::setLighten(view->shift);

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    int cursorRow = view->cursorStep / cols;

    // row labels, current row highlighted
    for (int r = 0; r < rows; r++)
    {
        int row = view->firstRow + r;
        if (row * cols >= view->steps)
            break;
        int y = kTop + r * rowH;
        draw::color(row == cursorRow ? color::kDrum : color::kPanel);
        draw::rect(4, y + 2, kLabelW - 4, y + rowH - 4);
    }

    // cells
    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            const DrumCell& cell = view->cell[r * cols + c];
            if (!cell.exists)
                continue;
            int x1 = kLabelW + c * cellW + 3;
            int y1 = kTop + r * rowH + 2;
            int x2 = x1 + cellW - 7;
            int y2 = y1 + rowH - 7;
            if (cell.on)
            {
                int v = cell.velocity;
                int bright = 35 + (65 * v) / 127;
                if (cell.chance < 100)
                    bright = bright * 75 / 100;
                draw::color(scaleColor(color::kDrum, bright, 100));
            }
            else
            {
                // beats a little lighter, like a drum machine's step lights
                draw::color(((view->firstRow + r) * cols + c) % 4 == 0 ? 0x2E2E38 : kCellOff);
            }
            draw::rect(x1, y1, x2, y2);

            // nudge: a small tick left or right of the centre
            if (cell.on && cell.nudge != 0)
            {
                int cx = (x1 + x2) / 2 + (cell.nudge * (cellW / 2 - 6)) / 24;
                draw::color(color::kText);
                draw::rect(cx - 1, y2 - 10, cx + 1, y2 - 3);
            }
        }
    }

    // fixed furniture before text-heavy parts
    drawInfoStrip(view->title, view->mode, view->modeColor, view->params, view->selectedParam, fRegular, fBold);
    drawBottomBar(view->bar, fBold);

    // playhead, cursor and Shift border before the text, so text is the only
    // thing that can ever be left out on a very busy screen
    int first = view->firstRow * cols;
    if (view->playStep >= first && view->playStep < first + rows * cols)
    {
        int k = view->playStep - first;
        int x1 = kLabelW + (k % cols) * cellW + 3;
        int y1 = kTop + (k / cols) * rowH + 2;
        draw::color(color::kPlayhead);
        draw::frame(x1 - 2, y1 - 2, x1 + cellW - 5, y1 + rowH - 5, 2);
    }
    if (view->cursorStep >= first && view->cursorStep < first + rows * cols)
    {
        int k = view->cursorStep - first;
        int x1 = kLabelW + (k % cols) * cellW + 3;
        int y1 = kTop + (k / cols) * rowH + 2;
        draw::color(color::kSelect);
        draw::frame(x1 - 3, y1 - 3, x1 + cellW - 4, y1 + rowH - 4, 3);
    }
    if (view->shift)
        drawShiftBorder();

    // text: the first step number of each row, then pitch and chance marks
    char buf[12];
    for (int r = 0; r < rows; r++)
    {
        int row = view->firstRow + r;
        if (row * cols >= view->steps)
            break;
        int y = kTop + r * rowH;
        snprintf(buf, sizeof(buf), "%d", row * cols + 1);
        draw::color(row == cursorRow ? color::kTextDark : color::kTextDim);
        draw::text(kLabelW / 2, y + rowH / 2 - 2, fBold, OPT_CENTERX | OPT_CENTERY, buf);
    }
    draw::color(color::kTextDark);
    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            const DrumCell& cell = view->cell[r * cols + c];
            if (!cell.exists || !cell.on)
                continue;
            int x1 = kLabelW + c * cellW + 3;
            int y1 = kTop + r * rowH + 2;
            if (cell.pitch[0])
                draw::text(x1 + 6, y1 + 12, fBold, OPT_CENTERY, cell.pitch);
            if (cell.chance < 100)
            {
                snprintf(buf, sizeof(buf), "%d%%", cell.chance);
                draw::text(x1 + 6, y1 + rowH - 22, fRegular, OPT_CENTERY, buf);
            }
        }
    }

    draw::end();
}

} // namespace gibgob
