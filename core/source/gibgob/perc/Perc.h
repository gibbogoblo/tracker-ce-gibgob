// SPDX-License-Identifier: GPL-3.0-only
#ifndef GIBGOB_PERC_H_
#define GIBGOB_PERC_H_

// GibGob OS PERC: a drum synth for the drum tracks, built on the LXR-02's sound engine (Erica
// Synths' LXR-02 firmware, GPL-3.0-only, after Julian Schmidt's Sonic Potions LXR; core/libs/lxr).
//
// Every PERC track has one voice of one of the LXR's four kinds: Drum (FM oscillators with a pitch
// envelope: kicks, toms, cowbells, zaps), Snare (oscillator + noise), Cymbal and Hihat (3-operator FM,
// the hat open or closed). A voice's sound is a list of 0..127 values (Param), set the same way the
// LXR's MIDI parser sets them, so the LXR-02's kits sound as they do there. On top of the LXR's one
// LFO there is a second one; both and the velocity can move any of the voice's own values.
//
// Host-testable: no hardware in here (gibgob-tests/perc_test.cpp).

#include <stdint.h>

#include "lxr/perc_voices.h"

namespace gibgob
{
namespace perc
{

enum VoiceType : uint8_t
{
    kDrum = 0,
    kSnare,
    kCymbal,
    kHihat,
    kVoiceTypeCount
};
extern const char* const kVoiceTypeNames[kVoiceTypeCount];

// A sound's values (0..127 each, the LXR's meaning). Some belong to only some kinds (marked).
enum Param : uint8_t
{
    pWave = 0, // oscillator waveform: sine, tri, saw, square, noise, pwm
    pCoarse,   // tune (semitones, 0..127)
    pFine,     // fine tune (64 = none)
    pPwm,      // the pwm waveform's width
    pAttack,   // amp envelope
    pDecay,    // amp envelope (the hat: closed)
    pDecayOpen, // the hat: its open decay
    pSlope,    // amp envelope curve
    pRepeat,   // snare, cymbal: the attack repeats (claps)
    pPitchDecay,  // drum, snare: the pitch envelope's decay
    pPitchSlope,  // drum, snare: its curve
    pPitchAmount, // drum, snare: how far it bends
    pFmAmount, // drum: FM depth (or the mix, see pMixMode)
    pFmFreq,   // drum: the modulator's tune
    pModWave,  // drum: the modulator's waveform
    pMixMode,  // drum: 0 = FM, 1 = the two oscillators mixed
    pNoiseFreq, // snare: the noise's rate
    pNoiseMix,  // snare: noise against the oscillator
    pWave2,    // cymbal, hat: modulator 1's waveform
    pWave3,    // cymbal, hat: modulator 2's waveform
    pModF1,    // cymbal, hat: modulator 1's tune
    pModF2,    // cymbal, hat: modulator 2's tune
    pModGain1, // cymbal, hat: modulator 1's depth
    pModGain2, // cymbal, hat: modulator 2's depth
    pTransWave, // transient (click): which
    pTransVol,  // transient: how loud
    pTransFreq, // transient: its pitch
    pFilterFreq,
    pFilterReso,
    pFilterType,  // LP, HP, BP, notch, peak, LP2 (the LXR's)
    pFilterDrive,
    pVolume,
    pPan,         // 64 = centre
    pSampleRate,  // the LXR's per-voice sample-rate reducer (127 = full rate)
    pDrive,       // waveshaper
    pVelAmount,   // velocity modulation: how much
    pVelDest,     // velocity modulation: which value (0 = none, else Param + 1)
    pVelVolume,   // velocity sets the level (0 / 1)
    pLfoFreq,
    pLfoSync,     // 0 = free, 1..11: 4/1 .. 1/32 of a bar
    pLfoAmount,
    pLfoWave,     // sine, tri, saw up, saw down, square, random, exp up, exp down
    pLfoRetrig,   // 0 = free running, 1 = restarts on each hit
    pLfoOffset,   // where it restarts
    pLfoDest,     // 0 = none, else Param + 1
    pLfo2Freq,    // the second LFO (GibGob's): the same
    pLfo2Sync,
    pLfo2Amount,
    pLfo2Wave,
    pLfo2Retrig,
    pLfo2Offset,
    pLfo2Dest,
    kParamCount
};

struct ParamInfo
{
    const char* name;
    uint8_t max;    // highest value
    uint8_t types;  // bit per VoiceType that uses it
};
const ParamInfo& paramInfo(int p);
bool paramUsed(int p, int type);
// a value the velocity and the LFOs can move (a destination: param + 1)
bool destAllowed(int param);
// a value as the screen shows it ("Sine", "LP", "C", "+5", "1/16")
void paramText(int p, int value, char* out, int outLen);

// A track's sound. Stored in the project (8 of them).
struct Sound
{
    uint8_t type;    // VoiceType
    uint8_t hatOpen; // the hat plays its open decay
    uint8_t value[kParamCount];
    char name[12];   // where it came from ("Badonka D1"), or the user's
};
void initSound(Sound& s, int type); // a plain sound of that kind
// PERC's first four tracks: kick, snare, closed hat, cowbell
static constexpr int kDefaultTracks = 4;
void defaultSound(int track, Sound& s);

// ---- the LXR-02 kits (PercKits.h) ----
enum KitSlot : uint8_t
{
    kSlotD1 = 0,
    kSlotD2,
    kSlotD3,
    kSlotSnare,
    kSlotCymbal,
    kSlotClosedHat,
    kSlotOpenHat,
    kKitSlotCount
};
extern const char* const kKitSlotNames[kKitSlotCount]; // "D1", "D2", "D3", "SN", "CY", "CH", "OH"
int kitCount();
const char* kitName(int kit);
bool kitIsFactory(int kit);
// one of a kit's voices as a GibGob sound
void soundFromKit(int kit, int slot, Sound& out);
// the same from an LXR-02 sound file's 247 values (after its 8 name bytes)
void soundFromKitData(const uint8_t* lxrValues, int slot, Sound& out);
int slotType(int slot);

// ---- parameter locks: a drum step can set two of these for its hit (the drum overview, Shift + FX1/2) ----
// A lock holds from its hit until the next one; a hit without it plays the sound as it is.
enum LockKind : uint8_t
{
    kLockOff = 0,
    kLockDecay,     // the amp envelope's decay (the LXR's has no release: the decay is the tail)
    kLockAttack,
    kLockTune,
    kLockCutoff,
    kLockReso,
    kLockDrive,
    kLockClick,     // the transient's level
    kLockClickTune,
    kLockPitchAmt,  // drum, snare: how far the pitch envelope bends
    kLockPitchDecay,
    kLockTone,      // drum: FM amount; snare: noise mix; cymbal, hat: modulator depth
    kLockVolume,
    kLockPan,
    kLockCrush,     // the sample-rate reducer
    kLockVariant,   // hat: open / closed; snare, cymbal: repeat (claps)
    kLockKinds
};
static_assert(kLockKinds <= 16, "a lock kind fits in 4 bits");
static constexpr int kLockHatOpen = -2; // lockParam: the hat's open / closed
int lockParam(int kind, int type);     // the Param it sets (-1: none for this voice type)
bool lockUsed(int kind, int type);
int lockMax(int kind, int type);
int lockValueOf(int kind, const Sound& s); // where the sound has it (a new lock starts there)
const char* lockName(int kind, int type);  // "Decay", "FM Amt", "Open"...
const char* lockShort(int kind, int type); // up to 4 letters, under a step
void lockText(int kind, int type, int value, char* out, int outLen);

// ---- the voices ----
struct VoiceState
{
    union
    {
        PercDrum drum;
        PercSnare snare;
        PercMetal metal; // cymbal, hat
    } dsp;
    PercLfo lfo[2];
    Sound sound;
    uint64_t modded;   // the values the modulators moved last block (bit per Param)
    float velocity;    // the last hit's, 0..1
    float decimRate;   // pSampleRate as a phase step
    float decimCount;
    int16_t decimHold;
    uint8_t live;      // rendering (cleared once the voice has gone quiet)
    float level;       // the last block's peak, 0..1
    float gain;        // the track's level and mute (0 = rendering on silently)
    uint64_t lockMask;            // the values the last hit locked (bit per Param)
    uint8_t lockVal[kParamCount]; // their locked values
};

class Engine
{
  public:
    static constexpr int kVoices = 8;
    void init();
    void setSound(int v, const Sound& s);      // a whole new sound (stops the voice)
    const Sound& sound(int v) const { return m_voice[v].sound; }
    void setValue(int v, int param, int value); // one value, live
    void setHatOpen(int v, bool open);
    // note: 60 = the sound's own pitch; locks: kind, value, kind, value (LockKind, 0..127) or nullptr
    void trigger(int v, uint8_t velocity, int note, const uint8_t* locks = nullptr);
    void stop(int v);
    // adds the voices into the stereo block (n: a multiple of 64)
    void render(float* left, float* right, int n);
    void setTempo(float bpm);
    void setGain(int v, float gain) { if (v >= 0 && v < kVoices) m_voice[v].gain = gain; }
    bool sounding(int v) const { return m_voice[v].live != 0; }
    float level(int v) const { return m_voice[v].level; } // 0..1 (the screen)

  private:
    void renderVoice(VoiceState& s, int16_t* buf);
    VoiceState m_voice[kVoices];
};

} // namespace perc
} // namespace gibgob

#endif
