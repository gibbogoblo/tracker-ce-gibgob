#include "gibgob/MelodicTrack.h"

#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "mtStructs.h"

#include <string.h>

#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif
#ifndef __NOINIT
#define __NOINIT(x)
#endif

namespace gibgob
{

const uint8_t kArpRateTicks[kArpRateCount] = { 192, 96, 64, 48, 32, 24 };
const char* const kArpRateNames[kArpRateCount] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };
const char* const kArpModeNames[kArpModeCount] = { "Off",     "Up",     "Down",
                                                   "Up-Down", "Random", "As placed" };

namespace
{
static_assert(sizeof(Sequencer::strPattern::strTrack::strStep) == 8, "step slot must be 8 bytes");

constexpr uint8_t kMagic0 = 'G';
constexpr uint8_t kMagic1 = 'M';
constexpr uint8_t kVersion = 1;

constexpr uint8_t kHeaderFlagMelodic = 0x01;
constexpr uint8_t kArpFlagApe = 0x01;

constexpr int kSlotBytes = 8;
constexpr int kSlotNotes = 1;
constexpr int kSlotSliceMap = 124;
constexpr int kSlotExtras = 125;
constexpr int kSlotSettings = 126;
constexpr int kSlotPalette = 127;
constexpr int kTrackBytes = 128 * kSlotBytes;
constexpr uint8_t kExtrasMagic = 'X';
constexpr int kApeCycleTicks = 64 * 48; // Go Ape sequences repeat every 4 bars

struct Header
{
    uint8_t magic0;
    uint8_t magic1;
    uint8_t version;
    uint8_t count;
    uint8_t sound;
    uint8_t flags;
    uint8_t arpFlags;
    uint8_t reserved;
};
static_assert(sizeof(Header) == kSlotBytes, "header must fit one step slot");

// The sequencer runs inside the audio interrupt. Edits from the UI block that
// interrupt for the few microseconds a copy takes, so playback never reads a
// half-written note list. Never hold this across a noteOn()/noteOff() call:
// those re-enable the interrupt themselves.
struct AudioLock
{
    AudioLock() { DISABLE_AUDIO_INTERRUPTS(); }
    ~AudioLock() { ENABLE_AUDIO_INTERRUPTS(); }
};

inline uint8_t* rawBytes(Sequencer::strPattern* p, int track)
{
    return reinterpret_cast<uint8_t*>(&p->track[kMidiTrackFirstIdx + track].step[0]);
}
inline const uint8_t* rawBytes(const Sequencer::strPattern* p, int track)
{
    return reinterpret_cast<const uint8_t*>(&p->track[kMidiTrackFirstIdx + track].step[0]);
}

inline bool validTrack(int track)
{
    return track >= 0 && track < kTracks;
}

inline int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------- defaults for patterns without GibGob data ----------------
struct Defaults
{
    uint8_t sliceMap[kTracks][8];
    uint8_t melodic[kTracks];
    uint8_t sound[kTracks];
    PaletteChord palette[kPaletteSize];
    uint8_t swing;
};

Defaults g_def = {
    { { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 },
      { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 } },
    { 0, 0, 0, 0, 1, 1, 1, 1 }, // tracks 1-4 drums, 5-8 melodic
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    {
      // bank A: the chords of C major
      { 48, 0, 0, 0 }, // C Maj7
      { 50, 1, 0, 0 }, // D Min7
      { 52, 1, 0, 0 }, // E Min7
      { 53, 0, 0, 0 }, // F Maj7
      { 55, 2, 0, 0 }, // G Dom7
      { 57, 1, 0, 0 }, // A Min7
      { 59, 3, 0, 0 }, // B m7b5
      { 60, 8, 0, 0 }, // C Maj9
      // bank B: A minor colours
      { 45, 9, 0, 0 },  // A Min9
      { 50, 9, 0, 0 },  // D Min9
      { 43, 10, 0, 0 }, // G Dom9
      { 48, 8, 0, 0 },  // C Maj9
      { 41, 0, 0, 0 },  // F Maj7
      { 47, 3, 0, 0 },  // B m7b5
      { 40, 2, 0, 0 },  // E Dom7
      { 45, 12, 0, 0 }, // A m add9
    },
    50,
};
const Defaults kFactoryDefaults = g_def;

// ---------------- header access ----------------
bool readHeader(const Sequencer::strPattern* p, int track, Header& h)
{
    memcpy(&h, rawBytes(p, track), sizeof(Header));
    if (h.magic0 != kMagic0 || h.magic1 != kMagic1 || h.version != kVersion)
    {
        return false;
    }
    if (h.count > kMaxNotes)
    {
        h.count = kMaxNotes;
    }
    return true;
}

Header defaultHeader(int track)
{
    Header h{};
    h.magic0 = kMagic0;
    h.magic1 = kMagic1;
    h.version = kVersion;
    h.count = 0;
    h.sound = g_def.sound[track];
    h.flags = g_def.melodic[track] ? kHeaderFlagMelodic : 0;
    return h;
}

Header headerOrDefault(const Sequencer::strPattern* p, int track)
{
    Header h;
    if (!readHeader(p, track, h))
    {
        h = defaultHeader(track);
    }
    return h;
}

// caller holds the AudioLock
void writeHeaderLocked(Sequencer::strPattern* p, int track, const Header& h)
{
    uint8_t* raw = rawBytes(p, track);
    Header old;
    if (!readHeader(p, track, old))
    {
        // first use of this hidden track: wipe whatever was there.
        // All-zero settings and palette slots mean "use the defaults".
        memset(raw, 0, kTrackBytes);
    }
    memcpy(raw, &h, sizeof(Header));
}

void ensureHeaderLocked(Sequencer::strPattern* p, int track)
{
    Header h;
    if (!readHeader(p, track, h))
    {
        writeHeaderLocked(p, track, defaultHeader(track));
    }
}

inline uint8_t* notePtr(Sequencer::strPattern* p, int track, int idx)
{
    return rawBytes(p, track) + (kSlotNotes + idx) * kSlotBytes;
}
inline const uint8_t* notePtr(const Sequencer::strPattern* p, int track, int idx)
{
    return rawBytes(p, track) + (kSlotNotes + idx) * kSlotBytes;
}

TrackSettings decodeSettings(const uint8_t* s, const Header& h)
{
    TrackSettings t;
    t.baseNote = s[0] ? (uint8_t)clampInt(s[0] - 1, 0, 127) : kDefaultBaseNote;
    t.arpMode = s[1] < kArpModeCount ? s[1] : (uint8_t)kArpOff;
    t.arpRate = s[2] ? (uint8_t)clampInt(s[2] - 1, 0, kArpRateCount - 1) : 3;
    t.arpOctaves = (uint8_t)clampInt(s[3] ? s[3] : 1, 1, 4);
    t.arpGate = (uint8_t)clampInt(s[4] ? s[4] : 50, 10, 100);
    t.apeRhythm = s[5] ? (uint8_t)clampInt(s[5] - 1, 0, 100) : 40;
    t.apeGate = s[6] ? (uint8_t)clampInt(s[6] - 1, 0, 100) : 40;
    t.apeNotes = s[7] ? (uint8_t)clampInt(s[7] - 1, 0, 100) : 40;
    t.apeOn = (h.arpFlags & kArpFlagApe) != 0;
    return t;
}

// ---------------- slices ----------------
bool isBeatSliceSound(uint8_t instrument)
{
    if (!isSampleInstrument(instrument))
        return false;
    const strInstrument& in = mtProject.instrument[instrument];
    return in.isActive == 1 && in.playMode == playModeBeatSlice && in.sliceNumber > 0;
}

// slot 124: byte i = slice of button i + 1 (0 = not set: the default). Only
// read when the header's spare byte carries this marker.
constexpr uint8_t kSliceMapMagic = 0xA5;

// ---------------- extras slot (seed, track length) ----------------
struct Extras
{
    uint32_t seed = 0;
    uint8_t steps = 0; // 0 = follow the pattern length
};

Extras readExtras(const uint8_t* raw)
{
    Extras e;
    const uint8_t* x = raw + kSlotExtras * kSlotBytes;
    if (x[7] != kExtrasMagic)
        return e;
    e.seed = (uint32_t)x[0] | ((uint32_t)x[1] << 8) | ((uint32_t)x[2] << 16) | ((uint32_t)x[3] << 24);
    e.steps = x[4] <= kMaxTrackSteps ? x[4] : 0;
    return e;
}

// caller holds the AudioLock and the track has a header
void writeExtrasLocked(uint8_t* raw, const Extras& e)
{
    uint8_t* x = raw + kSlotExtras * kSlotBytes;
    x[0] = (uint8_t)(e.seed & 0xFF);
    x[1] = (uint8_t)((e.seed >> 8) & 0xFF);
    x[2] = (uint8_t)((e.seed >> 16) & 0xFF);
    x[3] = (uint8_t)((e.seed >> 24) & 0xFF);
    x[4] = e.steps;
    x[5] = 0;
    x[6] = 0;
    x[7] = kExtrasMagic;
}

inline int masterSteps(const Sequencer::strPattern* p)
{
    return clampInt(p->track[0].length + 1, 1, kMaxTrackSteps);
}

// effective length of a track inside the pattern, in steps
int effectiveSteps(const Sequencer::strPattern* p, int track, const Header* hOrNull)
{
    int master = masterSteps(p);
    if (hOrNull == nullptr)
        return master;
    Extras e = readExtras(rawBytes(p, track));
    if (e.steps == 0)
        return master;
    return e.steps < master ? e.steps : master;
}

// deterministic random number for (seed, index): the same Go Ape sequence
// every time it is played, and when it is applied as notes
inline uint32_t hashRandom(uint32_t seed, uint32_t index)
{
    uint32_t z = seed + 0x9E3779B9u * (index + 1);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

// ---------------- random ----------------
uint32_t s_rng = 0x6B8B4567;
inline uint32_t nextRandom()
{
    // xorshift32: cheap and fine for note probability
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
inline bool percent(int chance)
{
    return (int)(nextRandom() % 100) < chance;
}

// ---------------- playback state ----------------
struct ActiveNote
{
    int16_t remaining; // micro steps until note off; <0 = slot free
    uint8_t pitch;
    uint8_t instrument;
    uint8_t serial;
    int8_t voice;   // sample voice (player index), -1 for synth / MIDI notes
    bool oneShot;   // drum hit: let the sample ring, send no note-off
};

struct HeldNote
{
    int16_t remaining;
    uint8_t pitch;
    uint8_t velocity;
};

constexpr int kMaxHeld = 8;

// The arpeggiator's state. Shared by live playback and by renderArp(), so the
// notes written by "Apply" are exactly what was heard.
struct ArpState
{
    HeldNote held[kMaxHeld]; // notes feeding the arpeggiator
    uint8_t heldCount;
    uint16_t arpIdx;
    // ratchet (second hit of a Go Ape double)
    int16_t ratchetIn;
    uint8_t ratchetPitch;
    uint8_t ratchetVel;
    int16_t ratchetGate;
};

struct ArpOut
{
    uint8_t pitch;
    uint8_t velocity;
    int16_t gate;
};

struct TrackPlayState
{
    ActiveNote notes[kMaxActiveNotes];
    ArpState arp;
    // a quantized recording put a note just ahead of the playhead: it was
    // already heard live, so playback skips it once
    int16_t skipTick;
    uint8_t skipCount;
    uint8_t skipPitch[kMaxChordNotes];
    uint32_t runTicks; // micro steps since playback started
    uint8_t serialCounter;
};

TrackPlayState playState[kTracks];
volatile int s_playTick[kTracks];
volatile bool s_liveMetronome = false;
bool s_quantizedRec = false;

// Which sequencer note owns each of the 8 sample voices. A track normally
// plays on its own voice; extra notes of a chord borrow the voices of tracks
// that are empty in this pattern.
struct VoiceUse
{
    int8_t track;
    uint8_t serial;
    bool on;
};
VoiceUse voiceUse[kTracks];
int8_t s_lastVoiceOwner[kTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 }; // track that last played each voice
uint8_t s_triggeredVoices = 0; // voices we started since the last stop
bool s_legacyPlayed[kTracks];

struct Audition
{
    bool on;
    bool oneShot;
    uint8_t count;
    uint8_t pitches[kMaxChordNotes];
    int8_t voices[kMaxChordNotes];
    uint8_t serials[kMaxChordNotes];
    uint8_t instrument;
};
Audition audition[kTracks];

bool initDone = false;
void ensureInit()
{
    if (initDone)
        return;
    for (int t = 0; t < kTracks; t++)
    {
        for (auto& a : playState[t].notes)
            a.remaining = -1;
        playState[t].arp.heldCount = 0;
        playState[t].arp.ratchetIn = 0;
        playState[t].arp.arpIdx = 0;
        playState[t].runTicks = 0;
        playState[t].serialCounter = 0;
        playState[t].skipTick = -1;
        playState[t].skipCount = 0;
        audition[t].on = false;
        voiceUse[t].on = false;
        voiceUse[t].track = -1;
        s_legacyPlayed[t] = false;
    }
    initDone = true;
}

inline uint8_t instrumentOrDefault(uint8_t sound, int track)
{
    return sound < kTotalInstrumentsCount ? sound : (uint8_t)track;
}

// audio interrupt must be blocked (or we are inside it)
int allocVoice(int track, uint8_t serial, int forceVoice = -1)
{
    int v = track;
    if (forceVoice >= 0)
    {
        v = forceVoice;
    }
    else if (voiceUse[track].on)
    {
        const Sequencer::strPattern* p = current();
        for (int c = 0; c < kTracks; c++)
        {
            if (c == track || voiceUse[c].on || s_legacyPlayed[c] || audition[c].on)
                continue;
            if (mtProject.values.trackMute[c])
                continue;
            Header h;
            if (readHeader(p, c, h) && h.count > 0)
                continue; // that track has its own notes in this pattern
            v = c;
            break;
        }
    }
    voiceUse[v].track = (int8_t)track;
    voiceUse[v].serial = serial;
    voiceUse[v].on = true;
    s_lastVoiceOwner[v] = (int8_t)track;
    s_triggeredVoices |= (uint8_t)(1u << v);
    return v;
}

// The engine uses a note's volume instead of the sample's Volume setting, so
// scale that setting: velocity 100 = the sample's own volume.
inline int8_t velocityToVolume(uint8_t instrument, uint8_t velocity)
{
    int base = isSampleInstrument(instrument) ? mtProject.instrument[instrument].volume : 50;
    return (int8_t)clampInt((base * velocity + 50) / 100, 0, MAX_INSTRUMENT_VOLUME);
}

void soundOffPoly(uint8_t instrument, uint8_t pitch)
{
    bool toSynth = isSynthInstrument(instrument);
    sendMidiNoteOff(pitch, 0, instrumentIdxToMidiChannel(instrument), toSynth);
}

void soundOnPoly(uint8_t instrument, uint8_t pitch, uint8_t velocity)
{
    bool toSynth = isSynthInstrument(instrument);
    sendMidiNoteOn(pitch, velocity, instrumentIdxToMidiChannel(instrument), toSynth);
}

void releaseActive(int track, ActiveNote& a)
{
    if (a.voice >= 0)
    {
        VoiceUse& u = voiceUse[a.voice];
        if (u.on && u.track == track && u.serial == a.serial)
        {
            if (!a.oneShot)
                instrumentPlayer[a.voice].noteOff();
            u.on = false;
        }
    }
    else
    {
        soundOffPoly(a.instrument, a.pitch);
    }
    a.remaining = -1;
}

// stop everything sounding on the track right now, ringing drum hits included
void chokeTrack(int track, TrackPlayState& st)
{
    for (auto& a : st.notes)
    {
        if (a.remaining < 0)
            continue;
        a.oneShot = false; // force the note-off
        releaseActive(track, a);
    }
    // hits whose step is over but which still ring on a voice of this track
    for (int v = 0; v < kTracks; v++)
    {
        if (s_lastVoiceOwner[v] != track || !(s_triggeredVoices & (1u << v)))
            continue;
        if (voiceUse[v].on)
            continue; // in use by a note that is still running
        instrumentPlayer[v].noteOff();
        s_triggeredVoices &= (uint8_t)~(1u << v);
    }
}

// per-note sound changes (slide, accent, filter envelope) as the engine's
// existing step effects; sample instruments only
void noteEffects(uint8_t instrument, uint8_t flags, uint8_t* fxType, uint8_t* fxValue)
{
    fxType[0] = fxType[1] = 0;
    fxValue[0] = fxValue[1] = 0;
    int slot = 0;

    if (flags & kNoteFlagSlide)
    {
        int lo = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_GLIDE);
        int hi = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_GLIDE);
        // the lower half of the range maps to 0..1000 ms: about 80 ms here
        fxType[slot] = Sequencer::strFxConsts::FX_TYPE_GLIDE;
        fxValue[slot] = (uint8_t)clampInt(lo + ((hi / 2 - lo) * 8) / 100, lo, hi);
        slot++;
    }

    const strInstrument& in = mtProject.instrument[instrument];
    if ((flags & kNoteFlagAccent) && slot < 2 && in.filterEnable && in.filterType == lowPass)
    {
        // open the instrument's own low-pass filter a quarter of the way further
        int lo = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);
        int hi = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);
        float cut = in.cutOff + 0.25f;
        if (cut > 1.0f)
            cut = 1.0f;
        fxType[slot] = Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS;
        fxValue[slot] = (uint8_t)clampInt(lo + (int)(cut * (float)(hi - lo) + 0.5f), lo, hi);
        slot++;
    }

    int env = (flags >> 4) & 0x0F;
    if (env > 0 && slot < 2)
    {
        // attack and release from the variant, spread over the whole range
        uint8_t attack = (uint8_t)((env * 7) & 0x0F);
        uint8_t release = (uint8_t)((env * 11 + 3) & 0x0F);
        fxType[slot] = Sequencer::strFxConsts::FX_TYPE_FILTER_ENV;
        fxValue[slot] = (uint8_t)((attack << 4) | release);
        slot++;
    }
}

