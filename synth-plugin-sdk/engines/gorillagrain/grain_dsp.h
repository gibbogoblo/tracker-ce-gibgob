// GORILLAGRAIN - GibGob OS granular synth engine, DSP core.
//
// Plain C++ with no Polyend SDK types: the same code runs in the .psyn plugin
// and in the host test / preview renderer.
//
// One playhead per voice moves through the sample (Position + Scan). Grains
// are cut from around it (Spray), each with its own pitch wobble (Jitter),
// sometimes an octave up (Sparkle) or backwards (Reverse), spread across the
// stereo field (Spread), shaped by a window.
//   -> filter (LP12 / LP24 / BP / HP) -> amp ADSR -> pan
//   2 LFOs + 2 envelopes, 2 modulation slots each
// FX (once per instrument): MORPHANGUTAN's delay and reverb (Shimmer!).
//
// MIT License, part of GibGob OS.

#pragma once

#include "morphangutan/morph_dsp.h"

#include <stdint.h>

namespace grain
{

using morph::clampf;
using morph::Envelope;
using morph::EnvParams;
using morph::kControlRate;
using morph::kSampleRate;
using morph::Lfo;
using morph::LfoParams;
using morph::ModSlot;
using morph::pow2;
using morph::Rng;
using morph::sinTurns;
using morph::Svf;

// ------------------------------------------------------------------ parameters

enum Window : uint8_t
{
    kWindowSmooth = 0, // a soft bell: smooth clouds
    kWindowSoft,       // flat top, short fades: more of the sound
    kWindowPluck,      // fast attack, falling: plucky, rhythmic
    kWindowSwell,      // rising, fast end: backwards-feeling
    kWindowCount
};

enum FilterType : uint8_t
{
    kFilterLp12 = 0,
    kFilterLp24,
    kFilterBp,
    kFilterHp,
    kFilterTypeCount
};

enum ModDest : uint8_t
{
    kDestOff = 0,
    kDestPitch,
    kDestPosition,
    kDestScan,
    kDestSize,
    kDestDensity,
    kDestSpray,
    kDestJitter,
    kDestSpread,
    kDestCutoff,
    kDestResonance,
    kDestVolume,
    kDestPan,
    kDestLfo1Rate,
    kDestLfo2Rate,
    kDestDelayMix,
    kDestReverbMix,
    kDestReverbDecay,
    kDestCount
};

struct Params
{
    // grains
    float position = 0.2f;  // 0..1 of the sample: where the playhead starts
    float scan = 0.15f;     // -2..2: playhead speed (1 = as recorded, 0 = frozen)
    float sizeMs = 140.0f;  // 10..1000 ms per grain
    float density = 22.0f;  // 1..100 grains per second
    float spray = 0.12f;    // 0..1: grains scattered around the playhead
    float jitter = 0.08f;   // 0..1: random pitch per grain (up to +-1 semitone)
    float sparkle = 0.0f;   // 0..1: chance a grain plays an octave up
    float reverse = 0.0f;   // 0..1: chance a grain plays backwards
    uint8_t window = kWindowSmooth;
    float spread = 0.6f;    // 0..1: grains panned apart
    float tune = 0.0f;      // semitones
    float fine = 0.0f;      // cents

    // filter
    uint8_t filterType = kFilterLp12;
    float cutoff = 9000.0f;
    float resonance = 0.1f;
    float keyTrack = 0.0f;

    // amp
    EnvParams amp{ 0.4f, 0.6f, 0.85f, 1.6f };
    float volume = 0.8f;
    float pan = 0.0f;
    float velToAmp = 0.5f;

    // modulation
    LfoParams lfo[2];
    struct ModEnv
    {
        EnvParams env;
        ModSlot slot[2];
    } env[2];

    // fx (the instrument's delay and reverb, see morph::Fx)
    uint8_t delayType = morph::kDelayOff;
    uint8_t delayTime = 5;
    float delayFeedback = 0.4f;
    float delayMix = 0.25f;
    uint8_t reverbType = morph::kReverbShimmer;
    float reverbDecay = 0.75f;
    float reverbTone = 0.6f;
    float reverbMix = 0.3f;

