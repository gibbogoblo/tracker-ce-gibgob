// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS PERC editor pages (PercEditView.h).
#include "gibgob/PercEditView.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{
namespace percui
{

using namespace synthui;
namespace pc = gibgob::perc;

const char* const kTools[8] = { "INIT", "GO APE", "LOOK", "", "COMPARE", "UNDO", "", "BACK" };

namespace
{
const char* const kTypeUpper[pc::kVoiceTypeCount] = { "DRUM", "SNARE", "CYMBAL", "HIHAT" };

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void upper(char* out, int len, const char* in)
{
    int i = 0;
    for (; in && in[i] && i < len - 1; i++)
        out[i] = (char)toupper((unsigned char)in[i]);
    out[i] = 0;
}

bool isParam(int id)
{
    return id >= 0 && id < pc::kParamCount;
}

bool isEnumParam(int p)
{
    return pc::paramInfo(p).max < 16;
}

void addGroupOf(SynthModel& m, int voiceType, int page, const char* name, const int* params, int count)
{
    int16_t ids[kMaxRows];
    const char* names[kMaxRows];
    bool en[kMaxRows];
    int n = 0;
    for (int i = 0; i < count && n < kMaxRows; i++)
    {
        if (!pc::paramUsed(params[i], voiceType))
            continue;
        ids[n] = (int16_t)params[i];
        names[n] = pc::paramInfo(params[i]).name;
        en[n] = isEnumParam(params[i]);
        n++;
    }
    if (n > 0)
        addGroup(m, page, name, ids, names, en, n);
}

void paramCell(int p, const pc::Sound& s, SynthCellText& t)
{
    snprintf(t.label, sizeof(t.label), "%s", pc::paramInfo(p).name);
    pc::paramText(p, s.value[p], t.value, sizeof(t.value));
    const int max = pc::paramInfo(p).max;
    t.fill = (int8_t)(max > 0 ? clampi(s.value[p] * 100 / max, 0, 100) : 0);
    t.bipolar = p == pc::pFine || p == pc::pPan;
}

void itemText(int item, const pc::Sound& s, const Info& info, SynthCellText& t)
{
    t.fill = -1;
    t.label[0] = t.value[0] = 0;
    if (isEngineItem(item))
    {
        const int k = item - kItemEngineFirst;
        snprintf(t.label, sizeof(t.label), "%s", k < pc::kVoiceTypeCount ? pc::kVoiceTypeNames[k] : "");
        t.lit = k == s.type;
        return;
    }
    const int p = mainParam(item, s.type);
    if (p >= 0)
    {
        paramCell(p, s, t);
        return;
    }
    switch (item)
    {
        case kItemVoices:
            snprintf(t.label, sizeof(t.label), "Source");
            snprintf(t.value, sizeof(t.value), "%s", info.sourcePerc ? "BONOBONGO" : "Sample");
            break;
        case kItemDelay:
            snprintf(t.label, sizeof(t.label), "Hat");
            snprintf(t.value, sizeof(t.value), "%s", s.hatOpen ? "Open" : "Closed");
            break;
        case kItemLoad:
            snprintf(t.label, sizeof(t.label), "Preset");
            snprintf(t.value, sizeof(t.value), "%.15s", info.presetName ? info.presetName : "");
            break;
        case kItemSave:
            snprintf(t.label, sizeof(t.label), "Kit: all tracks");
            snprintf(t.value, sizeof(t.value), "%.15s", info.kitName ? info.kitName : "");
            break;
        case kItemSaveAs: snprintf(t.label, sizeof(t.label), "Revert"); break;
        case kItemInit: snprintf(t.label, sizeof(t.label), "Init %s", pc::kVoiceTypeNames[s.type % pc::kVoiceTypeCount]); break;
        case kItemGoApe: snprintf(t.label, sizeof(t.label), "Go Ape"); break;
        case kItemApeAmount:
            snprintf(t.label, sizeof(t.label), "Ape amount");
            snprintf(t.value, sizeof(t.value), "%d%%", info.apeAmount);
            t.fill = (int8_t)clampi(info.apeAmount, 0, 100);
            break;
        default: break;
    }
}

// the pictures: the oscillator's wave, the filter, the LFO's wave
void fillGraph(const Group& g, const pc::Sound& s, SynthGraph& out)
{
    const uint8_t* v = s.value;
    if (g.graph == kGraphOsc)
    {
        static const float kShape[6] = { 0, 1, 2, 3, 5, 3 }; // sine tri saw square, noise: S&H, pwm: square
        const int r = g.find(kRoleShape);
        out.shape = r >= 0 ? kShape[clampi(v[g.id[r]], 0, 5)] : 0;
        out.cycles = 2;
    }
    else if (g.graph == kGraphFilter)
    {
        static const uint8_t kType[8] = { 0, 1, 2, 2, 2, 2, 0, 0 }; // LP HP BP UBP notch peak LP2 off
        out.cutoff = v[pc::pFilterFreq] / 127.f;
        out.reso = v[pc::pFilterReso] / 127.f;
        out.filterType = kType[clampi(v[pc::pFilterType], 0, 7)];
    }
    else if (g.graph == kGraphLfo)
    {
        static const float kShape[8] = { 0, 1, 2, 4, 3, 5, 2, 4 }; // sine tri up down square random exp up/down
        const bool second = g.id[0] == pc::pLfo2Freq;
        out.shape = kShape[clampi(v[second ? pc::pLfo2Wave : pc::pLfoWave], 0, 7)];
        out.cycles = 1 + v[second ? pc::pLfo2Freq : pc::pLfoFreq] / 127.f * 4;
    }
}
} // namespace

void buildModel(SynthModel& m, int voiceType)
{
    clearModel(m);
    snprintf(m.theme, sizeof(m.theme), "BONOBONGO");
    snprintf(m.page[kPageMain].title, sizeof(m.page[kPageMain].title), "Voice");
    {
        static const int osc[] = { pc::pWave, pc::pCoarse, pc::pFine, pc::pPwm };
        static const int fm[] = { pc::pFmAmount, pc::pFmFreq, pc::pModWave, pc::pMixMode };
        static const int noise[] = { pc::pNoiseFreq, pc::pNoiseMix };
        static const int metal[] = { pc::pWave2, pc::pWave3, pc::pModF1, pc::pModF2, pc::pModGain1, pc::pModGain2 };
        snprintf(m.page[kPageOsc].title, sizeof(m.page[kPageOsc].title), "Oscillators");
        addGroupOf(m, voiceType, kPageOsc, "Oscillator", osc, 4);
        addGroupOf(m, voiceType, kPageOsc, "FM", fm, 4);
        addGroupOf(m, voiceType, kPageOsc, "Noise", noise, 2);
        addGroupOf(m, voiceType, kPageOsc, "Modulators", metal, 6);
    }
    {
        static const int flt[] = { pc::pFilterFreq, pc::pFilterReso, pc::pFilterType, pc::pFilterDrive };
        snprintf(m.page[kPageFilter].title, sizeof(m.page[kPageFilter].title), "Filter");
        addGroupOf(m, voiceType, kPageFilter, "Filter", flt, 4);
    }
    {
        static const int amp[] = { pc::pAttack, pc::pDecay, pc::pDecayOpen, pc::pSlope, pc::pRepeat, pc::pVolume, pc::pVelVolume };
        static const int pitch[] = { pc::pPitchDecay, pc::pPitchSlope, pc::pPitchAmount };
        snprintf(m.page[kPageAmp].title, sizeof(m.page[kPageAmp].title), "Amp");
        addGroupOf(m, voiceType, kPageAmp, "Amp", amp, 7);
        addGroupOf(m, voiceType, kPageAmp, "Pitch env", pitch, 3);
    }
    {
        static const int lfo[] = { pc::pLfoFreq, pc::pLfoSync, pc::pLfoWave, pc::pLfoAmount, pc::pLfoDest, pc::pLfoRetrig, pc::pLfoOffset };
        static const int lfo2[] = { pc::pLfo2Freq, pc::pLfo2Sync, pc::pLfo2Wave, pc::pLfo2Amount, pc::pLfo2Dest, pc::pLfo2Retrig, pc::pLfo2Offset };
        snprintf(m.page[kPageLfo].title, sizeof(m.page[kPageLfo].title), "LFO 1");
        addGroupOf(m, voiceType, kPageLfo, "LFO 1", lfo, 7);
        snprintf(m.page[kPageEnv].title, sizeof(m.page[kPageEnv].title), "LFO 2");
        addGroupOf(m, voiceType, kPageEnv, "LFO 2", lfo2, 7);
    }
    {
        static const int click[] = { pc::pTransWave, pc::pTransVol, pc::pTransFreq };
        static const int drive[] = { pc::pDrive, pc::pSampleRate };
        static const int vel[] = { pc::pVelAmount, pc::pVelDest };
        snprintf(m.page[kPageFx].title, sizeof(m.page[kPageFx].title), "FX");
        addGroupOf(m, voiceType, kPageFx, "Click", click, 3);
        addGroupOf(m, voiceType, kPageFx, "Drive", drive, 2);
        addGroupOf(m, voiceType, kPageFx, "Velocity", vel, 2);
    }
    snprintf(m.page[kPagePatch].title, sizeof(m.page[kPagePatch].title), "Presets");
    finishModel(m);
    m.valid = true;
    m.engineCount = pc::kVoiceTypeCount; // Main: the voice types, one row each
    m.engineFirst = 0;
}

int mainParam(int item, int voiceType)
{
    switch (item)
    {
        case kItemGain: return pc::pVolume;
        case kItemPan: return pc::pPan;
        case kItemReverb: return pc::pCoarse;
        case kItemDelay: return voiceType == pc::kHihat ? -1 : pc::pDecay; // the hat: Open / Closed there
        default: return -1;
    }
}

void fillView(SynthView& v, const SynthModel& m, int page, int view, int cursor, const pc::Sound& s, const Info& info)
{
    const PageModel& pm = m.page[page];
    const int views = (page == kPageMain || page == kPagePatch) ? 1 : (pm.viewCount ? pm.viewCount : 1);
    view = clampi(view, 0, views - 1);
    layoutPage(m, page, view, v.layout);
    const ViewLayout& l = v.layout;
    v.cursor = l.cellCount ? clampi(cursor, 0, l.cellCount - 1) : 0;
    v.shift = info.shift;
    v.editing = true;
    v.cute = v.jungle = v.angelic = false;
    v.look = (uint8_t)(info.look >= kLookBlast && info.look < kLookCount ? info.look : kLookBlast);

    snprintf(v.synthName, sizeof(v.synthName), "BONOBONGO %s", kTypeUpper[s.type % pc::kVoiceTypeCount]);
    {
        char name[16];
        upper(name, sizeof(name), s.name);
        snprintf(v.headerInfo, sizeof(v.headerInfo), "TRK %d // %.13s%s", info.track + 1, name, info.sourcePerc ? "" : " (SAMPLE)");
    }
    if (views > 1)
        snprintf(v.pageText, sizeof(v.pageText), "%d/%d", view + 1, views);
    else
        v.pageText[0] = 0;
    if (info.comparing)
        snprintf(v.title, sizeof(v.title), "COMPARE: as opened");
    else
        snprintf(v.title, sizeof(v.title), "%s", pm.title[0] ? pm.title : kPageNames[page]);

    for (int p = 0; p < l.panelCount; p++)
    {
        v.panelTitle[p][0] = 0;
        v.panelInfo[p][0] = 0;
        memset(&v.graph[p], 0, sizeof(v.graph[p]));
        const PanelPos& pp = l.panel[p];
        if (pp.group >= 0 && pp.group < pm.groupCount)
        {
            const Group& g = pm.groups[pp.group];
            upper(v.panelTitle[p], sizeof(v.panelTitle[p]), g.name[0] ? g.name : pm.title);
            fillGraph(g, s, v.graph[p]);
            if (g.graph == kGraphFilter)
                pc::paramText(pc::pFilterType, s.value[pc::pFilterType], v.panelInfo[p], sizeof(v.panelInfo[p]));
        }
    }
    v.hint[0] = 0;
    v.pathText[0] = 0;
    v.warn = false;
    if (l.style == kStyleMain)
    {
        snprintf(v.panelTitle[0], sizeof(v.panelTitle[0]), "VOICE TYPE");
        snprintf(v.panelTitle[1], sizeof(v.panelTitle[1]), "TRACK SOUND");
        snprintf(v.pathText, sizeof(v.pathText), "Middle on a type: change to it");
        if (info.sourcePerc)
            snprintf(v.hint, sizeof(v.hint), "Pads, Middle: play  Shift+8: back");
        else
        {
            v.warn = true;
            snprintf(v.hint, sizeof(v.hint), "Source Sample: plays its sample");
        }
    }
    else if (l.style == kStylePatch)
    {
        snprintf(v.panelTitle[0], sizeof(v.panelTitle[0]), "PRESETS / KITS");
        snprintf(v.panelTitle[1], sizeof(v.panelTitle[1]), "INIT / GO APE");
        snprintf(v.hint, sizeof(v.hint), "Kit: +/- pick, Middle: load it");
    }

    for (int i = 0; i < l.cellCount; i++)
    {
        const CellPos& c = l.cell[i];
        SynthCellText& t = v.cells[i];
        t.fill = -1;
        t.bipolar = false;
        t.lit = false;
        if (isPatchItem(c.id))
        {
            itemText(c.id, s, info, t);
            continue;
        }
        if (!isParam(c.id))
        {
            snprintf(t.label, sizeof(t.label), "?");
            t.value[0] = 0;
            continue;
        }
        paramCell(c.id, s, t);
        // a destination in use: lit
        t.lit = (c.id == pc::pLfoDest || c.id == pc::pLfo2Dest || c.id == pc::pVelDest) && s.value[c.id] != 0;
    }

    for (int k = 0; k < 4; k++)
    {
        const int c = l.quick[k];
        v.quickActive[k] = (c >= 0 && c == v.cursor);
        if (c < 0 || c >= l.cellCount)
        {
            v.quick[k].label[0] = v.quick[k].value[0] = 0;
            continue;
        }
        abbreviate(v.cells[c].label, v.quick[k].label, (int)sizeof(v.quick[k].label) - 1);
        if (l.cell[c].kind == kCellAction)
            v.quick[k].value[0] = 0;
        else
            snprintf(v.quick[k].value, sizeof(v.quick[k].value), "%.9s", v.cells[c].value);
    }

    // the tabs: the page names (LFO 2 on Env's button), or with Shift the tools
    for (int i = 0; i < 8; i++)
    {
        const char* t = m.page[i].title;
        const bool own = i > kPageMain && i < kPagePatch && t[0] && strlen(t) <= 6;
        upper(info.tabNames[i], sizeof(info.tabNames[i]), own ? t : kPageNames[i]);
        v.tabText[i] = v.shift ? kTools[i] : info.tabNames[i];
        v.tabOn[i] = v.shift ? (i == 4 && info.comparing) : (i == page);
    }
    snprintf(v.message, sizeof(v.message), "%s", info.message ? info.message : "");
}

} // namespace percui
} // namespace gibgob
