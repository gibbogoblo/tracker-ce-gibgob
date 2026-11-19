// GibGob OS - main sequencer: 8 track lanes
// Every shortcut is listed in gibgobHelp.h (Shift + Menu shows them).

#include "gibgob/gibgobHome.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "Platform.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/Perform.h"
#include "gibgob/PerformControl.h"
#include "gibgob/gibgobPages.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/drumSeq.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/pianoRoll.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cGibGobHome gibgobHome;
static cGibGobHome* GH = &gibgobHome;
static elapsedMillis s_clock;

using namespace gibgob;

enum EditKind
{
    kEditStep = 1,
    kEditParam,
    kEditLength,
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

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

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
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<LaneControl>(&prop);
}

void cGibGobHome::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    view.help.active = false;
    view.help.title = "Main sequencer - shortcuts";
    view.help.lines = kHelpMain;
    view.help.count = (int)(sizeof(kHelpMain) / sizeof(kHelpMain[0]));
    if (options & 0x100)
        cursorTrack = (int)(options & 7); // opened for a given track
    // otherwise stay on the track you were on
    shiftParam = ShiftParam::None;
    paramHeld = false;
    // button 1 may still be held from the press that opened this screen
    patternHeld = mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonPattern);
    for (int i = 0; i < 8; i++)
    {
        held[i] = false;
        heldRecording[i] = false;
    }
    for (int i = 0; i < 8; i++)
        perfHeld[i] = false;
    perform::loadSaved();
    lastPattern = -1; // forces the defaults to be read again
    lastPlayPos = -2;
    auditionOffAt = 0;

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
    if (state == buttonPress)
        GH->welcomeChoose();
}

void cGibGobHome::openWelcome()
{
    view.welcome.active = true;
    view.welcome.choice = 0;
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->setButtonObj(interfaceButtonLeft, functWelcomeLeft);
    ip->setButtonObj(interfaceButtonRight, functWelcomeRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functWelcomeMiddle);
}

void cGibGobHome::welcomeMove(int d)
{
    int c = view.welcome.choice + d;
    view.welcome.choice = c < 0 ? 0 : (c >= kWelcomeChoices ? kWelcomeChoices - 1 : c);
    refresh();
}

void cGibGobHome::welcomeChoose()
{
    mtProject.values.globalTempo = (float)kWelcomeBpm[view.welcome.choice];
    newFileManager.setProjectStructChanged();
    view.welcome.active = false;
    setDefaultScreenFunct();
    refresh();
}

