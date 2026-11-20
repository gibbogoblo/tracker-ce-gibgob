#include "gibgob/TableControl.h"

#include "FT812.h"
#include "display.h"

namespace gibgob
{

namespace
{
constexpr uint32_t kRowBg = 0x151B21;
constexpr uint32_t kRowBgCursor = 0x26313A;
constexpr int kTop = kInfoY + kInfoH + 4;
constexpr int kBottom = kBottomBarY - 6;
constexpr int kHeaderH = 22;
} // namespace

TableControl::TableControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobTable, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (TableView*)properties->data;
    }
}

void TableControl::update()
{
    if (view == nullptr)
        return;

    const int fRegular = fonts[1].handle;
    const int fBold = fonts[3].handle;

    if (view->canopy != nullptr)
    {
        draw::begin(kDrawBudget);
        if (!drawHelp(view->help, fRegular, fBold))
        {
            draw::setLighten(view->shift);
            drawChordCanopy(*view->canopy, fRegular, fBold);
            if (view->shift)
                drawShiftBorder();
        }
        draw::end();
        return;
    }
    if (view->rows <= 0 || view->cols <= 0)
        return;

    const bool hasHeader = view->header[0] != nullptr;
    const int top = kTop + (hasHeader ? kHeaderH : 0);
    int rowH = (kBottom - top) / view->rows;
    if (rowH > 38)
        rowH = 38;

    auto colRight = [&](int c) { return (c + 1 < view->cols ? view->colX[c + 1] : kScreenW - 12) - 8; };

    draw::begin(kDrawBudget);

    if (drawHelp(view->help, fRegular, fBold))
    {
        draw::end();
        return;
    }
    draw::setLighten(view->shift);

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    if (view->keyboard)
    {
        // the keyboard draws itself on top; keep this light so both fit
        drawInfoStrip(10, view->title, view->mode, view->modeColor, nullptr, -1, fRegular, fBold);
        drawBottomBar(view->bar, fBold);
        draw::end();
        return;
    }

    // rows
    for (int r = 0; r < view->rows; r++)
    {
        int y1 = top + r * rowH;
        draw::color(r == view->cursorRow ? kRowBgCursor : kRowBg);
        draw::rect(8, y1, kScreenW - 9, y1 + rowH - 3);
    }

    // cursor cell
    uint32_t accent = view->rowColor[view->cursorRow] ? view->rowColor[view->cursorRow] : color::kSelect;
    if (view->cursorRow >= 0 && view->cursorRow < view->rows && view->cursorCol >= 0 &&
        view->cursorCol < view->cols)
    {
        int y1 = top + view->cursorRow * rowH;
        draw::color(accent);
        draw::rect(view->colX[view->cursorCol] - 6, y1 + 1, colRight(view->cursorCol), y1 + rowH - 4);
    }

    drawInfoStrip(10, view->title, view->mode, view->modeColor, nullptr, -1, fRegular, fBold);

    if (hasHeader)
    {
        draw::color(color::kTextDim);
        for (int c = 0; c < view->cols; c++)
            if (view->header[c])
                draw::text(view->colX[c], kTop + kHeaderH / 2, fRegular, OPT_CENTERY, view->header[c]);
    }

    for (int r = 0; r < view->rows; r++)
    {
        int cy = top + r * rowH + (rowH - 3) / 2;
        for (int c = 0; c < view->cols; c++)
        {
            bool cur = (r == view->cursorRow && c == view->cursorCol);
            if (cur)
                draw::color(color::kTextDark);
            else
                draw::color(c == 0 ? color::kTextDim : color::kText);
            draw::text(view->colX[c], cy, cur ? fBold : fRegular, OPT_CENTERY, view->cell[r][c]);
        }
    }

    drawBottomBar(view->bar, fBold);

    if (view->shift)
        drawShiftBorder();

    draw::end();
}

} // namespace gibgob
