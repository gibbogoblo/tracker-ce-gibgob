#include "gibgob/TrackGridControl.h"

#include "FT812.h"
#include "display.h"

namespace gibgob
{

namespace
{
constexpr uint32_t kBg = 0x101418;
constexpr uint32_t kTile = 0x1C2329;
constexpr uint32_t kTileMuted = 0x14181C;
constexpr uint32_t kDrumAccent = 0xFF8A3D;
constexpr uint32_t kMelodicAccent = 0x3FB8AF;
constexpr uint32_t kCursor = 0xFFFFFF;
constexpr uint32_t kText = 0xE6E9EC;
constexpr uint32_t kTextDim = 0x8A949C;
constexpr uint32_t kActive = 0xFF4F5E;

constexpr int kCols = 4;
constexpr int kRows = 2;
constexpr int kGap = 8;
constexpr int kInfoH = 26;
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

    const int top = posY + kInfoH;
    const int tileW = (width - kGap * (kCols + 1)) / kCols;
    const int tileH = (height - kInfoH - kGap * (kRows + 1)) / kRows;

    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    API_BEGIN(RECTS);
    API_COLOR(kBg);
    API_VERTEX2F(posX, posY);
    API_VERTEX2F(posX + width - 1, posY + height - 1);

    for (int i = 0; i < 8; i++)
    {
        const TrackTile& t = view->tiles[i];
        int c = i % kCols, r = i / kCols;
        int x = posX + kGap + c * (tileW + kGap);
        int y = top + kGap + r * (tileH + kGap);

        if (i == view->cursor)
        {
            API_COLOR(kCursor);
            API_VERTEX2F(x - 3, y - 3);
            API_VERTEX2F(x + tileW + 2, y + tileH + 2);
        }
        API_COLOR(t.muted ? kTileMuted : kTile);
        API_VERTEX2F(x, y);
        API_VERTEX2F(x + tileW - 1, y + tileH - 1);

        // type accent bar
        API_COLOR(t.melodic ? kMelodicAccent : kDrumAccent);
        API_VERTEX2F(x, y);
        API_VERTEX2F(x + 5, y + tileH - 1);

        if (t.active && !t.muted)
        {
            API_COLOR(kActive);
            API_VERTEX2F(x + tileW - 18, y + 8);
            API_VERTEX2F(x + tileW - 8, y + 18);
        }
    }
    API_END();

    int fTitle = fonts[4].handle; // 17 bold
    int fText = fonts[1].handle;  // 14 regular

    for (int i = 0; i < 8; i++)
    {
        const TrackTile& t = view->tiles[i];
        int c = i % kCols, r = i / kCols;
        int x = posX + kGap + c * (tileW + kGap) + 14;
        int y = top + kGap + r * (tileH + kGap);

        API_COLOR(t.muted ? kTextDim : kText);
        API_CMD_TEXT(x, y + 22, fTitle, OPT_CENTERY, t.title);

        API_COLOR(t.melodic ? kMelodicAccent : kDrumAccent);
        API_CMD_TEXT(x, y + 52, fText, OPT_CENTERY, t.type);

        API_COLOR(t.muted ? kTextDim : kText);
        API_CMD_TEXT(x, y + 82, fText, OPT_CENTERY, t.sound);

        API_COLOR(kTextDim);
        API_CMD_TEXT(x, y + 110, fText, OPT_CENTERY, t.muted ? "MUTED" : t.detail);
    }

    API_COLOR(kText);
    API_CMD_TEXT(posX + 8, posY + kInfoH / 2, fText, OPT_CENTERY, view->info);
}

} // namespace gibgob
