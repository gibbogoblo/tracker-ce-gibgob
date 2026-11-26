#ifndef GIBGOB_WHISTLE_H_
#define GIBGOB_WHISTLE_H_

// GibGob OS: whistle to notes. A whistle is nearly a pure tone, so its pitch
// can be followed reliably. The take goes through three steps:
//
//   1. PitchTracker: every 256 samples (5.8 ms at 44.1 kHz) the pitch (YIN
//      difference function, refined over several periods), how clear it is
//      and how loud. Fed while recording, so nothing is left to do at the end.
//   2. findNotes: the pitch track cut into notes. A note starts where the
//      whistle starts or where the pitch jumps and settles; a slow glide
//      between two notes marks the second as a slide. The take's own tuning
//      (nobody whistles at A = 440 Hz) is measured and taken out, and the key
//      is guessed from the notes.
//   3. toNotes: those notes onto the sequencer's grid, moved down into a
//      playable octave, snapped to the key's scale, as GibGob Notes.
//
// Host-testable: no hardware in here (gibgob-tests/whistle_test.cpp).

#include "gibgob/MelodicTrack.h"

#include <stdint.h>

namespace gibgob
{
namespace whistle
{

constexpr uint32_t kRate = 44100;
constexpr int kHop = 256;                       // one pitch reading
constexpr int kWindow = 512;                    // samples compared per reading
constexpr int kMinLag = 9;                      // 4900 Hz: the highest whistle
constexpr int kMaxLag = 120;                    // 367 Hz: the lowest
constexpr int kNeed = kWindow + kMaxLag + 2;    // samples one reading looks at
constexpr int kMaxHops = 64 * kRate / kHop;     // 64 s of readings
constexpr float kHopSeconds = (float)kHop / (float)kRate;

struct Reading
{
    float pitch;   // MIDI note (fractional), 0 = none
    float level;   // RMS, 0..1
    float clarity; // 0..1: 1 = a pure tone
};

class PitchTracker
{
  public:
    // `readings`: room for kMaxHops (big: 12 bytes each, so it lives where the caller has room)
    void begin(Reading* readings, int maxReadings);
    // follow the take: `pcm` holds the whole take so far (`frames` long);
    // reads what it has not read yet
    void feed(const int16_t* pcm, uint32_t frames);
    int count() const { return n; }
    const Reading* readings() const { return out; }
    // one reading at `pos` in `pcm` (needs kNeed samples from there)
    static Reading analyse(const int16_t* pcm);

  private:
    Reading* out = nullptr;
    int maxOut = 0;
    int n = 0;
};

// ---- the notes found in a take ----
struct Found
{
    int32_t startHop;
    int32_t endHop;   // exclusive
    float pitch;      // MIDI note as whistled (median)
    float level;      // loudest RMS in the note
    bool slideIn;     // glides in from the note before
};

constexpr int kMaxFound = 160;

struct Take
{
    int count = 0;
    Found note[kMaxFound];
    float tuning = 0.0f; // the take's offset from A = 440 Hz, in semitones (-0.5..0.5)
    int key = 0;         // 0 = C .. 11 = B
    bool minor = false;
};

// findNotes' working memory (about 110 KB: the caller puts it where there is room)
struct Scratch
{
    float pitch[kMaxHops];
    float smooth[kMaxHops];
    uint8_t voiced[kMaxHops];
    uint8_t moving[kMaxHops];
};

// the readings into notes (also measures the tuning and guesses the key)
void findNotes(const Reading* r, int count, Take& take, Scratch& work);

// ---- onto the grid ----
enum Snap : uint8_t
{
    kSnapChromatic = 0, // every note as whistled (nearest semitone)
    kSnapMajor,
    kSnapMinor,
    kSnapCount
};

struct Placement
{
    float hopsPerTick;  // readings per sequencer micro step (tempo)
    int32_t anchorHop;  // the reading where tick 0 of the track is
    int patternTicks;   // the track's length in micro steps
    int gridTicks;      // starts and ends snap to this (0 = as whistled)
    int octaves;        // octaves to move by (see autoOctaves)
    uint8_t snap;       // Snap
    int key;            // 0..11
    bool slides;        // glides become slides
};

// the octave shift that brings the take's middle note near the track's base note
int autoOctaves(const Take& take, int baseNote);
// the notes for the sequencer (start order); returns how many (<= maxOut)
int toNotes(const Take& take, const Placement& pl, Note* out, int maxOut);
// `semis` (a whistled note minus tuning) onto the scale of `key`
int snapToScale(float semis, uint8_t snap, int key);

} // namespace whistle
} // namespace gibgob

#endif
