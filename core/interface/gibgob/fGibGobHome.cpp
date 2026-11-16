// GibGob OS - main sequencer: 8 track lanes
//
//   D-pad            move cursor (Shift: Left/Right = 4 steps, Up/Down = previous/next pattern)
//   Middle           Drum lane: toggle a step.  Melodic lane: open the piano roll
//   Insert           open the track in the piano roll (Shift+Insert = track page)
//   Delete           delete the step under the cursor (Shift+Delete = clear the track)
//   Top row 1..8     play track 1..8 (finger drumming); recorded while Rec is on and playing
//                    Shift + top row = mute / unmute that track
//   Note/Instr/FX1/FX2  choose what small +/- edits: Probability / Velocity / Nudge / Pitch
//   small +/-        edit that value (Shift + small +/- = change the track's sound)
//   big +/-          step length          (Shift + big +/- = zoom)
//   Rec              toggle live recording        Play = play / stop (Shift = song)
//   Shift            toggles (red border while on)

#include "gibgob/gibgobHome.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobPages.h"
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
    cursorTrack = (int)(options % kTracks);
    for (int i = 0; i < 8; i++)
    {
        held[i] = false;
        heldRecording[i] = false;
    }
    lastPattern = -1; // forces the defaults to be read again
    lastPlayPos = -2;

    songTimer.show();
    snprintf(titleText, sizeof(titleText), "GibGob OS");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
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

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
// notes that belong to the step under the cursor (within half a step of it)
int cGibGobHome::cellNotes(int* out, int maxOut)
{
    const Sequencer::strPattern* p = current();
    int tick = cursorStep * kTicksPerStep;
    int count = noteCount(p, cursorTrack);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
    {
        Note note = getNote(p, cursorTrack, i);
        if (note.start >= tick - kTicksPerStep / 2 && note.start < tick + kTicksPerStep / 2)
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

    int patternSteps = p->track[0].length + 1;
    cursorStep = clampInt(cursorStep, 0, patternSteps - 1);
    cursorTrack = clampInt(cursorTrack, 0, kTracks - 1);
    stepsVisible = clampInt(stepsVisible, 4, 32);

    view.pattern = p;
    view.patternTicks = patternSteps * kTicksPerStep;
    view.stepsVisible = stepsVisible;
    view.cursorTrack = cursorTrack;
    view.cursorTick = cursorStep * kTicksPerStep;
    view.shift = shiftActive();
    view.playheadTick = playheadTick(cursorTrack);

    // scroll just enough to keep the cursor visible
    int firstStep = view.firstTick / kTicksPerStep;
    if (cursorStep < firstStep)
        firstStep = cursorStep;
    if (cursorStep >= firstStep + stepsVisible)
        firstStep = cursorStep - stepsVisible + 1;
    if (firstStep + stepsVisible > patternSteps)
        firstStep = patternSteps - stepsVisible;
    if (firstStep < 0)
        firstStep = 0;
    view.firstTick = firstStep * kTicksPerStep;

    for (int t = 0; t < kTracks; t++)
    {
        LaneInfo& lane = view.lanes[t];
        char name[12];
        gibgobSoundName(getSound(p, t), name, sizeof(name));
        snprintf(lane.name, sizeof(lane.name), "%d %.9s", t + 1, name);
        lane.melodic = isMelodic(p, t);
        lane.muted = mtProject.values.trackMute[t] != 0;
        lane.sounding = isSounding(t);
    }

    // values of the step under the cursor, or the defaults for new steps
    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    TrackSettings settings = getSettings(p, cursorTrack);
    int vel = velocity, chn = chance, nud = nudge, pitch = settings.baseNote;
    if (n > 0)
    {
        Note note = getNote(p, cursorTrack, targets[0]);
        vel = note.velocity;
        chn = note.chance();
        nud = note.start - view.cursorTick;
        pitch = note.pitch;
    }

    snprintf(view.title, sizeof(view.title), "Pat %d   Track %d   Step %d/%d", mtProject.values.actualPattern,
             cursorTrack + 1, cursorStep + 1, patternSteps);

    if (g_recArmed)
    {
        snprintf(view.mode, sizeof(view.mode), "REC");
        view.modeColor = color::kRec;
    }
    else
    {
        view.mode[0] = 0;
    }

    snprintf(view.params[0].label, sizeof(view.params[0].label), "Prob");
    snprintf(view.params[0].value, sizeof(view.params[0].value), "%d%%", chn);
    view.params[0].color = color::kNoteBtn;
    snprintf(view.params[1].label, sizeof(view.params[1].label), "Vel");
    snprintf(view.params[1].value, sizeof(view.params[1].value), "%d", vel);
    view.params[1].color = color::kInstrBtn;
    snprintf(view.params[2].label, sizeof(view.params[2].label), "Nudge");
    snprintf(view.params[2].value, sizeof(view.params[2].value), "%+d", nud);
    view.params[2].color = color::kFx1Btn;
    snprintf(view.params[3].label, sizeof(view.params[3].label), "Pitch");
    snprintf(view.params[3].value, sizeof(view.params[3].value), "%s", mtNotes[clampInt(pitch, 0, 127)]);
    view.params[3].color = color::kFx2Btn;
    view.selectedParam = param;

    // bottom bar: the 8 track buttons
    for (int i = 0; i < 8; i++)
    {
        bool muted = view.lanes[i].muted;
        if (view.shift)
        {
            snprintf(barText[i], sizeof(barText[i]), muted ? "Muted %d" : "Mute %d", i + 1);
            view.bar.fill[i] = muted ? color::kShiftBorder : 0;
            view.bar.highlighted[i] = muted;
        }
        else
        {
            snprintf(barText[i], sizeof(barText[i]), muted ? "%d  M" : "%d", i + 1);
            view.bar.fill[i] = view.lanes[i].melodic ? color::kMelodic : color::kDrum;
            view.bar.highlighted[i] = view.lanes[i].sounding && !muted;
        }
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

    if (auditionOffAt != 0 && (uint32_t)s_clock >= auditionOffAt)
    {
        auditionOffAt = 0;
        if (!held[cursorTrack])
            auditionOff(cursorTrack);
    }

    if (lastPattern != mtProject.values.actualPattern)
        changed = true;

    int ph = playheadTick(cursorTrack);
    int pos = ph < 0 ? -1 : ph / (kTicksPerStep / 2);
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

    if (changed)
        refresh();
}

// ---------------------------------------------------------------------------
// actions

void cGibGobHome::moveCursor(int dStep, int dTrack)
{
    cursorStep += dStep;
    cursorTrack += dTrack;
    refresh();
}

void cGibGobHome::openPianoRoll()
{
    int t = cursorTrack;
    pianoRoll.view.cursorTick = cursorStep * kTicksPerStep;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&pianoRoll, (uint32_t)t);
}

void cGibGobHome::openTrackPage()
{
    uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromHome, cursorTrack);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

void cGibGobHome::pressMiddle()
{
    Sequencer::strPattern* p = current();
    if (isMelodic(p, cursorTrack))
    {
        openPianoRoll();
        return;
    }

    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    if (n > 0)
    {
        // indices come back ascending: remove from the last so they stay valid
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, cursorTrack, targets[i]);
    }
    else
    {
        TrackSettings settings = getSettings(p, cursorTrack);
        int patternTicks = (p->track[cursorTrack].length + 1) * kTicksPerStep;
        Note note{};
        note.start = (uint16_t)clampInt(cursorStep * kTicksPerStep + nudge, 0, patternTicks - 1);
        note.length = (uint16_t)lengthTicks;
        note.pitch = settings.baseNote;
        note.velocity = (uint8_t)velocity;
        note.setChance(chance);
        if (addNote(p, cursorTrack, note) < 0)
            mtPopups.show(0, "Track is full (125 notes)", 1);
        else
            auditionOn(cursorTrack, note.pitch, note.velocity);
    }
    markChanged();
    refresh();
}

