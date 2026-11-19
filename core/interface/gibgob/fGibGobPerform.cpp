// GibGob OS - Perform page: an effect and its settings on each top button.
// Every shortcut is listed in gibgobHelp.h (Shift + Menu shows them).

#include "gibgob/gibgobPerform.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"
#include "gibgob/Perform.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cGibGobPerform gibgobPerform;
static cGibGobPerform* GPF = &gibgobPerform;

using namespace gibgob;
namespace pf = gibgob::perform;

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functDelete(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functFieldButton(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

static bool pressOrRepeat(uint8_t s)
{
    return s == buttonPress || s == buttonHold;
}

// ---------------------------------------------------------------------------
void cGibGobPerform::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &view;
    control = display.createControl<PerformControl>(&prop);
}

void cGibGobPerform::start(uint32_t /*options*/)
{
    moduleRefresh = 1;
    screenOpened();
    pf::loadSaved();
    fieldHeld = false;
    view.help.active = false;
    view.help.title = "CHIMP OUT FX - shortcuts";
    view.help.lines = kHelpPerform;
    view.help.count = (int)(sizeof(kHelpPerform) / sizeof(kHelpPerform[0]));
    for (int i = 0; i < 8; i++)
        held[i] = false;
    changed = false;
    lastActive = 0xFF;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "CHIMP OUT FX");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobPerform::stop()
{
    for (int i = 0; i < 8; i++)
        if (held[i])
        {
            held[i] = false;
            pf::release(i);
        }
    if (changed)
    {
        pf::save();
        changed = false;
    }
    moduleRefresh = 0;
}

void cGibGobPerform::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    ip->setButtonObj(interfaceButtonNote, functFieldButton);
    ip->setButtonObj(interfaceButtonInstr, functFieldButton);
    ip->setButtonObj(interfaceButtonFx1, functFieldButton);
    ip->setButtonObj(interfaceButtonFx2, functFieldButton);

    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cGibGobPerform::update()
{
    pf::uiTick();
    uint8_t active = 0;
    for (int i = 0; i < 8; i++)
        if (pf::isHeld(i) || pf::engine().cellActive(i))
            active |= (uint8_t)(1u << i);
    if (active != lastActive)
        refresh();
}

void cGibGobPerform::refresh()
{
    view.cursor = cursor;
    view.shift = shiftActive();

    uint8_t active = 0;
    for (int i = 0; i < 8; i++)
    {
        const pf::Cell c = pf::isHeld(i) ? pf::getLiveCell(i) : pf::getCell(i);
        const pf::FxInfo& info = pf::fxInfo(c.fx);
        PerformView::CellView& v = view.cells[i];
        const bool on = pf::isHeld(i) || pf::engine().cellActive(i);
        if (on)
            active |= (uint8_t)(1u << i);
        snprintf(v.name, sizeof(v.name), "%s", c.fx == pf::kFxOff ? "Off" : info.name);
        v.color = performFxColor(c.fx);
        v.off = c.fx == pf::kFxOff;
        v.active = on;
        for (int k = 0; k < 3; k++)
        {
            if (k < info.paramCount)
            {
                char val[16];
                pf::paramText(c.fx, k, c.p[k], val, sizeof(val));
                snprintf(v.line[k], sizeof(v.line[k]), "%s  %s", info.params[k].name, val);
            }
            else
            {
                v.line[k][0] = 0;
            }
        }

        snprintf(barText[i], sizeof(barText[i]), "%s", c.fx == pf::kFxOff ? "--" : info.shortName);
        view.bar.text[i] = barText[i];
        view.bar.fill[i] = c.fx == pf::kFxOff ? 0x3C3C42 : performFxColor(c.fx);
        view.bar.highlighted[i] = on;
    }
    lastActive = active;

    // the four value boxes: effect + its three settings of the selected button
    const pf::Cell sel = pf::getCell(cursor);
    const pf::FxInfo& info = pf::fxInfo(sel.fx);
    const uint32_t boxColor[4] = { color::kNoteBtn, color::kInstrBtn, color::kFx1Btn, color::kFx2Btn };
    snprintf(view.params[0].label, sizeof(view.params[0].label), "Effect");
    snprintf(view.params[0].value, sizeof(view.params[0].value), "%s", sel.fx == pf::kFxOff ? "Off" : info.shortName);
    for (int k = 0; k < 3; k++)
    {
        ParamBox& b = view.params[k + 1];
        if (k < info.paramCount)
        {
            snprintf(b.label, sizeof(b.label), "%.9s", info.params[k].name);
            pf::paramText(sel.fx, k, sel.p[k], b.value, sizeof(b.value));
        }
        else
        {
            snprintf(b.label, sizeof(b.label), "-");
            b.value[0] = 0;
        }
    }
    for (int k = 0; k < 4; k++)
        view.params[k].color = boxColor[k];
    if (field > kFieldEffect && (int)field - 1 >= info.paramCount)
        field = kFieldEffect;
    view.selectedParam = fieldHeld ? field : -1; // lit while its button is held

    snprintf(view.title, sizeof(view.title), "CHIMP OUT  -  button %d", cursor + 1);

    display.refreshControl(control);
}

// ---------------------------------------------------------------------------
void cGibGobPerform::moveCursor(int dCol, int dRow)
{
    int col = cursor % 4 + dCol;
    int row = cursor / 4 + dRow;
    col = col < 0 ? 0 : (col > 3 ? 3 : col);
    row = row < 0 ? 0 : (row > 1 ? 1 : row);
    cursor = row * 4 + col;
    refresh();
}

void cGibGobPerform::changeValue(int delta, bool big)
{
    pf::Cell c = pf::getCell(cursor);
    if (field == kFieldEffect)
    {
        // Pump is retired and Melodic cut is the same as Track cut: skip them
        const int step = delta > 0 ? 1 : -1;
        int fx = (int)c.fx;
        do
        {
            fx += step;
            if (fx < 0)
                fx = pf::kFxCount - 1;
            if (fx >= pf::kFxCount)
                fx = 0;
        } while (fx == pf::kFxMelodicCut || fx == pf::kFxPump);
        c = pf::defaultCell(fx);
    }
    else
    {
        const int k = field - 1;
        const pf::FxInfo& info = pf::fxInfo(c.fx);
        if (k >= info.paramCount)
            return;
        int v = (int)c.p[k] + delta * (big ? 4 : 1);
        const int maxV = info.params[k].count - 1;
        v = v < 0 ? 0 : (v > maxV ? maxV : v);
        c.p[k] = (uint8_t)v;
    }
    pf::setCell(cursor, c);
    changed = true;
    refresh();
}

void cGibGobPerform::topButton(int button, bool pressed)
{
    if (pressed)
    {
        cursor = button;
        held[button] = true;
        pf::press(button);
    }
    else if (held[button])
    {
        held[button] = false;
        pf::release(button);
    }
    refresh();
}

void cGibGobPerform::clearCell()
{
    pf::setCell(cursor, pf::defaultCell(pf::kFxOff));
    field = kFieldEffect;
    changed = true;
    refresh();
}

void cGibGobPerform::goBack()
{
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobHome, 0);
}

