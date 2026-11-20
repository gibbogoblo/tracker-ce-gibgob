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
//   slots 1..123  : up to 123 notes, 8 bytes each
//   slot 124      : slice tracks: the slice on each top-row button
//   slot 125      : extras (Go Ape seed, track length)
//   slot 126      : track settings (base note, arp, Go Ape)
//   slot 127      : two chords of the pattern's chord palette

#include "mtSequencer.h"
#include <stdint.h>

namespace gibgob
{

static constexpr int kTracks = kSampleTracks; // 8 visible tracks
static constexpr int kTicksPerStep = 48;      // sequencer micro steps per step
static constexpr int kMaxNotes = 123;         // notes per track per pattern
static constexpr int kMaxTrackSteps = 128;    // longest track
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
static constexpr uint8_t kNoteFlagSlide = 0x04;     // glide into this note from the one before
static constexpr uint8_t kNoteFlagAccent = 0x08;    // louder, filter opened
// bits 4..7: filter envelope variant (0 = instrument's own envelope)
inline int noteEnv(const Note& n)
{
    return (n.flags >> 4) & 0x0F;
}
inline void setNoteEnv(Note& n, int env)
{
    n.flags = (uint8_t)((n.flags & 0x0F) | ((env & 0x0F) << 4));
}

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
    uint32_t apeSeed = 0;   // the random sequence Go Ape made (0 = none yet)
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

// ---- slice tracks ----
// A track is a Slice track whenever its sound is a sample in Beat Slice mode
// (checked live, so it does not matter which happens first). Its notes are
// slices: pitch kFirstSliceNote + slice, like the stock beat-slice playback.
// It plays like a drum track: each chop cuts the one before.
static constexpr int kFirstSliceNote = 24;
bool isSliceTrack(const Sequencer::strPattern* p, int track);
int sliceCount(const Sequencer::strPattern* p, int track); // 0 when not a slice track
// the slice (0..) a top-row button plays (default: button 1 = slice 1 ...)
int sliceForButton(const Sequencer::strPattern* p, int track, int button);
void setSliceForButton(Sequencer::strPattern* p, int track, int button, int slice);
// the note a track's own button / a new lane step plays: the base note, or on
// a slice track the slice of top button 1
uint8_t laneNote(const Sequencer::strPattern* p, int track);

// Remember this pattern's track types, sounds and palette as the defaults for
// patterns that have no GibGob data yet (so a new pattern starts the same way).
void adoptDefaults(const Sequencer::strPattern* p);
// back to the factory layout (tracks 1-4 drums, 5-8 melodic, sounds 1-8,
// default chord palette, straight swing). Called for every new project.
void resetDefaults();

// ---- track length ----
// steps of the pattern (the longest track); every track loops inside it
int patternSteps(const Sequencer::strPattern* p);
// steps of one track (1..128)
int trackSteps(const Sequencer::strPattern* p, int track);
// sets one track's length; the pattern grows or shrinks to the longest track
void setTrackSteps(Sequencer::strPattern* p, int track, int steps);
// doubles the track and copies its notes into the new half.
// Returns false (and changes nothing) if it would be longer than 128 steps
// or need more than kMaxNotes notes.
bool duplicateTrack(Sequencer::strPattern* p, int track);

// ---- tools ----
// strength 1..10: 1 leaves notes within +-3 of the grid, 10 snaps them onto it
void quantizeTrack(Sequencer::strPattern* p, int track, int strength = 10);
// amount 1..10: nudges every note randomly by up to +-3 (1) .. +-20 (10)
void humanizeTrack(Sequencer::strPattern* p, int track, int amount);

enum class AcidNotes : uint8_t
{
    InUse = 0, // follow the notes / chords other melodic tracks play at that moment
    Pick = 1,  // only the notes picked by hand
};
struct AcidOptions
{
    uint8_t density = 7;  // 1..10: chance of a note on each step, x10 %
    uint8_t slide = 3;    // 0..10: chance of a slide, x10 %
    AcidNotes notes = AcidNotes::InUse;
    uint8_t picked[24];   // picked pitches (Pick mode)
    uint8_t pickedCount = 0;
    uint8_t accent = 3;   // 0..10: chance of an accent, x10 %
    uint8_t envelope = 0; // 0..10: chance of a random filter envelope, x10 %
};
// replaces the track's notes with a random acid line over the track's length
void generateAcid(Sequencer::strPattern* p, int track, const AcidOptions& o);

// Turns what the arpeggiator (with Go Ape) plays into real notes: renders
// max(track length, 4 bars) steps, sets the track to that length and turns
// the arp off. Returns the number of notes written (0 = nothing to render).
int renderArp(Sequencer::strPattern* p, int track);

// ---- note utilities (piano roll Shift functions) ----
// transposes the selected notes, or every note of the track if none is selected
void transposeNotes(Sequencer::strPattern* p, int track, int semitones);
// every note lasts until the next note starts (chords keep their shared start)
void legatoNotes(Sequencer::strPattern* p, int track);
// plays the track backwards: a note that ended at the end now starts at the start
void reverseNotes(Sequencer::strPattern* p, int track);
// note clipboard: copy notes (by index), paste them with the first one at `tick`
void copyNotes(const Sequencer::strPattern* p, int track, const int* indices, int count);
int pasteNotes(Sequencer::strPattern* p, int track, int tick); // notes pasted
bool hasClipboard();

// nudge around a step: offset of a note from its step (-24..23), step 1 can
// reach back into the end of the track
int stepOffset(int start, int step, int trackTicks);
int startFromOffset(int step, int offset, int trackTicks);
bool belongsToStep(int start, int step, int trackTicks);

// ---- global swing (stored with the pattern, carried into new patterns) ----
int patternSwing(const Sequencer::strPattern* p); // 50 = straight, 25..75
void setPatternSwing(Sequencer::strPattern* p, int value);

// ---- quantized live recording (notes land on the nearest step) ----
void setQuantizedRecording(bool on);
bool quantizedRecording();

// ---- metronome while live recording ----
void setLiveMetronome(bool on);
bool liveMetronome();

// ---- helpers on the sequencer's current pattern ----
Sequencer::strPattern* current();
bool isMelodic(int track);

// ---- playback (called from the sequencer, audio context) ----
// tickInPattern counts from the pattern start; each track loops at its own length
void playMicroStep(int track, int tickInPattern, int patternTicks);
void stopAll();
// called (audio context) whenever the sequencer starts a note on a track
extern void (*g_noteStartHook)(int track);
// Singing synths (LILITRON): a note's syllable lives in the top half of its
// flags (where sample notes keep their envelope variant). The UI sets this to
// say which instruments sing; the syllable then goes out as MIDI CC 3 just
// before the note.
extern bool (*g_singsSyllables)(uint8_t instrument);
static constexpr uint8_t kSyllableCC = 3;
static constexpr int kSyllables = 8;
inline int noteSyllable(const Note& n)
{
    return ((n.flags >> 4) & 0x0F) % kSyllables;
}
inline void setNoteSyllable(Note& n, int syllable)
{
    n.flags = (uint8_t)((n.flags & 0x0F) | ((((syllable % kSyllables) + kSyllables) % kSyllables) << 4));
}
// the syllable the top buttons sing live (auditions and live recording)
extern int g_liveSyllable;
extern const char* const kSyllableNames[kSyllables]; // LILITRON's, in order
int lastSyllable(int track); // the syllable last sung on a track (for the singer picture)
void stopTrack(int track);
void markLegacyPlayed(int track); // an old tracker step sounded on this track
int playheadTick(int track);      // micro steps inside the track, -1 when stopped
int voiceOwner(int voice);        // track that last played on a sample voice (chords borrow voices)
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
