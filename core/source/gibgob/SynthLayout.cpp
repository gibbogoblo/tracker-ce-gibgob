#include "gibgob/SynthLayout.h"

#include <stdio.h>

namespace gibgob
{
namespace synthui
{

const char* const kPageNames[kPageCount] = { "Main", "Osc", "Filter", "Amp", "LFO", "Env", "FX", "Patch" };

namespace
{
bool contains(const char* lowered, const char* word)
{
    return strstr(lowered, word) != nullptr;
}

void lower(const char* in, char* out, int outLen)
{
    int i = 0;
    for (; in && in[i] && i < outLen - 1; i++)
        out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

bool equalNoCase(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == 0 && *b == 0;
}

// rows one column holds besides its picture
int rowLimit(int graph)
{
    if (graph == kGraphEnv)
        return 10; // A D S R as one line of boxes + 6 rows
    return graph == kGraphNone ? 11 : 8;
}

int graphFor(int page, const Group& g)
{
    if (g.isEnvelope())
        return kGraphEnv;
    char name[16];
    lower(g.name, name, sizeof(name));
    switch (page)
    {
        case kPageOsc: return g.find(kRoleShape) >= 0 ? kGraphOsc : kGraphNone;
        case kPageFilter: return g.find(kRoleCutoff) >= 0 ? kGraphFilter : kGraphNone;
        case kPageLfo: return (g.find(kRoleShape) >= 0 || g.find(kRoleRate) >= 0) ? kGraphLfo : kGraphNone;
        case kPageFx:
            if (contains(name, "verb"))
                return kGraphReverb;
            if (contains(name, "delay") || contains(name, "echo") || g.find(kRoleFeedback) >= 0)
                return kGraphDelay;
            return kGraphNone;
        default: return kGraphNone;
    }
}

void fillGroup(Group& g, int page, const char* name, const int16_t* ids, const char* const* names, const bool* isEnum,
               int count)
{
    memset(&g, 0, sizeof(g));
    snprintf(g.name, sizeof(g.name), "%s", name ? name : "");
    g.count = (uint8_t)(count > kMaxRows ? kMaxRows : count);
    for (int i = 0; i < 4; i++)
        g.adsr[i] = -1;
    for (int i = 0; i < g.count; i++)
    {
        g.id[i] = ids[i];
        g.isEnum[i] = isEnum[i];
        g.role[i] = (uint8_t)roleFromName(names[i], isEnum[i]);
    }
    // an envelope needs all four
    const int a = g.find(kRoleAttack), d = g.find(kRoleDecay), s = g.find(kRoleSustain), r = g.find(kRoleRelease);
    if (a >= 0 && d >= 0 && s >= 0 && r >= 0)
    {
        g.adsr[0] = (int8_t)a;
        g.adsr[1] = (int8_t)d;
        g.adsr[2] = (int8_t)s;
        g.adsr[3] = (int8_t)r;
    }
    g.graph = (uint8_t)(page == kPageMain ? kGraphNone : graphFor(page, g));
}
} // namespace

Role roleFromName(const char* name, bool isEnum)
{
    char n[32];
    lower(name, n, sizeof(n));
    if (contains(n, "attack"))
        return kRoleAttack;
    if (contains(n, "sustain"))
        return kRoleSustain;
    if (contains(n, "release"))
        return kRoleRelease;
    if (contains(n, "decay"))
        return kRoleDecay;
    if (contains(n, "cutoff"))
        return kRoleCutoff;
    if (contains(n, "reso"))
        return kRoleReso;
    if (contains(n, "shape") || contains(n, "wave") || contains(n, "morph"))
        return kRoleShape;
    if (contains(n, "rate") || contains(n, "speed"))
        return kRoleRate;
    if (contains(n, "fdbk") || contains(n, "feedback"))
        return kRoleFeedback;
    if (contains(n, "time"))
        return kRoleTime;
    if (contains(n, "mix"))
        return kRoleMix;
    if (contains(n, "level") || contains(n, "volume"))
        return kRoleLevel;
    if (isEnum && contains(n, "type"))
        return kRoleType;
    return kRoleNone;
}

void abbreviate(const char* in, char* out, int chars)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", in ? in : "");
    int len = (int)strlen(buf);
    // drop small vowels (not the first letter), last ones first
    for (int i = len - 1; i > 0 && len > chars; i--)
    {
        const char c = buf[i];
        if ((c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') && buf[i - 1] != ' ')
        {
            memmove(buf + i, buf + i + 1, (size_t)(len - i));
            len--;
        }
    }
    if (len > chars)
        buf[chars] = 0;
    // no space at the end
    len = (int)strlen(buf);
    while (len > 0 && buf[len - 1] == ' ')
        buf[--len] = 0;
    memcpy(out, buf, (size_t)len + 1);
}

int pageFromShortName(const char* shortName)
{
    for (int p = kPageOsc; p <= kPageFx; p++)
        if (equalNoCase(shortName, kPageNames[p]))
            return p;
    return -1;
}

void clearModel(SynthModel& m)
{
    memset(&m, 0, sizeof(m));
}

void addGroup(SynthModel& m, int page, const char* name, const int16_t* ids, const char* const* names,
              const bool* isEnum, int count)
{
    if (page < 0 || page >= kPageCount || count <= 0)
        return;
    PageModel& p = m.page[page];
    bool first = true;
    while (count > 0 && p.groupCount < kMaxGroups)
    {
        Group& g = p.groups[p.groupCount];
        int n = count > kMaxRows ? kMaxRows : count;
        char title[16];
        snprintf(title, sizeof(title), first ? "%s" : "%.12s +", name ? name : "");
        fillGroup(g, page, title, ids, names, isEnum, n);
        if (page != kPageMain)
        {
            const int limit = rowLimit(g.graph);
            if (n > limit)
            {
                n = limit;
                fillGroup(g, page, title, ids, names, isEnum, n);
                // the first part may have lost its envelope: look again
                if (g.graph == kGraphEnv && !g.isEnvelope())
                    g.graph = kGraphNone;
            }
        }
        p.groupCount++;
        ids += n;
        names += n;
        isEnum += n;
        count -= n;
        first = false;
    }
}

void finishModel(SynthModel& m)
{
    for (int pg = 0; pg < kPageCount; pg++)
    {
        PageModel& p = m.page[pg];
        p.viewCount = 0;
        int g = 0;
        while (g < p.groupCount && p.viewCount < kMaxViews)
        {
            int n = p.groupCount - g;
            if (n > kGroupsPerView)
                n = kGroupsPerView;
            p.viewFirst[p.viewCount] = (uint8_t)g;
            p.viewLen[p.viewCount] = (uint8_t)n;
            p.viewCount++;
            g += n;
        }
    }
}

// ---------------------------------------------------------------------------
namespace
{
struct Builder
{
    ViewLayout& l;

    int panel(int x, int y, int w, int h, int graph, int group)
    {
        if (l.panelCount >= kMaxPanels)
            return kMaxPanels - 1;
        PanelPos& p = l.panel[l.panelCount];
        p.x = (int16_t)x;
        p.y = (int16_t)y;
        p.w = (int16_t)w;
        p.h = (int16_t)h;
        p.gx = p.gy = p.gw = p.gh = 0;
        p.graph = (uint8_t)graph;
        p.group = (int8_t)group;
        return l.panelCount++;
    }
    void graphArea(int panelIdx, int x, int y, int w, int h)
    {
        PanelPos& p = l.panel[panelIdx];
        p.gx = (int16_t)x;
        p.gy = (int16_t)y;
        p.gw = (int16_t)w;
        p.gh = (int16_t)h;
    }
    int cell(int x, int y, int w, int h, int kind, int panelIdx, int id, int role, bool isEnum)
    {
        if (l.cellCount >= kMaxCells)
            return -1;
        CellPos& c = l.cell[l.cellCount];
        c.x = (int16_t)x;
        c.y = (int16_t)y;
        c.w = (int16_t)w;
        c.h = (int16_t)h;
        c.kind = (uint8_t)kind;
        c.panel = (uint8_t)panelIdx;
        c.id = (int16_t)id;
        c.role = (uint8_t)role;
        c.isEnum = isEnum;
        return l.cellCount++;
    }
    // rows of a group (all but the envelope ones when skipEnv), from y down to yEnd
    void rows(const Group& g, int panelIdx, int x, int w, int y, int yEnd, bool skipEnv, int maxH, int gap)
    {
        int n = 0;
        for (int i = 0; i < g.count; i++)
            if (!(skipEnv && isAdsrRow(g, i)))
                n++;
        if (n == 0)
            return;
        int rh = (yEnd - y) / n;
        if (rh > maxH)
            rh = maxH;
        for (int i = 0; i < g.count; i++)
        {
            if (skipEnv && isAdsrRow(g, i))
                continue;
            cell(x, y, w, rh - gap, kCellRow, panelIdx, g.id[i], g.role[i], g.isEnum[i]);
            y += rh;
        }
    }
    // a line of four A / D / S / R boxes
    void chips(const Group& g, int panelIdx, int x, int w, int y, int h)
    {
        const int gap = 6;
        const int cw = (w - 3 * gap) / 4;
        for (int k = 0; k < 4; k++)
        {
            const int r = g.adsr[k];
            cell(x + k * (cw + gap), y, cw, h, kCellChip, panelIdx, g.id[r], g.role[r], g.isEnum[r]);
        }
    }
    static bool isAdsrRow(const Group& g, int i)
    {
        for (int k = 0; k < 4; k++)
            if (g.adsr[k] == i)
                return true;
        return false;
    }
};

void setQuick(ViewLayout& l)
{
    for (int k = 0; k < 4; k++)
        l.quick[k] = -1;
    int n = 0;
    if (l.style == kStyleMain)
    {
        for (int i = 0; i < l.cellCount && n < 4; i++)
            l.quick[n++] = (int8_t)i;
        return;
    }
    if (l.style == kStylePatch)
    {
        const int16_t want[4] = { kItemGain, kItemPan, kItemReverb, kItemDelay };
        for (int k = 0; k < 4; k++)
            l.quick[k] = (int8_t)cellOf(l, want[k]);
        return;
    }
    // an envelope on the page: its A / D / S / R
    for (int i = 0; i < l.cellCount && n < 4; i++)
        if (l.cell[i].kind == kCellChip)
            l.quick[n++] = (int8_t)i;
    if (n == 4)
        return;
    // one from each column in turn: the first settings that are numbers,
    // then lists if there is room left
    for (int k = 0; k < 4; k++)
        l.quick[k] = -1;
    n = 0;
    for (int pass = 0; pass < 2 && n < 4; pass++)
    {
        int nth[kMaxPanels] = { 0 };
        bool any = true;
        while (n < 4 && any)
        {
            any = false;
            for (int p = 0; p < l.panelCount && n < 4; p++)
            {
                int seen = 0;
                for (int i = 0; i < l.cellCount; i++)
                {
                    const CellPos& c = l.cell[i];
                    if (c.panel != p || c.isEnum != (pass == 1))
                        continue;
                    if (seen++ == nth[p])
                    {
                        l.quick[n++] = (int8_t)i;
                        nth[p]++;
                        any = true;
                        break;
                    }
                }
            }
        }
    }
}
} // namespace

void layoutPage(const SynthModel& m, int page, int view, ViewLayout& out)
{
    memset(&out, 0, sizeof(out));
    out.page = (uint8_t)page;
    Builder b{ out };
    const int top = kContentTop, bottom = kContentBottom;

    if (page == kPagePatch)
    {
        out.style = kStylePatch;
        const int p0 = b.panel(10, top, 385, bottom - top, kGraphNone, -1);
        const int p1 = b.panel(405, top, 385, bottom - top, kGraphNone, -1);
        const int16_t left[kPatchActions] = { kItemLoad, kItemSave, kItemSaveAs, kItemInit, kItemGoApe, kItemApeAmount };
        const int16_t right[kPatchSettings] = { kItemEngine, kItemVoices, kItemGain, kItemPan, kItemReverb, kItemDelay };
        for (int k = 0; k < kPatchActions; k++)
            b.cell(20, top + 34 + k * 50, 365, 42, left[k] == kItemApeAmount ? kCellRow : kCellAction, p0, left[k],
                   kRoleNone, false);
        for (int k = 0; k < kPatchSettings; k++)
            b.cell(415, top + 34 + k * 50, 365, 42, kCellRow, p1, right[k], kRoleNone, false);
        setQuick(out);
        return;
    }

    const PageModel& p = m.page[page];
    if (page == kPageMain)
    {
        out.style = kStyleMain;
        // the boxes on the left (text left of x = 512 costs the display half),
        // the output picture and the signal path on the right
        const int p0 = b.panel(490, top, 300, 148, kGraphOsc, -1);
        b.graphArea(p0, 500, top + 26, 280, 112);
        b.panel(490, top + 158, 300, 104, kGraphNone, -1);
        if (p.groupCount > 0)
        {
            const Group& g = p.groups[0];
            for (int i = 0; i < g.count && i < kMaxMain; i++)
                b.cell(10 + (i % 3) * 158, top + (i / 3) * 60, 150, 52, kCellGrid, 0, g.id[i], g.role[i], g.isEnum[i]);
        }
        setQuick(out);
        return;
    }

    if (p.viewCount == 0)
    {
        out.style = kStyleColumns;
        return;
    }
    if (view < 0 || view >= p.viewCount)
        view = 0;
    out.view = (uint8_t)view;
    const int first = p.viewFirst[view];
    const int count = p.viewLen[view];
    const Group& g0 = p.groups[first];

    if (count == 1 && g0.graph != kGraphNone && g0.graph != kGraphEnv)
    {
        // Filter style: big picture, list on the right
        out.style = kStyleGraphList;
        const int p0 = b.panel(10, top, 500, bottom - top, g0.graph, first);
        b.graphArea(p0, 22, top + 28, 476, bottom - top - 64);
        b.rows(g0, p0, 520, 270, top, bottom, false, 50, 6);
    }
    else if (g0.isEnvelope() && (count == 1 || (count == 2 && p.groups[first + 1].graph == kGraphNone)))
    {
        // Amp style: big envelope, A D S R under it, the rest on the right
        out.style = kStyleEnvList;
        const int p0 = b.panel(10, top, 520, 296, kGraphEnv, first);
        b.graphArea(p0, 24, top + 28, 492, 236);
        b.chips(g0, p0, 10, 520, top + 304, 44);
        int n = 0;
        for (int i = 0; i < g0.count; i++)
            if (!Builder::isAdsrRow(g0, i))
                n++;
        const Group* g1 = count == 2 ? &p.groups[first + 1] : nullptr;
        const int total = n + (g1 ? g1->count : 0);
        if (total > 0)
        {
            int rh = (bottom - top) / total;
            if (rh > 60)
                rh = 60;
            int y = top;
            for (int i = 0; i < g0.count; i++)
            {
                if (Builder::isAdsrRow(g0, i))
                    continue;
                b.cell(540, y, 250, rh - 8, kCellRow, p0, g0.id[i], g0.role[i], g0.isEnum[i]);
                y += rh;
            }
            for (int i = 0; g1 && i < g1->count; i++)
            {
                b.cell(540, y, 250, rh - 8, kCellRow, p0, g1->id[i], g1->role[i], g1->isEnum[i]);
                y += rh;
            }
        }
    }
    else
    {
        // columns
        out.style = kStyleColumns;
        const int gap = 10;
        const int colW = (780 - (count - 1) * gap) / count;
        for (int k = 0; k < count; k++)
        {
            const Group& g = p.groups[first + k];
            const int x = 10 + k * (colW + gap);
            const int pi = b.panel(x, top, colW, bottom - top, g.graph, first + k);
            int y = top + 30;
            if (g.graph != kGraphNone)
            {
                b.graphArea(pi, x + 10, y, colW - 20, 70);
                y += 78;
            }
            if (g.isEnvelope())
            {
                b.chips(g, pi, x + 10, colW - 20, y, 44);
                y += 52;
            }
            b.rows(g, pi, x + 10, colW - 20, y, bottom - 8, g.isEnvelope(), 38, 5);
        }
    }
    setQuick(out);
}

int navigate(const ViewLayout& l, int from, int dx, int dy)
{
    if (from < 0 || from >= l.cellCount)
        return l.cellCount > 0 ? 0 : -1;
    const CellPos& a = l.cell[from];
    const int ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    int best = from;
    long bestScore = 0x7FFFFFFF;
    for (int i = 0; i < l.cellCount; i++)
    {
        if (i == from)
            continue;
        const CellPos& c = l.cell[i];
        const int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        const int ddx = cx - ax, ddy = cy - ay;
        long primary, secondary;
        if (dx != 0)
        {
            if (ddx * dx <= 4)
                continue;
            primary = ddx * dx;
            secondary = ddy < 0 ? -ddy : ddy;
        }
        else
        {
            if (ddy * dy <= 4)
                continue;
            primary = ddy * dy;
            secondary = ddx < 0 ? -ddx : ddx;
        }
        const long score = primary + 3 * secondary;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

int cellOf(const ViewLayout& l, int id)
{
    for (int i = 0; i < l.cellCount; i++)
        if (l.cell[i].id == id)
            return i;
    return -1;
}

} // namespace synthui
} // namespace gibgob
