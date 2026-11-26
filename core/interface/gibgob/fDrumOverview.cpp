// GibGob OS - drum overview: every drum track, 8 steps on screen
// (shortcuts: gibgobHelp.h kHelpDrumOverview, Shift + Menu)

#include "gibgob/drumOverview.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/Perform.h"
#include "gibgob/PerformControl.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobPages.h"
#include "gibgob/gibgobUi.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <new>
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

cDrumOverview drumOverview;
static cDrumOverview* DO = &drumOverview;
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
static void functCopy(uint8_t state);
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
enum EditKind
{
    kEditStep = 1,
    kEditParam,
    kEditLength,
    kEditClear,
    kEditFill,
    kEditPaste,
};
constexpr int kSteps = DrumOverviewView::kSteps;
constexpr int kLengths[5] = { 8, 16, 32, 64, 128 };

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void markChanged()
{
    newFileManager.setActualPatternStructChanged();
}

// a page of 8 steps on the clipboard (Copy / Shift + Copy)
struct PageClip
{
    bool full;
    bool has[kSteps];
    Note note[kSteps]; // start = offset from its step
};
__NOINIT(EXTERNAL_RAM) PageClip s_clip;
bool s_clipReady = false;

__NOINIT(EXTERNAL_RAM) alignas(8) uint8_t s_viewMemory[sizeof(DrumOverviewView)];

bool isDrumTrack(const Sequencer::strPattern* p, int t)
{
    return !isMelodic(p, t) && !isSliceTrack(p, t);
}
} // namespace

// ---------------------------------------------------------------------------
void cDrumOverview::initDisplayControls()
{
    if (view == nullptr)
        view = new (s_viewMemory) DrumOverviewView();
    strControlProperties prop;
    prop.x = 0;
    prop.y = 0;
    prop.w = kScreenW;
    prop.h = 480;
    prop.style = controlStyleShow;
    prop.data = view;
    control = display.createControl<DrumOverviewControl>(&prop);
}

void cDrumOverview::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    perform::loadSaved();
    if (!s_clipReady)
    {
        memset(&s_clip, 0, sizeof(s_clip));
        s_clipReady = true;
    }
    if ((options & 0x100) && (int)(options & 7) != selTrack)
    {
        // another track: start at its first step (the same track, e.g. back
        // from its settings: stay where you were)
        selTrack = (int)(options & 7);
        cursor = 0;
        onName = false;
    }
    view->help.active = false;
    view->help.title = "Drum overview - shortcuts";
    view->help.lines = kHelpDrumOverview;
    view->help.count = (int)(sizeof(kHelpDrumOverview) / sizeof(kHelpDrumOverview[0]));
    param = kParamNone;
    insertDown = insertUsed = false;
    g_fillActive = false;
    for (int i = 0; i < 8; i++)
        perfHeld[i] = false;
    lastPlayStep = -2;

    songTimer.show();
    titleBar->show();
    titleBar->setTitle("Drum overview");

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cDrumOverview::stop()
{
    releaseAll();
    g_fillActive = false;
    if (track() >= 0)
        auditionOff(track());
    moduleRefresh = 0;
}

void cDrumOverview::setDefaultScreenFunct()
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
    ip->setButtonObj(interfaceButtonCopy, functCopy);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);
    ip->setButtonObj(interfaceButtonNote, functParamButton);
    ip->setButtonObj(interfaceButtonInstr, functParamButton);
    ip->setButtonObj(interfaceButtonFx1, functParamButton);
    ip->setButtonObj(interfaceButtonFx2, functParamButton);
    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonParams, functParams);
    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
int cDrumOverview::track() const
{
    return drumCount > 0 ? selTrack : -1;
}

