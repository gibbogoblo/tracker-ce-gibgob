#ifndef GIBGOB_PERFORMFX_H_
#define GIBGOB_PERFORMFX_H_

// GibGob OS performance effects.
//
// One stereo processor on the master mix (every track, synth and send), held
// on the top-row buttons. Eight "cells" (one per button) each hold an effect
// and up to three settings, so the same effect can sit on several buttons.
//
// Everything is locked to a beat clock that follows the sequencer, so rolls,
// stutters and gates land on the grid. Effects that move the playhead read
// from a recording of the last few seconds of the mix (the sequencer itself
// never moves), so patterns, track lengths and song position never change:
// let go and you are exactly where you would have been.
//
// The DSP has no hardware dependencies so it also runs in the host tests.

#include <stdint.h>

namespace gibgob
{
namespace perform
{

static constexpr int kCells = 8;
// memory the device reserves for the engine (checked at compile time)
static constexpr uint32_t kMemoryBudget = 1792u * 1024u;
static constexpr int kMaxParams = 3;

enum Fx : uint8_t
{
    kFxOff = 0,
    kFxRoll,
    kFxStutter,
    kFxFilter,
    kFxComb,
    kFxCut,
    kFxDelayCut,
    kFxReverb,
    kFxReverse,
    kFxTalkbox,
    kFxVinyl,
    kFxGate,
    // CHIMP OUT additions (new ids only at the end: saved buttons keep theirs)
    kFxHalftime,
    kFxBassKill,
    kFxRiser,
    kFxScratch,
    kFxPump,
    kFxDrumCut,    // mutes the drum tracks (handled outside the engine)
    kFxMelodicCut, // mutes the melodic tracks (handled outside the engine)
    kFxCount
};
static constexpr int kFxDelay = kFxDelayCut; // the delay (was "Delay cut")

struct Cell
{
    uint8_t fx = kFxOff;
    uint8_t p[kMaxParams] = { 0, 0, 0 };
};

// ---- effect / setting descriptions (for the perform page) ----
struct ParamInfo
{
    const char* name;
    uint8_t count;              // number of values (0..count-1)
    uint8_t def;                // default value
    const char* const* labels;  // value names, or nullptr: shows value + offset
    int8_t offset;              // for numbers: shown value = value + offset
};

struct FxInfo
{
    const char* name;
    const char* shortName; // fits a grid cell
    uint8_t paramCount;
    ParamInfo params[kMaxParams];
};

const FxInfo& fxInfo(int fx);
// text of a setting value ("1/16", "+3", "Plate" ...), buf >= 16 chars
void paramText(int fx, int param, int value, char* buf, int bufLen);
// a cell with the default settings of `fx`
Cell defaultCell(int fx);
// the factory layout of the eight buttons
void factoryCells(Cell cells[kCells]);

// ---- the processor ----
// twist switches of a held button (Engine::flags)
static constexpr uint8_t kTwistDryCut = 0x01;    // delay / reverb: mute the mix
static constexpr uint8_t kTwistFreeze = 0x02;    // reverb: hold the tail
static constexpr uint8_t kTwistSpinUp = 0x04;    // vinyl: back up to speed
static constexpr uint8_t kTwistBrake1 = 0x10;    // vinyl: brake harder (x2 per step)
static constexpr uint8_t kTwistBrakeMask = 0x30;

struct Clock
{
    bool playing = false;
    uint32_t ticks = 0; // sequencer micro steps since play started (48 per 1/16)
    float tempo = 120.0f;
};

class Engine
{
  public:
    // memory for the buffers (kMemoryBytes, any alignment of 8). Without it
    // the engine passes audio through untouched.
    static const uint32_t kMemoryBytes;
    void attach(void* memory);
    bool ready() const { return mem != nullptr; }

    // UI side (any thread): which cells are held and what they hold.
    void setCell(int i, const Cell& c);
    Cell cell(int i) const { return cells[i & 7]; }
    Cell liveCell(int i) const { return live[i & 7]; }
    // holding: a held button plays from a copy of its settings, which the
    // twists change; letting go forgets the twists
    void setHeld(uint8_t mask);
    // + / Up (dir > 0) or - / Down (dir < 0) while button i is held
    void twist(int i, int dir);
    uint8_t heldMask() const { return held; }
    void releaseAll() { held = 0; }

    // audio side: processes n samples (n <= 128) of the stereo mix in place
    void process(float* l, float* r, int n, const Clock& clock);

    // for the screen: is the effect on the cell sounding right now
    bool cellActive(int i) const { return (activeMask >> (i & 7)) & 1; }

    struct Impl;

  private:
    uint8_t* mem = nullptr;
    Impl* impl = nullptr;
    Cell cells[kCells];
    Cell live[kCells];          // what a held button plays (settings + twists)
    volatile uint8_t flags[kCells] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // twist switches
    volatile uint8_t held = 0;
    volatile uint8_t activeMask = 0;
};

} // namespace perform
} // namespace gibgob

#endif
