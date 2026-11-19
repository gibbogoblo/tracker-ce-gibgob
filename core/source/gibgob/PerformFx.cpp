#include "PerformFx.h"

// The reverb is the one MORPHANGUTAN uses (Plate / Dark / Shimmer).
#include "../../../synth-plugin-sdk/engines/morphangutan/morph_dsp.h"

#if __has_include("config.h")
#include "config.h" // AUDIO_CORE_RAM: the audio code runs from fast RAM on the device
#endif
#ifndef AUDIO_CORE_RAM
#define AUDIO_CORE_RAM
#endif

#include <new>
#include <stdio.h>
#include <string.h>

namespace gibgob
{
namespace perform
{

namespace
{

constexpr float kSr = 44100.0f;
constexpr uint32_t kHistFrames = 1u << 18; // 5.9 s of the mix (int16 stereo, 1 MB)
constexpr uint32_t kHistMask = kHistFrames - 1;
constexpr uint32_t kDelayFrames = 1u << 16; // 1.49 s for Delay cut
constexpr uint32_t kDelayMask = kDelayFrames - 1;
constexpr int kXfade = 96;   // samples of crossfade when the playhead jumps (2.2 ms)
constexpr int kCombSize = 2048;
// Sequencer notes reach the mix up to one audio block (128 samples) after
// their grid line. Slices start this much early so a hit keeps its attack.
constexpr float kPreRoll = 128.0f;

// ---------------------------------------------------------------- tables

const char* const kRollSpeed[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
const float kRollLen16[] = { 4.0f, 2.0f, 4.0f / 3.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f };
const char* const kFilterCut[] = { "Off", "8k", "5k", "3k", "2k", "1.2k", "800", "500" };
const float kFilterHz[] = { 0.0f, 8000.0f, 5000.0f, 3000.0f, 2000.0f, 1200.0f, 800.0f, 500.0f };
const char* const kDecay[] = { "Off", "Slow", "Mid", "Fast" };
const float kDecayLog2[] = { 0.0f, -0.1047f, -0.2345f, -0.4739f }; // log2 of 1, .93, .85, .72
const char* const kSweepShape[] = { "LP <>", "LP down", "HP <>", "HP up" };
const char* const kSweepSpeed[] = { "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
const float kSweepLen16[] = { 4, 8, 16, 32, 64 };
const char* const kCombFreq[] = { "60", "80", "100", "130", "160", "200", "260", "330",
                                  "400", "520", "660", "800", "1k", "1.3k", "1.6k", "2k" };
const float kCombHz[] = { 60, 80, 100, 130, 160, 200, 260, 330, 400, 520, 660, 800, 1000, 1300, 1600, 2000 };
const char* const kNoise[] = { "Off", "White", "Vinyl", "Hiss" };
const char* const kDelayTime[] = { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
const float kDelayLen16[] = { 1.0f, 4.0f / 3.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
const char* const kDelayType[] = { "Digital", "Tape", "Ping-pong" };
const char* const kReverbType[] = { "Plate", "Dark", "Shimmer" };
const char* const kVowels[] = { "Wah", "Yeah", "Talk", "Robot" };
const char* const kTalkSpeed[] = { "1/8", "1/4", "1/2", "1 bar", "2 bars" };
const float kTalkLen16[] = { 2, 4, 8, 16, 32 };
const char* const kVinylTime[] = { "1/4", "1/2", "1 bar", "2 bars" };
const float kVinylLen16[] = { 4, 8, 16, 32 };
const char* const kGateRate[] = { "1/8", "1/16", "1/32" };
const char* const kHalfSpeed[] = { "1/2", "2/3", "3/4" };
const float kHalfRate[] = { 0.5f, 2.0f / 3.0f, 0.75f };
const char* const kBassFreq[] = { "80", "120", "160", "250", "400" };
const float kBassHz[] = { 80.0f, 120.0f, 160.0f, 250.0f, 400.0f };
const char* const kRiserLen[] = { "1 bar", "2 bars", "4 bars" };
const float kRiserLen16[] = { 16, 32, 64 };
const char* const kScratchPattern[] = { "Baby",    "Chirp",   "Trans",   "Stab",     "Tears",   "Flare",
                                        "Flare 2", "Crab",    "Orbit",   "Scribble", "Drag",    "Hydro",
                                        "Combo 1", "Combo 2", "Combo 3", "Transf 2" };
// Scratch patterns 5-16: two beats (16 x 1/32) of hand moves. Each point is
// (time, record position) in 1/32 notes; the record moves smoothly between
// points (slowing to a stop at each, like a hand). The fader mask has one
// bit per 1/64 (bit 0 first): 0 = fader closed.
struct ScratchKey
{
    float u, r;
};
struct ScratchPattern
{
    uint8_t count;
    uint32_t fader;
    ScratchKey key[33];
};
// 12 patterns, up to 33 points
constexpr int kScratchKeysMax = 33;
const ScratchPattern kScratch[12] = {
    // Tears
    { 9, 0xFFFFFFFFu, { { 0.0f, 0.0f }, { 2.0f, 3.0f }, { 4.0f, 6.0f }, { 8.0f, 0.0f }, { 10.0f, 4.0f }, { 11.0f, 2.0f }, { 12.0f, 0.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Flare
    { 5, 0xEFEFEFEFu, { { 0.0f, 0.0f }, { 4.0f, 6.0f }, { 8.0f, 0.0f }, { 12.0f, 6.0f }, { 16.0f, 0.0f } } },
    // Flare 2
    { 5, 0xDBDBDBDBu, { { 0.0f, 0.0f }, { 4.0f, 6.0f }, { 8.0f, 0.0f }, { 12.0f, 6.0f }, { 16.0f, 0.0f } } },
    // Crab
    { 11, 0xF5FFF5F5u, { { 0.0f, 0.0f }, { 2.0f, 4.0f }, { 4.0f, 0.0f }, { 6.0f, 4.0f }, { 8.0f, 0.0f }, { 9.0f, 2.0f }, { 10.0f, 0.0f }, { 11.0f, 2.0f }, { 12.0f, 0.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Orbit
    { 9, 0x77777777u, { { 0.0f, 0.0f }, { 2.0f, 4.0f }, { 4.0f, 0.0f }, { 6.0f, 4.0f }, { 8.0f, 0.0f }, { 10.0f, 4.0f }, { 12.0f, 0.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Scribble
    { 33, 0xFFFFFFFFu, { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.45f }, { 1.5f, 1.3f }, { 2.0f, 0.75f }, { 2.5f, 1.6f }, { 3.0f, 1.05f }, { 3.5f, 1.9f }, { 4.0f, 1.35f }, { 4.5f, 2.2f }, { 5.0f, 1.65f }, { 5.5f, 2.5f }, { 6.0f, 1.95f }, { 6.5f, 2.8f }, { 7.0f, 2.25f }, { 7.5f, 3.1f }, { 8.0f, 2.55f }, { 8.5f, 3.1f }, { 9.0f, 2.25f }, { 9.5f, 2.8f }, { 10.0f, 1.95f }, { 10.5f, 2.5f }, { 11.0f, 1.65f }, { 11.5f, 2.2f }, { 12.0f, 1.35f }, { 12.5f, 1.9f }, { 13.0f, 1.05f }, { 13.5f, 1.6f }, { 14.0f, 0.75f }, { 14.5f, 1.3f }, { 15.0f, 0.45f }, { 15.5f, 1.0f }, { 16.0f, 0.0f } } },
    // Drag
    { 3, 0xFFFFFFFFu, { { 0.0f, 0.0f }, { 8.0f, 3.0f }, { 16.0f, 0.0f } } },
    // Hydro
    { 20, 0xFFFFFFFFu, { { 0.0f, 0.0f }, { 0.5f, 0.8f }, { 1.0f, 0.85f }, { 1.5f, 1.8f }, { 2.0f, 1.85f }, { 2.5f, 2.8f }, { 3.0f, 2.85f }, { 3.5f, 3.8f }, { 4.0f, 3.85f }, { 4.5f, 4.8f }, { 5.0f, 4.85f }, { 5.5f, 5.8f }, { 6.0f, 5.85f }, { 6.5f, 6.8f }, { 7.0f, 6.85f }, { 7.5f, 7.8f }, { 8.0f, 8.0f }, { 12.0f, 0.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Combo 1
    { 10, 0xBBFF0FFFu, { { 0.0f, 0.0f }, { 2.0f, 3.0f }, { 4.0f, 0.0f }, { 6.0f, 4.0f }, { 8.0f, 0.0f }, { 9.0f, 2.0f }, { 10.0f, 4.0f }, { 12.0f, 0.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Combo 2
    { 13, 0xDF55FFFFu, { { 0.0f, 0.0f }, { 4.0f, 2.0f }, { 5.0f, 0.0f }, { 6.0f, 2.0f }, { 7.0f, 0.0f }, { 8.0f, 2.0f }, { 10.0f, 0.0f }, { 11.0f, 2.0f }, { 12.0f, 0.0f }, { 13.0f, 2.0f }, { 14.0f, 0.0f }, { 15.0f, 2.0f }, { 16.0f, 0.0f } } },
    // Combo 3
    { 10, 0x5FF5FB0Fu, { { 0.0f, 0.0f }, { 2.0f, 4.0f }, { 4.0f, 0.0f }, { 6.0f, 4.0f }, { 8.0f, 0.0f }, { 10.0f, 4.0f }, { 12.0f, 0.0f }, { 13.0f, 2.0f }, { 14.0f, 4.0f }, { 16.0f, 0.0f } } },
    // Transf 2
    { 3, 0x45294529u, { { 0.0f, 0.0f }, { 8.0f, 8.0f }, { 16.0f, 0.0f } } },
};

const char* const kScratchSpeed[] = { "1/8", "1/16" };
const char* const kPumpRate[] = { "1/4", "1/8", "1/2" };
const float kPumpLen16[] = { 4, 2, 8 };
const char* const kGatePattern[] = { "Straight", "Trance 1", "Trance 2", "Trance 3",
                                     "Trance 4", "Trance 5", "Trance 6" };
// 16 slots, first slot = most significant bit
const uint16_t kGateBits[] = {
    0xFFFF, // straight
    0xB6B6, // 1011 0110 1011 0110
    0xDB6D, // 1101 1011 0110 1101
    0xEEEA, // 1110 1110 1110 1010
    0x924A, // 1001 0010 0100 1010
    0xF5F5, // 1111 0101 1111 0101
    0xABDB, // 1010 1011 1101 1011
};

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

const FxInfo kInfo[kFxCount] = {
    { "Off", "--", 0, {} },
    { "Roll", "ROLL", 3,
      { { "Speed", N(kRollSpeed), 3, kRollSpeed, 0 },
        { "Filter", N(kFilterCut), 0, kFilterCut, 0 },
        { "Decay", N(kDecay), 0, kDecay, 0 } } },
    { "Stutter", "STUT", 2,
      { { "Pattern", 16, 0, nullptr, 1 }, { "Filter", N(kFilterCut), 0, kFilterCut, 0 } } },
    { "Filter sweep", "SWEEP", 3,
      { { "Shape", N(kSweepShape), 1, kSweepShape, 0 },
        { "Speed", N(kSweepSpeed), 2, kSweepSpeed, 0 },
        { "Resonance", 11, 4, nullptr, 0 } } },
    { "Comb filter", "COMB", 2,
      { { "Frequency", N(kCombFreq), 6, kCombFreq, 0 }, { "Amount", 10, 6, nullptr, 1 } } },
    { "Cut", "CUT", 2,
      { { "Noise", N(kNoise), 0, kNoise, 0 }, { "Level", 10, 3, nullptr, 1 } } },
    { "Delay", "DELAY", 3,
      { { "Time", N(kDelayTime), 3, kDelayTime, 0 },
        { "Type", N(kDelayType), 0, kDelayType, 0 },
        { "Feedback", 10, 5, nullptr, 1 } } },
    { "Reverb", "VERB", 3,
      { { "Type", N(kReverbType), 0, kReverbType, 0 },
        { "Time", 10, 5, nullptr, 1 },
        { "Wet", 10, 5, nullptr, 1 } } },
    { "Reverse", "REV", 1, { { "Pitch", 25, 12, nullptr, -12 } } },
    { "Talkbox", "TALK", 3,
      { { "Vowels", N(kVowels), 0, kVowels, 0 },
        { "Speed", N(kTalkSpeed), 1, kTalkSpeed, 0 },
        { "Amount", 10, 7, nullptr, 1 } } },
    { "Vinyl drop", "VINYL", 1, { { "Time", N(kVinylTime), 1, kVinylTime, 0 } } },
    { "Gate", "GATE", 3,
      { { "Rate", N(kGateRate), 1, kGateRate, 0 },
        { "Pattern", N(kGatePattern), 0, kGatePattern, 0 },
        { "Depth", 10, 9, nullptr, 1 } } },
    { "Halftime", "HALF", 1, { { "Speed", N(kHalfSpeed), 0, kHalfSpeed, 0 } } },
    { "Bass kill", "BASS", 2,
      { { "Freq", N(kBassFreq), 2, kBassFreq, 0 }, { "Amount", 10, 9, nullptr, 1 } } },
    { "Riser", "RISER", 2,
      { { "Length", N(kRiserLen), 1, kRiserLen, 0 }, { "Level", 10, 5, nullptr, 1 } } },
    { "Scratch", "SCRTCH", 2,
      { { "Pattern", N(kScratchPattern), 0, kScratchPattern, 0 },
        { "Speed", N(kScratchSpeed), 0, kScratchSpeed, 0 } } },
    { "Pump", "PUMP", 2,
      { { "Rate", N(kPumpRate), 0, kPumpRate, 0 }, { "Depth", 10, 6, nullptr, 1 } } },
    { "Track cut", "TRKCUT", 0, {} },
    { "Track cut", "TRKCUT", 0, {} }, // old "Melodic cut" buttons: the same effect
};

// Stutter patterns: for each 1/16 of the bar, which 1/16 it plays
// (an earlier one of this bar, or of the bar before when larger) and an
// optional roll inside the step (2 = 1/32 repeats, 4 = 1/64 repeats).
struct StutStep
{
    int8_t src;
    uint8_t roll;
};
#define S_(x) { x, 0 }
#define R2(x) { x, 2 }
#define R4(x) { x, 4 }
const StutStep kStutter[16][16] = {
    // 1 Repeat: every other beat plays the beat before
    { S_(0), S_(1), S_(2), S_(3), S_(0), S_(1), S_(2), S_(3), S_(8), S_(9), S_(10), S_(11), S_(8), S_(9), S_(10), S_(11) },
    // 2 Threes: 3-step loops across the beat
    { S_(0), S_(1), S_(2), S_(0), S_(1), S_(2), S_(0), S_(1), S_(8), S_(9), S_(10), S_(8), S_(9), S_(10), S_(8), S_(9) },
    // 3 Skip: the second half of each beat pair repeats
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(4), S_(5), S_(8), S_(9), S_(10), S_(11), S_(12), S_(13), S_(12), S_(13) },
    // 4 Drill: 1/8 repeats, rolling out at the end
    { S_(0), S_(1), S_(0), S_(1), S_(4), S_(5), S_(4), S_(5), S_(8), S_(9), S_(8), S_(9), S_(12), R2(12), R4(12), R4(12) },
    // 5 Roll up: plays straight, rolls into the next bar
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7), S_(8), S_(9), S_(10), S_(11), S_(12), R2(12), R2(12), R4(12) },
    // 6 Chop: jungle-style re-edit
    { S_(0), S_(1), S_(2), S_(3), S_(2), S_(3), S_(6), S_(7), S_(8), S_(9), S_(8), S_(9), S_(12), S_(13), S_(14), R2(14) },
    // 7 Tumble: the beat falls over itself
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(4), S_(5), S_(4), S_(5), S_(10), S_(11), S_(8), R2(8), S_(9), R4(9) },
    // 8 Glitch: little rolls all over
    { S_(0), R4(0), S_(2), S_(3), S_(4), S_(5), R2(5), S_(7), S_(8), S_(9), S_(10), R4(10), S_(12), S_(13), R2(14), R4(15) },
    // 9 Half loop: the second half of the bar plays the first half again
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7), S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7) },
    // 10 Last beat: straight, then the last beat hammers its first 1/16
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7), S_(8), S_(9), S_(10), S_(11), S_(12), S_(12), S_(12), S_(12) },
    // 11 Three over four: a 3-step loop across the whole bar
    { S_(0), S_(1), S_(2), S_(0), S_(1), S_(2), S_(0), S_(1), S_(2), S_(0), S_(1), S_(2), S_(0), S_(1), S_(2), S_(0) },
    // 12 Amen edit: beats 1-2 come back in the second half
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7), S_(8), S_(9), S_(2), S_(3), S_(12), S_(13), S_(6), S_(7) },
    // 13 Double time: every 1/8 rolls at 1/32
    { S_(0), R2(0), S_(2), R2(2), S_(4), R2(4), S_(6), R2(6), S_(8), R2(8), S_(10), R2(10), S_(12), R2(12), S_(14), R2(14) },
    // 14 Glitch end: straight until the last beat falls apart
    { S_(0), S_(1), S_(2), S_(3), S_(4), S_(5), S_(6), S_(7), S_(8), S_(9), S_(10), S_(11), S_(12), R2(12), R4(13), R4(13) },
    // 15 Beats backwards: the beats of the bar in reverse order
    { S_(12), S_(13), S_(14), S_(15), S_(8), S_(9), S_(10), S_(11), S_(4), S_(5), S_(6), S_(7), S_(0), S_(1), S_(2), S_(3) },
    // 16 One beat: the first beat loops all bar
    { S_(0), S_(1), S_(2), S_(3), S_(0), S_(1), S_(2), S_(3), S_(0), S_(1), S_(2), S_(3), S_(0), S_(1), S_(2), S_(3) },
};

// vowel formants (Hz): A E I O U
const float kFormant[5][3] = {
    { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 },
};
const uint8_t kVowelSeq[4][5] = { { 4, 0 }, { 2, 1, 0 }, { 0, 1, 2, 3, 4 }, { 3, 2 } };
const uint8_t kVowelSeqLen[4] = { 2, 3, 5, 2 };

inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}
inline float tri01(float x) // 0 -> 1 -> 0 over x = 0..1
{
    x -= (float)(int)x;
    return x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x;
}
inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
inline bool isTimeFx(int fx)
{
    return fx == kFxRoll || fx == kFxStutter || fx == kFxReverse || fx == kFxVinyl || fx == kFxHalftime ||
           fx == kFxScratch;
}
inline bool isTrackCut(int fx)
{
    return fx == kFxDrumCut || fx == kFxMelodicCut;
}

// effects that exist once (the latest held button of that kind sets it)
enum Kind
{
    kKindGate = 0,
    kKindFilter,
    kKindComb,
    kKindTalk,
    kKindCut,
    kKindDelay,
    kKindReverb,
    kKindBass,
    kKindRiser,
    kKindPump,
    kKindCount
};
int kindOf(int fx)
{
    switch (fx)
    {
        case kFxGate: return kKindGate;
        case kFxFilter: return kKindFilter;
        case kFxComb: return kKindComb;
        case kFxTalkbox: return kKindTalk;
        case kFxCut: return kKindCut;
        case kFxDelayCut: return kKindDelay;
        case kFxReverb: return kKindReverb;
        case kFxBassKill: return kKindBass;
        case kFxRiser: return kKindRiser;
        case kFxPump: return kKindPump;
        default: return -1;
    }
}

struct StereoSvf
{
    morph::Svf f[2];
    void setup(float g, float k)
    {
        f[0].setup(g, k);
        f[1].setup(g, k);
    }
    void reset()
    {
        f[0].reset();
        f[1].reset();
    }
};

} // namespace

const FxInfo& fxInfo(int fx)
{
    return kInfo[(fx >= 0 && fx < kFxCount) ? fx : 0];
}

void paramText(int fx, int param, int value, char* buf, int bufLen)
{
    const FxInfo& in = fxInfo(fx);
    if (param < 0 || param >= in.paramCount)
    {
        snprintf(buf, bufLen, "-");
        return;
    }
    const ParamInfo& p = in.params[param];
    if (value < 0)
        value = 0;
    if (value >= p.count)
        value = p.count - 1;
    if (p.labels)
        snprintf(buf, bufLen, "%s", p.labels[value]);
    else if (p.offset < 0)
        snprintf(buf, bufLen, "%+d", value + p.offset);
    else
        snprintf(buf, bufLen, "%d", value + p.offset);
}

Cell defaultCell(int fx)
{
    Cell c;
    c.fx = (uint8_t)((fx >= 0 && fx < kFxCount) ? fx : 0);
    const FxInfo& in = fxInfo(c.fx);
    for (int i = 0; i < kMaxParams; i++)
        c.p[i] = i < in.paramCount ? in.params[i].def : 0;
    return c;
}

void factoryCells(Cell cells[kCells])
{
    cells[0] = defaultCell(kFxRoll); // 1/16
    cells[1] = defaultCell(kFxRoll);
    cells[1].p[0] = 5; // 1/32
    cells[1].p[1] = 3; // filter 3k
    cells[2] = defaultCell(kFxStutter);
    cells[3] = defaultCell(kFxFilter);
    cells[4] = defaultCell(kFxDelay);
    cells[5] = defaultCell(kFxReverb);
    cells[5].p[0] = 2; // shimmer
    cells[6] = defaultCell(kFxHalftime);
    cells[7] = defaultCell(kFxVinyl);
}

// ======================================================================
// processor state (lives at the start of the attached memory)
// ======================================================================
struct Engine::Impl
{
    int16_t* hist = nullptr;  // kHistFrames * 2
    int16_t* dly = nullptr;   // kDelayFrames * 2
    morph::Fx reverb;
    morph::Params reverbParams;

    uint32_t now = 0; // samples processed

    // beat clock: absolute 1/16 step and position inside it
    int32_t step = 0;
    float frac = 0.0f;
    uint32_t stepStart = 0; // sample where the current step began
    bool wasPlaying = false;

    uint8_t prevHeld = 0;
    uint8_t order[kCells] = { 0 }; // press order (higher = later)
    uint8_t orderCounter = 0;

    // ---- time effects (one at a time: the latest pressed) ----
    int timeCell = -1;
    Cell timeFx;
    bool waiting = false;
    int32_t anchorStep = 0;
    uint32_t anchor = 0;
    float vinylLen = 1.0f;
    float vinylSpeed = 1.0f, vinylBack = 0.0f;
    int stutPattern = 0;
    int32_t stutLastStep = -1;
    float curBack = 0.0f, lastBack = 0.0f, curDelta = 0.0f;
    uint32_t curSeg = 0;
    float curGain = 1.0f;
    float oldBack = 0.0f, oldDelta = 0.0f, oldGain = 0.0f;
    int xf = 0;
    bool readerIdle = true;
    StereoSvf timeLp;
    float timeLpMix = 0.0f;
    float timeLpHz = 0.0f;

    // ---- one-of-a-kind effects ----
    int owner[kKindCount];
    float mix[kKindCount] = { 0 };
    uint32_t pressedAt[kKindCount] = { 0 };

    // settings of the latest owner (kept while an effect fades out)
    int sweepShape = 1;
    float sweepK = 2.0f, sweepPeriod = 44100.0f;
    float combDelay = 100.0f, combFb = 0.5f;
    uint16_t gateBits = 0xFFFF;
    int gateRate = 1;
    float gateDepth = 1.0f;
    int talkSeq = 0;
    float talkPeriod = 22050.0f, talkAmount = 0.7f;
    int noiseType = 0;
    float noiseLevel = 0.0f;

    float gateEnv = 1.0f;
    StereoSvf sweep;
    float comb[2][kCombSize];
    int combPos = 0;
    float combDamp[2] = { 0, 0 };
    StereoSvf formant[3];
    int ctrlCount = 0;
    float talkGain[3] = { 1, 1, 1 };

    morph::Rng rng;
    float hissLp[2] = { 0, 0 };
    float rumble = 0.0f;
    float crackle = 0.0f;

    // delay: 0 idle, 1 held (the mix goes in), 2 held + cut (only the echoes
    // go round, the mix is muted), 3 tail (echoes ring out)
    int dlyState = 0;
    float dlySend = 0.0f;
    float dryCut = 0.0f; // delay / reverb twist: the mix is muted
    uint32_t dlyPos = 0;
    float dlySamples = 0.0f;
    float dlyLp[2] = { 0, 0 };
    float wow = 0.0f;
    float dlyQuiet = 0.0f;
    Cell dlyCell;
    float dlyWet = 0.0f;

    // bass kill, riser, pump
    StereoSvf bassHp;
    float bassAmount = 1.0f;
    StereoSvf riserBp;
    float riserLen = 44100.0f, riserLevel = 0.5f;
    int pumpLen16 = 4;
    float pumpDepth = 0.6f, pumpEnv = 1.0f;

    bool reverbTail = false;
    float revInL[128];
    float revInR[128];
    float reverbQuiet = 0.0f;

    Impl()
    {
        for (int k = 0; k < kKindCount; k++)
            owner[k] = -1;
        memset(comb, 0, sizeof(comb));
    }
};

const uint32_t Engine::kMemoryBytes = (uint32_t)((sizeof(Engine::Impl) + 63) & ~63u) + kHistFrames * 4 +
                                      kDelayFrames * 4 + (uint32_t)morph::Fx::kMemoryBytes + 64;

static_assert(((sizeof(Engine::Impl) + 63) & ~63u) + kHistFrames * 4 + kDelayFrames * 4 +
                (uint32_t)morph::Fx::kMemoryBytes + 64 <=
                kMemoryBudget,
              "perform effects need more memory than reserved");

void Engine::attach(void* memory)
{
    if (memory == nullptr)
        return;
    uint8_t* m = (uint8_t*)memory;
    impl = new (m) Impl();
    m += (sizeof(Impl) + 63) & ~63u;
    impl->hist = (int16_t*)m;
    memset(m, 0, kHistFrames * 4);
    m += kHistFrames * 4;
    impl->dly = (int16_t*)m;
    memset(m, 0, kDelayFrames * 4);
    m += kDelayFrames * 4;
    impl->reverb.attach(m);
    impl->reverb.wetOnly = true;
    impl->reverbParams.delayType = morph::kDelayOff;
    impl->reverbParams.reverbType = morph::kReverbOff;
    for (int i = 0; i < kCells; i++)
        if (cells[i].fx >= kFxCount)
            cells[i] = Cell();
    mem = (uint8_t*)memory;
}

void Engine::setHeld(uint8_t mask)
{
    // a newly held button starts from its saved settings, without twists
    const uint8_t newly = (uint8_t)(mask & ~held);
    for (int i = 0; i < kCells; i++)
        if (newly & (1u << i))
        {
            live[i] = cells[i];
            flags[i] = 0;
        }
    held = mask;
}

void Engine::twist(int i, int dir)
{
    // A twist lasts while + / Up (dir > 0) or - / Down (dir < 0) is held:
    // the held button plays one of two variants of its saved settings, and
    // dir == 0 (let go) brings the saved settings back.
    i &= 7;
    if (!((held >> i) & 1))
        return;
    Cell c = cells[i];
    const FxInfo& in = fxInfo(c.fx);
    auto clampP = [&](int k, int v)
    {
        if (k >= in.paramCount)
            return 0;
        const int hi = in.params[k].count - 1;
        return v < 0 ? 0 : (v > hi ? hi : v);
    };
    uint8_t f = 0;
    if (dir == 2 || dir == -2)
    {
        // the far twists: big + / Right (dir 2) and big - / Left (dir -2)
        const bool right = dir > 0;
        switch (c.fx)
        {
            case kFxRoll:
                if (right)
                    c.p[0] = (uint8_t)clampP(0, c.p[0] + 4); // 4x faster
                else
                {
                    // triplets: 1/8 <> 1/8T, 1/16 <> 1/16T, 1/32 <> 1/32T
                    static const uint8_t kTrip[8] = { 2, 2, 1, 4, 3, 6, 5, 6 };
                    c.p[0] = kTrip[c.p[0] & 7];
                }
                break;
            case kFxStutter: c.p[0] = (uint8_t)((c.p[0] + (right ? 1 : 15)) % 16); break; // next / previous pattern
            case kFxScratch: c.p[0] = (uint8_t)((c.p[0] + (right ? 1 : 15)) % 16); break;
            case kFxGate:
            {
                const int n = in.params[1].count;
                c.p[1] = (uint8_t)((c.p[1] + (right ? 1 : n - 1)) % n); // next / previous pattern
                break;
            }
            case kFxFilter:
                if (right)
                    c.p[2] = (uint8_t)clampP(2, c.p[2] + 4); // more resonance
                else
                    c.p[0] ^= 2; // LP <> HP
                break;
            case kFxComb: c.p[1] = (uint8_t)clampP(1, c.p[1] + (right ? 3 : -3)); break; // amount
            case kFxCut: c.p[1] = (uint8_t)clampP(1, c.p[1] + (right ? 3 : -3)); break;  // noise level
            case kFxDelay:
                if (right)
                    c.p[2] = (uint8_t)clampP(2, 9); // feedback up: the echoes hang on
                else
                {
                    // double the time
                    static const uint8_t kDouble[] = { 2, 4, 4, 5, 6, 6, 6 };
                    c.p[0] = kDouble[c.p[0] < 7 ? c.p[0] : 4];
                }
                break;
            case kFxReverb: c.p[1] = (uint8_t)clampP(1, c.p[1] + (right ? 4 : -4)); break; // longer / shorter
            case kFxReverse: c.p[0] = (uint8_t)clampP(0, c.p[0] + (right ? 7 : -7)); break; // a fifth up / down
            case kFxTalkbox:
            {
                const int n = in.params[0].count;
                c.p[0] = (uint8_t)((c.p[0] + (right ? 1 : n - 1)) % n); // next / previous vowels
                break;
            }
            case kFxVinyl: f |= right ? kTwistBrakeMask : kTwistBrakeSoft; break; // stop now / a long slow stop
            case kFxHalftime: c.p[0] = (uint8_t)(right ? in.params[0].count - 1 : 0); break; // 3/4 / 1/2
            case kFxBassKill:
                if (right)
                    c.p[0] = (uint8_t)clampP(0, 99); // everything under 400 Hz
                else
                    c.p[1] = (uint8_t)(c.p[1] / 2); // half the cut: a dip, not a kill
                break;
            case kFxRiser: c.p[1] = (uint8_t)(right ? in.params[1].count - 1 : 0); break; // loudest / quietest
            case kFxDrumCut:
            case kFxMelodicCut:
                // Right: drums and melodic tracks (everything); Left: slice tracks only
                f |= right ? (uint8_t)(kTwistCutDrums | kTwistCutMelodic) : kTwistCutSlices;
                break;
            default: break;
        }
    }
    else if (dir != 0)
    {
        const bool up = dir > 0;
        f = up ? kTwistUp : kTwistDown;
        switch (c.fx)
        {
            // rates: Up twice as fast, Down half as fast
            case kFxRoll: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 2 : -2)); break; // 1/16 <> 1/32, 1/8
            case kFxStutter: break; // the engine runs the pattern at 1/32 or 1/8 steps
            case kFxFilter: c.p[1] = (uint8_t)clampP(1, c.p[1] + (up ? -1 : 1)); break;
            case kFxComb: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 3 : -3)); break; // an octave
            case kFxCut:
                if (up)
                    c.p[0] = c.p[0] == 0 ? 1 : 0; // noise on / off
                else
                    c.p[0] = (uint8_t)(c.p[0] % 3 + 1); // the next noise
                break;
            case kFxDelay:
                if (up)
                    f |= kTwistDryCut; // only the echoes
                else
                {
                    // half the time
                    static const uint8_t kHalf[] = { 0, 0, 0, 1, 2, 3, 4 };
                    c.p[0] = kHalf[c.p[0] < 7 ? c.p[0] : 4];
                }
                break;
            case kFxReverb: f |= up ? kTwistDryCut : kTwistFreeze; break;
            case kFxReverse: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 12 : -12)); break; // an octave
            case kFxTalkbox: c.p[1] = (uint8_t)clampP(1, c.p[1] + (up ? -1 : 1)); break;
            case kFxVinyl: f |= up ? kTwistSpinUp : kTwistBrake1; break; // spin back up / brake twice as hard
            case kFxGate: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 1 : -1)); break;
            case kFxHalftime: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 1 : -1)); break;
            case kFxBassKill: c.p[0] = (uint8_t)clampP(0, c.p[0] + (up ? 2 : -2)); break; // an octave
            case kFxRiser: c.p[1] = (uint8_t)clampP(1, c.p[1] + (up ? 3 : -3)); break;
            case kFxScratch: break; // the engine scratches twice as fast / half as fast
            case kFxDrumCut:
            case kFxMelodicCut:
                // Up mutes the drum (and slice) tracks, Down the melodic tracks
                f |= up ? kTwistCutDrums : kTwistCutMelodic;
                break;
            case kFxPump: c.p[0] = (uint8_t)(up ? 1 : 2); break; // 1/8 or 1/2
            default: break;
        }
    }
    live[i] = c;
    flags[i] = f;
}

