#include "gibgob/ChordCanopy.h"
#include "gibgob/gibgobChordList.h"

#include "FT812.h"
#include "display.h"

#include <stdio.h>

namespace gibgob
{

namespace
{
// the jungle
constexpr uint32_t kSky = 0x0B1F14;       // deep under the canopy
constexpr uint32_t kCanopyTop = 0x143D24;
constexpr uint32_t kLeaf[4] = { 0x1D5530, 0x2A7340, 0x3B9150, 0x56B05F };
constexpr uint32_t kVine = 0x4A8F3C;
constexpr uint32_t kVineLeaf = 0x6DBF4E;
constexpr uint32_t kFruit = 0xB8732A;      // a mango, waiting
constexpr uint32_t kFruitLit = 0xFFC83A;   // ... ripe: the note sounds
constexpr uint32_t kWood = 0x1E160F;       // behind the keys
constexpr uint32_t kKeyWhite = 0xE6DDBF;   // bamboo
constexpr uint32_t kKeyWhiteChord = 0x9BD46A;
constexpr uint32_t kKeyWhiteLit = 0xD9FF7C;
constexpr uint32_t kKeyBlack = 0x2B2119;
constexpr uint32_t kKeyBlackChord = 0x3E8E41;
constexpr uint32_t kKeyBlackLit = 0x86E05A;
constexpr uint32_t kFieldBg = 0x16301F;

constexpr int kCanopyY1 = kInfoY + kInfoH + 3; // 68
constexpr int kCanopyY2 = 128;
constexpr int kFruitY = 204;
constexpr int kKeysX1 = 18;
constexpr int kKeysX2 = 782;
constexpr int kKeysY1 = 220;
constexpr int kKeysY2 = 340;
constexpr int kBlackH = 70;
constexpr int kFieldsY1 = 350;
constexpr int kFieldsY2 = 418;

// the leaves of the canopy: x, y, radius, shade (fixed, so nothing flickers)
struct Leaf
{
    int16_t x, y;
    uint8_t r, shade;
};
constexpr Leaf kLeaves[] = {
    // darkest at the back ...
    { 20, 78, 30, 0 }, { 128, 82, 30, 0 }, { 292, 74, 26, 0 }, { 452, 80, 28, 0 }, { 562, 84, 30, 0 }, { 724, 74, 26, 0 },
    { 75, 74, 26, 1 }, { 236, 84, 30, 1 }, { 508, 74, 26, 1 }, { 668, 82, 30, 1 }, { 160, 106, 20, 1 }, { 642, 106, 20, 1 },
    { 186, 76, 24, 2 }, { 348, 80, 28, 2 }, { 616, 76, 24, 2 }, { 780, 80, 30, 2 }, { 48, 104, 20, 2 }, { 264, 106, 20, 2 },
    { 536, 106, 20, 2 }, { 752, 104, 20, 2 },
    // ... the sunlit ones in front
    { 104, 110, 18, 3 }, { 212, 112, 16, 3 }, { 318, 112, 18, 3 }, { 482, 112, 18, 3 }, { 590, 112, 16, 3 }, { 696, 110, 18, 3 },
};

constexpr int8_t kWhiteOf[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

bool inList(const uint8_t* list, int count, int note)
{
    for (int i = 0; i < count; i++)
        if (list[i] == note)
            return true;
    return false;
}

void circle(int cx, int cy, int r)
{
    draw::roundRect(cx - r, cy - r, cx + r, cy + r, r);
}

struct Keys
{
    int lowC;
    int octaves;
    int whiteW;
    int blackW;
    bool isBlack(int note) const { return kWhiteOf[((note % 12) + 12) % 12] < 0; }
    int whiteIndex(int note) const
    {
        const int rel = note - lowC;
        return (rel / 12) * 7 + kWhiteOf[rel % 12];
    }
    // left edge of a white key, centre of any key
    int whiteX(int idx) const { return kKeysX1 + idx * whiteW; }
    int centre(int note) const
    {
        if (!isBlack(note))
            return whiteX(whiteIndex(note)) + whiteW / 2;
        return whiteX(whiteIndex(note + 1)); // between it and the white key above
    }
    bool shown(int note) const { return note >= lowC && note < lowC + 12 * octaves; }
};
} // namespace

void canopyKeyRange(const uint8_t* notes, int count, int& lowC, int& octaves)
{
    int lo = 127, hi = 0;
    for (int i = 0; i < count; i++)
    {
        lo = notes[i] < lo ? notes[i] : lo;
        hi = notes[i] > hi ? notes[i] : hi;
    }
    if (count == 0)
        lo = hi = 60;
    lowC = lo - lo % 12;
    octaves = (hi - lowC) / 12 + 1;
    octaves = octaves < 2 ? 2 : (octaves > 3 ? 3 : octaves);
    if (lowC + 12 * octaves > 128)
        lowC = 128 - 12 * octaves;
}

// the chord list over the canopy: one row per progression, its style, name
// and chords; the highlighted one in fruit gold
void drawChordList(const CanopyView& v, int fRegular, int fBold)
{
    static const uint32_t kStyleColor[4] = { 0xE88AC0, 0xFFC83A, 0x5AC8FA, 0xB69CFF };
    const int x1 = 12, x2 = kScreenW - 13, y1 = kCanopyY1 + 2, y2 = kFieldsY2;
    draw::color(kWood);
    draw::roundRect(x1, y1, x2, y2, 14);
    char head[64];
    snprintf(head, sizeof(head), "Chord list  %d/%d      Key: %s", v.listIndex + 1, v.listCount, v.listKey);
    draw::color(kFruitLit);
    draw::text(x1 + 18, y1 + 18, fBold, OPT_CENTERY, head);
    draw::color(color::kTextDim);
    draw::text(x2 - 16, y1 + 18, fRegular, OPT_CENTERY | OPT_RIGHTX, "Left/Right: key   Middle: load into 1-8");
    const int rowH = 38, top = y1 + 40;
    for (int i = 0; i < v.listShown && i < CanopyView::kListRows; i++)
    {
        const CanopyView::ListRow& r = v.listRows[i];
        const int ry = top + i * rowH;
        const bool cur = i == v.listCursor;
        if (cur)
        {
            draw::color(kFruitLit);
            draw::roundRect(x1 + 8, ry, x2 - 8, ry + rowH - 4, 12);
        }
        const uint32_t sc = kStyleColor[r.style & 3];
        draw::color(cur ? color::kTextDark : sc);
        draw::text(x1 + 22, ry + rowH / 2 - 2, fRegular, OPT_CENTERY, kChordListStyleNames[r.style & 3]);
        draw::color(cur ? color::kTextDark : color::kText);
        draw::text(x1 + 112, ry + rowH / 2 - 2, fBold, OPT_CENTERY, r.name);
        draw::color(cur ? color::kTextDark : color::kTextDim);
        draw::text(x1 + 290, ry + rowH / 2 - 2, fRegular, OPT_CENTERY, r.chords);
    }
}

void drawChordCanopy(const CanopyView& v, int fRegular, int fBold)
{
    draw::color(kSky);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);