// audio context
void startNote(int track,
               TrackPlayState& st,
               uint8_t instrument,
               uint8_t pitch,
               uint8_t velocity,
               int lengthTicks,
               bool oneShot,
               uint8_t noteFlags = 0)
{
    bool sample = isSampleInstrument(instrument);

    if (noteFlags & kNoteFlagAccent)
        velocity = (uint8_t)clampInt(velocity + 27, 1, 127);

    // a slide continues on the voice of the note still sounding before it
    int legatoVoice = -1;
    if (sample && (noteFlags & kNoteFlagSlide))
    {
        ActiveNote* prev = nullptr;
        for (auto& a : st.notes)
            if (a.remaining >= 0 && a.voice >= 0 && (prev == nullptr || (uint8_t)(a.serial - prev->serial) < 128))
                prev = &a;
        if (prev != nullptr)
        {
            VoiceUse& u = voiceUse[prev->voice];
            if (u.on && u.track == track && u.serial == prev->serial)
            {
                legatoVoice = prev->voice;
                prev->remaining = -1; // handed over without a note-off
            }
        }
    }

    // a retriggered pitch on a synth: end the old note first
    if (!sample)
    {
        for (auto& a : st.notes)
            if (a.remaining >= 0 && a.voice < 0 && a.pitch == pitch && a.instrument == instrument)
                releaseActive(track, a);
    }

    // find a free slot (steal the one closest to its end if full)
    ActiveNote* slot = nullptr;
    for (auto& a : st.notes)
    {
        if (a.remaining < 0)
        {
            slot = &a;
            break;
        }
    }
    if (slot == nullptr)
    {
        slot = &st.notes[0];
        for (auto& a : st.notes)
            if (a.remaining < slot->remaining)
                slot = &a;
        releaseActive(track, *slot);
    }

    slot->pitch = pitch;
    slot->instrument = instrument;
    slot->serial = ++st.serialCounter;
    slot->remaining = (int16_t)clampInt(lengthTicks, 1, 0x7FFF);
    slot->oneShot = sample && oneShot;
    slot->voice = -1;

    if (sample)
    {
        int v = allocVoice(track, slot->serial, legatoVoice);
        slot->voice = (int8_t)v;
        uint8_t fxType[2], fxValue[2];
        noteEffects(instrument, legatoVoice >= 0 ? noteFlags : (uint8_t)(noteFlags & ~kNoteFlagSlide), fxType,
                    fxValue);
        instrumentPlayer[v].noteOn(instrument, (int8_t)pitch, velocityToVolume(instrument, velocity), fxType[0],
                                   fxValue[0], fxType[1], fxValue[1]);
    }
    else
    {
        soundOnPoly(instrument, pitch, velocity);
    }
}

