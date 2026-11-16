#include "gibgob/MelodicTrack.h"

#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "mtStructs.h"

#include <string.h>

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
constexpr int kSlotSettings = 126;
constexpr int kSlotPalette = 127;
constexpr int kTrackBytes = 128 * kSlotBytes;


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
    uint8_t melodic[kTracks];
    uint8_t sound[kTracks];
    PaletteChord palette[kPaletteSize];
};

Defaults g_def = {
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
};

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

struct TrackPlayState
{
    ActiveNote notes[kMaxActiveNotes];
    HeldNote held[kMaxHeld]; // notes feeding the arpeggiator
    uint8_t heldCount;
    uint16_t arpIdx;
    // ratchet (second hit of a Go Ape double)
    int16_t ratchetIn;
    uint8_t ratchetPitch;
    uint8_t ratchetVel;
    int16_t ratchetGate;
    uint8_t ratchetInstrument;
    uint8_t serialCounter;
};

TrackPlayState playState[kTracks];
volatile int s_playTick[kTracks];

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
        playState[t].heldCount = 0;
        playState[t].ratchetIn = 0;
        playState[t].arpIdx = 0;
        playState[t].serialCounter = 0;
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
int allocVoice(int track, uint8_t serial)
{
    int v = track;
    if (voiceUse[track].on)
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
    s_triggeredVoices |= (uint8_t)(1u << v);
    return v;
}

inline int8_t velocityToVolume(uint8_t velocity)
{
    return (int8_t)((velocity * 100 + 63) / 127);
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

// audio context
void startNote(int track,
               TrackPlayState& st,
               uint8_t instrument,
               uint8_t pitch,
               uint8_t velocity,
               int lengthTicks,
               bool oneShot)
{
    bool sample = isSampleInstrument(instrument);

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
        int v = allocVoice(track, slot->serial);
        slot->voice = (int8_t)v;
        instrumentPlayer[v].noteOn(instrument, (int8_t)pitch, velocityToVolume(velocity), 0, 0, 0, 0);
    }
    else
    {
        soundOnPoly(instrument, pitch, velocity);
    }
}

void removeHeldAt(TrackPlayState& st, int i)
{
    for (int k = i; k + 1 < st.heldCount; k++)
        st.held[k] = st.held[k + 1];
    st.heldCount--;
}

// One step of the arpeggiator. Go Ape randomises rhythm, gate and notes.
void arpStep(int track, TrackPlayState& st, const TrackSettings& s, uint8_t instrument)
{
    int n = st.heldCount;
    if (n <= 0)
        return;

    // pitches in playing order
    uint8_t pitch[kMaxHeld];
    uint8_t vel[kMaxHeld];
    for (int i = 0; i < n; i++)
    {
        pitch[i] = st.held[i].pitch;
        vel[i] = st.held[i].velocity;
    }
    if (s.arpMode != kArpAsPlaced)
    {
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++)
                if (pitch[b] < pitch[a])
                {
                    uint8_t t = pitch[a];
                    pitch[a] = pitch[b];
                    pitch[b] = t;
                    t = vel[a];
                    vel[a] = vel[b];
                    vel[b] = t;
                }
    }

    int total = n * s.arpOctaves;
    int idx = st.arpIdx++;
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
        case kArpRandom: k = (int)(nextRandom() % (uint32_t)total); break;
        default: k = idx % total; break; // up, as placed
    }

    int rate = kArpRateTicks[s.arpRate];
    int gate = rate * s.arpGate / 100;
    int outPitch = pitch[k % n] + 12 * (k / n);
    int outVel = vel[k % n];

    if (s.apeOn)
    {
        // rhythm: rests and doubles
        if (percent(s.apeRhythm * 45 / 100))
            return; // rest
        bool ratchet = percent(s.apeRhythm * 25 / 100);

        // gate length
        if (s.apeGate > 0)
        {
            int delta = (int)(nextRandom() % (uint32_t)(2 * s.apeGate + 1)) - s.apeGate;
            gate = gate * (100 + delta) / 100;
        }

        // notes: add fifths, sevenths and octaves, up and down
        if (percent(s.apeNotes * 60 / 100))
        {
            static const int8_t kOffsets[6] = { 7, 12, -12, 10, -5, 19 };
            int shifted = outPitch + kOffsets[nextRandom() % 6];
            if (shifted >= 0 && shifted <= 127)
                outPitch = shifted;
        }

        if (ratchet && rate >= 12)
        {
            st.ratchetIn = (int16_t)(rate / 2);
            st.ratchetPitch = (uint8_t)clampInt(outPitch, 0, 127);
            st.ratchetVel = (uint8_t)clampInt(outVel * 3 / 4, 1, 127);
            st.ratchetGate = (int16_t)clampInt(gate / 2, 2, rate / 2);
            st.ratchetInstrument = instrument;
            gate = clampInt(gate, 2, rate / 2);
        }
    }

    gate = clampInt(gate, 2, rate * 2);
    startNote(track, st, instrument, (uint8_t)clampInt(outPitch, 0, 127), (uint8_t)clampInt(outVel, 1, 127),
              gate, false);
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
    return decodeSettings(rawBytes(p, track) + kSlotSettings * kSlotBytes, h);
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
        }
    }
    for (int i = 0; i < kPaletteSize; i++)
    {
        PaletteChord c;
        if (readStoredChord(p, i, c))
            g_def.palette[i] = c;
    }
}