    if (v.listOpen)
    {
        drawChordList(v, fRegular, fBold);
        drawInfoStrip(10, v.title, nullptr, 0, v.params, v.mode, fRegular, fBold);
        drawBottomBar(v.bar, fBold);
        return;
    }

    // ---- the canopy ----
    draw::fadeRect(0, kCanopyY1, kScreenW - 1, kFruitY + 20, kCanopyTop, kSky, true);
    int shade = -1;
    for (const Leaf& l : kLeaves)
    {
        if (l.shade != shade)
        {
            shade = l.shade;
            draw::color(kLeaf[shade]);
        }
        circle(l.x, l.y, l.r);
    }

    // ---- the keys (drawn before the vines, which hang down onto them) ----
    Keys k;
    canopyKeyRange(v.notes, v.noteCount, k.lowC, k.octaves);
    const int whites = 7 * k.octaves;
    k.whiteW = (kKeysX2 - kKeysX1) / whites;
    k.blackW = k.whiteW * 3 / 5;
    const int keysRight = kKeysX1 + whites * k.whiteW;

    draw::color(kWood);
    draw::roundRect(kKeysX1 - 8, kKeysY1 - 8, keysRight + 8, kKeysY2 + 8, 8);

    // white keys: plain ones first (one colour), then the chord's
    draw::color(kKeyWhite);
    for (int i = 0; i < whites; i++)
    {
        const int note = k.lowC + (i / 7) * 12 + (int)"\x00\x02\x04\x05\x07\x09\x0b"[i % 7];
        if (!inList(v.notes, v.noteCount, note))
            draw::rect(k.whiteX(i) + 1, kKeysY1, k.whiteX(i) + k.whiteW - 2, kKeysY2);
    }
    for (int n = 0; n < v.noteCount; n++)
    {
        const int note = v.notes[n];
        if (!k.shown(note) || k.isBlack(note))
            continue;
        const int i = k.whiteIndex(note);
        draw::color(inList(v.sounding, v.soundingCount, note) ? kKeyWhiteLit : kKeyWhiteChord);
        draw::rect(k.whiteX(i) + 1, kKeysY1, k.whiteX(i) + k.whiteW - 2, kKeysY2);
    }
    // black keys
    draw::color(kKeyBlack);
    for (int note = k.lowC; note < k.lowC + 12 * k.octaves; note++)
        if (k.isBlack(note) && !inList(v.notes, v.noteCount, note))
        {
            const int cx = k.centre(note);
            draw::rect(cx - k.blackW / 2, kKeysY1, cx + k.blackW / 2, kKeysY1 + kBlackH);
        }
    for (int n = 0; n < v.noteCount; n++)
    {
        const int note = v.notes[n];
        if (!k.shown(note) || !k.isBlack(note))
            continue;
        const int cx = k.centre(note);
        draw::color(inList(v.sounding, v.soundingCount, note) ? kKeyBlackLit : kKeyBlackChord);
        draw::rect(cx - k.blackW / 2, kKeysY1, cx + k.blackW / 2, kKeysY1 + kBlackH);
    }

