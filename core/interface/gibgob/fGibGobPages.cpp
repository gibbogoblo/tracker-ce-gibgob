// GibGob OS - settings pages
//
// Track page
//   Up/Down          choose a setting           Left/Right = previous / next track
//   small +/-        change it                  big +/- = bigger steps
//   Top row          1 Acid  2 Quantize  3 Clear (Shift)  4 Mute  6 Chords  7 Piano roll  8 Back
//
// Chord palette page (one complete chord per top-row button, two banks)
//   D-pad            choose a chord and a column (root / type / inversion / voicing)
//   small +/-        change it                  big +/- = octave / bigger steps
//   Top row 1..8     jump to that chord and hear it        Shift = second bank
//
// Both pages: Pattern (Fn1) or button 8 "Back" returns to where you came from.

#include "gibgob/gibgobPages.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/pianoRoll.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cGibGobPages gibgobPages;
static cGibGobPages* GP = &gibgobPages;
static elapsedMillis s_clock;
static uint32_t s_auditionOffAt = 0;

using namespace gibgob;

enum TrackRow
{
    kRowType = 0,
    kRowSound,
    kRowBaseNote,
    kRowPatternLength,
    kRowArp,
    kRowArpRate,
    kRowArpOctaves,
    kRowArpGate,
    kRowApe,
    kRowApeRhythm,
    kRowApeGate,
    kRowApeNotes,
    kTrackRowCount
};

static const char* const kTrackRowNames[kTrackRowCount] = {
    "Track type",  "Sound",    "Base note", "Pattern length", "Arp",        "Arp rate",
    "Arp octaves", "Arp gate", "Go Ape",    "Ape: rhythm",    "Ape: gate",  "Ape: notes",
};

static const char* const kTrackBar[8] = { "Acid", "Quantize", "Clear", "Mute", "", "Chords", "Piano roll", "Back" };

static char s_barText[8][16];

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
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

static bool soundUsable(int instr)
{
    if (isSampleInstrument(instr))
        return mtProject.instrument[instr].isActive == 1;
    return isSynthInstrument(instr);
}

static void auditionBriefly(int track, const uint8_t* notes, int count)
{
    auditionChord(track, notes, count, 100);
    s_auditionOffAt = (uint32_t)s_clock + 350;
}

// ---------------------------------------------------------------------------
void cGibGobPages::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &view;
    table = display.createControl<TableControl>(&prop);
}

void cGibGobPages::start(uint32_t options)
{
    moduleRefresh = 1;
    page = ((options >> 8) & 0xF) == kPagePalette ? kPagePalette : kPageTrack;
    origin = ((options >> 4) & 0xF) == kFromPianoRoll ? kFromPianoRoll : kFromHome;
    track = (int)(options & 7);
    row = 0;
    col = 1;
    auditioning = false;
    s_auditionOffAt = 0;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "GibGob OS");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobPages::stop()
{
    auditionOff(track);
    auditioning = false;
    s_auditionOffAt = 0;
    moduleRefresh = 0;
}

void cGibGobPages::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    ip->setButtonObj(interfaceButtonPattern, functPattern);
    bindModuleSwitchButtons(ip, functSwitchModule);

    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cGibGobPages::switchPage(Page newPage)
{
    auditionOff(track);
    page = newPage;
    row = 0;
    col = 1;
    refresh();
}

