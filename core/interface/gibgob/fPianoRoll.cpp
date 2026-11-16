// GibGob OS - piano roll editor for Melodic tracks
//
// Controls
//   D-pad          move ghost cursor (Shift+Up/Down = octave, Shift+Left/Right = zoom)
//   Middle         place ghost note/chord (auditions). Shift+Middle = auto-audition on/off
//   Top row 1..8   chord type for the ghost (press again to go back to single note)
//   Note/Instr/FX1/FX2  choose what small +/- edits: Pitch / Velocity / Nudge / Inversion
//   small +/-      edit chosen value (selection > note under cursor > ghost defaults)
//   big +/-        note length
//   Delete         delete note(s) under cursor, or all selected notes
//   Insert         select / unselect note under cursor (Shift+Insert = clear selection)
//   Play           play / stop pattern (Shift+Play = song)
//   Pattern (Fn1)  back to the GibGob main grid

#include "gibgob/pianoRoll.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cPianoRoll pianoRoll;
static cPianoRoll* PR = &pianoRoll;

static elapsedMillis s_clock;

using namespace gibgob;

static const char* const kParamNames[cPianoRoll::kParamCount] = { "Pitch", "Velocity", "Nudge",
                                                                    "Inversion" };

// ---------------------------------------------------------------------------
static void functPlay(uint8_t state);
static void functRec(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functDelete(uint8_t state);
static void functInsert(uint8_t state);
static void functEncoder(int16_t value);
static void functTopRow(uint8_t button, uint8_t state);
static void functParamButton(uint8_t button, uint8_t state);
static void functPattern(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

static bool shiftHeld()
{
    return PR->buttons->isButtonPressed(interfaceButtonShift);
}

static void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

// ---------------------------------------------------------------------------
void cPianoRoll::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = 29;
    prop.w = 800;
    prop.h = 394;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<PianoRollControl>(&prop);

    strControlProperties prop2;
    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    for (uint8_t i = 0; i < 8; i++)
    {
        strControlProperties lp;
        lp.colors = display.getTheme().activeLabelsColors;
        lp.text = labelText[i];
        lp.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        lp.x = (800 / 8) * i + (800 / 16);
        lp.y = 424;
        lp.w = 800 / 8 - 6;
        lp.h = 55;
        label[i] = display.createControl<cLabel>(&lp);
    }
}

void cPianoRoll::destroyDisplayControls()
{
    // controls are destroyed by Interface::deactivateModule()
}

void cPianoRoll::start(uint32_t options)
{
    moduleRefresh = 1;
    track = (int)(options % kTracks);

    Sequencer::strPattern* p = current();
    if (!isMelodic(p, track))
    {
        setTrackType(p, track, TrackType::Melodic);
        markChanged();
    }

    if (view.cursorPitch < 0 || view.cursorPitch > 127)
        view.cursorPitch = 60;

    songTimer.hide();
    showDefaultScreen();
    setDefaultScreenFunct();
    refreshView();
}

void cPianoRoll::stop()
{
    auditionOff(track);
    moduleRefresh = 0;
}

void cPianoRoll::setDefaultScreenFunct()
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
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setButtonObj(interfaceButtonInsert, functInsert);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    ip->setButtonObj(interfaceButtonNote, functParamButton);
    ip->setButtonObj(interfaceButtonInstr, functParamButton);
    ip->setButtonObj(interfaceButtonFx1, functParamButton);
    ip->setButtonObj(interfaceButtonFx2, functParamButton);

    ip->setButtonObj(interfaceButtonPattern, functPattern);

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cPianoRoll::showDefaultScreen()
{
    snprintf(titleText, sizeof(titleText), "Track %d  Piano Roll", track + 1);
    titleBar->show();
    titleBar->setTitle(titleText);
    refreshLabels();
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cPianoRoll::refreshLabels()
{
    for (int i = 0; i < 8; i++)
    {
        if (i == chordIdx)
            snprintf(labelText[i], sizeof(labelText[i]), "[%s]", kChordTypes[i].label);
        else
            snprintf(labelText[i], sizeof(labelText[i]), "%s", kChordTypes[i].label);
        display.setControlText(label[i], labelText[i]);
        display.refreshControl(label[i]);
    }
}

// ---------------------------------------------------------------------------
// view

static int zoomSteps(int z)
{
    return z == 0 ? 8 : (z == 1 ? 16 : 32);
}

void cPianoRoll::refreshView()
{
    const Sequencer::strPattern* p = current();
    view.pattern = p;
    view.track = track;
    view.patternTicks = (p->track[track].length + 1) * kTicksPerStep;
    view.ticksVisible = zoomSteps(zoomLevel) * kTicksPerStep;
    view.gridTicks = zoomLevel == 0 ? kTicksPerStep / 2 : kTicksPerStep;

    // keep cursor inside the pattern and on the grid
    if (view.cursorTick >= view.patternTicks)
        view.cursorTick = view.patternTicks - view.gridTicks;
    if (view.cursorTick < 0)
        view.cursorTick = 0;
    view.cursorTick -= view.cursorTick % view.gridTicks;

    // page horizontally
    view.firstTick = (view.cursorTick / view.ticksVisible) * view.ticksVisible;

    // scroll vertically to keep the cursor visible
    if (view.cursorPitch > view.topPitch)
        view.topPitch = view.cursorPitch;
    if (view.cursorPitch < view.topPitch - view.rowsVisible + 1)
        view.topPitch = view.cursorPitch + view.rowsVisible - 1;
    if (view.topPitch > 127)
        view.topPitch = 127;
    if (view.topPitch < view.rowsVisible - 1)
        view.topPitch = view.rowsVisible - 1;

    view.ghostLength = lengthTicks;
    view.ghostNudge = nudge;
    view.ghostCount = buildChord(chordIdx, view.cursorPitch, inversion, view.ghostPitches, 6);
    view.playheadTick = playheadTick(track);

    // info strip text
    int under = findNoteAt(p, track, view.cursorTick, view.cursorPitch);
    int vel = velocity, nud = nudge, len = lengthTicks;
    if (under >= 0)
    {
        Note n = getNote(p, track, under);
        vel = n.velocity;
        len = n.length;
        nud = n.start - view.cursorTick;
    }
    int selCount = 0;
    for (int i = 0; i < noteCount(p, track); i++)
        if (getNote(p, track, i).flags & kNoteFlagSelected)
            selCount++;

    snprintf(view.info,
             sizeof(view.info),
             "%s %s  Vel %d  Len %d.%02d  Nudge %+d  Inv %d  |  Edit: %s%s%s",
             kChordTypes[chordIdx].label,
             mtNotes[view.cursorPitch],
             vel,
             len / kTicksPerStep,
             (len % kTicksPerStep) * 100 / kTicksPerStep,
             nud,
             inversion,
             kParamNames[param],
             autoAudition ? "  | Audition" : "",
             selCount ? "  | Sel" : "");

    display.refreshControl(grid);
}

void cPianoRoll::update()
{
    if (autoAuditionOffAt != 0 && (uint32_t)s_clock >= autoAuditionOffAt)
    {
        auditionOff(track);
        autoAuditionOffAt = 0;
    }

    int ph = playheadTick(track);
    int phStep = ph < 0 ? -1 : ph / (kTicksPerStep / 4);
    if (phStep != lastPlayheadDrawn)
    {
        lastPlayheadDrawn = phStep;
        view.playheadTick = ph;
        display.refreshControl(grid);
    }
}

// ---------------------------------------------------------------------------
// editing helpers

// indices of notes to edit: selection first, otherwise the note under the cursor
static int collectTargets(int* out, int maxOut)
{
    const Sequencer::strPattern* p = current();
    int count = noteCount(p, PR->track);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
        if (getNote(p, PR->track, i).flags & kNoteFlagSelected)
            out[n++] = i;
    if (n > 0)
        return n;

    // all notes of the ghost chord at the cursor
    for (int g = 0; g < PR->view.ghostCount && n < maxOut; g++)
    {
        int idx = findNoteAt(p, PR->track, PR->view.cursorTick, PR->view.ghostPitches[g]);
        if (idx >= 0)
            out[n++] = idx;
    }
    return n;
}

void cPianoRoll::auditionGhost(bool on)
{
    if (on)
        auditionChord(track, view.ghostPitches, view.ghostCount, (uint8_t)velocity);
    else
        auditionOff(track);
}

void cPianoRoll::moveCursor(int dTicks, int dPitch)
{
    view.cursorTick += dTicks;
    if (view.cursorTick < 0)
        view.cursorTick = 0;
    view.cursorPitch += dPitch;
    if (view.cursorPitch < 0)
        view.cursorPitch = 0;
    if (view.cursorPitch > 127)
        view.cursorPitch = 127;
    refreshView();

    if (autoAudition && dPitch != 0)
    {
        auditionGhost(true);
        autoAuditionOffAt = (uint32_t)s_clock + 250;
    }
}

void cPianoRoll::placeGhost()
{
    Sequencer::strPattern* p = current();
    int start = view.cursorTick + nudge;
    if (start < 0)
        start = 0;
    if (start >= view.patternTicks)
        start = view.patternTicks - 1;

    bool any = false;
    for (int g = 0; g < view.ghostCount; g++)
    {
        uint8_t pitch = view.ghostPitches[g];
        if (findNoteAt(p, track, view.cursorTick, pitch) >= 0)
            continue; // already there
        Note n{};
        n.start = (uint16_t)start;
        n.length = (uint16_t)lengthTicks;
        n.pitch = pitch;
        n.velocity = (uint8_t)velocity;
        if (addNote(p, track, n) < 0)
        {
            mtPopups.show(0, "Track is full (127 notes)", 1);
            break;
        }
        any = true;
    }
    if (any)
        markChanged();
    refreshView();
}

void cPianoRoll::deleteAtCursor()
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    if (n == 0)
        return;
    // remove from highest index down so indices stay valid
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (targets[b] > targets[a])
            {
                int t = targets[a];
                targets[a] = targets[b];
                targets[b] = t;
            }
    for (int i = 0; i < n; i++)
        removeNote(p, track, targets[i]);
    markChanged();
    refreshView();
}

void cPianoRoll::toggleSelectAtCursor()
{
    Sequencer::strPattern* p = current();
    int idx = findNoteAt(p, track, view.cursorTick, view.cursorPitch);
    if (idx < 0)
        return;
    Note n = getNote(p, track, idx);
    n.flags ^= kNoteFlagSelected;
    setNote(p, track, idx, n);
    refreshView();
}

void cPianoRoll::clearSelection()
{
    Sequencer::strPattern* p = current();
    int count = noteCount(p, track);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        if (n.flags & kNoteFlagSelected)
        {
            n.flags &= ~kNoteFlagSelected;
            setNote(p, track, i, n);
        }
    }
    refreshView();
}

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void cPianoRoll::changeParam(int delta)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = (param == kParamInversion) ? 0 : collectTargets(targets, kMaxNotes);

    if (n == 0)
    {
        // edit the ghost / defaults for new notes
        switch (param)
        {
            case kParamPitch: moveCursor(0, delta); return;
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge:
                nudge = clampInt(nudge + delta, -(kTicksPerStep - 1), kTicksPerStep - 1);
                break;
            case kParamInversion:
                inversion = clampInt(inversion + delta, 0, 5);
                if (kChordTypes[chordIdx].count > 0)
                    inversion %= kChordTypes[chordIdx].count;
                break;
        }
        refreshView();
        if (autoAudition)
        {
            auditionGhost(true);
            autoAuditionOffAt = (uint32_t)s_clock + 250;
        }
        return;
    }

    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        switch (param)
        {
            case kParamPitch: note.pitch = (uint8_t)clampInt(note.pitch + delta, 0, 127); break;
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
                note.start = (uint16_t)clampInt(note.start + delta, 0, view.patternTicks - 1);
                break;
            default: break;
        }
        setNote(p, track, targets[i], note);
    }

    // follow a single edited note with the cursor so it stays "under" it
    if (n == 1 && param == kParamPitch)
    {
        view.cursorPitch = getNote(p, track, targets[0]).pitch;
    }
    markChanged();
    refreshView();
}