void cGibGobHome::stop()
{
    releaseAllButtons();
    auditionOff(cursorTrack);
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
// notes that belong to the step under the cursor (within half a step of it)
int cGibGobHome::cellNotes(int* out, int maxOut)
{
    if (cursorStep < 0)
        return 0;
    const Sequencer::strPattern* p = current();
    int trackTicks = trackSteps(p, cursorTrack) * kTicksPerStep;
    int count = noteCount(p, cursorTrack);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
    {
        if (belongsToStep(getNote(p, cursorTrack, i).start, cursorStep, trackTicks))
            out[n++] = i;
    }
    return n;
}

void cGibGobHome::refresh()
{
    const Sequencer::strPattern* p = current();

    if (lastPattern != mtProject.values.actualPattern)
    {
        lastPattern = mtProject.values.actualPattern;
        adoptDefaults(p);
    }

    int master = patternSteps(p);
    cursorTrack = clampInt(cursorTrack, 0, kTracks - 1);
    int laneSteps = trackSteps(p, cursorTrack);
    cursorStep = clampInt(cursorStep, -1, laneSteps - 1);
    stepsVisible = clampInt(stepsVisible, 4, 32);

    view.pattern = p;
    view.patternTicks = master * kTicksPerStep;
    view.stepsVisible = stepsVisible;
    view.cursorTrack = cursorTrack;
    view.nameCursor = cursorStep < 0;
    view.cursorTick = (cursorStep < 0 ? 0 : cursorStep) * kTicksPerStep;
    view.shift = shiftActive();

    // scroll just enough to keep the cursor visible
    int firstStep = view.firstTick / kTicksPerStep;
    int curStep = cursorStep < 0 ? 0 : cursorStep;
    if (curStep < firstStep)
        firstStep = curStep;
    if (curStep >= firstStep + stepsVisible)
        firstStep = curStep - stepsVisible + 1;
    if (firstStep + stepsVisible > master)
        firstStep = master - stepsVisible;
    if (firstStep < 0)
        firstStep = 0;
    view.firstTick = firstStep * kTicksPerStep;

    char names[kTracks][12];
    for (int t = 0; t < kTracks; t++)
    {
        LaneInfo& lane = view.lanes[t];
        trackDisplayName(t, names[t], sizeof(names[t]));
        snprintf(lane.name, sizeof(lane.name), "%d %.9s", t + 1, names[t]);
        lane.slice = isSliceTrack(p, t);
        lane.melodic = isMelodic(p, t) && !lane.slice;
        lane.muted = mtProject.values.trackMute[t] != 0;
        lane.sounding = isSounding(t);
        lane.steps = trackSteps(p, t);
        lane.playhead = playheadTick(t);
    }

    // values of the step under the cursor, or the defaults for new steps
    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    int vel = velocity, nud = nudge, pitch = laneNote(p, cursorTrack);
    if (n > 0)
    {
        Note note = getNote(p, cursorTrack, targets[0]);
        vel = note.velocity;
        nud = stepOffset(note.start, cursorStep, laneSteps * kTicksPerStep);
        pitch = note.pitch;
    }

    snprintf(view.patText, sizeof(view.patText), "Pat %d", mtProject.values.actualPattern);
    view.patHighlight = view.shift && blinkOn;
    if (cursorStep < 0)
        snprintf(view.title, sizeof(view.title), "Middle: settings   Insert: mute");
    else
        snprintf(view.title, sizeof(view.title), "Step %d/%d", cursorStep + 1, laneSteps);

    snprintf(view.mode, sizeof(view.mode), "%s", recModeText());
    view.modeColor = color::kRec;

    for (int i = 0; i < 4; i++)
    {
        view.params[i].label[0] = 0;
        view.params[i].value[0] = 0;
    }
    view.params[0].color = color::kNoteBtn;
    view.params[1].color = color::kInstrBtn;
    view.params[2].color = color::kFx1Btn;
    view.params[3].color = color::kFx2Btn;

    if (cursorStep < 0)
    {
        // on the track name: +/- choose the sound, big +/- the type
        char sound[12];
        gibgobSoundName(getSound(p, cursorTrack), sound, sizeof(sound));
        snprintf(view.params[0].label, sizeof(view.params[0].label), "+/-");
        snprintf(view.params[0].value, sizeof(view.params[0].value), "%.9s", sound);
        snprintf(view.params[1].label, sizeof(view.params[1].label), "Type");
        snprintf(view.params[1].value, sizeof(view.params[1].value), "%s",
                 isSliceTrack(p, cursorTrack) ? "Slice" : (isMelodic(p, cursorTrack) ? "Melodic" : "Drum"));
        view.selectedParam = -1;
    }
    else if (view.shift)
    {
        // what the coloured buttons do with Shift
        snprintf(view.params[0].label, sizeof(view.params[0].label), "All len");
        snprintf(view.params[0].value, sizeof(view.params[0].value), "%d", master);
        snprintf(view.params[2].label, sizeof(view.params[2].label), "BPM");
        snprintf(view.params[2].value, sizeof(view.params[2].value), "%d", (int)(mtProject.values.globalTempo + 0.5f));
        snprintf(view.params[3].label, sizeof(view.params[3].label), "Swing");
        snprintf(view.params[3].value, sizeof(view.params[3].value), "%d%%", patternSwing(p));
        view.selectedParam = shiftParam == ShiftParam::GlobalLen ? 0
                           : shiftParam == ShiftParam::Tempo     ? 2
                           : shiftParam == ShiftParam::Swing     ? 3
                                                                 : -1;
    }
    else
    {
        snprintf(view.params[0].label, sizeof(view.params[0].label), "Len");
        snprintf(view.params[0].value, sizeof(view.params[0].value), "%d", laneSteps);
        snprintf(view.params[1].label, sizeof(view.params[1].label), "Vel");
        snprintf(view.params[1].value, sizeof(view.params[1].value), "%d", vel);
        snprintf(view.params[2].label, sizeof(view.params[2].label), "Nudge");
        snprintf(view.params[2].value, sizeof(view.params[2].value), "%+d", nud);
        snprintf(view.params[3].label, sizeof(view.params[3].label), "Pitch");
        snprintf(view.params[3].value, sizeof(view.params[3].value), "%s", mtNotes[clampInt(pitch, 0, 127)]);
        view.selectedParam = paramHeld ? param : -1; // lit while its button is held
    }

    // bottom bar: the 8 track buttons, named after their tracks.
    // With Shift on they are the performance effects.
    if (shiftActive())
    {
        uint8_t active = 0;
        for (int i = 0; i < 8; i++)
        {
            const perform::Cell c = perform::getCell(i);
            const bool on = perform::isHeld(i) || perform::engine().cellActive(i);
            if (on)
                active |= (uint8_t)(1u << i);
            snprintf(barText[i], sizeof(barText[i]), "%s",
                     c.fx == perform::kFxOff ? "--" : perform::fxInfo(c.fx).shortName);
            view.bar.fill[i] = c.fx == perform::kFxOff ? 0x3C3C42 : performFxColor(c.fx);
            view.bar.highlighted[i] = on;
            view.bar.text[i] = barText[i];
        }
        lastPerfActive = active;
    }
    else
    for (int i = 0; i < 8; i++)
    {
        bool muted = view.lanes[i].muted;
        snprintf(barText[i], sizeof(barText[i]), "%.10s", names[i]);
        view.bar.fill[i] = muted ? 0x3C3C42
                                 : (view.lanes[i].slice ? color::kSlice
                                                        : (view.lanes[i].melodic ? color::kMelodic : color::kDrum));
        view.bar.highlighted[i] = view.lanes[i].sounding && !muted;
        view.bar.text[i] = barText[i];
    }

    uint8_t mask = 0;
    for (int t = 0; t < kTracks; t++)
        if (view.lanes[t].sounding)
            mask |= (uint8_t)(1u << t);
    lastSounding = mask;

    display.refreshControl(grid);
}

void cGibGobHome::update()
{
    bool changed = false;

    if (recPoll())
        changed = true;

    bool shiftNow = shiftActive();
    if (shiftNow != lastShift)
    {
        lastShift = shiftNow;
        if (!shiftNow)
            shiftParam = ShiftParam::None; // Shift off: +/- edit the normal values again
        blinkOn = true;
        blinkAt = (uint32_t)s_clock;
        changed = true;
    }
    if (shiftNow && (uint32_t)s_clock - blinkAt >= 400)
    {
        blinkAt = (uint32_t)s_clock;
        blinkOn = !blinkOn;
        changed = true;
    }

    if (auditionOffAt != 0 && (uint32_t)s_clock >= auditionOffAt)
    {
        auditionOffAt = 0;
        if (!held[cursorTrack])
            auditionOff(cursorTrack);
    }

    if (lastPattern != mtProject.values.actualPattern)
        changed = true;

    int pos = sequencer.isPlay() ? (int)(sequencer.getActualPos()) * 2 : -1;
    int ph = playheadTick(0);
    if (ph >= 0)
        pos = ph / (kTicksPerStep / 2);
    if (pos != lastPlayPos)
    {
        lastPlayPos = pos;
        changed = true;
    }

    uint8_t mask = 0;
    for (int t = 0; t < kTracks; t++)
        if (isSounding(t))
            mask |= (uint8_t)(1u << t);
    if (mask != lastSounding)
        changed = true;

    perform::uiTick();
    if (shiftNow)
    {
        uint8_t perfActive = 0;
        for (int i = 0; i < 8; i++)
            if (perform::isHeld(i) || perform::engine().cellActive(i))
                perfActive |= (uint8_t)(1u << i);
        if (perfActive != lastPerfActive)
            changed = true;
    }

    if (changed)
        refresh();
}

// ---------------------------------------------------------------------------
// actions

void cGibGobHome::moveCursor(int dStep, int dTrack)
{
    if (dStep != 0)
    {
        if (cursorStep < 0 && dStep > 0)
            cursorStep = 0; // from the name column back onto the steps
        else
            cursorStep += dStep;
        if (cursorStep < -1)
            cursorStep = -1;
    }
    cursorTrack += dTrack;
    refresh();
}

void cGibGobHome::openPianoRoll()
{
    int t = cursorTrack;
    int step = cursorStep < 0 ? 0 : cursorStep;
    mtInterface.deactivateModule(this);
    // melodic and slice tracks open the piano roll (slice tracks: one row per slice)
    if (isMelodic(current(), t) || isSliceTrack(current(), t))
    {
        pianoRoll.view.cursorTick = step * kTicksPerStep;
        mtInterface.activateModule(&pianoRoll, (uint32_t)t);
    }
    else
    {
        drumSeq.cursorStep = step;
        mtInterface.activateModule(&drumSeq, (uint32_t)t);
    }
}

void cGibGobHome::openTrackPage()
{
    uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromHome, cursorTrack);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

void cGibGobHome::pressMiddle()
{
    if (cursorStep < 0)
    {
        openTrackPage();
        return;
    }

    openPianoRoll(); // the track's own sequencer: piano roll or drum steps
}

// Insert: put a note on the step under the cursor (base pitch)
void cGibGobHome::insertNote()
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    if (cellNotes(targets, kMaxNotes) > 0)
    {
        auditionOn(cursorTrack, getNote(p, cursorTrack, targets[0]).pitch, (uint8_t)velocity);
        auditionOffAt = (uint32_t)s_clock + 150;
        return; // already a note here
    }
    beginEdit(kEditStep);
    {
        TrackSettings settings = getSettings(p, cursorTrack);
        int trackTicks = trackSteps(p, cursorTrack) * kTicksPerStep;
        Note note{};
        note.start = (uint16_t)startFromOffset(cursorStep, nudge, trackTicks);
        note.length = (uint16_t)lengthTicks;
        note.pitch = laneNote(p, cursorTrack);
        (void)settings;
        note.velocity = (uint8_t)velocity;
        if (addNote(p, cursorTrack, note) < 0)
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
        }
        else
        {
            auditionOn(cursorTrack, note.pitch, note.velocity);
            auditionOffAt = (uint32_t)s_clock + 150;
        }
    }
    markChanged();
    refresh();
}

