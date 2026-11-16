// GibGob OS - piano roll editor for Melodic tracks
//
//   D-pad            move cursor (Shift: Up/Down = octave, Left/Right = 4 steps)
//                    while notes are selected: moves the selected notes
//   Middle           empty square: place ghost note/chord. On a note: grab it (select).
//                    While notes are grabbed: let go. Shift+Middle = auto-audition on/off
//   Insert           1st: start selection box, 2nd: lock (select notes inside), 3rd: let go
//   Delete           delete selection, or the note(s) under the cursor; cancels a box
//   Top row 1..8     chord for the ghost (Shift = extended chords); press again = single note
//   Note/Instr/FX1/FX2  choose what small +/- edits (colour-coded value boxes)
//   small +/-        edit chosen value
//   big +/-          note length      Shift + big +/- = zoom (one square per press)
//   Shift            toggles (red border while on)
//   Play             play/stop (Shift = song)      Pattern (Fn1) = main grid

#include "gibgob/pianoRoll.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cPianoRoll pianoRoll;
static cPianoRoll* PR = &pianoRoll;
static elapsedMillis s_clock;

using namespace gibgob;

static void functPlay(uint8_t state);
static void functRec(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functDelete(uint8_t state);
static void functInsert(uint8_t state);
static void functShift(uint8_t state);
static void functEncoder(int16_t value);
static void functTopRow(uint8_t button, uint8_t state);
static void functParamButton(uint8_t button, uint8_t state);
static void functPattern(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

static void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------
void cPianoRoll::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = 29;
    prop.w = 800;
    prop.h = 451;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<PianoRollControl>(&prop);
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
    clearSelection();
    mode = Mode::Idle;
    view.boxActive = false;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "Track %d  -  Piano Roll", track + 1);
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refreshView();
    display.synchronizeRefresh();
}

