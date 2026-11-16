#include "gibgob/TrackGridControl.h"

#include "FT812.h"
#include "display.h"

namespace gibgob
{

namespace
{
constexpr uint32_t kActive = 0xFF2A3A;

constexpr int kCols = 4;
constexpr int kRows = 2;
constexpr int kGap = 10;
constexpr int kInfoY = 31;
constexpr int kInfoH = 28;
constexpr int kTilesTop = kInfoY + kInfoH + 4;
constexpr int kTilesBottom = kBottomBarY - 6;
} // namespace

TrackGridControl::TrackGridControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobTrackGrid, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (TrackGridView*)properties->data;
    }
}

void TrackGridControl::update()
{
    if (view == nullptr)
        return;

    const int tileW = (kScreenW - kGap * (kCols + 1)) / kCols;
    const int tileH = (kTilesBottom - kTilesTop - kGap * (kRows - 1)) / kRows;

    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    API_BEGIN(RECTS);
    API_COLOR(color::kBg);
    API_VERTEX2F(0, posY);
    API_VERTEX2F(kScreenW - 1, posY + height - 1);

    API_COLOR(color::kPanel);
    API_VERTEX2F(2, kInfoY);
    API_VERTEX2F(kScreenW - 3, kInfoY + kInfoH - 1);

    for (int i = 0; i < 8; i++)
    {
        const TrackTile& t = view->tiles[i];
        int c = i % kCols, r = i / kCols;
        int x = kGap + c * (tileW + kGap);
        int y = kTilesTop + r * (tileH + kGap);
        bool sel = (i == view->cursor);

        if (sel)
        {
            API_COLOR(color::kSelect);
            API_VERTEX2F(x - 4, y - 4);
            API_VERTEX2F(x + tileW + 3, y + tileH + 3);
        }

        // whole tile is coloured by track type; dimmer when not selected / muted
        uint32_t base = t.melodic ? color::kMelodic : color::kDrum;
        int bright = t.muted ? 25 : (sel ? 100 : 60);
        API_COLOR(scaleColor(base, bright, 100));
        API_VERTEX2F(x, y);
        API_VERTEX2F(x + tileW - 1, y + tileH - 1);

        if (t.active && !t.muted)
        {
            API_COLOR(kActive);
            API_VERTEX2F(x + tileW - 22, y + 10);
            API_VERTEX2F(x + tileW - 10, y + 22);
        }
    }

    drawBottomBarRects(view->bar);

    if (view->shift)
        drawShiftBorder(0, posY, kScreenW, height);

    API_END();

    const int fTitle = fonts[4].handle; // 17 bold
    const int fText = fonts[1].handle;  // 14 regular
    const int fBold = fonts[3].handle;  // 14 bold

    for (int i = 0; i < 8; i++)
    {
        const TrackTile& t = view->tiles[i];
        int c = i % kCols, r = i / kCols;
        int x = kGap + c * (tileW + kGap) + 12;
        int y = kTilesTop + r * (tileH + kGap);
        bool sel = (i == view->cursor);

        // dark text on bright tiles, light text on dim ones
        uint32_t txt = (sel && !t.muted) ? color::kTextDark : color::kText;
        API_COLOR(txt);
        API_CMD_TEXT(x, y + 24, fTitle, OPT_CENTERY, t.title);
        API_CMD_TEXT(x, y + 56, fBold, OPT_CENTERY, t.type);
        API_CMD_TEXT(x, y + 90, fText, OPT_CENTERY, t.sound);
        API_CMD_TEXT(x, y + 120, fText, OPT_CENTERY, t.muted ? "MUTED" : t.detail);
    }

    API_COLOR(color::kText);
    API_CMD_TEXT(10, kInfoY + kInfoH / 2, fText, OPT_CENTERY, view->info);

    drawBottomBarText(view->bar, fBold);
}

} // namespace gibgob
