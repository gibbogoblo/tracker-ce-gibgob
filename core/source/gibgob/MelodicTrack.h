#ifndef GIBGOB_MELODICTRACK_H_
#define GIBGOB_MELODICTRACK_H_

// GibGob OS track data and playback engine.
//
// Every one of the 8 tracks stores its notes as a list of polyphonic notes
// (Drum and Melodic tracks use the same format; the type only changes how the
// track is shown and edited).
//
// Storage trick: every pattern already holds 16 tracks (8 sample + 8 MIDI).
// GibGob OS hides the MIDI tracks and reuses the bytes of MIDI track (8 + t)
// as the data of track t. Because the data lives inside the normal pattern
// struct, saving, loading and song mode work with no file format change.
//
// Layout of the 1024 bytes of track[8 + t].step[0..127] (8 bytes per slot):
//   slot 0        : header (magic, version, note count, sound, flags)
//   slots 1..125  : up to 125 notes, 8 bytes each
//   slot 126      : track settings (base note, arp, Go Ape)
//   slot 127      : two chords of the pattern's chord palette

#include "mtSequencer.h"
#include <stdint.h>

namespace gibgob
{

static constexpr int kTracks = kSampleTracks; // 8 visible tracks
static constexpr int kTicksPerStep = 48;      // sequencer micro steps per step
static constexpr int kMaxNotes = 125;         // notes per track per pattern
static constexpr int kMaxActiveNotes = 16;    // sounding notes per track
static constexpr int kMaxChordNotes = 6;
static constexpr int kPaletteSize = 16;       // 2 banks of 8 chords
static constexpr int kDefaultBaseNote = 60;   // C5 = sample at original pitch
static constexpr int kChordTypeCount = 22;    // size of the chord type pool (gibgobChords.h)
static constexpr int kVoicingCount = 4;

enum class TrackType : uint8_t
{
    Drum = 0,
    Melodic = 1,
};

struct Note
{
    uint16_t start;    // in micro steps (48 per step); allows off-grid timing
    uint16_t length;   // in micro steps
    uint8_t pitch;     // MIDI note 0..127
    uint8_t velocity;  // 1..127
    uint8_t flags;     // kNoteFlag*
    uint8_t chanceInv; // 100 - probability in %, so 0 (old notes) = always plays

    int chance() const { return 100 - (chanceInv > 100 ? 100 : chanceInv); }
    void setChance(int c) { chanceInv = (uint8_t)(100 - (c < 0 ? 0 : (c > 100 ? 100 : c))); }
};
static_assert(sizeof(Note) == 8, "Note must fit exactly in one step slot");

static constexpr uint8_t kNoteFlagSelected = 0x01;  // UI selection
static constexpr uint8_t kNoteFlagRecording = 0x02; // being live-recorded (key still held)

// ---- per-track settings ----
enum ArpMode : uint8_t
{
    kArpOff = 0,
    kArpUp,
    kArpDown,
    kArpUpDown,
    kArpRandom,
    kArpAsPlaced,
    kArpModeCount
};

static constexpr int kArpRateCount = 6;
extern const uint8_t kArpRateTicks[kArpRateCount]; // 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
extern const char* const kArpRateNames[kArpRateCount];
extern const char* const kArpModeNames[kArpModeCount];

struct TrackSettings
{
    uint8_t baseNote = kDefaultBaseNote; // note used by the track button / lane steps
    uint8_t arpMode = kArpOff;
    uint8_t arpRate = 3;    // index into kArpRateTicks (1/16)
    uint8_t arpOctaves = 1; // 1..4
    uint8_t arpGate = 50;   // 10..100 %
    bool apeOn = false;     // "Go Ape": randomise the arp
    uint8_t apeRhythm = 40; // 0..100
    uint8_t apeGate = 40;   // 0..100
    uint8_t apeNotes = 40;  // 0..100
};

// ---- chord palette (one complete chord per top-row button) ----
struct PaletteChord
{
    uint8_t root;      // MIDI note of the root
    uint8_t type;      // index into the chord type pool (see gibgobChords.h)
    uint8_t inversion; // 0..n-1
    uint8_t voicing;   // see gibgobChords.h
};

// ---- pattern-level access (pattern may be any loaded pattern buffer) ----
bool hasHeader(const Sequencer::strPattern* p, int track);

TrackType getTrackType(const Sequencer::strPattern* p, int track);
void setTrackType(Sequencer::strPattern* p, int track, TrackType type);
bool isMelodic(const Sequencer::strPattern* p, int track);

uint8_t getSound(const Sequencer::strPattern* p, int track);
void setSound(Sequencer::strPattern* p, int track, uint8_t instrument);

TrackSettings getSettings(const Sequencer::strPattern* p, int track);
void setSettings(Sequencer::strPattern* p, int track, const TrackSettings& s);

PaletteChord getPaletteChord(const Sequencer::strPattern* p, int idx);
void setPaletteChord(Sequencer::strPattern* p, int idx, const PaletteChord& c);

int noteCount(const Sequencer::strPattern* p, int track);
Note getNote(const Sequencer::strPattern* p, int track, int idx);
void setNote(Sequencer::strPattern* p, int track, int idx, const Note& n);
int addNote(Sequencer::strPattern* p, int track, const Note& n); // index, or -1 if full
void removeNote(Sequencer::strPattern* p, int track, int idx);
void clearNotes(Sequencer::strPattern* p, int track);
// last note covering (tick, pitch), or -1
int findNoteAt(const Sequencer::strPattern* p, int track, int tick, int pitch);

// Remember this pattern's track types, sounds and palette as the defaults for
// patterns that have no GibGob data yet (so a new pattern starts the same way).
void adoptDefaults(const Sequencer::strPattern* p);

// ---- tools ----
void quantizeTrack(Sequencer::strPattern* p, int track);
// fills the track with a random acid-style line (notes, rests, accents, slides)
void generateAcid(Sequencer::strPattern* p, int track, int lengthSteps);

// ---- helpers on the sequencer's current pattern ----
Sequencer::strPattern* current();
bool isMelodic(int track);

// ---- playback (called from the sequencer, audio context) ----
void playMicroStep(int track, int tickInPattern, int patternTicks);
void stopAll();
void stopTrack(int track);
void markLegacyPlayed(int track); // an old tracker step sounded on this track
int playheadTick(int track);      // micro steps, -1 when stopped
bool isSounding(int track);       // for activity lights

// ---- live playing (UI context) ----
void auditionOn(int track, uint8_t pitch, uint8_t velocity);
void auditionChord(int track, const uint8_t* pitches, int count, uint8_t velocity);
void auditionOff(int track);

// Record what is being played into the pattern at the play position.
// holdLength = true: the note length is set when liveRecordOff() is called.
// Returns false if the sequencer is stopped or the track is full.
bool liveRecordOn(int track, const uint8_t* pitches, int count, uint8_t velocity, bool holdLength);
// pitches == nullptr finishes every note still being recorded on the track
void liveRecordOff(int track, const uint8_t* pitches = nullptr, int count = 0);

uint32_t random32();

} // namespace gibgob

#endif