void cPianoRoll::stop()
{
    auditionOff(track);
    clearSelection();
    mode = Mode::Idle;
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
    ip->setButtonObj(interfaceButtonShift, functShift);
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

// ---------------------------------------------------------------------------
// view

void cPianoRoll::refreshView()
{
    const Sequencer::strPattern* p = current();
    view.pattern = p;
    view.track = track;
    view.shift = shiftActive();
    view.patternTicks = (p->track[track].length + 1) * kTicksPerStep;
    view.gridTicks = kTicksPerStep;
    view.stepsVisible = clampInt(16 + zoom, 4, 64);
    view.rowsVisible = clampInt(12 + zoom, 4, 36);

    // cursor inside the pattern, on the grid
    view.cursorTick = clampInt(view.cursorTick, 0, view.patternTicks - view.gridTicks);
    view.cursorTick -= view.cursorTick % view.gridTicks;
    view.cursorPitch = clampInt(view.cursorPitch, 0, 127);

    // scroll horizontally just enough to keep the cursor visible
    int vis = view.stepsVisible * view.gridTicks;
    if (view.cursorTick < view.firstTick)
        view.firstTick = view.cursorTick;
    if (view.cursorTick + view.gridTicks > view.firstTick + vis)
        view.firstTick = view.cursorTick + view.gridTicks - vis;
    view.firstTick = clampInt(view.firstTick, 0, view.patternTicks);
    view.firstTick -= view.firstTick % view.gridTicks;

    // scroll vertically
    if (view.cursorPitch > view.topPitch)
        view.topPitch = view.cursorPitch;
    if (view.cursorPitch < view.topPitch - view.rowsVisible + 1)
        view.topPitch = view.cursorPitch + view.rowsVisible - 1;
    view.topPitch = clampInt(view.topPitch, view.rowsVisible - 1, 127);

    // ghost
    view.showGhost = (mode == Mode::Idle);
    view.ghostLength = lengthTicks;
    view.ghostNudge = nudge;
    view.ghostCount = buildChord(chordId, view.cursorPitch, inversion, view.ghostPitches, 6);
    view.playheadTick = playheadTick(track);

    // box
    if (mode == Mode::BoxDrag)
    {
        view.boxActive = true;
        view.boxTick2 = view.cursorTick;
        view.boxPitch2 = view.cursorPitch;
    }
    else
    {
        view.boxActive = false;
    }

    // values shown: selection / note under cursor / ghost defaults
    int idx = -1;
    int count = noteCount(p, track);
    for (int i = 0; i < count && idx < 0; i++)
        if (getNote(p, track, i).flags & kNoteFlagSelected)
            idx = i;
    if (idx < 0)
        idx = findNoteAt(p, track, view.cursorTick, view.cursorPitch);

    int pitch = view.cursorPitch, vel = velocity, nud = nudge, len = lengthTicks;
    if (idx >= 0)
    {
        Note n = getNote(p, track, idx);
        pitch = n.pitch;
        vel = n.velocity;
        len = n.length;
        nud = n.start % kTicksPerStep;
        if (nud > kTicksPerStep / 2)
            nud -= kTicksPerStep;
    }

    snprintf(view.title,
             sizeof(view.title),
             "%s %s  Step %d  Len %d.%02d",
             mtNotes[pitch],
             chordName(chordId),
             view.cursorTick / kTicksPerStep + 1,
             len / kTicksPerStep,
             (len % kTicksPerStep) * 100 / kTicksPerStep);

    int sel = selectionCount();
    if (mode == Mode::BoxDrag)
        snprintf(view.mode, sizeof(view.mode), "BOX");
    else if (mode == Mode::Move)
        snprintf(view.mode, sizeof(view.mode), "MOVE %d", sel);
    else if (autoAudition)
        snprintf(view.mode, sizeof(view.mode), "AUD");
    else
        view.mode[0] = 0;

    snprintf(view.params[0].label, sizeof(view.params[0].label), "Note");
    snprintf(view.params[0].value, sizeof(view.params[0].value), "%s", mtNotes[pitch]);
    view.params[0].color = color::kNoteBtn;
    snprintf(view.params[1].label, sizeof(view.params[1].label), "Vel");
    snprintf(view.params[1].value, sizeof(view.params[1].value), "%d", vel);
    view.params[1].color = color::kInstrBtn;
    snprintf(view.params[2].label, sizeof(view.params[2].label), "Nudge");
    snprintf(view.params[2].value, sizeof(view.params[2].value), "%+d", nud);
    view.params[2].color = color::kFx1Btn;
    snprintf(view.params[3].label, sizeof(view.params[3].label), "Inv");
    snprintf(view.params[3].value, sizeof(view.params[3].value), "%d", inversion);
    view.params[3].color = color::kFx2Btn;
    view.selectedParam = param;

    // bottom bar: chord bank (Shift = extended)
    int bank = view.shift ? 1 : 0;
    for (int i = 0; i < 8; i++)
    {
        snprintf(barText[i], sizeof(barText[i]), "%s", kChordTypes[bank][i].label);
        view.bar.text[i] = barText[i];
        view.bar.fill[i] = bank ? color::kFx2Btn : color::kMelodic;
        view.bar.highlighted[i] = (chordId == bank * kChordButtons + i);
    }

    display.refreshControl(grid);
}

void cPianoRoll::update()
{
    if (auditionOffAt != 0 && (uint32_t)s_clock >= auditionOffAt)
    {
        auditionOff(track);
        auditionOffAt = 0;
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
// selection helpers

int cPianoRoll::selectionCount()
{
    const Sequencer::strPattern* p = current();
    int count = noteCount(p, track), n = 0;
    for (int i = 0; i < count; i++)
        if (getNote(p, track, i).flags & kNoteFlagSelected)
            n++;
    return n;
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
}

int cPianoRoll::selectUnderCursor()
{
    Sequencer::strPattern* p = current();
    int idx = findNoteAt(p, track, view.cursorTick, view.cursorPitch);
    if (idx < 0)
        return 0;
    Note n = getNote(p, track, idx);
    n.flags |= kNoteFlagSelected;
    setNote(p, track, idx, n);
    return 1;
}

int cPianoRoll::selectInBox()
{
    Sequencer::strPattern* p = current();
    int t1 = view.boxTick1 < view.boxTick2 ? view.boxTick1 : view.boxTick2;
    int t2 = (view.boxTick1 < view.boxTick2 ? view.boxTick2 : view.boxTick1) + view.gridTicks;
    int pHi = view.boxPitch1 > view.boxPitch2 ? view.boxPitch1 : view.boxPitch2;
    int pLo = view.boxPitch1 > view.boxPitch2 ? view.boxPitch2 : view.boxPitch1;

    int count = noteCount(p, track), selected = 0;
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        bool inside = n.pitch >= pLo && n.pitch <= pHi && n.start < t2 && n.start + n.length > t1;
        if (inside)
        {
            n.flags |= kNoteFlagSelected;
            setNote(p, track, i, n);
            selected++;
        }
    }
    return selected;
}

// indices to edit: selection, else note(s) of the ghost chord under the cursor
static int collectTargets(int* out, int maxOut)
{
    const Sequencer::strPattern* p = current();
    int count = noteCount(p, PR->track);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
        if (getNote(p, PR->track, i).flags & kNoteFlagSelected)
            out[n++] = i;
    if (n > 0 || PR->mode != cPianoRoll::Mode::Idle)
        return n;
    for (int g = 0; g < PR->view.ghostCount && n < maxOut; g++)
    {
        int idx = findNoteAt(p, PR->track, PR->view.cursorTick, PR->view.ghostPitches[g]);
        if (idx >= 0)
            out[n++] = idx;
    }
    return n;
}

// ---------------------------------------------------------------------------
// actions

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
    view.cursorPitch += dPitch;
    refreshView();
    if (autoAudition && dPitch != 0 && mode == Mode::Idle)
    {
        auditionGhost(true);
        auditionOffAt = (uint32_t)s_clock + 250;
    }
}

void cPianoRoll::moveSelection(int dTicks, int dPitch)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    if (n == 0)
        return;

    // limit the move so every selected note stays inside the pattern / pitch range
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        if (note.start + dTicks < 0)
            dTicks = -note.start;
        if (note.start + dTicks > view.patternTicks - 1)
            dTicks = view.patternTicks - 1 - note.start;
        if (note.pitch + dPitch < 0)
            dPitch = -note.pitch;
        if (note.pitch + dPitch > 127)
            dPitch = 127 - note.pitch;
    }
    if (dTicks == 0 && dPitch == 0)
        return;

    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        note.start = (uint16_t)(note.start + dTicks);
        note.pitch = (uint8_t)(note.pitch + dPitch);
        setNote(p, track, targets[i], note);
    }
    view.cursorTick += dTicks;
    view.cursorPitch += dPitch;
    markChanged();
    refreshView();

    if (dPitch != 0)
    {
        // let you hear where the grabbed note went
        Note first = getNote(p, track, targets[0]);
        auditionOn(track, first.pitch, first.velocity);
        auditionOffAt = (uint32_t)s_clock + 200;
    }
}

