#include "gibgob/PianoRollControl.h"

#include "FT812.h"
#include "display.h"
#include "gibgob/MelodicTrack.h"
#include "mtStructs.h"

namespace gibgob
{

namespace
{
// GibGob palette
constexpr uint32_t kBg = 0x101418;
constexpr uint32_t kRowBlack = 0x0B0E11;
constexpr uint32_t kRowC = 0x18222B;
constexpr uint32_t kGridStep = 0x20282F;
constexpr uint32_t kGridBeat = 0x3A4752;
constexpr uint32_t kOutside = 0x050607;
constexpr uint32_t kKeyWhite = 0xC8CDD2;
constexpr uint32_t kKeyBlack = 0x2A3036;
constexpr uint32_t kKeyText = 0x101418;
constexpr uint32_t kKeyTextBlack = 0xC8CDD2;
constexpr uint32_t kNote = 0x3FB8AF;
constexpr uint32_t kNoteSelected = 0xFFB347;
constexpr uint32_t kGhost = 0xFFFFFF;
constexpr uint32_t kCursor = 0xFFFFFF;
constexpr uint32_t kPlayhead = 0xFF4F5E;
constexpr uint32_t kInfoBg = 0x1C2329;
constexpr uint32_t kInfoText = 0xE6E9EC;

constexpr int kKeyW = 56;
constexpr int kInfoH = 24;

inline bool isBlackKey(int pitch)
{
    int n = pitch % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

uint32_t scaleColor(uint32_t c, int num, int den)
{
    uint32_t r = ((c >> 16) & 0xFF) * num / den;
    uint32_t g = ((c >> 8) & 0xFF) * num / den;
    uint32_t b = (c & 0xFF) * num / den;
    return (r << 16) | (g << 8) | b;
}

void rect(int x1, int y1, int x2, int y2)
{
    API_VERTEX2F(x1, y1);
    API_VERTEX2F(x2, y2);
}
} // namespace

PianoRollControl::PianoRollControl(strControlProperties* properties)
  : cDisplayControl(controlIdGibGobPianoRoll, 1)
{
    if (properties)
    {
        posX = properties->x;
        posY = properties->y;
        width = properties->w;
        height = properties->h;
        style = properties->style;
        view = (PianoRollView*)properties->data;
    }
}

int PianoRollControl::rowHeight() const
{
    int rows = view->rowsVisible > 0 ? view->rowsVisible : 12;
    return (height - kInfoH) / rows;
}

int PianoRollControl::tickToX(int tick) const
{
    int gridW = width - kKeyW;
    int rel = tick - view->firstTick;
    return posX + kKeyW + (rel * gridW) / view->ticksVisible;
}

int PianoRollControl::pitchToY(int pitch) const
{
    return posY + kInfoH + (view->topPitch - pitch) * rowHeight();
}

void PianoRollControl::update()
{
    if (view == nullptr)
        return;

    const int gx1 = posX + kKeyW;
    const int gx2 = posX + width - 1;
    const int gy1 = posY + kInfoH;
    const int rh = rowHeight();
    const int gy2 = gy1 + rh * view->rowsVisible - 1;
    const int bottomPitch = view->topPitch - view->rowsVisible + 1;
    const int lastTick = view->firstTick + view->ticksVisible;

    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    // ---- background + rows ----
    API_BEGIN(RECTS);
    API_COLOR(kBg);
    rect(posX, posY, posX + width - 1, gy2);

    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        int y = gy1 + r * rh;
        if (isBlackKey(pitch))
        {
            API_COLOR(kRowBlack);
            rect(gx1, y, gx2, y + rh - 1);
        }
        else if (pitch % 12 == 0)
        {
            API_COLOR(kRowC);
            rect(gx1, y, gx2, y + rh - 1);
        }
    }

    // ---- area past the pattern end ----
    if (view->patternTicks < lastTick)
    {
        int x = tickToX(view->patternTicks > view->firstTick ? view->patternTicks : view->firstTick);
        API_COLOR(kOutside);
        rect(x, gy1, gx2, gy2);
    }