    // ---- a vine down to each note of the chord, a fruit at its end ----
    draw::lineWidth(40);
    draw::color(kVine);
    for (int n = 0; n < v.noteCount; n++)
    {
        if (!k.shown(v.notes[n]))
            continue;
        const int x = k.centre(v.notes[n]);
        const int sway = (n & 1) ? 4 : -4;
        const int16_t xy[10] = { (int16_t)x, (int16_t)(kCanopyY2 - 20), (int16_t)(x + sway), (int16_t)(kCanopyY2 + 10),
                                 (int16_t)(x - sway), (int16_t)(kCanopyY2 + 40), (int16_t)(x + sway / 2),
                                 (int16_t)(kFruitY - 30), (int16_t)x, (int16_t)(kFruitY - 6) };
        draw::lineStrip(xy, 5);
    }
    draw::color(kVineLeaf);
    for (int n = 0; n < v.noteCount; n++)
    {
        if (!k.shown(v.notes[n]))
            continue;
        const int x = k.centre(v.notes[n]);
        const int side = (n & 1) ? 1 : -1;
        draw::roundRect(x + side * 2 + (side < 0 ? -12 : 0), kCanopyY2 + 18, x + side * 2 + (side < 0 ? 0 : 12),
                        kCanopyY2 + 25, 3);
        draw::roundRect(x - side * 2 + (side > 0 ? -12 : 0), kCanopyY2 + 48, x - side * 2 + (side > 0 ? 0 : 12),
                        kCanopyY2 + 55, 3);
    }
    // fruit: a glow behind the ripe ones (the notes sounding), then the fruit
    bool anyLit = false;
    for (int n = 0; n < v.noteCount; n++)
        anyLit = anyLit || (k.shown(v.notes[n]) && inList(v.sounding, v.soundingCount, v.notes[n]));
    if (anyLit)
    {
        draw::alpha(90);
        draw::color(kFruitLit);
        for (int n = 0; n < v.noteCount; n++)
            if (k.shown(v.notes[n]) && inList(v.sounding, v.soundingCount, v.notes[n]))
                circle(k.centre(v.notes[n]), kFruitY, 16);
        draw::alpha(255);
        draw::color(kFruitLit);
        for (int n = 0; n < v.noteCount; n++)
            if (k.shown(v.notes[n]) && inList(v.sounding, v.soundingCount, v.notes[n]))
                circle(k.centre(v.notes[n]), kFruitY, 10);
    }
    draw::color(kFruit);
    for (int n = 0; n < v.noteCount; n++)
        if (k.shown(v.notes[n]) && !inList(v.sounding, v.soundingCount, v.notes[n]))
            circle(k.centre(v.notes[n]), kFruitY, 8);

