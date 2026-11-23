// GibGob OS - main screen: the 8 tracks as clips around your picture (the
// Gibbon vision layout) with the main sequencer's controls. Notes are edited
// in each track's own sequencer (Middle). Every shortcut is listed in
// gibgobHelp.h (Shift + Menu shows them).

#include "gibgob/gibgobHome.h"

#include "Platform.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/Master.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/Perform.h"
#include "gibgob/PerformControl.h"
#include "gibgob/drumOverview.h"
#include "gibgob/drumSeq.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobPages.h"
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

cGibGobHome gibgobHome;
static cGibGobHome* GH = &gibgobHome;
static elapsedMillis s_clock;

using namespace gibgob;

enum EditKind
{
    kEditParam = 1,
    kEditTrackLength,
    kEditSound,
    kEditClear,
    kEditDuplicate,
    kEditRecord,
};

static void functPlay(uint8_t state);
static void functRec(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functInsert(uint8_t state);
static void functDelete(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functParamButton(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);
static void functParams(uint8_t state);
static void functPattern(uint8_t state);

namespace
{
// the pictures in the display chip's memory (where the image viewer puts its pictures)
struct PictureSlot
{
    const char* path; // without .jpg
    uint32_t address;
    int maxW, maxH;
};
constexpr int kPictureSlots = 2;
const PictureSlot kSlots[kPictureSlots] = {
    { "GibGob/Vision/1", 350000, kVisionTileW, kVisionTileH },
    { "GibGob/Vision/2", 410000, kVisionTileW, kVisionTileH },
};
constexpr int kTicksPerBeat = 4 * kTicksPerStep;
constexpr int kScreenTicks = kVisionScreenSteps * kTicksPerStep;
constexpr uint32_t kSoloHoldMs = 400; // hold Insert this long: solo

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

bool soundUsable(int instr)
{
    if (isSampleInstrument(instr))
        return mtProject.instrument[instr].isActive == 1;
    return isSynthInstrument(instr);
}
} // namespace

// the pictures stay in the display's memory until another screen loads one
static bool s_picturesKept = false;
static uint32_t s_loadCallsAfterMine = 0;

// the view lives in the external memory (the fast memory is full)
__NOINIT(EXTERNAL_RAM) alignas(8) static uint8_t s_viewMemory[sizeof(VisionView)];

// ---------------------------------------------------------------------------
void gibgobSoundName(uint8_t instrument, char* out, int outLen)
{
    if (outLen <= 0)
        return;
    if (instrument >= kTotalInstrumentsCount)
    {
        snprintf(out, (size_t)outLen, "?");
    }
    else if (isSampleInstrument(instrument))
    {
        const strInstrument& in = mtProject.instrument[instrument];
        if (in.isActive && in.sample.file_name[0])
        {
            // file name without its extension
            int n = 0;
            while (n < outLen - 1 && n < 31 && in.sample.file_name[n] && in.sample.file_name[n] != '.')
            {
                out[n] = in.sample.file_name[n];
                n++;
            }
            out[n] = 0;
        }
        else
        {
            snprintf(out, (size_t)outLen, "empty %d", instrument + 1);
        }
    }
    else if (isSynthInstrument(instrument))
    {
        snprintf(out, (size_t)outLen, "Synth %d", instrumentIdxToMidiChannel(instrument));
    }
    else
    {
        snprintf(out, (size_t)outLen, "MIDI %d", instrumentIdxToMidiChannel(instrument));
    }
    out[outLen - 1] = 0;
}

// ---------------------------------------------------------------------------
void cGibGobHome::initDisplayControls()
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

void cGibGobHome::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    perform::loadSaved();
    if (options & 0x100)
        cursor = (int)(options & 7); // opened for a given track
    // otherwise stay on the track you were on
    view->help.active = false;
    view->help.title = "Main screen - shortcuts";
    view->help.lines = kHelpMain;
    view->help.count = (int)(sizeof(kHelpMain) / sizeof(kHelpMain[0]));
    view->welcome.active = false;
    param = kParamNone;
    insertDown = insertUsed = false;
    // button 1 may still be held from the press that opened this screen
    patternHeld = mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonPattern);
    for (int i = 0; i < 8; i++)
        held[i] = heldRecording[i] = perfHeld[i] = false;
    lastPattern = -1;
    lastPlayX = -2;
    lastFrame = -1;

    // the pictures, loaded one after the other by the display; not again if
    // no other screen has loaded a picture since (they are still there)
    if (!s_picturesKept || display.imgLoadCalls != s_loadCallsAfterMine)
    {
        view->pictureLoaded = false;
        view->pictureHint = "/GibGob/Vision/1.jpg + 2.jpg";
        loadSlot = 0;
        loadBusy = false;
        s_picturesKept = false;
        for (int i = 0; i < kPictureSlots; i++)
            loadedW[i] = loadedH[i] = 0;
    }

    songTimer.show();
    snprintf(titleText, sizeof(titleText), "GibGob OS");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    if (takeWelcome())
        openWelcome();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobHome::stop()
{
    releaseAllButtons();
    auditionOff(cursor);
    moduleRefresh = 0;
}

void cGibGobHome::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonRec, functRec);
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

    ip->setButtonObj(interfaceButtonNote, functParamButton);
    ip->setButtonObj(interfaceButtonInstr, functParamButton);
    ip->setButtonObj(interfaceButtonFx1, functParamButton);
    ip->setButtonObj(interfaceButtonFx2, functParamButton);

    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, functParams);
    ip->setButtonObj(interfaceButtonPattern, functPattern);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