// notes that belong to a step (within half a step of it)
int cDrumOverview::stepNotes(int t, int step, int* out, int maxOut)
{
    const Sequencer::strPattern* p = current();
    const int trackTicks = trackSteps(p, t) * kTicksPerStep;
    const int count = noteCount(p, t);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
        if (belongsToStep(getNote(p, t, i).start, step, trackTicks))
            out[n++] = i;
    return n;
}

void cDrumOverview::refresh()
{
    const Sequencer::strPattern* p = current();
    DrumOverviewView& v = *view;

    // the drum tracks of this pattern; keep the selected one if it still is one
    drumCount = 0;
    int selIndex = 0;
    for (int t = 0; t < kTracks; t++)
        if (isDrumTrack(p, t))
        {
            if (t == selTrack)
                selIndex = drumCount;
            drums[drumCount++] = t;
        }
    v.shift = shiftActive();
    v.fillOn = g_fillActive;
    v.message[0] = 0;
    if (drumCount == 0)
    {
        v.count = 0;
        snprintf(v.message, sizeof(v.message), "No drum tracks: set one to Drum on its page");
        display.refreshControl(control);
        return;
    }
    if (!isDrumTrack(p, selTrack))
        selTrack = drums[0];
    const int t = selTrack;
    const int steps = trackSteps(p, t);
    cursor = clampInt(cursor, 0, steps - 1);
    const int page = cursor / kSteps;
    const int pages = (steps + kSteps - 1) / kSteps;
    const int first = page * kSteps;

    v.count = drumCount;
    v.sel = selIndex;
    v.cursor = onName ? -1 : cursor - first;
    const int ph = playheadTick(t);
    const int playStep = ph < 0 ? -1 : (ph / kTicksPerStep) % steps;
    v.play = (playStep >= first && playStep < first + kSteps) ? playStep - first : -1;
    snprintf(v.pageText, sizeof(v.pageText), "%d/%d", page + 1, pages);
    v.firstStep = first;
    snprintf(v.lengthText, sizeof(v.lengthText), "%d steps", steps);

    int idx[kMaxNotes];
    for (int k = 0; k < drumCount; k++)
    {
        const int tk = drums[k];
        DrumOverviewTrack& row = v.tracks[k];
        char name[12];
        trackDisplayName(tk, name, sizeof(name));
        snprintf(row.name, sizeof(row.name), "%s", name);
        row.muted = mtProject.values.trackMute[tk] != 0;
        row.hit = row.exists = row.fillOnly = row.noFill = 0;
        const int len = trackSteps(p, tk);
        for (int s = 0; s < kSteps; s++)
        {
            const int step = first + s;
            if (step >= len)
                continue;
            row.exists |= (uint8_t)(1u << s);
            if (stepNotes(tk, step, idx, 1) > 0)
            {
                const Note n = getNote(p, tk, idx[0]);
                row.hit |= (uint8_t)(1u << s);
                const int f = noteFill(n);
                if (f > 0)
                    row.fillOnly |= (uint8_t)(1u << s);
                else if (f < 0)
                    row.noFill |= (uint8_t)(1u << s);
            }
        }
    }

    // the selected track: parameter names, each step's locks, the cursor's values
    const bool shift = v.shift;
    const TrackSettings settings = getSettings(p, t);
    snprintf(v.labelTop[0], sizeof(v.labelTop[0]), "%s", shift ? "ratchet" : "prob");
    snprintf(v.labelTop[1], sizeof(v.labelTop[1]), "%s", shift ? "sample" : "vel");
    const bool steps2 = drumStepsOn(p, t);
    snprintf(v.labelBot[0], sizeof(v.labelBot[0]), "%s", shift ? "fx1" : "nudge");
    snprintf(v.labelBot[1], sizeof(v.labelBot[1]), "%s", shift ? "fx2" : "pitch");
    for (int s = 0; s < kSteps; s++)
    {
        v.lockTop[s][0][0] = v.lockTop[s][1][0] = v.lockBot[s][0][0] = v.lockBot[s][1][0] = 0;
        const int step = first + s;
        if (step >= steps || stepNotes(t, step, idx, 1) == 0)
            continue;
        const Note n = getNote(p, t, idx[0]);
        if (shift)
        {
            // Shift: the ratchet ("x4", "x4/2" = for 2 steps) and the step's own sample ("S2")
            if (!steps2)
                continue;
            const int hits = kRatchetHits[noteRatchet(n)];
            const int span = clampInt((n.length + kTicksPerStep / 2) / kTicksPerStep, 1, 8);
            if (hits > 1)
                snprintf(v.lockTop[s][0], 8, span > 1 ? "x%d/%d" : "x%d", hits, span);
            if (noteSample(n) > 0 && altSample(p, t, noteSample(n)) >= 0)
                snprintf(v.lockTop[s][1], 8, "S%d", noteSample(n));
            continue;
        }
        if (n.chance() < 100)
            snprintf(v.lockTop[s][0], 8, "%d%%", n.chance());
        if (n.velocity != 100)
            snprintf(v.lockTop[s][1], 8, "%d", n.velocity);
        const int nud = stepOffset(n.start, step, steps * kTicksPerStep);
        if (nud != 0)
            snprintf(v.lockBot[s][0], 8, "%+d", nud);
        if (n.pitch != settings.baseNote)
            snprintf(v.lockBot[s][1], 8, "%+d", (int)n.pitch - (int)settings.baseNote);
    }
    static const char* const kNames[4] = { "prob", "vel", "nudge", "pitch" };
    static const char* const kShiftNames[4] = { "ratchet", "sample", "fx1", "fx2" };
    for (int k = 0; k < 4; k++)
    {
        snprintf(v.cloudLabel[k], sizeof(v.cloudLabel[k]), "%s", shift ? kShiftNames[k] : kNames[k]);
        snprintf(v.cloudValue[k], sizeof(v.cloudValue[k]), "-");
    }
    if (shift && stepNotes(t, cursor, idx, 1) > 0)
    {
        // the cursor step's ratchet and sample
        const Note n = getNote(p, t, idx[0]);
        const int hits = steps2 ? kRatchetHits[noteRatchet(n)] : 1;
        const int span = clampInt((n.length + kTicksPerStep / 2) / kTicksPerStep, 1, 8);
        if (hits > 1)
            snprintf(v.cloudValue[0], sizeof(v.cloudValue[0]), "x%d %dst", hits, span);
        else
            snprintf(v.cloudValue[0], sizeof(v.cloudValue[0]), "off");
        const int k = steps2 ? noteSample(n) : 0;
        const int ins = k > 0 && altSample(p, t, k) >= 0 ? altSample(p, t, k) : getSound(p, t);
        char nm[20];
        gibgobSoundName((uint8_t)ins, nm, sizeof(nm));
        snprintf(v.cloudValue[1], sizeof(v.cloudValue[1]), "%.9s", nm);
    }
    if (!shift && stepNotes(t, cursor, idx, 1) > 0)
    {
        const Note n = getNote(p, t, idx[0]);
        const int f = noteFill(n);
        if (f != 0)
            snprintf(v.cloudValue[0], sizeof(v.cloudValue[0]), "%s", f > 0 ? "FILL" : "NO FILL");
        else
            snprintf(v.cloudValue[0], sizeof(v.cloudValue[0]), "%d%%", n.chance());
        snprintf(v.cloudValue[1], sizeof(v.cloudValue[1]), "%d", n.velocity);
        snprintf(v.cloudValue[2], sizeof(v.cloudValue[2]), "%+d", stepOffset(n.start, cursor, steps * kTicksPerStep));
        snprintf(v.cloudValue[3], sizeof(v.cloudValue[3]), "%s", mtNotes[clampInt(n.pitch, 0, 127)]);
    }
    v.cloudLit = (param == kParamProb || param == kParamRatchet)    ? 0
                 : (param == kParamVelocity || param == kParamSample) ? 1
                 : param == kParamNudge                               ? 2
                 : param == kParamPitch                               ? 3
                                                                      : -1;

    // Shift: the bottom shows the CHIMP OUT effects
    for (int i = 0; i < 8; i++)
    {
        const perform::Cell cell = perform::getLiveCell(i);
        performBarLabel(cell.fx, cell.p, barText[i], sizeof(barText[i]));
        v.bar.fill[i] = cell.fx == perform::kFxOff ? 0x3C3C42 : performFxColor(cell.fx);
        v.bar.highlighted[i] = perform::isHeld(i) || perform::engine().cellActive(i);
        v.bar.text[i] = barText[i];
    }
    display.refreshControl(control);
}

