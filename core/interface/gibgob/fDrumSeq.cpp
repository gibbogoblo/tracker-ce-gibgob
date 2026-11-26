// GibGob OS - drum track sequencer (shortcuts: gibgobHelp.h, Shift + Menu)

#include "gibgob/drumSeq.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cDrumSeq drumSeq;
static cDrumSeq* DS = &drumSeq;
static elapsedMillis s_clock;
static uint32_t s_auditionOffAt = 0;

using namespace gibgob;

enum EditKind
{
    kEditStep = 60,
    kEditParam,
    kEditLength,
    kEditClear,
};

static void functPlay(uint8_t state);
static void functRec(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functDelete(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functParamButton(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functParams(uint8_t state);
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
void cDrumSeq::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<DrumSeqControl>(&prop);
}

void cDrumSeq::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    track = (int)(options % kTracks);
    lastPlayStep = -2;
    s_auditionOffAt = 0;
    paramHeld = false;
    view.help.active = false;
    view.help.title = "Drum sequencer - shortcuts";
    view.help.lines = kHelpDrum;
    view.help.count = (int)(sizeof(kHelpDrum) / sizeof(kHelpDrum[0]));

    songTimer.hide();
    char name[16];
    trackDisplayName(track, name, sizeof(name));
    snprintf(titleText, sizeof(titleText), "Track %d  %s", track + 1, name);
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cDrumSeq::stop()
{
    auditionOff(track);
    moduleRefresh = 0;
}

void cDrumSeq::setDefaultScreenFunct()
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

    ip->setPotObj(interfacePot0, functEncoder, false);
}

// ---------------------------------------------------------------------------
// notes that belong to a step (within half a step of it)
int cDrumSeq::stepNotes(int step, int* out, int maxOut)
{
    const Sequencer::strPattern* p = current();
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    int count = noteCount(p, track);
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++)
    {
        if (belongsToStep(getNote(p, track, i).start, step, trackTicks))
            out[n++] = i;
    }
    return n;
}

void cDrumSeq::changeZoom(int dir)
{
    // 2, 4 or 8 rows of 8 steps
    if (dir > 0)
        view.rows = view.rows > 4 ? 4 : 2;
    else
        view.rows = view.rows < 4 ? 4 : DrumSeqView::kMaxRows;
    refresh();
}