// ---------------------------------------------------------------------------
void cGibGobPages::refresh()
{
    const Sequencer::strPattern* p = current();
    view.shift = shiftActive();
    view.mode[0] = 0;

    if (page == kPageTrack)
    {
        TrackSettings s = getSettings(p, track);
        bool melodic = isMelodic(p, track);

        view.rows = kTrackRowCount;
        view.cols = 2;
        view.colX[0] = 24;
        view.colX[1] = 300;
        view.header[0] = nullptr;
        row = clampInt(row, 0, kTrackRowCount - 1);
        col = 1;
        view.cursorRow = row;
        view.cursorCol = 1;

        for (int r = 0; r < kTrackRowCount; r++)
        {
            snprintf(view.cell[r][0], sizeof(view.cell[r][0]), "%s", kTrackRowNames[r]);
            view.rowColor[r] = r >= kRowApe ? color::kFx1Btn : (r >= kRowArp ? color::kFx2Btn
                                                                               : (melodic ? color::kMelodic : color::kDrum));
        }

        char name[20];
        gibgobSoundName(getSound(p, track), name, sizeof(name));
        snprintf(view.cell[kRowType][1], 20, "%s", melodic ? "Melodic" : "Drum");
        snprintf(view.cell[kRowSound][1], 20, "%.19s", name);
        snprintf(view.cell[kRowBaseNote][1], 20, "%s", mtNotes[s.baseNote]);
        snprintf(view.cell[kRowPatternLength][1], 20, "%d steps", p->track[0].length + 1);
        snprintf(view.cell[kRowArp][1], 20, "%s", kArpModeNames[s.arpMode]);
        snprintf(view.cell[kRowArpRate][1], 20, "%s", kArpRateNames[s.arpRate]);
        snprintf(view.cell[kRowArpOctaves][1], 20, "%d", s.arpOctaves);
        snprintf(view.cell[kRowArpGate][1], 20, "%d%%", s.arpGate);
        snprintf(view.cell[kRowApe][1], 20, "%s", s.apeOn ? "On" : "Off");
        snprintf(view.cell[kRowApeRhythm][1], 20, "%d", s.apeRhythm);
        snprintf(view.cell[kRowApeGate][1], 20, "%d", s.apeGate);
        snprintf(view.cell[kRowApeNotes][1], 20, "%d", s.apeNotes);

        snprintf(view.title, sizeof(view.title), "Track %d settings   (%d notes)", track + 1,
                 noteCount(p, track));

        bool muted = mtProject.values.trackMute[track] != 0;
        for (int i = 0; i < 8; i++)
        {
            view.bar.text[i] = kTrackBar[i];
            view.bar.fill[i] = 0;
            view.bar.highlighted[i] = false;
        }
        if (muted)
        {
            view.bar.fill[3] = color::kShiftBorder;
            view.bar.highlighted[3] = true;
        }
    }
    else
    {
        int bank = view.shift ? 1 : 0;

        view.rows = 8;
        view.cols = 5;
        view.colX[0] = 24;
        view.colX[1] = 130;
        view.colX[2] = 240;
        view.colX[3] = 400;
        view.colX[4] = 540;
        view.header[0] = "Button";
        view.header[1] = "Root";
        view.header[2] = "Type";
        view.header[3] = "Inversion";
        view.header[4] = "Voicing";
        row = clampInt(row, 0, 7);
        col = clampInt(col, 1, 4);
        view.cursorRow = row;
        view.cursorCol = col;

        static const uint32_t kColColor[5] = { 0, color::kNoteBtn, color::kInstrBtn, color::kFx1Btn,
                                               color::kFx2Btn };
        for (int r = 0; r < 8; r++)
        {
            PaletteChord c = getPaletteChord(p, bank * 8 + r);
            snprintf(view.cell[r][0], 20, "%d", r + 1);
            snprintf(view.cell[r][1], 20, "%s", mtNotes[c.root]);
            snprintf(view.cell[r][2], 20, "%s", chordType(c.type).label);
            snprintf(view.cell[r][3], 20, "%d", c.inversion % chordType(c.type).count);
            snprintf(view.cell[r][4], 20, "%s", kVoicingNames[c.voicing < kVoicingCount ? c.voicing : 0]);
            view.rowColor[r] = kColColor[col];

            paletteChordName(c, s_barText[r], sizeof(s_barText[r]));
            view.bar.text[r] = s_barText[r];
            view.bar.fill[r] = bank ? color::kFx2Btn : color::kMelodic;
            view.bar.highlighted[r] = (r == row);
        }

        snprintf(view.title, sizeof(view.title), "Chord palette   Bank %s   (plays on track %d)",
                 bank ? "B" : "A", track + 1);
    }

    display.refreshControl(table);
}

void cGibGobPages::update()
{
    if (s_auditionOffAt != 0 && (uint32_t)s_clock >= s_auditionOffAt)
    {
        s_auditionOffAt = 0;
        if (!auditioning)
            auditionOff(track);
    }
}

