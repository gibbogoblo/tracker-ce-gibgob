#include "gibgob/Perform.h"

#include "TrackerImpl.h"
#include "gibgob/MelodicTrack.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "core/interfaceDefs.h"
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

namespace gibgob
{
namespace perform
{

namespace
{

__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t s_memory[kMemoryBudget];
Engine s_engine;

volatile bool s_playing = false;
volatile uint32_t s_ticks = 0;

const char* const kDir = "/GibGob";
const char* const kFile = "/GibGob/perform.cfg";
constexpr uint32_t kMagic = 0x46524550; // "PERF"

struct SavedCells
{
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    Cell cells[kCells];
};

void load()
{
    Cell cells[kCells];
    factoryCells(cells);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs != nullptr && fs->exists(kFile))
    {
        auto [ok, h] = fs->openFile(kFile, FileSystem::FileOpenMode::kRead);
        if (ok)
        {
            SavedCells saved;
            memset(&saved, 0, sizeof(saved));
            const int32_t n = fs->readFile(&saved, sizeof(saved), h);
            fs->closeFile(h);
            if (n == (int32_t)sizeof(saved) && saved.magic == kMagic && saved.version == 1)
                memcpy(cells, saved.cells, sizeof(cells));
        }
    }
    for (int i = 0; i < kCells; i++)
        s_engine.setCell(i, cells[i]); // setCell repairs anything out of range
}

} // namespace

Engine& engine()
{
    return s_engine;
}

void loadSaved()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    load();
    // until now the effects stage passed audio through untouched
    s_engine.attach(s_memory);
}

// ---- Drum cut / Melodic cut: mute the tracks of a type, sounds keep playing
// silently (a long sample comes back where it would be). Done on the voices
// (sample tracks) and on the synth parts the tracks use.
static bool s_cutDrums = false, s_cutMelodic = false, s_cutSlices = false;
static uint8_t s_cutVoices = 0;  // voices this is muting now
static uint8_t s_cutSynths = 0;  // synth parts this is muting now
static uint8_t s_order[kCells] = { 0 };
static uint8_t s_orderCounter = 0;

static void applyTrackCut()
{
    const Sequencer::strPattern* p = gibgob::current();
    auto typeCut = [&](int track)
    {
        if (gibgob::isSliceTrack(p, track))
            return s_cutDrums || s_cutSlices; // slices count as drums
        return gibgob::isMelodic(p, track) ? s_cutMelodic : s_cutDrums;
    };
    uint8_t voices = 0, synths = 0;
    if (s_cutDrums || s_cutMelodic || s_cutSlices)
    {
        for (int v = 0; v < gibgob::kTracks; v++)
            if (typeCut(gibgob::voiceOwner(v)))
                voices |= (uint8_t)(1u << v);
        for (int t = 0; t < gibgob::kTracks; t++)
        {
            const uint8_t sound = gibgob::getSound(p, t);
            if (isSynthInstrument(sound) && typeCut(t))
            {
                const int part = instrumentIdxToMidiChannel(sound) - 1;
                if (part >= 0 && part < 8)
                    synths |= (uint8_t)(1u << part);
            }
        }
    }
    for (int v = 0; v < gibgob::kTracks; v++)
    {
        const uint8_t bit = (uint8_t)(1u << v);
        if ((voices ^ s_cutVoices) & bit)
            ::engine.muteTrack((uint8_t)v, (voices & bit) || mtProject.values.trackMute[v] ? 1 : 0);
    }
#ifdef FEATURE_SYNTH
    for (int part = 0; part < kSynthParts; part++)
    {
        const uint8_t bit = (uint8_t)(1u << part);
        if ((synths ^ s_cutSynths) & bit)
            ::engine.gibgobCutSynth(part, (synths & bit) != 0);
    }
#endif
    s_cutVoices = voices;
    s_cutSynths = synths;
}

static void updateTrackCutFlags()
{
    bool drums = false, melodic = false, slices = false;
    for (int i = 0; i < kCells; i++)
    {
        if (!isHeld(i))
            continue;
        const Cell c = s_engine.liveCell(i);
        if (c.fx != kFxDrumCut && c.fx != kFxMelodicCut)
            continue;
        drums |= (s_engine.cellFlags(i) & kTwistCutDrums) != 0;
        melodic |= (s_engine.cellFlags(i) & kTwistCutMelodic) != 0;
        slices |= (s_engine.cellFlags(i) & kTwistCutSlices) != 0;
    }
    if (drums != s_cutDrums || melodic != s_cutMelodic || slices != s_cutSlices || drums || melodic || slices)
    {
        s_cutDrums = drums;
        s_cutMelodic = melodic;
        s_cutSlices = slices;
        applyTrackCut();
    }
}

