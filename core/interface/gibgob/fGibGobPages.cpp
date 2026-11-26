// GibGob OS - settings pages: track settings and the chord palette
// Every shortcut is listed in gibgobHelp.h (Shift + Menu shows them).

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
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobLibrary.h"
#include "gibgob/drumOverview.h"
#include "gibgob/gibgobUi.h"
#include "gibgob/pianoRoll.h"
#include "instrumentEditor/instrumentEditor.h"
#include "gibgob/gibgobChordList.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include "cr_section_macros.h"

#include <new>
#include <stdio.h>
#include <string.h>

cGibGobPages gibgobPages;
static cGibGobPages* GP = &gibgobPages;
static elapsedMillis s_clock;
static uint32_t s_auditionOffAt = 0;
// the Chord Canopy's chord list (Insert)
static bool s_listOpen = false;
static int s_listIndex = 0;
static int s_listKey = 0;

// a sample import started from the track page
static bool s_importPending = false;
static bool s_inputsBlocked = false;
static int s_importTrack = 0;
static int s_importSlot = 0;

// what the library asked for when it handed back to this page
static std::string s_queuedImport;
static bool s_queuedFolders = false;

static void importFile(const std::string& fullPath);

using namespace gibgob;

enum TrackRow
{
    kRowName = 0,
    kRowType,
    kRowSound,
    kRowBaseNote,
    kRowLength,
    kTrackRowCount
};

static const char* const kTrackRowNames[kTrackRowCount] = { "Name", "Track type", "Sound", "Base note", "Length" };
static constexpr int kTrackNameMax = 12;

// renaming with the on-screen keyboard
static bool s_naming = false;
static void startNaming();
static void finishNaming(bool save);
static void setNamingFunct();

enum EditKind
{
    kEditType = 40,
    kEditSound,
    kEditBase,
    kEditLength,
    kEditPalette,
};

static char s_barText[8][16];

// the Chord Canopy (the chord page): one chord at a time, then how it is played
enum CanopyField : uint8_t
{
    kFieldRoot = 0,
    kFieldType,
    kFieldInversion,
    kFieldVoicing,
    kFieldStrumSpeed,
    kFieldStrumDir,
    kFieldArpMode,
    kFieldArpRate,
    kFieldArpOctaves,
    kFieldArpGate,
    kFieldHumTime,
    kFieldHumVel,
};
static const char* const kStrumDirNames[kStrumDirCount] = { "Up", "Down", "Alt" };
static const char* const kFeelNames[kFeelModeCount] = { "Chord", "Strum", "Arp", "Human" };
static const uint32_t kFeelColors[kFeelModeCount] = { color::kNoteBtn, color::kInstrBtn, color::kFx1Btn, color::kFx2Btn };

// the values shown for a way of playing: the chord's four, then the mode's
static int canopyFields(int mode, uint8_t* ids)
{
    int n = 0;
    ids[n++] = kFieldRoot;
    ids[n++] = kFieldType;
    ids[n++] = kFieldInversion;
    ids[n++] = kFieldVoicing;
    if (mode == kFeelStrum)
    {
        ids[n++] = kFieldStrumSpeed;
        ids[n++] = kFieldStrumDir;
    }
    else if (mode == kFeelArp)
    {
        ids[n++] = kFieldArpMode;
        ids[n++] = kFieldArpRate;
        ids[n++] = kFieldArpOctaves;
        ids[n++] = kFieldArpGate;
    }
    else if (mode == kFeelHuman)
    {
        ids[n++] = kFieldHumTime;
        ids[n++] = kFieldHumVel;
    }
    return n;
}

// the page's picture data lives in external RAM (the fast RAM is full)
alignas(CanopyView) __NOINIT(EXTERNAL_RAM) static uint8_t s_canopyMem[sizeof(CanopyView)];
static CanopyView* s_canopy = nullptr;
static CanopyView& canopy()
{
    if (s_canopy == nullptr)
        s_canopy = new (s_canopyMem) CanopyView();
    return *s_canopy;
}

