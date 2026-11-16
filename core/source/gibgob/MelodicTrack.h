#ifndef GIBGOB_MELODICTRACK_H_
#define GIBGOB_MELODICTRACK_H_

// GibGob OS: polyphonic "Melodic" (piano roll) tracks.
//
// Storage trick: every pattern already holds 16 tracks (8 sample + 8 MIDI).
// GibGob OS hides the MIDI tracks and reuses the bytes of MIDI track (8 + t)
// as the note list of sample track t. Because the data lives inside the normal
// pattern struct, saving, loading, copying, undo and song mode keep working
// with no file format change.
//
// Layout of the 1024 bytes of track[8 + t].step[0..127] (8 bytes per step):
//   step[0]      : header (magic, version, note count, sound, flags)
//   step[1..127] : up to 127 notes, 8 bytes each

#include "mtSequencer.h"
#include <stdint.h>

namespace gibgob
{

static constexpr int kTracks = kSampleTracks;   // 8 visible tracks
static constexpr int kTicksPerStep = 48;        // sequencer micro steps per step
static constexpr int kMaxNotes = Sequencer::MAXSTEP; // 127 notes per track per pattern
static constexpr int kMaxActiveNotes = 16;      // sounding notes per track

enum class TrackType : uint8_t
{
    Drum = 0,
    Melodic = 1,
};

struct Note
{
    uint16_t start;  // in micro steps (48 per step); allows off-grid nudge
    uint16_t length; // in micro steps
    uint8_t pitch;   // MIDI note 0..127
    uint8_t velocity; // 1..127
    uint8_t flags;   // bit0 = selected (UI only)
    uint8_t reserved;
};
static_assert(sizeof(Note) == 8, "Note must fit exactly in one step slot");

static constexpr uint8_t kNoteFlagSelected = 0x01;

// ---- pattern-level access (pattern may be any loaded pattern buffer) ----
TrackType getTrackType(const Sequencer::strPattern* p, int track);
void setTrackType(Sequencer::strPattern* p, int track, TrackType type);
bool isMelodic(const Sequencer::strPattern* p, int track);

uint8_t getSound(const Sequencer::strPattern* p, int track);
void setSound(Sequencer::strPattern* p, int track, uint8_t instrument);

int noteCount(const Sequencer::strPattern* p, int track);
Note getNote(const Sequencer::strPattern* p, int track, int idx);
void setNote(Sequencer::strPattern* p, int track, int idx, const Note& n);
// returns index or -1 if full
int addNote(Sequencer::strPattern* p, int track, const Note& n);
void removeNote(Sequencer::strPattern* p, int track, int idx);
void clearNotes(Sequencer::strPattern* p, int track);
// first note covering (tick, pitch), or -1
int findNoteAt(const Sequencer::strPattern* p, int track, int tick, int pitch);

// ---- helpers on the sequencer's current pattern ----
Sequencer::strPattern* current();
bool isMelodic(int track);

// ---- playback (called from the sequencer, audio context) ----
void playMicroStep(int track, int tickInPattern, int patternTicks);
void stopAll();
void stopTrack(int track);

// ---- audition (UI) ----
void auditionOn(int track, uint8_t pitch, uint8_t velocity);
void auditionChord(int track, const uint8_t* pitches, int count, uint8_t velocity);
void auditionOff(int track);

} // namespace gibgob

#endif
