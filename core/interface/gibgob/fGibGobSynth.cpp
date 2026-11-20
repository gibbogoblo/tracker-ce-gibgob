// GibGob OS - synth editor, Y2K style (shortcuts: gibgobHelp.h, Shift + Menu)

#include "gibgob/gibgobSynth.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "display.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobPages.h"
#include "gibgob/gibgobUi.h"
#include "instrumentEditor/instrumentEditor.h"
#include "mtStructs.h"

#if GUI_HANDLE_SYNTH
#include "PolySynth/Patch.h"
#include "PolySynth/PsynStatus.h"
#include "elapsedMillis.h"
#include "fileBrowser/GlobalFileBrowser.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "util_string.h"
#endif

#include <ctype.h>
#include <math.h>
#include <memory>
#include <new>
#include <string>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

cGibGobSynth gibgobSynth;
static cGibGobSynth* GS = &gibgobSynth;

using namespace gibgob;
using namespace gibgob::synthui;

#if GUI_HANDLE_SYNTH

static cInstrumentEditor* IE = &instrumentEditor;

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functShift(uint8_t state);
static void functMiddle(uint8_t state);
static void functDelete(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functColour(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functParams(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

namespace
{
constexpr int kMaxParams = 256;
constexpr int kUndoSize = 64;
constexpr int kClipSize = kMaxRows * kMaxGroups;
constexpr int kPreviewPad = 12;

struct UndoEntry
{
    int16_t id;
    uint16_t step;
    float value;
};

// everything big lives in the external memory (the fast memory is full)
struct State
{
    SynthModel model;
    SynthView view;
    // the patch as it was opened / loaded / saved (for Compare), and the
    // edited one while comparing
    float saved[kMaxParams];
    float edited[kMaxParams];
    int paramCount;
    bool comparing;
    // undo: one step can change many parameters
    UndoEntry undo[kUndoSize];
    int undoCount;
    uint16_t undoStep;
    // Copy pg / Paste pg
    int clipEngine;
    int clipPage;
    int clipCount;
    int16_t clipId[kClipSize];
    float clipValue[kClipSize];
    // where the snapshot was taken (a different patch: a new snapshot)
    int snapPart;
    int snapEngine;
    char snapName[32];
};
__NOINIT(EXTERNAL_RAM) alignas(8) uint8_t s_stateMemory[sizeof(State)];
State* s = nullptr;

int s_view[kPageCount];     // which set of columns each page shows
int s_cursor[kPageCount];   // the cursor of each page
int s_quickBack[kPageCount]; // where a colour button jumped from (-1: none)
int s_apeAmount = 30;       // percent
int s_pendingEngine = -1;   // Main page: the engine picked, not yet applied
int s_engineFirst = 0;      // Main page: the first engine row shown
bool s_latched = false;
bool s_middleDown = false;
bool s_noteOn = false;
int s_singKey = -1;         // a singer: the top button (Shift) singing now
elapsedMillis s_messageAge;
char s_message[64];
char s_tabNames[8][10];

// the singer pictures (LILITRON) on the card, decoded one after the other
// into the display's picture memory (shared with the image viewer and
// Gibbon vision, so they load again each time this screen opens)
const char* const kAvatarFiles[SynthAvatar::kPics] = { "GibGob/Lilitron/singer", "GibGob/Lilitron/singer_sing",
                                                       "GibGob/Lilitron/duo", "GibGob/Lilitron/duo_sing" };
constexpr uint32_t kAvatarAddress = 350000; // 4 x 100 KB, up to 750000 (the project cover is at 670000, loaded again on its screen)
constexpr uint32_t kAvatarBytes = kSingerW * kSingerH * 2;
int s_avatarNext = 0;
bool s_avatarBusy = false;
uint32_t s_rand = 0x1234567;

const char* const kTools[8] = { "INIT PG", "APE PG", "COPY PG", "PASTE PG", "COMPARE", "UNDO", "LATCH", "SAVE" };

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float random01()
{
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return (s_rand & 0xFFFFFF) / 16777216.0f;
}

Synth* gsynth()
{
    return IE->getSynth();
}

polysynth::Patch& patch()
{
    return gsynth()->getPatch(GS->part);
}

const polysynth::Patch::Metadata& meta()
{
    return patch().getMetadata();
}

SynthPartSettings& settings()
{
    return mtProject.values.synthPart[GS->part];
}

bool validParam(int id)
{
    return id >= 0 && id < (int)meta().params.size() && id < kMaxParams;
}

float minOf(int id)
{
    return (float)meta().params[id].minValue;
}

float maxOf(int id)
{
    return (float)meta().params[id].maxValue;
}

float defaultOf(int id)
{
    return meta().params[id].defaultValue.value_or(minOf(id));
}

float rawValue(int id)
{
    return patch().getParamValue(id).value_or(defaultOf(id));
}

float mappedValue(int id)
{
    return clampf(patch().getParamValueEditMapped(id).value_or(0.0f), 0.0f, 1.0f);
}

void markDirty()
{
    gsynth()->setPatchDirty(GS->part);
}

void setRaw(int id, float v)
{
    DISABLE_AUDIO_INTERRUPTS();
    patch().setParamValue(id, v, polysynth::Patch::ParamChangeSource::kKnob);
    ENABLE_AUDIO_INTERRUPTS();
}

void setMapped(int id, float v)
{
    DISABLE_AUDIO_INTERRUPTS();
    patch().setParamValueEditMapped(id, clampf(v, 0.0f, 1.0f), polysynth::Patch::ParamChangeSource::kKnob);
    ENABLE_AUDIO_INTERRUPTS();
}

void formatValue(int id, float v, char* out, int len)
{
    const auto& p = meta().params[id];
    if (p.valueFormatter)
        polystrncpy(out, p.valueFormatter(patch(), v).c_str(), len);
    else if (p.isEnum)
        snprintf(out, len, "%d", (int)lroundf(v));
    else
        snprintf(out, len, "%.2f", (double)v);
}

int engineIdx()
{
    return settings().synthType;
}

// ---- undo ----
void beginUndoStep()
{
    s->undoStep++;
}

void remember(int id)
{
    if (!validParam(id))
        return;
    if (s->undoCount == kUndoSize)
    {
        memmove(&s->undo[0], &s->undo[1], sizeof(UndoEntry) * (kUndoSize - 1));
        s->undoCount--;
    }
    s->undo[s->undoCount++] = UndoEntry{ (int16_t)id, s->undoStep, rawValue(id) };
}

void clearUndo()
{
    s->undoCount = 0;
}

// ---- Compare: the patch as it was ----
void takeSnapshot()
{
    s->paramCount = (int)meta().params.size();
    if (s->paramCount > kMaxParams)
        s->paramCount = kMaxParams;
    for (int i = 0; i < s->paramCount; i++)
        s->saved[i] = rawValue(i);
    s->comparing = false;
    s->snapPart = GS->part;
    s->snapEngine = engineIdx();
    polystrncpy(s->snapName, settings().patchName, sizeof(s->snapName));
}

void applyValues(const float* values)
{
    DISABLE_AUDIO_INTERRUPTS();
    patch().startBatchUpdate();
    for (int i = 0; i < s->paramCount; i++)
        patch().setParamValue(i, values[i], polysynth::Patch::ParamChangeSource::kKnob);
    patch().finishBatchUpdate();
    ENABLE_AUDIO_INTERRUPTS();
}

// ---- every parameter of a page (all its columns) ----
int pageParams(int pg, int16_t* ids, int max)
{
    int n = 0;
    const PageModel& p = s->model.page[pg];
    for (int g = 0; g < p.groupCount; g++)
        for (int i = 0; i < p.groups[g].count && n < max; i++)
            ids[n++] = p.groups[g].id[i];
    return n;
}

// the synth sings (a "Syllable" parameter): the pink look, Shift + top sings
bool modelSings()
{
    for (int pg = 0; pg < kPageCount; pg++)
        for (int g = 0; g < s->model.page[pg].groupCount; g++)
            if (s->model.page[pg].groups[g].find(kRoleSyllable) >= 0)
                return true;
    return false;
}

int paramNamed(const char* a, const char* b = nullptr)
{
    for (int i = 0; i < (int)meta().params.size(); i++)
    {
        const std::string& n = meta().params[i].displayName;
        if (n == a || (b != nullptr && n == b))
            return i;
    }
    return -1;
}

bool isVolume(int id)
{
    char n[32];
    polystrncpy(n, meta().params[id].displayName.c_str(), sizeof(n));
    for (char* c = n; *c; c++)
        *c = (char)tolower((unsigned char)*c);
    return strstr(n, "volume") != nullptr;
}

// randomises one parameter by `amount` (0..1) of its range; the volume
// stays (a silent patch is no fun)
void apeParam(int id, float amount)
{
    if (!validParam(id) || isVolume(id))
        return;
    const auto& p = meta().params[id];
    remember(id);
    if (p.isEnum)
    {
        if (random01() < amount)
        {
            const int lo = (int)lroundf(minOf(id)), hi = (int)lroundf(maxOf(id));
            setRaw(id, (float)(lo + (int)(random01() * (hi - lo + 1) * 0.9999f)));
        }
        return;
    }
    setMapped(id, mappedValue(id) + (random01() * 2 - 1) * amount);
}

// text for the coloured boxes and the headers
void upper(char* out, int len, const char* in)
{
    polystrncpy(out, in ? in : "", len);
    for (char* c = out; *c; c++)
        *c = (char)toupper((unsigned char)*c);
}

// what the picture shows, from the parameters' names and values
float oscShape(int id)
{
    if (!validParam(id))
        return 2.0f;
    if (meta().params[id].isEnum)
    {
        char t[24];
        formatValue(id, rawValue(id), t, sizeof(t));
        for (char* c = t; *c; c++)
            *c = (char)tolower((unsigned char)*c);
        if (strstr(t, "sine") || strstr(t, "sin"))
            return 0;
        if (strstr(t, "tri"))
            return 1;
        if (strstr(t, "sq") || strstr(t, "pulse"))
            return 3;
        if (strstr(t, "down"))
            return 4;
        if (strstr(t, "s&h") || strstr(t, "sample"))
            return 5;
        if (strstr(t, "rnd") || strstr(t, "random") || strstr(t, "noise"))
            return 6;
        return 2; // saw
    }
    const float lo = minOf(id), hi = maxOf(id);
    return hi > lo ? (rawValue(id) - lo) / (hi - lo) * 3.0f : 2.0f;
}

int filterType(int id)
{
    if (!validParam(id))
        return 0;
    char t[24];
    formatValue(id, rawValue(id), t, sizeof(t));
    for (char* c = t; *c; c++)
        *c = (char)tolower((unsigned char)*c);
    if (strstr(t, "comb"))
        return 3;
    if (strstr(t, "band") || strstr(t, "bp"))
        return 2;
    if (strstr(t, "high") || strstr(t, "hp"))
        return 1;
    return 0;
}

void fillGraph(const Group& g, SynthGraph& out, int cursorRole)
{
    auto val = [&](Role r, float dflt, bool mapped)
    {
        const int row = g.find(r);
        if (row < 0 || !validParam(g.id[row]))
            return dflt;
        if (mapped)
            return mappedValue(g.id[row]);
        const int id = g.id[row];
        const float lo = minOf(id), hi = maxOf(id);
        return hi > lo ? (rawValue(id) - lo) / (hi - lo) : dflt;
    };
    out.a = val(kRoleAttack, 0.1f, true);
    out.d = val(kRoleDecay, 0.4f, true);
    out.s = val(kRoleSustain, 0.7f, false);
    out.r = val(kRoleRelease, 0.4f, true);
    out.handle = (int8_t)(cursorRole >= kRoleAttack && cursorRole <= kRoleRelease ? cursorRole - kRoleAttack : -1);
    const int shapeRow = g.find(kRoleShape);
    out.shape = shapeRow >= 0 ? oscShape(g.id[shapeRow]) : 0.0f;
    out.cycles = 1.5f + val(kRoleRate, 0.3f, true) * 4.5f;
    out.cutoff = val(kRoleCutoff, 0.5f, true);
    out.reso = val(kRoleReso, 0.2f, false);
    const int typeRow = g.find(kRoleType);
    out.filterType = (uint8_t)(typeRow >= 0 ? filterType(g.id[typeRow]) : 0);
    out.amount = g.graph == kGraphDelay ? val(kRoleFeedback, 0.4f, false) : val(kRoleDecay, 0.5f, false);
}

void patchItemText(int item, char* label, int llen, char* value, int vlen, int8_t& fill)
{
    SynthPartSettings& sp = settings();
    fill = -1;
    value[0] = 0;
    switch (item)
    {
        case kItemLoad:
            snprintf(label, llen, "Load patch");
            polystrncpy(value, sp.patchName, vlen);
            break;
        case kItemSave:
            snprintf(label, llen, "Save");
            snprintf(value, vlen, "%s", gsynth()->isPatchDirty(GS->part) ? "unsaved edits" : "saved");
            break;
        case kItemSaveAs: snprintf(label, llen, "Save as"); snprintf(value, vlen, "new name"); break;
        case kItemInit: snprintf(label, llen, "Init patch"); snprintf(value, vlen, "clean start"); break;
        case kItemGoApe: snprintf(label, llen, "GO APE!"); snprintf(value, vlen, "shake it all"); break;
        case kItemApeAmount:
            snprintf(label, llen, "Ape amount");
            snprintf(value, vlen, "%d%%", s_apeAmount);
            break;
        case kItemEngine:
        {
            snprintf(label, llen, "Engine");
            const int idx = s_pendingEngine >= 0 ? s_pendingEngine : engineIdx();
            char name[20];
            polystrncpy(name, gsynth()->getInstrumentTypeName(idx).c_str(), sizeof(name));
            if (s_pendingEngine >= 0 && s_pendingEngine != engineIdx())
                snprintf(value, vlen, ">%.9s?", name);
            else
                polystrncpy(value, name, vlen);
            break;
        }
        case kItemVoices:
        {
            snprintf(label, llen, "Voices");
            const int free = gsynth()->getMaxVoiceCount(GS->part) - (int)sp.voiceCount;
            snprintf(value, vlen, "%d  (+%d free)", (int)sp.voiceCount, free > 0 ? free : 0);
            break;
        }
        case kItemGain: snprintf(label, llen, "Gain"); snprintf(value, vlen, "%.1f dB", (double)sp.gainDb); break;
        case kItemPan:
        {
            snprintf(label, llen, "Pan");
            const int p = (int)lroundf(sp.panning);
            if (p == 0)
                snprintf(value, vlen, "C");
            else
                snprintf(value, vlen, "%c%d", p < 0 ? 'L' : 'R', p < 0 ? -p : p);
            break;
        }
        case kItemReverb:
            snprintf(label, llen, "Reverb send");
            snprintf(value, vlen, "%d%%", (int)lroundf(sp.reverbSend));
            break;
        case kItemDelay:
            snprintf(label, llen, "Delay send");
            snprintf(value, vlen, "%d%%", (int)lroundf(sp.delaySend));
            break;
        default:
            label[0] = 0;
            if (isEngineItem(item))
            {
                const int idx = item - kItemEngineFirst;
                polystrncpy(label, gsynth()->getInstrumentTypeName(idx).c_str(), llen);
                if (idx == engineIdx())
                    snprintf(value, vlen, "in use");
                else if (idx == s_pendingEngine)
                    snprintf(value, vlen, "Middle again!");
            }
            break;
    }
}

void openChordPalette()
{
    uint32_t options =
      cGibGobPages::makeOptions(cGibGobPages::kPagePalette, cGibGobPages::kFromInstrumentEditor, soundParametersTrack());
    mtInterface.deactivateModule(GS);
    mtInterface.activateModule(&gibgobPages, options);
}

std::string leafName(std::string name)
{
    if (!name.empty() && (name.front() == '/' || name.size() >= sizeof(SynthPartSettings::patchName)))
    {
        auto slash = name.find_last_of('/');
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
    }
    return name;
}

void previewNotesOff()
{
    gsynth()->triggerTestNote(GS->part, cInstrumentEditor::kSynthPreviewNote1, false);
    gsynth()->triggerTestNote(GS->part, cInstrumentEditor::kSynthPreviewNote2, false);
}

// after a new patch: no undo across it, Compare starts from it
void patchChanged()
{
    clearUndo();
    takeSnapshot();
}
} // namespace

// ---------------------------------------------------------------------------
// which synth parts sing (read by the sequencer in the audio interrupt, so it
// only looks at this table; the patches are checked here, in the UI loop)
namespace
{
volatile bool s_sings[kSynthParts] = {};
const void* s_singsPatch[kSynthParts] = {};
int s_singsType[kSynthParts] = { -1, -1, -1 };

bool singsSyllables(uint8_t instrument)
{
    const int p = instrument - SYNTH_INSTRUMENTS_START_IDX;
    return p >= 0 && p < kSynthParts && s_sings[p];
}
} // namespace

void gibgobSynthPoll()
{
    Synth* sy = IE->getSynth();
    if (sy == nullptr)
        return;
    gibgob::g_singsSyllables = singsSyllables;
    for (int p = 0; p < kSynthParts; p++)
    {
        if (!sy->hasPatch(p))
        {
            s_sings[p] = false;
            s_singsPatch[p] = nullptr;
            continue;
        }
        const polysynth::Patch& patch = sy->getPatch(p);
        const int type = sy->getInstrumentTypeIdx(p);
        // (a new patch can land at the old one's address: the engine counts too)
        if (s_singsPatch[p] == &patch && s_singsType[p] == type)
            continue;
        s_singsPatch[p] = &patch;
        s_singsType[p] = type;
        bool sings = false;
        for (const auto& prm : patch.getMetadata().params)
            if (prm.displayName == "Syllable" && prm.midiCC.has_value() && *prm.midiCC == gibgob::kSyllableCC)
                sings = true;
        s_sings[p] = sings;
    }
}

bool cGibGobSynth::handles(int instrument)
{
    if (!isSynthInstrument(instrument))
        return false;
    const int p = instrument - SYNTH_INSTRUMENTS_START_IDX;
    if (p < 0 || p >= kSynthParts || IE->getSynth() == nullptr)
        return false;
    for (const auto& sec : IE->getSynth()->getPatch(p).getMetadata().layout33.sections)
        if (sec.shortName == kMarkerShortName)
            return true;
    return false;
}

void cGibGobSynth::initDisplayControls()
{
    if (s == nullptr)
    {
        s = new (s_stateMemory) State();
        memset(s->saved, 0, sizeof(s->saved));
        s->snapPart = -1;
        for (int i = 0; i < kPageCount; i++)
            s_quickBack[i] = -1;
    }
    strControlProperties prop;
    prop.x = 0;
    prop.y = 0;
    prop.w = kScreenW;
    prop.h = 480;
    prop.style = controlStyleShow;
    prop.data = &s->view;
    control = display.createControl<SynthEditControl>(&prop);
}

void cGibGobSynth::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;
    screenOpened();
    part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    if (part < 0 || part >= kSynthParts)
        part = 0;
    s_pendingEngine = -1;
    s_message[0] = 0;
    s_rand ^= (uint32_t)s_messageAge * 2654435761u + 1u;

    SynthView& v = s->view;
    v.help.active = false;
    v.help.title = "Synth editor - shortcuts";
    v.help.lines = kHelpSynth;
    v.help.count = (int)(sizeof(kHelpSynth) / sizeof(kHelpSynth[0]));
    // the display's picture memory may hold something else now: load again
    for (int k = 0; k < SynthAvatar::kPics; k++)
        v.avatar.loaded[k] = false;
    v.avatar.singing = false;
    v.avatar.names = kSyllableNames;
    v.avatar.sung = -1;
    s_avatarNext = 0;
    s_avatarBusy = false;

    // another patch than last time: Compare and Undo start over
    if (s->snapPart != part || s->snapEngine != engineIdx() || strncmp(s->snapName, settings().patchName, 32) != 0)
        patchChanged();

    songTimer.hide();
    titleBar->hide();
    setDefaultScreenFunct();
    rebuild();
    display.synchronizeRefresh();
}

void cGibGobSynth::stop()
{
    releaseNotes();
    restoreCompare();
    moduleRefresh = 0;
}

void cGibGobSynth::releaseNotes()
{
    if (s_noteOn)
        IE->playSynthNoteByPad(kPreviewPad, buttonRelease);
    s_noteOn = false;
    s_latched = false;
    s_middleDown = false;
    s_singKey = -1;
}

void cGibGobSynth::restoreCompare()
{
    if (s != nullptr && s->comparing)
    {
        applyValues(s->edited);
        s->comparing = false;
    }
}

void cGibGobSynth::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);
    ip->setButtonObj(interfaceButtonNote, functColour);
    ip->setButtonObj(interfaceButtonInstr, functColour);
    ip->setButtonObj(interfaceButtonFx1, functColour);
    ip->setButtonObj(interfaceButtonFx2, functColour);

    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonParams, functParams);

    ip->setPotObj(interfacePot0, functEncoder, false);
    ip->setPadsGlobal(functPads);
}