static void functColour(uint8_t button, uint8_t state);

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functInsert(uint8_t state);
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
    screenOpened();
    view.help.active = false;
    page = ((options >> 8) & 0xF) == kPagePalette ? kPagePalette : kPageTrack;
    uint32_t o = (options >> 4) & 0xF;
    origin = o == kFromPianoRoll          ? kFromPianoRoll
             : o == kFromInstrumentEditor ? kFromInstrumentEditor
             : o == kFromDrums            ? kFromDrums
                                          : kFromHome;
    track = (int)(options & 7);
    row = 0;
    col = page == kPagePalette ? 0 : 1;
    auditioning = false;
    s_auditionOffAt = 0;

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), page == kPagePalette ? "Chord Canopy" : "Track settings");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobPages::stop()
{
    if (s_naming)
    {
        keyboardManager.deactivateKeyboard();
        s_naming = false;
    }
    for (int b = 0; b < 8; b++)
        chordRelease(track, b); // a chord still held stops
    auditionOff(track);
    auditioning = false;
    s_auditionOffAt = 0;
    s_listOpen = false;
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
    ip->setButtonObj(interfaceButtonParams, functParams);

    ip->setPotObj(interfacePot0, functEncoder, false);

    // the Chord Canopy: Note / Instr / FX1 / FX2 choose how chords are played
    if (page == kPagePalette)
    {
        ip->setButtonObj(interfaceButtonNote, functColour);
        ip->setButtonObj(interfaceButtonInstr, functColour);
        ip->setButtonObj(physFx1Button(), functColour);
        ip->setButtonObj(physFx2Button(), functColour);
        ip->setButtonObj(interfaceButtonInsert, functInsert); // the chord list
    }
}

void cGibGobPages::setFeelMode(int mode)
{
    Sequencer::strPattern* p = current();
    TrackSettings s = getSettings(p, track);
    if (s.feel.mode == mode)
        return;
    for (int b = 0; b < 8; b++)
        chordRelease(track, b); // a held chord stops; the next one plays the new way
    s.feel.mode = (uint8_t)mode;
    if (mode == kFeelHuman && s.feel.humTime == 0 && s.feel.humVel == 0)
    {
        s.feel.humTime = 2; // something to hear straight away
        s.feel.humVel = 2;
    }
    beginEdit(kEditPalette);
    setSettings(p, track, s);
    markChanged();
    uint8_t ids[CanopyView::kMaxFields];
    const int n = canopyFields(mode, ids);
    col = mode == kFeelChord ? clampInt(col, 0, 3) : (n > 4 ? 4 : n - 1); // onto the mode's first value
    refresh();
}

static void functColour(uint8_t button, uint8_t state)
{
    if (state != buttonPress || GP->page != cGibGobPages::kPagePalette || GP->view.help.active)
        return;
    if (button == interfaceButtonNote)
        GP->setFeelMode(kFeelChord);
    else if (button == interfaceButtonInstr)
        GP->setFeelMode(kFeelStrum);
    else if (button == physFx1Button())
        GP->setFeelMode(kFeelArp);
    else if (button == physFx2Button())
        GP->setFeelMode(kFeelHuman);
}

void cGibGobPages::switchPage(Page newPage)
{
    auditionOff(track);
    s_listOpen = false;
    page = newPage;
    row = 0;
    col = page == kPagePalette ? 0 : 1;
    setDefaultScreenFunct();
    refresh();
}