// welcome window of a new project: pick the tempo you're feeling
static void functWelcomeLeft(uint8_t state)
{
    if (state == buttonPress)
        GH->welcomeMove(-1);
}
static void functWelcomeRight(uint8_t state)
{
    if (state == buttonPress)
        GH->welcomeMove(1);
}
static void functWelcomeMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state == buttonPress)
        GH->welcomeChoose();
}

void cGibGobHome::openWelcome()
{
    view->welcome.active = true;
    view->welcome.choice = 0;
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->setButtonObj(interfaceButtonLeft, functWelcomeLeft);
    ip->setButtonObj(interfaceButtonRight, functWelcomeRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functWelcomeMiddle);
}

void cGibGobHome::welcomeMove(int d)
{
    view->welcome.choice = clampInt(view->welcome.choice + d, 0, kWelcomeChoices - 1);
    refresh();
}

void cGibGobHome::welcomeChoose()
{
    mtProject.values.globalTempo = (float)kWelcomeBpm[view->welcome.choice];
    // the glue preset follows the genre (Hip hop / Garage / Jungle); the glue stays off
    {
        master::Settings ms = master::songSettings();
        ms.glueStyle = (uint8_t)(view->welcome.choice == 0   ? master::kGlueHipHop
                                 : view->welcome.choice == 1 ? master::kGlueGarage
                                                             : master::kGlueJungle);
        master::setSongSettings(ms);
    }
    newFileManager.setProjectStructChanged();
    view->welcome.active = false;
    setDefaultScreenFunct();
    refresh();
}

