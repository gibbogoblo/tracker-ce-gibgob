// MORPHANGUTAN - GibGob OS synth engine, DSP core.
//
// Plain C++ with no Polyend SDK types, so the exact same code runs inside the
// .psyn plugin on the Tracker and in the host preview renderer (WAV files).
//
// Voice:  MAMA osc + BABY osc (morph sine > triangle > saw > square),
//         PAPA sub osc (morph, -2..0 octaves), noise, drift
//         -> filter (LP12 / LP24 / BP / HP / Comb+ / Comb-) with drive + key track
//         -> amp ADSR -> pan (stereo)
//         2 LFOs + 2 envelopes, each with 2 modulation slots (destination + amount)
// FX (once per instrument, on the mix of all voices):
//         delay (Digital / Tape / Ping-pong) -> reverb (Plate / Dark / Shimmer)
//
// MIT License, part of GibGob OS.

#pragma once

#include <stdint.h>
#include <string.h>

namespace morph
{

static constexpr float kSampleRate = 44100.0f;
static constexpr float kInvSampleRate = 1.0f / kSampleRate;
static constexpr int kControlRate = 16; // modulation is computed every 16 samples

// ------------------------------------------------------------------ math

// true for a real number (not NaN, not infinite). Works under -ffast-math,
// where x != x is optimised away.
inline bool finiteBits(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof(u));
    return (u & 0x7F800000u) != 0x7F800000u;
}

inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}
inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}
inline float floorf_(float x)
{
    const int i = (int)x;
    return (float)(x < (float)i ? i - 1 : i);
}

// sin(2*pi*phase), phase in turns (any value)
inline float sinTurns(float phase)
{
    float t = phase - floorf_(phase + 0.5f);
    if (t > 0.25f)
    {
        t = 0.5f - t;
    }
    else if (t < -0.25f)
    {
        t = -0.5f - t;
    }
    const float t2 = t * t;
    return t * (6.283164f + t2 * (-41.337143f + t2 * (81.340782f + t2 * -70.993568f)));
}

inline float pow2(float x)
{
    x = clampf(x, -126.0f, 127.0f);
    const float whole = floorf_(x);
    const float f = x - whole;
    const float mantissa =
      1.0f + f * (0.69315249f +
                  f * (0.24015281f + f * (0.05583591f + f * (0.0089734f + f * 0.00188529f))));
    union
    {
        uint32_t bits;
        float value;
    } scale;
    scale.bits = (uint32_t)((int32_t)whole + 127) << 23;
    return mantissa * scale.value;
}

// tan(pi * f / sr) for the filters, f/sr limited to 0.49
inline float prewarp(float hz)
{
    const float x = clampf(hz * kInvSampleRate, 0.00005f, 0.45f); // turns of half circle
    // tan(pi*x) = sin(pi*x) / cos(pi*x)
    const float s = sinTurns(x * 0.5f);
    const float c = sinTurns(x * 0.5f + 0.25f);
    return s / c;
}