// ---------------------------------------------------------------------------
void cGibGobPages::refresh()
{
    const Sequencer::strPattern* p = current();
    view.shift = shiftActive();
    view.mode[0] = 0;
    if (page == kPagePalette)
    {
        view.help.title = "Chord Canopy - shortcuts";
        view.help.lines = kHelpPalette;
        view.help.count = (int)(sizeof(kHelpPalette) / sizeof(kHelpPalette[0]));
    }
    else
    {
        view.help.title = "Track settings - shortcuts";
        view.help.lines = kHelpTrackPage;
        view.help.count = (int)(sizeof(kHelpTrackPage) / sizeof(kHelpTrackPage[0]));
    }

    if (page == kPageTrack)
    {
        view.canopy = nullptr;
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

        const bool slice = isSliceTrack(p, track);
        for (int r = 0; r < kTrackRowCount; r++)
        {
            snprintf(view.cell[r][0], sizeof(view.cell[r][0]), "%s", kTrackRowNames[r]);
            view.rowColor[r] = slice ? color::kSlice : (melodic ? color::kMelodic : color::kDrum);
        }

        char name[20];
        trackDisplayName(track, name, sizeof(name));
        snprintf(view.cell[kRowName][1], 20, "%.19s", name);
        gibgobSoundName(getSound(p, track), name, sizeof(name));
        // a beat-slice sample makes the track a Slice track whatever its type
        snprintf(view.cell[kRowType][1], 20, "%s", slice ? "Slice (auto)" : (melodic ? "Melodic" : "Drum"));
        snprintf(view.cell[kRowSound][1], 20, "%.19s", name);
        snprintf(view.cell[kRowBaseNote][1], 20, "%s", mtNotes[s.baseNote]);
        snprintf(view.cell[kRowLength][1], 20, "%d steps", trackSteps(p, track));

        if (s_importPending)
        {
            snprintf(view.mode, sizeof(view.mode), "LOADING");
            view.modeColor = color::kInstrBtn;
        }
        snprintf(view.title, sizeof(view.title),
                 row == kRowSound  ? "Track %d   Middle: load a sample"
                 : row == kRowName ? "Track %d   Middle: rename"
                                   : "Track %d",
                 track + 1);
        if (s_naming)
        {
            snprintf(view.title, sizeof(view.title), "Rename track %d", track + 1);
            static const char* const kKeys[8] = { "Enter", "", "", "", "", "", "Cancel", "Save" };
            for (int i = 0; i < 8; i++)
            {
                view.bar.text[i] = kKeys[i];
                view.bar.fill[i] = 0;
                view.bar.highlighted[i] = false;
            }
            view.keyboard = true;
            display.refreshControl(table);
            return;
        }
        view.keyboard = false;

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
        // the Chord Canopy: the chord of Shift + top `row` in the piano roll
        CanopyView& v = canopy();
        view.canopy = &v;
        const TrackSettings s = getSettings(p, track);
        const ChordFeel& f = s.feel;
        row = clampInt(row, 0, 7);
        uint8_t ids[CanopyView::kMaxFields];
        v.fieldCount = canopyFields(f.mode, ids);
        col = clampInt(col, 0, v.fieldCount - 1);
        v.cursor = col;
        v.mode = f.mode;
        v.shift = view.shift;
        v.chordIndex = row;

        const PaletteChord c = getPaletteChord(p, row);
        paletteChordName(c, v.chordName, sizeof(v.chordName));
        v.noteCount = buildPaletteChord(c, v.notes, CanopyView::kMaxNotes);
        v.soundingCount = auditionSounding(track, v.sounding, CanopyView::kMaxNotes);
        snprintf(v.title, sizeof(v.title), s_listOpen ? "Track %d   Insert: close the list" : "Track %d   Insert: Chord list",
                 track + 1);
        v.listOpen = s_listOpen;
        if (s_listOpen)
        {
            static const char* const kKeys[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
            v.listCount = kChordListCount;
            s_listIndex = clampInt(s_listIndex, 0, kChordListCount - 1);
            v.listIndex = s_listIndex;
            snprintf(v.listKey, sizeof(v.listKey), "%s", kKeys[s_listKey % 12]);
            // a window of rows around the highlighted one
            const int rows = CanopyView::kListRows;
            int first = clampInt(s_listIndex - rows / 2, 0, kChordListCount > rows ? kChordListCount - rows : 0);
            v.listShown = 0;
            for (int i = first; i < kChordListCount && v.listShown < rows; i++)
            {
                CanopyView::ListRow& r = v.listRows[v.listShown];
                const ChordListEntry& e = kChordList[i];
                snprintf(r.name, sizeof(r.name), "%s", e.name);
                r.style = e.style;
                int len = 0;
                r.chords[0] = 0;
                for (int k = 0; k < 8 && len < (int)sizeof(r.chords) - 1; k++)
                {
                    char nm[12];
                    chordListShortName(s_listKey + e.root[k], e.type[k], nm, sizeof(nm));
                    len += snprintf(r.chords + len, sizeof(r.chords) - (size_t)len, k ? " %s" : "%s", nm);
                }
                if (i == s_listIndex)
                    v.listCursor = v.listShown;
                v.listShown++;
            }
        }

        // the four ways of playing on the coloured buttons
        for (int m = 0; m < kFeelModeCount; m++)
        {
            snprintf(v.params[m].label, sizeof(v.params[m].label), "%s", kFeelNames[m]);
            v.params[m].color = kFeelColors[m];
        }
        snprintf(v.params[kFeelChord].value, sizeof(v.params[0].value), "all");
        snprintf(v.params[kFeelStrum].value, sizeof(v.params[0].value), "%d", f.strum);
        snprintf(v.params[kFeelArp].value, sizeof(v.params[0].value), "%s", kArpRateNames[clampInt(s.arpRate, 0, kArpRateCount - 1)]);
        snprintf(v.params[kFeelHuman].value, sizeof(v.params[0].value), "%d/%d", f.humTime, f.humVel);

        for (int i = 0; i < v.fieldCount; i++)
        {
            CanopyView::Field& fd = v.fields[i];
            fd.meter = 0;
            fd.meterMax = 0;
            fd.color = i < 4 ? 0xFFC83A : kFeelColors[f.mode];
            const int n = chordType(c.type).count;
            switch (ids[i])
            {
                case kFieldRoot: snprintf(fd.label, 10, "Root"); snprintf(fd.value, 12, "%s", mtNotes[c.root]); break;
                case kFieldType: snprintf(fd.label, 10, "Type"); snprintf(fd.value, 12, "%s", chordType(c.type).label); break;
                case kFieldInversion: snprintf(fd.label, 10, "Inversion"); snprintf(fd.value, 12, "%d", c.inversion % n); break;
                case kFieldVoicing:
                    snprintf(fd.label, 10, "Voicing");
                    snprintf(fd.value, 12, "%s", kVoicingNames[c.voicing < kVoicingCount ? c.voicing : 0]);
                    break;
                case kFieldStrumSpeed:
                    snprintf(fd.label, 10, "Speed");
                    snprintf(fd.value, 12, "%d", f.strum);
                    fd.meter = (f.strum + 1) / 2;
                    fd.meterMax = 5;
                    break;
                case kFieldStrumDir: snprintf(fd.label, 10, "Direction"); snprintf(fd.value, 12, "%s", kStrumDirNames[f.strumDir % kStrumDirCount]); break;
                case kFieldArpMode: snprintf(fd.label, 10, "Mode"); snprintf(fd.value, 12, "%s", kArpModeNames[clampInt(f.arpMode, 1, kArpModeCount - 1)]); break;
                case kFieldArpRate: snprintf(fd.label, 10, "Rate"); snprintf(fd.value, 12, "%s", kArpRateNames[clampInt(s.arpRate, 0, kArpRateCount - 1)]); break;
                case kFieldArpOctaves:
                    snprintf(fd.label, 10, "Octaves");
                    snprintf(fd.value, 12, "%d", s.arpOctaves);
                    fd.meter = s.arpOctaves;
                    fd.meterMax = 4;
                    break;
                case kFieldArpGate: snprintf(fd.label, 10, "Gate"); snprintf(fd.value, 12, "%d%%", s.arpGate); break;
                case kFieldHumTime:
                    snprintf(fd.label, 10, "Time");
                    snprintf(fd.value, 12, "%d", f.humTime);
                    fd.meter = f.humTime;
                    fd.meterMax = kHumanMax;
                    break;
                default:
                    snprintf(fd.label, 10, "Velocity");
                    snprintf(fd.value, 12, "%d", f.humVel);
                    fd.meter = f.humVel;
                    fd.meterMax = kHumanMax;
                    break;
            }
        }

        // bottom bar: the 8 chords
        for (int r = 0; r < 8; r++)
        {
            paletteChordName(getPaletteChord(p, r), s_barText[r], sizeof(s_barText[r]));
            v.bar.text[r] = s_barText[r];
            v.bar.fill[r] = 0x2A7340;
            v.bar.highlighted[r] = (r == row);
        }
    }

    display.refreshControl(table);
}

void cGibGobPages::update()
{
    // the library handed back with something to do (done here, not in start())
    if (!s_queuedImport.empty() && page == kPageTrack && !s_importPending)
    {
        const std::string path = s_queuedImport;
        s_queuedImport.clear();
        prepareImport();
        importFile(path);
        refresh();
    }
    else if (s_queuedFolders && page == kPageTrack)
    {
        s_queuedFolders = false;
        loadSample();
        return;
    }

    if (page == kPagePalette && s_canopy != nullptr && view.canopy != nullptr)
    {
        uint8_t now[CanopyView::kMaxNotes];
        const int n = auditionSounding(track, now, CanopyView::kMaxNotes);
        if (n != s_canopy->soundingCount || memcmp(now, s_canopy->sounding, (size_t)n) != 0)
            refresh(); // a note started or stopped: light its fruit
    }

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
// a file picked in the library or the folder browser: load it onto the track
static void importFile(const std::string& fullPath)
{
    auto slashPos = fullPath.find_last_of('/');
    std::string dirStr = (slashPos != std::string::npos && slashPos > 0) ? fullPath.substr(0, slashPos) : "";
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
        s_importPending = true;
    else
        mtPopups.show(0, "Can't load: file too long or sample memory full", 2);
}

void cGibGobPages::queueImport(const char* fullPath)
{
    s_queuedImport = fullPath != nullptr ? fullPath : "";
    s_queuedFolders = false;
}

void cGibGobPages::queueFolderBrowser()
{
    s_queuedImport.clear();
    s_queuedFolders = true;
}

void cGibGobPages::prepareImport()
{
    Sequencer::strPattern* p = current();
    // the track's own sample slot; a synth track gets the slot with its number
    uint8_t sound = getSound(p, track);
    s_importSlot = isSampleInstrument(sound) ? sound : track;
    s_importTrack = track;
    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
        sequencer.stop(); // sample memory moves around while loading
    auditionOff(track);
}

void cGibGobPages::openLibrary()
{
    if (s_importPending)
        return;
    auditionOff(track);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobLibrary, cGibGobLibrary::makeOptions(track, origin));
}

void cGibGobPages::loadSample()
{
    if (s_importPending)
        return;
    prepareImport();

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadWav;
    char title[40];
    snprintf(title, sizeof(title), "Load sample onto track %d", track + 1);
    cfg.title = title;
    cfg.contextKey = "gibgob_track_wav";
    cfg.initialDirectory = "/Instruments";
    cfg.rememberLastPath = false; // always start in /Instruments
    cfg.extensionFilter = ".wav";
    cfg.returnModule = this;
    cfg.returnModuleOptions = makeOptions(kPageTrack, origin, track);
    cfg.onConfirm = [](const std::string& fullPath) { importFile(fullPath); };

    openGlobalFileBrowser(cfg);
}

// the chord list: open it (the key of chord 1) or close it
void cGibGobPages::toggleChordList()
{
    if (page != kPagePalette)
        return;
    s_listOpen = !s_listOpen;
    if (s_listOpen)
        s_listKey = getPaletteChord(current(), 0).root % 12;
    refresh();
}

// the first chord of the highlighted progression, briefly
void cGibGobPages::auditionListChord()
{
    PaletteChord pc[8];
    chordListChords(s_listIndex, s_listKey, pc);
    uint8_t notes[kMaxChordNotes];
    const int n = buildPaletteChord(pc[0], notes, kMaxChordNotes);
    if (n > 0)
        auditionBriefly(track, notes, n);
}

// the highlighted progression into chords 1-8 (Shift + Rec undoes it)
void cGibGobPages::loadChordList()
{
    Sequencer::strPattern* p = current();
    PaletteChord pc[8];
    chordListChords(s_listIndex, s_listKey, pc);
    beginEdit(kEditPalette);
    for (int i = 0; i < 8; i++)
        setPaletteChord(p, i, pc[i]);
    markChanged();
    s_listOpen = false;
    row = 0;
    uint8_t notes[kMaxChordNotes];
    const int n = buildPaletteChord(pc[0], notes, kMaxChordNotes);
    if (n > 0)
        auditionBriefly(track, notes, n);
    refresh();
}

void cGibGobPages::pressMiddle()
{
    if (page == kPagePalette && s_listOpen)
    {
        loadChordList();
        return;
    }
    if (page == kPageTrack && row == kRowSound)
        openLibrary();
    else if (page == kPageTrack && row == kRowName)
        startNaming();
}

// ---------------------------------------------------------------------------
// renaming

static void namingEnter()
{
    GP->keyboardManager.confirmKey();
}
static void namingLeft()
{
    GP->keyboardManager.makeMove('a');
}
static void namingRight()
{
    GP->keyboardManager.makeMove('d');
}
static void namingUp()
{
    GP->keyboardManager.makeMove('w');
}
static void namingDown()
{
    GP->keyboardManager.makeMove('s');
}
static void namingBackspace()
{
    GP->keyboardManager.makeBackspace();
}
static void namingCancel()
{
    finishNaming(false);
}
static void namingSave()
{
    finishNaming(true);
}

static void setNamingFunct()
{
    InputProcessor* ip = GP->ip;
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->setButtonObj(interfaceButtonLeft, buttonPress, namingLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, namingRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, namingUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, namingDown);
    ip->setButtonObj(interfaceButton0, buttonPress, namingEnter);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, namingEnter);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, namingEnter);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, namingBackspace);
    ip->setButtonObj(interfaceButton6, buttonPress, namingCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, namingSave);
}