void cDrumOverview::update()
{
    perform::uiTick();
    bool changed = recPoll();
    if (auditionOffAt != 0 && (uint32_t)s_clock >= auditionOffAt)
    {
        auditionOffAt = 0;
        if (track() >= 0)
            auditionOff(track());
    }
    // the fill plays while Insert is held during playback
    const bool fill = insertDown && sequencer.isPlay();
    if (fill != g_fillActive)
    {
        g_fillActive = fill;
        changed = true;
    }
    const bool shiftNow = shiftActive();
    if (shiftNow != lastShift)
    {
        lastShift = shiftNow;
        param = kParamNone;
        changed = true;
    }
    if (track() >= 0)
    {
        const int ph = playheadTick(track());
        const int step = ph < 0 ? -1 : ph / kTicksPerStep;
        if (step != lastPlayStep)
        {
            lastPlayStep = step;
            changed = true;
        }
    }
    if (changed)
        refresh();
}

// ---------------------------------------------------------------------------
// actions
void cDrumOverview::selectTrack(int delta)
{
    if (drumCount == 0)
        return;
    int k = 0;
    for (int i = 0; i < drumCount; i++)
        if (drums[i] == selTrack)
            k = i;
    k = clampInt(k + delta, 0, drumCount - 1);
    if (drums[k] != selTrack)
    {
        auditionOff(selTrack);
        selTrack = drums[k];
    }
    refresh();
}

