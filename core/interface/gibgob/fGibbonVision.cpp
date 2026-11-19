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
// the pictures in the display chip's memory (where the image viewer puts its pictures)
struct PictureSlot
{
    const char* path; // without .jpg
    uint32_t address;
    int maxW, maxH;
};
constexpr int kPictureSlots = 3;
const PictureSlot kSlots[kPictureSlots] = {
    { "GibGob/Vision/1", 350000, kVisionTileW, kVisionTileH },
    { "GibGob/Vision/2", 410000, kVisionTileW, kVisionTileH },
    { "GibGob/Vision/logo", 470000, kVisionLogoW, kVisionLogoH },
};
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
    prop.y = 0;
    prop.w = kScreenW;
    prop.h = 480;
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
    view->logoLoaded = false;
    view->pictureHint = "/GibGob/Vision/1.jpg + 2.jpg";
    loadSlot = 0;
    loadBusy = false;
    for (int i = 0; i < kPictureSlots; i++)
        loadedW[i] = loadedH[i] = 0;

    // the view draws its own header over the whole screen
    songTimer.hide();
    titleBar->hide();

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

    // pictures: frame 1, frame 2, the logo, one after the other (the display
    // decodes them into its own memory)
    if (loadSlot < kPictureSlots)
    {
        const PictureSlot& slot = kSlots[loadSlot];
        if (!loadBusy)
        {
            if (display.getImgLoadState() == 0 || display.getImgLoadState() == 3)
            {
                char path[40];
                snprintf(path, sizeof(path), "%s", slot.path);
                if (display.readImgFromSd(path, slot.address, slot.maxW, slot.maxH) == 0)
                    loadBusy = true;
                else
                    pictureDone(false); // missing, too big or not a baseline JPEG
            }
        }
        else if (display.isImgLoaded())
        {
            loadedW[loadSlot] = getProps.Width;
            loadedH[loadSlot] = getProps.Height;
            pictureDone(true);
        }
        else if (display.getImgLoadState() == 0)
        {
            pictureDone(false); // the load failed
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
    snprintf(view->patternText, sizeof(view->patternText), "Pattern %d", mtProject.values.actualPattern);
    snprintf(view->barText, sizeof(view->barText), "Bar %d.%d", bar, beat);
    snprintf(view->swingText, sizeof(view->swingText), "%d%%", patternSwing(p));
    snprintf(view->bpmText, sizeof(view->bpmText), "%d", (int)(mtProject.values.globalTempo + 0.5f));
    (void)mutedCount;

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
            performBarLabel(cell.fx, cell.p, barText[i], sizeof(barText[i]));
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

// one picture slot finished (loaded or not): use it, go on with the next
void cGibbonVision::pictureDone(bool ok)
{
    loadBusy = false;
    if (loadSlot == 0 && ok)
    {
        view->frameAddress[0] = view->frameAddress[1] = kSlots[0].address;
        view->frameW = loadedW[0];
        view->frameH = loadedH[0];
        view->pictureLoaded = true;
    }
    else if (loadSlot == 1 && ok && view->pictureLoaded && loadedW[1] == loadedW[0] && loadedH[1] == loadedH[0])
    {
        view->frameAddress[1] = kSlots[1].address; // frame 2 (same size as frame 1)
    }
    else if (loadSlot == 2 && ok)
    {
        view->logoAddress = kSlots[2].address;
        view->logoW = loadedW[2];
        view->logoH = loadedH[2];
        view->logoLoaded = true;
    }
    loadSlot++;
    refresh();
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
    if (state != buttonPress)
        return;
    if (perform::anyHeld())
    {
        if (perform::twist(-2)) // far twist (like big -)
            GV->refresh();
        return;
    }
    GV->moveCursor(-1, 0);
}

static void functRight(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (perform::anyHeld())
    {
        if (perform::twist(2)) // far twist (like big +)
            GV->refresh();
        return;
    }
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
        if (perform::twist(perform::twistDir(value)))
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