// =====================================================================
// tools
// =====================================================================

void quantizeTrack(Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return;
    int patternTicks = (p->track[track].length + 1) * kTicksPerStep;
    int count = noteCount(p, track);
    for (int i = 0; i < count; i++)
    {
        Note n = getNote(p, track, i);
        int start = ((n.start + kTicksPerStep / 2) / kTicksPerStep) * kTicksPerStep;
        if (start >= patternTicks)
            start = 0;
        n.start = (uint16_t)start;
        setNote(p, track, i, n);
    }
}

void generateAcid(Sequencer::strPattern* p, int track, int lengthSteps)
{
    if (!validTrack(track) || p == nullptr)
        return;
    clearNotes(p, track);

    TrackSettings s = getSettings(p, track);
    int root = s.baseNote >= 24 ? s.baseNote - 24 : s.baseNote; // bass register

    // minor pentatonic with the root and octave weighted up
    static const int8_t kScale[10] = { 0, 0, 0, 12, 12, 3, 5, 7, 10, -2 };

    lengthSteps = clampInt(lengthSteps, 1, Sequencer::MAXSTEP + 1);
    for (int step = 0; step < lengthSteps; step++)
    {
        if (!percent(72))
            continue; // rest
        Note n{};
        n.start = (uint16_t)(step * kTicksPerStep);
        n.pitch = (uint8_t)clampInt(root + kScale[nextRandom() % 10], 0, 127);
        n.velocity = percent(30) ? 127 : 84;                     // accent
        n.length = percent(28) ? (uint16_t)(kTicksPerStep + 12)  // slide: overlaps the next step
                               : (uint16_t)(kTicksPerStep / 2);
        if (addNote(p, track, n) < 0)
            break;
    }
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

    TrackPlayState& st = playState[track];
    s_playTick[track] = tickInPattern;

    // 1. count down / release sounding notes and held arp notes
    for (auto& a : st.notes)
    {
        if (a.remaining < 0)
            continue;
        a.remaining--;
        if (a.remaining <= 0)
            releaseActive(track, a);
    }
    for (int i = st.heldCount - 1; i >= 0; i--)
    {
        if (--st.held[i].remaining <= 0)
            removeHeldAt(st, i);
    }

    bool muted = mtProject.values.trackMute[track] != 0;

    // 2. second hit of a Go Ape double
    if (st.ratchetIn > 0 && --st.ratchetIn == 0 && !muted)
    {
        startNote(track, st, st.ratchetInstrument, st.ratchetPitch, st.ratchetVel, st.ratchetGate, false);
    }

    if (muted)
        return;

    const Sequencer::strPattern* p = current();
    Header h;
    if (!readHeader(p, track, h))
        return;

    const uint8_t* raw = rawBytes(p, track);
    TrackSettings settings = decodeSettings(raw + kSlotSettings * kSlotBytes, h);
    uint8_t instrument = instrumentOrDefault(h.sound, track);
    bool drum = !(h.flags & kHeaderFlagMelodic);
    bool arp = settings.arpMode != kArpOff;

    // 3. notes that begin on this micro step
    const uint8_t* base = raw + kSlotNotes * kSlotBytes;
    for (int i = 0; i < h.count; i++)
    {
        Note n;
        memcpy(&n, base + i * sizeof(Note), sizeof(Note));
        if (n.start != tickInPattern || n.start >= patternTicks || n.length == 0)
            continue;

        int chance = n.chance();
        if (chance < 100 && !percent(chance))
            continue;

        uint8_t velocity = n.velocity ? n.velocity : 100;
        if (arp)
        {
            if (st.heldCount < kMaxHeld)
            {
                if (st.heldCount == 0)
                    st.arpIdx = 0;
                HeldNote& hn = st.held[st.heldCount++];
                hn.pitch = n.pitch;
                hn.velocity = velocity;
                hn.remaining = (int16_t)clampInt(n.length, 1, 0x7FFF);
            }
        }
        else
        {
            // drum hits of up to one step ring out; longer notes are gated
            bool oneShot = drum && n.length <= kTicksPerStep;
            startNote(track, st, instrument, n.pitch, velocity, n.length, oneShot);
        }
    }

    // 4. arpeggiator clock, locked to the pattern grid
    if (arp && st.heldCount > 0 && (tickInPattern % kArpRateTicks[settings.arpRate]) == 0)
    {
        arpStep(track, st, settings, instrument);
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
    st.heldCount = 0;
    st.ratchetIn = 0;
}

void stopAll()
{
    ensureInit();
    for (int t = 0; t < kTracks; t++)
        stopTrack(t);

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
    a.oneShot = sample && !isMelodic(p, track);
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
            instrumentPlayer[voice].noteOn(instrument, (int8_t)pitch, velocityToVolume(velocity), 0, 0, 0, 0);
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
    int patternTicks = (p->track[track].length + 1) * kTicksPerStep;
    int tick = clampInt(s_playTick[track], 0, patternTicks - 1);

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
    return any;
}

void liveRecordOff(int track, const uint8_t* pitches, int pitchCount)
{
    if (!validTrack(track))
        return;
    Sequencer::strPattern* p = current();
    int patternTicks = (p->track[track].length + 1) * kTicksPerStep;
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
        n.length = (uint16_t)clampInt(len, kTicksPerStep / 8, 0x7FFF);
        n.flags &= (uint8_t)~kNoteFlagRecording;
        setNote(p, track, i, n);
    }
}

} // namespace gibgob
