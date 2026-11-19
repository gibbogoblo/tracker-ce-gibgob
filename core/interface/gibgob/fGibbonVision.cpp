// GibGob OS - Gibbon vision, the performance view (shortcuts: gibgobHelp.h, Shift + Menu)

#include "gibgob/gibbonVision.h"

#include "Platform.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/Perform.h"
#include "gibgob/PerformControl.h"
#include "gibgob/PerformFx.h"
#include "gibgob/drumSeq.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/pianoRoll.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <new>
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

cGibbonVision gibbonVision;
static cGibbonVision* GV = &gibbonVision;

using namespace gibgob;

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functInsert(uint8_t state);
static void functDelete(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functParams(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

namespace
{
constexpr int kEditVisionClear = 90;
// the two frames in the display chip's memory (where the image viewer puts its pictures)
constexpr uint32_t kFrameAddress[2] = { 350000, 410000 };
constexpr int kTicksPerBeat = 4 * kTicksPerStep;
constexpr int kScreenTicks = kVisionScreenSteps * kTicksPerStep;
} // namespace

// ---------------------------------------------------------------------------
// the view lives in the external memory (the fast memory is full)
__NOINIT(EXTERNAL_RAM) alignas(8) static uint8_t s_viewMemory[sizeof(VisionView)];

void cGibbonVision::initDisplayControls()
{
    if (view == nullptr)
        view = new (s_viewMemory) VisionView();
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = view;
    control = display.createControl<VisionControl>(&prop);
}

void cGibbonVision::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    perform::loadSaved();
    if (options & 0x100)
        cursor = (int)(options & 7);
    view->help.active = false;
    view->help.title = "Gibbon vision - shortcuts";
    view->help.lines = kHelpVision;
    view->help.count = (int)(sizeof(kHelpVision) / sizeof(kHelpVision[0]));
    // button 1 is usually still held from the hold 1 + big + that opened this
    patternHeld = mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonPattern);
    patternUsed = patternHeld;
    for (int i = 0; i < 8; i++)
        perfHeld[i] = trackHeld[i] = false;
    lastPlayX = -2;
    lastFrame = -1;

    // the pictures, loaded one after the other by the display
    view->pictureLoaded = false;
    view->pictureHint = "/GibGob/Vision/1.jpg + 2.jpg";
    pictureState = 1;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "GIBBON VISION");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibbonVision::stop()
{
    releaseAllButtons();
    // solo is for this view only: put the mutes back as they were
    if (soloTrack >= 0)
        toggleSolo(soloTrack);
    moduleRefresh = 0;
}

void cGibbonVision::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonInsert, functInsert);
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonParams, functParams);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
void cGibbonVision::update()
{
    perform::uiTick();

    // picture loading (one frame at a time, the display does the work)
    if (pictureState == 1 || pictureState == 3)
    {
        const int f = pictureState == 1 ? 0 : 1;
        if (display.getImgLoadState() == 0 || display.getImgLoadState() == 3)
        {
            char path[40];
            snprintf(path, sizeof(path), "GibGob/Vision/%d", f + 1);
            if (display.readImgFromSd(path, kFrameAddress[f], kVisionTileW, kVisionTileH) == 0)
                pictureState = f == 0 ? 2 : 4;
            else
                pictureState = f == 0 ? 6 : 5; // no frame 1: no picture; no frame 2: frame 1 only
            if (pictureState == 5)
            {
                frameW[1] = frameW[0];
                frameH[1] = frameH[0];
                view->frameAddress[1] = kFrameAddress[0];
                refresh();
            }
        }
    }
    else if (pictureState == 2 || pictureState == 4)
    {
        if (display.isImgLoaded())
        {
            const int f = pictureState == 2 ? 0 : 1;
            frameW[f] = getProps.Width;
            frameH[f] = getProps.Height;
            view->frameAddress[f] = kFrameAddress[f];
            if (f == 0)
            {
                view->frameAddress[1] = kFrameAddress[0];
                view->frameW = frameW[0];
                view->frameH = frameH[0];
                view->pictureLoaded = true;
                pictureState = 3;
            }
            else
            {
                // both frames the same size, or frame 2 is not used
                if (frameW[1] != frameW[0] || frameH[1] != frameH[0])
                    view->frameAddress[1] = kFrameAddress[0];
                pictureState = 5;
            }
            refresh();
        }
        else if (display.getImgLoadState() == 0)
        {
            pictureState = pictureState == 2 ? 6 : 5; // the load failed
            refresh();
        }
    }

    // redraw when something moved: playhead (1/4 steps), hits, effects, frame
    const perform::Clock c = perform::currentClock();
    const int playX = c.playing ? (int)((c.ticks % kScreenTicks) / (kTicksPerStep / 4)) : -1;
    const int frame = c.playing ? (int)((c.ticks / kTicksPerBeat) & 1) : 0;
    uint8_t sounding = 0, active = 0;
    for (int t = 0; t < kTracks; t++)
        if (isSounding(t))
            sounding |= (uint8_t)(1u << t);
    for (int i = 0; i < 8; i++)
        if (perform::isHeld(i) || perform::engine().cellActive(i))
            active |= (uint8_t)(1u << i);
    if (recPoll() || playX != lastPlayX || sounding != lastSounding || active != lastPerfActive || frame != lastFrame)
        refresh();
}