void Engine::setCell(int i, const Cell& c)
{
    Cell v = c;
    if (v.fx >= kFxCount)
        v.fx = kFxOff;
    const FxInfo& in = fxInfo(v.fx);
    for (int k = 0; k < kMaxParams; k++)
        if (k >= in.paramCount || v.p[k] >= in.params[k].count)
            v.p[k] = k < in.paramCount ? in.params[k].def : 0;
    cells[i & 7] = v;
    if (!((held >> (i & 7)) & 1))
        live[i & 7] = v;
}

// ======================================================================
// processing
// ======================================================================
namespace
{

inline float histRead(const Engine::Impl& s, int ch, float back)
{
    const uint32_t bi = (uint32_t)back;
    const float f = back - (float)bi;
    const uint32_t i0 = (s.now - bi) & kHistMask;
    const uint32_t i1 = (s.now - bi - 1) & kHistMask;
    const float a = (float)s.hist[i0 * 2 + ch];
    const float b = (float)s.hist[i1 * 2 + ch];
    return (a + (b - a) * f) * (1.0f / 32768.0f);
}

inline int16_t toI16(float x)
{
    x = clampf(x, -1.0f, 1.0f) * 32767.0f;
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

struct Target
{
    float back;
    float gain;
    uint32_t seg; // changes whenever the read position jumps
};

} // namespace

AUDIO_CORE_RAM void Engine::process(float* l, float* r, int n, const Clock& clock)
{
    if (impl == nullptr)
        return;
    Impl& s = *impl;

    const float tempo = clock.tempo > 20.0f ? clock.tempo : 120.0f;
    const float S = kSr * 60.0f / tempo / 4.0f; // samples per 1/16
    const float inc = 1.0f / S;

    // ---- follow the sequencer ----
    if (clock.playing)
    {
        const int32_t tStep = (int32_t)(clock.ticks / 48u);
        // the tick count is up to one tick old: aim for the middle of the tick
        const float tFrac = ((float)(clock.ticks % 48u) + 0.5f) / 48.0f;
        float err = (float)(tStep - s.step) + (tFrac - s.frac);
        if (!s.wasPlaying || err > 0.5f || err < -0.5f)
        {
            s.step = tStep;
            s.frac = tFrac;
            s.stepStart = s.now - (uint32_t)(tFrac * S);
        }
        else
        {
            // gentle correction that never moves the clock across a step line
            s.frac = clampf(s.frac + err * 0.02f, 0.0f, 0.999f);
        }
    }
    s.wasPlaying = clock.playing;

    // ---- buttons ----
    const uint8_t heldNow = held;
    const uint8_t pressed = (uint8_t)(heldNow & ~s.prevHeld);
    const uint8_t released = (uint8_t)(~heldNow & s.prevHeld);
    s.prevHeld = heldNow;

    auto startTime = [&](int cellIdx)
    {
        s.timeCell = cellIdx;
        s.timeFx = live[cellIdx];
        const int fx = s.timeFx.fx;
        // the grid the effect locks to (in 1/16): rolls of a beat or 1/8 wait for theirs
        int grid = 1;
        if (fx == kFxRoll)
        {
            const float len = kRollLen16[s.timeFx.p[0] < 8 ? s.timeFx.p[0] : 3];
            grid = len >= 4.0f ? 4 : (len >= 2.0f ? 2 : 1);
        }
        if (fx == kFxHalftime)
            grid = 4; // from the beat
        if (fx == kFxVinyl)
        {
            // a vinyl drop starts right away
            s.waiting = false;
            s.anchor = s.now;
            s.vinylLen = kVinylLen16[s.timeFx.p[0] < 4 ? s.timeFx.p[0] : 1] * S;
            s.vinylSpeed = 1.0f;
            s.vinylBack = 0.0f;
            return;
        }
        if (fx == kFxStutter)
        {
            // the pattern starts from its beginning on the nearest 1/16
            s.stutPattern = s.timeFx.p[0] & 15;
            s.stutLastStep = -1;
        }
        // nearest grid line: a little late still counts as on the line
        const float pos = (float)(s.step % grid) + s.frac; // position inside the grid cell
        const int32_t cellStart = s.step - (s.step % grid);
        if (pos < 0.5f * (float)grid)
        {
            s.waiting = false;
            s.anchorStep = cellStart;
            s.anchor = s.stepStart - (uint32_t)((float)(s.step - cellStart) * S);
        }
        else
        {
            s.waiting = true;
            s.anchorStep = cellStart + grid;
        }
    };

    for (int i = 0; i < kCells; i++)
    {
        if (pressed & (1 << i))
        {
            s.order[i] = ++s.orderCounter;
            const int fx = live[i].fx;
            if (isTimeFx(fx))
                startTime(i);
            const int k = kindOf(fx);
            if (k >= 0)
            {
                s.owner[k] = i;
                s.pressedAt[k] = s.now;
                if (k == kKindDelay)
                {
                    s.dlyCell = live[i];
                    if (s.dlyState == 0)
                        s.dlySamples = -1.0f; // jump straight to this button's time
                    s.dlyState = 1;
                }
                if (k == kKindRiser)
                    s.riserBp.reset();
                if (k == kKindReverb)
                    s.reverbTail = true;
            }
        }
    }
    for (int i = 0; i < kCells; i++)
    {
        if (!(released & (1 << i)))
            continue;
        if (s.timeCell == i)
        {
            // fall back to another held time effect, or back to live
            int best = -1;
            for (int j = 0; j < kCells; j++)
                if ((heldNow & (1 << j)) && isTimeFx(live[j].fx) && (best < 0 || s.order[j] > s.order[best]))
                    best = j;
            s.timeCell = -1;
            if (best >= 0)
                startTime(best);
        }
        for (int k = 0; k < kKindCount; k++)
        {
            if (s.owner[k] != i)
                continue;
            int best = -1;
            for (int j = 0; j < kCells; j++)
                if ((heldNow & (1 << j)) && kindOf(live[j].fx) == k && (best < 0 || s.order[j] > s.order[best]))
                    best = j;
            s.owner[k] = best;
            if (k == kKindDelay && best < 0)
                s.dlyState = 3; // let the echoes ring out
        }
    }
    // a cell changed while held: follow it
    if (s.timeCell >= 0 && live[s.timeCell].fx != s.timeFx.fx)
        s.timeCell = -1;
    if (s.timeCell >= 0)
        for (int k = 0; k < kMaxParams; k++)
            s.timeFx.p[k] = live[s.timeCell].p[k];

    uint8_t active = 0;
    for (int i = 0; i < kCells; i++)
        if (((heldNow >> i) & 1) && isTrackCut(live[i].fx))
            active |= (uint8_t)(1 << i);
    if (s.timeCell >= 0)
        active |= (uint8_t)(1 << s.timeCell);
    for (int k = 0; k < kKindCount; k++)
        if (s.owner[k] >= 0)
            active |= (uint8_t)(1 << s.owner[k]);
    activeMask = active;

    const Cell* own[kKindCount];
    for (int k = 0; k < kKindCount; k++)
        own[k] = s.owner[k] >= 0 ? &live[s.owner[k]] : nullptr;

    // ---- per block settings ----
    // roll / stutter filter
    float lpHz = 0.0f;
    if (s.timeCell >= 0 && (s.timeFx.fx == kFxRoll || s.timeFx.fx == kFxStutter))
        lpHz = kFilterHz[s.timeFx.p[1] < 8 ? s.timeFx.p[1] : 0];
    if (lpHz > 0.0f)
    {
        s.timeLpHz = lpHz;
        s.timeLp.setup(morph::prewarp(lpHz), 1.2f);
    }

    // settings of the one-of-a-kind effects (kept while they fade out)
    if (own[kKindFilter])
    {
        s.sweepShape = own[kKindFilter]->p[0] & 3;
        s.sweepPeriod = kSweepLen16[own[kKindFilter]->p[1] < 5 ? own[kKindFilter]->p[1] : 2] * S;
        s.sweepK = 2.0f - (float)own[kKindFilter]->p[2] * 0.17f;
    }
    if (own[kKindComb])
    {
        const float hz = kCombHz[own[kKindComb]->p[0] < 16 ? own[kKindComb]->p[0] : 6];
        s.combDelay = clampf(kSr / hz, 2.0f, (float)(kCombSize - 2));
        s.combFb = 0.45f + 0.05f * (float)(own[kKindComb]->p[1] + 1);
    }
    if (own[kKindGate])
    {
        s.gateRate = own[kKindGate]->p[0] < 3 ? own[kKindGate]->p[0] : 1;
        s.gateBits = kGateBits[own[kKindGate]->p[1] < 7 ? own[kKindGate]->p[1] : 0];
        s.gateDepth = (float)(own[kKindGate]->p[2] + 1) / 10.0f;
    }
    if (own[kKindTalk])
    {
        s.talkSeq = own[kKindTalk]->p[0] & 3;
        s.talkPeriod = kTalkLen16[own[kKindTalk]->p[1] < 5 ? own[kKindTalk]->p[1] : 1] * S;
        s.talkAmount = (float)(own[kKindTalk]->p[2] + 1) / 10.0f;
    }
    if (own[kKindCut])
    {
        s.noiseType = own[kKindCut]->p[0] & 3;
        s.noiseLevel = 0.012f * (float)(own[kKindCut]->p[1] + 1);
    }
    const int sweepShape = s.sweepShape;
    const float sweepPeriod = s.sweepPeriod, sweepK = s.sweepK;
    const uint16_t gateBits = s.gateBits;
    const int gateRate = s.gateRate;
    const float gateDepth = s.gateDepth;
    const float gateDuty = gateBits == 0xFFFF ? 0.5f : 0.75f;
    const int talkSeq = s.talkSeq;
    const float talkPeriod = s.talkPeriod, talkAmount = s.talkAmount;
    const int noiseType = s.noiseType;
    const float noiseLevel = s.noiseLevel;

    // delay: follows the held button (its time can be twisted)
    if (own[kKindDelay])
    {
        s.dlyCell = *own[kKindDelay];
        s.dlyState = (flags[s.owner[kKindDelay]] & kTwistDryCut) ? 2 : 1;
    }
    const Cell& dc = s.dlyCell;
    const float dlyTarget =
      clampf(kDelayLen16[dc.p[0] < 7 ? dc.p[0] : 3] * S, 64.0f, (float)(kDelayFrames - 64));
    if (s.dlySamples <= 0.0f)
        s.dlySamples = dlyTarget;
    // follows tempo changes slowly (tape-like), at most half a sample per sample
    const float dlyGlideMax = 0.5f;
    const int dlyType = dc.p[1] < 3 ? dc.p[1] : 0;
    const float dlyFb = 0.3f + 0.0667f * (float)(dc.p[2] < 10 ? dc.p[2] : 5);

    // reverb
    if (own[kKindReverb])
    {
        const Cell& rc = *own[kKindReverb];
        s.reverbParams.reverbType = (uint8_t)(morph::kReverbPlate + (rc.p[0] < 3 ? rc.p[0] : 0));
        s.reverbParams.reverbDecay = 0.085f * (float)(rc.p[1] + 1); // longest stays finite
        s.reverbParams.reverbMix = 0.1f * (float)(rc.p[2] + 1);
        s.reverbParams.reverbTone = 0.55f;
    }
    const uint8_t revFlags = own[kKindReverb] ? flags[s.owner[kKindReverb]] : 0;
    s.reverb.freeze = (revFlags & kTwistFreeze) != 0;
    // the delay or reverb twist that mutes the mix
    const bool dryCutOn = (own[kKindDelay] && s.dlyState == 2) || (revFlags & kTwistDryCut) != 0;

    if (own[kKindBass])
    {
        s.bassHp.setup(morph::prewarp(kBassHz[own[kKindBass]->p[0] < 5 ? own[kKindBass]->p[0] : 2]), 1.3f);
        s.bassAmount = (float)(own[kKindBass]->p[1] + 1) / 10.0f;
    }
    if (own[kKindRiser])
    {
        s.riserLen = kRiserLen16[own[kKindRiser]->p[0] < 3 ? own[kKindRiser]->p[0] : 1] * S;
        s.riserLevel = 0.03f * (float)(own[kKindRiser]->p[1] + 1);
    }
    if (own[kKindPump])
    {
        s.pumpLen16 = (int)kPumpLen16[own[kKindPump]->p[0] < 3 ? own[kKindPump]->p[0] : 0];
        s.pumpDepth = (float)(own[kKindPump]->p[1] + 1) / 10.0f;
    }

    // ================= per sample =================
    // reverb input (in the engine, not on the interrupt stack)
    float* revInL = s.revInL;
    float* revInR = s.revInR;
    const bool reverbRunning = s.reverbTail && s.reverbParams.reverbType != morph::kReverbOff;

    for (int i = 0; i < n; i++)
    {
        // clock
        s.frac += inc;
        if (s.frac >= 1.0f)
        {
            s.frac -= 1.0f;
            s.step++;
            s.stepStart = s.now;
        }

        float dryL = l[i], dryR = r[i];
        s.hist[(s.now & kHistMask) * 2] = toI16(dryL);
        s.hist[(s.now & kHistMask) * 2 + 1] = toI16(dryR);

        // ---------- time effects ----------
        Target t{ 0.0f, 1.0f, 0 };
        if (s.timeCell >= 0)
        {
            const int fx = s.timeFx.fx;
            if (s.waiting && s.step >= s.anchorStep)
            {
                s.waiting = false;
                s.anchor = s.stepStart - (uint32_t)((float)(s.step - s.anchorStep) * S);
            }
            if (!s.waiting)
            {
                const float e = (float)(s.now - s.anchor) + kPreRoll;
                if (fx == kFxRoll)
                {
                    const float R = kRollLen16[s.timeFx.p[0] < 8 ? s.timeFx.p[0] : 3] * S;
                    if (e >= R)
                    {
                        const uint32_t k = (uint32_t)(e / R);
                        t.back = (float)k * R;
                        t.seg = 100 + k;
                        const float lg = kDecayLog2[s.timeFx.p[2] < 4 ? s.timeFx.p[2] : 0];
                        t.gain = lg < 0.0f ? morph::pow2(lg * (float)(k < 200 ? k : 200)) : 1.0f;
                    }
                }
                else if (fx == kFxStutter)
                {
                    // The pattern runs from the press (its first step on the grid
                    // line nearest the press), so every press stutters from the top.
                    // Its steps are 1/16 long; held Up makes them 1/32, Down 1/8.
                    const uint8_t sf = flags[s.timeCell];
                    const float U = (sf & kTwistUp) ? 0.5f * S : ((sf & kTwistDown) ? 2.0f * S : S);
                    const uint32_t scaleId = (sf & kTwistUp) ? 1u : ((sf & kTwistDown) ? 2u : 0u);
                    const int32_t pos = (int32_t)(e / U);
                    const float into = e - (float)pos * U;
                    if (pos != s.stutLastStep)
                    {
                        s.stutLastStep = pos;
                        s.stutPattern = s.timeFx.p[0] & 15;
                    }
                    const StutStep& st = kStutter[s.stutPattern][pos & 15];
                    int d = (pos & 15) - st.src;
                    if (d < 0)
                        d += 16;
                    float back = (float)d * U;
                    uint32_t seg = ((uint32_t)d << 12) | (scaleId << 26) | (1u << 28);
                    if (st.roll)
                    {
                        const float R = U / (float)st.roll;
                        const uint32_t k = (uint32_t)(into / R);
                        back += (float)k * R;
                        seg |= (uint32_t)((pos & 0xFF) << 4) | (k + 1);
                    }
                    t.back = back;
                    t.seg = seg;
                }
                else if (fx == kFxReverse)
                {
                    const int pitch = (int)(s.timeFx.p[0] < 25 ? s.timeFx.p[0] : 12) - 12;
                    const float speed = morph::pow2((float)pitch / 12.0f);
                    t.back = (e - kPreRoll) * (1.0f + speed);
                    if (t.back < 0.0f)
                        t.back = 0.0f, t.seg = 0;
                    t.seg = 2;
                }
                else if (fx == kFxVinyl)
                {
                    // the record slows to a stop; Up spins it back up faster than
                    // normal until it has caught up with the song, Down brakes harder
                    const uint8_t vf = flags[s.timeCell];
                    const float D = s.vinylLen;
                    if (vf & kTwistSpinUp)
                    {
                        s.vinylSpeed += 2.0f / D;
                        if (s.vinylSpeed > 2.0f)
                            s.vinylSpeed = 2.0f;
                    }
                    else
                    {
                        float brake = (float)(1 << ((vf & kTwistBrakeMask) >> 4));
                        if (vf & kTwistBrakeSoft)
                            brake *= 0.5f; // a long, slow stop
                        s.vinylSpeed -= brake / D;
                        if (s.vinylSpeed < 0.0f)
                            s.vinylSpeed = 0.0f;
                    }
                    s.vinylBack += 1.0f - s.vinylSpeed;
                    if (s.vinylBack <= 0.0f)
                    {
                        // caught up: back on the song
                        s.vinylBack = 0.0f;
                        s.vinylSpeed = 1.0f;
                    }
                    t.back = s.vinylBack;
                    t.seg = s.vinylBack > 0.0f ? 3 : 0;
                    t.gain = s.vinylSpeed > 0.15f ? 1.0f : s.vinylSpeed / 0.15f;
                }
                else if (fx == kFxHalftime)
                {
                    // half (or 2/3, 3/4) speed from the beat: an octave down, the groove
                    // stretched. Every bar of source starts again from the song, so
                    // it never runs out of recording.
                    const float rate = kHalfRate[s.timeFx.p[0] < 3 ? s.timeFx.p[0] : 0];
                    const float take = 16.0f * S / rate;
                    const uint32_t k = (uint32_t)(e / take);
                    const float ee = e - (float)k * take;
                    t.back = ee * (1.0f - rate);
                    t.seg = 20 + k + ((uint32_t)s.timeFx.p[0] << 16);
                }
                else if (fx == kFxScratch && (s.timeFx.p[0] & 15) >= 4)
                {
                    // hand-made patterns over two beats of the latest beat
                    const ScratchPattern& sp = kScratch[(s.timeFx.p[0] & 15) - 4];
                    const uint8_t xf = flags[s.timeCell];
                    const float unit = 0.5f * S * (s.timeFx.p[1] ? 0.5f : 1.0f) *
                                       ((xf & kTwistUp) ? 0.5f : ((xf & kTwistDown) ? 2.0f : 1.0f));
                    const float repLen = 16.0f * unit;
                    const uint32_t k = (uint32_t)(e / repLen);
                    const float ee = e - (float)k * repLen;
                    float u = ee / unit;
                    if (u > 15.999f)
                        u = 15.999f;
                    int i = 0;
                    while (i + 2 < sp.count && sp.key[i + 1].u <= u)
                        i++;
                    const ScratchKey& k0 = sp.key[i];
                    const ScratchKey& k1 = sp.key[i + 1];
                    const float x = (u - k0.u) / (k1.u - k0.u);
                    const float ease = 0.5f - 0.5f * morph::sinTurns(x * 0.5f + 0.25f);
                    const float r = (k0.r + (k1.r - k0.r) * ease) * unit;
                    float back = 4.0f * S + ee - r;
                    t.back = back < 0.0f ? 0.0f : back;
                    t.seg = 30 + k + ((uint32_t)(s.timeFx.p[0] & 15) << 20);
                    const int bit = (int)(u * 2.0f);
                    t.gain = ((sp.fader >> (bit & 31)) & 1u) ? 1.0f : 0.0f;
                }
                else if (fx == kFxScratch)
                {
                    // the beat before is scratched back and forth; every beat takes
                    // the latest beat again
                    const int pat = s.timeFx.p[0] & 3;
                    // stroke length; held Up scratches twice as fast, Down half as fast
                    const uint8_t xf = flags[s.timeCell];
                    const float P = (s.timeFx.p[1] ? 1.0f : 2.0f) * S *
                                    ((xf & kTwistUp) ? 0.5f : ((xf & kTwistDown) ? 2.0f : 1.0f));
                    const float beat = 4.0f * S;
                    const uint32_t k = (uint32_t)(e / beat);
                    const float ee = e - (float)k * beat;
                    float x = ee / P;
                    x -= (float)(int)x;
                    // smooth stroke: out and back, peak speed about 1
                    const float D = P * 0.3183f; // P / pi
                    const float off = D * 0.5f * (1.0f - morph::sinTurns(x + 0.25f));
                    t.back = beat + ee - off;
                    t.seg = 30 + k;
                    float g = 1.0f;
                    if (pat == 1 && x >= 0.5f)
                        g = 0.0f; // chirp: only the forward stroke
                    else if (pat == 2 && (((int)(ee / (S * 0.5f))) & 1))
                        g = 0.0f; // transformer: chopped at 1/32
                    else if (pat == 3 && x >= 0.35f)
                        g = 0.0f; // stab: a short push
                    t.gain = g;
                }
            }
        }
        // past the end of the recording: silence
        if (t.back > (float)(kHistFrames - 2048))
            t.gain = 0.0f;

        float outL = dryL, outR = dryR;
        const bool live = (t.seg == 0 && t.back == 0.0f);
        if (!(live && s.readerIdle && s.xf == 0))
        {
            if (t.seg != s.curSeg)
            {
                // the playhead jumps: fade the old position out under the new one.
                // (a jump during a fade keeps fading whichever part is louder)
                if (s.xf == 0 || s.xf < kXfade / 2)
                {
                    s.oldBack = s.curBack + s.curDelta;
                    s.oldDelta = s.curDelta;
                    s.oldGain = s.curGain;
                }
                s.xf = kXfade;
                s.curSeg = t.seg;
                s.curDelta = 0.0f;
                s.lastBack = t.back;
            }
            else
            {
                s.curDelta = t.back - s.lastBack;
                s.lastBack = t.back;
            }
            s.curBack = t.back;
            s.curGain += (t.gain - s.curGain) * 0.02f;
            float a = histRead(s, 0, s.curBack) * s.curGain;
            float b = histRead(s, 1, s.curBack) * s.curGain;
            if (s.xf > 0)
            {
                s.oldBack += s.oldDelta;
                const float w = (float)s.xf / (float)kXfade;
                const float oa = s.oldBack < (float)(kHistFrames - 2) ? histRead(s, 0, s.oldBack) * s.oldGain : 0.0f;
                const float ob = s.oldBack < (float)(kHistFrames - 2) ? histRead(s, 1, s.oldBack) * s.oldGain : 0.0f;
                a = a * (1.0f - w) + oa * w;
                b = b * (1.0f - w) + ob * w;
                s.xf--;
            }
            outL = a;
            outR = b;
            s.readerIdle = live && s.xf == 0 && s.curGain > 0.999f;
            if (s.readerIdle)
                s.curGain = 1.0f;
        }

        // roll / stutter filter
        const float lpTarget = (lpHz > 0.0f) ? 1.0f : 0.0f;
        s.timeLpMix += (lpTarget - s.timeLpMix) * 0.005f;
        if (s.timeLpMix > 0.0005f)
        {
            float lo, bp, hp;
            s.timeLp.f[0].process(outL, lo, bp, hp);
            outL = lerp(outL, lo, s.timeLpMix);
            s.timeLp.f[1].process(outR, lo, bp, hp);
            outR = lerp(outR, lo, s.timeLpMix);
        }
        else if (s.timeLpMix != 0.0f)
        {
            s.timeLpMix = 0.0f;
            s.timeLp.reset();
        }

        // ---------- ramps of the one-of-a-kind effects ----------
        for (int k = 0; k < kKindCount; k++)
        {
            const float target = own[k] ? 1.0f : 0.0f;
            const float rate = (k == kKindCut || k == kKindDelay) ? 0.01f : 0.004f;
            if (s.mix[k] < target)
                s.mix[k] = s.mix[k] + rate > target ? target : s.mix[k] + rate;
            else if (s.mix[k] > target)
                s.mix[k] = s.mix[k] - rate < target ? target : s.mix[k] - rate;
        }

        // ---------- gate ----------
        if (s.mix[kKindGate] > 0.0f)
        {
            int slot;
            float ph;
            if (gateRate == 0)
            {
                slot = s.step >> 1;
                ph = ((float)(s.step & 1) + s.frac) * 0.5f;
            }
            else if (gateRate == 1)
            {
                slot = s.step;
                ph = s.frac;
            }
            else
            {
                slot = s.step * 2 + (s.frac >= 0.5f ? 1 : 0);
                ph = s.frac >= 0.5f ? s.frac * 2.0f - 1.0f : s.frac * 2.0f;
            }
            const bool on = ((gateBits >> (15 - (slot & 15))) & 1) && ph < gateDuty;
            s.gateEnv += ((on ? 1.0f : 0.0f) - s.gateEnv) * (on ? 0.05f : 0.02f);
            const float g = 1.0f - s.mix[kKindGate] * gateDepth * (1.0f - s.gateEnv);
            outL *= g;
            outR *= g;
        }

        // ---------- pump: ducks on every beat (or 1/8, 1/2) and swells back ----------
        if (s.mix[kKindPump] > 0.0f)
        {
            const int len = s.pumpLen16 > 0 ? s.pumpLen16 : 4;
            const float x = ((float)(s.step % len) + s.frac) / (float)len;
            const float dip = (1.0f - x) * (1.0f - x);
            const float target = 1.0f - s.pumpDepth * dip;
            s.pumpEnv += (target - s.pumpEnv) * 0.06f;
            const float g = 1.0f - s.mix[kKindPump] * (1.0f - s.pumpEnv);
            outL *= g;
            outR *= g;
        }

        // ---------- filter sweep ----------
        if (s.mix[kKindFilter] > 0.0f)
        {
            if ((s.ctrlCount & 15) == 0)
            {
                const float e = (float)(s.now - s.pressedAt[kKindFilter]);
                const bool oneWay = (sweepShape & 1) != 0;
                float x = oneWay ? clampf(e / sweepPeriod, 0.0f, 1.0f) : tri01(e / sweepPeriod);
                x = x * x * (3.0f - 2.0f * x); // ease in and out
                const bool hp = sweepShape >= 2;
                const float hz = hp ? 20.0f * morph::pow2(x * 7.2f) : 18000.0f * morph::pow2(-x * 6.9f);
                s.sweep.setup(morph::prewarp(hz), sweepK);
            }
            float lo, bp, hp, fl, fr;
            s.sweep.f[0].process(outL, lo, bp, hp);
            fl = sweepShape >= 2 ? hp : lo;
            s.sweep.f[1].process(outR, lo, bp, hp);
            fr = sweepShape >= 2 ? hp : lo;
            outL = lerp(outL, fl, s.mix[kKindFilter]);
            outR = lerp(outR, fr, s.mix[kKindFilter]);
            if (s.mix[kKindFilter] <= 0.0f)
                s.sweep.reset();
        }

        // ---------- comb ----------
        if (s.mix[kKindComb] > 0.0f)
        {
            float rp = (float)s.combPos - s.combDelay;
            if (rp < 0.0f)
                rp += (float)kCombSize;
            const int i0 = (int)rp;
            const float f = rp - (float)i0;
            const int i1 = (i0 + 1) & (kCombSize - 1);
            float y[2];
            const float in[2] = { outL, outR };
            for (int c = 0; c < 2; c++)
            {
                const float d = s.comb[c][i0] + (s.comb[c][i1] - s.comb[c][i0]) * f;
                s.combDamp[c] += (d - s.combDamp[c]) * 0.6f;
                y[c] = in[c] + s.combFb * s.combDamp[c];
                s.comb[c][s.combPos] = morph::softClip(y[c]);
                y[c] *= (1.0f - 0.6f * s.combFb);
            }
            s.combPos = (s.combPos + 1) & (kCombSize - 1);
            outL = lerp(outL, y[0], s.mix[kKindComb]);
            outR = lerp(outR, y[1], s.mix[kKindComb]);
        }

        // ---------- bass kill ----------
        if (s.mix[kKindBass] > 0.0f)
        {
            float lo, bp, hp;
            s.bassHp.f[0].process(outL, lo, bp, hp);
            outL = lerp(outL, hp, s.mix[kKindBass] * s.bassAmount);
            s.bassHp.f[1].process(outR, lo, bp, hp);
            outR = lerp(outR, hp, s.mix[kKindBass] * s.bassAmount);
        }
        else
        {
            s.bassHp.reset();
        }

        // ---------- talkbox ----------
        if (s.mix[kKindTalk] > 0.0f)
        {
            if ((s.ctrlCount & 15) == 0)
            {
                const float e = (float)(s.now - s.pressedAt[kKindTalk]);
                const int len = kVowelSeqLen[talkSeq];
                const float x = tri01(e / talkPeriod) * (float)(len - 1);
                const int v0 = (int)x;
                const int v1 = v0 + 1 < len ? v0 + 1 : v0;
                const float fr = x - (float)v0;
                const uint8_t* seq = kVowelSeq[talkSeq];
                for (int f = 0; f < 3; f++)
                {
                    const float hz = lerp(kFormant[seq[v0]][f], kFormant[seq[v1]][f], fr);
                    s.formant[f].setup(morph::prewarp(hz), 0.12f);
                }
            }
            float sumL = 0.0f, sumR = 0.0f;
            const float fg[3] = { 1.0f, 0.75f, 0.45f };
            for (int f = 0; f < 3; f++)
            {
                float lo, bp, hp;
                s.formant[f].f[0].process(outL, lo, bp, hp);
                sumL += bp * 0.12f * fg[f];
                s.formant[f].f[1].process(outR, lo, bp, hp);
                sumR += bp * 0.12f * fg[f];
            }
            const float w = s.mix[kKindTalk] * talkAmount;
            outL = lerp(outL, morph::softClip(sumL * 9.0f), w);
            outR = lerp(outR, morph::softClip(sumR * 9.0f), w);
        }
        s.ctrlCount++;

        // ---------- cut (mutes the mix, tails of delay / reverb carry on) ----------
        float noiseL = 0.0f, noiseR = 0.0f;
        if (s.mix[kKindCut] > 0.0f)
        {
            const float g = 1.0f - s.mix[kKindCut];
            outL *= g;
            outR *= g;
            if (noiseType != 0)
            {
                const float w0 = s.rng.bipolar(), w1 = s.rng.bipolar();
                if (noiseType == 1)
                {
                    noiseL = w0;
                    noiseR = w1;
                }
                else if (noiseType == 3)
                {
                    s.hissLp[0] += (w0 - s.hissLp[0]) * 0.35f;
                    s.hissLp[1] += (w1 - s.hissLp[1]) * 0.35f;
                    noiseL = s.hissLp[0] * 1.6f;
                    noiseR = s.hissLp[1] * 1.6f;
                }
                else
                {
                    // vinyl: rumble, soft hiss and crackles
                    s.rumble += (w0 - s.rumble) * 0.004f;
                    if (s.rng.unipolar() < 0.0007f)
                        s.crackle = s.rng.bipolar() * 6.0f;
                    s.crackle *= 0.86f;
                    s.hissLp[0] += (w1 - s.hissLp[0]) * 0.2f;
                    noiseL = s.rumble * 5.0f + s.crackle + s.hissLp[0] * 0.35f;
                    noiseR = s.rumble * 5.0f + s.crackle * 0.8f + s.hissLp[0] * 0.35f;
                }
                noiseL *= noiseLevel * s.mix[kKindCut];
                noiseR *= noiseLevel * s.mix[kKindCut];
            }
        }

        // ---------- riser: noise sweeping up over 1/2/4 bars ----------
        if (s.mix[kKindRiser] > 0.0f)
        {
            const float x = clampf((float)(s.now - s.pressedAt[kKindRiser]) / s.riserLen, 0.0f, 1.0f);
            if ((s.ctrlCount & 15) == 1)
                s.riserBp.setup(morph::prewarp(200.0f * morph::pow2(x * 5.3f)), 0.7f);
            float lo, bp, hp;
            const float g = s.riserLevel * (0.15f + 0.85f * x * x) * s.mix[kKindRiser] * 0.7f;
            s.riserBp.f[0].process(s.rng.bipolar(), lo, bp, hp);
            noiseL += bp * g;
            s.riserBp.f[1].process(s.rng.bipolar(), lo, bp, hp);
            noiseR += bp * g;
        }

        // the delay / reverb twist mutes the mix (reverb and delay still hear it)
        s.dryCut += ((dryCutOn ? 1.0f : 0.0f) - s.dryCut) * 0.01f;
        const float sendL = outL, sendR = outR;

        // ---------- delay ----------
        float dlyOutL = 0.0f, dlyOutR = 0.0f;
        {
            s.dlySamples += clampf((dlyTarget - s.dlySamples) * 0.0005f, -dlyGlideMax, dlyGlideMax);
            float d = s.dlySamples;
            if (dlyType == 1)
            {
                s.wow += 0.5f / kSr;
                if (s.wow >= 1.0f)
                    s.wow -= 1.0f;
                d += 14.0f * morph::sinTurns(s.wow);
            }
            const uint32_t di = (uint32_t)d;
            const float df = d - (float)di;
            const uint32_t a0 = (s.dlyPos - di) & kDelayMask, a1 = (s.dlyPos - di - 1) & kDelayMask;
            float wl = lerp((float)s.dly[a0 * 2], (float)s.dly[a1 * 2], df) * (1.0f / 32768.0f);
            float wr = lerp((float)s.dly[a0 * 2 + 1], (float)s.dly[a1 * 2 + 1], df) * (1.0f / 32768.0f);
            if (dlyType == 1)
            {
                s.dlyLp[0] += (wl - s.dlyLp[0]) * 0.35f;
                s.dlyLp[1] += (wr - s.dlyLp[1]) * 0.35f;
                wl = s.dlyLp[0];
                wr = s.dlyLp[1];
            }
            // held: the mix goes in and the echoes come out. Held + cut: only
            // the echoes go round (the mix just before keeps repeating). Let go:
            // the echoes ring out. Sends and outputs fade, so nothing clicks.
            s.dlySend += ((s.dlyState == 1 ? 1.0f : 0.0f) - s.dlySend) * 0.02f;
            s.dlyWet += ((s.dlyState != 0 ? 1.0f : 0.0f) - s.dlyWet) * 0.02f;
            float recL, recR, echoL, echoR;
            if (dlyType == 2)
            {
                recL = (outL + outR) * 0.5f;
                recR = 0.0f;
                echoL = wr * dlyFb;
                echoR = wl * dlyFb;
            }
            else
            {
                recL = outL;
                recR = outR;
                echoL = wl * dlyFb;
                echoR = wr * dlyFb;
            }
            if (dlyType == 1)
            {
                echoL = morph::softClip(echoL * 1.2f) * 0.85f;
                echoR = morph::softClip(echoR * 1.2f) * 0.85f;
            }
            const float inL = recL * s.dlySend + echoL;
            const float inR = recR * s.dlySend + echoR;
            if (s.dlyState != 0 || s.dlyWet > 0.0005f)
            {
                dlyOutL = wl * s.dlyWet;
                dlyOutR = wr * s.dlyWet;
                if (s.dlyState == 3)
                {
                    const float lvl = (wl < 0 ? -wl : wl) + (wr < 0 ? -wr : wr);
                    s.dlyQuiet = lvl < 0.0005f ? s.dlyQuiet + 1.0f : 0.0f;
                    if (s.dlyQuiet > kSr * 0.3f)
                    {
                        s.dlyState = 0;
                        s.dlyQuiet = 0.0f;
                    }
                }
            }
            s.dly[(s.dlyPos & kDelayMask) * 2] = toI16(inL);
            s.dly[(s.dlyPos & kDelayMask) * 2 + 1] = toI16(inR);
            s.dlyPos++;
        }

        // reverb input: the mix while held (also while the mix itself is cut)
        revInL[i] = own[kKindReverb] ? sendL * s.mix[kKindReverb] : 0.0f;
        revInR[i] = own[kKindReverb] ? sendR * s.mix[kKindReverb] : 0.0f;

        outL *= 1.0f - s.dryCut;
        outR *= 1.0f - s.dryCut;

        l[i] = outL + noiseL + dlyOutL;
        r[i] = outR + noiseR + dlyOutR;
        s.now++;
    }

    // ---------- reverb (block) ----------
    if (reverbRunning && s.reverb.ready())
    {
        static const float zeroMod[4] = { 0, 0, 0, 0 };
        s.reverb.process(s.reverbParams, tempo, zeroMod, revInL, revInR, n);
        float lvl = 0.0f;
        for (int i = 0; i < n; i++)
        {
            l[i] += revInL[i];
            r[i] += revInR[i];
            lvl += (revInL[i] < 0 ? -revInL[i] : revInL[i]);
        }
        if (!own[kKindReverb])
        {
            s.reverbQuiet = lvl < 0.0002f * (float)n ? s.reverbQuiet + (float)n : 0.0f;
            if (s.reverbQuiet > kSr * 0.5f)
            {
                s.reverbTail = false;
                s.reverbQuiet = 0.0f;
                s.reverbParams.reverbType = morph::kReverbOff;
                s.reverb.process(s.reverbParams, tempo, zeroMod, revInL, revInR, 0); // clears it
            }
        }
    }
}

} // namespace perform
} // namespace gibgob
