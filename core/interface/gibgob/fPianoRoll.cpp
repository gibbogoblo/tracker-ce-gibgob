// GibGob OS - piano roll editor (any track)
//
//   D-pad            move cursor (Shift: Up/Down = octave, Left/Right = 4 steps)
//                    while notes are selected: moves the selected notes
//   Middle           empty square: place ghost note/chord. On a note: grab it (select).
//                    While notes are grabbed: let go. Shift+Middle = auto-audition on/off
//   Insert           1st: start selection box, 2nd: lock (select notes inside), 3rd: let go
//                    Shift+Insert = track page (sound, arp, Go Ape, chord palette)
//   Delete           delete selection, or the note(s) under the cursor; cancels a box
//   Top row 1..8     chord palette slot for the ghost (Shift = second bank); press again
//                    for a single note. Sliced samples: the 8 buttons play slices.
//                    While Rec is on and playing, what you play is recorded.
//   Note/Instr/FX1/FX2  choose what small +/- edits: Probability / Velocity / Nudge / Inversion
//   small +/-        edit chosen value
//   big +/-          note length      Shift + big +/- = zoom (one square per press)
//   Rec              toggle live recording        Play = play / stop (Shift = song)
//   Shift            toggles (red border while on)   Pattern (Fn1) = main sequencer

#include "gibgob/pianoRoll.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobPages.h"
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
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<PianoRollControl>(&prop);
}

void cPianoRoll::start(uint32_t options)
{
    moduleRefresh = 1;
    track = (int)(options % kTracks);

    for (int i = 0; i < 8; i++)
    {
        held[i] = false;
        heldRecording[i] = false;
        heldCount[i] = 0;
    }
    clearSelection();
    mode = Mode::Idle;
    view.boxActive = false;
    auditionOffAt = 0;
    lastPlayheadDrawn = -2;

    if (padsMode())
    {
        // sliced sample: start on the slices
        chordSlot = -1;
        if (view.cursorPitch < FIRST_BEAT_SLICE_NOTE || view.cursorPitch > FIRST_BEAT_SLICE_NOTE + 47)
            view.cursorPitch = FIRST_BEAT_SLICE_NOTE;
        view.topPitch = view.cursorPitch + 8;
    }

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
    releaseAllButtons();
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
    bindModuleSwitchButtons(ip, functSwitchModule);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
bool cPianoRoll::padsMode()
{
    uint8_t sound = getSound(current(), track);
    if (!isSampleInstrument(sound))
        return false;
    const strInstrument& in = mtProject.instrument[sound];
    return in.isActive == 1 && in.playMode == playModeBeatSlice;
}

int cPianoRoll::chordNoteCount()
{
    if (chordSlot < 0)
        return 1;
    return chordType(getPaletteChord(current(), chordSlot).type).count;
}

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
    if (view.firstTick + vis > view.patternTicks)
        view.firstTick = view.patternTicks - vis;
    if (view.firstTick < 0)
        view.firstTick = 0;
    view.firstTick -= view.firstTick % view.gridTicks;

    // scroll vertically
    if (view.cursorPitch > view.topPitch)
        view.topPitch = view.cursorPitch;
    if (view.cursorPitch < view.topPitch - view.rowsVisible + 1)
        view.topPitch = view.cursorPitch + view.rowsVisible - 1;
    view.topPitch = clampInt(view.topPitch, view.rowsVisible - 1, 127);

    // ghost: a single note, or the palette chord's shape on the cursor note
    bool pads = padsMode();
    if (pads)
        chordSlot = -1;
    view.showGhost = (mode == Mode::Idle);
    view.ghostLength = lengthTicks;
    view.ghostNudge = nudge;
    PaletteChord ghostChord{};
    if (chordSlot >= 0)
    {
        ghostChord = getPaletteChord(p, chordSlot);
        view.ghostCount = buildChordNotes(ghostChord.type, view.cursorPitch, inversion, ghostChord.voicing,
                                          view.ghostPitches, kMaxChordNotes);
        if (view.ghostCount == 0)
        {
            view.ghostPitches[0] = (uint8_t)view.cursorPitch;
            view.ghostCount = 1;
        }
    }
    else
    {
        view.ghostPitches[0] = (uint8_t)view.cursorPitch;
        view.ghostCount = 1;
    }
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

    // values shown: selection / note under cursor / defaults for new notes
    int idx = -1;
    int count = noteCount(p, track);
    int sel = 0;
    for (int i = 0; i < count; i++)
    {
        if (getNote(p, track, i).flags & kNoteFlagSelected)
        {
            if (idx < 0)
                idx = i;
            sel++;
        }
    }
    if (idx < 0)
        idx = findNoteAt(p, track, view.cursorTick, view.cursorPitch);

    int pitch = view.cursorPitch, vel = velocity, nud = nudge, len = lengthTicks, chn = chance;
    if (idx >= 0)
    {
        Note n = getNote(p, track, idx);
        pitch = n.pitch;
        vel = n.velocity;
        chn = n.chance();
        len = n.length;
        nud = n.start % kTicksPerStep;
        if (nud >= kTicksPerStep / 2)
            nud -= kTicksPerStep;
    }

    snprintf(view.title, sizeof(view.title), "%s %s  Step %d  Len %d.%02d", mtNotes[pitch],
             chordSlot >= 0 ? chordType(ghostChord.type).label : (pads ? "Slice" : "Note"),
             view.cursorTick / kTicksPerStep + 1, len / kTicksPerStep,
             (len % kTicksPerStep) * 100 / kTicksPerStep);

    view.modeColor = color::kBox;
    if (g_recArmed)
    {
        snprintf(view.mode, sizeof(view.mode), "REC");
        view.modeColor = color::kRec;
    }
    else if (mode == Mode::BoxDrag)
        snprintf(view.mode, sizeof(view.mode), "BOX");
    else if (mode == Mode::Move)
        snprintf(view.mode, sizeof(view.mode), "MOVE %d", sel);
    else if (autoAudition)
        snprintf(view.mode, sizeof(view.mode), "AUD");
    else
        view.mode[0] = 0;

    snprintf(view.params[0].label, sizeof(view.params[0].label), "Prob");
    snprintf(view.params[0].value, sizeof(view.params[0].value), "%d%%", chn);
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

    // bottom bar: slices for a sliced sample, otherwise the chord palette
    int bank = view.shift ? 1 : 0;
    for (int i = 0; i < 8; i++)
    {
        int slot = bank * 8 + i;
        if (pads)
        {
            snprintf(barText[i], sizeof(barText[i]), "Slice %d", slot + 1);
            view.bar.fill[i] = color::kDrum;
            view.bar.highlighted[i] = (view.cursorPitch == FIRST_BEAT_SLICE_NOTE + slot);
        }
        else
        {
            paletteChordName(getPaletteChord(p, slot), barText[i], sizeof(barText[i]));
            view.bar.fill[i] = bank ? color::kFx2Btn : color::kMelodic;
            view.bar.highlighted[i] = (chordSlot == slot);
        }
        view.bar.text[i] = barText[i];
    }

    display.refreshControl(grid);
}