void cGibbonVision::refresh()
{
    const Sequencer::strPattern* p = current();
    view->shift = shiftActive();
    view->cursor = cursor;

    // the 4 bars playing now, the same window for every track
    const perform::Clock c = perform::currentClock();
    const int intoWindow = c.playing ? (int)(c.ticks % kScreenTicks) : 0;
    lastPlayX = c.playing ? intoWindow / (kTicksPerStep / 4) : -1;
    lastFrame = c.playing ? (int)((c.ticks / kTicksPerBeat) & 1) : 0;
    view->frame = lastFrame;
    view->frameW = frameW[0];
    view->frameH = frameH[0];

    uint8_t sounding = 0;
    int mutedCount = 0;
    for (int t = 0; t < kTracks; t++)
    {
        VisionTile& tile = view->tiles[t];
        char name[12];
        trackDisplayName(t, name, sizeof(name));
        snprintf(tile.name, sizeof(tile.name), "%d %.9s", t + 1, name);
        const bool slice = isSliceTrack(p, t);
        const bool melodic = isMelodic(p, t) && !slice;
        tile.color = slice ? color::kSlice : (melodic ? color::kMelodic : color::kDrum);
        tile.muted = mtProject.values.trackMute[t] != 0;
        tile.soloed = (soloTrack == t);
        tile.flash = isSounding(t);
        if (tile.flash)
            sounding |= (uint8_t)(1u << t);
        if (tile.muted)
            mutedCount++;
        tile.playX = lastPlayX;

        const int steps = trackSteps(p, t);
        const int trackTicks = steps * kTicksPerStep;
        const int ph = playheadTick(t);
        int startTick = ph < 0 ? 0 : ph - intoWindow;
        startTick %= trackTicks;
        if (startTick < 0)
            startTick += trackTicks;
        if (steps > kVisionScreenSteps)
        {
            const int bar = startTick / (16 * kTicksPerStep) + 1;
            snprintf(tile.info, sizeof(tile.info), "%d st  bars %d-%d", steps, bar, bar + 3);
        }
        else
        {
            snprintf(tile.info, sizeof(tile.info), "%d st", steps);
        }
        buildVisionTile(p, t, startTick, tile);
    }
    lastSounding = sounding;

    // title and value boxes
    const int bar = c.playing ? (int)(c.ticks / (16 * kTicksPerStep)) + 1 : 1;
    const int beat = c.playing ? (int)((c.ticks / kTicksPerBeat) % 4) + 1 : 1;
    snprintf(view->title, sizeof(view->title), "Pattern %d   Bar %d.%d", mtProject.values.actualPattern, bar, beat);
    snprintf(view->params[0].label, sizeof(view->params[0].label), "BPM");
    snprintf(view->params[0].value, sizeof(view->params[0].value), "%d", (int)(mtProject.values.globalTempo + 0.5f));
    snprintf(view->params[1].label, sizeof(view->params[1].label), "Swing");
    snprintf(view->params[1].value, sizeof(view->params[1].value), "%d%%", patternSwing(p));
    snprintf(view->params[2].label, sizeof(view->params[2].label), "Solo");
    snprintf(view->params[2].value, sizeof(view->params[2].value), "%s", soloTrack >= 0 ? "" : "--");
    if (soloTrack >= 0)
        snprintf(view->params[2].value, sizeof(view->params[2].value), "Trk %d", soloTrack + 1);
    snprintf(view->params[3].label, sizeof(view->params[3].label), "Muted");
    snprintf(view->params[3].value, sizeof(view->params[3].value), "%d", mutedCount);
    for (int i = 0; i < 4; i++)
        view->params[i].color = 0;

    // bottom bar: the CHIMP OUT effects; with Shift on, the tracks
    uint8_t active = 0;
    for (int i = 0; i < 8; i++)
    {
        if (view->shift)
        {
            snprintf(barText[i], sizeof(barText[i]), "%.10s", view->tiles[i].name + 2);
            view->bar.fill[i] = view->tiles[i].muted ? 0x3C3C42 : view->tiles[i].color;
            view->bar.highlighted[i] = view->tiles[i].flash && !view->tiles[i].muted;
        }
        else
        {
            const perform::Cell cell = perform::getLiveCell(i);
            const bool on = perform::isHeld(i) || perform::engine().cellActive(i);
            snprintf(barText[i], sizeof(barText[i]), "%s",
                     cell.fx == perform::kFxOff ? "--" : perform::fxInfo(cell.fx).shortName);
            view->bar.fill[i] = cell.fx == perform::kFxOff ? 0x3C3C42 : performFxColor(cell.fx);
            view->bar.highlighted[i] = on;
        }
        view->bar.text[i] = barText[i];
        if (perform::isHeld(i) || perform::engine().cellActive(i))
            active |= (uint8_t)(1u << i);
    }
    lastPerfActive = active;

    display.refreshControl(control);
}