static void startNaming()
{
    char name[kTrackNameMax + 8];
    trackDisplayName(GP->track, name, sizeof(name));
    name[kTrackNameMax] = 0;
    GP->keyboardManager.fillName(name);
    GP->keyboardManager.activateKeyboard(kTrackNameMax);
    s_naming = true;
    setNamingFunct();
    GP->refresh();
}

static void finishNaming(bool save)
{
    if (save)
    {
        int t = GP->track;
        const char* name = GP->keyboardManager.getName();
        if (name == nullptr || name[0] == 0)
            snprintf(mtProject.values.TrackNames[t], sizeof(mtProject.values.TrackNames[t]), "Track %d", t + 1);
        else
            snprintf(mtProject.values.TrackNames[t], sizeof(mtProject.values.TrackNames[t]), "%.12s", name);
        newFileManager.setProjectStructChanged();
    }
    GP->keyboardManager.deactivateKeyboard();
    s_naming = false;
    GP->setDefaultScreenFunct();
    GP->refresh();
}

void cGibGobPages::moveCursor(int dCol, int dRow)
{
    if (page == kPageTrack)
    {
        row = clampInt(row + dRow, 0, kTrackRowCount - 1);
    }
    else
    {
        if (s_listOpen)
        {
            // the list: Up/Down a progression, Left/Right the key
            if (dRow != 0)
            {
                s_listIndex = clampInt(s_listIndex + dRow, 0, kChordListCount - 1);
                auditionListChord();
            }
            if (dCol != 0)
            {
                s_listKey = ((s_listKey + dCol) % 12 + 12) % 12;
                auditionListChord();
            }
            refresh();
            return;
        }
        uint8_t ids[CanopyView::kMaxFields];
        const int n = canopyFields(getSettings(current(), track).feel.mode, ids);
        row = clampInt(row + dRow, 0, 7); // Up/Down: the previous / next chord
        col = clampInt(col + dCol, 0, n - 1);
    }
    refresh();
}