void cPianoRoll::update()
{
    if (auditionOffAt != 0 && (uint32_t)s_clock >= auditionOffAt)
    {
        auditionOffAt = 0;
        bool anyHeld = false;
        for (int i = 0; i < 8; i++)
            anyHeld = anyHeld || held[i];
        if (!anyHeld)
            auditionOff(track);
    }

    int ph = playheadTick(track);
    int phStep = ph < 0 ? -1 : ph / (kTicksPerStep / 4);
    if (phStep != lastPlayheadDrawn)
    {
        lastPlayheadDrawn = phStep;
        // live-recorded notes appear as they are played
        refreshView();
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
            n.flags &= (uint8_t)~kNoteFlagSelected;
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

// indices to edit: the selection, else the note(s) of the ghost under the cursor
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
        if (idx < 0)
            continue;
        bool dup = false;
        for (int k = 0; k < n; k++)
            dup = dup || out[k] == idx;
        if (!dup)
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

void cPianoRoll::pressUp()
{
    int d = shiftActive() ? 12 : 1;
    if (mode == Mode::Move)
        moveSelection(0, d);
    else
        moveCursor(0, d);
}

void cPianoRoll::pressDown()
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
        n.setChance(chance);
        if (addNote(p, track, n) < 0)
        {
            mtPopups.show(0, "Track is full (125 notes)", 1);
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
        {
            int idx = findNoteAt(current(), track, view.cursorTick, view.cursorPitch);
            if (idx >= 0 && selectUnderCursor())
            {
                mode = Mode::Move; // grabbed a note
                Note n = getNote(current(), track, idx);
                auditionOn(track, n.pitch, n.velocity);
            }
            else
            {
                auditionGhost(true);
                placeGhost();
            }
            break;
        }
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
        // nothing under the cursor: change what the next placed note gets
        switch (param)
        {
            case kParamProb: chance = clampInt(chance + delta * 5, 0, 100); break;
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge:
                nudge = clampInt(nudge + delta, -(kTicksPerStep / 2), kTicksPerStep / 2 - 1);
                break;
            case kParamInversion:
            {
                int c = chordNoteCount();
                inversion = ((inversion + delta) % c + c) % c;
                break;
            }
            default: break;
        }
        refreshView();
        if (mode == Mode::Idle && (autoAudition || param == kParamInversion))
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
            case kParamProb: note.setChance(note.chance() + delta * 5); break;
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
                note.start = (uint16_t)clampInt(note.start + delta, 0, view.patternTicks - 1);
                break;
            default: break;
        }
        setNote(p, track, targets[i], note);
    }
    markChanged();
    refreshView();
}