void cDrumOverview::moveCursor(int delta)
{
    if (track() < 0)
        return;
    // Left from step 1: onto the track name (Middle there: its settings)
    if (onName)
    {
        if (delta > 0)
        {
            onName = false;
            cursor = 0;
        }
        refresh();
        return;
    }
    if (delta == -1 && cursor == 0)
    {
        onName = true;
        refresh();
        return;
    }
    const int steps = trackSteps(current(), track());
    cursor = clampInt(cursor + delta, 0, steps - 1);
    refresh();
}

// Middle on the track name: the track's settings (sound / sample library, name, length...)
void cDrumOverview::openTrackPage()
{
    const int t = track();
    if (t < 0)
        return;
    const uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, cGibGobPages::kFromDrums, t);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

void cDrumOverview::toggleStep(int step)
{
    const int t = track();
    if (t < 0)
        return;
    Sequencer::strPattern* p = current();
    const int steps = trackSteps(p, t);
    if (step < 0 || step >= steps)
        return;
    beginEdit(kEditStep);
    int idx[kMaxNotes];
    const int n = stepNotes(t, step, idx, kMaxNotes);
    if (n > 0)
    {
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, t, idx[i]);
    }
    else
    {
        const TrackSettings settings = getSettings(p, t);
        Note note{};
        note.start = (uint16_t)startFromOffset(step, 0, steps * kTicksPerStep);
        note.length = (uint16_t)kTicksPerStep;
        note.pitch = settings.baseNote;
        note.velocity = 100;
        if (addNote(p, t, note) < 0)
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
        }
        else if (!sequencer.isPlay())
        {
            auditionOn(t, note.pitch, note.velocity);
            auditionOffAt = (uint32_t)s_clock + 150;
        }
    }
    cursor = step;
    markChanged();
    refresh();
}