void cPianoRoll::pressUp(bool)
{
    int d = shiftActive() ? 12 : 1;
    if (mode == Mode::Move)
        moveSelection(0, d);
    else
        moveCursor(0, d);
}

void cPianoRoll::pressDown(bool)
{
    int d = shiftActive() ? -12 : -1;
    if (mode == Mode::Move)
        moveSelection(0, d);
    else
        moveCursor(0, d);
}

void cPianoRoll::pressLeft()
{
    int d = -view.gridTicks * (shiftActive() ? 4 : 1);
    if (mode == Mode::Move)
        moveSelection(d, 0);
    else
        moveCursor(d, 0);
}

void cPianoRoll::pressRight()
{
    int d = view.gridTicks * (shiftActive() ? 4 : 1);
    if (mode == Mode::Move)
        moveSelection(d, 0);
    else
        moveCursor(d, 0);
}

void cPianoRoll::placeGhost()
{
    Sequencer::strPattern* p = current();
    int start = clampInt(view.cursorTick + nudge, 0, view.patternTicks - 1);
    bool any = false;
    for (int g = 0; g < view.ghostCount; g++)
    {
        uint8_t pitch = view.ghostPitches[g];
        if (findNoteAt(p, track, view.cursorTick, pitch) >= 0)
            continue;
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
}

void cPianoRoll::pressMiddle()
{
    switch (mode)
    {
        case Mode::Idle:
            if (selectUnderCursor())
            {
                mode = Mode::Move; // grabbed a note
                Note n = getNote(current(), track,
                                 findNoteAt(current(), track, view.cursorTick, view.cursorPitch));
                auditionOn(track, n.pitch, n.velocity);
            }
            else
            {
                auditionGhost(true);
                placeGhost();
            }
            break;
        case Mode::Move:
            clearSelection(); // let go
            mode = Mode::Idle;
            break;
        case Mode::BoxDrag:
            pressInsert(); // middle also locks the box
            return;
    }
    refreshView();
}

void cPianoRoll::pressInsert()
{
    switch (mode)
    {
        case Mode::Idle:
            clearSelection();
            mode = Mode::BoxDrag;
            view.boxTick1 = view.cursorTick;
            view.boxPitch1 = view.cursorPitch;
            break;
        case Mode::BoxDrag:
            mode = selectInBox() > 0 ? Mode::Move : Mode::Idle;
            if (mode == Mode::Idle)
                mtPopups.show(0, "No notes inside the box", 1);
            break;
        case Mode::Move:
            clearSelection();
            mode = Mode::Idle;
            break;
    }
    refreshView();
}

void cPianoRoll::pressDelete()
{
    if (mode == Mode::BoxDrag)
    {
        mode = Mode::Idle;
        refreshView();
        return;
    }
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    // remove highest indices first so the others stay valid
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
    if (n > 0)
        markChanged();
    mode = Mode::Idle;
    refreshView();
}

void cPianoRoll::toggleShift()
{
    g_shiftLatched = !g_shiftLatched;
    refreshView();
}

void cPianoRoll::changeParam(int delta)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    int n = (param == kParamInversion) ? 0 : collectTargets(targets, kMaxNotes);

    if (n == 0)
    {
        switch (param)
        {
            case kParamNote: moveCursor(0, delta); return;
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge:
                nudge = clampInt(nudge + delta, -(kTicksPerStep / 2), kTicksPerStep / 2);
                break;
            case kParamInversion:
            {
                int c = chordNoteCount(chordId);
                inversion = (inversion + delta + c * 4) % c;
                break;
            }
        }
        refreshView();
        if (autoAudition || param == kParamInversion)
        {
            auditionGhost(true);
            auditionOffAt = (uint32_t)s_clock + 250;
        }
        return;
    }

    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        switch (param)
        {
            case kParamNote: note.pitch = (uint8_t)clampInt(note.pitch + delta, 0, 127); break;
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
                note.start = (uint16_t)clampInt(note.start + delta, 0, view.patternTicks - 1);
                break;
            default: break;
        }
        setNote(p, track, targets[i], note);
    }
    if (param == kParamNote && n == 1)
        view.cursorPitch = getNote(p, track, targets[0]).pitch;
    markChanged();
    refreshView();
}