// ---------------------------------------------------------------------------
void cGibGobHome::update()
{
    perform::uiTick();
    bool changed = recPoll();

    // pictures: frame 1, frame 2, one after the other (the display
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

    // Insert held long enough: solo (a tap mutes, on release)
    if (insertDown && !insertUsed && (uint32_t)s_clock - insertAt >= kSoloHoldMs)
    {
        insertUsed = true;
        toggleSolo(cursor);
    }

    const bool shiftNow = shiftActive();
    if (shiftNow != lastShift)
    {
        lastShift = shiftNow;
        param = kParamNone; // Shift changes what the coloured buttons do
        changed = true;
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
    if (changed || playX != lastPlayX || sounding != lastSounding || active != lastPerfActive || frame != lastFrame ||
        lastPattern != mtProject.values.actualPattern)
        refresh();
}

void cGibGobHome::refresh()
{
    Sequencer::strPattern* p = current();
    if (lastPattern != mtProject.values.actualPattern)
    {
        lastPattern = mtProject.values.actualPattern;
        adoptDefaults(p);
    }
    cursor = clampInt(cursor, 0, kTracks - 1);
    view->shift = shiftActive();
    view->cursor = cursor;

    // the 4 bars playing now, the same window for every track
    const perform::Clock c = perform::currentClock();
    const int intoWindow = c.playing ? (int)(c.ticks % kScreenTicks) : 0;
    lastPlayX = c.playing ? intoWindow / (kTicksPerStep / 4) : -1;
    lastFrame = c.playing ? (int)((c.ticks / kTicksPerBeat) & 1) : 0;
    view->frame = lastFrame;

    uint8_t sounding = 0;
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

    // header (the main sequencer's info strip): pattern, the selected track's
    // step, REC, the four coloured-button values
    const int laneSteps = trackSteps(p, cursor);
    const int ph = playheadTick(cursor);
    snprintf(view->patText, sizeof(view->patText), "Pat %d", mtProject.values.actualPattern);
    snprintf(view->title, sizeof(view->title), "Track %d   Step %d/%d", cursor + 1,
             ph < 0 ? 1 : ph / kTicksPerStep + 1, laneSteps);
    snprintf(view->mode, sizeof(view->mode), "%s", recModeText());
    view->modeColor = color::kRec;
    ParamBox* pb = view->params;
    for (int i = 0; i < 4; i++)
        pb[i].label[0] = pb[i].value[0] = 0;
    pb[0].color = color::kNoteBtn;
    pb[1].color = color::kInstrBtn;
    pb[2].color = color::kFx1Btn;
    pb[3].color = color::kFx2Btn;
    if (view->shift)
    {
        snprintf(pb[0].label, sizeof(pb[0].label), "All len");
        snprintf(pb[0].value, sizeof(pb[0].value), "%d", patternSteps(p));
        snprintf(pb[2].label, sizeof(pb[2].label), "BPM");
        snprintf(pb[2].value, sizeof(pb[2].value), "%d", (int)(mtProject.values.globalTempo + 0.5f));
        snprintf(pb[3].label, sizeof(pb[3].label), "Swing");
        snprintf(pb[3].value, sizeof(pb[3].value), "%d%%", patternSwing(p));
    }
    else
    {
        char sound[12];
        gibgobSoundName(getSound(p, cursor), sound, sizeof(sound));
        snprintf(pb[0].label, sizeof(pb[0].label), "Len");
        snprintf(pb[0].value, sizeof(pb[0].value), "%d", laneSteps);
        snprintf(pb[1].label, sizeof(pb[1].label), "Vel");
        snprintf(pb[1].value, sizeof(pb[1].value), "%d", velocity);
        snprintf(pb[2].label, sizeof(pb[2].label), "Snd");
        snprintf(pb[2].value, sizeof(pb[2].value), "%.7s", sound);
        snprintf(pb[3].label, sizeof(pb[3].label), "Note");
        snprintf(pb[3].value, sizeof(pb[3].value), "%s", mtNotes[clampInt(laneNote(p, cursor), 0, 127)]);
    }
    // lit while its button is held
    switch (param)
    {
        case kParamLength:
        case kParamAllLength: view->selectedParam = 0; break;
        case kParamVelocity: view->selectedParam = 1; break;
        case kParamSound:
        case kParamTempo: view->selectedParam = 2; break;
        case kParamPitch:
        case kParamSwing: view->selectedParam = 3; break;
        default: view->selectedParam = -1; break;
    }

    // bottom bar: the tracks (play / record); with Shift on, the CHIMP OUT effects
    uint8_t active = 0;
    for (int i = 0; i < 8; i++)
    {
        if (view->shift)
        {
            const perform::Cell cell = perform::getLiveCell(i);
            const bool on = perform::isHeld(i) || perform::engine().cellActive(i);
            performBarLabel(cell.fx, cell.p, barText[i], sizeof(barText[i]));
            view->bar.fill[i] = cell.fx == perform::kFxOff ? 0x3C3C42 : performFxColor(cell.fx);
            view->bar.highlighted[i] = on;
        }
        else
        {
            snprintf(barText[i], sizeof(barText[i]), "%.10s", view->tiles[i].name + 2);
            view->bar.fill[i] = view->tiles[i].muted ? 0x3C3C42 : view->tiles[i].color;
            view->bar.highlighted[i] = view->tiles[i].flash && !view->tiles[i].muted;
        }
        view->bar.text[i] = barText[i];
        if (perform::isHeld(i) || perform::engine().cellActive(i))
            active |= (uint8_t)(1u << i);
    }
    lastPerfActive = active;

    display.refreshControl(control);
}

// one picture slot finished (loaded or not): use it, go on with the next
void cGibGobHome::pictureDone(bool ok)
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
    loadSlot++;
    if (loadSlot >= kPictureSlots)
    {
        s_picturesKept = true;
        s_loadCallsAfterMine = display.imgLoadCalls;
    }
    refresh();
}