// smooth saturation, ~linear below 0.5
inline float softClip(float x)
{
    x = clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float polyBlep(float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

struct Rng
{
    uint32_t state = 0x12345678u;
    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float bipolar() { return (float)(int32_t)next() * (1.0f / 2147483648.0f); }
    float unipolar() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
};

// ------------------------------------------------------------------ parameters

enum Shape : uint8_t
{
    kShapeSine = 0,
    kShapeTri,
    kShapeSaw,
    kShapeSquare
};

enum FilterType : uint8_t
{
    kFilterLp12 = 0,
    kFilterLp24,
    kFilterBp,
    kFilterHp,
    kFilterCombPlus,
    kFilterCombMinus,
    kFilterTypeCount
};

enum LfoShape : uint8_t
{
    kLfoSine = 0,
    kLfoTriangle,
    kLfoSawUp,
    kLfoSawDown,
    kLfoSquare,
    kLfoSampleHold,
    kLfoSmoothRandom,
    kLfoShapeCount
};

// LFO tempo sync: 0 = free (Hz). Values are length in beats (quarter notes).
static constexpr int kLfoSyncCount = 13;
static constexpr float kLfoSyncBeats[kLfoSyncCount] = {
    0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.5f,
};

enum ModDest : uint8_t
{
    kDestOff = 0,
    kDestPitch,
    kDestMamaPitch,
    kDestBabyPitch,
    kDestMamaMorph,
    kDestBabyMorph,
    kDestPapaMorph,
    kDestMamaVol,
    kDestBabyVol,
    kDestPapaVol,
    kDestNoiseVol,
    kDestCutoff,
    kDestResonance,
    kDestDrive,
    kDestVolume,
    kDestPan,
    kDestLfo1Rate,
    kDestLfo2Rate,
    kDestDelayMix,
    kDestDelayFeedback,
    kDestReverbMix,
    kDestReverbDecay,
    kDestCount
};

enum DelayType : uint8_t
{
    kDelayOff = 0,
    kDelayDigital,
    kDelayTape,
    kDelayPingPong,
    kDelayTypeCount
};

// delay time in beats
static constexpr int kDelayTimeCount = 11;
static constexpr float kDelayBeats[kDelayTimeCount] = {
    0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 1.5f, 2.0f,
};

enum ReverbType : uint8_t
{
    kReverbOff = 0,
    kReverbPlate,
    kReverbDark,
    kReverbShimmer,
    kReverbTypeCount
};

struct ModSlot
{
    uint8_t dest = kDestOff;
    float amount = 0.0f; // -1..1
};

struct LfoParams
{
    uint8_t shape = kLfoSine;
    float rateHz = 2.0f;
    uint8_t sync = 0;     // index into kLfoSyncBeats, 0 = free
    bool retrig = false;  // restart on every note
    ModSlot slot[2];
};

struct EnvParams
{
    float attack = 0.005f; // seconds
    float decay = 0.3f;
    float sustain = 0.5f; // 0..1
    float release = 0.3f;
};

struct ModEnvParams
{
    EnvParams env;
    ModSlot slot[2];
};

struct Params
{
    // oscillators
    float mamaMorph = 2.0f; // 0 sine, 1 tri, 2 saw, 3 square
    float mamaTune = 0.0f;  // semitones
    float mamaFine = 0.0f;  // cents
    float mamaLevel = 0.8f;
    float babyMorph = 2.0f;
    float babyTune = 0.0f;
    float babyFine = 7.0f;
    float babyLevel = 0.6f;
    float papaMorph = 0.0f;
    int papaOctave = -1; // -2..0
    float papaLevel = 0.4f;
    float noiseLevel = 0.0f;
    float drift = 0.15f; // 0..1, slow random pitch wander

    // filter
    uint8_t filterType = kFilterLp24;
    float cutoff = 2400.0f; // Hz
    float resonance = 0.25f;
    float drive = 0.0f;     // 0..1
    float keyTrack = 0.3f;  // 0..1

    // amp
    EnvParams amp{ 0.003f, 0.4f, 0.75f, 0.25f };
    float volume = 0.8f;
    float pan = 0.0f;       // -1..1
    float velToAmp = 0.7f;  // 0..1
    uint8_t glideMode = 0;  // 0 always, 1 overlap, 2 legato (as the stock synths)
    float glideTime = 0.0f; // seconds

    // modulation
    LfoParams lfo[2];
    ModEnvParams env[2];

    // fx
    uint8_t delayType = kDelayOff;
    uint8_t delayTime = 5; // index into kDelayBeats (1/8)
    float delayFeedback = 0.4f;
    float delayMix = 0.3f;
    uint8_t reverbType = kReverbOff;
    float reverbDecay = 0.6f;
    float reverbTone = 0.5f; // 0 dark .. 1 bright
    float reverbMix = 0.3f;

    Params()
    {
        // Env 1 opens the filter a little by default (a classic filter envelope)
        env[0].env = EnvParams{ 0.002f, 0.35f, 0.2f, 0.3f };
        env[0].slot[0] = ModSlot{ kDestCutoff, 0.35f };
        env[1].env = EnvParams{ 0.5f, 0.5f, 0.5f, 0.5f };
        lfo[1].rateHz = 0.3f;
    }
};

// ------------------------------------------------------------------ envelope

class Envelope
{
  public:
    enum Stage : uint8_t
    {
        kIdle = 0,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
        kDeclick // a new note on a sounding voice: fade out fast, then attack from zero
    };

    // Analog-style segments: the attack aims above 1.0 so it is slightly
    // curved, decay and release are exponential.
    static float coefFor(float seconds)
    {
        const float samples = (seconds < 0.0005f ? 0.0005f : seconds) * kSampleRate;
        // reach ~99 % of the way in `seconds`: c = 1 - exp(-4.6/samples)
        const float x = 4.6f / samples;
        return x > 1.0f ? 1.0f : x * (1.0f - 0.5f * x + x * x * (1.0f / 6.0f)); // 1-e^-x
    }

    void set(const EnvParams& p)
    {
        sustain = clampf(p.sustain, 0.0f, 1.0f);
        attackCoef = coefFor(p.attack * 3.1f); // aims at 1.3, gets to 1.0 in about `attack`
        decayCoef = coefFor(p.decay);
        releaseCoef = coefFor(p.release);
    }

    void gate(bool on, bool retrigger)
    {
        if (on)
        {
            if (retrigger || stage == kIdle || stage == kRelease || stage == kDeclick)
            {
                // the amp envelope starts every note from silence, so a long
                // attack is heard on every note, even over the last note's tail
                stage = (restartFromZero && level > 0.001f) ? kDeclick : kAttack;
            }
        }
        else if (stage != kIdle)
        {
            stage = kRelease;
        }
    }

    void reset()
    {
        stage = kIdle;
        level = 0.0f;
    }

    inline float process()
    {
        switch (stage)
        {
            case kAttack:
                level += (1.3f - level) * attackCoef;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = kDecay;
                }
                break;
            case kDecay:
                level += (sustain - level) * decayCoef;
                if (level - sustain < 0.0005f)
                {
                    level = sustain;
                    stage = (sustain <= 0.0005f) ? kIdle : kSustain;
                }
                break;
            case kSustain:
                level = sustain;
                break;
            case kRelease:
                level += (-0.02f - level) * releaseCoef;
                if (level <= 0.0f)
                {
                    level = 0.0f;
                    stage = kIdle;
                }
                break;
            case kDeclick:
                level -= kDeclickStep;
                if (level <= 0.0f)
                {
                    level = 0.0f;
                    stage = kAttack;
                }
                break;
            case kIdle:
                break;
        }
        return level;
    }

    // advance n samples at once (modulation envelopes run at control rate)
    inline float processBlock(int n)
    {
        float v = level;
        for (int i = 0; i < n; ++i)
        {
            v = process();
        }
        return v;
    }

    Stage stage = kIdle;
    float level = 0.0f;
    bool restartFromZero = false; // the amp envelope: yes (see gate)
    static constexpr float kDeclickStep = 1.0f / (0.003f * kSampleRate); // full level to 0 in 3 ms

  private:
    float sustain = 0.5f;
    float attackCoef = 1.0f;
    float decayCoef = 1.0f;
    float releaseCoef = 1.0f;
};

// ------------------------------------------------------------------ LFO

class Lfo
{
  public:
    // returns -1..1, advances by `samples`
    float advance(const LfoParams& p, float tempo, float rateMulOct, int samples)
    {
        float hz = p.rateHz;
        if (p.sync > 0 && p.sync < kLfoSyncCount && tempo > 1.0f)
        {
            hz = tempo / 60.0f / kLfoSyncBeats[p.sync];
        }
        if (rateMulOct != 0.0f)
        {
            hz *= pow2(rateMulOct);
        }
        phase += hz * kInvSampleRate * (float)samples;
        bool wrapped = false;
        while (phase >= 1.0f)
        {
            phase -= 1.0f;
            wrapped = true;
        }
        if (wrapped)
        {
            prevRandom = nextRandom;
            nextRandom = rng.bipolar();
        }
        switch (p.shape)
        {
            case kLfoSine:
                return sinTurns(phase);
            case kLfoTriangle:
                return phase < 0.25f ? 4.0f * phase : (phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f);
            case kLfoSawUp:
                return 2.0f * phase - 1.0f;
            case kLfoSawDown:
                return 1.0f - 2.0f * phase;
            case kLfoSquare:
                return phase < 0.5f ? 1.0f : -1.0f;
            case kLfoSampleHold:
                return nextRandom;
            case kLfoSmoothRandom:
            {
                // cosine interpolation between random points
                const float w = 0.5f - 0.5f * sinTurns(phase * 0.5f + 0.25f);
                return prevRandom + (nextRandom - prevRandom) * w;
            }
            default:
                return 0.0f;
        }
    }

    void retrigger()
    {
        phase = 0.0f;
    }

    void seed(uint32_t s)
    {
        rng.state = s ? s : 1u;
        phase = rng.unipolar();
        prevRandom = rng.bipolar();
        nextRandom = rng.bipolar();
    }

    float phase = 0.0f;

  private:
    Rng rng;
    float prevRandom = 0.0f;
    float nextRandom = 0.0f;
};

// ------------------------------------------------------------------ oscillator

// One morphing oscillator. morph 0..3 crossfades sine > triangle > saw > square.
// All four shapes start at zero rising at phase 0, so crossfades don't cancel.
class MorphOsc
{
  public:
    inline float process(float dt, float morph)
    {
        const float p = phase;
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
        }

        morph = clampf(morph, 0.0f, 3.0f);
        int idx = (int)morph;
        if (idx > 2)
        {
            idx = 2;
        }
        const float frac = morph - (float)idx;
        const float a = shape(idx, p, dt);
        if (frac < 0.001f)
        {
            return a;
        }
        const float b = shape(idx + 1, p, dt);
        return a + (b - a) * frac;
    }

    inline void skip(float dt)
    {
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
        }
    }

    float phase = 0.0f;

  private:
    static inline float shape(int s, float p, float dt)
    {
        switch (s)
        {
            case kShapeSine:
                return sinTurns(p);
            case kShapeTri:
                return 1.05f * (p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f));
            case kShapeSaw:
            {
                float t = p + 0.5f;
                if (t >= 1.0f)
                {
                    t -= 1.0f;
                }
                return 0.9f * (2.0f * t - 1.0f - polyBlep(t, dt));
            }
            default:
            {
                float t2 = p + 0.5f;
                if (t2 >= 1.0f)
                {
                    t2 -= 1.0f;
                }
                const float sq = (p < 0.5f ? 1.0f : -1.0f) + polyBlep(p, dt) - polyBlep(t2, dt);
                return 0.7f * sq;
            }
        }
    }
};