void cDrumOverview::deleteStep()
{
    const int t = track();
    if (t < 0 || onName) // on the track name: no step to change
        return;
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    const int n = stepNotes(t, cursor, idx, kMaxNotes);
    if (n == 0)
        return;
    beginEdit(kEditStep);
    for (int i = n - 1; i >= 0; i--)
        removeNote(p, t, idx[i]);
    markChanged();
    refresh();
}

void cDrumOverview::clearTrack()
{
    const int t = track();
    if (t < 0)
        return;
    beginEdit(kEditClear);
    stopTrack(t);
    clearNotes(current(), t);
    markChanged();
    mtPopups.show(0, "Track cleared", 1);
    refresh();
}

// the held coloured button's value on the cursor step (and its lock)
void cDrumOverview::changeParam(int dir, bool big)
{
    const int t = track();
    if (t < 0 || onName) // on the track name: no step to change
        return;
    if (param == kParamShift)
    {
        mtPopups.show(0, "Effects per step: next version", 1);
        return;
    }
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    const int n = stepNotes(t, cursor, idx, kMaxNotes);
    if (n == 0)
    {
        mtPopups.show(0, "No hit on this step (top row adds one)", 1);
        return;
    }
    if (param == kParamRatchet || param == kParamSample)
    {
        changeDrumStep(t, idx, n, dir, big);
        return;
    }
    const int trackTicks = trackSteps(p, t) * kTicksPerStep;
    beginEdit(kEditParam);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, t, idx[i]);
        switch (param)
        {
            case kParamProb: note.setChance(note.chance() + dir * (big ? 25 : 5)); break; // ends a fill mark
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + dir * (big ? 10 : 1), 1, 127); break;
            case kParamNudge:
            {
                // half a step early or late; step 1 may go early into the track's end
                const int off = clampInt(stepOffset(note.start, cursor, trackTicks) + dir * (big ? 6 : 1),
                                         -(kTicksPerStep / 2), kTicksPerStep / 2 - 1);
                note.start = (uint16_t)startFromOffset(cursor, off, trackTicks);
                break;
            }
            case kParamPitch: note.pitch = (uint8_t)clampInt(note.pitch + dir * (big ? 12 : 1), 0, 127); break;
            default: break;
        }
        setNote(p, t, idx[i], note);
    }
    if (param == kParamPitch && !sequencer.isPlay())
    {
        const Note note = getNote(p, t, idx[0]);
        auditionOn(t, note.pitch, note.velocity);
        auditionOffAt = (uint32_t)s_clock + 200;
    }
    markChanged();
    refresh();
}

