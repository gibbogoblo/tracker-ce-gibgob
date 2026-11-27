// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS - PERC editor: a PERC track's drum synth on the synth editor's screen (gibgobPerc.h;
// shortcuts: gibgobHelp.h kHelpPerc, Shift + Menu).

#include "gibgob/gibgobPerc.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"
#include "display.h"
#include "elapsedMillis.h"
#include "fileManager.h"
#include "gibgob/SynthEditControl.h"
#include "gibgob/SynthLayout.h"
#include "gibgob/PercEditView.h"
#include "gibgob/drumOverview.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/perc/PercDevice.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <ctype.h>
#include <new>
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

// the fast memory is full: the module object goes into the on-chip RAM, its screen state outside
__BSS(SRAM_OC) cGibGobPerc gibgobPerc;
static cGibGobPerc* GP = &gibgobPerc;

using namespace gibgob;
using namespace gibgob::synthui;
namespace pc = gibgob::perc;

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
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

namespace
{
struct State
{
    SynthModel model;
    SynthView view;
    pc::Sound opened; // as it was when the editor opened (Compare, Revert)
    pc::Sound edited; // the edited one while comparing
    pc::Sound undo;   // before the last change
    bool hasUndo;
    bool comparing;
    int lastEdited; // the value the last change was to (a new one: a new undo step)
    int modelType;  // the voice type the pages were made for
};
__NOINIT(EXTERNAL_RAM) alignas(8) uint8_t s_stateMemory[sizeof(State)];

// (all in the on-chip RAM: the fast memory is full; set up in initDisplayControls)
__BSS(SRAM_OC) int s_view[kPageCount];
__BSS(SRAM_OC) int s_cursor[kPageCount];
__BSS(SRAM_OC) int s_quickBack[kPageCount];
__BSS(SRAM_OC) int s_preset[pc::kVoiceTypeCount]; // the preset each voice type was last on
__BSS(SRAM_OC) int s_kit;                         // Patch page: the kit picked
__BSS(SRAM_OC) int s_apeAmount;
__BSS(SRAM_OC) elapsedMillis s_messageAge;
__BSS(SRAM_OC) char s_message[64];
__BSS(SRAM_OC) char s_tabNames[8][10];
__BSS(SRAM_OC) uint32_t s_rand;
__BSS(SRAM_OC) State* s;


int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

uint32_t nextRandom()
{
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return s_rand;
}


const pc::Sound& snd()
{
    return pc::sound(GP->track);
}

int type()
{
    return snd().type;
}

bool isParam(int id)
{
    return id >= 0 && id < pc::kParamCount;
}

bool isEnumParam(int p)
{
    return pc::paramInfo(p).max < 16;
}

// the presets of a voice type: its slot of every kit (drums: D1, D2, D3; hats: closed, open)
int slotsOf(int t, int* slots)
{
    switch (t)
    {
        case pc::kDrum:
            slots[0] = pc::kSlotD1;
            slots[1] = pc::kSlotD2;
            slots[2] = pc::kSlotD3;
            return 3;
        case pc::kSnare: slots[0] = pc::kSlotSnare; return 1;
        case pc::kCymbal: slots[0] = pc::kSlotCymbal; return 1;
        default:
            slots[0] = pc::kSlotClosedHat;
            slots[1] = pc::kSlotOpenHat;
            return 2;
    }
}
int presetCount(int t)
{
    int slots[3];
    return pc::kitCount() * slotsOf(t, slots);
}
void presetSound(int t, int i, pc::Sound& out)
{
    int slots[3];
    const int n = slotsOf(t, slots);
    pc::soundFromKit(i / n, slots[i % n], out);
}

// a change is about to happen: what it was goes on the undo (a run of changes to one value is one step)
void beginChange(int what)
{
    if (s->comparing)
    {
        pc::setSound(GP->track, s->edited);
        s->comparing = false;
    }
    if (!s->hasUndo || what != s->lastEdited || what < 0)
    {
        s->undo = snd();
        s->hasUndo = true;
    }
    s->lastEdited = what;
}

// the values a destination steps through: none, then those this voice type has
int stepDest(int current, int dir)
{
    int d = current;
    for (int guard = 0; guard <= pc::kParamCount + 1; guard++)
    {
        d += dir;
        if (d < 0)
            d = pc::kParamCount;
        if (d > pc::kParamCount)
            d = 0;
        if (d == 0 || (pc::destAllowed(d - 1) && pc::paramUsed(d - 1, type())))
            return d;
    }
    return current;
}

void changeParam(int p, int value)
{
    const int dir = value > 0 ? 1 : -1;
    const bool big = value >= 10 || value <= -10;
    const int cur = snd().value[p];
    int next;
    if (p == pc::pVelDest || p == pc::pLfoDest || p == pc::pLfo2Dest)
        next = stepDest(cur, dir);
    else if (isEnumParam(p))
        next = clampi(cur + dir, 0, pc::paramInfo(p).max);
    else
        next = clampi(cur + dir * (big ? 8 : 1), 0, pc::paramInfo(p).max);
    if (next == cur)
        return;
    beginChange(p);
    pc::setValue(GP->track, p, next);
}

void setType(int t)
{
    if (t == type() || t < 0 || t >= pc::kVoiceTypeCount)
        return;
    beginChange(-1);
    pc::Sound ns;
    presetSound(t, s_preset[t], ns);
    pc::setSound(GP->track, ns);
    GP->rebuild();
}

void stepPreset(int dir, bool big)
{
    const int t = type();
    const int n = presetCount(t);
    int slots[3];
    const int per = slotsOf(t, slots);
    s_preset[t] = ((s_preset[t] + dir * (big ? per * 8 : 1)) % n + n) % n;
    beginChange(-1);
    pc::Sound ps;
    presetSound(t, s_preset[t], ps);
    pc::setSound(GP->track, ps);
    pc::play(GP->track, 110);
}

void goApe()
{
    beginChange(-1);
    pc::Sound ns = snd();
    for (int p = 0; p < pc::kParamCount; p++)
    {
        if (!pc::paramUsed(p, ns.type) || p == pc::pVelDest || p == pc::pLfoDest || p == pc::pLfo2Dest ||
            p == pc::pPan || p == pc::pVolume || p == pc::pVelVolume || p == pc::pSampleRate)
            continue;
        const int max = pc::paramInfo(p).max;
        if (max < 16)
        {
            // a switch or a list: now and then another one
            if ((int)(nextRandom() % 100) < s_apeAmount / 3)
                ns.value[p] = (uint8_t)(nextRandom() % (max + 1));
            continue;
        }
        const int span = max * s_apeAmount / 100;
        const int d = (int)(nextRandom() % (2 * span + 1)) - span;
        ns.value[p] = (uint8_t)clampi(ns.value[p] + d, 0, max);
    }
    snprintf(ns.name, sizeof(ns.name), "Ape %s", pc::kVoiceTypeNames[ns.type]);
    pc::setSound(GP->track, ns);
    pc::play(GP->track, 110);
    GP->say("Gone ape");
}
} // namespace