void cGibGobHome::pressInsert()
{
    if (cursorStep < 0)
    {
        toggleMute(cursorTrack); // on the track name: mute / unmute
        return;
    }
    if (!shiftActive())
    {
        insertNote();
        return;
    }
    Sequencer::strPattern* p = current();
    beginEdit(kEditDuplicate);
    if (duplicateTrack(p, cursorTrack))
    {
        markChanged();
        char msg[40];
        snprintf(msg, sizeof(msg), "Track %d duplicated: %d steps", cursorTrack + 1, trackSteps(p, cursorTrack));
        mtPopups.show(0, msg, 1);
    }
    else
    {
        mtPopups.show(0, "Can't duplicate: max 128 steps / 123 notes", 2);
    }
    refresh();
}

void cGibGobHome::pressDelete()
{
    Sequencer::strPattern* p = current();
    if (shiftActive())
    {
        beginEdit(kEditClear);
        stopTrack(cursorTrack);
        clearNotes(p, cursorTrack);
        mtPopups.show(0, "Track cleared", 1);
    }
    else
    {
        int targets[kMaxNotes];
        int n = cellNotes(targets, kMaxNotes);
        if (n == 0)
            return;
        beginEdit(kEditStep);
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, cursorTrack, targets[i]);
    }
    markChanged();
    refresh();
}