void removeHeldAt(ArpState& a, int i)
{
    for (int k = i; k + 1 < a.heldCount; k++)
        a.held[k] = a.held[k + 1];
    a.heldCount--;
}

void addHeld(ArpState& a, uint8_t pitch, uint8_t velocity, int length)
{
    if (a.heldCount >= kMaxHeld)
        return;
    if (a.heldCount == 0)
        a.arpIdx = 0;
    HeldNote& hn = a.held[a.heldCount++];
    hn.pitch = pitch;
    hn.velocity = velocity;
    hn.remaining = (int16_t)clampInt(length, 1, 0x7FFF);
}

// one micro step of held-note and ratchet countdown. Returns true if the
// ratchet (second hit of a Go Ape double) fires now; out is filled then.
bool arpCountdown(ArpState& a, ArpOut& out)
{
    for (int i = a.heldCount - 1; i >= 0; i--)
    {
        if (--a.held[i].remaining <= 0)
            removeHeldAt(a, i);
    }
    if (a.ratchetIn > 0 && --a.ratchetIn == 0)
    {
        out.pitch = a.ratchetPitch;
        out.velocity = a.ratchetVel;
        out.gate = a.ratchetGate;
        return true;
    }
    return false;
}

// One step of the arpeggiator at run time `runTick`. Go Ape randomises
// rhythm, gate and notes, always the same way for the same seed and position
// in its 4-bar cycle. Returns true if a note should play.
bool arpStep(ArpState& a, const TrackSettings& s, uint32_t runTick, ArpOut& out)
{
    int n = a.heldCount;
    if (n <= 0)
        return false;

    int rate = kArpRateTicks[s.arpRate];
    bool ape = s.apeOn && s.apeSeed != 0;
    uint32_t cycleStep = (runTick % kApeCycleTicks) / (uint32_t)rate;
    uint32_t r1 = ape ? hashRandom(s.apeSeed, cycleStep * 4 + 0) : 0;
    uint32_t r2 = ape ? hashRandom(s.apeSeed, cycleStep * 4 + 1) : 0;
    uint32_t r3 = ape ? hashRandom(s.apeSeed, cycleStep * 4 + 2) : 0;
    uint32_t r4 = ape ? hashRandom(s.apeSeed, cycleStep * 4 + 3) : 0;

    // pitches in playing order
    uint8_t pitch[kMaxHeld];
    uint8_t vel[kMaxHeld];
    for (int i = 0; i < n; i++)
    {
        pitch[i] = a.held[i].pitch;
        vel[i] = a.held[i].velocity;
    }
    if (s.arpMode != kArpAsPlaced)
    {
        for (int x = 0; x < n; x++)
            for (int y = x + 1; y < n; y++)
                if (pitch[y] < pitch[x])
                {
                    uint8_t t = pitch[x];
                    pitch[x] = pitch[y];
                    pitch[y] = t;
                    t = vel[x];
                    vel[x] = vel[y];
                    vel[y] = t;
                }
    }

    int total = n * s.arpOctaves;
    int idx = a.arpIdx++;
    int k = 0;
    switch (s.arpMode)
    {
        case kArpDown: k = total - 1 - (idx % total); break;
        case kArpUpDown:
        {
            int period = total > 1 ? 2 * total - 2 : 1;
            int m = idx % period;
            k = m < total ? m : period - m;
            break;
        }
        case kArpRandom: k = (int)((ape ? (r4 >> 8) : nextRandom()) % (uint32_t)total); break;
        default: k = idx % total; break; // up, as placed
    }

    int gate = rate * s.arpGate / 100;
    int outPitch = pitch[k % n] + 12 * (k / n);
    int outVel = vel[k % n];

    if (ape)
    {
        // the basic rhythm is fixed by the seed: the same rests every time,
        // whatever the amounts. Rhythm only adds doubles on top.
        if ((int)(r1 % 100) < 35)
            return false; // rest
        bool ratchet = (int)((r1 >> 8) % 100) < s.apeRhythm * 40 / 100;

        // gate length
        if (s.apeGate > 0)
        {
            int delta = (int)(r2 % (uint32_t)(2 * s.apeGate + 1)) - s.apeGate;
            gate = gate * (100 + delta) / 100;
        }

        // notes: up to half way only fifths, sevenths and octaves (keeps chords
        // intact); above half, other chord colours join in
        if ((int)(r3 % 100) < s.apeNotes * 60 / 100)
        {
            static const int8_t kSafe[6] = { 7, -5, 10, -2, 12, -12 };
            static const int8_t kWide[6] = { 3, 4, 5, 9, -3, 2 };
            bool wide = s.apeNotes > 50 && (int)((r3 >> 8) % 100) < (s.apeNotes - 50) * 2;
            int shifted = outPitch + (wide ? kWide[(r3 >> 16) % 6] : kSafe[(r3 >> 16) % 6]);
            if (shifted >= 0 && shifted <= 127)
                outPitch = shifted;
        }

        if (ratchet && rate >= 12)
        {
            a.ratchetIn = (int16_t)(rate / 2);
            a.ratchetPitch = (uint8_t)clampInt(outPitch, 0, 127);
            a.ratchetVel = (uint8_t)clampInt(outVel * 3 / 4, 1, 127);
            a.ratchetGate = (int16_t)clampInt(gate / 2, 2, rate / 2);
            gate = clampInt(gate, 2, rate / 2);
        }
    }

    out.pitch = (uint8_t)clampInt(outPitch, 0, 127);
    out.velocity = (uint8_t)clampInt(outVel, 1, 127);
    // an arp note always ends before the next one starts: notes never overlap
    out.gate = (int16_t)clampInt(gate, 2, rate);
    return true;
}

} // namespace