namespace
{
bool pageHasAvatar(const ViewLayout& l)
{
    for (int p = 0; p < l.panelCount; p++)
        if (l.panel[p].graph == kGraphAvatar)
            return true;
    return false;
}

// is the singer singing: a note of this synth sounds on a track, or the preview
bool singerSinging(int instrument, int& track)
{
    track = -1;
    const Sequencer::strPattern* p = current();
    for (int t = 0; t < 8; t++)
        if (getSound(p, t) == instrument && isSounding(t))
        {
            track = t;
            return true;
        }
    return false;
}
} // namespace

void cGibGobSynth::update()
{
    if (s_message[0] && s_messageAge > 1400)
    {
        s_message[0] = 0;
        refresh();
    }
    SynthAvatar& av = s->view.avatar;
    if (!pageHasAvatar(s->view.layout))
        return;

    // load the pictures (a missing one keeps its stand-in face)
    if (s_avatarNext < SynthAvatar::kPics)
    {
        const int k = s_avatarNext;
        if (!s_avatarBusy)
        {
            if (display.getImgLoadState() == 0 || display.getImgLoadState() == 3)
            {
                char path[40];
                snprintf(path, sizeof(path), "%s", kAvatarFiles[k]);
                av.address[k] = kAvatarAddress + (uint32_t)k * kAvatarBytes;
                if (display.readImgFromSd(path, av.address[k], kSingerW, kSingerH) == 0)
                    s_avatarBusy = true;
                else
                    s_avatarNext++; // missing, too big or not a baseline JPEG
            }
        }
        else if (display.isImgLoaded())
        {
            av.w[k] = (int16_t)getProps.Width;
            av.h[k] = (int16_t)getProps.Height;
            av.loaded[k] = true;
            s_avatarBusy = false;
            s_avatarNext++;
            refresh();
        }
        else if (display.getImgLoadState() == 0)
        {
            s_avatarBusy = false; // the load failed
            s_avatarNext++;
        }
    }

    // the mouth: open while a note of this synth sounds
    int track = -1;
    const bool singing = s_noteOn || singerSinging(mtProject.values.lastUsedInstrument, track);
    bool cat = false;
    int picked = 0;
    for (int i = 0; i < (int)meta().params.size(); i++)
    {
        if (meta().params[i].displayName == "BAB-MODE" || meta().params[i].displayName == "Cat")
            cat = rawValue(i) > 0.5f;
        if (meta().params[i].displayName == "Syllable")
            picked = (int)lroundf(rawValue(i)) % kSyllables;
    }
    // Middle here sings the Syllable setting; a track sings its notes' syllables
    const int sung = !singing ? -1 : (s_noteOn || track < 0 ? picked : lastSyllable(track) % kSyllables);
    if (singing != av.singing || cat != av.cat || sung != av.sung || picked != av.picked)
    {
        av.singing = singing;
        av.cat = cat;
        av.sung = (int8_t)sung;
        av.picked = (int8_t)picked;
        av.names = kSyllableNames;
        snprintf(av.word, sizeof(av.word), "%s", sung >= 0 ? kSyllableNames[sung] : "");
        refresh();
    }
}

