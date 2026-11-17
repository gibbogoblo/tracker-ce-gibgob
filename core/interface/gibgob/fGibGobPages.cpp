// GibGob OS - settings pages
//
// Track page (Middle on a track name in the main sequencer)
//   Up/Down          choose a setting           small +/- = change it, big +/- = bigger steps
//   Middle on Sound  browse and preview samples, load one straight onto this track
//   Top row 1..8     switch to track 1..8
//   Pattern (Fn1)    back to where you came from
//
// Chord palette (3rd page of the synth parameters: press button 2 to reach it)
//   D-pad            choose a chord and a column (root / type / inversion / voicing)
//   small +/-        change it                  big +/- = octave / bigger steps
//   Top row 1..8     jump to that chord and hear it        Shift = second bank
//   Button 2         back to the first synth-parameters page

#include "gibgob/gibgobPages.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileBrowser/GlobalFileBrowser.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobChords.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/pianoRoll.h"
#include "instrumentEditor/instrumentEditor.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

cGibGobPages gibgobPages;
static cGibGobPages* GP = &gibgobPages;
static elapsedMillis s_clock;
static uint32_t s_auditionOffAt = 0;

// a sample import started from the track page
static bool s_importPending = false;
static bool s_inputsBlocked = false;
static int s_importTrack = 0;
static int s_importSlot = 0;

using namespace gibgob;

enum TrackRow
{
    kRowType = 0,
    kRowSound,
    kRowBaseNote,
    kRowLength,
    kTrackRowCount
};

static const char* const kTrackRowNames[kTrackRowCount] = { "Track type", "Sound", "Base note", "Length" };

enum EditKind
{
    kEditType = 40,
    kEditSound,
    kEditBase,
    kEditLength,
    kEditPalette,
};

static char s_barText[8][16];

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
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
    uint32_t o = (options >> 4) & 0xF;
    origin = o == kFromPianoRoll ? kFromPianoRoll : (o == kFromInstrumentEditor ? kFromInstrumentEditor : kFromHome);
    track = (int)(options & 7);
    row = 0;
    col = 1;
    auditioning = false;
    s_auditionOffAt = 0;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), page == kPagePalette ? "Chord palette" : "Track settings");
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
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

    ip->setButtonObj(interfaceButtonPattern, functPattern);
    bindModuleSwitchButtons(ip, functSwitchModule);
    if (page == kPagePalette && origin == kFromInstrumentEditor)
        ip->setButtonObj(interfaceButtonParams, functParams); // button 2 cycles the synth pages

    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cGibGobPages::switchPage(Page newPage)
{
    auditionOff(track);
    page = newPage;
    row = 0;
    col = 1;
    setDefaultScreenFunct();
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
            view.rowColor[r] = melodic ? color::kMelodic : color::kDrum;
        }

        char name[20];
        gibgobSoundName(getSound(p, track), name, sizeof(name));
        snprintf(view.cell[kRowType][1], 20, "%s", melodic ? "Melodic" : "Drum");
        snprintf(view.cell[kRowSound][1], 20, "%.19s", name);
        snprintf(view.cell[kRowBaseNote][1], 20, "%s", mtNotes[s.baseNote]);
        snprintf(view.cell[kRowLength][1], 20, "%d steps", trackSteps(p, track));

        if (s_importPending)
        {
            snprintf(view.mode, sizeof(view.mode), "LOADING");
            view.modeColor = color::kInstrBtn;
        }
        snprintf(view.title, sizeof(view.title), row == kRowSound ? "Track %d   Middle: load a sample" : "Track %d",
                 track + 1);

        // bottom bar: the 8 tracks
        for (int i = 0; i < 8; i++)
        {
            trackDisplayName(i, s_barText[i], 11);
            view.bar.text[i] = s_barText[i];
            view.bar.fill[i] = isMelodic(p, i) ? color::kMelodic : color::kDrum;
            view.bar.highlighted[i] = (i == track);
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

        snprintf(view.title, sizeof(view.title), "Chord palette   Bank %s   (plays on track %d)", bank ? "B" : "A",
                 track + 1);
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

    if (s_importPending)
    {
        if (!s_inputsBlocked)
        {
            ip->blockAllInputs(); // nothing may touch sample memory while it loads
            s_inputsBlocked = true;
            refresh();
        }
        uint8_t status = newFileManager.getStatus();
        if (status == fmImportSamplesEnd)
        {
            newFileManager.clearStatus();
            ip->unblockAllInputs();
            s_inputsBlocked = false;
            s_importPending = false;

            strInstrument& instr = mtProject.instrument[s_importSlot];
            instr.startPoint = 0;
            instr.loopPoint1 = 1;
            instr.loopPoint2 = MAX_16BIT - 1;
            instr.endPoint = MAX_16BIT;

            Sequencer::strPattern* p = current();
            stopTrack(s_importTrack);
            setSound(p, s_importTrack, (uint8_t)s_importSlot);
            markChanged();
            newFileManager.setProjectStructChanged();
            mtPopups.show(0, "Sample loaded onto the track", 1);
            refresh();
        }
        else if (status == fmImportSamplesError)
        {
            newFileManager.clearStatus();
            ip->unblockAllInputs();
            s_inputsBlocked = false;
            s_importPending = false;
            mtPopups.show(0, "Could not load that sample", 2);
            refresh();
        }
    }
}