// ---------------------------------------------------------------------------
void cGibGobPages::moveCursor(int dCol, int dRow)
{
    if (page == kPageTrack)
    {
        row = clampInt(row + dRow, 0, kTrackRowCount - 1);
        if (dCol != 0)
        {
            // left / right steps through the tracks
            auditionOff(track);
            track = clampInt(track + dCol, 0, kTracks - 1);
        }
    }
    else
    {
        row = clampInt(row + dRow, 0, 7);
        col = clampInt(col + dCol, 1, 4);
    }
    refresh();
}

void cGibGobPages::changeValue(int delta, bool big)
{
    Sequencer::strPattern* p = current();

    if (page == kPagePalette)
    {
        int idx = (shiftActive() ? 8 : 0) + row;
        PaletteChord c = getPaletteChord(p, idx);
        switch (col)
        {
            case 1: c.root = (uint8_t)clampInt(c.root + delta * (big ? 12 : 1), 0, 127); break;
            case 2:
                c.type = (uint8_t)(((c.type + delta * (big ? 4 : 1)) % kChordTypeCount + kChordTypeCount) %
                                   kChordTypeCount);
                break;
            case 3:
            {
                int n = chordType(c.type).count;
                c.inversion = (uint8_t)(((c.inversion + delta) % n + n) % n);
                break;
            }
            case 4:
                c.voicing = (uint8_t)(((c.voicing + delta) % kVoicingCount + kVoicingCount) % kVoicingCount);
                break;
            default: break;
        }
        c.inversion = (uint8_t)(c.inversion % chordType(c.type).count);
        setPaletteChord(p, idx, c);
        markChanged();

        uint8_t notes[kMaxChordNotes];
        int n = buildPaletteChord(c, notes, kMaxChordNotes);
        if (n > 0)
            auditionBriefly(track, notes, n);
        refresh();
        return;
    }

    TrackSettings s = getSettings(p, track);
    bool settingsChanged = true;
    int step = big ? 10 : 1;

    switch (row)
    {
        case kRowType:
            stopTrack(track);
            setTrackType(p, track, delta > 0 ? TrackType::Melodic : TrackType::Drum);
            settingsChanged = false;
            break;
        case kRowSound:
        {
            int instr = getSound(p, track);
            for (int tries = 0; tries < kTotalInstrumentsCount; tries++)
            {
                instr = (instr + (delta > 0 ? 1 : -1) + kTotalInstrumentsCount) % kTotalInstrumentsCount;
                if (soundUsable(instr))
                    break;
            }
            if (!soundUsable(instr))
            {
                mtPopups.show(0, "No sounds loaded yet", 1);
                return;
            }
            auditionOff(track);
            stopTrack(track);
            setSound(p, track, (uint8_t)instr);
            settingsChanged = false;
            break;
        }
        case kRowBaseNote:
        {
            s.baseNote = (uint8_t)clampInt(s.baseNote + delta * (big ? 12 : 1), 0, 127);
            break;
        }
        case kRowPatternLength:
        {
            int len = clampInt(p->track[0].length + 1 + delta * (big ? 4 : 1), 1, Sequencer::MAXSTEP + 1);
            for (int t = 0; t < kAllTracks; t++)
                p->track[t].length = (uint8_t)(len - 1);
            settingsChanged = false;
            break;
        }
        case kRowArp: s.arpMode = (uint8_t)clampInt(s.arpMode + delta, 0, kArpModeCount - 1); break;
        case kRowArpRate: s.arpRate = (uint8_t)clampInt(s.arpRate + delta, 0, kArpRateCount - 1); break;
        case kRowArpOctaves: s.arpOctaves = (uint8_t)clampInt(s.arpOctaves + delta, 1, 4); break;
        case kRowArpGate: s.arpGate = (uint8_t)clampInt(s.arpGate + delta * (big ? 10 : 5), 10, 100); break;
        case kRowApe: s.apeOn = delta > 0; break;
        case kRowApeRhythm: s.apeRhythm = (uint8_t)clampInt(s.apeRhythm + delta * step * (big ? 1 : 5), 0, 100); break;
        case kRowApeGate: s.apeGate = (uint8_t)clampInt(s.apeGate + delta * step * (big ? 1 : 5), 0, 100); break;
        case kRowApeNotes: s.apeNotes = (uint8_t)clampInt(s.apeNotes + delta * step * (big ? 1 : 5), 0, 100); break;
        default: return;
    }

    if (settingsChanged)
        setSettings(p, track, s);
    markChanged();

    if (row == kRowBaseNote)
    {
        uint8_t note = s.baseNote;
        auditionBriefly(track, &note, 1);
    }
    refresh();
}