void cDrumSeq::refresh()
{
    const Sequencer::strPattern* p = current();
    int steps = trackSteps(p, track);
    cursorStep = clampInt(cursorStep, 0, steps - 1);
    TrackSettings settings = getSettings(p, track);

    // keep the cursor's row on screen
    int rows = view.rows, cols = DrumSeqView::kCols;
    int cursorRow = cursorStep / cols;
    if (cursorRow < view.firstRow)
        view.firstRow = cursorRow;
    if (cursorRow >= view.firstRow + rows)
        view.firstRow = cursorRow - rows + 1;
    int lastRow = (steps - 1) / cols;
    if (view.firstRow > lastRow)
        view.firstRow = lastRow;
    if (view.firstRow < 0)
        view.firstRow = 0;

    view.steps = steps;
    view.cursorStep = cursorStep;
    view.shift = shiftActive();
    int ph = playheadTick(track);
    view.playStep = ph < 0 ? -1 : (ph / kTicksPerStep) % steps;

    int idx[kMaxNotes];
    for (int k = 0; k < rows * cols; k++)
    {
        DrumCell& cell = view.cell[k];
        int step = view.firstRow * cols + k;
        cell = DrumCell();
        if (step >= steps)
            continue;
        cell.exists = true;
        int n = stepNotes(step, idx, kMaxNotes);
        if (n > 0)
        {
            Note note = getNote(p, track, idx[0]);
            cell.on = true;
            cell.velocity = note.velocity;
            cell.chance = (uint8_t)note.chance();
            cell.nudge = (int8_t)stepOffset(note.start, step, steps * kTicksPerStep);
            if (note.pitch != settings.baseNote)
                snprintf(cell.pitch, sizeof(cell.pitch), "%s", mtNotes[note.pitch]);
        }
    }

    // values of the step under the cursor, or the defaults for new steps
    int n = stepNotes(cursorStep, idx, kMaxNotes);
    int vel = velocity, chn = chance, nud = nudge, pitch = settings.baseNote;
    if (n > 0)
    {
        Note note = getNote(p, track, idx[0]);
        vel = note.velocity;
        chn = note.chance();
        nud = stepOffset(note.start, cursorStep, steps * kTicksPerStep);
        pitch = note.pitch;
    }
    snprintf(view.title, sizeof(view.title), "Step %d/%d%s", cursorStep + 1, steps, n > 0 ? "" : "   (empty)");
    if (g_recArmed)
    {
        snprintf(view.mode, sizeof(view.mode), "%s", recModeText());
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
    view.selectedParam = paramHeld ? param : -1; // lit while its button is held

    // bottom bar: the 8 steps of the cursor's row
    int rowStart = cursorRow * cols;
    for (int i = 0; i < 8; i++)
    {
        int step = rowStart + i;
        if (step >= steps)
        {
            barText[i][0] = 0;
            view.bar.fill[i] = 0;
            view.bar.highlighted[i] = false;
        }
        else
        {
            snprintf(barText[i], sizeof(barText[i]), "%d", step + 1);
            view.bar.fill[i] = color::kDrum;
            view.bar.highlighted[i] = stepNotes(step, idx, 1) > 0;
        }
        view.bar.text[i] = barText[i];
    }

    display.refreshControl(grid);
}

void cDrumSeq::update()
{
    if (recPoll())
        refresh();

    if (s_auditionOffAt != 0 && (uint32_t)s_clock >= s_auditionOffAt)
    {
        s_auditionOffAt = 0;
        auditionOff(track);
    }
    int ph = playheadTick(track);
    int step = ph < 0 ? -1 : ph / kTicksPerStep;
    if (step != lastPlayStep)
    {
        lastPlayStep = step;
        refresh();
    }
}

// ---------------------------------------------------------------------------
void cDrumSeq::toggleStep(int step)
{
    Sequencer::strPattern* p = current();
    int steps = trackSteps(p, track);
    if (step < 0 || step >= steps)
        return;
    beginEdit(kEditStep);
    int idx[kMaxNotes];
    int n = stepNotes(step, idx, kMaxNotes);
    if (n > 0)
    {
        for (int i = n - 1; i >= 0; i--)
            removeNote(p, track, idx[i]);
    }
    else
    {
        TrackSettings settings = getSettings(p, track);
        Note note{};
        note.start = (uint16_t)startFromOffset(step, nudge, steps * kTicksPerStep);
        note.length = (uint16_t)lengthTicks;
        note.pitch = settings.baseNote;
        note.velocity = (uint8_t)velocity;
        note.setChance(chance);
        if (addNote(p, track, note) < 0)
        {
            mtPopups.show(0, "Track is full (123 notes)", 1);
        }
        else if (!sequencer.isPlay())
        {
            auditionOn(track, note.pitch, note.velocity);
            s_auditionOffAt = (uint32_t)s_clock + 150;
        }
    }
    markChanged();
    refresh();
}

void cDrumSeq::deleteStep(int step)
{
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    int n = stepNotes(step, idx, kMaxNotes);
    if (n == 0)
        return;
    beginEdit(kEditStep);
    for (int i = n - 1; i >= 0; i--)
        removeNote(p, track, idx[i]);
    markChanged();
    refresh();
}

void cDrumSeq::moveCursor(int dStep)
{
    cursorStep += dStep;
    refresh();
}

void cDrumSeq::changeParam(int delta)
{
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    int n = stepNotes(cursorStep, idx, kMaxNotes);
    int trackTicks = trackSteps(p, track) * kTicksPerStep;

    if (n == 0)
    {
        // empty step: change what new steps get
        switch (param)
        {
            case kParamProb: chance = clampInt(chance + delta * 5, 0, 100); break;
            case kParamVelocity: velocity = clampInt(velocity + delta, 1, 127); break;
            case kParamNudge: nudge = clampInt(nudge + delta, -(kTicksPerStep / 2), kTicksPerStep / 2 - 1); break;
            case kParamPitch:
            {
                beginEdit(kEditParam);
                TrackSettings s = getSettings(p, track);
                s.baseNote = (uint8_t)clampInt(s.baseNote + delta, 0, 127);
                setSettings(p, track, s);
                markChanged();
                auditionOn(track, s.baseNote, (uint8_t)velocity);
                s_auditionOffAt = (uint32_t)s_clock + 200;
                break;
            }
        }
        refresh();
        return;
    }

    beginEdit(kEditParam);
    for (int i = 0; i < n; i++)
    {
        Note note = getNote(p, track, idx[i]);
        switch (param)
        {
            case kParamProb: note.setChance(note.chance() + delta * 5); break;
            case kParamVelocity: note.velocity = (uint8_t)clampInt(note.velocity + delta, 1, 127); break;
            case kParamNudge:
            {
                // half a step early or late; step 1 may go early into the track's end
                int off = clampInt(stepOffset(note.start, cursorStep, trackTicks) + delta, -(kTicksPerStep / 2),
                                   kTicksPerStep / 2 - 1);
                note.start = (uint16_t)startFromOffset(cursorStep, off, trackTicks);
                break;
            }
            case kParamPitch:
                note.pitch = (uint8_t)clampInt(note.pitch + delta, 0, 127);
                break;
        }
        setNote(p, track, idx[i], note);
    }
    if (param == kParamPitch && !sequencer.isPlay())
    {
        Note note = getNote(p, track, idx[0]);
        auditionOn(track, note.pitch, note.velocity);
        s_auditionOffAt = (uint32_t)s_clock + 200;
    }
    markChanged();
    refresh();
}

void cDrumSeq::changeLength(int delta)
{
    Sequencer::strPattern* p = current();
    int idx[kMaxNotes];
    int n = stepNotes(cursorStep, idx, kMaxNotes);
    int minLen = kTicksPerStep / 2;
    int maxLen = Sequencer::MAXSTEP * kTicksPerStep;
    auto stepLen = [&](int len) {
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
        Note note = getNote(p, track, idx[i]);
        note.length = (uint16_t)stepLen(note.length);
        setNote(p, track, idx[i], note);
        lengthTicks = note.length;
    }
    markChanged();
    refresh();
}

// ---------------------------------------------------------------------------
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
        sequencer.stop();
        setRecArmed(false); // stopping ends live recording
    }
    DS->refresh();
}

