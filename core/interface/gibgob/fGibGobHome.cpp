// GibGob OS - main screen: 8 track tiles
//
// Controls
//   D-pad        select track
//   Middle       open track (Melodic -> piano roll, Drum -> tracker editor)
//   Top row      1 Type  2 Mute  3 Open  4 Sound-  5 Sound+  6 Clear(+Shift)  8 Tracker
//   small +/-    change sound of a Melodic track
//   Play         play / stop pattern (Shift+Play = song)
//   Pattern(Fn1) open the classic tracker for the selected track

#include "gibgob/gibgobHome.h"

#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/pianoRoll.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"

#include <stdio.h>
#include <string.h>

cGibGobHome gibgobHome;
static cGibGobHome* GH = &gibgobHome;

using namespace gibgob;

static const char* const kLabels[8] = { "Type", "Mute", "Open", "Sound -",
                                        "Sound +", "Clear", "", "Tracker" };

static void functPlay(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

// ---------------------------------------------------------------------------
void gibgobSoundName(uint8_t instrument, char* out, int outLen)
{
    if (isSampleInstrument(instrument))
    {
        const strInstrument& in = mtProject.instrument[instrument];
        if (in.isActive && in.sample.file_name[0])
            snprintf(out, outLen, "%02d %s", instrument + 1, in.sample.file_name);
        else
            snprintf(out, outLen, "%02d (empty)", instrument + 1);
    }
    else if (isSynthInstrument(instrument))
    {
        snprintf(out, outLen, "Synth %d", instrumentIdxToMidiChannel(instrument));
    }
    else
    {
        snprintf(out, outLen, "MIDI ch %d", instrumentIdxToMidiChannel(instrument));
    }
    out[outLen - 1] = 0;
}

// ---------------------------------------------------------------------------
void cGibGobHome::initDisplayControls()
{
    strControlProperties prop;
    prop.x = 0;
    prop.y = 29;
    prop.w = 800;
    prop.h = 394;
    prop.style = controlStyleShow;
    prop.data = &view;
    grid = display.createControl<TrackGridControl>(&prop);

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
        lp.text = kLabels[i];
        lp.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        lp.x = (800 / 8) * i + (800 / 16);
        lp.y = 424;
        lp.w = 800 / 8 - 6;
        lp.h = 55;
        label[i] = display.createControl<cLabel>(&lp);
    }
}

void cGibGobHome::start(uint32_t options)
{
    moduleRefresh = 1;
    cursor = (int)(options % kTracks);

    songTimer.show();
    snprintf(titleText, sizeof(titleText), "GibGob OS  -  Pattern %d", mtProject.values.actualPattern);
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refreshLabels();
    refreshTiles();
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cGibGobHome::stop()
{
    moduleRefresh = 0;
}

void cGibGobHome::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);

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

void cGibGobHome::refreshLabels()
{
    for (int i = 0; i < 8; i++)
    {
        display.setControlText(label[i], kLabels[i]);
        display.refreshControl(label[i]);
    }
}

void cGibGobHome::refreshTiles()
{
    const Sequencer::strPattern* p = current();
    int playTickAll = sequencer.isPlay() ? sequencer.getActualPos() : -1;

    for (int t = 0; t < kTracks; t++)
    {
        TrackTile& tile = view.tiles[t];
        tile.melodic = isMelodic(p, t);
        tile.muted = mtProject.values.trackMute[t];

        const char* name = mtProject.values.TrackNames[t];
        if (name[0])
            snprintf(tile.title, sizeof(tile.title), "%d  %s", t + 1, name);
        else
            snprintf(tile.title, sizeof(tile.title), "Track %d", t + 1);

        snprintf(tile.type, sizeof(tile.type), "%s", tile.melodic ? "MELODIC" : "DRUM");

        tile.active = false;
        if (tile.melodic)
        {
            gibgobSoundName(getSound(p, t), tile.sound, sizeof(tile.sound));
            int count = noteCount(p, t);
            snprintf(tile.detail, sizeof(tile.detail), "%d notes", count);

            int ph = playheadTick(t);
            for (int i = 0; ph >= 0 && i < count; i++)
            {
                Note n = getNote(p, t, i);
                if (ph >= n.start && ph < n.start + n.length)
                {
                    tile.active = true;
                    break;
                }
            }
        }
        else
        {
            int steps = 0;
            int firstInstr = -1;
            int len = p->track[t].length + 1;
            for (int s = 0; s < len; s++)
            {
                const auto& st = p->track[t].step[s];
                if (st.note >= 0)
                {
                    steps++;
                    if (firstInstr < 0)
                        firstInstr = st.instrument;
                }
            }
            if (firstInstr >= 0)
                gibgobSoundName((uint8_t)firstInstr, tile.sound, sizeof(tile.sound));
            else
                snprintf(tile.sound, sizeof(tile.sound), "(no steps)");
            snprintf(tile.detail, sizeof(tile.detail), "%d steps", steps);

            if (playTickAll >= 0 && playTickAll < len && p->track[t].step[playTickAll].note >= 0)
                tile.active = true;
        }
    }

    view.cursor = cursor;
    snprintf(view.info,
             sizeof(view.info),
             "Pattern %d   %d steps   %s",
             mtProject.values.actualPattern,
             p->track[0].length + 1,
             sequencer.isPlay() ? "PLAYING" : "STOPPED");

    display.refreshControl(grid);
}