void cGibGobPages::goBack()
{
    int t = track;
    hModule target = (origin == kFromPianoRoll) ? (hModule)&pianoRoll : (hModule)&gibgobHome;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(target, (uint32_t)t);
}

void cGibGobPages::topButton(int button, bool pressed)
{
    Sequencer::strPattern* p = current();

    if (page == kPagePalette)
    {
        if (!pressed)
        {
            auditioning = false;
            if (s_auditionOffAt == 0)
                auditionOff(track);
            return;
        }
        row = clampInt(button, 0, 7);
        PaletteChord c = getPaletteChord(p, (shiftActive() ? 8 : 0) + row);
        uint8_t notes[kMaxChordNotes];
        int n = buildPaletteChord(c, notes, kMaxChordNotes);
        if (n > 0)
        {
            s_auditionOffAt = 0;
            auditioning = true;
            auditionChord(track, notes, n, 100);
        }
        refresh();
        return;
    }

    if (!pressed)
        return;

    switch (button)
    {
        case 0: // Acid
            if (noteCount(p, track) > 0 && !shiftActive())
            {
                mtPopups.show(0, "Track has notes: turn Shift on to replace them", 2);
                return;
            }
            stopTrack(track);
            generateAcid(p, track, p->track[0].length + 1);
            markChanged();
            mtPopups.show(0, "Acid line generated", 1);
            break;
        case 1: // Quantize
            quantizeTrack(p, track);
            markChanged();
            mtPopups.show(0, "Notes snapped to the grid", 1);
            break;
        case 2: // Clear
            if (!shiftActive())
            {
                mtPopups.show(0, "Turn Shift on, then Clear, to erase this track", 2);
                return;
            }
            stopTrack(track);
            clearNotes(p, track);
            markChanged();
            mtPopups.show(0, "Track cleared", 1);
            break;
        case 3: // Mute
            mtProject.values.trackMute[track] = !mtProject.values.trackMute[track];
            engine.muteTrack((uint8_t)track, mtProject.values.trackMute[track]);
            if (mtProject.values.trackMute[track])
                stopTrack(track);
            newFileManager.setProjectStructChanged();
            break;
        case 5: // Chords
            switchPage(kPagePalette);
            return;
        case 6: // Piano roll
        {
            int t = track;
            mtInterface.deactivateModule(this);
            mtInterface.activateModule(&pianoRoll, (uint32_t)t);
            return;
        }
        case 7: goBack(); return;
        default: return;
    }
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

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s))
        GP->moveCursor(0, -1);
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s))
        GP->moveCursor(0, 1);
}
static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        GP->moveCursor(-1, 0);
}
static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        GP->moveCursor(1, 0);
}

static void functShift(uint8_t state)
{
    if (state != buttonPress)
        return;
    g_shiftLatched = !g_shiftLatched;
    GP->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    int idx = button - interfaceButton0;
    if (idx < 0 || idx > 7)
        return;
    if (state == buttonPress)
        GP->topButton(idx, true);
    else if (state == buttonRelease)
        GP->topButton(idx, false);
}

static void functEncoder(int16_t value)
{
    bool big = value >= 10 || value <= -10;
    GP->changeValue(value > 0 ? 1 : -1, big);
}

static void functPattern(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (GP->page == cGibGobPages::kPagePalette)
        GP->switchPage(cGibGobPages::kPageTrack);
    else
        GP->goBack();
}

static void functSwitchModule(uint8_t button)
{
    GP->eventFunct(eventSwitchModule, GP, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GP);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GP);
}