// =====================================================================
// data access
// =====================================================================

bool hasHeader(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return false;
    Header h;
    return readHeader(p, track, h);
}

TrackType getTrackType(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return TrackType::Drum;
    Header h = headerOrDefault(p, track);
    return (h.flags & kHeaderFlagMelodic) ? TrackType::Melodic : TrackType::Drum;
}

void setTrackType(Sequencer::strPattern* p, int track, TrackType type)
{
    if (!validTrack(track) || p == nullptr)
        return;
    AudioLock lock;
    Header h = headerOrDefault(p, track);
    if (type == TrackType::Melodic)
        h.flags |= kHeaderFlagMelodic;
    else
        h.flags &= (uint8_t)~kHeaderFlagMelodic;
    writeHeaderLocked(p, track, h);
    g_def.melodic[track] = (type == TrackType::Melodic) ? 1 : 0;
}

bool isMelodic(const Sequencer::strPattern* p, int track)
{
    return getTrackType(p, track) == TrackType::Melodic;
}

bool isSliceTrack(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return false;
    return isBeatSliceSound(getSound(p, track));
}

int sliceCount(const Sequencer::strPattern* p, int track)
{
    if (!isSliceTrack(p, track))
        return 0;
    int n = mtProject.instrument[getSound(p, track)].sliceNumber;
    return n > MAX_SLICE_NUMBER ? MAX_SLICE_NUMBER : n;
}

int sliceForButton(const Sequencer::strPattern* p, int track, int button)
{
    button &= 7;
    int slice = button;
    if (validTrack(track) && p != nullptr)
    {
        slice = g_def.sliceMap[track][button];
        Header h;
        if (readHeader(p, track, h))
        {
            const uint8_t* m = rawBytes(p, track) + kSlotSliceMap * kSlotBytes;
            if (h.reserved == kSliceMapMagic && m[button] != 0)
                slice = m[button] - 1;
        }
    }
    const int n = sliceCount(p, track);
    if (n > 0 && slice >= n)
        slice = n - 1;
    return slice < 0 ? 0 : slice;
}

uint8_t laneNote(const Sequencer::strPattern* p, int track)
{
    if (isSliceTrack(p, track))
        return (uint8_t)(kFirstSliceNote + sliceForButton(p, track, 0));
    return getSettings(p, track).baseNote;
}

void setSliceForButton(Sequencer::strPattern* p, int track, int button, int slice)
{
    if (!validTrack(track) || p == nullptr)
        return;
    button &= 7;
    slice = clampInt(slice, 0, MAX_SLICE_NUMBER - 1);
    AudioLock lock;
    Header h = headerOrDefault(p, track);
    // the first header write clears the track: do it before the map
    writeHeaderLocked(p, track, h);
    uint8_t* m = rawBytes(p, track) + kSlotSliceMap * kSlotBytes;
    if (h.reserved != kSliceMapMagic)
    {
        // first time on this track: start from the defaults
        for (int b = 0; b < 8; b++)
            m[b] = (uint8_t)(g_def.sliceMap[track][b] + 1);
        h.reserved = kSliceMapMagic;
    }
    m[button] = (uint8_t)(slice + 1);
    writeHeaderLocked(p, track, h);
    g_def.sliceMap[track][button] = (uint8_t)slice;
}

uint8_t getSound(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return 0;
    return instrumentOrDefault(headerOrDefault(p, track).sound, track);
}

void setSound(Sequencer::strPattern* p, int track, uint8_t instrument)
{
    if (!validTrack(track) || p == nullptr || instrument >= kTotalInstrumentsCount)
        return;
    AudioLock lock;
    Header h = headerOrDefault(p, track);
    h.sound = instrument;
    writeHeaderLocked(p, track, h);
    g_def.sound[track] = instrument;
}

TrackSettings getSettings(const Sequencer::strPattern* p, int track)
{
    TrackSettings t;
    if (!validTrack(track) || p == nullptr)
        return t;
    Header h;
    if (!readHeader(p, track, h))
        return t;
    t = decodeSettings(rawBytes(p, track) + kSlotSettings * kSlotBytes, h);
    t.apeSeed = readExtras(rawBytes(p, track)).seed;
    return t;
}