void cPianoRoll::changeLength(int deltaGrid)
{
    Sequencer::strPattern* p = current();
    int step = view.gridTicks;
    int maxLen = Sequencer::MAXSTEP * kTicksPerStep;
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);

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
        lengthTicks = note.length;
    }
    markChanged();
    refreshView();
}

void cPianoRoll::changeZoom(int delta)
{
    zoom = clampInt(zoom + delta, -12, 48);
    refreshView();
}

void cPianoRoll::pressChordButton(int button)
{
    int id = (shiftActive() ? kChordButtons : 0) + button;
    chordId = (chordId == id) ? kChordNone : id;
    inversion %= chordNoteCount(chordId);
    refreshView();
    if (mode == Mode::Idle)
    {
        auditionGhost(true);
        auditionOffAt = (uint32_t)s_clock + 300;
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
        sequencer.stop();
    }
}

static void functRec(uint8_t state)
{
    if (state == buttonPress)
        mtPopups.show(0, "Live recording to piano roll: coming soon", 1);
}

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressUp(s == buttonHold);
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressDown(s == buttonHold);
}
static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressLeft();
}
static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressRight();
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
    {
        if (shiftActive())
        {
            PR->autoAudition = !PR->autoAudition;
            mtPopups.show(0, PR->autoAudition ? "Auto audition ON" : "Auto audition OFF", 1);
            PR->refreshView();
            return;
        }
        PR->auditionOffAt = 0;
        PR->pressMiddle();
    }
    else if (state == buttonRelease)
    {
        auditionOff(PR->track);
    }
}

static void functDelete(uint8_t state)
{
    if (state == buttonPress)
        PR->pressDelete();
}

static void functInsert(uint8_t state)
{
    if (state == buttonPress)
        PR->pressInsert();
}

static void functShift(uint8_t state)
{
    if (state == buttonPress)
        PR->toggleShift();
}

static void functEncoder(int16_t value)
{
    // the Mini's big +/- send +/-10, the small ones +/-1
    bool big = value >= 10 || value <= -10;
    int dir = value > 0 ? 1 : -1;
    if (big && shiftActive())
        PR->changeZoom(-dir); // big + = zoom in (fewer squares), big - = zoom out
    else if (big)
        PR->changeLength(dir);
    else
        PR->changeParam(value);
}

static void functTopRow(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    int idx = button - interfaceButton0;
    if (idx >= 0 && idx < kChordButtons)
        PR->pressChordButton(idx);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        PR->param = cPianoRoll::kParamNote;
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