// ---------------------------------------------------------------------------
void cGibGobPerc::initDisplayControls()
{
    if (s == nullptr)
    {
        s = new (s_stateMemory) State();
        s->modelType = -1;
        for (int i = 0; i < kPageCount; i++)
            s_quickBack[i] = -1;
        s_apeAmount = 30;
        s_rand = 0x2545F491;
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

void cGibGobPerc::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    track = (int)(options & 7);
    s_message[0] = 0;
    s->opened = snd();
    s->hasUndo = false;
    s->comparing = false;
    s->lastEdited = -2;
    s->modelType = -1;

    SynthView& v = s->view;
    v.help.active = false;
    v.help.title = "PERC - shortcuts";
    v.help.lines = kHelpPerc;
    v.help.count = (int)(sizeof(kHelpPerc) / sizeof(kHelpPerc[0]));
    v.avatar.names = nullptr;

    songTimer.hide();
    titleBar->hide();
    setDefaultScreenFunct();
    rebuild();
    display.synchronizeRefresh();
}

void cGibGobPerc::stop()
{
    if (s != nullptr && s->comparing)
    {
        pc::setSound(track, s->edited);
        s->comparing = false;
    }
    moduleRefresh = 0;
}

void cGibGobPerc::setDefaultScreenFunct()
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
    ip->setPotObj(interfacePot0, functEncoder, false);
    ip->setPadsGlobal(functPads);
}