    Params()
    {
        // LFO 1 wanders the playhead a little: a living cloud
        lfo[0].shape = morph::kLfoSmoothRandom;
        lfo[0].rateHz = 0.35f;
        lfo[0].slot[0] = ModSlot{ kDestPosition, 0.08f };
        lfo[1].shape = morph::kLfoSine;
        lfo[1].rateHz = 0.12f;
        env[0].env = EnvParams{ 0.01f, 0.6f, 0.0f, 0.5f };
        env[1].env = EnvParams{ 1.0f, 1.0f, 0.5f, 1.0f };
    }

    // the FX part, in the form morph::Fx reads
    void fillFx(morph::Params& fx) const
    {
        fx.delayType = delayType;
        fx.delayTime = delayTime;
        fx.delayFeedback = delayFeedback;
        fx.delayMix = delayMix;
        fx.reverbType = reverbType;
        fx.reverbDecay = reverbDecay;
        fx.reverbTone = reverbTone;
        fx.reverbMix = reverbMix;
    }
};

// What the voice plays from. frames: per channel; data interleaved.
struct Source
{
    const int16_t* data = nullptr;
    uint32_t frames = 0;
    int channels = 1;
    float rateRatio = 1.0f; // the file's sample rate / 44100
    float rootNote = 60.0f; // the note at which it plays as recorded
};

// window value at x (0..1)
inline float windowAt(uint8_t shape, float x)
{
    switch (shape)
    {
        case kWindowSoft:
        {
            float r = x < 0.5f ? x : 1.0f - x;
            r = r * 5.0f; // 20 % fades
            if (r >= 1.0f)
                return 1.0f;
            return r * r * (3.0f - 2.0f * r);
        }
        case kWindowPluck:
        {
            if (x < 0.03f)
                return x * (1.0f / 0.03f);
            const float t = (1.0f - x) * (1.0f / 0.97f);
            return t * t;
        }
        case kWindowSwell:
        {
            if (x > 0.97f)
                return (1.0f - x) * (1.0f / 0.03f);
            const float t = x * (1.0f / 0.97f);
            return t * t;
        }
        default:
        {
            const float s = sinTurns(x * 0.5f);
            return s * s;
        }
    }
}

// ------------------------------------------------------------------ voice

class Voice
{
  public:
    static constexpr int kMaxGrains = 12;

    void init(uint32_t seed)
    {
        rng.state = seed * 2654435761u + 7u;
        amp.restartFromZero = true;
        lfo[0].seed(seed * 7919u + 3u);
        lfo[1].seed(seed * 104729u + 5u);
    }

    void noteOn(const Params& p, const Source& s, float note, float velocity)
    {
        src = s;
        currentNote = note;
        vel = clampf(velocity, 0.0f, 1.0f);
        if (amp.stage == Envelope::kIdle)
        {
            svfL1.reset();
            svfR1.reset();
            svfL2.reset();
            svfR2.reset();
            for (auto& g : grains)
                g.on = false;
        }
        travel = 0.0f; // the playhead starts at Position
        spawnIn = 0;   // the first grain at once
        amp.set(p.amp);
        amp.gate(true, true);
        for (int e = 0; e < 2; ++e)
        {
            env[e].set(p.env[e].env);
            env[e].gate(true, true);
        }
        for (int l = 0; l < 2; ++l)
            if (p.lfo[l].retrig)
                lfo[l].retrigger();
        controlCountdown = 0;
    }

    // the sample may change while a note plays: grains past its end stop
    void setSource(const Source& s)
    {
        if (s.data == src.data && s.frames == src.frames && s.channels == src.channels)
        {
            src.rateRatio = s.rateRatio;
            src.rootNote = s.rootNote;
            return;
        }
        src = s;
        for (auto& g : grains)
            if (g.on && (src.data == nullptr || g.ipos >= (int32_t)src.frames))
                g.on = false;
    }