void cPianoRoll::changeLength(int deltaGrid)
{
    Sequencer::strPattern* p = current();
    int step = view.gridTicks;
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    int maxLen = Sequencer::MAXSTEP * kTicksPerStep;

    if (n == 0)
    {
        lengthTicks = clampInt(lengthTicks + deltaGrid * step, step / 2, maxLen);
        refreshView();
        return;
    }
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        note.length = (uint16_t)clampInt(note.length + deltaGrid * step, step / 2, maxLen);
        setNote(p, track, targets[i], note);
        lengthTicks = note.length; // new notes copy the last edited length
    }
    markChanged();
    refreshView();
}

void cPianoRoll::setChord(int idx)
{
    chordIdx = (chordIdx == idx) ? 0 : idx;
    if (kChordTypes[chordIdx].count > 0)
        inversion %= kChordTypes[chordIdx].count;
    refreshLabels();
    refreshView();
    auditionGhost(true);
    autoAuditionOffAt = (uint32_t)s_clock + 300;
}

void cPianoRoll::setZoom(int dir)
{
    zoomLevel = clampInt(zoomLevel + dir, 0, 2);
    refreshView();
}

// ---------------------------------------------------------------------------
// button handlers

static bool isPressOrRepeat(uint8_t state)
{
    return state == buttonPress || state == buttonHold;
}