// Shift + Note: the ratchet (small +/- its speed, big +/- how many steps it
// lasts); Shift + Instr: the step's sample (the track's own or up to 3 more)
void cDrumOverview::changeDrumStep(int t, const int* idx, int n, int dir, bool big)
{
    Sequencer::strPattern* p = current();
    const int trackTicks = trackSteps(p, t) * kTicksPerStep;
    Note first = getNote(p, t, idx[0]);
    if (param == kParamSample)
    {
        // the next loaded sample, from the one the step plays now
        const int k = noteSample(first);
        int ins = k > 0 && altSample(p, t, k) >= 0 ? altSample(p, t, k) : getSound(p, t);
        int found = -1;
        for (int tries = 1; tries <= 48 && found < 0; tries++)
        {
            const int c = ((ins + dir * tries) % 48 + 48) % 48;
            if (isSampleInstrument(c) && mtProject.instrument[c].isActive == 1)
                found = c;
        }
        if (found < 0)
            return;
        beginEdit(kEditParam);
        enableDrumSteps(p, t);
        const int slot = altSlotFor(p, t, (uint8_t)found);
        if (slot < 0)
        {
            mtPopups.show(0, "3 extra samples per track: all in use", 1);
            refresh();
            return;
        }
        for (int i = 0; i < n; i++)
        {
            Note note = getNote(p, t, idx[i]);
            setNoteSample(note, slot);
            setNote(p, t, idx[i], note);
        }
        if (!sequencer.isPlay())
        {
            auditionSample(t, (uint8_t)found, first.pitch, first.velocity);
            auditionOffAt = (uint32_t)s_clock + 200;
        }
    }
    else
    {
        beginEdit(kEditParam);
        enableDrumSteps(p, t);
        for (int i = 0; i < n; i++)
        {
            Note note = getNote(p, t, idx[i]);
            int r = noteRatchet(note);
            int span = (note.length + kTicksPerStep / 2) / kTicksPerStep;
            span = clampInt(span, 1, 8);
            if (big)
                span = clampInt(span + dir, 1, 8); // its length in steps
            else
                r = clampInt(r + dir, 0, kRatchetCount - 1); // its speed
            if (r == 0)
                span = 1; // no ratchet: a plain hit
            // not past the end of the track
            const int startStep = cursor;
            span = clampInt(span, 1, trackTicks / kTicksPerStep - startStep);
            setNoteRatchet(note, r);
            note.length = (uint16_t)(span * kTicksPerStep);
            setNote(p, t, idx[i], note);
        }
    }
    markChanged();
    refresh();
}

// big +/-: the selected track's length 8 / 16 / 32 / 64 / 128
void cDrumOverview::changeLength(int dir, bool fine)
{
    const int t = track();
    if (t < 0)
        return;
    Sequencer::strPattern* p = current();
    const int steps = trackSteps(p, t);
    int target = steps;
    if (fine)
        target = clampInt(steps + dir, 1, kLengths[4]); // on the name: small +/- one step
    else if (dir > 0)
    {
        for (int L : kLengths)
            if (L > steps)
            {
                target = L;
                break;
            }
    }
    else
    {
        for (int k = 4; k >= 0; k--)
            if (kLengths[k] < steps)
            {
                target = kLengths[k];
                break;
            }
    }
    if (target == steps)
        return;
    beginEdit(kEditLength);
    setTrackSteps(p, t, target);
    markChanged();
    refresh();
}

// Insert + plus / minus: the step plays only in fills / is left out of fills
void cDrumOverview::markFill(int fill)
{
    const int t = track();
    if (t < 0 || onName) // on the track name: no step to change
        return;
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    const int n = stepNotes(t, cursor, idx, kMaxNotes);
    if (n == 0)
    {
        mtPopups.show(0, "No hit on this step (top row adds one)", 1);
        return;
    }
    beginEdit(kEditFill);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, t, idx[i]);
        setNoteFill(note, fill);
        setNote(p, t, idx[i], note);
    }
    markChanged();
    refresh();
}

// Insert on a marked step: the mark goes
void cDrumOverview::clearFillMark()
{
    const int t = track();
    if (t < 0 || onName) // on the track name: no step to change
        return;
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    const int n = stepNotes(t, cursor, idx, kMaxNotes);
    bool any = false;
    for (int i = 0; i < n; i++)
        any = any || noteFill(getNote(p, t, idx[i])) != 0;
    if (!any)
        return;
    beginEdit(kEditFill);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, t, idx[i]);
        if (noteFill(note) != 0)
        {
            setNoteFill(note, 0);
            setNote(p, t, idx[i], note);
        }
    }
    markChanged();
    refresh();
}