void cGibGobSynth::say(const char* text)
{
    snprintf(s_message, sizeof(s_message), "%s", text);
    s_messageAge = 0;
}

// ---------------------------------------------------------------------------
void cGibGobSynth::rebuild()
{
    buildModel(meta(), s->model);
    page = clampi(page, 0, kPageCount - 1);
    // an engine without the GibGob pages: start where the engine is picked
    if (!s->model.valid && page != kPagePatch)
        page = kPageMain;
    refresh();
}

void cGibGobSynth::refresh()
{
    SynthView& v = s->view;
    const PageModel& pm = s->model.page[page];
    const int views = (page == kPageMain || page == kPagePatch) ? 1 : (pm.viewCount ? pm.viewCount : 1);
    s_view[page] = clampi(s_view[page], 0, views - 1);
    // the engine list of the Main page
    {
        const int count = clampi((int)gsynth()->getSynthTypes().size(), 0, kMaxEngines);
        s->model.engineCount = (uint8_t)count;
        s->model.engineFirst = (uint8_t)clampi(s_engineFirst, 0, count > kEngineRows ? count - kEngineRows : 0);
        s_engineFirst = s->model.engineFirst;
    }
    layoutPage(s->model, page, s_view[page], v.layout);
    const ViewLayout& l = v.layout;
    s_cursor[page] = l.cellCount ? clampi(s_cursor[page], 0, l.cellCount - 1) : 0;
    v.cursor = s_cursor[page];
    v.shift = shiftActive();
    v.editing = true;
    const bool singer = modelSings();
    v.cute = singer;

    // header
    upper(v.synthName, sizeof(v.synthName), gsynth()->getInstrumentTypeName(engineIdx()).c_str());
    {
        char name[32];
        upper(name, sizeof(name), settings().patchName);
        snprintf(v.headerInfo, sizeof(v.headerInfo), "TRK %d // %.13s%s", soundParametersTrack() + 1, name,
                 gsynth()->isPatchDirty(part) ? "*" : "");
    }
    if (views > 1)
        snprintf(v.pageText, sizeof(v.pageText), "%d/%d", s_view[page] + 1, views);
    else
        v.pageText[0] = 0;
    if (page == kPagePatch)
        snprintf(v.title, sizeof(v.title), "Patch");
    else if (page == kPageMain)
        snprintf(v.title, sizeof(v.title), "Engine");
    else if (s->comparing)
        snprintf(v.title, sizeof(v.title), "COMPARE: as saved");
    else
        snprintf(v.title, sizeof(v.title), "%s", pm.title[0] ? pm.title : kPageNames[page]);

    // panels
    int cursorRole = (v.cursor < l.cellCount) ? l.cell[v.cursor].role : 0;
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
            const bool cursorHere = v.cursor < l.cellCount && l.cell[v.cursor].panel == p;
            fillGraph(g, v.graph[p], cursorHere ? cursorRole : 0);
            if (g.graph == kGraphFilter)
            {
                const int tr = g.find(kRoleType);
                if (tr >= 0)
                    formatValue(g.id[tr], rawValue(g.id[tr]), v.panelInfo[p], sizeof(v.panelInfo[p]));
            }
        }
    }
    v.hint[0] = 0;
    v.pathText[0] = 0;
    v.warn = false;
    if (l.style == kStyleMain)
    {
        snprintf(v.panelTitle[0], sizeof(v.panelTitle[0]), "ENGINE");
        snprintf(v.panelTitle[1], sizeof(v.panelTitle[1]), "TRACK SOUND");
        const polysynth::PsynStatus st = polysynth::psynStatus();
        const int freeKb = (int)((st.arenaTotal - st.arenaUsed) / 1024);
        if (settings().voiceCount <= 0)
        {
            v.warn = true;
            snprintf(v.pathText, sizeof(v.pathText), "No voices: this synth is silent!");
            snprintf(v.hint, sizeof(v.hint), "Other synths use all 8 voices");
        }
        else if (st.voiceFailures > 0)
        {
            v.warn = true;
            snprintf(v.pathText, sizeof(v.pathText), "%lu voices failed: memory full",
                     (unsigned long)st.voiceFailures);
            snprintf(v.hint, sizeof(v.hint), "Free: %d KB (old engines in /Synths?)", freeKb);
        }
        else
        {
            snprintf(v.pathText, sizeof(v.pathText), "All synths share 8 voices");
            snprintf(v.hint, sizeof(v.hint), "Plugin memory free: %d KB", freeKb);
        }
    }
    else if (l.style == kStyleEnvList)
        snprintf(v.hint, sizeof(v.hint), "Colour buttons: A / D / S / R");
    else if (l.style == kStylePatch)
    {
        snprintf(v.panelTitle[0], sizeof(v.panelTitle[0]), "PATCH");
        snprintf(v.panelTitle[1], sizeof(v.panelTitle[1]), "INIT / GO APE");
    }
    if (l.cellCount == 0)
    {
        v.warn = !s->model.valid;
        snprintf(v.hint, sizeof(v.hint), "%s",
                 s->model.valid ? "This synth has nothing for this page" : "This engine has no GibGob pages: pick one on Main");
    }

    // boxes
    for (int i = 0; i < l.cellCount; i++)
    {
        const CellPos& c = l.cell[i];
        SynthCellText& t = v.cells[i];
        t.fill = -1;
        t.bipolar = false;
        t.lit = false;
        if (isPatchItem(c.id))
        {
            patchItemText(c.id, t.label, sizeof(t.label), t.value, sizeof(t.value), t.fill);
            t.lit = isEngineItem(c.id) && c.id - kItemEngineFirst == engineIdx();
            continue;
        }
        if (!validParam(c.id))
        {
            snprintf(t.label, sizeof(t.label), "?");
            t.value[0] = 0;
            continue;
        }
        polystrncpy(t.label, meta().params[c.id].displayName.c_str(), sizeof(t.label));
        const float raw = rawValue(c.id);
        formatValue(c.id, raw, t.value, sizeof(t.value));
        const float lo = minOf(c.id), hi = maxOf(c.id);
        if (meta().params[c.id].isEnum)
            t.fill = (int8_t)(hi > lo ? clampi((int)lroundf((raw - lo) / (hi - lo) * 100), 0, 100) : 0);
        else
            t.fill = (int8_t)clampi((int)lroundf(mappedValue(c.id) * 100), 0, 100);
        t.bipolar = lo < 0 && hi > 0 && fabsf(lo + hi) < 0.001f * (hi - lo);
    }

    // the coloured boxes at the top
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
        // an action has no value: just its name
        if (l.cell[c].kind == kCellAction)
            v.quick[k].value[0] = 0;
        else
            polystrncpy(v.quick[k].value, v.cells[c].value, sizeof(v.quick[k].value));
    }

    // bottom: pages, or with Shift the tools
    for (int i = 0; i < 8; i++)
    {
        // a section with its own short name names its tab (LILITRON: VOICE);
        // "Oscillators", "LFOs" and the like keep the page's name
        const char* t = s->model.page[i].title;
        const size_t pn = strlen(kPageNames[i]);
        const bool own = i > kPageMain && i < kPagePatch && t[0] && strlen(t) <= 6 && strncasecmp(t, kPageNames[i], pn) != 0;
        upper(s_tabNames[i], sizeof(s_tabNames[i]), own ? t : kPageNames[i]);
        if (singer && v.shift)
        {
            // a singer: Shift turns the top buttons into the syllables
            v.tabText[i] = kSyllableNames[i];
            v.tabOn[i] = (s_singKey == i);
            continue;
        }
        v.tabText[i] = v.shift ? kTools[i] : s_tabNames[i];
        v.tabOn[i] = v.shift ? ((i == 4 && s->comparing) || (i == 6 && s_latched)) : (i == page);
    }
    snprintf(v.message, sizeof(v.message), "%s", s_message);
    display.refreshControl(control);
}