// ---------------------------------------------------------------------------
// actions

void cGibGobHome::moveCursor(int dCol, int dRow)
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
    auditionOff(cursor);
    cursor = cell < 4 ? cell : cell - 1;
    refresh();
}

// the track's own sequencer: piano roll (melodic, slice) or drum steps
void cGibGobHome::openEditor()
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
        mtInterface.activateModule(&drumOverview, homeOptions(t)); // every drum track, this one picked
    }
}

void cGibGobHome::openTrackPage()
{
    uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromHome, cursor);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

void cGibGobHome::trackButton(int track, bool pressed)
{
    // Shift + top row: CHIMP OUT, held as long as the button is
    if (pressed && shiftActive())
    {
        perfHeld[track] = true;
        perform::press(track);
        refresh();
        return;
    }
    if (!pressed && perfHeld[track])
    {
        perfHeld[track] = false;
        perform::release(track);
        refresh();
        return;
    }
    if (!pressed)
    {
        if (!held[track])
            return;
        held[track] = false;
        auditionOff(track);
        if (heldRecording[track])
        {
            liveRecordOff(track, &heldPitch[track], 1);
            heldRecording[track] = false;
            markChanged();
        }
        refresh();
        return;
    }

    // top row: play the track; with Rec on (and playing) it is recorded
    Sequencer::strPattern* p = current();
    uint8_t pitch = laneNote(p, track);
    held[track] = true;
    heldPitch[track] = pitch;
    auditionOn(track, pitch, (uint8_t)velocity);

    if (g_recArmed && sequencer.isPlay())
    {
        // melodic tracks record how long the button is held, drum hits are one step
        bool hold = isMelodic(p, track) && !isSliceTrack(p, track);
        beginEdit(kEditRecord);
        if (liveRecordOn(track, &pitch, 1, (uint8_t)velocity, hold))
        {
            heldRecording[track] = hold;
            markChanged();
        }
        else
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
        }
    }
    refresh();
}

void cGibGobHome::releaseAllButtons()
{
    for (int t = 0; t < kTracks; t++)
    {
        if (perfHeld[t])
        {
            perfHeld[t] = false;
            perform::release(t);
        }
        if (!held[t])
            continue;
        held[t] = false;
        auditionOff(t);
        if (heldRecording[t])
        {
            liveRecordOff(t, &heldPitch[t], 1);
            heldRecording[t] = false;
            markChanged();
        }
    }
}

void cGibGobHome::toggleMute(int track)
{
    soloTrack = -1; // a mute by hand ends the solo (the mutes stay as they are now)
    mtProject.values.trackMute[track] = !mtProject.values.trackMute[track];
    engine.muteTrack((uint8_t)track, mtProject.values.trackMute[track]);
    if (mtProject.values.trackMute[track])
        stopTrack(track);
    newFileManager.setProjectStructChanged();
    refresh();
}