static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (shiftHeld())
            sequencer.playSong();
        else
            sequencer.playPattern();
    }
    else
    {
        sequencer.stop();
    }
}

static void functRec(uint8_t state)
{
    if (state == buttonPress)
        mtPopups.show(0, "Live recording to piano roll: coming soon", 1);
}

static void functUp(uint8_t state)
{
    if (!isPressOrRepeat(state))
        return;
    PR->moveCursor(0, shiftHeld() ? 12 : 1);
}

static void functDown(uint8_t state)
{
    if (!isPressOrRepeat(state))
        return;
    PR->moveCursor(0, shiftHeld() ? -12 : -1);
}

static void functLeft(uint8_t state)
{
    if (!isPressOrRepeat(state))
        return;
    if (shiftHeld())
    {
        if (state == buttonPress)
            PR->setZoom(-1);
        return;
    }
    PR->moveCursor(-PR->view.gridTicks, 0);
}

static void functRight(uint8_t state)
{
    if (!isPressOrRepeat(state))
        return;
    if (shiftHeld())
    {
        if (state == buttonPress)
            PR->setZoom(1);
        return;
    }
    if (PR->view.cursorTick + PR->view.gridTicks < PR->view.patternTicks)
        PR->moveCursor(PR->view.gridTicks, 0);
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
    {
        if (shiftHeld())
        {
            PR->autoAudition = !PR->autoAudition;
            mtPopups.show(0, PR->autoAudition ? "Auto audition ON" : "Auto audition OFF", 1);
            PR->refreshView();
            return;
        }
        PR->autoAuditionOffAt = 0;
        PR->auditionGhost(true);
        if (findNoteAt(current(), PR->track, PR->view.cursorTick, PR->view.cursorPitch) < 0)
            PR->placeGhost();
    }
    else if (state == buttonRelease)
    {
        PR->auditionGhost(false);
    }
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        PR->deleteAtCursor();
}

static void functInsert(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftHeld())
        PR->clearSelection();
    else
        PR->toggleSelectAtCursor();
}

static void functEncoder(int16_t value)
{
    // the Mini's big +/- send +/-10, the small ones +/-1
    if (value >= 10 || value <= -10)
        PR->changeLength(value > 0 ? 1 : -1);
    else
        PR->changeParam(value);
}

static void functTopRow(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    int idx = button - interfaceButton0;
    if (idx >= 0 && idx < kChordButtons)
        PR->setChord(idx);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        PR->param = cPianoRoll::kParamPitch;
    else if (button == interfaceButtonInstr)
        PR->param = cPianoRoll::kParamVelocity;
    else if (button == interfaceButtonFx1)
        PR->param = cPianoRoll::kParamNudge;
    else if (button == interfaceButtonFx2)
        PR->param = cPianoRoll::kParamInversion;
    PR->refreshView();
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    int t = PR->track;
    mtInterface.deactivateModule(PR);
    mtInterface.activateModule(&gibgobHome, (uint32_t)t);
}

static void functSwitchModule(uint8_t button)
{
    PR->eventFunct(eventSwitchModule, PR, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(PR);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(PR);
}
