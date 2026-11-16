#include "gibgob/MelodicTrack.h"

#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "mtStructs.h"

#include <string.h>

namespace gibgob
{

namespace
{
constexpr uint8_t kMagic0 = 'G';
constexpr uint8_t kMagic1 = 'M';
constexpr uint8_t kVersion = 1;

constexpr uint8_t kHeaderFlagMelodic = 0x01;

struct Header
{
    uint8_t magic0;
    uint8_t magic1;
    uint8_t version;
    uint8_t count;
    uint8_t sound;
    uint8_t flags;
    uint8_t reserved[2];
};
static_assert(sizeof(Header) == 8, "header must fit one step slot");

inline uint8_t* rawBytes(Sequencer::strPattern* p, int track)
{
    return reinterpret_cast<uint8_t*>(&p->track[kMidiTrackFirstIdx + track].step[0]);
}
inline const uint8_t* rawBytes(const Sequencer::strPattern* p, int track)
{
    return reinterpret_cast<const uint8_t*>(&p->track[kMidiTrackFirstIdx + track].step[0]);
}

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
    h.sound = (uint8_t)track;
    h.flags = 0;
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

void writeHeader(Sequencer::strPattern* p, int track, const Header& h)
{
    uint8_t* raw = rawBytes(p, track);
    Header old;
    if (!readHeader(p, track, old))
    {
        // first use of this hidden track: wipe old MIDI step data
        memset(raw, 0, (kMaxNotes + 1) * sizeof(Note));
    }
    memcpy(raw, &h, sizeof(Header));
}

inline bool validTrack(int track)
{
    return track >= 0 && track < kTracks;
}

// ---------------- playback state ----------------
struct ActiveNote
{
    int16_t remaining; // micro steps until note off; <0 = slot free
    uint8_t pitch;
    uint8_t instrument;
    uint8_t serial;
};

struct TrackPlayState
{
    ActiveNote notes[kMaxActiveNotes];
    uint8_t serialCounter;
    uint8_t lastSampleSerial; // serial of the note currently owning the mono sample voice
    bool sampleVoiceOn;
};

TrackPlayState playState[kTracks];
volatile int s_playTick[kTracks];

struct Audition
{
    bool on;
    uint8_t count;
    uint8_t pitches[8];
    uint8_t instrument;
};
Audition audition[kTracks];

void soundOn(int track, uint8_t instrument, uint8_t pitch, uint8_t velocity)
{
    if (isSampleInstrument(instrument))
    {
        int8_t vol = (int8_t)((velocity * 100 + 63) / 127);
        instrumentPlayer[track].noteOn(instrument, (int8_t)pitch, vol, 0, 0, 0, 0);
    }
    else
    {
        bool toSynth = isSynthInstrument(instrument);
        sendMidiNoteOn(pitch, velocity, instrumentIdxToMidiChannel(instrument), toSynth);
    }
}

void soundOffPoly(uint8_t instrument, uint8_t pitch)
{
    bool toSynth = isSynthInstrument(instrument);
    sendMidiNoteOff(pitch, 0, instrumentIdxToMidiChannel(instrument), toSynth);
}

void releaseSlot(int track, ActiveNote& a)
{
    TrackPlayState& st = playState[track];
    if (isSampleInstrument(a.instrument))
    {
        // mono sample voice: only stop it if this note is still the one playing
        if (st.sampleVoiceOn && st.lastSampleSerial == a.serial)
        {
            instrumentPlayer[track].noteOff();
            st.sampleVoiceOn = false;
        }
    }
    else
    {
        soundOffPoly(a.instrument, a.pitch);
    }
    a.remaining = -1;
}

bool initDone = false;
void ensureInit()
{
    if (initDone)
        return;
    for (int t = 0; t < kTracks; t++)
    {
        for (auto& a : playState[t].notes)
            a.remaining = -1;
        playState[t].sampleVoiceOn = false;
        playState[t].serialCounter = 0;
        audition[t].on = false;
    }
    initDone = true;
}

} // namespace

// ---------------- data access ----------------

TrackType getTrackType(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return TrackType::Drum;
    Header h;
    if (!readHeader(p, track, h))
        return TrackType::Drum;
    return (h.flags & kHeaderFlagMelodic) ? TrackType::Melodic : TrackType::Drum;
}

void setTrackType(Sequencer::strPattern* p, int track, TrackType type)
{
    if (!validTrack(track) || p == nullptr)
        return;
    Header h = headerOrDefault(p, track);
    if (type == TrackType::Melodic)
        h.flags |= kHeaderFlagMelodic;
    else
        h.flags &= ~kHeaderFlagMelodic;
    writeHeader(p, track, h);
}

bool isMelodic(const Sequencer::strPattern* p, int track)
{
    return getTrackType(p, track) == TrackType::Melodic;
}

uint8_t getSound(const Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return 0;
    uint8_t s = headerOrDefault(p, track).sound;
    if (s >= kTotalInstrumentsCount)
        s = (uint8_t)track;
    return s;
}

void setSound(Sequencer::strPattern* p, int track, uint8_t instrument)
{
    if (!validTrack(track) || p == nullptr || instrument >= kTotalInstrumentsCount)
        return;
    Header h = headerOrDefault(p, track);
    h.sound = instrument;
    writeHeader(p, track, h);
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
    memcpy(&n, rawBytes(p, track) + sizeof(Header) + idx * sizeof(Note), sizeof(Note));
    return n;
}

void setNote(Sequencer::strPattern* p, int track, int idx, const Note& n)
{
    if (idx < 0 || idx >= noteCount(p, track))
        return;
    memcpy(rawBytes(p, track) + sizeof(Header) + idx * sizeof(Note), &n, sizeof(Note));
}

int addNote(Sequencer::strPattern* p, int track, const Note& n)
{
    if (!validTrack(track) || p == nullptr)
        return -1;
    Header h = headerOrDefault(p, track);
    if (h.count >= kMaxNotes)
        return -1;
    int idx = h.count;
    h.count++;
    writeHeader(p, track, h);
    setNote(p, track, idx, n);
    return idx;
}

void removeNote(Sequencer::strPattern* p, int track, int idx)
{
    int count = noteCount(p, track);
    if (idx < 0 || idx >= count)
        return;
    uint8_t* base = rawBytes(p, track) + sizeof(Header);
    memmove(base + idx * sizeof(Note),
            base + (idx + 1) * sizeof(Note),
            (count - idx - 1) * sizeof(Note));
    memset(base + (count - 1) * sizeof(Note), 0, sizeof(Note));
    Header h = headerOrDefault(p, track);
    h.count = (uint8_t)(count - 1);
    writeHeader(p, track, h);
}

void clearNotes(Sequencer::strPattern* p, int track)
{
    if (!validTrack(track) || p == nullptr)
        return;
    Header h = headerOrDefault(p, track);
    h.count = 0;
    writeHeader(p, track, h);
    memset(rawBytes(p, track) + sizeof(Header), 0, kMaxNotes * sizeof(Note));
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

Sequencer::strPattern* current()
{
    return sequencer.getActualPattern();
}

bool isMelodic(int track)
{
    return isMelodic(current(), track);
}

// ---------------- playback ----------------

AUDIO_CORE_RAM void playMicroStep(int track, int tickInPattern, int patternTicks)
{
    ensureInit();
    if (!validTrack(track))
        return;

    TrackPlayState& st = playState[track];
    s_playTick[track] = tickInPattern;

    // 1. count down / release sounding notes
    for (auto& a : st.notes)
    {
        if (a.remaining < 0)
            continue;
        a.remaining--;
        if (a.remaining <= 0)
            releaseSlot(track, a);
    }

    if (mtProject.values.trackMute[track])
        return;

    // 2. start notes that begin on this micro step
    const Sequencer::strPattern* p = current();
    Header h;
    if (!readHeader(p, track, h) || !(h.flags & kHeaderFlagMelodic))
        return;

    const uint8_t* base = rawBytes(p, track) + sizeof(Header);
    for (int i = 0; i < h.count; i++)
    {
        Note n;
        memcpy(&n, base + i * sizeof(Note), sizeof(Note));
        if (n.start >= patternTicks || n.start != tickInPattern || n.length == 0)
            continue;

        uint8_t instrument = h.sound < kTotalInstrumentsCount ? h.sound : (uint8_t)track;

        // find a free voice slot (steal the shortest remaining if full)
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
            releaseSlot(track, *slot);
        }

        // retrigger of the same pitch on a poly synth: end the old one first
        if (!isSampleInstrument(instrument))
        {
            for (auto& a : st.notes)
                if (a.remaining >= 0 && a.pitch == n.pitch && a.instrument == instrument)
                    releaseSlot(track, a);
        }

        slot->pitch = n.pitch;
        slot->instrument = instrument;
        slot->serial = ++st.serialCounter;
        slot->remaining = (int16_t)(n.length > 0x7FFF ? 0x7FFF : n.length);

        soundOn(track, instrument, n.pitch, n.velocity ? n.velocity : 100);
        if (isSampleInstrument(instrument))
        {
            st.lastSampleSerial = slot->serial;
            st.sampleVoiceOn = true;
        }
    }
}

void stopTrack(int track)
{
    ensureInit();
    if (!validTrack(track))
        return;
    for (auto& a : playState[track].notes)
        if (a.remaining >= 0)
            releaseSlot(track, a);
}

int playheadTick(int track)
{
    if (!validTrack(track) || !sequencer.isPlay())
        return -1;
    return s_playTick[track];
}

void stopAll()
{
    for (int t = 0; t < kTracks; t++)
        stopTrack(t);
}

// ---------------- audition ----------------

void auditionChord(int track, const uint8_t* pitches, int count, uint8_t velocity)
{
    ensureInit();
    if (!validTrack(track) || count <= 0)
        return;
    auditionOff(track);
    if (count > 8)
        count = 8;
    uint8_t instrument = getSound(current(), track);
    Audition& a = audition[track];
    a.instrument = instrument;
    a.count = 0;
    for (int i = 0; i < count; i++)
    {
        // a sample track is mono for now: audition only the root
        if (isSampleInstrument(instrument) && i > 0)
            break;
        soundOn(track, instrument, pitches[i], velocity);
        a.pitches[a.count++] = pitches[i];
    }
    a.on = true;
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
    if (isSampleInstrument(a.instrument))
        instrumentPlayer[track].noteOff();
    else
        for (int i = 0; i < a.count; i++)
            soundOffPoly(a.instrument, a.pitches[i]);
    a.on = false;
}

} // namespace gibgob