void cGibGobHome::changeTempo(float delta)
{
    if (!sequencer.isSetInternalClock())
    {
        mtPopups.show(0, "Tempo follows the external clock", 1);
        return;
    }
    float t = (float)(int)(mtProject.values.globalTempo + 0.5f) + delta;
    if (t < Sequencer::MIN_TEMPO)
        t = Sequencer::MIN_TEMPO;
    if (t > Sequencer::MAX_TEMPO)
        t = Sequencer::MAX_TEMPO;
    mtProject.values.globalTempo = t;
    newFileManager.setProjectStructChanged();
    refresh();
}

void cGibGobHome::changeSwing(int delta)
{
    Sequencer::strPattern* p = current();
    beginEdit(kEditParam);
    setPatternSwing(p, patternSwing(p) + delta);
    markChanged();
    refresh();
}

void cGibGobHome::changeGlobalLength(int delta, bool big)
{
    Sequencer::strPattern* p = current();
    int steps = patternSteps(p);
    int target = big ? snapLength(steps, delta) : clampInt(steps + delta, 1, kMaxTrackSteps);
    beginEdit(kEditTrackLength);
    for (int t = 0; t < kTracks; t++)
        setTrackSteps(p, t, target);
    markChanged();
    refresh();
}

void cGibGobHome::changeParam(int delta, bool big)
{
    Sequencer::strPattern* p = current();

    if (param == kParamLength)
    {
        beginEdit(kEditTrackLength);
        int steps = trackSteps(p, cursorTrack);
        setTrackSteps(p, cursorTrack, big ? snapLength(steps, delta) : steps + delta);
        markChanged();
        refresh();
        return;
    }

    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    int trackTicks = trackSteps(p, cursorTrack) * kTicksPerStep;
    if (n == 0)
    {
        // no step here: change the defaults used for new steps
        switch (param)
        {
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge:
                nudge = clampInt(nudge + delta, -(kTicksPerStep / 2), kTicksPerStep / 2 - 1);
                break;
            case kParamPitch:
            {
                beginEdit(kEditParam);
                TrackSettings s = getSettings(p, cursorTrack);
                s.baseNote = (uint8_t)clampInt(s.baseNote + delta, 0, 127);
                setSettings(p, cursorTrack, s);
                markChanged();
                auditionOn(cursorTrack, s.baseNote, (uint8_t)velocity);
                auditionOffAt = (uint32_t)s_clock + 200;
                break;
            }
            default: break;
        }
        refresh();
        return;
    }

    beginEdit(kEditParam);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, cursorTrack, targets[i]);
        switch (param)
        {
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
            {
                // stays within half a step either side; step 1 may go early
                int off = clampInt(stepOffset(note.start, cursorStep, trackTicks) + delta, -(kTicksPerStep / 2),
                                   kTicksPerStep / 2 - 1);
                note.start = (uint16_t)startFromOffset(cursorStep, off, trackTicks);
                break;
            }
            case kParamPitch: note.pitch = (uint8_t)clampInt(note.pitch + delta, 0, 127); break;
            default: break;
        }
        setNote(p, cursorTrack, targets[i], note);
    }
    markChanged();
    refresh();
}