void setSettings(Sequencer::strPattern* p, int track, const TrackSettings& s)
{
    if (!validTrack(track) || p == nullptr)
        return;
    uint8_t enc[kSlotBytes];
    enc[0] = (uint8_t)(clampInt(s.baseNote, 0, 127) + 1);
    enc[1] = s.arpMode < kArpModeCount ? s.arpMode : (uint8_t)kArpOff;
    enc[2] = (uint8_t)(clampInt(s.arpRate, 0, kArpRateCount - 1) + 1);
    enc[3] = (uint8_t)clampInt(s.arpOctaves, 1, 4);
    enc[4] = (uint8_t)clampInt(s.arpGate, 10, 100);
    enc[5] = (uint8_t)(clampInt(s.apeRhythm, 0, 100) + 1);
    enc[6] = (uint8_t)(clampInt(s.apeGate, 0, 100) + 1);
    enc[7] = (uint8_t)(clampInt(s.apeNotes, 0, 100) + 1);

    AudioLock lock;
    Header h = headerOrDefault(p, track);
    if (s.apeOn)
        h.arpFlags |= kArpFlagApe;
    else
        h.arpFlags &= (uint8_t)~kArpFlagApe;
    writeHeaderLocked(p, track, h);
    memcpy(rawBytes(p, track) + kSlotSettings * kSlotBytes, enc, kSlotBytes);
    Extras e = readExtras(rawBytes(p, track));
    e.seed = s.apeSeed;
    writeExtrasLocked(rawBytes(p, track), e);
}

static bool readStoredChord(const Sequencer::strPattern* p, int idx, PaletteChord& c)
{
    int track = idx / 2;
    Header h;
    if (!readHeader(p, track, h))
        return false;
    const uint8_t* s = rawBytes(p, track) + kSlotPalette * kSlotBytes + (idx % 2) * 4;
    if (s[0] == 0)
        return false;
    c.root = (uint8_t)clampInt(s[0] - 1, 0, 127);
    c.type = s[1] < kChordTypeCount ? s[1] : 0;
    c.inversion = s[2] < kMaxChordNotes ? s[2] : 0;
    c.voicing = s[3] < kVoicingCount ? s[3] : 0;
    return true;
}

PaletteChord getPaletteChord(const Sequencer::strPattern* p, int idx)
{
    idx = clampInt(idx, 0, kPaletteSize - 1);
    PaletteChord c;
    if (p != nullptr && readStoredChord(p, idx, c))
        return c;
    return g_def.palette[idx];
}

void setPaletteChord(Sequencer::strPattern* p, int idx, const PaletteChord& chord)
{
    if (p == nullptr || idx < 0 || idx >= kPaletteSize)
        return;

    // store the whole palette in this pattern so it is saved with it
    PaletteChord all[kPaletteSize];
    for (int i = 0; i < kPaletteSize; i++)
        all[i] = (i == idx) ? chord : getPaletteChord(p, i);

    AudioLock lock;
    for (int i = 0; i < kPaletteSize; i++)
    {
        int track = i / 2;
        ensureHeaderLocked(p, track);
        uint8_t* s = rawBytes(p, track) + kSlotPalette * kSlotBytes + (i % 2) * 4;
        s[0] = (uint8_t)(clampInt(all[i].root, 0, 127) + 1);
        s[1] = all[i].type < kChordTypeCount ? all[i].type : 0;
        s[2] = all[i].inversion;
        s[3] = all[i].voicing < kVoicingCount ? all[i].voicing : 0;
        g_def.palette[i] = all[i];
    }
}

int noteCount(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return 0;
    Header h;
    if (!readHeader(p, track, h))
        return 0;
    return h.count;
}

Note getNote(const Sequencer::strPattern* p, int track, int idx)
{
    Note n{};
    if (idx < 0 || idx >= noteCount(p, track))
        return n;
    memcpy(&n, notePtr(p, track, idx), sizeof(Note));
    return n;
}

void setNote(Sequencer::strPattern* p, int track, int idx, const Note& n)
{
    if (idx < 0 || idx >= noteCount(p, track))
        return;
    AudioLock lock;
    memcpy(notePtr(p, track, idx), &n, sizeof(Note));
}

int addNote(Sequencer::strPattern* p, int track, const Note& n)
{
    if (!validTrack(track) || p == nullptr)
        return -1;
    AudioLock lock;
    Header h = headerOrDefault(p, track);
    if (h.count >= kMaxNotes)
        return -1;
    int idx = h.count;
    writeHeaderLocked(p, track, h); // makes sure the track is initialised
    memcpy(notePtr(p, track, idx), &n, sizeof(Note));
    h.count++;
    memcpy(rawBytes(p, track), &h, sizeof(Header));
    return idx;
}

void removeNote(Sequencer::strPattern* p, int track, int idx)
{
    if (!validTrack(track) || p == nullptr)
        return;
    AudioLock lock;
    Header h;
    if (!readHeader(p, track, h))
        return;
    int count = h.count;
    if (idx < 0 || idx >= count)
        return;
    memmove(notePtr(p, track, idx), notePtr(p, track, idx + 1), (size_t)(count - idx - 1) * sizeof(Note));
    memset(notePtr(p, track, count - 1), 0, sizeof(Note));
    h.count = (uint8_t)(count - 1);
    memcpy(rawBytes(p, track), &h, sizeof(Header));
}

void clearNotes(Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return;
    AudioLock lock;
    Header h = headerOrDefault(p, track);
    h.count = 0;
    writeHeaderLocked(p, track, h);
    memset(notePtr(p, track, 0), 0, kMaxNotes * sizeof(Note));
}

int findNoteAt(const Sequencer::strPattern* p, int track, int tick, int pitch)
{
    int count = noteCount(p, track);
    for (int i = count - 1; i >= 0; i--)
    {
        Note n = getNote(p, track, i);
        if (n.pitch == pitch && tick >= n.start && tick < n.start + n.length)
            return i;
    }
    return -1;
}

void resetDefaults()
{
    g_def = kFactoryDefaults;
}

void adoptDefaults(const Sequencer::strPattern* p)
{
    if (p == nullptr)
        return;
    for (int t = 0; t < kTracks; t++)
    {
        Header h;
        if (readHeader(p, t, h))
        {
            g_def.melodic[t] = (h.flags & kHeaderFlagMelodic) ? 1 : 0;
            g_def.sound[t] = instrumentOrDefault(h.sound, t);
            if (h.reserved == kSliceMapMagic)
            {
                const uint8_t* m = rawBytes(p, t) + kSlotSliceMap * kSlotBytes;
                for (int b = 0; b < 8; b++)
                    if (m[b] != 0)
                        g_def.sliceMap[t][b] = (uint8_t)(m[b] - 1);
            }
        }
    }
    for (int i = 0; i < kPaletteSize; i++)
    {
        PaletteChord c;
        if (readStoredChord(p, i, c))
            g_def.palette[i] = c;
    }
    if (p->swingRez >= 25.0f && p->swingRez <= 75.0f)
        g_def.swing = (uint8_t)(p->swingRez + 0.5f);
}

int patternSwing(const Sequencer::strPattern* p)
{
    if (p != nullptr && p->swingRez >= 25.0f && p->swingRez <= 75.0f)
        return (int)(p->swingRez + 0.5f);
    return g_def.swing;
}

void setPatternSwing(Sequencer::strPattern* p, int value)
{
    if (p == nullptr)
        return;
    value = clampInt(value, 25, 75);
    p->swingRez = (float)value;
    g_def.swing = (uint8_t)value;
}

// ---------------- note utilities ----------------
__NOINIT(EXTERNAL_RAM) static Note s_clip[kMaxNotes];
static int s_clipCount = -1;

void transposeNotes(Sequencer::strPattern* p, int track, int semitones)
{
    int count = noteCount(p, track);
    bool anySelected = false;
    for (int i = 0; i < count; i++)
        anySelected = anySelected || (getNote(p, track, i).flags & kNoteFlagSelected);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        if (anySelected && !(n.flags & kNoteFlagSelected))
            continue;
        n.pitch = (uint8_t)clampInt(n.pitch + semitones, 0, 127);
        setNote(p, track, i, n);
    }
}