// ---------------------------------------------------------------------------
static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
        sequencer.playPattern();
    else
        sequencer.stop();
}

static void functUp(uint8_t s)
{
    if (pf::anyHeld())
    {
        if (s == buttonPress && pf::twist(+1))
            GPF->refresh();
        return;
    }
    if (pressOrRepeat(s))
        GPF->moveCursor(0, -1);
}
static void functDown(uint8_t s)
{
    if (pf::anyHeld())
    {
        if (s == buttonPress && pf::twist(-1))
            GPF->refresh();
        return;
    }
    if (pressOrRepeat(s))
        GPF->moveCursor(0, 1);
}
static void functLeft(uint8_t s)
{
    // holding a button: Left is its far twist (like big -)
    if (pf::anyHeld())
    {
        if (s == buttonPress && pf::twist(-2))
            GPF->refresh();
        return;
    }
    if (pressOrRepeat(s))
        GPF->moveCursor(-1, 0);
}
static void functRight(uint8_t s)
{
    if (pf::anyHeld())
    {
        if (s == buttonPress && pf::twist(2))
            GPF->refresh();
        return;
    }
    if (pressOrRepeat(s))
        GPF->moveCursor(1, 0);
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        GPF->clearCell();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GPF->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (idx < 0 || idx > 7)
        return;
    if (state == buttonPress)
        GPF->topButton(idx, true);
    else if (state == buttonRelease)
        GPF->topButton(idx, false);
}

static void functFieldButton(uint8_t button, uint8_t state)
{
    // the coloured buttons work while held: hold one + press +/- to change it
    if (state == buttonRelease)
    {
        if (GPF->fieldHeld)
        {
            GPF->fieldHeld = false;
            GPF->refresh();
        }
        return;
    }
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        GPF->field = cGibGobPerform::kFieldEffect;
    else if (button == interfaceButtonInstr)
        GPF->field = cGibGobPerform::kFieldParam1;
    else if (button == physFx1Button())
        GPF->field = cGibGobPerform::kFieldParam2;
    else if (button == physFx2Button())
        GPF->field = cGibGobPerform::kFieldParam3;
    else
        return;
    GPF->fieldHeld = true;
    GPF->refresh();
}

static void functEncoder(int16_t value)
{
    // holding a button: +/- twist its effect (until it is let go)
    if (pf::anyHeld())
    {
        if (pf::twist(pf::twistDir(value)))
            GPF->refresh();
        return;
    }
    // only while a coloured button is held, so nothing changes by accident
    if (!GPF->fieldHeld)
        return;
    const bool big = value >= 10 || value <= -10;
    GPF->changeValue(value > 0 ? 1 : -1, big);
}

static void functPattern(uint8_t state)
{
    if (state == buttonPress)
        GPF->goBack();
}

static void functSwitchModule(uint8_t button)
{
    GPF->eventFunct(eventSwitchModule, GPF, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || GPF->view.help.active))
    {
        toggleHelp(GPF->view.help, GPF->ip);
        GPF->refresh();
        return;
    }
    if (GPF->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GPF);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GPF);
}