static void functRec(uint8_t state)
{
    RecAction a = recButton(state);
    if (a == RecAction::Undo)
        undoEdit();
    if (a != RecAction::None)
        DS->refresh();
}

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s) && DS->cursorStep >= DrumSeqView::kCols)
        DS->moveCursor(-DrumSeqView::kCols);
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s) && DS->cursorStep + DrumSeqView::kCols < trackSteps(current(), DS->track))
        DS->moveCursor(DrumSeqView::kCols);
}
static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        DS->moveCursor(-1);
}
static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        DS->moveCursor(1);
}

static void functMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state == buttonPress)
        DS->toggleStep(DS->cursorStep);
}

static void functDelete(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (shiftActive())
    {
        beginEdit(kEditClear);
        stopTrack(DS->track);
        clearNotes(current(), DS->track);
        clearAllStepFx(current(), DS->track); // the steps' effects too
        markChanged();
        mtPopups.show(0, "Track cleared", 1);
        DS->refresh();
        return;
    }
    DS->deleteStep(DS->cursorStep);
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        DS->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    int idx = button - interfaceButton0;
    if (idx < 0 || idx >= DrumSeqView::kCols)
        return;
    int rowStart = (DS->cursorStep / DrumSeqView::kCols) * DrumSeqView::kCols;
    DS->toggleStep(rowStart + idx);
}

static void functParamButton(uint8_t button, uint8_t state)
{
    // the coloured buttons work while held: hold one + press +/- to change it
    if (state == buttonRelease)
    {
        if (DS->paramHeld)
        {
            DS->paramHeld = false;
            DS->refresh();
        }
        return;
    }
    if (state != buttonPress)
        return;
    if (button == interfaceButtonNote)
        DS->param = cDrumSeq::kParamProb;
    else if (button == interfaceButtonInstr)
        DS->param = cDrumSeq::kParamVelocity;
    else if (button == physFx1Button())
        DS->param = cDrumSeq::kParamNudge;
    else if (button == physFx2Button())
        DS->param = cDrumSeq::kParamPitch;
    else
        return;
    DS->paramHeld = true;
    DS->refresh();
}

static void functEncoder(int16_t value)
{
    bool big = value >= 10 || value <= -10;
    int dir = value > 0 ? 1 : -1;
    if (DS->paramHeld)
        DS->changeParam(dir); // the held coloured button's value
    else if (big)
        DS->changeZoom(dir); // big + = fewer rows (bigger), big - = more rows
    else
        DS->changeLength(dir); // small +/-: the step's length
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    int t = DS->track;
    mtInterface.deactivateModule(DS);
    mtInterface.activateModule(&gibgobHome, homeOptions(t));
}

static void functParams(uint8_t state)
{
    if (state == buttonPress)
        openSoundParameters(DS, DS->track);
}

static void functSwitchModule(uint8_t button)
{
    DS->eventFunct(eventSwitchModule, DS, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && !mtMenuPopup.isPopupVisible() && (shiftActive() || DS->view.help.active))
    {
        toggleHelp(DS->view.help, DS->ip);
        DS->refresh();
        return;
    }
    if (DS->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(DS);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(DS);
}