// ---------------------------------------------------------------------------
void cGibGobSynth::goToPage(int newPage)
{
    s_pendingEngine = -1;
    if (newPage == page)
    {
        // the same button again: the page's next set of columns
        const PageModel& pm = s->model.page[page];
        if (pm.viewCount > 1)
        {
            s_view[page] = (s_view[page] + 1) % pm.viewCount;
            s_cursor[page] = 0;
            s_quickBack[page] = -1;
        }
    }
    else
        page = clampi(newPage, 0, kPageCount - 1);
    refresh();
}

void cGibGobSynth::moveCursor(int dx, int dy)
{
    const ViewLayout& l = s->view.layout;
    // the engine list scrolls at its top and bottom rows
    if (page == kPageMain && dx == 0 && s_cursor[page] < l.cellCount && isEngineItem(l.cell[s_cursor[page]].id))
    {
        const int idx = l.cell[s_cursor[page]].id - kItemEngineFirst;
        const int first = s->model.engineFirst;
        const int count = s->model.engineCount;
        if ((dy < 0 && idx == first && first > 0) || (dy > 0 && idx == first + kEngineRows - 1 && idx + 1 < count))
        {
            s_engineFirst = first + dy;
            s_pendingEngine = -1;
            refresh(); // the same row: the list moved under it
            return;
        }
    }
    const int next = navigate(l, s_cursor[page], dx, dy);
    if (next >= 0 && next != s_cursor[page])
    {
        s_cursor[page] = next;
        s_pendingEngine = -1;
        s_quickBack[page] = -1;
        refresh();
    }
}