// Copy: the 8 steps on screen of the selected track
void cDrumOverview::copyPage()
{
    const int t = track();
    if (t < 0)
        return;
    const Sequencer::strPattern* p = current();
    const int steps = trackSteps(p, t);
    const int first = (cursor / kSteps) * kSteps;
    int idx[kMaxNotes];
    for (int s = 0; s < kSteps; s++)
    {
        s_clip.has[s] = false;
        const int step = first + s;
        if (step < steps && stepNotes(t, step, idx, 1) > 0)
        {
            Note n = getNote(p, t, idx[0]);
            n.start = (uint16_t)(stepOffset(n.start, step, steps * kTicksPerStep) + kTicksPerStep); // offset + 48
            n.flags &= (uint8_t)~kNoteFlagSelected;
            s_clip.note[s] = n;
            s_clip.has[s] = true;
        }
    }
    s_clip.full = true;
    mtPopups.show(0, "8 steps copied (Shift + Copy: paste)", 1);
}

// Shift + Copy: the copied steps over the 8 on screen (of any drum track)
void cDrumOverview::pastePage()
{
    const int t = track();
    if (t < 0)
        return;
    if (!s_clip.full)
    {
        mtPopups.show(0, "Nothing copied yet (Copy)", 1);
        return;
    }
    Sequencer::strPattern* p = current();
    const int steps = trackSteps(p, t);
    const int first = (cursor / kSteps) * kSteps;
    beginEdit(kEditPaste);
    int idx[kMaxNotes];
    bool full = false;
    for (int s = 0; s < kSteps; s++)
    {
        const int step = first + s;
        if (step >= steps)
            break;
        const int n = stepNotes(t, step, idx, kMaxNotes);
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, t, idx[i]);
        if (s_clip.has[s])
        {
            Note note = s_clip.note[s];
            note.start = (uint16_t)startFromOffset(step, (int)note.start - kTicksPerStep, steps * kTicksPerStep);
            if (addNote(p, t, note) < 0)
                full = true;
        }
    }
    markChanged();
    mtPopups.show(0, full ? "Track is full: not every step pasted" : "8 steps pasted", 1);
    refresh();
}

void cDrumOverview::goHome()
{
    const int t = track() >= 0 ? track() : 0;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobHome, homeOptions(t));
}

void cDrumOverview::topButton(int button, bool pressed)
{
    // Shift + top row: CHIMP OUT, held as long as the button is
    if (pressed && shiftActive())
    {
        perfHeld[button] = true;
        perform::press(button);
        refresh();
        return;
    }
    if (!pressed)
    {
        if (perfHeld[button])
        {
            perfHeld[button] = false;
            perform::release(button);
            refresh();
        }
        return;
    }
    // top row: the 8 steps on screen on / off
    if (track() < 0)
        return;
    toggleStep((cursor / kSteps) * kSteps + button);
}

void cDrumOverview::releaseAll()
{
    for (int i = 0; i < 8; i++)
        if (perfHeld[i])
        {
            perfHeld[i] = false;
            perform::release(i);
        }
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
        DO->releaseAll();
        sequencer.stop();
        setRecArmed(false);
        perform::releaseAll();
    }
    DO->refresh();
}

static void functRec(uint8_t state)
{
    const RecAction a = recButton(state);
    if (a == RecAction::Undo)
        undoEdit();
    if (a != RecAction::None)
        DO->refresh();
}

// the D-pad: Up/Down pick a track, Left/Right a step; while a CHIMP OUT
// effect is held they twist it
static bool twisting(uint8_t state, int twist)
{
    if (!perform::anyHeld())
        return false;
    if (state == buttonPress && perform::twist(twist))
        DO->refresh();
    return true;
}
static bool pressOrRepeat(uint8_t s)
{
    return s == buttonPress || s == buttonHold;
}
static void functUp(uint8_t s)
{
    if (!twisting(s, +1) && s == buttonPress)
        DO->selectTrack(-1);
}
static void functDown(uint8_t s)
{
    if (!twisting(s, -1) && s == buttonPress)
        DO->selectTrack(1);
}
static void functLeft(uint8_t s)
{
    if (!twisting(s, -2) && pressOrRepeat(s))
        DO->moveCursor(-1);
}
static void functRight(uint8_t s)
{
    if (!twisting(s, 2) && pressOrRepeat(s))
        DO->moveCursor(1);
}