void legatoNotes(Sequencer::strPattern* p, int track)
{
    int count = noteCount(p, track);
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        // the next later start, wrapping round to the first note of the loop
        int next = -1, first = trackTicks;
        for (int k = 0; k < count; k++)
        {
            int s = getNote(p, track, k).start;
            if (s > n.start && (next < 0 || s < next))
                next = s;
            if (s < first)
                first = s;
        }
        int len = next >= 0 ? next - n.start : trackTicks - n.start + first;
        n.length = (uint16_t)clampInt(len, 1, 0x7FFF);
        setNote(p, track, i, n);
    }
}

void reverseNotes(Sequencer::strPattern* p, int track)
{
    int count = noteCount(p, track);
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        int len = n.length < trackTicks ? n.length : trackTicks;
        int start = trackTicks - (n.start + len);
        if (start < 0)
            start = 0;
        n.start = (uint16_t)clampInt(start, 0, trackTicks - 1);
        setNote(p, track, i, n);
    }
}

void copyNotes(const Sequencer::strPattern* p, int track, const int* indices, int count)
{
    if (count <= 0)
        return;
    int first = 0x7FFF;
    for (int i = 0; i < count; i++)
    {
        int s = getNote(p, track, indices[i]).start;
        if (s < first)
            first = s;
    }
    int n = 0;
    for (int i = 0; i < count && n < kMaxNotes; i++)
    {
        Note note = getNote(p, track, indices[i]);
        note.flags &= (uint8_t)~(kNoteFlagSelected | kNoteFlagRecording);
        note.start = (uint16_t)(note.start - first); // relative to the first note
        s_clip[n++] = note;
    }
    s_clipCount = n;
}

int pasteNotes(Sequencer::strPattern* p, int track, int tick)
{
    if (s_clipCount <= 0)
        return 0;
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    int pasted = 0;
    for (int i = 0; i < s_clipCount; i++)
    {
        Note n = s_clip[i];
        int start = tick + n.start;
        if (start >= trackTicks)
            continue; // past the end of the track
        n.start = (uint16_t)start;
        if (addNote(p, track, n) < 0)
            break;
        pasted++;
    }
    return pasted;
}

bool hasClipboard()
{
    return s_clipCount > 0;
}

int stepOffset(int start, int step, int trackTicks)
{
    int off = start - step * kTicksPerStep;
    if (step == 0 && start >= trackTicks - kTicksPerStep / 2)
        off = start - trackTicks; // step 1 nudged early sits at the end
    return off;
}

int startFromOffset(int step, int offset, int trackTicks)
{
    int start = step * kTicksPerStep + offset;
    if (start < 0)
        start += trackTicks;
    if (start >= trackTicks)
        start -= trackTicks;
    return clampInt(start, 0, trackTicks - 1);
}

bool belongsToStep(int start, int step, int trackTicks)
{
    int off = stepOffset(start, step, trackTicks);
    return off >= -kTicksPerStep / 2 && off < kTicksPerStep / 2;
}

void setQuantizedRecording(bool on)
{
    s_quantizedRec = on;
}

bool quantizedRecording()
{
    return s_quantizedRec;
}

// =====================================================================
// track length
// =====================================================================

int patternSteps(const Sequencer::strPattern* p)
{
    return p == nullptr ? 16 : masterSteps(p);
}

int trackSteps(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return 16;
    Header h;
    bool has = readHeader(p, track, h);
    return effectiveSteps(p, track, has ? &h : nullptr);
}

void setTrackSteps(Sequencer::strPattern* p, int track, int steps)
{
    if (!validTrack(track) || p == nullptr)
        return;
    steps = clampInt(steps, 1, kMaxTrackSteps);

    // every track keeps the length it has now; the pattern becomes as long as
    // the longest track. Tracks still holding old tracker steps (old projects)
    // keep following the pattern length.
    int master = masterSteps(p);
    int len[kTracks];
    bool legacy[kTracks];
    for (int t = 0; t < kTracks; t++)
    {
        Header h;
        bool has = readHeader(p, t, h);
        len[t] = effectiveSteps(p, t, has ? &h : nullptr);
        legacy[t] = false;
        if (!has && t != track)
        {
            for (int s = 0; s <= Sequencer::MAXSTEP && !legacy[t]; s++)
                legacy[t] = p->track[t].step[s].note >= 0;
        }
    }
    len[track] = steps;

    int newMaster = 1;
    for (int t = 0; t < kTracks; t++)
    {
        int l = legacy[t] ? master : len[t];
        if (l > newMaster)
            newMaster = l;
    }

    AudioLock lock;
    for (int t = 0; t < kTracks; t++)
    {
        if (legacy[t])
            continue;
        ensureHeaderLocked(p, t);
        Extras e = readExtras(rawBytes(p, t));
        e.steps = (uint8_t)len[t];
        writeExtrasLocked(rawBytes(p, t), e);
    }
    for (int t = 0; t < kAllTracks; t++)
        p->track[t].length = (uint8_t)(newMaster - 1);
}

bool duplicateTrack(Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return false;
    int steps = trackSteps(p, track);
    int count = noteCount(p, track);
    if (steps * 2 > kMaxTrackSteps || count * 2 > kMaxNotes)
        return false;
    int offset = steps * kTicksPerStep;
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        n.flags &= (uint8_t)~(kNoteFlagSelected | kNoteFlagRecording);
        n.start = (uint16_t)(n.start + offset);
        addNote(p, track, n);
    }
    setTrackSteps(p, track, steps * 2);
    return true;
}

// =====================================================================
// tools
// =====================================================================

void quantizeTrack(Sequencer::strPattern* p, int track, int strength)
{
    if (!validTrack(track) || p == nullptr)
        return;
    strength = clampInt(strength, 1, 10);
    int maxOff = (3 * (10 - strength) + 4) / 9; // 1 -> 3, 10 -> 0
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    int count = noteCount(p, track);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        int grid = ((n.start + kTicksPerStep / 2) / kTicksPerStep) * kTicksPerStep;
        int off = clampInt((int)n.start - grid, -maxOff, maxOff);
        if (grid >= trackTicks)
            grid = 0; // rounded past the end: wraps to the start
        n.start = (uint16_t)clampInt(grid + off, 0, trackTicks - 1);
        setNote(p, track, i, n);
    }
}

void humanizeTrack(Sequencer::strPattern* p, int track, int amount)
{
    if (!validTrack(track) || p == nullptr)
        return;
    amount = clampInt(amount, 1, 10);
    int range = 3 + ((amount - 1) * 17 + 4) / 9; // 1 -> 3, 10 -> 20
    int trackTicks = trackSteps(p, track) * kTicksPerStep;
    int count = noteCount(p, track);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        int delta = (int)(nextRandom() % (uint32_t)(2 * range + 1)) - range;
        n.start = (uint16_t)clampInt((int)n.start + delta, 0, trackTicks - 1);
        setNote(p, track, i, n);
    }
}

// pitch classes (bit mask of 12) that other melodic tracks play at `tick`
static uint16_t pitchClassesAt(const Sequencer::strPattern* p, int track, int tick, bool anyTime)
{
    uint16_t mask = 0;
    for (int u = 0; u < kTracks; u++)
    {
        if (u == track || !isMelodic(p, u))
            continue;
        int uTicks = trackSteps(p, u) * kTicksPerStep;
        int t = tick % uTicks;
        int count = noteCount(p, u);
        for (int i = 0; i < count; i++)
        {
            Note n = getNote(p, u, i);
            bool on = anyTime || (t >= n.start && t < n.start + n.length) ||
                      (n.start + n.length > uTicks && t < n.start + n.length - uTicks);
            if (on)
                mask |= (uint16_t)(1u << (n.pitch % 12));
        }
    }
    return mask;
}