    // ---- vertical grid lines ----
    int firstStep = view->firstTick / 48;
    for (int s = firstStep; s * 48 <= lastTick; s++)
    {
        int x = tickToX(s * 48);
        if (x < gx1 || x > gx2)
            continue;
        API_COLOR(s % 4 == 0 ? kGridBeat : kGridStep);
        rect(x, gy1, x, gy2);
    }
    // horizontal separators
    API_COLOR(kGridStep);
    for (int r = 1; r < view->rowsVisible; r++)
    {
        int y = gy1 + r * rh;
        rect(gx1, y, gx2, y);
    }

    // ---- notes ----
    if (view->pattern)
    {
        int count = noteCount(view->pattern, view->track);
        for (int i = 0; i < count; i++)
        {
            Note n = getNote(view->pattern, view->track, i);
            if (n.pitch > view->topPitch || n.pitch < bottomPitch)
                continue;
            int s = n.start, e = n.start + n.length;
            if (e <= view->firstTick || s >= lastTick)
                continue;
            int x1 = tickToX(s < view->firstTick ? view->firstTick : s);
            int x2 = tickToX(e > lastTick ? lastTick : e) - 2;
            if (x2 < x1 + 2)
                x2 = x1 + 2;
            int y1 = pitchToY(n.pitch) + 2;
            int y2 = y1 + rh - 5;
            uint32_t base = (n.flags & kNoteFlagSelected) ? kNoteSelected : kNote;
            // velocity shading: 40%..100% brightness
            int v = n.velocity ? n.velocity : 100;
            API_COLOR(scaleColor(base, 40 + (60 * v) / 127, 100));
            rect(x1, y1, x2, y2);
        }
    }

    // ---- ghost notes (cursor) ----
    API_COLOR(kGhost);
    API_COLOR_A(70);
    int gs = view->cursorTick + view->ghostNudge;
    int ge = gs + view->ghostLength;
    for (int i = 0; i < view->ghostCount; i++)
    {
        int p = view->ghostPitches[i];
        if (p > view->topPitch || p < bottomPitch)
            continue;
        int x1 = tickToX(gs < view->firstTick ? view->firstTick : gs);
        int x2 = tickToX(ge > lastTick ? lastTick : ge) - 2;
        int y1 = pitchToY(p) + 2;
        rect(x1, y1, x2, y1 + rh - 5);
    }
    API_COLOR_A(255);

    // ---- playhead ----
    if (view->playheadTick >= view->firstTick && view->playheadTick < lastTick)
    {
        int x = tickToX(view->playheadTick);
        API_COLOR(kPlayhead);
        rect(x, gy1, x + 1, gy2);
    }

    // ---- piano keys column ----
    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        int y = gy1 + r * rh;
        API_COLOR(isBlackKey(pitch) ? kKeyBlack : kKeyWhite);
        rect(posX, y, posX + kKeyW - 3, y + rh - 2);
    }

    // ---- info strip ----
    API_COLOR(kInfoBg);
    rect(posX, posY, posX + width - 1, posY + kInfoH - 2);
    API_END();

    // ---- cursor outline (root pitch) ----
    if (view->cursorPitch <= view->topPitch && view->cursorPitch >= bottomPitch)
    {
        int x1 = tickToX(gs < view->firstTick ? view->firstTick : gs);
        int x2 = tickToX(ge > lastTick ? lastTick : ge) - 2;
        int y1 = pitchToY(view->cursorPitch) + 1;
        int y2 = y1 + rh - 3;
        API_COLOR(kCursor);
        API_LINE_WIDTH(24);
        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(x1, y1);
        API_VERTEX2F(x2, y1);
        API_VERTEX2F(x2, y2);
        API_VERTEX2F(x1, y2);
        API_VERTEX2F(x1, y1);
        API_END();
    }

    // ---- text ----
    int fontHandle = fonts[1].handle;
    for (int r = 0; r < view->rowsVisible; r++)
    {
        int pitch = view->topPitch - r;
        if (pitch < 0 || pitch > 127)
            continue;
        API_COLOR(isBlackKey(pitch) ? kKeyTextBlack : kKeyText);
        API_CMD_TEXT(posX + 4, gy1 + r * rh + rh / 2, fontHandle, OPT_CENTERY, mtNotes[pitch]);
    }

    API_COLOR(kInfoText);
    API_CMD_TEXT(posX + 6, posY + kInfoH / 2, fontHandle, OPT_CENTERY, view->info);
}

} // namespace gibgob