    void noteOff()
    {
        amp.gate(false, false);
        env[0].gate(false, false);
        env[1].gate(false, false);
    }

    bool active() const { return amp.stage != Envelope::kIdle; }

    void tickIdle(const Params& p, float tempo, int samples)
    {
        lfo[0].advance(p.lfo[0], tempo, 0.0f, samples);
        lfo[1].advance(p.lfo[1], tempo, 0.0f, samples);
    }

    // writes n samples (does not add)
    void render(const Params& p, float tempo, float pitchBend, float* outL, float* outR, int n)
    {
        if (!active() || src.data == nullptr || src.frames < 2)
        {
            for (int i = 0; i < n; ++i)
                outL[i] = outR[i] = 0.0f;
            if (active())
                amp.processBlock(n); // no sample: let the envelope run out
            else
                tickIdle(p, tempo, n);
            return;
        }
        amp.set(p.amp);
        const float frames = (float)src.frames;

        for (int i = 0; i < n; ++i)
        {
            if (controlCountdown <= 0)
            {
                controlUpdate(p, tempo, pitchBend);
                controlCountdown = kControlRate;
            }
            --controlCountdown;

            // the playhead
            travel += scanStep;
            if (travel >= 1.0f)
                travel -= 1.0f;
            else if (travel < 0.0f)
                travel += 1.0f;

            if (--spawnIn <= 0)
            {
                spawn(p, frames);
                const float jitterT = 1.0f + sprayNow * 0.5f * rng.bipolar();
                spawnIn = (int)(kSampleRate / densityNow * jitterT);
                if (spawnIn < 1)
                    spawnIn = 1;
            }

            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (!g.on)
                    continue;
                const float w = windowAt(g.shape, (float)g.age * g.invLen) * g.gain;
                float a, b;
                read(g.ipos, g.frac, a, b);
                l += a * w * g.panL;
                r += b * w * g.panR;
                // next frame
                g.frac += g.inc;
                const int whole = (int)(g.frac >= 0.0f ? g.frac : g.frac - 1.0f);
                g.frac -= (float)whole;
                g.ipos += whole;
                if (g.ipos >= (int32_t)src.frames)
                    g.ipos -= (int32_t)src.frames;
                else if (g.ipos < 0)
                    g.ipos += (int32_t)src.frames;
                if (++g.age >= g.len)
                    g.on = false;
            }

            gAmp += (tAmp - gAmp) * 0.05f;
            panL += (tPanL - panL) * 0.05f;
            panR += (tPanR - panR) * 0.05f;

            float yl, yr, lo, bp, hp;
            switch (p.filterType)
            {
                case kFilterLp24:
                    svfL1.process(l, lo, bp, hp);
                    svfL2.process(lo, yl, bp, hp);
                    svfR1.process(r, lo, bp, hp);
                    svfR2.process(lo, yr, bp, hp);
                    break;
                case kFilterBp:
                    svfL1.process(l, lo, yl, hp);
                    svfR1.process(r, lo, yr, hp);
                    yl *= bpGain;
                    yr *= bpGain;
                    break;
                case kFilterHp:
                    svfL1.process(l, lo, bp, yl);
                    svfR1.process(r, lo, bp, yr);
                    break;
                default:
                    svfL1.process(l, yl, bp, hp);
                    svfR1.process(r, yr, bp, hp);
                    break;
            }
            const float e = amp.process() * gAmp;
            outL[i] = yl * e * panL;
            outR[i] = yr * e * panR;
        }
    }