static void functMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state != buttonPress)
        return;
    if (DO->onName)
        DO->openTrackPage(); // on the name: the track's settings
    else
        DO->toggleStep(DO->cursor); // on a step: that hit on / off
}

// Insert: held while playing = the fill; Insert + plus / minus marks the step;
// a tap on a marked step clears its mark
static void functInsert(uint8_t state)
{
    if (state == buttonPress)
    {
        DO->insertDown = true;
        DO->insertUsed = false;
    }
    else if (state == buttonRelease && DO->insertDown)
    {
        DO->insertDown = false;
        const bool wasFill = g_fillActive;
        g_fillActive = false;
        if (!DO->insertUsed && !wasFill)
            DO->clearFillMark();
        DO->refresh();
    }
}

static void functDelete(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftActive())
        DO->clearTrack();
    else
        DO->deleteStep();
}

static void functCopy(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftActive())
        DO->pastePage();
    else
        DO->copyPage();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        DO->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int i = button - interfaceButton0;
    if (i < 0 || i > 7)
        return;
    if (state == buttonPress)
        DO->topButton(i, true);
    else if (state == buttonRelease)
        DO->topButton(i, false);
}

// the coloured buttons work while held: hold one and press +/-
static void functParamButton(uint8_t button, uint8_t state)
{
    if (state == buttonRelease)
    {
        DO->param = cDrumOverview::kParamNone;
        DO->refresh();
        return;
    }
    if (state != buttonPress)
        return;
    if (shiftActive())
        DO->param = button == interfaceButtonNote    ? cDrumOverview::kParamRatchet
                    : button == interfaceButtonInstr ? cDrumOverview::kParamSample
                                                     : cDrumOverview::kParamShift;
    else if (button == interfaceButtonNote)
        DO->param = cDrumOverview::kParamProb;
    else if (button == interfaceButtonInstr)
        DO->param = cDrumOverview::kParamVelocity;
    else if (button == physFx1Button())
        DO->param = cDrumOverview::kParamNudge;
    else if (button == physFx2Button())
        DO->param = cDrumOverview::kParamPitch;
    else
        return;
    DO->refresh();
}

static void functEncoder(int16_t value)
{
    // holding a CHIMP OUT effect: +/- twist it
    if (perform::anyHeld())
    {
        if (perform::twist(perform::twistDir(value)))
            DO->refresh();
        return;
    }
    const bool big = value >= 10 || value <= -10;
    const int dir = value > 0 ? 1 : -1;
    if (DO->insertDown)
    {
        // Insert + plus: only in fills; Insert + minus: left out of fills
        DO->insertUsed = true;
        DO->markFill(dir);
        return;
    }
    if (DO->param != cDrumOverview::kParamNone)
    {
        DO->changeParam(dir, big);
        return;
    }
    if (DO->onName)
        DO->changeLength(dir, !big); // on the name: the length (small +/-: a step, big: 8/16/32/64/128)
    else if (big)
        DO->changeLength(dir, false); // big +/-: the track's length
    else
        DO->moveCursor(dir * DrumOverviewView::kSteps); // small +/-: the next / previous 8 steps
}

static void functSwitchModule(uint8_t button)
{
    DO->eventFunct(eventSwitchModule, DO, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && !mtMenuPopup.isPopupVisible() && (shiftActive() || DO->view->help.active))
    {
        toggleHelp(DO->view->help, DO->ip);
        DO->refresh();
        return;
    }
    if (DO->view->help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(DO);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(DO);
}

// button 1: back to the main screen
static void functPattern(uint8_t state)
{
    if (state == buttonPress)
        DO->goHome();
}

static void functParams(uint8_t state)
{
    if (state == buttonPress && DO->track() >= 0)
        openSoundParameters(DO, DO->track());
}