void cGibGobPerc::update()
{
    if (s_message[0] && s_messageAge > 1400)
    {
        s_message[0] = 0;
        refresh();
    }
}

void cGibGobPerc::say(const char* text)
{
    snprintf(s_message, sizeof(s_message), "%s", text);
    s_messageAge = 0;
}

void cGibGobPerc::rebuild()
{
    s->modelType = type();
    percui::buildModel(s->model, s->modelType);
    page = clampi(page, 0, kPageCount - 1);
    for (int i = 0; i < kPageCount; i++)
        s_quickBack[i] = -1;
    refresh();
}

void cGibGobPerc::refresh()
{
    if (s->modelType != type())
    {
        rebuild(); // the voice type changed (a preset, a kit, Undo)
        return;
    }
    pc::Sound ps;
    presetSound(type(), s_preset[type()], ps);
    percui::Info info;
    info.track = track;
    info.sourcePerc = pc::trackSource(track);
    info.presetName = ps.name;
    info.kitName = pc::kitName(s_kit);
    info.apeAmount = s_apeAmount;
    info.comparing = s->comparing;
    info.shift = shiftActive();
    info.message = s_message;
    info.tabNames = s_tabNames;
    percui::fillView(s->view, s->model, page, s_view[page], s_cursor[page], snd(), info);
    s_cursor[page] = s->view.cursor;
    display.refreshControl(control);
}