void generateAcid(Sequencer::strPattern* p, int track, const AcidOptions& o)
{
    if (!validTrack(track) || p == nullptr)
        return;

    TrackSettings s = getSettings(p, track);
    int center = s.baseNote >= 24 ? s.baseNote - 24 : s.baseNote; // bass register
    int steps = trackSteps(p, track);
    uint16_t anyMask = pitchClassesAt(p, track, 0, true);

    clearNotes(p, track);

    int prevIdx = -1;
    int density = clampInt(o.density, 1, 10) * 10;
    for (int step = 0; step < steps; step++)
    {
        if (!percent(density))
            continue;

        int pitch = center;
        if (o.notes == AcidNotes::Pick && o.pickedCount > 0)
        {
            pitch = o.picked[nextRandom() % o.pickedCount];
        }
        else
        {
            uint16_t mask = pitchClassesAt(p, track, step * kTicksPerStep, false);
            if (mask == 0)
                mask = anyMask;
            if (mask == 0)
            {
                // nothing to follow: minor pentatonic on the base note
                int root = center % 12;
                static const int kPenta[5] = { 0, 3, 5, 7, 10 };
                for (int k = 0; k < 5; k++)
                    mask |= (uint16_t)(1u << ((root + kPenta[k]) % 12));
            }
            int classes[12], nc = 0;
            for (int pc = 0; pc < 12; pc++)
                if (mask & (1u << pc))
                    classes[nc++] = pc;
            int pc = classes[nextRandom() % (uint32_t)nc];
            // the octave that lands closest to the bass register
            pitch = center - ((center - pc) % 12 + 12) % 12;
            if (center - pitch > 6)
                pitch += 12;
            if (percent(15))
                pitch += 12;
        }

        Note n{};
        n.start = (uint16_t)(step * kTicksPerStep);
        n.length = (uint16_t)(kTicksPerStep / 2);
        n.pitch = (uint8_t)clampInt(pitch, 0, 127);
        bool accent = percent(clampInt(o.accent, 0, 10) * 10);
        n.velocity = accent ? 118 : 88;
        if (accent)
            n.flags |= kNoteFlagAccent;
        if (percent(clampInt(o.envelope, 0, 10) * 10))
            setNoteEnv(n, 1 + (int)(nextRandom() % 15));

        if (prevIdx >= 0 && percent(clampInt(o.slide, 0, 10) * 10))
        {
            // slide: the note before is held into this one and this one glides
            Note prev = getNote(p, track, prevIdx);
            prev.length = (uint16_t)(n.start - prev.start + kTicksPerStep / 8);
            setNote(p, track, prevIdx, prev);
            n.flags |= kNoteFlagSlide;
        }

        int idx = addNote(p, track, n);
        if (idx < 0)
            break;
        prevIdx = idx;
    }
}

int renderArp(Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return 0;
    TrackSettings s = getSettings(p, track);
    if (s.arpMode == kArpOff)
        return 0;

    int srcSteps = trackSteps(p, track);
    int outSteps = srcSteps < 64 ? 64 : srcSteps;
    int srcTicks = srcSteps * kTicksPerStep;
    int masterTicks = masterSteps(p) * kTicksPerStep; // the track restarts with the pattern
    int outTicks = outSteps * kTicksPerStep;
    int rate = kArpRateTicks[s.arpRate];

    // copy of the source notes (they are replaced below)
    int count = noteCount(p, track);
    Note src[kMaxNotes];
    for (int i = 0; i < count; i++)
        src[i] = getNote(p, track, i);

    Note out[kMaxNotes];
    int outCount = 0;
    ArpState a{};
    for (int tick = 0; tick < outTicks && outCount < kMaxNotes; tick++)
    {
        ArpOut o;
        if (arpCountdown(a, o) && outCount < kMaxNotes)
        {
            Note n{};
            n.start = (uint16_t)tick;
            n.length = (uint16_t)clampInt(o.gate, 1, 0x7FFF);
            n.pitch = o.pitch;
            n.velocity = o.velocity;
            out[outCount++] = n;
        }
        int t = (tick % masterTicks) % srcTicks;
        for (int i = 0; i < count; i++)
        {
            if (src[i].start != t || src[i].length == 0)
                continue;
            int chance = src[i].chance();
            if (chance < 100 && !percent(chance))
                continue;
            addHeld(a, src[i].pitch, src[i].velocity ? src[i].velocity : 100, src[i].length);
        }
        if (a.heldCount > 0 && tick % rate == 0 && arpStep(a, s, (uint32_t)tick, o) && outCount < kMaxNotes)
        {
            Note n{};
            n.start = (uint16_t)tick;
            n.length = (uint16_t)o.gate;
            n.pitch = o.pitch;
            n.velocity = o.velocity;
            out[outCount++] = n;
        }
    }
    if (outCount == 0)
        return 0;

    clearNotes(p, track);
    for (int i = 0; i < outCount; i++)
        addNote(p, track, out[i]);
    s.arpMode = kArpOff;
    s.apeOn = false;
    setSettings(p, track, s);
    setTrackSteps(p, track, outSteps);
    return outCount;
}

void setLiveMetronome(bool on)
{
    s_liveMetronome = on;
}

bool liveMetronome()
{
    return s_liveMetronome;
}

Sequencer::strPattern* current()
{
    return sequencer.getActualPattern();
}

bool isMelodic(int track)
{
    return isMelodic(current(), track);
}

uint32_t random32()
{
    return nextRandom();
}

// =====================================================================
// playback (audio context)
// =====================================================================

AUDIO_CORE_RAM void playMicroStep(int track, int tickInPattern, int patternTicks)
{
    ensureInit();
    if (!validTrack(track))
        return;
    (void)patternTicks;

    TrackPlayState& st = playState[track];
    uint32_t runTick = st.runTicks++;

    const Sequencer::strPattern* p = current();
    Header h;
    bool has = readHeader(p, track, h);
    int trackTicks = effectiveSteps(p, track, has ? &h : nullptr) * kTicksPerStep;
    int tick = tickInPattern % trackTicks;
    s_playTick[track] = tick;

    // 1. count down / release sounding notes and held arp notes
    for (auto& a : st.notes)
    {
        if (a.remaining < 0)
            continue;
        a.remaining--;
        if (a.remaining <= 0)
            releaseActive(track, a);
    }
    bool muted = mtProject.values.trackMute[track] != 0;

    ArpOut ratchet;
    bool ratchetNow = arpCountdown(st.arp, ratchet);

    if (muted || !has)
        return;

    const uint8_t* raw = rawBytes(p, track);
    TrackSettings settings = decodeSettings(raw + kSlotSettings * kSlotBytes, h);
    settings.apeSeed = readExtras(raw).seed;
    uint8_t instrument = instrumentOrDefault(h.sound, track);
    // slice tracks play like drums: one chop at a time
    bool drum = !(h.flags & kHeaderFlagMelodic) || isBeatSliceSound(instrument);
    bool arp = settings.arpMode != kArpOff;

    // 2. second hit of a Go Ape double
    if (ratchetNow && arp)
        startNote(track, st, instrument, ratchet.pitch, ratchet.velocity, ratchet.gate, false);

    // 3. notes that begin on this micro step
    const uint8_t* base = raw + kSlotNotes * kSlotBytes;
    for (int i = 0; i < h.count; i++)
    {
        Note n;
        memcpy(&n, base + i * sizeof(Note), sizeof(Note));
        if (n.start != tick || n.length == 0)
            continue;

        if (st.skipTick == tick)
        {
            bool skip = false;
            for (int k = 0; k < st.skipCount; k++)
                if (st.skipPitch[k] == n.pitch)
                {
                    skip = true;
                    st.skipPitch[k] = 0xFF; // only once
                }
            if (skip)
                continue;
        }

        int chance = n.chance();
        if (chance < 100 && !percent(chance))
            continue;

        uint8_t velocity = n.velocity ? n.velocity : 100;
        if (arp)
        {
            addHeld(st.arp, n.pitch, velocity, n.length);
        }
        else
        {
            // drum tracks are monophonic: a new hit cuts the one before.
            // Hits of up to one step ring out until then; longer ones are gated.
            bool oneShot = drum && n.length <= kTicksPerStep;
            if (drum)
                chokeTrack(track, st);
            startNote(track, st, instrument, n.pitch, velocity, n.length, oneShot, n.flags);
        }
    }

    if (st.skipTick == tick)
        st.skipTick = -1;

    // 4. arpeggiator clock: from the start of playback, so Go Ape sequences
    //    line up with the bars
    ArpOut o;
    if (arp && st.arp.heldCount > 0 && (runTick % kArpRateTicks[settings.arpRate]) == 0 &&
        arpStep(st.arp, settings, runTick, o))
    {
        startNote(track, st, instrument, o.pitch, o.velocity, o.gate, false);
    }
}