void cGibGobPages::changeValue(int delta, bool big)
{
    Sequencer::strPattern* p = current();

    if (page == kPagePalette && s_listOpen)
    {
        s_listIndex = clampInt(s_listIndex + delta * (big ? 4 : 1), 0, kChordListCount - 1);
        auditionListChord();
        refresh();
        return;
    }
    if (page == kPagePalette)
    {
        TrackSettings s = getSettings(p, track);
        ChordFeel& f = s.feel;
        PaletteChord c = getPaletteChord(p, row);
        uint8_t ids[CanopyView::kMaxFields];
        const int n = canopyFields(f.mode, ids);
        const int field = ids[clampInt(col, 0, n - 1)];
        auto wrap = [](int v, int count) { return ((v % count) + count) % count; };
        beginEdit(kEditPalette);
        if (field <= kFieldVoicing)
        {
            switch (field)
            {
                case kFieldRoot: c.root = (uint8_t)clampInt(c.root + delta * (big ? 12 : 1), 0, 127); break;
                case kFieldType: c.type = (uint8_t)wrap(c.type + delta * (big ? 4 : 1), kChordTypeCount); break;
                case kFieldInversion:
                {
                    const int count = chordType(c.type).count;
                    c.inversion = (uint8_t)wrap(c.inversion + delta, count);
                    break;
                }
                default: c.voicing = (uint8_t)wrap(c.voicing + delta, kVoicingCount); break;
            }
            c.inversion = (uint8_t)(c.inversion % chordType(c.type).count);
            setPaletteChord(p, row, c);
            uint8_t notes[kMaxChordNotes];
            const int count = buildPaletteChord(c, notes, kMaxChordNotes);
            if (count > 0)
                auditionBriefly(track, notes, count);
        }
        else
        {
            switch (field)
            {
                case kFieldStrumSpeed: f.strum = (uint8_t)clampInt(f.strum + delta * (big ? 5 : 1), 1, kStrumMax); break;
                case kFieldStrumDir: f.strumDir = (uint8_t)wrap(f.strumDir + delta, kStrumDirCount); break;
                case kFieldArpMode: f.arpMode = (uint8_t)(wrap(f.arpMode - 1 + delta, kArpModeCount - 1) + 1); break;
                case kFieldArpRate: s.arpRate = (uint8_t)clampInt(s.arpRate + delta, 0, kArpRateCount - 1); break;
                case kFieldArpOctaves: s.arpOctaves = (uint8_t)clampInt(s.arpOctaves + delta, 1, 4); break;
                case kFieldArpGate: s.arpGate = (uint8_t)clampInt(s.arpGate + delta * (big ? 25 : 5), 10, 100); break;
                case kFieldHumTime: f.humTime = (uint8_t)clampInt(f.humTime + delta, 0, kHumanMax); break;
                default: f.humVel = (uint8_t)clampInt(f.humVel + delta, 0, kHumanMax); break;
            }
            setSettings(p, track, s);
        }
        markChanged();
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
        {
            beginEdit(kEditLength);
            int steps = trackSteps(p, track);
            setTrackSteps(p, track, big ? snapLength(steps, delta) : steps + delta);
            break;
        }
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
    mtInterface.deactivateModule(this);
    if (origin == kFromPianoRoll)
        mtInterface.activateModule(&pianoRoll, (uint32_t)t);
    else if (origin == kFromDrums)
        mtInterface.activateModule(&drumOverview, homeOptions(t));
    else
        mtInterface.activateModule(&gibgobHome, homeOptions(t));
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
            if (chordHeld(track))
            {
                chordRelease(track, button);
                if (g_recArmed && sequencer.isPlay())
                    markChanged();
            }
            if (s_auditionOffAt == 0 && !chordHeld(track))
                auditionOff(track);
            return;
        }
        // the chord of Shift + this button in the piano roll, played with the track's feel
        row = clampInt(button, 0, 7); // the Canopy shows this chord
        PaletteChord c = getPaletteChord(p, row);
        uint8_t notes[kMaxChordNotes];
        int n = buildPaletteChord(c, notes, kMaxChordNotes);
        if (n > 0)
        {
            s_auditionOffAt = 0;
            auditioning = true;
            auditionOff(track);
            chordPress(track, button, notes, n, 100);
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
    if (middleBlocked(state))
        return;
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
    if (topRowBounce(idx, state))
        return; // the contact bouncing as the button is let go
    if (idx < 0 || idx > 7)
        return;
    if (state == buttonPress)
        GP->topButton(idx, true);
    else if (state == buttonRelease)
        GP->topButton(idx, false);
}

static void functInsert(uint8_t state)
{
    if (state == buttonPress)
        GP->toggleChordList();
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
        int t = GP->track;
        mtInterface.deactivateModule(GP);
        mtInterface.activateModule(&gibgobHome, homeOptions(t));
        return;
    }
    GP->goBack();
}

static void functParams(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (GP->page == cGibGobPages::kPagePalette && GP->origin == cGibGobPages::kFromInstrumentEditor)
        GP->backToInstrumentEditor(); // button 2 cycles the sound pages
    else
        openSoundParameters(GP, GP->track);
}

static void functSwitchModule(uint8_t button)
{
    GP->eventFunct(eventSwitchModule, GP, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && !mtMenuPopup.isPopupVisible() && (shiftActive() || GP->view.help.active))
    {
        toggleHelp(GP->view.help, GP->ip);
        GP->refresh();
        return;
    }
    if (GP->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(GP);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(GP);
}