void cGibGobHome::pressDelete()
{
    Sequencer::strPattern* p = current();
    if (shiftActive())
    {
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
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, cursorTrack, targets[i]);
    }
    markChanged();
    refresh();
}

void cGibGobHome::changeParam(int delta)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = cellNotes(targets, kMaxNotes);
    int patternTicks = (p->track[cursorTrack].length + 1) * kTicksPerStep;
    int cellTick = cursorStep * kTicksPerStep;

    if (n == 0)
    {
        // no step here: change the defaults used for new steps
        switch (param)
        {
            case kParamProb: chance = clampInt(chance + delta * 5, 0, 100); break;
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge:
                nudge = clampInt(nudge + delta, -(kTicksPerStep / 2), kTicksPerStep / 2 - 1);
                break;
            case kParamPitch:
            {
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

    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, cursorTrack, targets[i]);
        switch (param)
        {
            case kParamProb: note.setChance(note.chance() + delta * 5); break;
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
            {
                // keep the note inside this step's half-step window
                int lo = clampInt(cellTick - kTicksPerStep / 2, 0, patternTicks - 1);
                int hi = clampInt(cellTick + kTicksPerStep / 2 - 1, 0, patternTicks - 1);
                note.start = (uint16_t)clampInt(note.start + delta, lo, hi);
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

    if (n == 0)
    {
        lengthTicks = clampInt(lengthTicks + delta * kTicksPerStep, minLen, maxLen);
        char msg[32];
        snprintf(msg, sizeof(msg), "New step length: %d.%d", lengthTicks / kTicksPerStep,
                 (lengthTicks % kTicksPerStep) * 10 / kTicksPerStep);
        mtPopups.show(0, msg, 1);
        return;
    }
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, cursorTrack, targets[i]);
        int len = note.length + delta * kTicksPerStep;
        // going down from one step lands on half a step
        if (note.length == kTicksPerStep && delta < 0)
            len = minLen;
        if (note.length == minLen && delta > 0)
            len = kTicksPerStep;
        note.length = (uint16_t)clampInt(len, minLen, maxLen);
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

    if (shiftActive())
    {
        toggleMute(track);
        return;
    }

    Sequencer::strPattern* p = current();
    uint8_t pitch = getSettings(p, track).baseNote;
    held[track] = true;
    heldPitch[track] = pitch;
    auditionOn(track, pitch, (uint8_t)velocity);

    if (g_recArmed && sequencer.isPlay())
    {
        // melodic tracks record how long the button is held, drum hits are one step
        bool hold = isMelodic(p, track);
        if (liveRecordOn(track, &pitch, 1, (uint8_t)velocity, hold))
        {
            heldRecording[track] = hold;
            markChanged();
        }
        else
        {
            mtPopups.show(0, "Track is full (125 notes)", 1);
        }
    }
    refresh();
}

void cGibGobHome::releaseAllButtons()
{
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
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (shiftActive())
            sequencer.playSong();
        else
            sequencer.playPattern();
    }
    else
    {
        GH->releaseAllButtons();
        sequencer.stop();
    }
    GH->refresh();
}

static void functRec(uint8_t state)
{
    if (state != buttonPress)
        return;
    g_recArmed = !g_recArmed;
    GH->refresh();
}

static void functUp(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (shiftActive())
    {
        if (s == buttonPress)
            GH->changePattern(-1);
        return;
    }
    GH->moveCursor(0, -1);
}

static void functDown(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (shiftActive())
    {
        if (s == buttonPress)
            GH->changePattern(1);
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
    else if (state == buttonRelease)
        auditionOff(GH->cursorTrack);
}

static void functInsert(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftActive())
        GH->openTrackPage();
    else
        GH->openPianoRoll();
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        GH->pressDelete();
}

static void functShift(uint8_t state)
{
    if (state != buttonPress)
        return;
    g_shiftLatched = !g_shiftLatched;
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
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        GH->param = cGibGobHome::kParamProb;
    else if (button == interfaceButtonInstr)
        GH->param = cGibGobHome::kParamVelocity;
    else if (button == interfaceButtonFx1)
        GH->param = cGibGobHome::kParamNudge;
    else if (button == interfaceButtonFx2)
        GH->param = cGibGobHome::kParamPitch;
    GH->refresh();
}

static void functEncoder(int16_t value)
{
    // the Mini's big +/- send +/-10, the small ones +/-1
    bool big = value >= 10 || value <= -10;
    int dir = value > 0 ? 1 : -1;
    if (shiftActive())
    {
        if (big)
            GH->changeZoom(-dir); // big + = zoom in (fewer steps on screen)
        else
            GH->changeSound(dir);
        return;
    }
    if (big)
        GH->changeLength(dir);
    else
        GH->changeParam(value);
}

static void functSwitchModule(uint8_t button)
{
    GH->eventFunct(eventSwitchModule, GH, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GH);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GH);
}