void cGibGobPerc::goToPage(int newPage)
{
    if (newPage == page)
    {
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

void cGibGobPerc::moveCursor(int dx, int dy)
{
    const ViewLayout& l = s->view.layout;
    const int next = navigate(l, s_cursor[page], dx, dy);
    if (next >= 0 && next != s_cursor[page])
    {
        s_cursor[page] = next;
        s_quickBack[page] = -1;
        refresh();
    }
}

void cGibGobPerc::colourButton(int k)
{
    const ViewLayout& l = s->view.layout;
    const int target = l.quick[k];
    if (target < 0)
        return;
    if (s_cursor[page] == target && s_quickBack[page] >= 0)
    {
        s_cursor[page] = s_quickBack[page];
        s_quickBack[page] = -1;
    }
    else if (s_cursor[page] != target)
    {
        if (s_quickBack[page] < 0)
            s_quickBack[page] = s_cursor[page];
        s_cursor[page] = target;
    }
    refresh();
}

void cGibGobPerc::changeValue(int value)
{
    const ViewLayout& l = s->view.layout;
    if (s_cursor[page] >= l.cellCount)
        return;
    const CellPos& c = l.cell[s_cursor[page]];
    const int dir = value > 0 ? 1 : -1;
    const bool big = value >= 10 || value <= -10;
    if (isParam(c.id))
        changeParam(c.id, value);
    else if (isEngineItem(c.id))
        setType(clampi(type() + dir, 0, pc::kVoiceTypeCount - 1));
    else
    {
        const int p = percui::mainParam(c.id, type());
        if (p >= 0)
            changeParam(p, value);
        else
            switch (c.id)
            {
                case kItemVoices: pc::setTrackSource(track, !pc::trackSource(track)); break;
                case kItemDelay:
                    beginChange(-1);
                    pc::setHatOpen(track, !snd().hatOpen);
                    pc::play(track, 110);
                    break;
                case kItemLoad: stepPreset(dir, big); break;
                case kItemSave: s_kit = ((s_kit + dir * (big ? 10 : 1)) % pc::kitCount() + pc::kitCount()) % pc::kitCount(); break;
                case kItemApeAmount: s_apeAmount = clampi(s_apeAmount + dir * 10, 10, 100); break;
                default: return;
            }
    }
    refresh();
}

void cGibGobPerc::setDefault()
{
    const ViewLayout& l = s->view.layout;
    if (s_cursor[page] >= l.cellCount)
        return;
    const CellPos& c = l.cell[s_cursor[page]];
    const int p = isParam(c.id) ? c.id : percui::mainParam(c.id, type());
    if (p < 0)
        return;
    pc::Sound init;
    pc::initSound(init, type());
    beginChange(-1);
    pc::setValue(track, p, init.value[p]);
    refresh();
}

void cGibGobPerc::middle(bool pressed)
{
    if (!pressed)
        return;
    const ViewLayout& l = s->view.layout;
    const int id = s_cursor[page] < l.cellCount ? l.cell[s_cursor[page]].id : 0;
    if (isEngineItem(id))
    {
        setType(id - kItemEngineFirst);
        pc::play(track, 110);
    }
    else if (id == kItemSave)
    {
        // the kit onto every PERC track (the drum tracks in order: D1, SN, CH, D2, D3, OH, CY)
        pc::loadKit(s_kit);
        s->opened = snd();
        s->hasUndo = false;
        char t[48];
        snprintf(t, sizeof(t), "Kit %s on the PERC tracks", pc::kitName(s_kit));
        say(t);
    }
    else if (id == kItemSaveAs)
    {
        beginChange(-1);
        pc::setSound(track, s->opened);
        say("Back as it was");
    }
    else if (id == kItemInit)
        tool(0);
    else if (id == kItemGoApe)
        goApe();
    else
        pc::play(track, 110);
    refresh();
}

void cGibGobPerc::tool(int button)
{
    switch (button)
    {
        case 0:
        {
            beginChange(-1);
            pc::Sound init;
            pc::initSound(init, type());
            pc::setSound(track, init);
            say("Init");
            break;
        }
        case 1: goApe(); break;
        case 4:
            if (s->comparing)
            {
                pc::setSound(track, s->edited);
                s->comparing = false;
            }
            else
            {
                s->edited = snd();
                pc::setSound(track, s->opened);
                s->comparing = true;
            }
            pc::play(track, 110);
            break;
        case 5:
            if (s->hasUndo)
            {
                const pc::Sound now = snd();
                pc::setSound(track, s->undo);
                s->undo = now; // Undo again: redo
                s->lastEdited = -2;
                say("Undo");
            }
            break;
        case 7: back(); return;
        default: return;
    }
    refresh();
}

void cGibGobPerc::back()
{
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&drumOverview, homeOptions(track));
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
        GP->moveCursor(0, -1);
}
static void functDown(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GP->moveCursor(0, 1);
}
static void functLeft(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GP->moveCursor(-1, 0);
}
static void functRight(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        GP->moveCursor(1, 0);
}
static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GP->refresh();
}
static void functMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state == buttonPress)
        GP->middle(true);
}
static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        GP->setDefault();
}
static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (idx < 0 || idx >= 8 || state != buttonPress)
        return;
    if (shiftActive())
        GP->tool(idx);
    else
        GP->goToPage(idx);
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
        GP->colourButton(k);
}
static void functEncoder(int16_t value)
{
    if (value != 0)
        GP->changeValue(value);
}
static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    if (state != buttonPress)
        return;
    pc::play(GP->track, (uint8_t)(velo > 0 && velo <= 127 ? velo : 110), 48 + pad);
}
static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    mtInterface.deactivateModule(GP);
    mtInterface.activateModule(&gibgobHome, homeOptions(GP->track));
}
static void functSwitchModule(uint8_t button)
{
    GP->eventFunct(eventSwitchModule, GP, &button, 0);
}
static void functMenu(uint8_t state)
{
    if (state == buttonPress && !mtMenuPopup.isPopupVisible() && (shiftActive() || s->view.help.active))
    {
        toggleHelp(s->view.help, GP->ip);
        GP->refresh();
        return;
    }
    if (s->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GP);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GP);
}
