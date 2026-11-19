#include "gibgob/Master.h"

#include "gibgob/MelodicTrack.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

// the tracks mixer reads these (libs/Audio/mixer.h)
volatile int32_t g_gibgobDuckQ16 = 65536;
volatile uint32_t g_gibgobDuckExempt = 0;

namespace gibgob
{
namespace master
{

namespace
{
constexpr uint32_t kMemoryBudget = 40u * 1024u;
__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t s_memory[kMemoryBudget];
Engine s_engine;
bool s_ready = false;

// song settings: MIDI instrument 0's reserved bytes, a marker in instrument 1's
constexpr uint8_t kSongMagic = 0xB3;
// a pattern's own: its 4 reserved float bytes + rezerwa1..3, a marker in rezerwa4
constexpr uint8_t kPatternMagic = 0xA7;
constexpr int kTracksMixerVoices = 8; // sample voices before the synth parts in the tracks mixer

uint8_t s_lastPacked[kPackedBytes] = { 0 };
bool s_havePacked = false;

void hookNoteStart(int track)
{
    s_engine.noteStarted(track);
}
} // namespace

static_assert(sizeof(((strMtValues*)0)->midiInstrument[0].reserved) >= kPackedBytes, "room for the song settings");

Engine& engine()
{
    return s_engine;
}

void init()
{
    if (s_ready)
        return;
    if (Engine::kMemoryBytes > kMemoryBudget)
        return; // (checked by the host tests too) the effects stay off
    s_ready = true;
    s_engine.attach(s_memory);
    g_noteStartHook = hookNoteStart;
}

Settings songSettings()
{
    const strMtValues& v = mtProject.values;
    if (v.midiInstrument[1].reserved[0] != kSongMagic)
        return Settings(); // an older project: everything off
    return unpack(v.midiInstrument[0].reserved);
}

void setSongSettings(const Settings& s)
{
    strMtValues& v = mtProject.values;
    pack(s, v.midiInstrument[0].reserved);
    v.midiInstrument[1].reserved[0] = kSongMagic;
}

bool patternHasOwn(const Sequencer::strPattern* p)
{
    return p != nullptr && p->rezerwa4 == kPatternMagic;
}

static void readPatternBytes(const Sequencer::strPattern* p, uint8_t out[kPackedBytes])
{
    memcpy(out, &p->tempoRezerwa, 4);
    out[4] = p->rezerwa1;
    out[5] = p->rezerwa2;
    out[6] = p->rezerwa3;
}

Settings patternSettings(const Sequencer::strPattern* p)
{
    if (!patternHasOwn(p))
        return songSettings();
    uint8_t b[kPackedBytes];
    readPatternBytes(p, b);
    return unpack(b);
}

void setPatternSettings(Sequencer::strPattern* p, const Settings& s)
{
    if (p == nullptr)
        return;
    uint8_t b[kPackedBytes];
    pack(s, b);
    memcpy(&p->tempoRezerwa, b, 4);
    p->rezerwa1 = b[4];
    p->rezerwa2 = b[5];
    p->rezerwa3 = b[6];
    p->rezerwa4 = kPatternMagic;
}

void clearPatternSettings(Sequencer::strPattern* p)
{
    if (p == nullptr)
        return;
    p->rezerwa4 = 0;
}

Settings activeSettings()
{
    return patternSettings(current());
}

void audioBlock(float* l, float* r, int n)
{
    if (!s_ready)
        return;

    // the settings in use: the current pattern's own, else the song's
    // (seven bytes compared each block; unpacked only when they change)
    const Sequencer::strPattern* p = current();
    uint8_t packed[kPackedBytes];
    if (patternHasOwn(p))
        readPatternBytes(p, packed);
    else if (mtProject.values.midiInstrument[1].reserved[0] == kSongMagic)
        memcpy(packed, mtProject.values.midiInstrument[0].reserved, kPackedBytes);
    else
        memset(packed, 0, kPackedBytes);
    if (!s_havePacked || memcmp(packed, s_lastPacked, kPackedBytes) != 0)
    {
        memcpy(s_lastPacked, packed, kPackedBytes);
        s_havePacked = true;
        s_engine.setSettings(unpack(packed));
    }

    // the duck: the gain for the next block, and the inputs it leaves alone
    const float g = s_engine.duckBlock(n, sequencer.getCurrentTempo());
    const Settings& cfg = s_engine.settings();
    uint32_t exempt = 0;
    if (cfg.duckOn && p != nullptr)
    {
        const int kick = cfg.duckTrack & 7;
        for (int v = 0; v < kTracksMixerVoices; v++)
            if (voiceOwner(v) == kick)
                exempt |= 1u << v;
        const uint8_t sound = getSound(p, kick);
        if (isSynthInstrument(sound))
        {
            const int part = instrumentIdxToMidiChannel(sound) - 1;
            if (part >= 0 && part < 8)
                exempt |= 1u << (kTracksMixerVoices + part);
        }
    }
    g_gibgobDuckExempt = exempt;
    g_gibgobDuckQ16 = (int32_t)(g * 65536.0f);

    s_engine.process(l, r, n);
}

} // namespace master
} // namespace gibgob