void cGibGobHome::changeLength(int delta)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    int maxLen = Sequencer::MAXSTEP * kTicksPerStep;
    int minLen = kTicksPerStep / 2;

    auto stepLen = [&](int len) {
        // half a step is the shortest; from there lengths move in whole steps
        if (len <= minLen && delta > 0)
            return (int)kTicksPerStep;
        if (len == kTicksPerStep && delta < 0)
            return minLen;
        return clampInt(len + delta * kTicksPerStep, minLen, maxLen);
    };

    if (n == 0)
    {
        lengthTicks = stepLen(lengthTicks);
        char msg[32];
        snprintf(msg, sizeof(msg), "New step length: %d.%d", lengthTicks / kTicksPerStep,
                 (lengthTicks % kTicksPerStep) * 10 / kTicksPerStep);
        mtPopups.show(0, msg, 1);
        return;
    }
    beginEdit(kEditLength);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, cursorTrack, targets[i]);
        note.length = (uint16_t)stepLen(note.length);
        setNote(p, cursorTrack, targets[i], note);
        lengthTicks = note.length;
    }
    markChanged();
    refresh();
}

void cGibGobHome::changeZoom(int delta)
{
    stepsVisible = clampInt(stepsVisible + delta, 4, 32);
    refresh();
}

static bool soundUsable(int instr)
{
    if (isSampleInstrument(instr))
        return mtProject.instrument[instr].isActive == 1;
    return isSynthInstrument(instr);
}