// ---------------------------------------------------------------------------
void cGibbonVision::moveCursor(int dCol, int dRow)
{
    int cell = visionCellOfTrack(cursor);
    int col = cell % 3 + dCol, row = cell / 3 + dRow;
    if (col == 1 && row == 1)
    {
        // jump over the picture
        col += dCol;
        row += dRow;
    }
    if (col < 0 || col > 2 || row < 0 || row > 2)
        return;
    cell = row * 3 + col;
    cursor = cell < 4 ? cell : cell - 1;
    refresh();
}

void cGibbonVision::topButton(int button, bool pressed)
{
    if (pressed)
    {
        if (shiftActive())
        {
            // Shift + top row: play the track
            trackHeld[button] = true;
            auditionOn(button, laneNote(current(), button), 100);
        }
        else
        {
            perfHeld[button] = true;
            perform::press(button);
        }
    }
    else
    {
        if (perfHeld[button])
        {
            perfHeld[button] = false;
            perform::release(button);
        }
        if (trackHeld[button])
        {
            trackHeld[button] = false;
            auditionOff(button);
        }
    }
    refresh();
}

void cGibbonVision::releaseAllButtons()
{
    for (int i = 0; i < 8; i++)
    {
        if (perfHeld[i])
        {
            perfHeld[i] = false;
            perform::release(i);
        }
        if (trackHeld[i])
        {
            trackHeld[i] = false;
            auditionOff(i);
        }
    }
}

void cGibbonVision::toggleMute(int track)
{
    mtProject.values.trackMute[track] = !mtProject.values.trackMute[track];
    engine.muteTrack((uint8_t)track, mtProject.values.trackMute[track]);
    if (mtProject.values.trackMute[track])
        stopTrack(track);
    newFileManager.setProjectStructChanged();
    refresh();
}

void cGibbonVision::toggleSolo(int track)
{
    if (soloTrack < 0)
    {
        // solo: every other track muted, their mutes remembered
        soloTrack = track;
        for (int t = 0; t < kTracks; t++)
        {
            muteBeforeSolo[t] = mtProject.values.trackMute[t];
            const uint8_t m = t == track ? 0 : 1;
            mtProject.values.trackMute[t] = m;
            engine.muteTrack((uint8_t)t, m);
            if (m)
                stopTrack(t);
        }
    }
    else if (soloTrack != track)
    {
        // solo another track instead
        for (int t = 0; t < kTracks; t++)
        {
            const uint8_t m = t == track ? 0 : 1;
            mtProject.values.trackMute[t] = m;
            engine.muteTrack((uint8_t)t, m);
            if (m)
                stopTrack(t);
        }
        soloTrack = track;
    }
    else
    {
        // un-solo: the mutes as they were
        for (int t = 0; t < kTracks; t++)
        {
            mtProject.values.trackMute[t] = muteBeforeSolo[t];
            engine.muteTrack((uint8_t)t, muteBeforeSolo[t]);
        }
        soloTrack = -1;
    }
    refresh();
}