    float fxMod[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // delay mix, delay fb, reverb mix, reverb decay
    int activeGrains() const
    {
        int c = 0;
        for (const auto& g : grains)
            c += g.on ? 1 : 0;
        return c;
    }
    // where the playhead is (0..1 of the sample), for a picture
    float playhead() const { return headNow; }

  private:
    struct Grain
    {
        bool on = false;
        uint8_t shape = 0;
        int32_t ipos = 0;
        float frac = 0.0f;
        float inc = 1.0f;
        int32_t age = 0;
        int32_t len = 1;
        float invLen = 1.0f;
        float gain = 0.0f;
        float panL = 1.0f, panR = 1.0f;
    };

    inline void read(int32_t i0, float f, float& a, float& b) const
    {
        int32_t i1 = i0 + 1;
        if (i1 >= (int32_t)src.frames)
            i1 = 0;
        constexpr float k = 1.0f / 32768.0f;
        if (src.channels >= 2)
        {
            const int16_t* p0 = src.data + i0 * src.channels;
            const int16_t* p1 = src.data + i1 * src.channels;
            a = ((float)p0[0] + ((float)p1[0] - (float)p0[0]) * f) * k;
            b = ((float)p0[1] + ((float)p1[1] - (float)p0[1]) * f) * k;
        }
        else
        {
            const float s0 = (float)src.data[i0], s1 = (float)src.data[i1];
            a = b = (s0 + (s1 - s0) * f) * k;
        }
    }

    void spawn(const Params& p, float frames)
    {
        Grain* g = nullptr;
        for (auto& c : grains)
            if (!c.on)
            {
                g = &c;
                break;
            }
        if (g == nullptr)
            return; // full: skip this one

        // where: around the playhead
        float pos = headNow;
        const float sprayFrames = sprayNow * (frames * 0.5f < kSampleRate * 2.0f ? frames * 0.5f : kSampleRate * 2.0f);
        float at = pos * frames + rng.bipolar() * sprayFrames;
        while (at >= frames)
            at -= frames;
        while (at < 0.0f)
            at += frames;

        // pitch
        float semis = pitchNow + rng.bipolar() * jitterNow;
        if (p.sparkle > 0.0f && rng.unipolar() < p.sparkle)
            semis += 12.0f;
        float inc = pow2(semis * (1.0f / 12.0f)) * src.rateRatio;
        inc = clampf(inc, 0.0f, 8.0f);
        if (p.reverse > 0.0f && rng.unipolar() < p.reverse)
            inc = -inc;

        g->on = true;
        g->shape = p.window < kWindowCount ? p.window : 0;
        g->ipos = (int32_t)at;
        g->frac = at - (float)g->ipos;
        g->inc = inc;
        g->age = 0;
        g->len = (int32_t)(sizeNow * 0.001f * kSampleRate);
        if (g->len < 16)
            g->len = 16;
        g->invLen = 1.0f / (float)g->len;
        g->gain = grainGain;
        const float pan = clampf(rng.bipolar() * spreadNow, -1.0f, 1.0f);
        g->panL = sinTurns((1.0f - pan) * 0.125f) * 1.41421356f;
        g->panR = sinTurns((1.0f + pan) * 0.125f) * 1.41421356f;
    }

    void controlUpdate(const Params& p, float tempo, float pitchBend)
    {
        float mod[kDestCount];
        for (int d = 0; d < kDestCount; ++d)
            mod[d] = 0.0f;
        for (int e = 0; e < 2; ++e)
        {
            env[e].set(p.env[e].env);
            const float v = env[e].processBlock(kControlRate);
            for (const ModSlot& m : p.env[e].slot)
                if (m.dest != kDestOff && m.dest < kDestCount)
                    mod[m.dest] += v * m.amount;
        }
        for (int l = 0; l < 2; ++l)
        {
            const float rateMod = mod[l == 0 ? kDestLfo1Rate : kDestLfo2Rate] * 3.0f;
            const float v = lfo[l].advance(p.lfo[l], tempo, rateMod, kControlRate);
            for (const ModSlot& m : p.lfo[l].slot)
                if (m.dest != kDestOff && m.dest < kDestCount)
                    mod[m.dest] += v * m.amount;
        }

        // the playhead: Position (+ modulation) plus how far it has scanned
        float head = p.position + mod[kDestPosition] + travel;
        head -= (float)(int)head;
        if (head < 0.0f)
            head += 1.0f;
        headNow = head;
        const float scan = clampf(p.scan + mod[kDestScan] * 2.0f, -4.0f, 4.0f);
        scanStep = src.frames > 0 ? scan * src.rateRatio / (float)src.frames : 0.0f;

        sizeNow = clampf(p.sizeMs * pow2(mod[kDestSize] * 2.0f), 5.0f, 2000.0f);
        densityNow = clampf(p.density * pow2(mod[kDestDensity] * 2.0f), 0.5f, 200.0f);
        sprayNow = clampf(p.spray + mod[kDestSpray], 0.0f, 1.0f);
        jitterNow = clampf(p.jitter + mod[kDestJitter], 0.0f, 1.0f);
        spreadNow = clampf(p.spread + mod[kDestSpread], 0.0f, 1.0f);
        pitchNow = currentNote - src.rootNote + p.tune + p.fine * 0.01f + pitchBend + mod[kDestPitch] * 12.0f;

        // keep the level about the same however many grains overlap
        const float overlap = densityNow * sizeNow * 0.001f;
        const float o = overlap > 1.0f ? overlap : 1.0f;
        // (a smooth window holds ~40 % of the power: x1.6 brings it back to about the sample level)
        grainGain = 1.6f / morph::clampf(sqrtApprox(o), 1.0f, 100.0f);

        const float velGain = 1.0f - p.velToAmp * (1.0f - vel);
        tAmp = clampf(p.volume + mod[kDestVolume], 0.0f, 1.5f) * velGain;
        const float pan = clampf(p.pan + mod[kDestPan], -1.0f, 1.0f);
        tPanL = sinTurns((1.0f - pan) * 0.125f) * 1.41421356f;
        tPanR = sinTurns((1.0f + pan) * 0.125f) * 1.41421356f;

        const float keyOct = (currentNote - 60.0f) / 12.0f * p.keyTrack;
        const float cutoff = clampf(p.cutoff * pow2(keyOct + mod[kDestCutoff] * 5.0f), 16.0f, 20000.0f);
        const float res = clampf(p.resonance + mod[kDestResonance], 0.0f, 1.0f);
        const float g = morph::prewarp(cutoff);
        if (p.filterType == kFilterLp24)
        {
            svfL1.setup(g, 1.41421356f - res * 0.6f);
            svfR1.setup(g, 1.41421356f - res * 0.6f);
            svfL2.setup(g, 2.0f - res * 1.9f);
            svfR2.setup(g, 2.0f - res * 1.9f);
        }
        else
        {
            svfL1.setup(g, 2.0f - res * 1.9f);
            svfR1.setup(g, 2.0f - res * 1.9f);
        }
        bpGain = (2.0f - res * 1.9f) * 1.2f;

        fxMod[0] = mod[kDestDelayMix];
        fxMod[1] = 0.0f;
        fxMod[2] = mod[kDestReverbMix];
        fxMod[3] = mod[kDestReverbDecay];
    }

    static float sqrtApprox(float x)
    {
        // Newton steps from a rough start: plenty for a gain
        float y = x > 4.0f ? x * 0.25f : 1.0f;
        for (int i = 0; i < 5; ++i)
            y = 0.5f * (y + x / y);
        return y;
    }

    Source src;
    Grain grains[kMaxGrains];
    Svf svfL1, svfR1, svfL2, svfR2;
    Envelope amp;
    Envelope env[2];
    Lfo lfo[2];
    Rng rng;

    float currentNote = 60.0f;
    float vel = 1.0f;
    int controlCountdown = 0;
    int spawnIn = 0;
    float travel = 0.0f, scanStep = 0.0f, headNow = 0.0f;
    float sizeNow = 100.0f, densityNow = 20.0f, sprayNow = 0.0f, jitterNow = 0.0f, spreadNow = 0.0f;
    float pitchNow = 0.0f, grainGain = 0.5f, bpGain = 1.0f;
    float tAmp = 0.0f, gAmp = 0.0f, tPanL = 1.0f, tPanR = 1.0f, panL = 1.0f, panR = 1.0f;
};

} // namespace grain
