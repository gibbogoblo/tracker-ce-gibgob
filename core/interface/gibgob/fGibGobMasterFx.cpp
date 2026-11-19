// GibGob OS - Master FX page (shortcuts: gibgobHelp.h, Shift + Menu)

#include "gibgob/gibgobMasterFx.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/Master.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "masterParams/masterParams.h"
#include "mtStructs.h"

#include <new>
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

cGibGobMasterFx gibgobMasterFx;
static cGibGobMasterFx* MF = &gibgobMasterFx;

using namespace gibgob;
namespace ms = gibgob::master;

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functMaster(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

namespace
{
const char* const kReleaseNames[4] = { "1/16", "1/8", "1/4", "1/2" };
const char* const kColumnNames[cGibGobMasterFx::kColCount] = { "Duck (kick)", "Glue", "Colour 1", "Colour 2" };
constexpr uint32_t kColumnColor[cGibGobMasterFx::kColCount] = { 0xE07B39, 0x5AC8FA, 0xB8AA53, 0xD6639E };

// the table lives in the external memory (the fast memory is full)
__NOINIT(EXTERNAL_RAM) alignas(8) uint8_t s_viewMemory[sizeof(TableView)];

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

ms::Settings editedSettings(bool pattern)
{
    const Sequencer::strPattern* p = current();
    return pattern ? ms::patternSettings(p) : ms::songSettings();
}

void storeSettings(bool pattern, const ms::Settings& s)
{
    if (pattern)
    {
        ms::setPatternSettings(current(), s); // from now on the pattern has its own
        newFileManager.setActualPatternStructChanged();
    }
    else
    {
        ms::setSongSettings(s);
        newFileManager.setProjectStructChanged();
    }
}
} // namespace

// ---------------------------------------------------------------------------
void cGibGobMasterFx::initDisplayControls()
{
    if (view == nullptr)
        view = new (s_viewMemory) TableView();
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = view;
    table = display.createControl<TableControl>(&prop);
}

void cGibGobMasterFx::start(uint32_t /*options*/)
{
    moduleRefresh = 1;
    screenOpened();
    ms::init();
    view->help.active = false;
    view->help.title = "Master FX - shortcuts";
    view->help.lines = kHelpMasterFx;
    view->help.count = (int)(sizeof(kHelpMasterFx) / sizeof(kHelpMasterFx[0]));
    // a pattern with its own settings opens on them
    editPattern = ms::patternHasOwn(current());
    row = clampInt(row, 0, rowsIn(col) - 1);
    lastGr = -1.0f;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "Master FX");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobMasterFx::stop()
{
    moduleRefresh = 0;
}

void cGibGobMasterFx::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonMaster, functMaster);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cGibGobMasterFx::update()
{
    // the glue's gain reduction, shown while it works
    const float gr = ms::engine().glueReduction();
    if (gr < lastGr - 0.5f || gr > lastGr + 0.5f || (gr == 0.0f && lastGr != 0.0f))
        refresh();
}

int cGibGobMasterFx::rowsIn(int column) const
{
    if (column == kColDuck)
        return 6;
    if (column == kColGlue)
        return 2;
    const ms::Settings s = editedSettings(editPattern);
    const ms::Slot& sl = s.slot[column == kColSlot1 ? 0 : 1];
    return 1 + ms::colourInfo(sl.type).paramCount;
}

// ---------------------------------------------------------------------------
void cGibGobMasterFx::refresh()
{
    const Sequencer::strPattern* p = current();
    const ms::Settings s = editedSettings(editPattern);
    TableView& v = *view;
    v.shift = shiftActive();
    v.rows = 6;
    v.cols = kColCount;
    for (int c = 0; c < kColCount; c++)
    {
        v.colX[c] = 16 + c * 196;
        v.header[c] = kColumnNames[c];
    }
    for (int r = 0; r < TableView::kMaxRows; r++)
        for (int c = 0; c < TableView::kMaxCols; c++)
            v.cell[r][c][0] = 0;

    auto put = [&](int r, int c, const char* label, const char* value)
    {
        snprintf(v.cell[r][c], sizeof(v.cell[r][c]), "%-9s %.9s", label, value);
    };
    char num[16];

    // duck
    put(0, kColDuck, "Duck", s.duckOn ? "On" : "Off");
    {
        char name[12];
        trackDisplayName(s.duckTrack, name, sizeof(name));
        snprintf(num, sizeof(num), "%d %.6s", s.duckTrack + 1, name);
        put(1, kColDuck, "Kick", num);
    }
    snprintf(num, sizeof(num), "%d", s.duckDepth);
    put(2, kColDuck, "Depth", num);
    put(3, kColDuck, "Release", kReleaseNames[s.duckRelease & 3]);
    put(4, kColDuck, "Shape", s.duckShape ? "Hard" : "Smooth");
    put(5, kColDuck, "Noise", s.duckNoise ? "Pumps" : "Steady");
    // glue
    if (s.glue == 0)
        snprintf(num, sizeof(num), "Off");
    else
        snprintf(num, sizeof(num), "%d", s.glue);
    put(0, kColGlue, "Amount", num);
    put(1, kColGlue, "Preset", ms::glueStyleInfo(s.glueStyle).name);
    // colour slots
    for (int k = 0; k < ms::kSlots; k++)
    {
        const int c = kColSlot1 + k;
        const ms::Slot& sl = s.slot[k];
        const ms::ColourInfo& ci = ms::colourInfo(sl.type);
        put(0, c, "Effect", ci.name);
        for (int i = 0; i < ci.paramCount; i++)
        {
            const ms::ParamInfo& pi = ci.params[i];
            if (pi.labels)
                snprintf(num, sizeof(num), "%s", pi.labels[sl.p[i] < pi.count ? sl.p[i] : 0]);
            else
                snprintf(num, sizeof(num), "%d", sl.p[i]);
            put(1 + i, c, pi.name, num);
        }
    }

    row = clampInt(row, 0, rowsIn(col) - 1);
    v.cursorRow = row;
    v.cursorCol = col;
    for (int r = 0; r < TableView::kMaxRows; r++)
        v.rowColor[r] = kColumnColor[col];

    // title: whose settings these are
    const int pat = mtProject.values.actualPattern;
    if (!editPattern)
        snprintf(v.title, sizeof(v.title), "Song settings%s", ms::patternHasOwn(p) ? "  (pattern has its own)" : "");
    else
        snprintf(v.title, sizeof(v.title), "Pattern %d%s", pat,
                 ms::patternHasOwn(p) ? "  (its own)" : "  (song's - edit to make own)");
    lastGr = ms::engine().glueReduction();
    if (s.glue > 0 && lastGr > 0.05f)
    {
        snprintf(v.mode, sizeof(v.mode), "GR %.1f", (double)lastGr);
        v.modeColor = kColumnColor[kColGlue];
    }
    else
    {
        v.mode[0] = 0;
    }

    // bottom bar: whose settings to edit
    snprintf(barText[0], sizeof(barText[0]), "Song");
    snprintf(barText[1], sizeof(barText[1]), "Pattern %d", pat);
    snprintf(barText[2], sizeof(barText[2]), "%s", ms::patternHasOwn(p) ? "Use song" : "");
    for (int i = 3; i < 8; i++)
        barText[i][0] = 0;
    for (int i = 0; i < 8; i++)
    {
        v.bar.text[i] = barText[i];
        v.bar.fill[i] = 0;
        v.bar.highlighted[i] = (i == 0 && !editPattern) || (i == 1 && editPattern);
    }
    display.refreshControl(table);
}

