// GibGob OS - piano roll editor (melodic tracks)
// Every shortcut is listed in gibgobHelp.h (Shift + Menu shows them).

#include "gibgob/pianoRoll.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobHelp.h"
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
static void functParams(uint8_t state);
static void functCopy(uint8_t state);

static bool s_otherTracks = false; // ghost notes (Shift + top 3)

static void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

enum EditKind
{
    kEditPlace = 20,
    kEditMove,
    kEditDelete,
    kEditParam,
    kEditLength,
    kEditRecord,
    kEditTool,
    kEditClear,
};

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
    screenOpened();
    track = (int)(options % kTracks);
    view.help.active = false;
    view.help.title = "Piano roll - shortcuts";
    view.help.lines = kHelpPianoRoll;
    view.help.count = (int)(sizeof(kHelpPianoRoll) / sizeof(kHelpPianoRoll[0]));

    for (int i = 0; i < 8; i++)
    {
        held[i] = false;
        heldRecording[i] = false;
        heldCount[i] = 0;
    }
    clearSelection();
    mode = Mode::Idle;
    menu = Menu::None;
    paramHeld = false;
    view.menu.active = false;
    view.picker.active = false;
    view.boxActive = false;
    auditionOffAt = 0;
    lastPlayheadDrawn = -2;

    if (padsMode())
    {
        // sliced sample: start on the slices
        chordSlot = -1;
        int n = sliceCount(current(), track);
        if (view.cursorPitch < kFirstSliceNote || view.cursorPitch > kFirstSliceNote + n - 1)
            view.cursorPitch = kFirstSliceNote;
        view.topPitch = kFirstSliceNote + n - 1;
    }

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), padsMode() ? "Track %d  -  Slices" : "Track %d  -  Piano Roll",
             track + 1);
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refreshView();
    display.synchronizeRefresh();
}