void cGibGobSynth::colourButton(int k)
{
    const ViewLayout& l = s->view.layout;
    const int target = l.quick[k];
    if (target < 0)
        return;
    if (s_cursor[page] == target && s_quickBack[page] >= 0)
    {
        // pressed again: back where it was
        s_cursor[page] = s_quickBack[page];
        s_quickBack[page] = -1;
    }
    else if (s_cursor[page] != target)
    {
        if (s_quickBack[page] < 0)
            s_quickBack[page] = s_cursor[page];
        s_cursor[page] = target;
    }
    s_pendingEngine = -1;
    refresh();
}

void cGibGobSynth::changeValue(int value)
{
    const ViewLayout& l = s->view.layout;
    if (s_cursor[page] >= l.cellCount)
        return;
    const CellPos& c = l.cell[s_cursor[page]];
    const int dir = value > 0 ? 1 : -1;
    const bool big = value >= 10 || value <= -10;

    if (isPatchItem(c.id))
    {
        SynthPartSettings& sp = settings();
        switch (c.id)
        {
            case kItemApeAmount: s_apeAmount = clampi(s_apeAmount + dir * 10, 10, 100); break;
            case kItemVoices:
                sp.voiceCount = clampi(sp.voiceCount + dir, 0, gsynth()->getMaxVoiceCount(part));
                gsynth()->setVoiceCount(part, sp.voiceCount);
                newFileManager.setProjectStructChanged();
                break;
            case kItemGain:
                sp.gainDb = clampf(sp.gainDb + value * 0.5f, Synth::kGainSilence, Synth::kGainMax);
                IE->setSynthGain(part, sp.gainDb, false);
                break;
            case kItemPan:
                sp.panning = clampf(sp.panning + value, -Synth::kPanningMax, Synth::kPanningMax);
                IE->setSynthPanning(part, sp.panning, false);
                break;
            case kItemReverb:
                sp.reverbSend = clampf(sp.reverbSend + value, 0.0f, 100.0f);
                IE->setSynthReverbSend(part, sp.reverbSend, false);
                break;
            case kItemDelay:
                sp.delaySend = clampf(sp.delaySend + value, 0.0f, 100.0f);
                IE->setSynthDelaySend(part, sp.delaySend, false);
                break;
            default: return;
        }
        refresh();
        return;
    }

    if (!validParam(c.id))
        return;
    if (s->comparing)
    {
        say("Compare is on: Shift + 5 to go back");
        refresh();
        return;
    }
    beginUndoStep();
    remember(c.id);
    const auto& p = meta().params[c.id];
    if (p.isEnum)
    {
        const int lo = (int)lroundf(minOf(c.id)), hi = (int)lroundf(maxOf(c.id));
        setRaw(c.id, (float)clampi((int)lroundf(rawValue(c.id)) + dir, lo, hi));
    }
    else
    {
        // the same steps as the stock editor on the Mini
        const float range = fabsf(maxOf(c.id) - minOf(c.id));
        const float precision = range >= 10000 ? range * 0.03f
                                : range >= 5000 ? range * 0.1f
                                : range >= 1000 ? range * 0.2f
                                : range >= 500  ? range * 0.5f
                                : range >= 100  ? range * 10.0f
                                : range >= 10   ? range * 20.0f
                                                : range * 200.0f;
        float pv = mappedValue(c.id) * precision;
        pv = clampf(pv + value * 2, 0, precision);
        setMapped(c.id, pv / precision);
    }
    (void)big;
    markDirty();
    refresh();
}