void stopTrack(int track)
{
    ensureInit();
    if (!validTrack(track))
        return;
    TrackPlayState& st = playState[track];
    for (auto& a : st.notes)
        if (a.remaining >= 0)
            releaseActive(track, a);
    st.arp.heldCount = 0;
    st.arp.ratchetIn = 0;
}

void stopAll()
{
    ensureInit();
    for (int t = 0; t < kTracks; t++)
    {
        stopTrack(t);
        playState[t].runTicks = 0; // the next playback starts its arp cycle fresh
    }

    // silence every sample voice we started (ringing one-shots, loops)
    for (int v = 0; v < kTracks; v++)
    {
        // voices still marked "on" here belong to a key the user is holding
        if ((s_triggeredVoices & (1u << v)) && !voiceUse[v].on)
        {
            instrumentPlayer[v].noteOff();
            s_triggeredVoices &= (uint8_t)~(1u << v);
        }
        s_legacyPlayed[v] = false;
    }
}

int voiceOwner(int voice)
{
    if (voice < 0 || voice >= kTracks)
        return 0;
    return s_lastVoiceOwner[voice] >= 0 ? s_lastVoiceOwner[voice] : voice;
}

void markLegacyPlayed(int track)
{
    if (validTrack(track))
        s_legacyPlayed[track] = true;
}

int playheadTick(int track)
{
    if (!validTrack(track) || !sequencer.isPlay())
        return -1;
    return s_playTick[track];
}

bool isSounding(int track)
{
    if (!validTrack(track) || !initDone)
        return false;
    if (audition[track].on)
        return true;
    for (const auto& a : playState[track].notes)
        if (a.remaining >= 0)
            return true;
    return false;
}

// =====================================================================
// live playing (UI context)
// =====================================================================

void auditionChord(int track, const uint8_t* pitches, int count, uint8_t velocity)
{
    ensureInit();
    if (!validTrack(track) || count <= 0 || pitches == nullptr)
        return;
    auditionOff(track);

    const Sequencer::strPattern* p = current();
    uint8_t instrument = getSound(p, track);
    bool sample = isSampleInstrument(instrument);
    if (sample && mtProject.instrument[instrument].isActive != 1)
        return; // empty sample slot: nothing to play

    Audition& a = audition[track];
    a.instrument = instrument;
    a.oneShot = sample && (!isMelodic(p, track) || isBeatSliceSound(instrument));
    a.count = 0;
    a.on = true;

    if (count > kMaxChordNotes)
        count = kMaxChordNotes;
    for (int i = 0; i < count; i++)
    {
        uint8_t pitch = pitches[i] > 127 ? 127 : pitches[i];
        int voice = -1;
        uint8_t serial = 0;
        if (sample)
        {
            {
                AudioLock lock;
                serial = ++playState[track].serialCounter;
                voice = allocVoice(track, serial);
            }
            instrumentPlayer[voice].noteOn(instrument, (int8_t)pitch, velocityToVolume(instrument, velocity), 0, 0, 0,
                                           0);
        }
        else
        {
            soundOnPoly(instrument, pitch, velocity);
        }
        a.pitches[a.count] = pitch;
        a.voices[a.count] = (int8_t)voice;
        a.serials[a.count] = serial;
        a.count++;
    }
}

void auditionOn(int track, uint8_t pitch, uint8_t velocity)
{
    auditionChord(track, &pitch, 1, velocity);
}

void auditionOff(int track)
{
    ensureInit();
    if (!validTrack(track) || !audition[track].on)
        return;
    Audition& a = audition[track];
    for (int i = 0; i < a.count; i++)
    {
        if (a.voices[i] >= 0)
        {
            bool mine = false;
            {
                AudioLock lock;
                VoiceUse& u = voiceUse[a.voices[i]];
                mine = u.on && u.track == track && u.serial == a.serials[i];
                if (mine)
                    u.on = false;
            }
            if (mine && !a.oneShot)
                instrumentPlayer[a.voices[i]].noteOff();
        }
        else
        {
            soundOffPoly(a.instrument, a.pitches[i]);
        }
    }
    a.count = 0;
    a.on = false;
}

bool liveRecordOn(int track, const uint8_t* pitches, int count, uint8_t velocity, bool holdLength)
{
    if (!validTrack(track) || pitches == nullptr || count <= 0 || !sequencer.isPlay())
        return false;
    Sequencer::strPattern* p = current();
    int patternTicks = trackSteps(p, track) * kTicksPerStep;
    int now = clampInt(s_playTick[track], 0, patternTicks - 1);
    int tick = now;
    bool ahead = false;
    if (s_quantizedRec)
    {
        tick = ((now + kTicksPerStep / 2) / kTicksPerStep) * kTicksPerStep;
        if (tick >= patternTicks)
            tick = 0;
        ahead = tick > now || (tick == 0 && now > 0);
    }

    bool any = false;
    for (int i = 0; i < count && i < kMaxChordNotes; i++)
    {
        Note n{};
        n.start = (uint16_t)tick;
        n.length = (uint16_t)kTicksPerStep;
        n.pitch = pitches[i] > 127 ? 127 : pitches[i];
        n.velocity = velocity ? velocity : 100;
        n.flags = holdLength ? kNoteFlagRecording : 0;
        if (addNote(p, track, n) < 0)
            break;
        any = true;
    }
    if (any && ahead)
    {
        // heard already: don't play it again when the playhead gets there
        TrackPlayState& st = playState[track];
        DISABLE_AUDIO_INTERRUPTS();
        st.skipCount = 0;
        for (int i = 0; i < count && i < kMaxChordNotes; i++)
            st.skipPitch[st.skipCount++] = pitches[i] > 127 ? 127 : pitches[i];
        st.skipTick = (int16_t)tick;
        ENABLE_AUDIO_INTERRUPTS();
    }
    return any;
}

void liveRecordOff(int track, const uint8_t* pitches, int pitchCount)
{
    if (!validTrack(track))
        return;
    Sequencer::strPattern* p = current();
    int patternTicks = trackSteps(p, track) * kTicksPerStep;
    int now = clampInt(s_playTick[track], 0, patternTicks - 1);
    int count = noteCount(p, track);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        if (!(n.flags & kNoteFlagRecording))
            continue;
        if (pitches != nullptr)
        {
            bool match = false;
            for (int k = 0; k < pitchCount; k++)
                if (pitches[k] == n.pitch)
                    match = true;
            if (!match)
                continue;
        }
        int len = now - n.start;
        if (len <= 0)
            len += patternTicks;
        if (s_quantizedRec)
            len = ((len + kTicksPerStep / 2) / kTicksPerStep) * kTicksPerStep; // whole steps
        n.length = (uint16_t)clampInt(len, s_quantizedRec ? kTicksPerStep : kTicksPerStep / 8, 0x7FFF);
        n.flags &= (uint8_t)~kNoteFlagRecording;
        setNote(p, track, i, n);
    }
}

} // namespace gibgob