    // note names on the chord's keys
    char name[6];
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int n = 0; n < v.noteCount; n++)
    {
        const int note = v.notes[n];
        if (!k.shown(note))
            continue;
        const int cx = k.centre(note);
        if (k.isBlack(note))
        {
            draw::color(color::kTextDark);
            draw::text(cx, kKeysY1 + kBlackH - 12, fRegular, OPT_CENTER, kNames[note % 12]);
        }
        else
        {
            snprintf(name, sizeof(name), "%s%d", kNames[note % 12], note / 12 - 1);
            draw::color(color::kTextDark);
            draw::text(cx, kKeysY2 - 14, fRegular, OPT_CENTER, name);
        }
    }

    // the chord's name, on a branch in the middle of the canopy
    {
        char label[40];
        snprintf(label, sizeof(label), "%d  %s", v.chordIndex + 1, v.chordName);
        draw::color(kSky);
        draw::roundRect(kScreenW / 2 - 92, 84, kScreenW / 2 + 92, 118, 16);
        draw::color(kFruitLit);
        draw::text(kScreenW / 2, 101, fBold, OPT_CENTER, label);
    }

    // ---- the values: leaf-shaped boxes ----
    if (v.fieldCount > 0)
    {
        const int gap = 6;
        const int total = kScreenW - 16;
        int w = (total - gap * (v.fieldCount - 1)) / v.fieldCount;
        w = w > 150 ? 150 : w;
        const int x0 = (kScreenW - (w * v.fieldCount + gap * (v.fieldCount - 1))) / 2;
        // leaf-shaped boxes: the plain ones in one go, then the one with the cursor
        draw::color(kFieldBg);
        for (int i = 0; i < v.fieldCount; i++)
            if (i != v.cursor)
                draw::roundRect(x0 + i * (w + gap), kFieldsY1, x0 + i * (w + gap) + w, kFieldsY2, 14);
        if (v.cursor >= 0 && v.cursor < v.fieldCount)
        {
            draw::color(v.fields[v.cursor].color);
            draw::roundRect(x0 + v.cursor * (w + gap), kFieldsY1, x0 + v.cursor * (w + gap) + w, kFieldsY2, 14);
        }
        int x = x0;
        for (int i = 0; i < v.fieldCount; i++, x += w + gap)
        {
            const CanopyView::Field& f = v.fields[i];
            const bool cur = (i == v.cursor);
            draw::color(cur ? color::kTextDark : color::kTextDim);
            draw::text(x + w / 2, kFieldsY1 + 14, fRegular, OPT_CENTER, f.label);
            draw::color(cur ? color::kTextDark : color::kText);
            draw::text(x + w / 2, kFieldsY1 + 36, fBold, OPT_CENTER, f.value);
            if (f.meterMax > 0)
            {
                // a vine that grows with the value
                const int bx1 = x + 16, bx2 = x + w - 16, by = kFieldsY2 - 12;
                draw::color(cur ? scaleColor(f.color, 3, 5) : kWood);
                draw::roundRect(bx1, by - 3, bx2, by + 3, 3);
                if (f.meter > 0)
                {
                    draw::color(cur ? color::kTextDark : f.color);
                    draw::roundRect(bx1, by - 3, bx1 + 6 + (bx2 - bx1 - 6) * f.meter / f.meterMax, by + 3, 3);
                }
            }
        }
    }

    drawInfoStrip(10, v.title, nullptr, 0, v.params, v.mode, fRegular, fBold);
    drawBottomBar(v.bar, fBold);
}

} // namespace gibgob