void cGibGobSynth::setDefault()
{
    const ViewLayout& l = s->view.layout;
    if (s_cursor[page] >= l.cellCount)
        return;
    const CellPos& c = l.cell[s_cursor[page]];
    SynthPartSettings& sp = settings();
    switch (c.id)
    {
        case kItemApeAmount: s_apeAmount = 30; break;
        case kItemGain:
            sp.gainDb = Synth::kGainDefault;
            IE->setSynthGain(part, sp.gainDb, false);
            break;
        case kItemPan:
            sp.panning = Synth::kPanningDefault;
            IE->setSynthPanning(part, sp.panning, false);
            break;
        case kItemReverb:
            sp.reverbSend = Synth::kSendDefaultValue;
            IE->setSynthReverbSend(part, sp.reverbSend, false);
            break;
        case kItemDelay:
            sp.delaySend = Synth::kSendDefaultValue;
            IE->setSynthDelaySend(part, sp.delaySend, false);
            break;
        default:
            if (!validParam(c.id) || !meta().params[c.id].defaultValue.has_value())
                return;
            if (s->comparing)
            {
                say("Compare is on: Shift + 5 to go back");
                break;
            }
            beginUndoStep();
            remember(c.id);
            setRaw(c.id, defaultOf(c.id));
            markDirty();
            break;
    }
    refresh();
}

void cGibGobSynth::middle(bool pressed)
{
    const ViewLayout& l = s->view.layout;
    const int item = s_cursor[page] < l.cellCount ? l.cell[s_cursor[page]].id : 0;
    if (page == kPagePatch && pressed)
    {
        switch (item)
        {
            case kItemLoad: loadPatch(); return;
            case kItemSave: savePatch(); return;
            case kItemSaveAs: savePatchAs(); return;
            case kItemInit: initPatch(); return;
            case kItemGoApe: goApe(true); return;
            default: break;
        }
    }
    if (page == kPageMain && pressed && isEngineItem(item))
    {
        const int idx = item - kItemEngineFirst;
        if (idx != engineIdx())
        {
            if (s_pendingEngine == idx)
                applyEngine(); // the second press
            else
            {
                s_pendingEngine = idx;
                say("Middle again: change engine (patch starts clean)");
                refresh();
            }
            return;
        }
    }
    // play a note while held (not while latched: that one is already on)
    if (s_latched)
        return;
    if (pressed && !s_middleDown)
    {
        s_middleDown = true;
        s_noteOn = true;
        IE->playSynthNoteByPad(kPreviewPad, buttonPress);
    }
    else if (!pressed && s_middleDown)
    {
        s_middleDown = false;
        s_noteOn = false;
        IE->playSynthNoteByPad(kPreviewPad, buttonRelease);
    }
}

// a singer with Shift on: the top buttons sing their syllable while held
void cGibGobSynth::singKey(int k, bool pressed)
{
    const int syl = paramNamed("Syllable");
    if (pressed)
    {
        if (s_noteOn)
            IE->playSynthNoteByPad(kPreviewPad, buttonRelease); // one note at a time
        if (syl >= 0)
            setRaw(syl, (float)k);
        IE->playSynthNoteByPad(kPreviewPad, buttonPress);
        s_noteOn = true;
        s_singKey = k;
        s_latched = false;
        s_middleDown = false;
    }
    else if (s_singKey == k)
    {
        IE->playSynthNoteByPad(kPreviewPad, buttonRelease);
        s_noteOn = false;
        s_singKey = -1;
    }
    refresh();
}

// Shift + top row
void cGibGobSynth::tool(int button)
{
    int16_t ids[kClipSize];
    const int n = (page == kPagePatch || page == kPageMain) ? 0 : pageParams(page, ids, kClipSize);
    const bool needsParams = button <= 3;
    if (needsParams && n == 0)
    {
        say("No sound settings on this page");
        refresh();
        return;
    }
    if (s->comparing && button != 4)
    {
        say("Compare is on: Shift + 5 to go back");
        refresh();
        return;
    }
    switch (button)
    {
        case 0: // Init pg
            beginUndoStep();
            for (int i = 0; i < n; i++)
                if (validParam(ids[i]) && meta().params[ids[i]].defaultValue.has_value())
                {
                    remember(ids[i]);
                    setRaw(ids[i], defaultOf(ids[i]));
                }
            markDirty();
            say("Page back to its start values");
            break;
        case 1: // Ape pg
            beginUndoStep();
            for (int i = 0; i < n; i++)
                apeParam(ids[i], s_apeAmount / 100.0f);
            markDirty();
            say("Page went ape");
            break;
        case 2: // Copy pg
            s->clipEngine = engineIdx();
            s->clipPage = page;
            s->clipCount = 0;
            for (int i = 0; i < n; i++)
                if (validParam(ids[i]))
                {
                    s->clipId[s->clipCount] = ids[i];
                    s->clipValue[s->clipCount] = rawValue(ids[i]);
                    s->clipCount++;
                }
            say("Page copied");
            break;
        case 3: // Paste pg
            if (s->clipCount == 0 || s->clipEngine != engineIdx() || s->clipPage != page)
            {
                say(s->clipCount == 0 ? "Nothing copied yet" : "Copied from another page / engine");
                break;
            }
            beginUndoStep();
            for (int i = 0; i < s->clipCount; i++)
                if (validParam(s->clipId[i]))
                {
                    remember(s->clipId[i]);
                    setRaw(s->clipId[i], s->clipValue[i]);
                }
            markDirty();
            say("Page pasted");
            break;
        case 4: // Compare
            if (!s->comparing)
            {
                for (int i = 0; i < s->paramCount; i++)
                    s->edited[i] = rawValue(i);
                applyValues(s->saved);
                s->comparing = true;
                say("Compare: the saved sound");
            }
            else
            {
                applyValues(s->edited);
                s->comparing = false;
                say("Back to your edits");
            }
            break;
        case 5: // Undo
        {
            if (s->undoCount == 0)
            {
                say("Nothing to undo");
                break;
            }
            const uint16_t step = s->undo[s->undoCount - 1].step;
            while (s->undoCount > 0 && s->undo[s->undoCount - 1].step == step)
            {
                const UndoEntry& e = s->undo[--s->undoCount];
                if (validParam(e.id))
                    setRaw(e.id, e.value);
            }
            markDirty();
            say("Undone");
            break;
        }
        case 6: // Latch: the note keeps playing
            if (!s_latched)
            {
                if (!s_noteOn)
                    IE->playSynthNoteByPad(kPreviewPad, buttonPress);
                s_noteOn = true;
                s_latched = true;
                s_middleDown = false;
                say("Latch on: the note holds");
            }
            else
            {
                releaseNotes();
                say("Latch off");
            }
            break;
        case 7: savePatch(); return;
    }
    refresh();
}