void cGibGobMasterFx::moveCursor(int dCol, int dRow)
{
    col = clampInt(col + dCol, 0, kColCount - 1);
    row = clampInt(row + dRow, 0, rowsIn(col) - 1);
    refresh();
}

void cGibGobMasterFx::changeValue(int delta, bool big)
{
    ms::Settings s = editedSettings(editPattern);
    const int step = big ? 3 : 1;
    auto num = [&](uint8_t& v, int hi) { v = (uint8_t)clampInt((int)v + delta * step, 0, hi); };
    auto cycle = [&](uint8_t& v, int count) { v = (uint8_t)(((int)v + (delta > 0 ? 1 : count - 1)) % count); };
    if (col == kColDuck)
    {
        switch (row)
        {
            case 0: s.duckOn = !s.duckOn; break;
            case 1: cycle(s.duckTrack, 8); break;
            case 2: num(s.duckDepth, 10); break;
            case 3: s.duckRelease = (uint8_t)clampInt(s.duckRelease + delta, 0, 3); break;
            case 4: s.duckShape ^= 1; break;
            case 5: s.duckNoise = !s.duckNoise; break;
        }
    }
    else if (col == kColGlue)
    {
        switch (row)
        {
            case 0: num(s.glue, 10); break;
            case 1: cycle(s.glueStyle, ms::kGlueStyleCount); break;
        }
    }
    else
    {
        ms::Slot& sl = s.slot[col == kColSlot1 ? 0 : 1];
        if (row == 0)
        {
            uint8_t t = sl.type;
            cycle(t, ms::kColourCount);
            sl = ms::defaultSlot(t);
        }
        else
        {
            const ms::ColourInfo& ci = ms::colourInfo(sl.type);
            const int k = row - 1;
            if (k < ci.paramCount)
                num(sl.p[k], ci.params[k].count - 1);
        }
    }
    storeSettings(editPattern, s);
    refresh();
}

void cGibGobMasterFx::topButton(int button)
{
    if (button == 0)
        editPattern = false;
    else if (button == 1)
        editPattern = true;
    else if (button == 2 && ms::patternHasOwn(current()))
    {
        ms::clearPatternSettings(current());
        newFileManager.setActualPatternStructChanged();
        editPattern = false;
        mtPopups.show(0, "Pattern uses the song settings", 1);
    }
    refresh();
}

void cGibGobMasterFx::goToMaster()
{
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&masterParams, (1u << 8) | interfaceButtonMaster);
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
        MF->moveCursor(0, -1);
}

static void functDown(uint8_t state)
{
    if (state == buttonPress || state == buttonHold)
        MF->moveCursor(0, 1);
}

static void functLeft(uint8_t state)
{
    if (state == buttonPress)
        MF->moveCursor(-1, 0);
}

static void functRight(uint8_t state)
{
    if (state == buttonPress)
        MF->moveCursor(1, 0);
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        MF->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (state == buttonPress && idx >= 0 && idx < 8)
        MF->topButton(idx);
}

static void functEncoder(int16_t value)
{
    const bool big = value >= 10 || value <= -10;
    MF->changeValue(value > 0 ? 1 : -1, big);
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    mtInterface.deactivateModule(MF);
    mtInterface.activateModule(&gibgobHome, 0);
}

// Master: the next Master page (back to the first)
static void functMaster(uint8_t state)
{
    if (state == buttonRelease)
        MF->goToMaster();
}

static void functSwitchModule(uint8_t button)
{
    MF->eventFunct(eventSwitchModule, MF, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || MF->view->help.active))
    {
        toggleHelp(MF->view->help, MF->ip);
        MF->refresh();
        return;
    }
    if (MF->view->help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(MF);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(MF);
}
