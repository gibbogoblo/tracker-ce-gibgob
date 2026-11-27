// GibGob OS PERC editor: every page of every voice type on the synth editor's screen.
// Included by screen_test.cpp (uses its render() and SVG writer). PERC_SVG=<dir>: the pages as pictures.
#include "gibgob/PercEditView.h"

static int percTests(int limit)
{
    using namespace synthui;
    namespace pc = gibgob::perc;
    static SynthModel model;
    static SynthView view;
    static char tabs[8][10];
    strControlProperties prop; prop.y = 0; prop.w = 800; prop.h = 480; prop.data = &view;
    SynthEditControl sc(&prop);
    g_minY = 0;
    const char* const svgDir = getenv("PERC_SVG");
    for (int type = 0; type < pc::kVoiceTypeCount; type++)
    {
        pc::Sound s;
        if (type == pc::kDrum) pc::defaultSound(3, s); // the cowbell
        else pc::initSound(s, type);
        s.value[pc::pLfoDest] = pc::pFilterFreq + 1;
        s.value[pc::pLfoAmount] = 40;
        percui::buildModel(model, type);
        for (int page = 0; page < kPageCount; page++)
        {
            const int views = (page == kPageMain || page == kPagePatch) ? 1 : (model.page[page].viewCount ? model.page[page].viewCount : 1);
            if (page != kPageMain && page != kPagePatch && model.page[page].groupCount == 0)
            { printf("  perc: %s has nothing on page %d\n", pc::kVoiceTypeNames[type], page); fails++; }
            for (int vw = 0; vw < views; vw++)
                for (int shift = 0; shift < 2; shift++)
                for (int look = kLookBlast; look < kLookCount; look++)
                {
                    view = SynthView();
                    percui::Info info;
                    info.track = 2; info.presetName = "Badonka D1"; info.kitName = "Badonka";
                    info.shift = shift; info.tabNames = tabs; info.sourcePerc = type != pc::kSnare; info.look = look;
                    percui::fillView(view, model, page, vw, 1, s, info);
                    // every box has a name; every parameter box its value
                    for (int i = 0; i < view.layout.cellCount; i++)
                    {
                        const CellPos& c = view.layout.cell[i];
                        if (!view.cells[i].label[0] || !strcmp(view.cells[i].label, "?"))
                        { printf("  perc: %s page %d box %d has no name\n", pc::kVoiceTypeNames[type], page, i); fails++; }
                        if (c.id >= 0 && c.id < pc::kParamCount && !view.cells[i].value[0])
                        { printf("  perc: %s page %d '%s' has no value\n", pc::kVoiceTypeNames[type], page, view.cells[i].label); fails++; }
                        if (c.id >= 0 && c.id < pc::kParamCount && !pc::paramUsed(c.id, type))
                        { printf("  perc: %s page %d shows '%s' it has not\n", pc::kVoiceTypeNames[type], page, view.cells[i].label); fails++; }
                    }
                    static const char* const kLookNames[kLookCount] = { "", "blast", "club", "red" };
                    if (view.look != look) { printf("  perc: look %d not set\n", look); fails++; }
                    char nm[64]; snprintf(nm, sizeof(nm), "BONOBONGO %s %s%s %s%s", pc::kVoiceTypeNames[type], kPageNames[page], vw ? " 2" : "", kLookNames[look], shift ? " shift" : "");
                    render(sc, nm, limit, shift);
                    if (svgDir && !shift && vw == 0 && (look == kLookBlast || page <= kPageOsc))
                    {
                        char path[256]; snprintf(path, sizeof(path), "%s/perc_%d_%d_%d.svg", svgDir, type, page, look);
                        g_svg = fopen(path, "w");
                        if (g_svg) { fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n"); words = 0; sc.update(); fprintf(g_svg, "</svg>\n"); fclose(g_svg); g_svg = nullptr; }
                    }
                }
        }
    }
    // the Main page: the voice types are its rows, the one in use lit
    {
        pc::Sound s; pc::initSound(s, pc::kCymbal);
        percui::buildModel(model, pc::kCymbal);
        percui::Info info; info.tabNames = tabs;
        view = SynthView();
        percui::fillView(view, model, kPageMain, 0, 0, s, info);
        int types = 0, lit = -1;
        for (int i = 0; i < view.layout.cellCount; i++)
            if (isEngineItem(view.layout.cell[i].id)) { types++; if (view.cells[i].lit) lit = view.layout.cell[i].id - kItemEngineFirst; }
        if (types != pc::kVoiceTypeCount || lit != pc::kCymbal) { printf("  perc: main page types %d lit %d\n", types, lit); fails++; }
        if (strcmp(tabs[kPageEnv], "LFO 2") != 0) { printf("  perc: the Env tab is '%s'\n", tabs[kPageEnv]); fails++; }
    }
    g_minY = 27;
    return 0;
}