void cGibGobHome::toggleSolo(int track)
{
    if (soloTrack < 0)
    {
        // solo: every other track muted, their mutes remembered
        for (int t = 0; t < kTracks; t++)
            muteBeforeSolo[t] = mtProject.values.trackMute[t];
    }
    if (soloTrack == track)
    {
        // un-solo: the mutes as they were
        for (int t = 0; t < kTracks; t++)
        {
            mtProject.values.trackMute[t] = muteBeforeSolo[t];
            engine.muteTrack((uint8_t)t, muteBeforeSolo[t]);
            if (muteBeforeSolo[t])
                stopTrack(t);
        }
        soloTrack = -1;
    }
    else
    {
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
    newFileManager.setProjectStructChanged();
    refresh();
}

void cGibGobHome::duplicate()
{
    Sequencer::strPattern* p = current();
    beginEdit(kEditDuplicate);
    if (duplicateTrack(p, cursor))
    {
        markChanged();
        char msg[40];
        snprintf(msg, sizeof(msg), "Track %d duplicated: %d steps", cursor + 1, trackSteps(p, cursor));
        mtPopups.show(0, msg, 1);
    }
    else
    {
        mtPopups.show(0, "Can't duplicate: max 128 steps / 123 notes", 2);
    }
    refresh();
}

void cGibGobHome::clearTrack()
{
    beginEdit(kEditClear);
    stopTrack(cursor);
    clearNotes(current(), cursor);
    markChanged();
    mtPopups.show(0, "Track cleared", 1);
    refresh();
}

// the held coloured button's value (small +/- = 1, big +/- = bigger steps)
void cGibGobHome::changeParam(int dir, bool big)
{
    Sequencer::strPattern* p = current();
    switch (param)
    {
        case kParamLength:
        {
            beginEdit(kEditTrackLength);
            const int steps = trackSteps(p, cursor);
            setTrackSteps(p, cursor, big ? snapLength(steps, dir) : clampInt(steps + dir, 1, kMaxTrackSteps));
            markChanged();
            break;
        }
        case kParamAllLength:
        {
            const int steps = patternSteps(p);
            const int target = big ? snapLength(steps, dir) : clampInt(steps + dir, 1, kMaxTrackSteps);
            beginEdit(kEditTrackLength);
            for (int t = 0; t < kTracks; t++)
                setTrackSteps(p, t, target);
            markChanged();
            break;
        }
        case kParamVelocity: velocity = clampInt(velocity + (big ? dir * 10 : dir), 1, 127); break;
        case kParamSound:
        {
            int instr = getSound(p, cursor);
            for (int tries = 0; tries < kTotalInstrumentsCount; tries++)
            {
                instr = (instr + dir + kTotalInstrumentsCount) % kTotalInstrumentsCount;
                if (soundUsable(instr))
                    break;
            }
            if (!soundUsable(instr))
            {
                mtPopups.show(0, "No sounds loaded yet", 1);
                return;
            }
            beginEdit(kEditSound);
            auditionOff(cursor);
            stopTrack(cursor);
            setSound(p, cursor, (uint8_t)instr);
            markChanged();
            break;
        }
        case kParamPitch:
        {
            beginEdit(kEditParam);
            TrackSettings s = getSettings(p, cursor);
            s.baseNote = (uint8_t)clampInt(s.baseNote + (big ? dir * 12 : dir), 0, 127);
            setSettings(p, cursor, s);
            markChanged();
            auditionOn(cursor, s.baseNote, (uint8_t)velocity);
            break;
        }
        case kParamTempo:
        {
            if (!sequencer.isSetInternalClock())
            {
                mtPopups.show(0, "Tempo follows the external clock", 1);
                return;
            }
            float t = (float)(int)(mtProject.values.globalTempo + 0.5f) + (float)(big ? dir * 10 : dir);
            t = t < Sequencer::MIN_TEMPO ? Sequencer::MIN_TEMPO : (t > Sequencer::MAX_TEMPO ? Sequencer::MAX_TEMPO : t);
            mtProject.values.globalTempo = t;
            newFileManager.setProjectStructChanged();
            break;
        }
        case kParamSwing:
            beginEdit(kEditParam);
            setPatternSwing(p, patternSwing(p) + (big ? dir * 5 : dir));
            markChanged();
            break;
        default: return;
    }
    refresh();
}

void cGibGobHome::changePattern(int delta)
{
    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        mtPopups.show(0, "Stop playback to change pattern", 1);
        return;
    }
    int target = clampInt(mtProject.values.actualPattern + delta, PATTERN_INDEX_MIN, PATTERN_INDEX_MAX);
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
// button handlers

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
        GH->releaseAllButtons();
        sequencer.stop();
        setRecArmed(false); // stopping ends live recording
        perform::releaseAll();
    }
    GH->refresh();
}

static void functRec(uint8_t state)
{
    RecAction a = recButton(state);
    if (a == RecAction::Undo)
    {
        GH->releaseAllButtons();
        undoEdit();
    }
    if (a != RecAction::None)
        GH->refresh();
}