void cPianoRoll::stop()
{
    if (menu != Menu::None)
        closeMenu(false);
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
    ip->setButtonObj(interfaceButtonParams, functParams);
    ip->setButtonObj(interfaceButtonCopy, functCopy);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
bool cPianoRoll::padsMode()
{
    // slice track (beat-slice sample): one row per slice
    return isSliceTrack(current(), track);
}

bool cPianoRoll::singMode()
{
    return !padsMode() && g_singsSyllables != nullptr && g_singsSyllables(getSound(current(), track));
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
    view.patternTicks = trackSteps(p, track) * kTicksPerStep;
    view.gridTicks = kTicksPerStep;
    view.stepsVisible = clampInt(16 + zoom, 4, 64);
    view.rowsVisible = clampInt(12 + zoom, 4, 36);

    // cursor inside the pattern, on the grid
    view.cursorTick = clampInt(view.cursorTick, 0, view.patternTicks - view.gridTicks);
    view.cursorTick -= view.cursorTick % view.gridTicks;
    view.cursorPitch = clampInt(view.cursorPitch, 0, 127);
    const int slices = sliceCount(p, track);
    view.sliceCount = slices;
    view.otherTracks = s_otherTracks && slices == 0;
    if (slices > 0)
        view.cursorPitch = clampInt(view.cursorPitch, kFirstSliceNote, kFirstSliceNote + slices - 1);

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
    if (slices > 0)
    {
        // never scroll past the last slice (rows below the first are empty)
        int top = kFirstSliceNote + slices - 1;
        if (view.topPitch > top)
            view.topPitch = top;
        if (view.topPitch < view.cursorPitch)
            view.topPitch = view.cursorPitch;
        for (int b = 0; b < 8; b++)
            view.sliceButton[b] = (int8_t)sliceForButton(p, track, b);
    }

    // ghost: a single note, or the palette chord's shape on the cursor note
    bool pads = padsMode();
    const bool sing = singMode();
    if (pads || sing)
        chordSlot = -1;
    syllable = ((syllable % kSyllables) + kSyllables) % kSyllables;
    view.syllables = sing ? kSyllableNames : nullptr;
    view.ghostSyllable = syllable;
    if (sing)
        g_liveSyllable = syllable;
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
        idx = findEditNote(view.cursorTick, view.cursorPitch);

    int pitch = view.cursorPitch, vel = velocity, nud = nudge, len = lengthTicks, chn = chance;
    int syl = syllable;
    if (idx >= 0)
    {
        Note n = getNote(p, track, idx);
        syl = noteSyllable(n);
        pitch = n.pitch;
        vel = n.velocity;
        chn = n.chance();
        len = n.length;
        nud = n.start - ((n.start + kTicksPerStep / 2) / kTicksPerStep) * kTicksPerStep;
    }

    if (pads)
        snprintf(view.title, sizeof(view.title), "Slice %d  Step %d  Len %d.%02d", pitch - kFirstSliceNote + 1,
                 view.cursorTick / kTicksPerStep + 1, len / kTicksPerStep,
                 (len % kTicksPerStep) * 100 / kTicksPerStep);
    else
        snprintf(view.title, sizeof(view.title), "%s %s  Step %d  Len %d.%02d", mtNotes[pitch],
                 sing ? kSyllableNames[syl] : (chordSlot >= 0 ? chordType(ghostChord.type).label : "Note"),
                 view.cursorTick / kTicksPerStep + 1,
                 len / kTicksPerStep, (len % kTicksPerStep) * 100 / kTicksPerStep);

    view.modeColor = color::kBox;
    if (g_recArmed)
    {
        snprintf(view.mode, sizeof(view.mode), "%s", recModeText());
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
    if (sing)
    {
        // FX2 picks the syllable (of the note under the cursor, or of the next one)
        snprintf(view.params[3].label, sizeof(view.params[3].label), "Syl");
        snprintf(view.params[3].value, sizeof(view.params[3].value), "%s", kSyllableNames[syl]);
    }
    else
    {
        snprintf(view.params[3].label, sizeof(view.params[3].label), "Inv");
        snprintf(view.params[3].value, sizeof(view.params[3].value), "%d", inversion);
    }
    view.params[3].color = color::kFx2Btn;
    view.selectedParam = paramHeld ? param : -1; // lit while its button is held

    if (view.shift)
    {
        // what the coloured buttons do with Shift
        static const char* const kShiftNames[4] = { "Quantize", "Acid", "Arp", "" };
        for (int i = 0; i < 4; i++)
        {
            snprintf(view.params[i].label, sizeof(view.params[i].label), "%s", kShiftNames[i]);
            view.params[i].value[0] = 0;
        }
        view.selectedParam = -1;
    }

    // bottom bar: Shift = utilities; sliced sample = slices; otherwise
    // button 1 = single note and buttons 2-8 = the chord palette
    static const char* const kUtility[8] = { "Oct -",   "Oct +",  "Ghosts", "Semi +",
                                             "Sel all", "Reverse", "Double", "Legato" };
    for (int i = 0; i < 8; i++)
    {
        if (view.shift)
        {
            snprintf(barText[i], sizeof(barText[i]), "%s", kUtility[i]);
            view.bar.fill[i] = 0;
            view.bar.highlighted[i] = (i == 2 && s_otherTracks); // lit while ghost notes are on
        }
        else if (sing)
        {
            // a singing synth: the top row = the 8 syllables
            snprintf(barText[i], sizeof(barText[i]), "%s", kSyllableNames[i]);
            view.bar.fill[i] = color::kMelodic;
            view.bar.highlighted[i] = (syllable == i);
        }
        else if (pads)
        {
            int s = sliceForButton(p, track, i);
            snprintf(barText[i], sizeof(barText[i]), "Slice %d", s + 1);
            view.bar.fill[i] = color::kSlice;
            view.bar.highlighted[i] = (view.cursorPitch == kFirstSliceNote + s);
        }
        else if (i == 0)
        {
            snprintf(barText[i], sizeof(barText[i]), "Note");
            view.bar.fill[i] = color::kMelodic;
            view.bar.highlighted[i] = (chordSlot < 0);
        }
        else
        {
            paletteChordName(getPaletteChord(p, i - 1), barText[i], sizeof(barText[i]));
            view.bar.fill[i] = color::kMelodic;
            view.bar.highlighted[i] = (chordSlot == i - 1);
        }
        view.bar.text[i] = barText[i];
    }

    if (menu != Menu::None)
        refreshMenu();
    else
    {
        view.menu.active = false;
        view.picker.active = false;
    }

    display.refreshControl(grid);
}

void cPianoRoll::update()
{
    if (recPoll())
        refreshView();

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

// the note the cursor is on: one starting within +-10 of the cursor square
// (closest first), otherwise one that covers it
int cPianoRoll::findEditNote(int tick, int pitch)
{
    const Sequencer::strPattern* p = current();
    int count = noteCount(p, track);
    int best = -1, bestDist = 11;
    for (int i = count - 1; i >= 0; i--)
    {
        Note n = getNote(p, track, i);
        if (n.pitch != pitch)
            continue;
        int d = (int)n.start - tick;
        if (d < 0)
            d = -d;
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    if (best >= 0)
        return best;
    return findNoteAt(p, track, tick, pitch);
}

int cPianoRoll::selectUnderCursor()
{
    Sequencer::strPattern* p = current();
    int idx = findEditNote(view.cursorTick, view.cursorPitch);
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
        (void)p;
        int idx = PR->findEditNote(PR->view.cursorTick, PR->view.ghostPitches[g]);
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
    if (on && singMode())
        g_liveSyllable = syllable;
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
        if (view.sliceCount > 0)
        {
            // slices stay on slice rows
            if (note.pitch + dPitch < kFirstSliceNote)
                dPitch = kFirstSliceNote - note.pitch;
            if (note.pitch + dPitch > kFirstSliceNote + view.sliceCount - 1)
                dPitch = kFirstSliceNote + view.sliceCount - 1 - note.pitch;
        }
    }
    if (dTicks == 0 && dPitch == 0)
        return;

    beginEdit(kEditMove);
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
        if (singMode())
            g_liveSyllable = noteSyllable(first);
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
    beginEdit(kEditPlace);
    for (int g = 0; g < view.ghostCount; g++)
    {
        uint8_t pitch = view.ghostPitches[g];
        if (findEditNote(view.cursorTick, pitch) >= 0)
            continue;
        Note n{};
        n.start = (uint16_t)start;
        n.length = (uint16_t)lengthTicks;
        n.pitch = pitch;
        n.velocity = (uint8_t)velocity;
        n.setChance(chance);
        if (singMode())
            setNoteSyllable(n, syllable);
        if (addNote(p, track, n) < 0)
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
            break;
        }
        any = true;
    }
    if (any)
        markChanged();
}

// slice tracks: hold a top button, move to a slice row, press Middle =
// that button plays the slice from now on
bool cPianoRoll::assignSliceToHeld()
{
    if (!padsMode() || mode != Mode::Idle)
        return false;
    int button = -1;
    for (int i = 0; i < 8; i++)
        if (held[i])
        {
            button = i;
            break;
        }
    if (button < 0)
        return false;
    int slice = view.cursorPitch - kFirstSliceNote;
    if (slice < 0 || slice >= sliceCount(current(), track))
        return false;
    setSliceForButton(current(), track, button, slice);
    heldNotes[button][0] = (uint8_t)view.cursorPitch; // its release frees the right note
    char msg[40];
    snprintf(msg, sizeof(msg), "Button %d = slice %d", button + 1, slice + 1);
    mtPopups.show(0, msg, 1);
    markChanged();
    refreshView();
    return true;
}

void cPianoRoll::pressMiddle()
{
    switch (mode)
    {
        case Mode::Idle:
        {
            int idx = findEditNote(view.cursorTick, view.cursorPitch);
            if (idx >= 0 && selectUnderCursor())
            {
                mode = Mode::Move; // grabbed a note
                Note n = getNote(current(), track, idx);
                if (singMode())
                    g_liveSyllable = noteSyllable(n);
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
    if (shiftActive())
    {
        beginEdit(kEditClear);
        stopTrack(track);
        clearNotes(p, track);
        markChanged();
        mode = Mode::Idle;
        mtPopups.show(0, "Track cleared", 1);
        refreshView();
        return;
    }
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    if (n == 0)
    {
        if (chordSlot >= 0)
        {
            chordSlot = -1; // nothing to delete: the ghost goes back to a single note
            inversion = 0;
            refreshView();
        }
        return;
    }
    beginEdit(kEditDelete);
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
    refreshView();
}

void cPianoRoll::changeParam(int delta)
{
    Sequencer::strPattern* p = current();
    int targets[kMaxNotes];
    const bool sing = singMode();
    int n = (param == kParamInversion && !sing) ? 0 : collectTargets(targets, kMaxNotes);

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
                if (sing)
                {
                    syllable = ((syllable + delta) % kSyllables + kSyllables) % kSyllables; // the next note's syllable
                    break;
                }
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

    beginEdit(kEditParam);
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
            case kParamInversion: setNoteSyllable(note, noteSyllable(note) + delta); break; // a singing synth
            default: break;
        }
        setNote(p, track, targets[i], note);
    }
    markChanged();
    refreshView();
    if (sing && param == kParamInversion && mode == Mode::Idle)
    {
        // hear the new syllable
        Note first = getNote(p, track, targets[0]);
        g_liveSyllable = noteSyllable(first);
        auditionOn(track, first.pitch, first.velocity);
        auditionOffAt = (uint32_t)s_clock + 250;
    }
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
    beginEdit(kEditLength);
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
    if (shiftActive())
    {
        utility(button);
        return;
    }
    bool recording = g_recArmed && sequencer.isPlay();
    bool pads = padsMode();
    int slot = button - 1; // buttons 2-8 = palette chords 1-7

    uint8_t notes[kMaxChordNotes];
    int n = 0;
    if (pads)
    {
        int pitch = clampInt(kFirstSliceNote + sliceForButton(p, track, button), 0, 127);
        notes[0] = (uint8_t)pitch;
        n = 1;
        chordSlot = -1;
        view.cursorPitch = pitch;
    }
    else if (singMode())
    {
        // a singing synth: the button picks the syllable and sings it at C
        chordSlot = -1;
        inversion = 0;
        syllable = button;
        g_liveSyllable = button;
        notes[0] = 60;
        n = 1;
    }
    else if (button == 0)
    {
        // single note at the cursor
        chordSlot = -1;
        inversion = 0;
        notes[0] = (uint8_t)view.cursorPitch;
        n = 1;
    }
    else
    {
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
        bool hold = !pads;
        beginEdit(kEditRecord);
        if (liveRecordOn(track, notes, n, (uint8_t)velocity, hold))
        {
            heldRecording[button] = hold;
            markChanged();
        }
        else
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
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

void cPianoRoll::utility(int button)
{
    Sequencer::strPattern* p = current();
    switch (button)
    {
        case 2:
            // ghost notes: see the other melodic tracks (view only)
            s_otherTracks = !s_otherTracks;
            mtPopups.show(0, s_otherTracks ? "Ghost notes ON" : "Ghost notes OFF", 1);
            break;
        case 0:
        case 1:
        case 3:
        {
            static const int kShift[4] = { -12, 12, -1, 1 };
            beginEdit(kEditTool);
            transposeNotes(p, track, kShift[button]);
            markChanged();
            break;
        }
        case 4:
        {
            // select every note; the d-pad then moves them all
            int count = noteCount(p, track);
            for (int i = 0; i < count; i++)
            {
                Note n = getNote(p, track, i);
                n.flags |= kNoteFlagSelected;
                setNote(p, track, i, n);
            }
            mode = count > 0 ? Mode::Move : Mode::Idle;
            break;
        }
        case 5:
            beginEdit(kEditTool);
            reverseNotes(p, track);
            markChanged();
            mtPopups.show(0, "Reversed", 1);
            break;
        case 6:
            beginEdit(kEditTool);
            if (duplicateTrack(p, track))
            {
                markChanged();
                mtPopups.show(0, "Track doubled", 1);
            }
            else
            {
                mtPopups.show(0, "Can't double: max 128 steps / 123 notes", 2);
            }
            break;
        case 7:
            beginEdit(kEditTool);
            legatoNotes(p, track);
            markChanged();
            mtPopups.show(0, "Legato", 1);
            break;
        default: return;
    }
    refreshView();
}

// Copy: the selected notes, or the note(s) under the cursor.
// Shift + Copy: paste them with the first note at the cursor.
void cPianoRoll::copyOrPaste()
{
    Sequencer::strPattern* p = current();
    if (shiftActive())
    {
        if (!hasClipboard())
        {
            mtPopups.show(0, "Copy some notes first", 1);
            return;
        }
        beginEdit(kEditTool);
        int n = pasteNotes(p, track, view.cursorTick);
        markChanged();
        char msg[32];
        snprintf(msg, sizeof(msg), "Pasted %d notes", n);
        mtPopups.show(0, msg, 1);
        refreshView();
        return;
    }
    int targets[kMaxNotes];
    int n = collectTargets(targets, kMaxNotes);
    if (n == 0)
    {
        mtPopups.show(0, "Nothing to copy here", 1);
        return;
    }
    copyNotes(p, track, targets, n);
    char msg[32];
    snprintf(msg, sizeof(msg), "Copied %d notes", n);
    mtPopups.show(0, msg, 1);
}

void cPianoRoll::openTrackPage()
{
    uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromPianoRoll, track);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

// ---------------------------------------------------------------------------
// menus: Quantize (Shift+Note), Acid (Shift+Instr), Arp (Shift+FX1)

enum AcidRow
{
    kAcidDensity = 0,
    kAcidSlide,
    kAcidNotes,
    kAcidAccent,
    kAcidEnvelope,
    kAcidApply,
    kAcidRowCount
};

enum ArpRow
{
    kArpRowMode = 0,
    kArpRowRate,
    kArpRowOctaves,
    kArpRowGate,
    kArpRowGoApe,
    kArpRowRhythm,
    kArpRowApeGate,
    kArpRowApeNotes,
    kArpRowCount
};

static int s_menuRow = 0;

void cPianoRoll::openMenu(Menu m)
{
    if (mode != Mode::Idle)
    {
        clearSelection();
        mode = Mode::Idle;
    }
    releaseAllButtons();
    g_shiftLatched = false; // Shift did its job: opening the menu
    menu = m;
    s_menuRow = 0;
    if (m == Menu::Quantize)
    {
        s_menuRow = -1; // on the column titles
    }
    else if (m == Menu::Arp)
    {
        arpBackup = getSettings(current(), track);
        if (arpBackup.arpMode == kArpOff)
        {
            // start from something you can hear
            TrackSettings s = arpBackup;
            s.arpMode = kArpUp;
            setSettings(current(), track, s);
        }
    }
    refreshView();
}

void cPianoRoll::closeMenu(bool apply)
{
    Sequencer::strPattern* p = current();
    Menu m = menu;
    menu = Menu::None;
    view.menu.active = false;
    view.picker.active = false;

    if (m == Menu::Arp)
    {
        if (apply)
        {
            TrackSettings chosen = getSettings(p, track);
            setSettings(p, track, arpBackup); // undo goes back to before the menu
            beginEdit(kEditTool);
            setSettings(p, track, chosen);
            stopTrack(track);
            int written = renderArp(p, track);
            if (written > 0)
            {
                markChanged();
                char msg[40];
                snprintf(msg, sizeof(msg), "Arp applied: %d notes", written);
                mtPopups.show(0, msg, 1);
            }
            else
            {
                setSettings(p, track, arpBackup);
                mtPopups.show(0, "Nothing to apply: place notes for the arp first", 2);
            }
        }
        else
        {
            setSettings(p, track, arpBackup); // back: as it was before the menu
        }
    }
    refreshView();
}

void cPianoRoll::refreshMenu()
{
    PianoMenuView& m = view.menu;
    PickerView& pk = view.picker;
    m.active = false;
    pk.active = false;

    for (int i = 0; i < 8; i++)
    {
        view.bar.text[i] = "";
        view.bar.fill[i] = 0;
        view.bar.highlighted[i] = false;
    }
    view.bar.text[7] = "Back";

    if (menu == Menu::Picker)
    {
        pk.active = true;
        memcpy(pk.mask, pickerMask, sizeof(pk.mask));
        pk.cursor = clampInt(pk.cursor, 0, 127);
        if (pk.cursor < pk.firstPitch)
            pk.firstPitch = pk.cursor - pk.cursor % 12;
        if (pk.cursor >= pk.firstPitch + pk.keys)
            pk.firstPitch = (pk.cursor - pk.keys + 12) - (pk.cursor - pk.keys + 12) % 12;
        pk.firstPitch = clampInt(pk.firstPitch, 0, 128 - pk.keys);
        view.bar.text[6] = "Apply";
        view.bar.fill[6] = color::kNoteBtn;
        view.bar.highlighted[6] = true;
        return;
    }

    m.active = true;
    if (menu == Menu::Quantize)
    {
        m.cols = 2;
        m.rows = 1;
        snprintf(m.title, sizeof(m.title), "Quantize / Humanize  -  track %d", track + 1);
        snprintf(m.colTitle[0], sizeof(m.colTitle[0]), "Quantize");
        snprintf(m.colTitle[1], sizeof(m.colTitle[1]), "Humanize");
        for (int i = 0; i < 2; i++)
        {
            snprintf(m.row[i].label, sizeof(m.row[i].label), "Amount");
            snprintf(m.row[i].value, sizeof(m.row[i].value), "%d", quantAmount[i]);
            m.row[i].isAction = false;
        }
        m.colCursor = quantCol;
        m.cursor = s_menuRow < 0 ? -1 : 0;
        m.accent = color::kNoteBtn;
        snprintf(m.hint, sizeof(m.hint), "Left/Right: column  Down: amount  +/-: change  7: Apply");
        view.bar.text[6] = "Apply";
        view.bar.fill[6] = color::kNoteBtn;
        view.bar.highlighted[6] = true;
    }
    else if (menu == Menu::Acid)
    {
        m.cols = 1;
        m.rows = kAcidRowCount;
        snprintf(m.title, sizeof(m.title), "Acid generator  -  track %d", track + 1);
        static const char* const kLabels[kAcidRowCount] = { "Density", "Slide", "Notes", "Accent", "Envelope",
                                                            "Apply" };
        for (int r = 0; r < kAcidRowCount; r++)
        {
            snprintf(m.row[r].label, sizeof(m.row[r].label), "%s", kLabels[r]);
            m.row[r].isAction = (r == kAcidApply);
            m.row[r].value[0] = 0;
        }
        snprintf(m.row[kAcidDensity].value, 20, "%d", acid.density);
        snprintf(m.row[kAcidSlide].value, 20, "%d", acid.slide);
        if (acid.notes == AcidNotes::Pick)
            snprintf(m.row[kAcidNotes].value, 20, "Pick (%d)", acid.pickedCount);
        else
            snprintf(m.row[kAcidNotes].value, 20, "In use");
        snprintf(m.row[kAcidAccent].value, 20, "%d", acid.accent);
        snprintf(m.row[kAcidEnvelope].value, 20, "%d", acid.envelope);
        snprintf(m.row[kAcidApply].value, 20, "Middle");
        m.cursor = clampInt(s_menuRow, 0, kAcidRowCount - 1);
        m.accent = color::kInstrBtn;
        if (m.cursor == kAcidNotes)
            snprintf(m.hint, sizeof(m.hint), "+/-: In use / Pick   Middle: pick notes");
        else if (m.cursor == kAcidApply)
            snprintf(m.hint, sizeof(m.hint), "Middle: generate (replaces this track's notes)");
        else
            snprintf(m.hint, sizeof(m.hint), "Up/Down: setting   +/-: change");
    }
    else if (menu == Menu::Arp)
    {
        TrackSettings s = getSettings(current(), track);
        m.cols = 1;
        m.rows = kArpRowCount;
        snprintf(m.title, sizeof(m.title), "Arp  -  track %d", track + 1);
        static const char* const kLabels[kArpRowCount] = { "Arp",    "Rate",        "Octaves",     "Gate",
                                                           "Go Ape", "Ape: rhythm", "Ape: gate", "Ape: notes" };
        for (int r = 0; r < kArpRowCount; r++)
        {
            snprintf(m.row[r].label, sizeof(m.row[r].label), "%s", kLabels[r]);
            m.row[r].isAction = (r == kArpRowGoApe);
        }
        snprintf(m.row[kArpRowMode].value, 20, "%s", kArpModeNames[s.arpMode]);
        snprintf(m.row[kArpRowRate].value, 20, "%s", kArpRateNames[s.arpRate]);
        snprintf(m.row[kArpRowOctaves].value, 20, "%d", s.arpOctaves);
        snprintf(m.row[kArpRowGate].value, 20, "%d%%", s.arpGate);
        snprintf(m.row[kArpRowGoApe].value, 20, "%s", (s.apeOn && s.apeSeed) ? "On  (Middle: new)" : "Middle: go");
        snprintf(m.row[kArpRowRhythm].value, 20, "%d", s.apeRhythm);
        snprintf(m.row[kArpRowApeGate].value, 20, "%d", s.apeGate);
        snprintf(m.row[kArpRowApeNotes].value, 20, "%d", s.apeNotes);
        m.cursor = clampInt(s_menuRow, 0, kArpRowCount - 1);
        m.accent = color::kFx1Btn;
        snprintf(m.hint, sizeof(m.hint), "Plays live. 7: Apply as notes  6: Keep arp  8: Back");
        view.bar.text[0] = "Go Ape";
        view.bar.fill[0] = color::kFx1Btn;
        view.bar.highlighted[0] = s.apeOn && s.apeSeed;
        view.bar.text[4] = (s.apeOn && s.apeSeed) ? "Ape off" : "";
        view.bar.text[5] = "Keep";
        view.bar.text[6] = "Apply";
        view.bar.fill[6] = color::kNoteBtn;
        view.bar.highlighted[6] = true;
    }
}

void cPianoRoll::menuMove(int dCol, int dRow)
{
    switch (menu)
    {
        case Menu::Quantize:
            if (dCol != 0)
                quantCol = clampInt(quantCol + dCol, 0, 1);
            if (dRow > 0)
                s_menuRow = 0;
            if (dRow < 0)
                s_menuRow = -1;
            break;
        case Menu::Acid: s_menuRow = clampInt(s_menuRow + dRow, 0, kAcidRowCount - 1); break;
        case Menu::Arp: s_menuRow = clampInt(s_menuRow + dRow, 0, kArpRowCount - 1); break;
        case Menu::Picker:
        {
            PickerView& pk = view.picker;
            pk.cursor = clampInt(pk.cursor + dCol + dRow * 12, 0, 127);
            auditionOn(track, (uint8_t)pk.cursor, (uint8_t)velocity);
            auditionOffAt = (uint32_t)s_clock + 200;
            break;
        }
        default: break;
    }
    refreshView();
}

void cPianoRoll::menuChange(int delta, bool big)
{
    if (menu == Menu::Quantize)
    {
        if (s_menuRow >= 0)
            quantAmount[quantCol] = clampInt(quantAmount[quantCol] + delta * (big ? 5 : 1), 1, 10);
    }
    else if (menu == Menu::Acid)
    {
        int step = big ? 5 : 1;
        switch (s_menuRow)
        {
            case kAcidDensity: acid.density = (uint8_t)clampInt(acid.density + delta * step, 1, 10); break;
            case kAcidSlide: acid.slide = (uint8_t)clampInt(acid.slide + delta * step, 0, 10); break;
            case kAcidNotes: acid.notes = delta > 0 ? AcidNotes::Pick : AcidNotes::InUse; break;
            case kAcidAccent: acid.accent = (uint8_t)clampInt(acid.accent + delta * step, 0, 10); break;
            case kAcidEnvelope: acid.envelope = (uint8_t)clampInt(acid.envelope + delta * step, 0, 10); break;
            default: break;
        }
    }
    else if (menu == Menu::Arp)
    {
        Sequencer::strPattern* p = current();
        TrackSettings s = getSettings(p, track);
        int step = big ? 10 : 5;
        switch (s_menuRow)
        {
            case kArpRowMode: s.arpMode = (uint8_t)clampInt(s.arpMode + delta, 1, kArpModeCount - 1); break;
            case kArpRowRate: s.arpRate = (uint8_t)clampInt(s.arpRate + delta, 0, kArpRateCount - 1); break;
            case kArpRowOctaves: s.arpOctaves = (uint8_t)clampInt(s.arpOctaves + delta, 1, 4); break;
            case kArpRowGate: s.arpGate = (uint8_t)clampInt(s.arpGate + delta * step, 10, 100); break;
            case kArpRowRhythm: s.apeRhythm = (uint8_t)clampInt(s.apeRhythm + delta * step, 0, 100); break;
            case kArpRowApeGate: s.apeGate = (uint8_t)clampInt(s.apeGate + delta * step, 0, 100); break;
            case kArpRowApeNotes: s.apeNotes = (uint8_t)clampInt(s.apeNotes + delta * step, 0, 100); break;
            default: return;
        }
        setSettings(p, track, s); // heard right away; Back restores the old settings
    }
    refreshView();
}

static uint32_t newSeed()
{
    uint32_t s = random32() ^ ((uint32_t)s_clock * 2654435761u);
    return s == 0 ? 1 : s;
}

void cPianoRoll::menuMiddle()
{
    if (menu == Menu::Acid)
    {
        if (s_menuRow == kAcidNotes)
        {
            // open the note picker on the notes picked so far
            memset(pickerMask, 0, sizeof(pickerMask));
            for (int i = 0; i < acid.pickedCount; i++)
                pickerMask[acid.picked[i] >> 3] |= (uint8_t)(1u << (acid.picked[i] & 7));
            view.picker.cursor = acid.pickedCount ? acid.picked[0] : getSettings(current(), track).baseNote - 24;
            view.picker.firstPitch = view.picker.cursor - view.picker.cursor % 12;
            menu = Menu::Picker;
            refreshView();
            return;
        }
        if (s_menuRow == kAcidApply)
        {
            if (acid.notes == AcidNotes::Pick && acid.pickedCount == 0)
            {
                mtPopups.show(0, "Pick some notes first (Notes row, Middle)", 2);
                return;
            }
            beginEdit(kEditTool);
            stopTrack(track);
            generateAcid(current(), track, acid);
            markChanged();
            menu = Menu::None;
            mtPopups.show(0, "Acid line generated", 1);
            refreshView();
        }
        return;
    }
    if (menu == Menu::Arp && s_menuRow == kArpRowGoApe)
    {
        Sequencer::strPattern* p = current();
        TrackSettings s = getSettings(p, track);
        s.apeOn = true;
        s.apeSeed = newSeed(); // a new random sequence every press
        setSettings(p, track, s);
        refreshView();
        return;
    }
    if (menu == Menu::Picker)
    {
        pickerToggle();
    }
}

void cPianoRoll::pickerToggle()
{
    int pitch = view.picker.cursor;
    uint8_t bit = (uint8_t)(1u << (pitch & 7));
    int count = 0;
    for (int b = 0; b < 16; b++)
        for (int k = 0; k < 8; k++)
            count += (pickerMask[b] >> k) & 1;
    if (pickerMask[pitch >> 3] & bit)
    {
        pickerMask[pitch >> 3] &= (uint8_t)~bit;
    }
    else if (count < (int)sizeof(acid.picked))
    {
        pickerMask[pitch >> 3] |= bit;
        auditionOn(track, (uint8_t)pitch, (uint8_t)velocity);
        auditionOffAt = (uint32_t)s_clock + 300;
    }
    else
    {
        mtPopups.show(0, "24 notes at most", 1);
    }
    refreshView();
}

void cPianoRoll::menuTopButton(int button)
{
    if (button == 7)
    {
        // Back: leave without applying
        if (menu == Menu::Picker)
        {
            menu = Menu::Acid;
            refreshView();
            return;
        }
        closeMenu(false);
        return;
    }

    switch (menu)
    {
        case Menu::Picker:
            if (button == 6)
            {
                acid.pickedCount = 0;
                for (int pitch = 0; pitch < 128 && acid.pickedCount < sizeof(acid.picked); pitch++)
                    if (pickerMask[pitch >> 3] & (1u << (pitch & 7)))
                        acid.picked[acid.pickedCount++] = (uint8_t)pitch;
                acid.notes = AcidNotes::Pick;
                menu = Menu::Acid;
                refreshView();
            }
            break;
        case Menu::Quantize:
            if (button == 6)
            {
                beginEdit(kEditTool);
                if (quantCol == 0)
                    quantizeTrack(current(), track, quantAmount[0]);
                else
                    humanizeTrack(current(), track, quantAmount[1]);
                markChanged();
                mtPopups.show(0, quantCol == 0 ? "Quantized" : "Humanized", 1);
                closeMenu(false);
            }
            break;
        case Menu::Arp:
            if (button == 0)
            {
                s_menuRow = kArpRowGoApe;
                menuMiddle();
            }
            else if (button == 4)
            {
                Sequencer::strPattern* p = current();
                TrackSettings s = getSettings(p, track);
                s.apeOn = false;
                setSettings(p, track, s);
                refreshView();
            }
            else if (button == 5)
            {
                // keep the live arp as it is, without turning it into notes
                TrackSettings chosen = getSettings(current(), track);
                setSettings(current(), track, arpBackup);
                beginEdit(kEditTool);
                setSettings(current(), track, chosen);
                markChanged();
                menu = Menu::None;
                refreshView();
            }
            else if (button == 6)
            {
                closeMenu(true);
            }
            break;
        default: break; // Acid applies from its own row, never from a top button
    }
}

// ---------------------------------------------------------------------------
// button handlers

static bool pressOrRepeat(uint8_t s)
{
    return s == buttonPress || s == buttonHold;
}

static bool inMenu()
{
    return PR->menu != cPianoRoll::Menu::None;
}

static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP && !sequencer.isCountingIn())
    {
        if (shiftActive() && !inMenu())
            sequencer.playSong();
        else
            playFromStop();
    }
    else
    {
        PR->releaseAllButtons();
        sequencer.stop();
        setRecArmed(false); // stopping ends live recording
    }
    PR->refreshView();
}

static void functRec(uint8_t state)
{
    if (inMenu())
        return;
    RecAction a = recButton(state);
    if (a == RecAction::Undo)
    {
        PR->releaseAllButtons();
        PR->mode = cPianoRoll::Mode::Idle;
        undoEdit();
    }
    if (a != RecAction::None)
        PR->refreshView();
}

static void functUp(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (inMenu())
        PR->menuMove(0, PR->menu == cPianoRoll::Menu::Picker ? 1 : -1);
    else
        PR->pressUp();
}
static void functDown(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (inMenu())
        PR->menuMove(0, PR->menu == cPianoRoll::Menu::Picker ? -1 : 1);
    else
        PR->pressDown();
}
static void functLeft(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (inMenu())
        PR->menuMove(-1, 0);
    else
        PR->pressLeft();
}
static void functRight(uint8_t s)
{
    if (!pressOrRepeat(s))
        return;
    if (inMenu())
        PR->menuMove(1, 0);
    else
        PR->pressRight();
}

static void functMiddle(uint8_t state)
{
    if (inMenu())
    {
        if (state == buttonPress)
            PR->menuMiddle();
        return;
    }
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
        if (PR->assignSliceToHeld())
            return;
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
    if (state == buttonPress && !inMenu())
        PR->pressDelete();
}

static void functInsert(uint8_t state)
{
    if (state != buttonPress || inMenu() || shiftActive())
        return;
    PR->pressInsert();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        PR->toggleShift();
}

static void functEncoder(int16_t value)
{
    // the Mini's big +/- send +/-10, the small ones +/-1
    bool big = value >= 10 || value <= -10;
    int dir = value > 0 ? 1 : -1;
    if (inMenu())
    {
        PR->menuChange(dir, big);
        return;
    }
    if (PR->paramHeld)
        PR->changeParam(value); // the held coloured button's value
    else if (big)
        PR->changeZoom(-dir); // big + = zoom in (fewer squares), big - = zoom out
    else
        PR->changeLength(dir); // small +/-: note length
}

static void functTopRow(uint8_t button, uint8_t state)
{
    int idx = button - interfaceButton0;
    if (inMenu())
    {
        if (state == buttonPress)
            PR->menuTopButton(idx);
        return;
    }
    if (state == buttonPress)
        PR->topButton(idx, true);
    else if (state == buttonRelease)
        PR->topButton(idx, false);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    // the coloured buttons work while held: hold one + press +/- to change it
    if (state == buttonRelease)
    {
        if (PR->paramHeld)
        {
            PR->paramHeld = false;
            PR->refreshView();
        }
        return;
    }
    if (state != buttonPress || inMenu())
        return;
    if (shiftActive())
    {
        if (button == interfaceButtonNote)
            PR->openMenu(cPianoRoll::Menu::Quantize);
        else if (button == interfaceButtonInstr)
            PR->openMenu(cPianoRoll::Menu::Acid);
        else if (button == physFx1Button())
            PR->openMenu(cPianoRoll::Menu::Arp);
        return;
    }
    if (button == interfaceButtonNote)
        PR->param = cPianoRoll::kParamProb;
    else if (button == interfaceButtonInstr)
        PR->param = cPianoRoll::kParamVelocity;
    else if (button == physFx1Button())
        PR->param = cPianoRoll::kParamNudge;
    else if (button == physFx2Button())
        PR->param = cPianoRoll::kParamInversion;
    else
        return;
    PR->paramHeld = true;
    PR->refreshView();
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    int t = PR->track;
    mtInterface.deactivateModule(PR);
    mtInterface.activateModule(&gibgobHome, homeOptions(t));
}

static void functSwitchModule(uint8_t button)
{
    PR->eventFunct(eventSwitchModule, PR, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && !inMenu() && (shiftActive() || PR->view.help.active))
    {
        toggleHelp(PR->view.help, PR->ip);
        PR->refreshView();
        return;
    }
    if (PR->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(PR);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(PR);
}

static void functCopy(uint8_t state)
{
    if (state == buttonPress && !inMenu())
        PR->copyOrPaste();
}

static void functParams(uint8_t state)
{
    if (state == buttonPress && !inMenu())
        openSoundParameters(PR, PR->track);
}