void cGibbonVision::clearTrack(int track)
{
    beginEdit(kEditVisionClear);
    stopTrack(track);
    clearNotes(current(), track);
    newFileManager.setActualPatternStructChanged();
    mtPopups.show(0, "Track cleared", 1);
    refresh();
}

void cGibbonVision::openEditor()
{
    const int t = cursor;
    mtInterface.deactivateModule(this);
    if (isMelodic(current(), t) || isSliceTrack(current(), t))
    {
        pianoRoll.view.cursorTick = 0;
        mtInterface.activateModule(&pianoRoll, (uint32_t)t);
    }
    else
    {
        drumSeq.cursorStep = 0;
        mtInterface.activateModule(&drumSeq, (uint32_t)t);
    }
}

void cGibbonVision::goBack()
{
    const int t = cursor;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobHome, homeOptions(t));
}

void cGibbonVision::changePattern(int delta)
{
    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        mtPopups.show(0, "Stop playback to change pattern", 1);
        return;
    }
    int target = mtProject.values.actualPattern + delta;
    target = target < PATTERN_INDEX_MIN ? PATTERN_INDEX_MIN : (target > PATTERN_INDEX_MAX ? PATTERN_INDEX_MAX : target);
    if (target == mtProject.values.actualPattern)
        return;
    releaseAllButtons();
    stopAll();
    // same sequence the stock pattern editor uses
    sequencer.sequencialSwitch_Reset();
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    mtProject.values.actualPattern = (uint8_t)target;
    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    sequencer.switchRamPatternsNow();
    refresh();
}

// ---------------------------------------------------------------------------
static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP && !sequencer.isCountingIn())
    {
        if (shiftActive())
            sequencer.playSong();
        else
            playFromStop();
    }
    else
    {
        sequencer.stop();
        setRecArmed(false);
        perform::releaseAll();
    }
    GV->refresh();
}

static void functUp(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (perform::anyHeld())
    {
        if (perform::twist(+1))
            GV->refresh();
        return;
    }
    GV->moveCursor(0, -1);
}

static void functDown(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (perform::anyHeld())
    {
        if (perform::twist(-1))
            GV->refresh();
        return;
    }
    GV->moveCursor(0, 1);
}

static void functLeft(uint8_t state)
{
    if (state == buttonPress)
        GV->moveCursor(-1, 0);
}

static void functRight(uint8_t state)
{
    if (state == buttonPress)
        GV->moveCursor(1, 0);
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
        GV->openEditor();
}

static void functInsert(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftActive())
        GV->toggleSolo(GV->cursor);
    else
        GV->toggleMute(GV->cursor);
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress && shiftActive())
        GV->clearTrack(GV->cursor);
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GV->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (idx < 0 || idx > 7)
        return;
    if (state == buttonPress)
        GV->topButton(idx, true);
    else if (state == buttonRelease)
        GV->topButton(idx, false);
}

static void functEncoder(int16_t value)
{
    // holding a CHIMP OUT effect: +/- twist it while held
    if (perform::anyHeld())
    {
        if (perform::twist(value > 0 ? 1 : -1))
            GV->refresh();
        return;
    }
    const bool big = value >= 10 || value <= -10;
    const int dir = value > 0 ? 1 : -1;
    if (!GV->patternHeld)
        return; // nothing else, so nothing changes by accident
    GV->patternUsed = true;
    if (big)
    {
        if (dir < 0)
            GV->goBack(); // hold 1 + big -: back to the main sequencer
    }
    else
    {
        GV->changePattern(dir); // hold 1 + small +/-: pattern
    }
}

static void functPattern(uint8_t state)
{
    if (state == buttonPress)
    {
        GV->patternHeld = true;
        GV->patternUsed = false;
    }
    else if (state == buttonRelease)
    {
        const bool tap = GV->patternHeld && !GV->patternUsed;
        GV->patternHeld = false;
        if (tap)
            GV->goBack(); // a tap on 1: back to the main sequencer, like everywhere
    }
}

static void functParams(uint8_t state)
{
    if (state == buttonPress)
        openSoundParameters(GV, GV->cursor);
}

static void functSwitchModule(uint8_t button)
{
    GV->eventFunct(eventSwitchModule, GV, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || GV->view->help.active))
    {
        toggleHelp(GV->view->help, GV->ip);
        GV->refresh();
        return;
    }
    if (GV->view->help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GV);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GV);
}