// ------------------------------------------------------------------ filters

// Topology-preserving state variable filter (stable under fast modulation)
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;

    void setup(float g, float kk)
    {
        k = kk;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // returns low, writes band/high
    inline void process(float x, float& low, float& band, float& high)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

// Comb filter: a short delay tuned to the cutoff (it rings at the cutoff and
// its harmonics). Comb+ adds the delayed signal, Comb- subtracts it (hollower,
// odd harmonics an octave down).
class Comb
{
  public:
    static constexpr int kSize = 2048; // lowest tuning ~21.5 Hz

    void reset()
    {
        memset(buf, 0, sizeof(buf));
        damp = 0.0f;
        dc = 0.0f;
    }

    inline float process(float x, float delaySamples, float feedback, float dampCoef)
    {
        float rp = (float)pos - delaySamples;
        if (rp < 0.0f)
        {
            rp += (float)kSize;
        }
        int i0 = (int)rp;
        const float f = rp - (float)i0;
        int i1 = i0 + 1;
        if (i1 >= kSize)
        {
            i1 = 0;
        }
        const float delayed = buf[i0] + (buf[i1] - buf[i0]) * f;
        damp += (delayed - damp) * dampCoef; // one-pole low pass in the loop
        const float hp = damp - dc;          // DC blocker in the loop (~14 Hz)
        dc += hp * 0.002f;
        const float y = x + feedback * hp;
        buf[pos] = softClip(y);
        if (++pos >= kSize)
        {
            pos = 0;
        }
        return y;
    }

  private:
    float buf[kSize];
    int pos = 0;
    float damp = 0.0f;
    float dc = 0.0f;
};

// ------------------------------------------------------------------ voice

class Voice
{
  public:
    void init(uint32_t seed)
    {
        rng.state = seed * 2654435761u + 1u;
        amp.restartFromZero = true;
        lfo[0].seed(seed * 7919u + 3u);
        lfo[1].seed(seed * 104729u + 5u);
        mama.phase = rng.unipolar();
        baby.phase = rng.unipolar();
        papa.phase = 0.0f;
        comb.reset();
    }

    // velocity 0..1. legato = keep the envelopes running (mono legato)
    void noteOn(const Params& p, float note, float velocity, bool legato)
    {
        // glide modes: 0 always, 1 only while the voice still sounds, 2 only legato
        const bool sounding = amp.stage != Envelope::kIdle && amp.stage != Envelope::kRelease;
        const bool glide = hasPlayed && p.glideTime > 0.001f &&
                           (p.glideMode == 0 || (p.glideMode == 1 && (sounding || legato)) ||
                            (p.glideMode == 2 && legato));
        if (!glide)
        {
            currentNote = note;
        }
        targetNote = note;
        hasPlayed = true;
        vel = clampf(velocity, 0.0f, 1.0f);

        // envelopes carry on only in the Legato glide mode; otherwise every
        // note starts its envelopes again (from silence for the amp)
        const bool restart = !(legato && p.glideMode == 2);
        if (restart && amp.stage == Envelope::kIdle)
        {
            // fresh start: oscillators from a clean phase avoid a click
            svf1.reset();
            svf2.reset();
            papa.phase = 0.0f;
        }
        amp.set(p.amp);
        amp.gate(true, restart);
        for (int e = 0; e < 2; ++e)
        {
            env[e].set(p.env[e].env);
            env[e].gate(true, restart);
        }
        for (int l = 0; l < 2; ++l)
        {
            if (p.lfo[l].retrig && restart)
            {
                lfo[l].retrigger();
            }
        }
        controlCountdown = 0;
    }

    void noteOff()
    {
        amp.gate(false, false);
        env[0].gate(false, false);
        env[1].gate(false, false);
    }

    bool active() const { return amp.stage != Envelope::kIdle; }

    // keeps free running LFOs moving while the voice is silent
    void tickIdle(const Params& p, float tempo, int samples)
    {
        lfo[0].advance(p.lfo[0], tempo, 0.0f, samples);
        lfo[1].advance(p.lfo[1], tempo, 0.0f, samples);
    }

    // Adds nothing: writes `n` samples to outL/outR. Also returns the mod
    // values the instrument FX use (only voice 0's are used).
    void render(const Params& p, float tempo, float pitchBend, float* outL, float* outR, int n)
    {
        if (!active())
        {
            for (int i = 0; i < n; ++i)
            {
                outL[i] = outR[i] = 0.0f;
            }
            tickIdle(p, tempo, n);
            return;
        }

        amp.set(p.amp);
        const float glideStep =
          p.glideTime > 0.001f ? (1.0f / (p.glideTime * kSampleRate)) * 24.0f : 1000.0f;

        for (int i = 0; i < n; ++i)
        {
            if (controlCountdown <= 0)
            {
                controlUpdate(p, tempo, pitchBend);
                controlCountdown = kControlRate;
            }
            --controlCountdown;

            // glide (per sample for smoothness)
            if (currentNote != targetNote)
            {
                const float d = targetNote - currentNote;
                const float step = glideStep < absf(d) ? glideStep : absf(d);
                currentNote += d > 0.0f ? step : -step;
            }

            // smooth gains towards their control-rate targets
            gMama += (tMama - gMama) * 0.05f;
            gBaby += (tBaby - gBaby) * 0.05f;
            gPapa += (tPapa - gPapa) * 0.05f;
            gNoise += (tNoise - gNoise) * 0.05f;
            gAmp += (tAmp - gAmp) * 0.05f;
            panL += (tPanL - panL) * 0.05f;
            panR += (tPanR - panR) * 0.05f;

            float s = 0.0f;
            if (gMama > 0.0001f || tMama > 0.0f)
            {
                s += gMama * mama.process(dtMama, morphMama);
            }
            else
            {
                mama.skip(dtMama);
            }
            if (gBaby > 0.0001f || tBaby > 0.0f)
            {
                s += gBaby * baby.process(dtBaby, morphBaby);
            }
            else
            {
                baby.skip(dtBaby);
            }
            if (gPapa > 0.0001f || tPapa > 0.0f)
            {
                s += gPapa * papa.process(dtPapa, morphPapa);
            }
            if (gNoise > 0.0001f)
            {
                s += gNoise * rng.bipolar();
            }
            s *= 0.5f;
#ifdef MORPH_DIAG
            dbgS = absf(s) > dbgS ? absf(s) : dbgS;
            if (!(s == s))
                dbgNan = true;
#endif

            // filter
            if (driveGain > 1.01f)
            {
                s = softClip(s * driveGain) * driveMakeup;
            }
            float y;
            float lo, bp, hp;
            switch (p.filterType)
            {
                case kFilterLp12:
                    svf1.process(s, lo, bp, hp);
                    y = lo;
                    break;
                case kFilterLp24:
                    svf1.process(s, lo, bp, hp);
                    svf2.process(lo, lo, bp, hp);
                    y = lo;
                    break;
                case kFilterBp:
                    svf1.process(s, lo, bp, hp);
                    y = bp * bpGain;
                    break;
                case kFilterHp:
                    svf1.process(s, lo, bp, hp);
                    y = hp;
                    break;
                case kFilterCombPlus:
                case kFilterCombMinus:
                default:
                    y = comb.process(s, combDelay, combFeedback, combDamp) * combGain;
                    break;
            }

            const float e = amp.process();
            const float out = y * e * gAmp;
#ifdef MORPH_DIAG
            dbgY = absf(y) > dbgY ? absf(y) : dbgY;
            dbgE = e > dbgE ? e : dbgE;
            dbgAmp = gAmp > dbgAmp ? gAmp : dbgAmp;
            dbgPan = panL + panR > dbgPan ? panL + panR : dbgPan;
            if (!(y == y) || !(e == e) || !(gAmp == gAmp))
                dbgNan = true;
#endif
            outL[i] = out * panL;
            outR[i] = out * panR;
        }
    }

    // modulation values of the FX destinations (summed mod amounts, -1..1 each)
    float fxMod[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // delay mix, delay fb, reverb mix, reverb decay

    // for the editor scope / graphs
    float ampLevel() const { return amp.level; }

#ifdef MORPH_DIAG
    // hardware diagnosis: the largest values seen inside the voice
    float dbgS = 0.0f, dbgY = 0.0f, dbgE = 0.0f, dbgAmp = 0.0f, dbgPan = 0.0f;
    bool dbgNan = false;
    void dbgReset() { dbgS = dbgY = dbgE = dbgAmp = dbgPan = 0.0f; dbgNan = false; }
#endif

  private:
    void controlUpdate(const Params& p, float tempo, float pitchBend)
    {
        float mod[kDestCount];
        for (int d = 0; d < kDestCount; ++d)
        {
            mod[d] = 0.0f;
        }

        // envelopes first (they can modulate LFO rates)
        float envVal[2];
        for (int e = 0; e < 2; ++e)
        {
            env[e].set(p.env[e].env);
            envVal[e] = env[e].processBlock(kControlRate);
            for (int s = 0; s < 2; ++s)
            {
                const ModSlot& m = p.env[e].slot[s];
                if (m.dest != kDestOff && m.dest < kDestCount)
                {
                    mod[m.dest] += envVal[e] * m.amount;
                }
            }
        }
        for (int l = 0; l < 2; ++l)
        {
            const float rateMod = mod[l == 0 ? kDestLfo1Rate : kDestLfo2Rate] * 3.0f; // +-3 octaves
            const float v = lfo[l].advance(p.lfo[l], tempo, rateMod, kControlRate);
            for (int s = 0; s < 2; ++s)
            {
                const ModSlot& m = p.lfo[l].slot[s];
                if (m.dest != kDestOff && m.dest < kDestCount)
                {
                    mod[m.dest] += v * m.amount;
                }
            }
        }

        // drift: slow random walk per oscillator, up to +-12 cents
        driftMama += (rng.bipolar() - driftMama) * 0.002f;
        driftBaby += (rng.bipolar() - driftBaby) * 0.002f;
        const float driftAmt = p.drift * 0.12f * 4.0f; // walk is roughly +-0.25

        const float base = currentNote + pitchBend + mod[kDestPitch] * 12.0f;
        const float mamaNote = base + p.mamaTune + p.mamaFine * 0.01f + mod[kDestMamaPitch] * 12.0f +
                               driftMama * driftAmt;
        const float babyNote = base + p.babyTune + p.babyFine * 0.01f + mod[kDestBabyPitch] * 12.0f +
                               driftBaby * driftAmt;
        const float papaNote = base + p.mamaTune + 12.0f * (float)p.papaOctave;
        dtMama = clampf(noteHz(mamaNote) * kInvSampleRate, 0.0f, 0.45f);
        dtBaby = clampf(noteHz(babyNote) * kInvSampleRate, 0.0f, 0.45f);
        dtPapa = clampf(noteHz(papaNote) * kInvSampleRate, 0.0f, 0.45f);

        morphMama = p.mamaMorph + mod[kDestMamaMorph] * 3.0f;
        morphBaby = p.babyMorph + mod[kDestBabyMorph] * 3.0f;
        morphPapa = p.papaMorph + mod[kDestPapaMorph] * 3.0f;

        tMama = clampf(p.mamaLevel + mod[kDestMamaVol], 0.0f, 1.0f);
        tBaby = clampf(p.babyLevel + mod[kDestBabyVol], 0.0f, 1.0f);
        tPapa = clampf(p.papaLevel + mod[kDestPapaVol], 0.0f, 1.0f);
        tNoise = clampf(p.noiseLevel + mod[kDestNoiseVol], 0.0f, 1.0f);

        const float velGain = 1.0f - p.velToAmp * (1.0f - vel);
        tAmp = clampf(p.volume + mod[kDestVolume], 0.0f, 1.5f) * velGain;

        // equal power pan
        const float pan = clampf(p.pan + mod[kDestPan], -1.0f, 1.0f);
        // (at centre both are 1.0, hard left gives L = 1.41, R = 0)
        tPanL = sinTurns((1.0f - pan) * 0.125f) * 1.41421356f;
        tPanR = sinTurns((1.0f + pan) * 0.125f) * 1.41421356f;

        // filter
        const float keyOct = (currentNote - 60.0f) / 12.0f * p.keyTrack;
        const float cutoff = clampf(p.cutoff * pow2(keyOct + mod[kDestCutoff] * 5.0f), 16.0f, 20000.0f);
        const float res = clampf(p.resonance + mod[kDestResonance], 0.0f, 1.0f);
        const float drive = clampf(p.drive + mod[kDestDrive], 0.0f, 1.0f);
        driveGain = 1.0f + drive * 7.0f;
        driveMakeup = 1.0f / (1.0f + drive * 1.5f);

        const float g = prewarp(cutoff);
        if (p.filterType == kFilterLp24)
        {
            svf1.setup(g, 1.41421356f - res * 0.6f);
            svf2.setup(g, 2.0f - res * 1.9f);
        }
        else
        {
            svf1.setup(g, 2.0f - res * 1.9f);
        }
        bpGain = (2.0f - res * 1.9f) * 1.2f; // band output peaks at 1/k

        // resonance 50 % -> 0.75 feedback, 95 % -> 0.995 (long ring)
        const float fb = 0.998f * (1.0f - (1.0f - res) * (1.0f - res));
        combFeedback = (p.filterType == kFilterCombMinus) ? -fb : fb;
        combDamp = clampf(0.55f + cutoff * (1.0f / 8000.0f), 0.55f, 1.0f);
        // the damping low pass in the loop delays the signal by (1-a)/a samples:
        // take that off so the comb stays in tune
        const float dampDelay = (1.0f - combDamp) / combDamp;
        combDelay = clampf(kSampleRate / cutoff - dampDelay, 2.0f, (float)(Comb::kSize - 2));
        combGain = 0.5f + 0.5f * (1.0f - fb);

        fxMod[0] = mod[kDestDelayMix];
        fxMod[1] = mod[kDestDelayFeedback];
        fxMod[2] = mod[kDestReverbMix];
        fxMod[3] = mod[kDestReverbDecay];
    }

    static float noteHz(float note) { return 440.0f * pow2((note - 69.0f) * (1.0f / 12.0f)); }

    MorphOsc mama, baby, papa;
    Svf svf1, svf2;
    Comb comb;
    Envelope amp;
    Envelope env[2];
    Lfo lfo[2];
    Rng rng;

    bool hasPlayed = false;
    float currentNote = 60.0f, targetNote = 60.0f;
    float vel = 1.0f;
    int controlCountdown = 0;

    float dtMama = 0.0f, dtBaby = 0.0f, dtPapa = 0.0f;
    float morphMama = 0.0f, morphBaby = 0.0f, morphPapa = 0.0f;
    float tMama = 0.0f, tBaby = 0.0f, tPapa = 0.0f, tNoise = 0.0f, tAmp = 0.0f;
    float gMama = 0.0f, gBaby = 0.0f, gPapa = 0.0f, gNoise = 0.0f, gAmp = 0.0f;
    float tPanL = 1.0f, tPanR = 1.0f, panL = 1.0f, panR = 1.0f;
    float driftMama = 0.0f, driftBaby = 0.0f;
    float driveGain = 1.0f, driveMakeup = 1.0f, bpGain = 1.0f;
    float combDelay = 100.0f, combFeedback = 0.0f, combDamp = 1.0f, combGain = 1.0f;
};

// ------------------------------------------------------------------ FX

// Memory for the FX is allocated once, by whoever owns the Fx object.
// Fx::kMemoryBytes is the size of the block passed to Fx::attach().
namespace fxsize
{
// reverb (Dattorro plate, delays scaled from 29761 Hz to 44100 Hz)
static constexpr int kPreDiff[4] = { 211, 159, 562, 411 };
static constexpr int kTankAp1[2] = { 996, 1346 }; // modulated
static constexpr int kTankDel1[2] = { 6599, 6249 };
static constexpr int kTankAp2[2] = { 2667, 3936 };
static constexpr int kTankDel2[2] = { 5512, 4687 };
static constexpr int kModExcursion = 24;
static constexpr int kShimmerSize = 4096;
static constexpr int kDelaySize = 65536; // per channel, int16 (1.49 s)

constexpr int reverbFloats()
{
    int n = 0;
    for (int i = 0; i < 4; ++i)
    {
        n += kPreDiff[i] + 1;
    }
    for (int c = 0; c < 2; ++c)
    {
        n += kTankAp1[c] + kModExcursion + 2 + kTankDel1[c] + 1 + kTankAp2[c] + 1 + kTankDel2[c] + 1;
    }
    return n + kShimmerSize;
}
} // namespace fxsize

class Fx
{
  public:
    static constexpr int kMemoryBytes = fxsize::reverbFloats() * 4 + fxsize::kDelaySize * 2 * 2;

    void attach(void* mem)
    {
        memory = (uint8_t*)mem;
        if (!memory)
        {
            return;
        }
        memset(memory, 0, kMemoryBytes);
        float* f = (float*)memory;
        for (int i = 0; i < 4; ++i)
        {
            preDiff[i].init(f, kPreDiff[i] + 1);
            f += kPreDiff[i] + 1;
        }
        for (int c = 0; c < 2; ++c)
        {
            ap1[c].init(f, kTankAp1[c] + kModExcursion + 2);
            f += kTankAp1[c] + kModExcursion + 2;
            del1[c].init(f, kTankDel1[c] + 1);
            f += kTankDel1[c] + 1;
            ap2[c].init(f, kTankAp2[c] + 1);
            f += kTankAp2[c] + 1;
            del2[c].init(f, kTankDel2[c] + 1);
            f += kTankDel2[c] + 1;
        }
        shimmerBuf = f;
        f += kShimmerSize;
        delayBuf[0] = (int16_t*)f;
        delayBuf[1] = delayBuf[0] + kDelaySize;
    }

    bool ready() const { return memory != nullptr; }

    // reverb output only (no dry signal), for effects that mix it themselves
    bool wetOnly = false;
    // freeze: the tail holds (no loss, no new input) until switched off
    bool freeze = false;

    // modulation: delay mix, delay feedback, reverb mix, reverb decay (from voice 0)
    void process(const Params& p, float tempo, const float* fxMod, float* l, float* r, int n)
    {
        if (!memory)
        {
            return;
        }
        // a broken number must never reach the feedback loops: once inside a
        // reverb tank it goes round for ever and the sound stays silent
        if (l != nullptr && r != nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                if (!finiteBits(l[i]) || l[i] > 64.0f || l[i] < -64.0f)
                    l[i] = 0.0f;
                if (!finiteBits(r[i]) || r[i] > 64.0f || r[i] < -64.0f)
                    r[i] = 0.0f;
            }
        }
        processEffects(p, tempo, fxMod, l, r, n);
        if (l != nullptr && r != nullptr)
        {
            bool ok = true;
            for (int i = 0; i < n && ok; ++i)
                ok = finiteBits(l[i]) && finiteBits(r[i]) && l[i] < 64.0f && l[i] > -64.0f && r[i] < 64.0f &&
                     r[i] > -64.0f;
            if (!ok)
            {
                // something went wrong anyway: empty the effects, a silent block
                clearDelay();
                clearReverb();
                for (int i = 0; i < n; ++i)
                    l[i] = r[i] = 0.0f;
                ++repairs;
            }
        }
    }
    // how many times the effects had to clear themselves (tests, diagnosis)
    uint32_t repairs = 0;

  private:
    void processEffects(const Params& p, float tempo, const float* fxMod, float* l, float* r, int n)
    {
        if (p.delayType != kDelayOff)
        {
            processDelay(p, tempo, fxMod, l, r, n);
        }
        else if (delayWasOn)
        {
            clearDelay();
        }
        if (p.reverbType != kReverbOff)
        {
            processReverb(p, fxMod, l, r, n);
        }
        else if (reverbWasOn)
        {
            clearReverb();
        }
    }

    static constexpr const int* kPreDiff = fxsize::kPreDiff;
    static constexpr const int* kTankAp1 = fxsize::kTankAp1;
    static constexpr const int* kTankDel1 = fxsize::kTankDel1;
    static constexpr const int* kTankAp2 = fxsize::kTankAp2;
    static constexpr const int* kTankDel2 = fxsize::kTankDel2;
    static constexpr int kModExcursion = fxsize::kModExcursion;
    static constexpr int kShimmerSize = fxsize::kShimmerSize;
    static constexpr int kDelaySize = fxsize::kDelaySize;

    struct Line
    {
        float* buf = nullptr;
        int size = 1;
        int pos = 0;
        void init(float* b, int s)
        {
            buf = b;
            size = s;
            pos = 0;
        }
        inline void write(float x)
        {
            buf[pos] = x;
            if (++pos >= size)
            {
                pos = 0;
            }
        }
        // value written `d` samples ago (d >= 1)
        inline float tap(int d) const
        {
            int i = pos - d;
            if (i < 0)
            {
                i += size;
            }
            return buf[i];
        }
        inline float tapFrac(float d) const
        {
            const int di = (int)d;
            const float f = d - (float)di;
            const float a = tap(di);
            const float b = tap(di + 1);
            return a + (b - a) * f;
        }
        // Schroeder all-pass with delay `d`
        inline float allpass(float x, int d, float g)
        {
            const float delayed = tap(d);
            const float v = x + g * delayed;
            write(v);
            return delayed - g * v;
        }
        inline float allpassMod(float x, float d, float g)
        {
            const float delayed = tapFrac(d);
            const float v = x + g * delayed;
            write(v);
            return delayed - g * v;
        }
        void clear()
        {
            if (buf)
            {
                memset(buf, 0, sizeof(float) * size);
            }
        }
    };

    void clearDelay()
    {
        memset(delayBuf[0], 0, sizeof(int16_t) * kDelaySize * 2);
        delayWasOn = false;
        dlyLp[0] = dlyLp[1] = 0.0f;
    }

    void clearReverb()
    {
        for (int i = 0; i < 4; ++i)
        {
            preDiff[i].clear();
        }
        for (int c = 0; c < 2; ++c)
        {
            ap1[c].clear();
            del1[c].clear();
            ap2[c].clear();
            del2[c].clear();
            damp[c] = 0.0f;
        }
        memset(shimmerBuf, 0, sizeof(float) * kShimmerSize);
        reverbWasOn = false;
        bandwidth = 0.0f;
        shimmerLp = 0.0f;
    }

    static inline int16_t toI16(float x)
    {
        x = clampf(x, -1.0f, 1.0f) * 32767.0f;
        return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
    }

    inline float readDelay(int ch, float d) const
    {
        float rp = (float)dlyPos - d;
        while (rp < 0.0f)
        {
            rp += (float)kDelaySize;
        }
        int i0 = (int)rp;
        const float f = rp - (float)i0;
        int i1 = i0 + 1;
        if (i1 >= kDelaySize)
        {
            i1 -= kDelaySize;
        }
        if (i0 >= kDelaySize)
        {
            i0 -= kDelaySize;
        }
        const float a = (float)delayBuf[ch][i0];
        const float b = (float)delayBuf[ch][i1];
        return (a + (b - a) * f) * (1.0f / 32767.0f);
    }

    void processDelay(const Params& p, float tempo, const float* fxMod, float* l, float* r, int n)
    {
        delayWasOn = true;
        const int ti = p.delayTime < kDelayTimeCount ? p.delayTime : 5;
        const float bpm = tempo > 20.0f ? tempo : 120.0f;
        float target = kDelayBeats[ti] * 60.0f / bpm * kSampleRate;
        target = clampf(target, 64.0f, (float)(kDelaySize - 64));
        if (dlySamples <= 0.0f)
        {
            dlySamples = target;
        }
        const float fb = clampf(p.delayFeedback + fxMod[1], 0.0f, 0.95f);
        const float mix = clampf(p.delayMix + fxMod[0], 0.0f, 1.0f);
        const bool tape = p.delayType == kDelayTape;
        const bool ping = p.delayType == kDelayPingPong;
        const float lpCoef = tape ? 0.35f : 0.9f;

        for (int i = 0; i < n; ++i)
        {
            // glide the delay time (tape-like, no clicks when the tempo changes)
            dlySamples += (target - dlySamples) * 0.0005f;
            float d = dlySamples;
            if (tape)
            {
                wowPhase += 0.6f * kInvSampleRate;
                if (wowPhase >= 1.0f)
                {
                    wowPhase -= 1.0f;
                }
                d += 18.0f * sinTurns(wowPhase) + 4.0f * sinTurns(wowPhase * 7.3f);
            }
            float wl = readDelay(0, d);
            float wr = readDelay(1, d);
            dlyLp[0] += (wl - dlyLp[0]) * lpCoef;
            dlyLp[1] += (wr - dlyLp[1]) * lpCoef;
            wl = dlyLp[0];
            wr = dlyLp[1];

            float inL, inR;
            if (ping)
            {
                // mono input goes to the left, repeats bounce left <-> right
                inL = (l[i] + r[i]) * 0.5f + wr * fb;
                inR = wl * fb;
            }
            else
            {
                inL = l[i] + wl * fb;
                inR = r[i] + wr * fb;
            }
            if (tape)
            {
                inL = softClip(inL * 1.3f) * 0.77f;
                inR = softClip(inR * 1.3f) * 0.77f;
            }
            delayBuf[0][dlyPos] = toI16(inL);
            delayBuf[1][dlyPos] = toI16(inR);
            if (++dlyPos >= kDelaySize)
            {
                dlyPos = 0;
            }
            l[i] = l[i] * (1.0f - 0.4f * mix) + wl * mix;
            r[i] = r[i] * (1.0f - 0.4f * mix) + wr * mix;
        }
    }

    // octave-up pitch shifter for the shimmer: two crossfading read heads
    inline float shimmer(float x)
    {
        shimmerBuf[shimPos] = x;
        shimPhase += 1.0f / 2048.0f; // (ratio - 1) / window
        if (shimPhase >= 1.0f)
        {
            shimPhase -= 1.0f;
        }
        float out = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            float ph = shimPhase + 0.5f * (float)h;
            if (ph >= 1.0f)
            {
                ph -= 1.0f;
            }
            const float d = 2048.0f * (1.0f - ph) + 2.0f;
            float rp = (float)shimPos - d;
            if (rp < 0.0f)
            {
                rp += (float)kShimmerSize;
            }
            int i0 = (int)rp;
            const float f = rp - (float)i0;
            const int i1 = (i0 + 1) & (kShimmerSize - 1);
            const float v = shimmerBuf[i0] + (shimmerBuf[i1] - shimmerBuf[i0]) * f;
            out += v * sinTurns(ph * 0.5f); // sin window, the two heads sum to constant power
        }
        shimPos = (shimPos + 1) & (kShimmerSize - 1);
        return out;
    }

    void processReverb(const Params& p, const float* fxMod, float* l, float* r, int n)
    {
        reverbWasOn = true;
        const bool dark = p.reverbType == kReverbDark;
        const bool shim = p.reverbType == kReverbShimmer;
        const float decayParam = clampf(p.reverbDecay + fxMod[3], 0.0f, 1.0f);
        const float mix = clampf(p.reverbMix + fxMod[2], 0.0f, 1.0f);
        float decay = 0.25f + decayParam * (dark ? 0.73f : 0.70f); // 0.25 .. 0.95/0.98
        if (shim)
        {
            decay = 0.4f + decayParam * 0.55f;
        }
        const float tone = clampf(p.reverbTone, 0.0f, 1.0f);
        // the octave-up copy goes round the tank again: keep that loop below 1
        const float shimmerGain = 0.12f + 0.1f * (1.0f - decayParam);
        // damping: amount of high end lost in the tank on every pass
        float dampCoef = dark ? 0.12f + tone * 0.25f : 0.25f + tone * 0.7f;
        float bw = dark ? 0.2f + tone * 0.3f : 0.55f + tone * 0.44f;
        if (freeze)
        {
            decay = 0.9995f;
            dampCoef = 1.0f; // no damping: the frozen sound keeps its colour
        }
        const float modDepth = dark ? 16.0f : 8.0f;
        const float lfoInc = (dark ? 0.6f : 1.0f) * kInvSampleRate;

        for (int i = 0; i < n; ++i)
        {
            float x = freeze ? 0.0f : (l[i] + r[i]) * 0.5f;
            if (shim && !freeze)
            {
                // feed an octave-up copy of the tail back in
                shimmerLp += (shimmerFeedback - shimmerLp) * 0.3f;
                x += shimmer(shimmerLp) * shimmerGain;
            }
            bandwidth += (x - bandwidth) * bw;
            x = bandwidth;
            x = preDiff[0].allpass(x, kPreDiff[0], 0.75f);
            x = preDiff[1].allpass(x, kPreDiff[1], 0.75f);
            x = preDiff[2].allpass(x, kPreDiff[2], 0.625f);
            x = preDiff[3].allpass(x, kPreDiff[3], 0.625f);

            revLfo += lfoInc;
            if (revLfo >= 1.0f)
            {
                revLfo -= 1.0f;
            }
            const float m0 = modDepth * (1.0f + sinTurns(revLfo)) * 0.5f;
            const float m1 = modDepth * (1.0f + sinTurns(revLfo + 0.25f)) * 0.5f;

            // the two halves of the tank feed each other
            const float fromRight = del2[1].tap(kTankDel2[1]);
            const float fromLeft = del2[0].tap(kTankDel2[0]);

            for (int c = 0; c < 2; ++c)
            {
                float t = x + decay * (c == 0 ? fromRight : fromLeft);
                t = softClip(t);
                t = ap1[c].allpassMod(t, (float)kTankAp1[c] + (c == 0 ? m0 : m1), -0.7f);
                del1[c].write(t);
                float dd = del1[c].tap(kTankDel1[c]);
                damp[c] += (dd - damp[c]) * dampCoef;
                dd = damp[c] * decay;
                dd = ap2[c].allpass(dd, kTankAp2[c], 0.5f);
                del2[c].write(dd);
            }

            // output taps (Dattorro), scaled to 44.1 kHz
            const float s = 1.4818f;
            float yl = del1[1].tap((int)(266 * s)) + del1[1].tap((int)(2974 * s)) -
                       ap2[1].tap((int)(1913 * s)) + del2[1].tap((int)(1996 * s)) -
                       del1[0].tap((int)(1990 * s)) - ap2[0].tap((int)(187 * s)) -
                       del2[0].tap((int)(1066 * s));
            float yr = del1[0].tap((int)(353 * s)) + del1[0].tap((int)(3627 * s)) -
                       ap2[0].tap((int)(1228 * s)) + del2[0].tap((int)(2673 * s)) -
                       del1[1].tap((int)(2111 * s)) - ap2[1].tap((int)(335 * s)) -
                       del2[1].tap((int)(121 * s));
            yl *= 0.4f;
            yr *= 0.4f;
            if (shim)
            {
                shimmerFeedback = (yl + yr) * 0.5f;
            }
            if (wetOnly)
            {
                l[i] = yl * mix;
                r[i] = yr * mix;
            }
            else
            {
                l[i] = l[i] * (1.0f - 0.5f * mix) + yl * mix;
                r[i] = r[i] * (1.0f - 0.5f * mix) + yr * mix;
            }
        }
    }

    uint8_t* memory = nullptr;

    Line preDiff[4];
    Line ap1[2], del1[2], ap2[2], del2[2];
    float damp[2] = { 0.0f, 0.0f };
    float bandwidth = 0.0f;
    float revLfo = 0.0f;
    float* shimmerBuf = nullptr;
    int shimPos = 0;
    float shimPhase = 0.0f;
    float shimmerFeedback = 0.0f;
    float shimmerLp = 0.0f;
    bool reverbWasOn = false;

    int16_t* delayBuf[2] = { nullptr, nullptr };
    int dlyPos = 0;
    float dlySamples = 0.0f;
    float dlyLp[2] = { 0.0f, 0.0f };
    float wowPhase = 0.0f;
    bool delayWasOn = false;
};

} // namespace morph