void cGibGobHome::update()
{
    int pos = sequencer.isPlay() ? sequencer.getActualPos() : -1;
    if (pos != lastPlayStep)
    {
        lastPlayStep = pos;
        refreshTiles();
    }
}

// ---------------------------------------------------------------------------
void cGibGobHome::moveCursor(int dCol, int dRow)
{
    int c = cursor % 4 + dCol;
    int r = cursor / 4 + dRow;
    if (c < 0)
        c = 0;
    if (c > 3)
        c = 3;
    if (r < 0)
        r = 0;
    if (r > 1)
        r = 1;
    cursor = r * 4 + c;
    refreshTiles();
}

void cGibGobHome::openTrack()
{
    if (isMelodic(current(), cursor))
    {
        int t = cursor;
        mtInterface.deactivateModule(this);
        mtInterface.activateModule(&pianoRoll, (uint32_t)t);
    }
    else
    {
        openTracker();
    }
}

void cGibGobHome::openTracker()
{
    patternEditor.trackerPattern.actualTrack = (int8_t)cursor;
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&patternEditor, 0);
}

void cGibGobHome::toggleType()
{
    Sequencer::strPattern* p = current();
    bool toMelodic = !isMelodic(p, cursor);
    stopTrack(cursor);
    setTrackType(p, cursor, toMelodic ? TrackType::Melodic : TrackType::Drum);
    newFileManager.setActualPatternStructChanged();
    refreshTiles();
}

void cGibGobHome::toggleMute()
{
    mtProject.values.trackMute[cursor] = !mtProject.values.trackMute[cursor];
    engine.muteTrack(cursor, mtProject.values.trackMute[cursor]);
    if (mtProject.values.trackMute[cursor])
        stopTrack(cursor);
    newFileManager.setProjectStructChanged();
    refreshTiles();
}

static bool soundUsable(int instr)
{
    if (isSampleInstrument(instr))
        return mtProject.instrument[instr].isActive;
    return isSynthInstrument(instr);
}

void cGibGobHome::changeSound(int dir)
{
    Sequencer::strPattern* p = current();
    if (!isMelodic(p, cursor))
    {
        mtPopups.show(0, "Drum tracks pick sounds per step", 1);
        return;
    }
    int instr = getSound(p, cursor);
    for (int tries = 0; tries < kTotalInstrumentsCount; tries++)
    {
        instr = (instr + dir + kTotalInstrumentsCount) % kTotalInstrumentsCount;
        if (soundUsable(instr))
            break;
    }
    stopTrack(cursor);
    setSound(p, cursor, (uint8_t)instr);
    newFileManager.setActualPatternStructChanged();
    refreshTiles();
}

void cGibGobHome::clearTrack()
{
    if (!buttons->isButtonPressed(interfaceButtonShift))
    {
        mtPopups.show(0, "Hold Shift + Clear to erase this track", 1);
        return;
    }
    Sequencer::strPattern* p = current();
    stopTrack(cursor);
    if (isMelodic(p, cursor))
    {
        clearNotes(p, cursor);
    }
    else
    {
        for (auto& st : p->track[cursor].step)
        {
            st.note = Sequencer::STEP_NOTE_EMPTY;
            st.instrument = 0;
            st.fx[0].type = st.fx[0].value = 0;
            st.fx[1].type = st.fx[1].value = 0;
            st.volume = Sequencer::STEP_VELO_DEFAULT;
            st.micromove = -1;
        }
    }
    newFileManager.setActualPatternStructChanged();
    refreshTiles();
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
        if (GH->buttons->isButtonPressed(interfaceButtonShift))
            sequencer.playSong();
        else
            sequencer.playPattern();
    }
    else
    {
        sequencer.stop();
    }
    GH->refreshTiles();
}

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(0, -1);
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(0, 1);
}
static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(-1, 0);
}
static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        GH->moveCursor(1, 0);
}

static void functMiddle(uint8_t state)
{
    if (state == buttonPress)
        GH->openTrack();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;
    switch (button - interfaceButton0)
    {
        case 0: GH->toggleType(); break;
        case 1: GH->toggleMute(); break;
        case 2: GH->openTrack(); break;
        case 3: GH->changeSound(-1); break;
        case 4: GH->changeSound(1); break;
        case 5: GH->clearTrack(); break;
        case 7: GH->openTracker(); break;
        default: break;
    }
}

static void functEncoder(int16_t value)
{
    GH->changeSound(value > 0 ? 1 : -1);
}

static void functPattern(uint8_t state)
{
    if (state == buttonPress)
        GH->openTracker();
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