// ---------------------------------------------------------------------------
void cGibGobSynth::savePatch()
{
    restoreCompare();
    const char* name = settings().patchName;
    if (name[0] == 0 || strcmp(name, Synth::kInitPatchName) == 0)
    {
        savePatchAs(); // no name yet
        return;
    }
    const bool ok = gsynth()->savePatch(part);
    if (ok)
        takeSnapshot();
    say(ok ? "Patch saved" : "Error saving patch");
    refresh();
}

void cGibGobSynth::initPatch()
{
    releaseNotes();
    restoreCompare();
    SynthPartSettings& sp = settings();
    gsynth()->initPatch(part);
    gsynth()->initSynthPartMacrosValues(part, sp);
    engine.refreshSynthInstrumentParams(sp, part);
    polystrncpy(sp.patchName, gsynth()->getPatchName(part).c_str(), sizeof(SynthPartSettings::patchName));
    newFileManager.setProjectStructChanged();
    patchChanged();
    say("Init patch loaded");
    rebuild();
}

void cGibGobSynth::goApe(bool wholePatch)
{
    (void)wholePatch;
    if (s->comparing)
    {
        say("Compare is on: Shift + 5 to go back");
        refresh();
        return;
    }
    beginUndoStep();
    const float amount = s_apeAmount / 100.0f;
    for (int pg = kPageOsc; pg <= kPageFx; pg++)
    {
        int16_t ids[kClipSize];
        const int n = pageParams(pg, ids, kClipSize);
        for (int i = 0; i < n; i++)
            apeParam(ids[i], amount);
    }
    markDirty();
    say("GO APE! (Shift + 6 undoes it)");
    refresh();
}

void cGibGobSynth::applyEngine()
{
    if (s_pendingEngine < 0)
        return;
    releaseNotes();
    restoreCompare();
    const auto& types = gsynth()->getSynthTypes();
    const int idx = clampi(s_pendingEngine, 0, (int)types.size() - 1);
    s_pendingEngine = -1;
    SynthPartSettings& sp = settings();
    if (idx == sp.synthType)
        return;
    sp.synthType = idx;
    gsynth()->setInstrumentType(part, types.at(idx));
    // at least one voice if any is free, else the new engine stays silent
    const int maxVoices = gsynth()->getMaxVoiceCount(part);
    sp.voiceCount = clampi(sp.voiceCount < 1 ? 4 : sp.voiceCount, 0, maxVoices);
    gsynth()->setVoiceCount(part, sp.voiceCount);
    initPatch();
    newFileManager.setProjectStructChanged();
}

// Load: the file browser, hearing each patch while moving through them
// (the stock synth page's loader, coming back here)
void cGibGobSynth::loadPatch()
{
    releaseNotes();
    restoreCompare();
    const int p = part;
    sequencer.stopManualNotes();
    gsynth()->silencePart(p);

    const bool wasDirty = gsynth()->isPatchDirty(p);
    IE->previousPatchName = gsynth()->getPatchName(p);
    gsynth()->savePatchToWorkspace(p);

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
    cfg.title = "Load Patch - " + gsynth()->getSynthName(p);
    cfg.initialDirectory = gsynth()->getEnginePatchFolder(p);
    cfg.extensionFilter = ".patch";
    cfg.confirmButtonLabel = "Load";
    cfg.contextKey = "synth_patch_" + gsynth()->getSynthName(p);
    cfg.returnModule = this;
    cfg.returnModuleOptions = 0;

    auto loadedPreviewPath = std::make_shared<std::string>();
    auto patchModified = std::make_shared<bool>(false);

    if (!IE->previousPatchName.empty() && IE->previousPatchName != Synth::kInitPatchName)
    {
        cfg.initialSelectedName = leafName(IE->previousPatchName);
        FileSystem* fs = newFileManager.getFileSystem();
        std::string curFull = IE->previousPatchName.front() == '/' ? IE->previousPatchName
                                                                  : cfg.initialDirectory + "/" + IE->previousPatchName;
        if (curFull.size() < 6 || strcasecmp(curFull.c_str() + curFull.size() - 6, ".patch") != 0)
            curFull += ".patch";
        if (!wasDirty && fs != nullptr && fs->exists(curFull.c_str()))
            *loadedPreviewPath = curFull;
        cfg.notifyHighlightOnOpen = false;
    }
    else if (wasDirty)
    {
        cfg.notifyHighlightOnOpen = false;
    }

    auto ensureLoaded = [p, loadedPreviewPath, patchModified](const std::string& fullPath) -> bool
    {
        if (fullPath.empty())
            return false;
        if (*loadedPreviewPath == fullPath)
            return true;
        *patchModified = true;
        sequencer.stopManualNotes();
        IE->getSynth()->silencePart(p);
        if (IE->getSynth()->loadPatchByPath(p, fullPath))
        {
            *loadedPreviewPath = fullPath;
            SynthPartSettings temp = mtProject.values.synthPart[p];
            IE->getSynth()->initSynthPartMacrosValues(p, temp);
            engine.refreshSynthInstrumentParams(temp, p);
            return true;
        }
        loadedPreviewPath->clear();
        return false;
    };

    cfg.onDescribeItem = [p](const std::string& fullPath, bool isFolder, bool) -> std::string
    {
        return isFolder ? std::string() : IE->getSynth()->describePatchFile(p, fullPath);
    };
    cfg.onHighlightChange = [ensureLoaded](const std::string& fullPath, bool isSelectable)
    {
        if (isSelectable && !fullPath.empty())
            ensureLoaded(fullPath);
    };
    cfg.onPreviewStart = [p, ensureLoaded](const std::string& fullPath)
    {
        if (ensureLoaded(fullPath))
            IE->getSynth()->triggerTestNote(p, cInstrumentEditor::kSynthPreviewNote2, true);
    };
    cfg.onPreviewStop = []() { previewNotesOff(); };
    cfg.onPadEvent = [ensureLoaded](uint8_t pad, uint8_t state, int16_t)
    {
        if (state != 0)
        {
            std::string sel = globalFileBrowser.getSelectedPathIfSelectable();
            if (!sel.empty() && !ensureLoaded(sel))
                return;
        }
        IE->playSynthNoteByPad(pad, state);
    };
    // the browser has already brought this screen back when these run
    cfg.onConfirm = [p, ensureLoaded](const std::string& fullPath)
    {
        previewNotesOff();
        sequencer.stopManualNotes();
        if (!ensureLoaded(fullPath))
        {
            IE->revertPatch();
            engine.refreshSynthInstrumentParams(mtProject.values.synthPart[p], p);
            GS->say("Error loading patch");
            GS->rebuild();
            return;
        }
        std::string stored = leafName(IE->getSynth()->getPatchName(p));
        polystrncpy(mtProject.values.synthPart[p].patchName, stored.c_str(), sizeof(SynthPartSettings::patchName));
        IE->getSynth()->initSynthPartMacrosValues(p, mtProject.values.synthPart[p]);
        engine.refreshSynthInstrumentParams(mtProject.values.synthPart[p], p);
        IE->getSynth()->savePatchToWorkspace(p);
        newFileManager.setProjectStructChanged();
        patchChanged();
        GS->say("Patch loaded");
        GS->rebuild();
    };
    cfg.onCancel = [p, patchModified, wasDirty]()
    {
        previewNotesOff();
        sequencer.stopManualNotes();
        IE->getSynth()->silencePart(p);
        if (*patchModified)
        {
            IE->revertPatch();
            engine.refreshSynthInstrumentParams(mtProject.values.synthPart[p], p);
        }
        if (wasDirty)
            IE->getSynth()->setPatchDirty(p);
        GS->rebuild();
    };
    openGlobalFileBrowser(cfg);
}