// ---------------------------------------------------------------------------
void cGibGobPages::loadSample()
{
    if (s_importPending)
        return;
    Sequencer::strPattern* p = current();

    // the track's own sample slot; a synth track gets the slot with its number
    uint8_t sound = getSound(p, track);
    s_importSlot = isSampleInstrument(sound) ? sound : track;
    s_importTrack = track;

    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
        sequencer.stop(); // sample memory moves around while loading
    auditionOff(track);

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadWav;
    char title[40];
    snprintf(title, sizeof(title), "Load sample onto track %d", track + 1);
    cfg.title = title;
    cfg.contextKey = "gibgob_track_wav";
    cfg.initialDirectory = "/Samples";
    cfg.extensionFilter = ".wav";
    cfg.returnModule = this;
    cfg.returnModuleOptions = makeOptions(kPageTrack, origin, track);
    cfg.onConfirm = [](const std::string& fullPath)
    {
        auto slashPos = fullPath.find_last_of('/');
        std::string dirStr =
          (slashPos != std::string::npos && slashPos > 0) ? fullPath.substr(0, slashPos) : "";
        std::string fileStr = (slashPos != std::string::npos) ? fullPath.substr(slashPos + 1) : fullPath;
        auto dotPos = fileStr.find_last_of('.');
        if (dotPos != std::string::npos)
            fileStr = fileStr.substr(0, dotPos);

        char dirBuf[kMaxFilePathLen + 1]{};
        char nameBuf[kMaxFileNameLen + 1]{};
        strncpy(dirBuf, dirStr.c_str(), kMaxFilePathLen);
        strncpy(nameBuf, fileStr.c_str(), kMaxFileNameLen);

        newFileManager.flushWorkspaceSave();
        if (newFileManager.importSampleToProject(dirBuf, nameBuf, (uint8_t)s_importSlot))
        {
            s_importPending = true;
        }
        else
        {
            mtPopups.show(0, "Can't load: file too long or sample memory full", 2);
        }
    };

    openGlobalFileBrowser(cfg);
}

void cGibGobPages::pressMiddle()
{
    if (page == kPageTrack && row == kRowSound)
        loadSample();
}

void cGibGobPages::moveCursor(int dCol, int dRow)
{
    if (page == kPageTrack)
    {
        row = clampInt(row + dRow, 0, kTrackRowCount - 1);
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
        beginEdit(kEditPalette);
        setPaletteChord(p, idx, c);
        markChanged();

        uint8_t notes[kMaxChordNotes];
        int n = buildPaletteChord(c, notes, kMaxChordNotes);
        if (n > 0)
            auditionBriefly(track, notes, n);
        refresh();
        return;
    }

    switch (row)
    {
        case kRowType:
            beginEdit(kEditType);
            stopTrack(track);
            setTrackType(p, track, delta > 0 ? TrackType::Melodic : TrackType::Drum);
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
                mtPopups.show(0, "No sounds loaded yet: press Middle to load one", 2);
                return;
            }
            beginEdit(kEditSound);
            auditionOff(track);
            stopTrack(track);
            setSound(p, track, (uint8_t)instr);
            break;
        }
        case kRowBaseNote:
        {
            beginEdit(kEditBase);
            TrackSettings s = getSettings(p, track);
            s.baseNote = (uint8_t)clampInt(s.baseNote + delta * (big ? 12 : 1), 0, 127);
            setSettings(p, track, s);
            uint8_t note = s.baseNote;
            auditionBriefly(track, &note, 1);
            break;
        }
        case kRowLength:
            beginEdit(kEditLength);
            setTrackSteps(p, track, trackSteps(p, track) + delta * (big ? 4 : 1));
            break;
        default: return;
    }
    markChanged();
    refresh();
}

void cGibGobPages::goBack()
{
    int t = track;
    if (origin == kFromInstrumentEditor)
    {
        backToInstrumentEditor();
        return;
    }
    hModule target = (origin == kFromPianoRoll) ? (hModule)&pianoRoll : (hModule)&gibgobHome;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(target, (uint32_t)t);
}

void cGibGobPages::backToInstrumentEditor()
{
    // the first synth-parameters page for the instrument being edited
#if GUI_HANDLE_SYNTH
    if (mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX)
        instrumentEditor.mode = mtInstEditModeSynth;
    else
#endif
      if (mtProject.values.lastUsedInstrument >= MIDI_INSTRUMENTS_START_IDX)
        instrumentEditor.mode = mtInstEditModeMidi;
    else
        instrumentEditor.mode = mtInstEditModeParams;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&instrumentEditor, 0);
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
    auditionOff(track);
    track = clampInt(button, 0, kTracks - 1);
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

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
        GP->pressMiddle();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
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
    if (GP->origin == cGibGobPages::kFromInstrumentEditor)
    {
        // Pattern from the synth pages goes to the main sequencer
        uint32_t t = (uint32_t)GP->track;
        mtInterface.deactivateModule(GP);
        mtInterface.activateModule(&gibgobHome, t);
        return;
    }
    GP->goBack();
}

static void functParams(uint8_t state)
{
    if (state == buttonPress)
        GP->backToInstrumentEditor();
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
