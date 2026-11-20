// GibGob OS synth editor: page model, layout, navigation and drawing.
// Included by screen_test.cpp (uses its word counter and SVG writer).
#include "gibgob/SynthEditControl.h"
#include "gibgob/SynthLayout.h"
#include <ctype.h>
#include <string>
#include <vector>

namespace synthfix
{
// the members buildModel reads, like polysynth::Patch::Metadata
struct Element { int type = 1; int id = -1; };
struct Sub { std::string name; std::vector<Element> elements; };
struct Section { std::string name, shortName; std::vector<Sub> subsections; std::vector<Element> elements; };
struct Layout { std::vector<Section> sections; };
struct Param { std::string displayName; bool isEnum = false; };
struct Meta { std::vector<Param> params; Layout layout33; };

// MORPHANGUTAN's parameters and layout
Meta morph()
{
    Meta m;
    const char* names[] = { "MAMA shape", "MAMA tune", "MAMA fine", "MAMA level", "BABY shape", "BABY tune", "BABY fine", "BABY level",
                            "PAPA shape", "PAPA octave", "PAPA level", "Noise", "Drift", "Filter type", "Cutoff", "Resonance", "Drive",
                            "Key track", "Attack", "Decay", "Sustain", "Release", "Volume", "Pan", "Velocity", "Glide mode", "Glide",
                            "LFO1 shape", "LFO1 rate", "LFO1 sync", "LFO1 trig", "LFO1 dest A", "LFO1 amt A", "LFO1 dest B", "LFO1 amt B",
                            "LFO2 shape", "LFO2 rate", "LFO2 sync", "LFO2 trig", "LFO2 dest A", "LFO2 amt A", "LFO2 dest B", "LFO2 amt B",
                            "Env1 attack", "Env1 decay", "Env1 sustain", "Env1 release", "Env1 dest A", "Env1 amt A", "Env1 dest B", "Env1 amt B",
                            "Env2 attack", "Env2 decay", "Env2 sustain", "Env2 release", "Env2 dest A", "Env2 amt A", "Env2 dest B", "Env2 amt B",
                            "Delay", "Delay time", "Delay fdbk", "Delay mix", "Reverb", "Rev decay", "Rev tone", "Rev mix" };
    const int enums[] = { 9, 13, 25, 27, 29, 30, 31, 33, 35, 37, 38, 39, 41, 47, 49, 55, 57, 59, 60, 63 };
    for (const char* n : names) m.params.push_back({ n, false });
    for (int e : enums) m.params[e].isEnum = true;
    auto els = [](std::initializer_list<int> ids) { std::vector<Element> v; for (int i : ids) v.push_back({ 1, i }); return v; };
    m.layout33.sections = {
        { "Main", "GibGob", {}, els({ 14, 15, 18, 21, 0, 4, 10, 12, 48, 16, 62, 66 }) },
        { "Oscillators", "Osc", { { "MAMA", els({ 0, 1, 2, 3 }) }, { "BABY", els({ 4, 5, 6, 7 }) }, { "PAPA", els({ 8, 9, 10, 11, 12 }) } }, {} },
        { "Filter", "Filter", { { "", els({ 13, 14, 15, 16, 17 }) } }, {} },
        { "Amp", "Amp", { { "Envelope", els({ 18, 19, 20, 21 }) }, { "Voice", els({ 22, 23, 24, 25, 26 }) } }, {} },
        { "LFOs", "LFO", { { "LFO 1", els({ 27, 28, 29, 30, 31, 32, 33, 34 }) }, { "LFO 2", els({ 35, 36, 37, 38, 39, 40, 41, 42 }) } }, {} },
        { "Envelopes", "Env", { { "Env 1", els({ 43, 44, 45, 46, 47, 48, 49, 50 }) }, { "Env 2", els({ 51, 52, 53, 54, 55, 56, 57, 58 }) } }, {} },
        { "Effects", "FX", { { "Delay", els({ 59, 60, 61, 62 }) }, { "Reverb", els({ 63, 64, 65, 66 }) } }, {} },
    };
    return m;
}
} // namespace synthfix

