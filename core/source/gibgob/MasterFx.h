#ifndef GIBGOB_MASTERFX_H_
#define GIBGOB_MASTERFX_H_

// GibGob OS master effects: after CHIMP OUT, before the stock limiter.
//
//   tracks --(duck: every track but the kick, from the kick's notes)--> mix
//   mix -> CHIMP OUT -> colour slot 1 -> colour slot 2 -> glue -> limiter
//
// Colour effects: Cassette, Vinyl, Lo-fi, Width. Settings are per song, and a
// pattern can have its own. The DSP has no hardware dependencies, so it also
// runs in the host tests.

#include <stdint.h>

namespace gibgob
{
namespace master
{

enum Colour : uint8_t
{
    kColourOff = 0,
    kColourCassette,
    kColourVinyl,
    kColourLofi,
    kColourWidth,
    kColourCount
};

enum GlueStyle : uint8_t
{
    kGlueHipHop = 0, // slow attack, long release, 4:1: the hits punch through, the loop thickens
    kGlueGarage,     // medium attack, fast release, 2:1: light, keeps the swing bouncing
    kGlueJungle,     // fast attack, very fast release, 6:1: slammed breaks
    kGlueClean,      // all-round, 2:1
    kGlueStyleCount
};
struct GlueStyleInfo
{
    const char* name;
    float attackMs, releaseMs, ratio, detectorHpHz;
};
const GlueStyleInfo& glueStyleInfo(int style);

static constexpr int kSlots = 2;
static constexpr int kSlotParams = 4;

struct Slot
{
    uint8_t type = kColourOff;
    uint8_t p[kSlotParams] = { 0, 0, 0, 0 }; // 0..15
};

struct Settings
{
    // duck (sidechain from the kick track's notes)
    bool duckOn = false;
    uint8_t duckTrack = 0;   // 0..7
    uint8_t duckDepth = 6;   // 0..10
    uint8_t duckRelease = 1; // 0 1/16, 1 1/8, 2 1/4, 3 1/2
    uint8_t duckShape = 0;   // 0 smooth, 1 hard
    bool duckNoise = false;  // the colour effects' noise pumps too
    // glue compressor
    uint8_t glue = 0;        // 0 off .. 10
    uint8_t glueStyle = 0;   // GlueStyle: the preset (attack, release, ratio, detector high-pass)
    Slot slot[kSlots];

    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// 7 bytes, so settings fit spare bytes of the project and of each pattern
static constexpr int kPackedBytes = 7;
void pack(const Settings& s, uint8_t out[kPackedBytes]);
Settings unpack(const uint8_t in[kPackedBytes]);

// ---- descriptions for the page ----
struct ParamInfo
{
    const char* name;
    uint8_t count;              // values 0..count-1
    const char* const* labels;  // nullptr: shows the number
};
struct ColourInfo
{
    const char* name;
    uint8_t paramCount;
    ParamInfo params[kSlotParams];
    uint8_t defaults[kSlotParams];
};
const ColourInfo& colourInfo(int type);
Slot defaultSlot(int type);
// duck release in 1/16 steps
float duckReleaseSteps(int release);

// ---- the processor ----
class Engine
{
  public:
    // memory for the delay lines (kMemoryBytes). Without it the colour
    // effects that need it (wobble, wow) leave the pitch alone.
    static const uint32_t kMemoryBytes;
    void attach(void* memory);

    // UI side: the settings in use (copied over at the next block)
    void setSettings(const Settings& s);
    const Settings& settings() const { return pending; }

    // sequencer side: a note started on `track` (interrupt context)
    void noteStarted(int track);

    // audio side: once per block, before processing: advances the duck and
    // returns the gain for the tracks it ducks (1 = no duck)
    float duckBlock(int n, float tempo);
    float duckGain() const { return duckNow; }
    // processes n samples (n <= 128) of the stereo mix in place
    void process(float* l, float* r, int n);

    // for the page: gain reduction of the glue (dB, >= 0)
    float glueReduction() const { return glueGrDb; }

    struct Impl;

  private:
    Settings pending;
    Settings live;
    volatile bool changed = false;
    volatile uint32_t duckTriggers = 0;
    uint32_t duckSeen = 0;
    float duckPos = 1e9f; // samples since the kick
    float duckNow = 1.0f;
    float glueGrDb = 0.0f;
    Impl* impl = nullptr;
};

} // namespace master
} // namespace gibgob

#endif
