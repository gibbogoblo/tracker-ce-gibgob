// CHILLAMANG - GibGob OS electric piano (a mellow tine piano, Rhodes style).
// The DSP core: used by the plugin (chillamang.cpp) and by the WAV previews and
// tests on the computer, so what you hear in a preview is what the Mini plays.
//
// A voice, per note:
//   body    a soft FM tone (carrier + modulator at the note), its brightness
//           set by how hard the key is hit, mellowing as it rings
//   tine    a bell ping at the start: glassy (inharmonic, FM) .. round (a pure
//           harmonic two octaves up, swelling in softly): the Tine tone knob
//   beat    a faint detuned partial: the slow shimmer of the tone bar
//   pickup  uneven saturation: warmth, and "bark" when hit hard
//   hammer  a felt thump at the attack; damper  a soft thud on release
//   tone    a gentle low-pass that follows the key and the velocity
//   decay   two stages (quick drop, long ring), low notes ring longer
//   warmth  the analog side: each voice drifts a little in pitch, every hit
//           differs slightly (tuning, brightness, level, ping), a steeper
//           softer top and fuller low mids
// The instrument (all voices together): suitcase tremolo (stereo autopan),
// chorus, phaser, then MORPHANGUTAN's delay and reverb (morph::Fx).
// MIT License, part of GibGob OS.

#pragma once

