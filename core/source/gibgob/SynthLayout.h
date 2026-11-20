#ifndef GIBGOB_SYNTHLAYOUT_H_
#define GIBGOB_SYNTHLAYOUT_H_

// GibGob OS synth editor: which parameters go on which of the 8 pages, and
// where each box sits on the screen. No hardware here (host tested).
//
// A synth opts in by having, in its 3x3 editor layout (layout33, not used by
// the Tracker's stock editor), a section whose short name is "GibGob". Its
// parameters are not shown (Main is the engine list). The other sections fill the pages by short
// name: "Osc", "Filter", "Amp", "LFO", "Env", "FX". Each subsection is one
// column on its page; parameter names give the pictures:
//   Attack + Decay + Sustain + Release : an envelope (any page)
//   Osc page, "...shape"               : the waveform
//   Filter page, "...cutoff"           : the filter response
//   LFO page, "...shape" / "...rate"   : the LFO wave
//   FX page, "Delay..." / "Reverb..."  : echoes / a reverb tail

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{
namespace synthui
{

enum Page : uint8_t
{
    kPageMain = 0,
    kPageOsc,
    kPageFilter,
    kPageAmp,
    kPageLfo,
    kPageEnv,
    kPageFx,
    kPagePatch,
    kPageCount
};
extern const char* const kPageNames[kPageCount];
static constexpr const char* kMarkerShortName = "GibGob";

enum Graph : uint8_t
{
    kGraphNone = 0,
    kGraphOsc,
    kGraphFilter,
    kGraphEnv,
    kGraphLfo,
    kGraphDelay,
    kGraphReverb,
    kGraphAvatar, // a singer (a "Syllable" parameter): pictures with a mouth that opens
};

enum Role : uint8_t
{
    kRoleNone = 0,
    kRoleAttack,
    kRoleDecay,
    kRoleSustain,
    kRoleRelease,
    kRoleShape,
    kRoleRate,
    kRoleCutoff,
    kRoleReso,
    kRoleType,
    kRoleFeedback,
    kRoleTime,
    kRoleMix,
    kRoleLevel,
    kRoleSyllable,
};
// the role a parameter plays, from its name
Role roleFromName(const char* name, bool isEnum);

// a sample slot of the synth (kItemSampleFirst + slot)
static constexpr int16_t kItemSampleFirst = -300;
static constexpr int kMaxSampleSlots = 4;
inline bool isSampleItem(int id)
{
    return id >= kItemSampleFirst && id < kItemSampleFirst + kMaxSampleSlots;
}

// ---- what is on each page ----
static constexpr int kMaxRows = 16;  // parameters in one column
static constexpr int kMaxGroups = 9; // columns on one page (3 at a time)
static constexpr int kMaxMain = 12;
static constexpr int kGroupsPerView = 3;
static constexpr int kMaxViews = 3;

struct Group
{
    char name[16];
    uint8_t graph;
    uint8_t count;
    int16_t id[kMaxRows];
    uint8_t role[kMaxRows];
    bool isEnum[kMaxRows];
    // the envelope: rows of attack, decay, sustain, release (-1: not one)
    int8_t adsr[4];

    bool isEnvelope() const { return adsr[0] >= 0; }
    int find(Role r) const
    {
        for (int i = 0; i < count; i++)
            if (role[i] == r)
                return i;
        return -1;
    }
};

struct PageModel
{
    char title[20]; // the section's name ("Oscillators")
    uint8_t groupCount;
    Group groups[kMaxGroups];
    uint8_t viewCount; // sets of up to 3 columns (the page button again: the next set)
    uint8_t viewFirst[kMaxViews];
    uint8_t viewLen[kMaxViews];
};

struct SynthModel
{
    bool valid; // the synth has the GibGob layout
    PageModel page[kPageCount];
    // Main page (set by the screen, not from the synth): the engine list
    uint8_t engineCount;
    uint8_t engineFirst; // first row shown
};

void clearModel(SynthModel& m);
// adds a group (name, parameter ids, their names / enum flags) to a page,
// splitting it when it has more rows than a column holds
void addGroup(SynthModel& m, int page, const char* name, const int16_t* ids, const char* const* names,
              const bool* isEnum, int count);
// works out the sets of columns after all groups are in
void finishModel(SynthModel& m);
int pageFromShortName(const char* shortName);
// a short name for the coloured boxes: "Resonance" -> "Rsnnc" (out: chars + 1)
void abbreviate(const char* in, char* out, int chars);

// Fills the model from the synth's metadata (polysynth::Patch::Metadata, or
// anything with the same members).
template<class Meta>
bool buildModel(const Meta& meta, SynthModel& out)
{
    clearModel(out);
    const auto& sections = meta.layout33.sections;
    int marker = -1;
    for (int s = 0; s < (int)sections.size(); s++)
        if (sections[s].shortName == kMarkerShortName)
        {
            marker = s;
            break;
        }
    if (marker < 0)
        return false;

    int16_t ids[kMaxRows * 4];
    const char* names[kMaxRows * 4];
    bool en[kMaxRows * 4];
    auto collect = [&](const auto& elements, int& n)
    {
        for (const auto& e : elements)
        {
            if (n >= kMaxRows * 4 || e.id < 0)
                continue;
            if ((int)e.type == 2)
            {
                // a sample slot: a row with the sample's name (+/- picks, Middle browses)
                if (e.id >= (int)meta.sampleSlots.size() || e.id >= kMaxSampleSlots)
                    continue;
                ids[n] = (int16_t)(kItemSampleFirst + e.id);
                names[n] = meta.sampleSlots[e.id].displayName.c_str();
                en[n] = true;
                n++;
                continue;
            }
            if ((int)e.type != 1 || e.id >= (int)meta.params.size())
                continue;
            ids[n] = (int16_t)e.id;
            names[n] = meta.params[e.id].displayName.c_str();
            en[n] = meta.params[e.id].isEnum;
            n++;
        }
    };

    for (int s = 0; s < (int)sections.size(); s++)
    {
        const auto& sec = sections[s];
        const int page = (s == marker) ? (int)kPageMain : pageFromShortName(sec.shortName.c_str());
        if (page < 0 || out.page[page].groupCount > 0)
            continue;
        snprintf(out.page[page].title, sizeof(out.page[page].title), "%s", page == kPageMain ? "Overview" : sec.name.c_str());
        if (page == kPageMain)
        {
            int n = 0;
            collect(sec.elements, n);
            for (const auto& sub : sec.subsections)
                collect(sub.elements, n);
            if (n > kMaxMain)
                n = kMaxMain;
            addGroup(out, kPageMain, "Main", ids, names, en, n);
            continue;
        }
        if (!sec.elements.empty())
        {
            int n = 0;
            collect(sec.elements, n);
            addGroup(out, page, sec.name.c_str(), ids, names, en, n);
        }
        for (const auto& sub : sec.subsections)
        {
            int n = 0;
            collect(sub.elements, n);
            addGroup(out, page, sub.name.empty() ? sec.name.c_str() : sub.name.c_str(), ids, names, en, n);
        }
    }
    finishModel(out);
    out.valid = true;
    return true;
}

// ---- where things go on the screen ----
enum Style : uint8_t
{
    kStyleMain = 0, // the engine list | the track's sound settings
    kStyleGraphList, // one column with a picture: big picture left, list right
    kStyleEnvList,   // an envelope + a list: big envelope left, A D S R under it
    kStyleColumns,   // up to 3 columns side by side
    kStylePatch,     // load / save | init / Go Ape
    kStyleSinger,    // a singer: the syllables big on the left, settings top right, the picture bottom right
};

enum CellKind : uint8_t
{
    kCellRow = 0, // name left, value right
    kCellGrid,    // name, value, a bar of lights (Main page)
    kCellChip,    // A / D / S / R
    kCellAction,  // Patch page action
};

// special "parameters" of the Patch page
enum PatchItem : int16_t
{
    kItemLoad = -100,
    kItemSave,
    kItemSaveAs,
    kItemInit,
    kItemGoApe,
    kItemApeAmount,
    kItemEngine,
    kItemVoices,
    kItemGain,
    kItemPan,
    kItemReverb,
    kItemDelay,
};
// Main page: the engines on the card, one row each (kItemEngineFirst + its index)
static constexpr int16_t kItemEngineFirst = -200;
static constexpr int kMaxEngines = 64;
static constexpr int kEngineRows = 7; // rows shown at once (the list scrolls)
inline bool isEngineItem(int id)
{
    return id >= kItemEngineFirst && id < kItemEngineFirst + kMaxEngines;
}
inline bool isPatchItem(int id)
{
    return (id >= kItemLoad && id <= kItemDelay) || isEngineItem(id) || isSampleItem(id);
}

struct CellPos
{
    int16_t x, y, w, h;
    uint8_t kind;
    uint8_t panel;
    int16_t id; // parameter id, or a PatchItem
    uint8_t role;
    bool isEnum;
};

struct PanelPos
{
    int16_t x, y, w, h;     // the panel
    int16_t gx, gy, gw, gh; // its picture (gw = 0: none)
    uint8_t graph;
    int8_t group; // model group (-1: none)
};

static constexpr int kMaxCells = 40;
static constexpr int kMaxPanels = 4;

struct ViewLayout
{
    uint8_t style;
    uint8_t page;
    uint8_t view;
    uint8_t panelCount;
    PanelPos panel[kMaxPanels];
    uint8_t cellCount;
    CellPos cell[kMaxCells];
    int8_t quick[4]; // the cells the 4 coloured buttons jump to (-1: none)
};

// screen areas
static constexpr int kContentTop = 72;
static constexpr int kContentBottom = 420;
static constexpr int kTabsY = 430;
static constexpr int kTabsH = 44;
static constexpr int kQuickY = 34;
static constexpr int kQuickH = 28;
static constexpr int kQuickW = 112;
static constexpr int kQuickX0 = 334; // first quick box; then every 116
// the singer picture (kStyleSinger): up to this size, in the bottom right corner
static constexpr int kSingerW = 200;
static constexpr int kSingerH = 250;
static constexpr int kSingerRight = 790;
static constexpr int kSingerBottom = 425;

void layoutPage(const SynthModel& m, int page, int view, ViewLayout& out);
// the cell the D-pad moves to from `from` (dx, dy = -1, 0, 1); `from` if none
int navigate(const ViewLayout& l, int from, int dx, int dy);
// the cell showing parameter `id` (-1: none)
int cellOf(const ViewLayout& l, int id);

} // namespace synthui
} // namespace gibgob

#endif