void cGibGobHome::changeSound(int dir)
{
    Sequencer::strPattern* p = current();
    int instr = getSound(p, cursorTrack);
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
    auditionOff(cursorTrack);
    stopTrack(cursorTrack);
    setSound(p, cursorTrack, (uint8_t)instr);
    markChanged();
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

void cGibGobHome::toggleMute(int track)
{
    mtProject.values.trackMute[track] = !mtProject.values.trackMute[track];
    engine.muteTrack((uint8_t)track, mtProject.values.trackMute[track]);
    if (mtProject.values.trackMute[track])
        stopTrack(track);
    newFileManager.setProjectStructChanged();
    refresh();
}

void cGibGobHome::trackButton(int track, bool pressed)
{
    // Shift + top row: performance effects, held as long as the button is
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
        if (perfHeld[t])
        {
            perfHeld[t] = false;
            perform::release(t);
        }
    for (int t = 0; t < kTracks; t++)
    {
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

// ---------------------------------------------------------------------------
// button handlers

static bool pressOrRepeat(uint8_t s)
{
    return s == buttonPress || s == buttonHold;
}

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

static void functUp(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    // holding a CHIMP OUT effect: Up twists it (Down / - the other way)
    if (perform::anyHeld())
    {
        if (s == buttonPress && perform::twist(+1))
            GH->refresh();
        return;
    }
    GH->moveCursor(0, -1); // with or without Shift (hold 1 + +/- change pattern)
}

static void functDown(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (perform::anyHeld())
    {
        if (s == buttonPress && perform::twist(-1))
            GH->refresh();
        return;
    }
    GH->moveCursor(0, 1);
}

static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(shiftActive() ? -4 : -1, 0);
}

static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(shiftActive() ? 4 : 1, 0);
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
        GH->pressMiddle();
    else if (state == buttonRelease && GH->auditionOffAt == 0 && !GH->held[GH->cursorTrack])
        auditionOff(GH->cursorTrack);
}

static void functInsert(uint8_t state)
{
    if (state == buttonPress)
        GH->pressInsert();
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        GH->pressDelete();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        GH->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    int idx = button - interfaceButton0;
    if (idx < 0 || idx >= kTracks)
        return;
    if (state == buttonPress)
        GH->trackButton(idx, true);
    else if (state == buttonRelease)
        GH->trackButton(idx, false);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    // the coloured buttons work while held: hold one and press +/- to change
    // its value; let go and +/- zoom / change the step length again
    if (state == buttonRelease)
    {
        GH->paramHeld = false;
        GH->shiftParam = cGibGobHome::ShiftParam::None;
        GH->refresh();
        return;
    }
    if (state != buttonPress)
        return;
    if (GH->cursorStep < 0)
        return; // on the track name the coloured buttons do nothing
    if (shiftActive())
    {
        if (button == interfaceButtonNote)
            GH->shiftParam = cGibGobHome::ShiftParam::GlobalLen;
        else if (button == physFx1Button())
            GH->shiftParam = cGibGobHome::ShiftParam::Tempo;
        else if (button == physFx2Button())
            GH->shiftParam = cGibGobHome::ShiftParam::Swing;
        else
            return;
        GH->refresh();
        return;
    }
    if (button == interfaceButtonNote)
        GH->param = cGibGobHome::kParamLength;
    else if (button == interfaceButtonInstr)
        GH->param = cGibGobHome::kParamVelocity;
    else if (button == physFx1Button())
        GH->param = cGibGobHome::kParamNudge;
    else if (button == physFx2Button())
        GH->param = cGibGobHome::kParamPitch;
    else
        return;
    GH->paramHeld = true;
    GH->refresh();
}

static void functEncoder(int16_t value)
{
    // holding a CHIMP OUT effect: +/- twist it, one step per press
    if (perform::anyHeld())
    {
        if (perform::twist(value > 0 ? 1 : -1))
            GH->refresh();
        return;
    }
    // the Mini's big +/- send +/-10, the small ones +/-1
    bool big = value >= 10 || value <= -10;
    int dir = value > 0 ? 1 : -1;
    // holding button 1: small +/- = next / previous pattern
    if (GH->patternHeld)
    {
        if (!big)
            GH->changePattern(dir);
        return;
    }
    // big +/- always zoom (big + = fewer steps on screen)
    if (big && GH->shiftParam == cGibGobHome::ShiftParam::None && !GH->paramHeld)
    {
        GH->changeZoom(-dir);
        return;
    }
    if (GH->cursorStep < 0)
    {
        // on the track name: small +/- choose the sound (not with Shift on,
        // so CHIMP OUT can't change sounds by accident)
        if (!big && !shiftActive())
            GH->changeSound(dir);
        return;
    }
    if (shiftActive())
    {
        // only while Shift + a coloured button is held
        switch (GH->shiftParam)
        {
            case cGibGobHome::ShiftParam::GlobalLen: GH->changeGlobalLength(dir, big); break;
            case cGibGobHome::ShiftParam::Tempo: GH->changeTempo((float)(big ? dir * 10 : dir)); break;
            case cGibGobHome::ShiftParam::Swing: GH->changeSwing(big ? dir * 5 : dir); break;
            default: break;
        }
        return;
    }
    if (GH->paramHeld)
        GH->changeParam(dir, big); // the held coloured button's value
    else
        GH->changeLength(dir); // small +/- alone: the step's length
}

static void functSwitchModule(uint8_t button)
{
    GH->eventFunct(eventSwitchModule, GH, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && (shiftActive() || GH->view.help.active))
    {
        toggleHelp(GH->view.help, GH->ip);
        GH->refresh();
        return;
    }
    if (GH->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GH);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GH);
}

// button 1: already here; held, +/- change the pattern
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
        openSoundParameters(GH, GH->cursorTrack);
}