static int synthTests(int limit)
{
    using namespace synthui;
    static SynthModel model;
    synthfix::Meta meta = synthfix::morph();
    if (!buildModel(meta, model) || !model.valid) { printf("  synth: model not built\n"); fails++; return 0; }

    // a synth without the GibGob section keeps the stock editor
    {
        synthfix::Meta stock = synthfix::morph();
        stock.layout33.sections.erase(stock.layout33.sections.begin());
        static SynthModel m2;
        if (buildModel(stock, m2) || m2.valid) { printf("  synth: stock synth taken over\n"); fails++; }
    }

    // what landed where
    const PageModel& osc = model.page[kPageOsc];
    if (osc.groupCount != 3 || osc.groups[0].graph != kGraphOsc) { printf("  synth: osc page wrong (%d)\n", osc.groupCount); fails++; }
    const PageModel& amp = model.page[kPageAmp];
    if (amp.groupCount != 2 || !amp.groups[0].isEnvelope() || amp.groups[1].graph != kGraphNone) { printf("  synth: amp page wrong\n"); fails++; }
    const PageModel& env = model.page[kPageEnv];
    if (env.groupCount != 2 || !env.groups[0].isEnvelope() || !env.groups[1].isEnvelope()) { printf("  synth: env page wrong\n"); fails++; }
    if (model.page[kPageFilter].groups[0].graph != kGraphFilter) { printf("  synth: filter picture missing\n"); fails++; }
    if (model.page[kPageLfo].groups[1].graph != kGraphLfo) { printf("  synth: lfo picture missing\n"); fails++; }
    if (model.page[kPageFx].groups[0].graph != kGraphDelay || model.page[kPageFx].groups[1].graph != kGraphReverb) { printf("  synth: fx pictures wrong\n"); fails++; }
    {
        char ab[16];
        abbreviate("Resonance", ab, 6);
        if (strcmp(ab, "Resnnc") != 0) { printf("  synth: abbreviate gave %s\n", ab); fails++; }
        abbreviate("Env1 amt A", ab, 6);
        if (strlen(ab) > 6) { printf("  synth: abbreviate too long (%s)\n", ab); fails++; }
        abbreviate("Cutoff", ab, 6);
        if (strcmp(ab, "Cutoff") != 0) { printf("  synth: abbreviate changed a short name (%s)\n", ab); fails++; }
    }
    model.engineCount = 9; model.engineFirst = 1; // more engines than rows: a scrolled list
    {
        ViewLayout l; layoutPage(model, kPageMain, 0, l);
        int engines = 0;
        for (int i = 0; i < l.cellCount; i++) if (isEngineItem(l.cell[i].id)) engines++;
        if (engines != kEngineRows || cellOf(l, kItemEngineFirst + 1) < 0 || cellOf(l, kItemVoices) < 0) { printf("  synth: main page engine list wrong (%d)\n", engines); fails++; }
    }

    {
        ViewLayout l; layoutPage(model, kPageOsc, 0, l);
        bool cols[3] = { false, false, false };
        for (int k = 0; k < 3; k++) if (l.quick[k] >= 0) cols[l.cell[l.quick[k]].panel] = true;
        if (!cols[0] || !cols[1] || !cols[2]) { printf("  synth: colour buttons not spread over the columns\n"); fails++; }
        layoutPage(model, kPageAmp, 0, l);
        for (int k = 0; k < 4; k++) if (l.quick[k] < 0 || l.cell[l.quick[k]].role != kRoleAttack + k) { printf("  synth: Amp colour buttons are not A D S R\n"); fails++; break; }
    }
    // a singer (LILITRON): the Voice page shows the avatar, big
    {
        synthfix::Meta lili;
        lili.params = { { "Syllable", true }, { "Cat", true } };
        lili.layout33.sections = { { "Main", "GibGob", {}, { { 1, 1 } } }, { "Voice", "Osc", { { "LILITRON", { { 1, 0 }, { 1, 1 } } } }, {} } };
        static SynthModel lm;
        if (!buildModel(lili, lm) || lm.page[kPageOsc].groups[0].graph != kGraphAvatar) { printf("  synth: singer has no avatar\n"); fails++; }
        static SynthView av;
        memset(&av, 0, sizeof(av));
        layoutPage(lm, kPageOsc, 0, av.layout);
        if (av.layout.style != kStyleGraphList) { printf("  synth: singer page style %d\n", av.layout.style); fails++; }
        snprintf(av.synthName, sizeof(av.synthName), "LILITRON"); av.editing = true;
        snprintf(av.title, sizeof(av.title), "Voice");
        snprintf(av.panelTitle[0], 18, "LILITRON");
        for (int i = 0; i < av.layout.cellCount; i++) { snprintf(av.cells[i].label, 20, i ? "Cat" : "Syllable"); snprintf(av.cells[i].value, 16, i ? "On" : "tah"); av.cells[i].fill = -1; }
        static const char* const kTabs[8] = { "MAIN", "VOICE", "FILTER", "AMP", "LFO", "ENV", "FX", "PATCH" };
        for (int i = 0; i < 8; i++) { av.tabText[i] = kTabs[i]; av.tabOn[i] = i == 1; }
        strControlProperties prop; prop.y = 0; prop.w = 800; prop.h = 480; prop.data = &av;
        SynthEditControl sc(&prop);
        g_minY = 0;
        for (int pic = 0; pic < 2; pic++)
            for (int sing = 0; sing < 2; sing++)
            {
                av.avatar.cat = true; av.avatar.singing = sing; snprintf(av.avatar.word, 8, "umm");
                for (int k = 0; k < SynthAvatar::kPics; k++) { av.avatar.loaded[k] = pic; av.avatar.address[k] = 350000 + k * 40000; av.avatar.w[k] = 140; av.avatar.h[k] = 140; }
                av.shift = sing;
                char nm[48]; snprintf(nm, 48, "synth singer, %s, %s", pic ? "pictures" : "stand-ins", sing ? "singing shift" : "quiet");
                render(sc, nm, limit, sing);
                if (getenv("SYNTH_SVG") && !pic && sing)
                {
                    av.shift = false;
                    char path[256]; snprintf(path, sizeof(path), "%s/9_singer.svg", getenv("SYNTH_SVG"));
                    g_svg = fopen(path, "w");
                    if (g_svg) { fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n"); words = 0; sc.update(); fprintf(g_svg, "</svg>\n"); fclose(g_svg); g_svg = nullptr; }
                }
            }
        g_minY = 27;
    }
    // a long column is split, and 4+ columns make a second set
    {
        synthfix::Meta big = synthfix::morph();
        std::vector<synthfix::Element> many;
        for (int i = 0; i < 30; i++) many.push_back({ 1, i });
        big.layout33.sections[1].subsections.push_back({ "HUGE", many });
        static SynthModel m3;
        buildModel(big, m3);
        const PageModel& p = m3.page[kPageOsc];
        int rows = 0;
        for (int g = 0; g < p.groupCount; g++) rows += p.groups[g].count;
        if (rows != 13 + 30 || p.viewCount < 2) { printf("  synth: split wrong (%d rows, %d views)\n", rows, p.viewCount); fails++; }
        for (int v = 0; v < p.viewCount; v++)
        {
            ViewLayout l; layoutPage(m3, kPageOsc, v, l);
            for (int i = 0; i < l.cellCount; i++)
                if (l.cell[i].h < 18) { printf("  synth: rows too thin (%d)\n", l.cell[i].h); fails++; break; }
        }
    }

    static SynthView sv;
    const char* const svgDir = getenv("SYNTH_SVG");
    const char* const kStyleName[] = { "main", "graph+list", "envelope+list", "columns", "patch" };
    for (int page = 0; page < kPageCount; page++)
    {
        const int views = page == kPageMain || page == kPagePatch ? 1 : (model.page[page].viewCount ? model.page[page].viewCount : 1);
        for (int vi = 0; vi < views; vi++)
        {
            memset(&sv, 0, sizeof(sv));
            ViewLayout& l = sv.layout;
            layoutPage(model, page, vi, l);
            if (l.cellCount == 0) { printf("  synth: page %s empty\n", kPageNames[page]); fails++; continue; }

            // every box on the screen, inside the content area, none on top of another
            for (int i = 0; i < l.cellCount; i++)
            {
                const CellPos& a = l.cell[i];
                if (a.x < 4 || a.y < kContentTop || a.x + a.w > 796 || a.y + a.h > kContentBottom + 4)
                { printf("  synth %s: cell %d outside (%d,%d %dx%d)\n", kPageNames[page], i, a.x, a.y, a.w, a.h); fails++; }
                for (int j = i + 1; j < l.cellCount; j++)
                {
                    const CellPos& b = l.cell[j];
                    if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
                    { printf("  synth %s: cells %d and %d overlap\n", kPageNames[page], i, j); fails++; }
                }
            }
            // the D-pad reaches every box from the first one
            {
                bool seen[kMaxCells] = { false };
                int queue[kMaxCells], qn = 0, qi = 0;
                queue[qn++] = 0; seen[0] = true;
                while (qi < qn)
                {
                    const int c = queue[qi++];
                    const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (auto& dd : d)
                    {
                        const int n = navigate(l, c, dd[0], dd[1]);
                        if (n >= 0 && !seen[n]) { seen[n] = true; queue[qn++] = n; }
                    }
                }
                if (qn != l.cellCount) { printf("  synth %s: D-pad reaches %d of %d boxes\n", kPageNames[page], qn, l.cellCount); fails++; }
            }
            // the coloured buttons have something to pick
            for (int k = 0; k < 4; k++)
                if (l.quick[k] < 0 || l.quick[k] >= l.cellCount) { printf("  synth %s: colour button %d has nothing\n", kPageNames[page], k + 1); fails++; }

            // worst-case texts
            snprintf(sv.synthName, sizeof(sv.synthName), "MORPHANGUTAN");
            snprintf(sv.headerInfo, sizeof(sv.headerInfo), "TRK 5 // WWWWWWWWWWWWWWW");
            snprintf(sv.pageText, sizeof(sv.pageText), "%d/%d", vi + 1, views);
            snprintf(sv.title, sizeof(sv.title), "Mod envelopes 2/3");
            snprintf(sv.pathText, sizeof(sv.pathText), "MAMA + BABY + PAPA > LP 24 > ADSR > FX");
            if (l.style == kStyleMain) { snprintf(sv.hint, sizeof(sv.hint), "Free: 99999 KB (old engines in /Synths?)"); snprintf(sv.pathText, sizeof(sv.pathText), "4294967295 voices failed: memory full"); sv.warn = true; }
            if (l.style == kStyleEnvList) snprintf(sv.hint, sizeof(sv.hint), "Colour buttons: A / D / S / R");
            for (int i = 0; i < l.cellCount; i++)
            {
                const int id = l.cell[i].id;
                snprintf(sv.cells[i].label, sizeof(sv.cells[i].label), "%s", id >= 0 && id < (int)meta.params.size() ? meta.params[id].displayName.c_str() : "Reverb send");
                snprintf(sv.cells[i].value, sizeof(sv.cells[i].value), l.cell[i].isEnum ? "Smooth rnd" : "-100.0 ms");
                sv.cells[i].fill = (int8_t)((i * 37) % 101);
                sv.cells[i].bipolar = (i % 4) == 3;
            }
            for (int p = 0; p < l.panelCount; p++)
            {
                snprintf(sv.panelTitle[p], sizeof(sv.panelTitle[p]), "%s", l.panel[p].group >= 0 ? model.page[page].groups[l.panel[p].group].name : (p ? "SIGNAL PATH" : "OUTPUT"));
                snprintf(sv.panelInfo[p], sizeof(sv.panelInfo[p]), "Smooth random");
                SynthGraph& g = sv.graph[p];
                g.a = 0.1f; g.d = 0.5f; g.s = 0.7f; g.r = 0.6f; g.handle = 1; g.shape = 1.6f + p; g.cycles = 3;
                g.cutoff = 0.55f; g.reso = 0.6f; g.filterType = 0; g.amount = 0.6f;
            }
            for (int k = 0; k < 4; k++)
            {
                snprintf(sv.quick[k].label, sizeof(sv.quick[k].label), "Rls");
                snprintf(sv.quick[k].value, sizeof(sv.quick[k].value), "250 ms");
                sv.quickActive[k] = (k == 1);
            }
            static const char* const kTabs[8] = { "MAIN", "OSC", "FILTER", "AMP", "LFO", "ENV", "FX", "PATCH" };
            static const char* const kTools[8] = { "INIT PG", "APE PG", "COPY PG", "PASTE PG", "COMPARE", "UNDO", "LATCH", "SAVE" };
            sv.editing = true;
            sv.cursor = l.cellCount - 1;
            snprintf(sv.message, sizeof(sv.message), "Page copied");

            strControlProperties prop; prop.y = 0; prop.w = 800; prop.h = 480; prop.data = &sv;
            SynthEditControl sc(&prop);
            g_minY = 0;
            for (int shift = 0; shift < 2; shift++)
            {
                sv.shift = shift;
                for (int i = 0; i < 8; i++) { sv.tabText[i] = shift ? kTools[i] : kTabs[i]; sv.tabOn[i] = shift ? (i == 4 || i == 6) : (i == page); }
                char nm[48];
                snprintf(nm, sizeof(nm), "synth %s %d/%d (%s)%s", kPageNames[page], vi + 1, views, kStyleName[l.style], shift ? " shift" : "");
                render(sc, nm, limit, shift);
                if (g_ctxN != 0) { printf("  synth: save/restore not paired (%d)\n", g_ctxN); fails++; g_ctxN = 0; }
            }
            if (svgDir)
            {
                // a picture: no Shift, a short note off, the cursor on the second box
                sv.shift = false; sv.message[0] = 0; sv.cursor = l.cellCount > 1 ? 1 : 0;
                for (int i = 0; i < 8; i++) { sv.tabText[i] = kTabs[i]; sv.tabOn[i] = (i == page); }
                for (int i = 0; i < l.cellCount; i++)
                    if (isEngineItem(l.cell[i].id))
                    {
                        static const char* const kEng[9] = { "ACD", "DIRT", "FAT", "MORPHANGUTAN", "PERC", "VAP", "WTFM", "CHIPTUNE", "BASSBOON" };
                        snprintf(sv.cells[i].label, sizeof(sv.cells[i].label), "%s", kEng[(l.cell[i].id - kItemEngineFirst) % 9]);
                        snprintf(sv.cells[i].value, sizeof(sv.cells[i].value), "%s", l.cell[i].id - kItemEngineFirst == 3 ? "in use" : "");
                        sv.cells[i].lit = l.cell[i].id - kItemEngineFirst == 3;
                        sv.cells[i].fill = -1;
                    }
                if (l.style == kStyleMain)
                {
                    snprintf(sv.panelTitle[0], 18, "ENGINE"); snprintf(sv.panelTitle[1], 18, "TRACK SOUND");
                    snprintf(sv.pathText, sizeof(sv.pathText), "All synths share 8 voices");
                    snprintf(sv.hint, sizeof(sv.hint), "Plugin memory free: 1712 KB");
                    sv.warn = false;
                }
                static const char* const kItemLabel[12] = { "Load patch", "Save", "Save as", "Init patch", "GO APE!", "Ape amount",
                                                            "Engine", "Voices", "Gain", "Pan", "Reverb send", "Delay send" };
                static const char* const kItemValue[12] = { "Hairy Lead", "unsaved edits", "new name", "clean start", "shake it all", "30%",
                                                            "MORPHANGUTAN", "4  (+4 free)", "-9.0 dB", "C", "20%", "10%" };
                for (int i = 0; i < l.cellCount; i++)
                {
                    const int id = l.cell[i].id;
                    if (isEngineItem(id))
                        continue;
                    if (id >= kItemLoad && id <= kItemDelay)
                    {
                        snprintf(sv.cells[i].label, sizeof(sv.cells[i].label), "%s", kItemLabel[id - kItemLoad]);
                        snprintf(sv.cells[i].value, sizeof(sv.cells[i].value), "%s", kItemValue[id - kItemLoad]);
                        sv.cells[i].fill = -1;
                    }
                    else
                        snprintf(sv.cells[i].value, sizeof(sv.cells[i].value), l.cell[i].isEnum ? "Tri" : "%d%%", (i * 37) % 101);
                }
                if (l.style == kStylePatch) { snprintf(sv.panelTitle[0], 18, "PATCH"); snprintf(sv.panelTitle[1], 18, "INIT / GO APE"); }
                for (int p = 0; p < l.panelCount; p++)
                    if (l.panel[p].group >= 0) for (char* c = sv.panelTitle[p]; *c; c++) *c = (char)toupper((unsigned char)*c);
                for (int k = 0; k < 4; k++)
                {
                    const int c = l.quick[k];
                    abbreviate(sv.cells[c].label, sv.quick[k].label, 9);
                    snprintf(sv.quick[k].value, sizeof(sv.quick[k].value), "%s", l.cell[c].kind == kCellAction ? "" : sv.cells[c].value);
                    sv.quickActive[k] = (c == sv.cursor);
                }
                snprintf(sv.headerInfo, sizeof(sv.headerInfo), "TRK 5 // HAIRY LEAD*");
                sv.pageText[0] = 0;
                snprintf(sv.title, sizeof(sv.title), "%s", page == kPagePatch ? "Patch" : (page == kPageMain ? "Engine" : model.page[page].title));
                sv.panelInfo[0][0] = 0;
                if (l.style == kStyleGraphList) snprintf(sv.panelInfo[0], 20, "LP 24");
                for (int p = 0; p < l.panelCount; p++) sv.graph[p].shape = (float)((page * 3 + p) % 4);
                char path[256];
                snprintf(path, sizeof(path), "%s/%d_%s_%d.svg", svgDir, page + 1, kPageNames[page], vi + 1);
                g_svg = fopen(path, "w");
                if (g_svg)
                {
                    fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
                    words = 0; sc.update();
                    fprintf(g_svg, "</svg>\n");
                    fclose(g_svg); g_svg = nullptr;
                }
            }
            g_minY = 27;
        }
    }

    // the pictures stay inside their boxes
    {
        SynthGraph g{}; g.a = 1; g.d = 1; g.s = 1; g.r = 1;
        int16_t xy[2 * 64], hp[8];
        int n = envelopePoints(g, 100, 100, 300, 120, xy, 32, hp);
        for (int i = 0; i < n; i++) if (xy[2 * i] < 100 || xy[2 * i] > 399 || xy[2 * i + 1] < 100 || xy[2 * i + 1] > 219) { printf("  synth: envelope point outside\n"); fails++; break; }
        for (float sh : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 2.5f })
        {
            n = wavePoints(sh, 3, 10, 10, 200, 60, xy, 44);
            if (n > 44 || n < 10) { printf("  synth: wave %.1f has %d points\n", sh, n); fails++; }
            for (int i = 0; i < n; i++) if (xy[2 * i + 1] < 10 || xy[2 * i + 1] > 69) { printf("  synth: wave point outside\n"); fails++; break; }
        }
        for (int t = 0; t < 4; t++)
        {
            g.filterType = (uint8_t)t; g.cutoff = 1; g.reso = 1;
            n = filterPoints(g, 10, 10, 300, 100, xy, 48);
            for (int i = 0; i < n; i++) if (xy[2 * i + 1] < 10 || xy[2 * i + 1] > 109) { printf("  synth: filter point outside\n"); fails++; break; }
        }
    }
    return 0;
}