#include "morphangutan/morph_dsp.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace chill
{

using morph::absf;
using morph::clampf;
using morph::finiteBits;
using morph::kInvSampleRate;
using morph::kSampleRate;
using morph::pow2;
using morph::sinTurns;
using morph::softClip;

enum TremMode : uint8_t
{
    kTremPan = 0, // stereo autopan (the suitcase sound)
    kTremAmp,     // volume only
    kTremModeCount
};

struct Params
{
    // Tines
    float tineLevel = 0.35f; // the bell ping at the start
    float tineDecay = 0.30f; // 0..1: how long the ping rings
    float tineTone = 0.55f;  // 0 glassy (bright, a little clangy) .. 1 round (pure, serene)
    float warmth = 0.6f;     // 0 clean and exact .. 1 warm, drifting, analog
    float bark = 0.35f;      // growl when hit hard
    float body = 0.45f;      // brightness of the tone
    float pickup = 0.55f;    // 0 hollow .. 1 full (where the tine sits at the pickup)
    float hammer = 0.30f;    // felt thump at the attack
    float beat = 0.25f;      // tone bar shimmer
    float tune = 0.0f;       // semitones
    float fine = 0.0f;       // cents
    // Tone
    float brightness = 4600.0f; // Hz, the low-pass
    float velBright = 0.55f;    // harder = brighter
    float drive = 0.15f;        // preamp warmth
    float age = 0.15f;          // darker, older, a little wobbly
    float keyTrack = 0.5f;      // higher keys brighter
    // Keys
    float decay = 0.55f;   // 0..1: how long notes ring
    float release = 0.18f; // seconds: the damper
    float felt = 0.0f;     // softer, rounder hammers
    float velToAmp = 0.65f;
    float volume = 0.8f;
    float pan = 0.0f;
    float spread = 0.35f; // low keys left, high keys right
    // Tremolo / vibrato
    float tremRate = 4.2f; // Hz (free)
    uint8_t tremSync = 0;  // index into morph::kLfoSyncBeats (0 = free)
    float tremDepth = 0.35f;
    uint8_t tremMode = kTremPan;
    float vibRate = 5.0f;
    float vibDepth = 0.0f; // cents x 30
    // Chorus / phaser
    float chorusMix = 0.2f;
    float chorusRate = 0.55f; // Hz
    float chorusDepth = 0.5f;
    float phaserMix = 0.0f;
    float phaserRate = 0.35f; // Hz
    float phaserFeedback = 0.45f;
    // delay / reverb (as MORPHANGUTAN)
    uint8_t delayType = morph::kDelayOff;
    uint8_t delayTime = 5;
    float delayFeedback = 0.35f;
    float delayMix = 0.25f;
    uint8_t reverbType = morph::kReverbPlate;
    float reverbDecay = 0.55f;
    float reverbTone = 0.45f;
    float reverbMix = 0.22f;

    // the delay / reverb settings in MORPHANGUTAN's form
    void fillFx(morph::Params& f) const
    {
        f.delayType = delayType;
        f.delayTime = delayTime;
        f.delayFeedback = delayFeedback;
        f.delayMix = delayMix;
        f.reverbType = reverbType;
        f.reverbDecay = reverbDecay;
        f.reverbTone = reverbTone;
        f.reverbMix = reverbMix;
    }
    bool wantsModFx() const { return chorusMix > 0.001f || phaserMix > 0.001f; }
    bool wantsSpaceFx() const { return delayType != morph::kDelayOff || reverbType != morph::kReverbOff; }
};

// a smooth, tube-like saturation: rounds the peaks off gradually and never
// flattens them (a hard flat top has sharp corners: harsh, digital)
inline float warmSat(float x)
{
    return x / sqrtf(1.0f + x * x);
}

// exp(-1 / (seconds * sr)): the per-sample factor of a decay with that time constant
inline float decayCoef(float seconds)
{
    if (seconds < 0.0005f)
        seconds = 0.0005f;
    // e^-x for small x: 1 - x + x^2/2 (x < 0.05 here for every time used)
    const float x = kInvSampleRate / seconds;
    return x < 0.05f ? 1.0f - x + 0.5f * x * x : pow2(-x * 1.4426950f);
}

class Voice
{
  public:
    void init(uint32_t seed)
    {
        rng.state = 0x9E3779B9u ^ (seed * 2654435761u);
        if (rng.state == 0)
            rng.state = 1;
        // each voice its own slow drift (two slow waves at its own rates)
        driftRate1 = (0.07f + 0.10f * rng.unipolar()) * kInvSampleRate;
        driftRate2 = (0.23f + 0.25f * rng.unipolar()) * kInvSampleRate;
        driftPh1 = rng.unipolar();
        driftPh2 = rng.unipolar();
        reset();
    }

    void reset()
    {
        amp = ampFast = ampSlow = 0.0f;
        idxEnv = bellEnv = hamEnv = thumpEnv = 0.0f;
        relMul = 1.0f;
        releasing = false;
        lp1 = lp2 = lp3 = hamLp = thumpLp = bellLp = 0.0f;
        bellRise = 0.0f;
        dcX = dcY = 0.0f;
        playing = false;
    }

    void noteOn(const Params& p, float note, float velocity)
    {
        const bool wasPlaying = playing && amp > 0.001f;
        noteNum = note;
        vel = clampf(velocity, 0.0f, 1.0f);
        const float semis = note - 69.0f + p.tune + p.fine * 0.01f;
        f0 = 440.0f * pow2(semis * (1.0f / 12.0f));
        // low keys ring longer, high keys die faster
        keyScale = clampf(pow2(-(note - 60.0f) * (1.0f / 22.0f)), 0.2f, 3.0f);

        if (!wasPlaying)
        {
            phC = phM = phB = phBm = phBeat = phR = 0.0f;
            lp1 = lp2 = lp3 = bellLp = 0.0f;
            dcX = dcY = 0.0f;
        }
        // no two hits alike (warmth): tuning, brightness, level, the ping
        const float w = clampf(p.warmth, 0.0f, 1.0f);
        noteDetune = 1.0f + rng.bipolar() * w * 2.5f * 0.000577623f; // up to +-2.5 cents
        noteBright = 1.0f + rng.bipolar() * w * 0.12f;
        noteBell = 1.0f + rng.bipolar() * w * 0.25f;
        noteAmp = 1.0f + rng.bipolar() * w * 0.09f; // about +-0.8 dB
        // two-stage decay: a quick drop then the long ring
        attackLeft = (int)((0.0015f + p.felt * 0.012f) * kSampleRate);
        attackStep = attackLeft > 0 ? 1.0f / (float)attackLeft : 1.0f;
        attackGain = attackLeft > 0 ? 0.0f : 1.0f;
        ampFast = 0.35f;
        ampSlow = 0.65f;
        fastCoef = decayCoef(0.22f * keyScale);
        slowCoef = decayCoef((0.6f + p.decay * p.decay * 14.0f) * keyScale); // ~5 s by default, up to ~15 s
        // brightness of the hit: the FM index, falling as the note rings
        const float v13 = vel * vel; // ~ vel^2: soft keys stay mellow
        idx0 = p.body * (0.35f + 2.2f * v13) * (1.0f - 0.2f * w) * noteBright;
        idxEnv = 1.0f;
        idxCoef = decayCoef((0.18f + 0.35f * p.body) * (0.6f + 0.4f * keyScale));
        // the tine ping
        bellEnv = 1.0f;
        bellCoef = decayCoef((0.02f + p.tineDecay * 0.35f) * (0.5f + 0.5f * keyScale) *
                             (1.0f + 0.6f * clampf(p.tineTone, 0.0f, 1.0f))); // a round ping rings a little longer
        // the ping swells in: sharp when glassy, soft (about 8 ms) when round
        bellRise = 1.0f;
        bellRiseCoef = decayCoef(0.0004f + 0.008f * clampf(p.tineTone, 0.0f, 1.0f));
        // the felt thump
        hamEnv = (0.4f + 0.6f * vel) * p.hammer * (1.0f - 0.6f * p.felt);
        hamCoef = decayCoef(0.003f + 0.004f * p.felt);
        relMul = 1.0f;
        releasing = false;
        thumpEnv = 0.0f;
        playing = true;
    }

    void noteOff(const Params& p)
    {
        if (!playing || releasing)
            return;
        releasing = true;
        relCoef = decayCoef(clampf(p.release, 0.01f, 3.0f) * 0.35f);
        // the damper meets the tine: a soft low thud
        thumpEnv = 0.0075f * (0.3f + 0.7f * vel) * amp; // barely there: a chord let go must not knock
        thumpCoef = decayCoef(0.02f);
    }

    bool active() const { return playing; }

    // adds this voice to outL / outR (does not clear them)
    void render(const Params& p, float pitchBend, float* outL, float* outR, int n)
    {
        if (!playing)
            return;
        // control values for this block
        const float age = clampf(p.age, 0.0f, 1.0f);
        const float vibCents = p.vibDepth * 30.0f + age * 6.0f;
        float vibHz = p.vibRate;
        const float bendMul = pow2(pitchBend * (1.0f / 12.0f));
        const float f = f0 * bendMul;
        const float dC = f * kInvSampleRate;
        const float dBeat = dC * (1.0f + (0.0012f + 0.003f * p.beat)); // a few cents above
        // the tine's ping: inharmonic, kept under 12 kHz
        float bellRatio = 7.11f;
        while (f * bellRatio > 12000.0f && bellRatio > 2.0f)
            bellRatio *= 0.5f;
        const float dB = dC * bellRatio;
        const float dBm = dC * 2.0f;
        const float tone = clampf(p.tineTone, 0.0f, 1.0f);
        const float bellAmt = p.tineLevel * (0.25f + 0.75f * vel) * (1.0f - 0.7f * p.felt) * noteBell;
        // glassy: the inharmonic FM ping; round: a pure partial two octaves up
        // (an octave and a fifth for high keys), a little louder to make up
        const float glassAmt = bellAmt * (1.0f - tone) * (1.0f - tone);
        const float roundAmt = bellAmt * (1.0f - (1.0f - tone) * (1.0f - tone)) * 1.15f;
        float roundRatio = 4.0f;
        if (f * roundRatio > 9000.0f)
            roundRatio = 3.0f;
        if (f * roundRatio > 9000.0f)
            roundRatio = 2.0f;
        const float dR = dC * roundRatio;
        const float glassFm = 0.25f * (1.0f - tone);
        // the ping's own soft low-pass (round: just above the partial)
        const float bellCut = clampf(f * (roundRatio + 1.0f + 10.0f * (1.0f - tone)), 400.0f, 15000.0f);
        const float bellG = 1.0f - pow2(-6.2831853f * bellCut * kInvSampleRate * 1.4426950f);
        // warmth: drift, fuller low mids, a softer top
        const float w = clampf(p.warmth, 0.0f, 1.0f);
        const float driftCents = w * 3.5f;
        const float fund = w * 0.4f; // more of the pure fundamental: rounder, fuller
        const float warmNorm = 1.0f / (1.0f + fund * 0.5f);
        const float topBlend = w * 0.75f;
        const float beatAmt = 0.06f + 0.22f * p.beat;
        const float bark = clampf(p.bark, 0.0f, 1.0f) * vel * vel;
        const float drv = 1.0f + 12.0f * bark;
        // even harmonics (0 hollow .. 1 full), stronger the harder the key is hit
        const float asym = clampf(p.pickup, 0.0f, 1.0f) * 0.45f * (0.15f + 0.85f * vel) * (1.0f + bark);
        // into the pickup gently: normal playing stays clean and keeps its
        // dynamics, only bark (hard hits) pushes it into saturation
        const float pickIn = 0.45f * drv;
        const float pickupNorm = 1.0f / warmSat(pickIn);
        const float fundGain = pickIn * pickupNorm; // the pickup's gain for quiet signals
        const float idxFloor = p.body * 0.05f; // soft keys nearly pure
        // tone: a 12 dB low-pass, brighter for high keys and hard hits, darker with age
        float cutoff = p.brightness * noteBright * (0.45f + p.velBright * vel * 1.1f) *
                       pow2(p.keyTrack * (noteNum - 60.0f) * (1.0f / 24.0f)) * (1.0f - 0.65f * age);
        cutoff *= 1.0f - 0.3f * clampf(p.warmth, 0.0f, 1.0f); // warmth: darker
        cutoff = clampf(cutoff, 120.0f, 16000.0f);
        const float g = 1.0f - pow2(-6.2831853f * cutoff * kInvSampleRate * 1.4426950f);
        const float driveAmt = clampf(p.drive, 0.0f, 1.0f);
        const float driveIn = 1.0f + driveAmt * 2.5f;
        const float driveOut = 1.0f / warmSat(driveIn);
        const float level = p.volume * (1.0f - p.velToAmp + p.velToAmp * (0.15f + 0.85f * vel)) * 0.21f * noteAmp;
        // keyboard spread: low keys left, high keys right
        const float panPos = clampf(p.pan + p.spread * (noteNum - 60.0f) * (1.0f / 30.0f), -1.0f, 1.0f);
        const float gL = sinTurns(0.125f * (1.0f - panPos) + 0.25f) * 1.4142135f; // cos law, 1 at centre
        const float gR = sinTurns(0.125f * (1.0f + panPos)) * 1.4142135f;
        const float hamG = 1.0f - pow2(-6.2831853f * (2200.0f - 1500.0f * p.felt) * kInvSampleRate * 1.4426950f);
        const float thumpG = 1.0f - pow2(-6.2831853f * 180.0f * kInvSampleRate * 1.4426950f);
        const float vibInc = vibHz * kInvSampleRate;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // envelope
            if (attackLeft > 0)
            {
                attackGain += attackStep;
                if (--attackLeft <= 0)
                    attackGain = 1.0f;
            }
            ampFast *= fastCoef;
            ampSlow *= slowCoef;
            if (releasing)
                relMul *= relCoef;
            amp = (ampFast + ampSlow) * attackGain * relMul;
            idxEnv *= idxCoef;
            bellEnv *= bellCoef;
            hamEnv *= hamCoef;
            thumpEnv *= thumpCoef;

            // vibrato (and the wobble of age)
            vibPh += vibInc;
            if (vibPh >= 1.0f)
                vibPh -= 1.0f;
            float vib = vibCents > 0.01f ? 1.0f + sinTurns(vibPh) * vibCents * 0.000577623f : 1.0f;
            // the analog drift: two slow waves of this voice's own
            driftPh1 += driftRate1;
            driftPh2 += driftRate2;
            if (driftPh1 >= 1.0f) driftPh1 -= 1.0f;
            if (driftPh2 >= 1.0f) driftPh2 -= 1.0f;
            if (driftCents > 0.01f)
                vib *= 1.0f + (0.7f * sinTurns(driftPh1) + 0.3f * sinTurns(driftPh2)) * driftCents * 0.000577623f;
            vib *= noteDetune;

            phC += dC * vib;
            phM += dC * vib;
            phB += dB * vib;
            phBm += dBm * vib;
            phBeat += dBeat * vib;
            phR += dR * vib;
            if (phR >= 1.0f) phR -= 1.0f;
            if (phC >= 1.0f) phC -= 1.0f;
            if (phM >= 1.0f) phM -= 1.0f;
            if (phB >= 1.0f) phB -= 1.0f;
            if (phBm >= 1.0f) phBm -= 1.0f;
            if (phBeat >= 1.0f) phBeat -= 1.0f;

            // body: y = sin(carrier + I * sin(modulator))
            const float idx = idx0 * idxEnv + idxFloor;
            const float body = sinTurns(phC + idx * 0.15915494f * sinTurns(phM));
            const float beat = sinTurns(phBeat) * beatAmt;
            bellRise *= bellRiseCoef;
            float bell = 0.0f;
            if (bellEnv > 1e-5f)
            {
                const float glass = glassAmt > 1e-5f ? sinTurns(phB + glassFm * sinTurns(phBm)) * glassAmt : 0.0f;
                const float round = sinTurns(phR) * roundAmt;
                bellLp += ((glass + round) * bellEnv * (1.0f - bellRise) - bellLp) * bellG;
                bell = bellLp;
            }
            float x = (body + beat) * amp + bell * relMul * attackGain; // the ping starts softly too

            // pickup: uneven saturation (bark when hit hard)
            x = warmSat(x * pickIn + asym * x * x * pickIn) * pickupNorm;
            // warmth: more of the pure fundamental, clean (after the pickup, so
            // it adds no overtones and soft keys stay soft)
            x = (x + sinTurns(phC) * fund * fundGain * amp) * warmNorm;

            // hammer thump and damper thud (filtered noise)
            if (hamEnv > 1e-5f || thumpEnv > 1e-5f)
            {
                const float nz = rng.bipolar();
                hamLp += (nz - hamLp) * hamG;
                thumpLp += (nz - thumpLp) * thumpG;
                x += hamLp * hamEnv * attackGain + thumpLp * thumpEnv * 4.0f; // the knock starts softly too
            }
            // the preamp's warmth, before the tone filter (which then smooths
            // the overtones it adds instead of letting them through raw)
            if (driveAmt > 0.001f)
                x = warmSat(x * driveIn) * driveOut;

            // tone
            lp1 += (x - lp1) * g;
            lp2 += (lp1 - lp2) * g;
            lp3 += (lp2 - lp3) * g;
            float y = lp2 + (lp3 - lp2) * topBlend; // warmth: a steeper, softer top
            // the uneven pickup (and the drive on it) shift the wave off centre:
            // take that out last (~20 Hz)
            const float hp = y - dcX + 0.997f * dcY;
            dcX = y;
            dcY = hp;
            y = hp;
            y *= level;
            outL[i] += y * gL;
            outR[i] += y * gR;
            const float a = absf(y);
            peak = a > peak ? a : peak;
        }
        // broken numbers: start over quietly instead of poisoning the mix
        if (!finiteBits(lp3) || !finiteBits(phC) || !finiteBits(amp) || !finiteBits(dcY) || !finiteBits(bellLp))
        {
            reset();
            return;
        }
        // finished: the ring has died away, or the damper is down
        if ((releasing && relMul < 0.0002f) || (amp < 0.00005f && bellEnv < 0.0001f && attackLeft <= 0))
        {
            if (thumpEnv < 1e-4f)
                playing = false;
        }
        lastPeak = peak;
    }

    float level() const { return amp; }
    float lastPeak = 0.0f;

  private:
    morph::Rng rng;
    bool playing = false;
    bool releasing = false;
    float noteNum = 60.0f, vel = 1.0f, f0 = 261.6f, keyScale = 1.0f;
    float phC = 0.0f, phM = 0.0f, phB = 0.0f, phBm = 0.0f, phBeat = 0.0f, phR = 0.0f, vibPh = 0.0f;
    float driftPh1 = 0.0f, driftPh2 = 0.0f, driftRate1 = 0.0f, driftRate2 = 0.0f;
    float noteDetune = 1.0f, noteBright = 1.0f, noteBell = 1.0f, noteAmp = 1.0f;
    float bellRise = 0.0f, bellRiseCoef = 1.0f, bellLp = 0.0f;
    int attackLeft = 0;
    float attackStep = 1.0f, attackGain = 1.0f;
    float amp = 0.0f, ampFast = 0.0f, ampSlow = 0.0f, fastCoef = 1.0f, slowCoef = 1.0f;
    float relMul = 1.0f, relCoef = 1.0f;
    float idx0 = 0.0f, idxEnv = 0.0f, idxCoef = 1.0f;
    float bellEnv = 0.0f, bellCoef = 1.0f;
    float hamEnv = 0.0f, hamCoef = 1.0f, hamLp = 0.0f;
    float thumpEnv = 0.0f, thumpCoef = 1.0f, thumpLp = 0.0f;
    float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;
    float dcX = 0.0f, dcY = 0.0f;
};