// the D-pad moves between the tracks; while a CHIMP OUT effect is held it
// twists it (Up / Down: near twist, Left / Right: far twist)
static void dpad(uint8_t state, int twist, int dCol, int dRow)
{
    if (state != buttonPress)
        return;
    if (perform::anyHeld())
    {
        if (perform::twist(twist))
            GH->refresh();
        return;
    }
    GH->moveCursor(dCol, dRow);
}
static void functUp(uint8_t s)
{
    dpad(s, +1, 0, -1);
}
static void functDown(uint8_t s)
{
    dpad(s, -1, 0, 1);
}
static void functLeft(uint8_t s)
{
    dpad(s, -2, -1, 0);
}
static void functRight(uint8_t s)
{
    dpad(s, 2, 1, 0);
}

static void functMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state != buttonPress)
        return;
    if (shiftActive())
        GH->openTrackPage(); // Shift + Middle: the track's settings
    else
        GH->openEditor();
}

// Insert: tap = mute, hold = solo, Shift + Insert = duplicate the track
static void functInsert(uint8_t state)
{
    if (state == buttonPress)
    {
        if (shiftActive())
        {
            GH->duplicate();
            return;
        }
        GH->insertDown = true;
        GH->insertUsed = false;
        GH->insertAt = (uint32_t)s_clock;
    }
    else if (state == buttonRelease && GH->insertDown)
    {
        GH->insertDown = false;
        if (!GH->insertUsed)
            GH->toggleMute(GH->cursor);
    }
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress && shiftActive())
        GH->clearTrack(); // Shift + Delete: clear the track
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GH->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (idx < 0 || idx >= kTracks)
        return;
    if (state == buttonPress)
        GH->trackButton(idx, true);
    else if (state == buttonRelease)
        GH->trackButton(idx, false);
}

// the coloured buttons work while held: hold one and press +/-
static void functParamButton(uint8_t button, uint8_t state)
{
    if (state == buttonRelease)
    {
        GH->param = cGibGobHome::kParamNone;
        auditionOff(GH->cursor);
        GH->refresh();
        return;
    }
    if (state != buttonPress)
        return;
    const bool shift = shiftActive();
    if (button == interfaceButtonNote)
        GH->param = shift ? cGibGobHome::kParamAllLength : cGibGobHome::kParamLength;
    else if (button == interfaceButtonInstr)
        GH->param = shift ? cGibGobHome::kParamNone : cGibGobHome::kParamVelocity;
    else if (button == physFx1Button())
        GH->param = shift ? cGibGobHome::kParamTempo : cGibGobHome::kParamSound;
    else if (button == physFx2Button())
        GH->param = shift ? cGibGobHome::kParamSwing : cGibGobHome::kParamPitch;
    else
        return;
    GH->refresh();
}

static void functEncoder(int16_t value)
{
    // holding a CHIMP OUT effect: +/- twist it, one step per press
    if (perform::anyHeld())
    {
        if (perform::twist(perform::twistDir(value)))
            GH->refresh();
        return;
    }
    // the Mini's big +/- send +/-10, the small ones +/-1
    const bool big = value >= 10 || value <= -10;
    const int dir = value > 0 ? 1 : -1;
    // holding button 1: small +/- = next / previous pattern
    if (GH->patternHeld)
    {
        if (!big)
            GH->changePattern(dir);
        return;
    }
    // otherwise only with a coloured button held, so nothing changes by accident
    if (GH->param != cGibGobHome::kParamNone)
        GH->changeParam(dir, big);
}

static void functSwitchModule(uint8_t button)
{
    GH->eventFunct(eventSwitchModule, GH, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || GH->view->help.active))
    {
        toggleHelp(GH->view->help, GH->ip);
        GH->refresh();
        return;
    }
    if (GH->view->help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GH);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GH);
}

// button 1: already here; held, small +/- change the pattern
static void functPattern(uint8_t state)
{
    if (state == buttonPress)
        GH->patternHeld = true;
    else if (state == buttonRelease)
        GH->patternHeld = false;
}

static void functParams(uint8_t state)
{
    if (state == buttonPress)
        openSoundParameters(GH, GH->cursor);
}