// Save as: the file browser with a name (the stock patch editor's, coming back here)
void cGibGobSynth::savePatchAs()
{
    releaseNotes();
    restoreCompare();
    const int p = part;
    sequencer.stopManualNotes();

    std::string engineRoot = gsynth()->getEnginePatchFolder(p);
    std::string curName = gsynth()->getPatchName(p);
    std::string initDir = engineRoot;
    std::string defName = "Patch";
    if (!curName.empty() && curName != Synth::kInitPatchName)
    {
        auto slash = curName.find_last_of('/');
        if (curName.front() == '/')
        {
            if (slash != std::string::npos)
            {
                initDir = (slash == 0) ? "/" : curName.substr(0, slash);
                defName = curName.substr(slash + 1);
            }
            else
                defName = curName;
        }
        else if (slash != std::string::npos)
        {
            initDir = engineRoot + "/" + curName.substr(0, slash);
            defName = curName.substr(slash + 1);
        }
        else
            defName = curName;
    }

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveGenericFile;
    cfg.title = "Save Patch As - " + gsynth()->getSynthName(p);
    cfg.initialDirectory = initDir;
    cfg.defaultFileName = defName;
    cfg.extensionFilter = ".patch";
    cfg.maxNameLength = PROJECT_NAME_SIZE - 1;
    cfg.contextKey = "synth_patch_" + gsynth()->getSynthName(p);
    cfg.returnModule = this;
    cfg.returnModuleOptions = 0;
    cfg.onDescribeItem = [p](const std::string& fullPath, bool isFolder, bool) -> std::string
    {
        return isFolder ? std::string() : IE->getSynth()->describePatchFile(p, fullPath);
    };
    cfg.onPadEvent = [](uint8_t pad, uint8_t state, int16_t) { IE->playSynthNoteByPad(pad, state); };
    cfg.onConfirm = [p](const std::string& fullPath)
    {
        sequencer.stopManualNotes();
        const bool ok = IE->getSynth()->savePatchByPath(p, fullPath);
        if (ok)
        {
            std::string name = leafName(IE->getSynth()->getPatchName(p));
            polystrncpy(mtProject.values.synthPart[p].patchName, name.c_str(), sizeof(SynthPartSettings::patchName));
            IE->getSynth()->savePatchToWorkspace(p);
            newFileManager.setProjectStructChanged();
            takeSnapshot();
        }
        GS->say(ok ? "Patch saved" : "Error saving patch");
        GS->refresh();
    };
    openGlobalFileBrowser(cfg);
}

// ---------------------------------------------------------------------------
static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP && !sequencer.isCountingIn())
        playFromStop();
    else
    {
        sequencer.stop();
        setRecArmed(false);
    }
}

static void functUp(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GS->moveCursor(0, -1);
}

static void functDown(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GS->moveCursor(0, 1);
}

static void functLeft(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GS->moveCursor(-1, 0);
}

static void functRight(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GS->moveCursor(1, 0);
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GS->refresh();
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
        GS->middle(true);
    else if (state == buttonRelease)
        GS->middle(false);
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        GS->setDefault();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (idx < 0 || idx >= 8)
        return;
    if (s_singKey == idx && state == buttonRelease)
    {
        GS->singKey(idx, false); // let go of a sung syllable, Shift or not
        return;
    }
    if (state != buttonPress)
        return;
    if (shiftActive() && modelSings())
        GS->singKey(idx, true);
    else if (shiftActive())
        GS->tool(idx);
    else
        GS->goToPage(idx);
}

static void functColour(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    int k = -1;
    if (button == interfaceButtonNote)
        k = 0;
    else if (button == interfaceButtonInstr)
        k = 1;
    else if (button == physFx1Button())
        k = 2;
    else if (button == physFx2Button())
        k = 3;
    if (k >= 0)
        GS->colourButton(k);
}

static void functEncoder(int16_t value)
{
    if (value != 0)
        GS->changeValue(value);
}

static void functPads(uint8_t pad, uint8_t state, int16_t)
{
    IE->playSynthNoteByPad(pad, state);
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    const int t = soundParametersTrack();
    mtInterface.deactivateModule(GS);
    mtInterface.activateModule(&gibgobHome, homeOptions(t));
}

// button 2 again: the chord palette (the next sound page), as on the stock pages
static void functParams(uint8_t state)
{
    if (state == buttonPress)
        openChordPalette();
}

static void functSwitchModule(uint8_t button)
{
    GS->eventFunct(eventSwitchModule, GS, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || s->view.help.active))
    {
        toggleHelp(s->view.help, GS->ip);
        GS->refresh();
        return;
    }
    if (s->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GS);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GS);
}

#else // no synths in this build

void gibgobSynthPoll() {}

bool cGibGobSynth::handles(int)
{
    return false;
}
void cGibGobSynth::initDisplayControls() {}
void cGibGobSynth::start(uint32_t) {}
void cGibGobSynth::stop() {}
void cGibGobSynth::update() {}
void cGibGobSynth::setDefaultScreenFunct() {}
void cGibGobSynth::rebuild() {}
void cGibGobSynth::refresh() {}
void cGibGobSynth::goToPage(int) {}
void cGibGobSynth::moveCursor(int, int) {}
void cGibGobSynth::changeValue(int) {}
void cGibGobSynth::setDefault() {}
void cGibGobSynth::colourButton(int) {}
void cGibGobSynth::middle(bool) {}
void cGibGobSynth::tool(int) {}
void cGibGobSynth::singKey(int, bool) {}
void cGibGobSynth::say(const char*) {}
void cGibGobSynth::loadPatch() {}
void cGibGobSynth::savePatch() {}
void cGibGobSynth::savePatchAs() {}
void cGibGobSynth::initPatch() {}
void cGibGobSynth::goApe(bool) {}
void cGibGobSynth::applyEngine() {}
void cGibGobSynth::releaseNotes() {}
void cGibGobSynth::restoreCompare() {}

#endif