// ------------------------------------------------------------------ the instrument's own effects

// tremolo, chorus and phaser on the mix of all voices (before delay / reverb)
class ModFx
{
  public:
    static constexpr int kChorusSize = 2048; // samples per side (46 ms)
    static constexpr int kMemoryBytes = kChorusSize * 2 * (int)sizeof(float);

    void attach(void* mem)
    {
        buf = (float*)mem;
        if (buf != nullptr)
            memset(buf, 0, kMemoryBytes);
        pos = 0;
        for (auto& s : ap)
            s = 0.0f;
        fbL = fbR = 0.0f;
    }
    bool ready() const { return buf != nullptr; }

    // the tremolo needs no memory; chorus and phaser only run with it
    void process(const Params& p, float tempo, float* l, float* r, int n)
    {
        // tremolo rate: free, or a note length of the tempo
        float rate = p.tremRate;
        if (p.tremSync > 0 && p.tremSync < morph::kLfoSyncCount && tempo > 1.0f)
            rate = tempo / (60.0f * morph::kLfoSyncBeats[p.tremSync]);
        const float tremInc = rate * kInvSampleRate;
        const float depth = clampf(p.tremDepth, 0.0f, 1.0f);
        const bool pan = p.tremMode == kTremPan;
        const float chMix = ready() ? clampf(p.chorusMix, 0.0f, 1.0f) : 0.0f;
        const float chInc = p.chorusRate * kInvSampleRate;
        const float base = 0.007f * kSampleRate, sweep = 0.0045f * kSampleRate * clampf(p.chorusDepth, 0.0f, 1.0f);
        const float phMix = clampf(p.phaserMix, 0.0f, 1.0f);
        const float phInc = p.phaserRate * kInvSampleRate;
        const float fb = clampf(p.phaserFeedback, 0.0f, 0.9f);
        for (int i = 0; i < n; ++i)
        {
            float L = l[i], R = r[i];
            // phaser: 4 all-pass stages swept between ~250 Hz and ~2.5 kHz
            if (phMix > 0.001f)
            {
                phPh += phInc;
                if (phPh >= 1.0f)
                    phPh -= 1.0f;
                const float sw = 0.5f + 0.5f * sinTurns(phPh);
                const float hz = 250.0f * pow2(sw * 3.3f);
                const float t = morph::prewarp(hz);
                const float a = (t - 1.0f) / (t + 1.0f);
                float xl = L + fbL * fb, xr = R + fbR * fb;
                for (int k = 0; k < 4; ++k)
                {
                    const float yl = a * xl + ap[k];
                    ap[k] = xl - a * yl;
                    xl = yl;
                    const float yr = a * xr + ap[4 + k];
                    ap[4 + k] = xr - a * yr;
                    xr = yr;
                }
                fbL = xl;
                fbR = xr;
                L = L + (xl - L) * phMix * 0.5f;
                R = R + (xr - R) * phMix * 0.5f;
            }
            // chorus: two moving delays, opposite phase left and right
            if (chMix > 0.001f)
            {
                chPh += chInc;
                if (chPh >= 1.0f)
                    chPh -= 1.0f;
                buf[pos] = L;
                buf[kChorusSize + pos] = R;
                const float dl = base + sweep * (0.5f + 0.5f * sinTurns(chPh));
                const float dr = base + sweep * (0.5f + 0.5f * sinTurns(chPh + 0.5f));
                const float wl = read(buf, dl), wr = read(buf + kChorusSize, dr);
                L = L * (1.0f - 0.5f * chMix) + wl * chMix * 0.7f;
                R = R * (1.0f - 0.5f * chMix) + wr * chMix * 0.7f;
                pos = (pos + 1) & (kChorusSize - 1);
            }
            // tremolo
            if (depth > 0.001f)
            {
                trPh += tremInc;
                if (trPh >= 1.0f)
                    trPh -= 1.0f;
                const float s = sinTurns(trPh);
                if (pan)
                {
                    L *= 1.0f - depth * (0.5f - 0.5f * s);
                    R *= 1.0f - depth * (0.5f + 0.5f * s);
                }
                else
                {
                    const float gmul = 1.0f - depth * (0.5f + 0.5f * s);
                    L *= gmul;
                    R *= gmul;
                }
            }
            l[i] = L;
            r[i] = R;
        }
        // broken numbers: clear and carry on
        if (!finiteBits(fbL) || !finiteBits(fbR) || !finiteBits(ap[0]) || !finiteBits(ap[4]))
        {
            fbL = fbR = 0.0f;
            for (auto& s : ap)
                s = 0.0f;
            if (buf != nullptr)
                memset(buf, 0, kMemoryBytes);
        }
    }

    float tremPhase() const { return trPh; }

  private:
    float read(const float* b, float delay) const
    {
        float rp = (float)pos - delay;
        while (rp < 0.0f)
            rp += (float)kChorusSize;
        const int i0 = (int)rp;
        const float fr = rp - (float)i0;
        const int i1 = (i0 + 1) & (kChorusSize - 1);
        return b[i0 & (kChorusSize - 1)] * (1.0f - fr) + b[i1] * fr;
    }
    float* buf = nullptr;
    int pos = 0;
    float trPh = 0.0f, chPh = 0.0f, phPh = 0.0f;
    float ap[8] = { 0 };
    float fbL = 0.0f, fbR = 0.0f;
};

} // namespace chill