void cPianoRoll::changeLength(int deltaGrid)
{
    Sequencer::strPattern* p = current();
    int step = view.gridTicks;
    int minLen = step / 2;
    int maxLen = Sequencer::MAXSTEP * kTicksPerStep;
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);

    auto stepLength = [&](int len) {
        // half a step is the shortest; from there lengths move in whole steps
        if (len <= minLen && deltaGrid > 0)
            return step;
        if (len == step && deltaGrid < 0)
            return minLen;
        return clampInt(len + deltaGrid * step, minLen, maxLen);
    };

    if (n == 0)
    {
        lengthTicks = stepLength(lengthTicks);
        refreshView();
        return;
    }
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, targets[i]);
        note.length = (uint16_t)stepLength(note.length);
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

void cPianoRoll::topButton(int button, bool pressed)
{
    if (button < 0 || button >= 8)
        return;

    if (!pressed)
    {
        if (!held[button])
            return;
        held[button] = false;
        if (heldRecording[button])
        {
            liveRecordOff(track, heldNotes[button], heldCount[button]);
            heldRecording[button] = false;
            markChanged();
        }
        bool anyHeld = false;
        for (int i = 0; i < 8; i++)
            anyHeld = anyHeld || held[i];
        if (!anyHeld && auditionOffAt == 0)
            auditionOff(track);
        refreshView();
        return;
    }

    Sequencer::strPattern* p = current();
    bool recording = g_recArmed && sequencer.isPlay();
    bool pads = padsMode();
    int slot = (shiftActive() ? 8 : 0) + button;

    uint8_t notes[kMaxChordNotes];
    int n = 0;
    if (pads)
    {
        int pitch = clampInt(FIRST_BEAT_SLICE_NOTE + slot, 0, 127);
        notes[0] = (uint8_t)pitch;
        n = 1;
        chordSlot = -1;
        view.cursorPitch = pitch;
    }
    else
    {
        if (chordSlot == slot && !recording)
        {
            // pressing the selected chord again goes back to a single note
            chordSlot = -1;
            inversion = 0;
            refreshView();
            return;
        }
        PaletteChord c = getPaletteChord(p, slot);
        chordSlot = slot;
        inversion = c.inversion;
        view.cursorPitch = c.root;
        n = buildPaletteChord(c, notes, kMaxChordNotes);
        if (n == 0)
        {
            notes[0] = c.root;
            n = 1;
        }
    }

    held[button] = true;
    heldCount[button] = (uint8_t)n;
    for (int i = 0; i < n; i++)
        heldNotes[button][i] = notes[i];

    auditionOffAt = 0;
    auditionChord(track, notes, n, (uint8_t)velocity);

    if (recording)
    {
        // slices on a drum track are single hits; everything else records its held length
        bool hold = !(pads && !isMelodic(p, track));
        if (liveRecordOn(track, notes, n, (uint8_t)velocity, hold))
        {
            heldRecording[button] = hold;
            markChanged();
        }
        else
        {
            mtPopups.show(0, "Track is full (125 notes)", 1);
        }
    }
    refreshView();
}

void cPianoRoll::releaseAllButtons()
{
    for (int i = 0; i < 8; i++)
    {
        if (!held[i])
            continue;
        held[i] = false;
        if (heldRecording[i])
        {
            liveRecordOff(track, heldNotes[i], heldCount[i]);
            heldRecording[i] = false;
            markChanged();
        }
    }
    auditionOff(track);
}

void cPianoRoll::openTrackPage()
{
    uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromPianoRoll, track);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
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
        PR->releaseAllButtons();
        sequencer.stop();
    }
    PR->refreshView();
}

static void functRec(uint8_t state)
{
    if (state != buttonPress)
        return;
    g_recArmed = !g_recArmed;
    PR->refreshView();
}

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressUp();
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s))
        PR->pressDown();
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
        bool anyHeld = false;
        for (int i = 0; i < 8; i++)
            anyHeld = anyHeld || PR->held[i];
        if (!anyHeld && PR->auditionOffAt == 0)
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
    if (state != buttonPress)
        return;
    if (shiftActive())
        PR->openTrackPage();
    else
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
    int idx = button - interfaceButton0;
    if (state == buttonPress)
        PR->topButton(idx, true);
    else if (state == buttonRelease)
        PR->topButton(idx, false);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        PR->param = cPianoRoll::kParamProb;
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