void press(int cell)
{
    if (cell < 0 || cell >= kCells)
        return;
    s_order[cell] = ++s_orderCounter;
    s_engine.setHeld((uint8_t)(s_engine.heldMask() | (1u << cell)));
    updateTrackCutFlags();
}

void release(int cell)
{
    if (cell < 0 || cell >= kCells)
        return;
    s_engine.setHeld((uint8_t)(s_engine.heldMask() & ~(1u << cell)));
    updateTrackCutFlags();
}

void releaseAll()
{
    s_engine.releaseAll();
    updateTrackCutFlags();
}

bool anyHeld()
{
    return s_engine.heldMask() != 0;
}

// the twist being held: which button, which way
static int s_twistCell = -1;
static int s_twistDir = 0;

// + / - (small or big) or Up / Down still pressed
static bool twistButtonsDown()
{
    SwitchGroup* b = TrackerImpl::getInstance()->getPlatform()->getButtons();
    if (b == nullptr)
        return false;
    static const int kButtons[] = { interfaceButtonUp,         interfaceButtonDown,       interfaceButtonLeft,
                                    interfaceButtonRight,      interfaceButtonMiniPlus1,  interfaceButtonMiniPlus10,
                                    interfaceButtonMiniMinus1, interfaceButtonMiniMinus10 };
    for (int id : kButtons)
        if (b->isButtonPressed(id))
            return true;
    return false;
}

bool twist(int dir)
{
    int best = -1;
    for (int i = 0; i < kCells; i++)
        if (isHeld(i) && (best < 0 || s_order[i] > s_order[best]))
            best = i;
    if (best < 0)
        return false;
    if (s_twistCell >= 0 && s_twistCell != best)
        s_engine.twist(s_twistCell, 0);
    s_engine.twist(best, dir);
    s_twistCell = best;
    s_twistDir = dir;
    updateTrackCutFlags();
    return true;
}

void uiTick()
{
    // a twist lasts while its button is held
    if (s_twistDir != 0 && (s_twistCell < 0 || !isHeld(s_twistCell) || !twistButtonsDown()))
    {
        if (s_twistCell >= 0)
            s_engine.twist(s_twistCell, 0);
        s_twistCell = -1;
        s_twistDir = 0;
        updateTrackCutFlags();
    }
    if (s_cutDrums || s_cutMelodic || s_cutSlices || s_cutVoices || s_cutSynths)
        updateTrackCutFlags();
}

bool isHeld(int cell)
{
    return (s_engine.heldMask() >> (cell & 7)) & 1;
}

Cell getCell(int i)
{
    return s_engine.cell(i);
}

Cell getLiveCell(int i)
{
    return s_engine.liveCell(i);
}

void setCell(int i, const Cell& c)
{
    s_engine.setCell(i, c);
}

void save()
{
    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr)
        return;
    if (!fs->exists(kDir))
        fs->mkdir(0, kDir);
    SavedCells saved;
    memset(&saved, 0, sizeof(saved));
    saved.magic = kMagic;
    saved.version = 1;
    for (int i = 0; i < kCells; i++)
        saved.cells[i] = s_engine.cell(i);
    auto [ok, h] = fs->openFile(kFile, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
        return;
    fs->writeFile(&saved, sizeof(saved), h);
    fs->closeFile(h);
}

void clockTick(uint32_t globalPos, uint8_t uStep)
{
    if (!s_playing)
    {
        // bars count from the step playback started on
        s_ticks = globalPos * 48u + (uStep > 0 ? uStep - 1u : 0u);
        s_playing = true;
    }
    else
    {
        s_ticks = s_ticks + 1u;
    }
}

void clockStop()
{
    s_playing = false;
}

Clock currentClock()
{
    Clock c;
    c.playing = s_playing;
    c.ticks = s_ticks;
    c.tempo = sequencer.getCurrentTempo();
    return c;
}

} // namespace perform
} // namespace gibgob
