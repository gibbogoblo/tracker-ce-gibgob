#include "gibgob/MasterFx.h"

#if __has_include("config.h")
#include "config.h" // AUDIO_CORE_RAM: the audio code runs from fast RAM on the device
#endif
#ifndef AUDIO_CORE_RAM
#define AUDIO_CORE_RAM
#endif

#include <math.h>
#include <new>
#include <string.h>

namespace gibgob
{
namespace master
{

namespace
{
constexpr float kSr = 44100.0f;
constexpr float kPi = 3.14159265f;
constexpr int kDelayFrames = 2048; // per channel per slot: wobble / wow
constexpr int kDelayMask = kDelayFrames - 1;

const char* const kRates[] = { "44k", "32k", "22k", "16k", "11k", "8k" };
const float kRateHz[] = { 44100.0f, 32000.0f, 22050.0f, 16000.0f, 11025.0f, 8000.0f };
const char* const kBits[] = { "16", "12", "10", "8", "6", "4" };
const int kBitsN[] = { 16, 12, 10, 8, 6, 4 };
const char* const kLofiFilter[] = { "Off", "8k", "5k", "3k" };
const float kLofiFilterHz[] = { 0.0f, 8000.0f, 5000.0f, 3000.0f };
const char* const kWidth[] = { "0%", "20%", "40%", "60%", "80%", "100%", "120%", "140%", "160%", "180%", "200%" };
const char* const kMonoBass[] = { "Off", "80", "120", "200" };
const float kMonoBassHz[] = { 0.0f, 80.0f, 120.0f, 200.0f };

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

const ColourInfo kColours[kColourCount] = {
    { "Off", 0, {}, { 0, 0, 0, 0 } },
    { "Cassette", 4, { { "Tape", 11, nullptr }, { "Hiss", 11, nullptr }, { "Wobble", 6, nullptr }, { "Dropouts", 6, nullptr } },
      { 5, 3, 2, 1 } },
    { "Vinyl", 4, { { "Crackle", 11, nullptr }, { "Noise", 11, nullptr }, { "Wow", 6, nullptr }, { "Wear", 11, nullptr } },
      { 4, 3, 2, 3 } },
    { "Lo-fi", 3, { { "Rate", N(kRates), kRates }, { "Bits", N(kBits), kBits }, { "Filter", N(kLofiFilter), kLofiFilter } },
      { 2, 3, 1, 0 } },
    { "Width", 2, { { "Width", N(kWidth), kWidth }, { "Mono bass", N(kMonoBass), kMonoBass } }, { 7, 2, 0, 0 } },
};

inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}
// one-pole low-pass coefficient for a cutoff
inline float onePole(float hz)
{
    if (hz <= 0.0f)
        return 1.0f;
    const float c = 1.0f - expf(-2.0f * kPi * hz / kSr);
    return clampf(c, 0.0001f, 1.0f);
}
// soft saturation, unity for small signals
inline float sat(float x)
{
    x = clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
inline uint32_t xorshift(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline float noise(uint32_t& s) // -1..1
{
    return (float)(int32_t)xorshift(s) * (1.0f / 2147483648.0f);
}
// a slow random wander in -1..1: a new target every so often, glided to
struct Wander
{
    float v = 0.0f, target = 0.0f;
    int left = 0;
    inline float step(uint32_t& rng, int holdSamples)
    {
        if (--left <= 0)
        {
            target = noise(rng);
            left = holdSamples / 2 + (int)(xorshift(rng) % (uint32_t)holdSamples);
        }
        v += (target - v) * 0.00012f; // about 0.2 s to get there
        return v;
    }
};
// 0..5 setting: gentle at 1, drastic at 5
inline float shaped(int p)
{
    const float w = (float)p / 5.0f;
    return w * (0.35f + 0.65f * w);
}
inline float lfo(float& phase, float hz)
{
    phase += hz / kSr;
    if (phase >= 1.0f)
        phase -= 1.0f;
    return sinf(2.0f * kPi * phase);
}
} // namespace

const GlueStyleInfo& glueStyleInfo(int style)
{
    // attack, release (ms), ratio, the detector's high-pass (so the bass does not pump the mix)
    static const GlueStyleInfo kStyles[kGlueStyleCount] = {
        { "Hip hop", 30.0f, 400.0f, 4.0f, 90.0f },
        { "Garage", 10.0f, 100.0f, 2.0f, 60.0f },
        { "Jungle", 1.0f, 60.0f, 6.0f, 120.0f },
        { "Clean", 10.0f, 300.0f, 2.0f, 0.0f },
    };
    return kStyles[(style >= 0 && style < kGlueStyleCount) ? style : 0];
}

const ColourInfo& colourInfo(int type)
{
    return kColours[(type >= 0 && type < kColourCount) ? type : 0];
}

Slot defaultSlot(int type)
{
    Slot s;
    s.type = (uint8_t)((type >= 0 && type < kColourCount) ? type : 0);
    for (int k = 0; k < kSlotParams; k++)
        s.p[k] = kColours[s.type].defaults[k];
    return s;
}

float duckReleaseSteps(int release)
{
    static const float kSteps[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
    return kSteps[release & 3];
}

bool Settings::operator==(const Settings& o) const
{
    uint8_t a[kPackedBytes], b[kPackedBytes];
    pack(*this, a);
    pack(o, b);
    return memcmp(a, b, kPackedBytes) == 0;
}

// ---------------------------------------------------------------- packing
namespace
{
struct BitWriter
{
    uint8_t* out;
    int bit = 0;
    void put(uint32_t v, int bits)
    {
        for (int i = 0; i < bits; i++, bit++)
            if ((v >> i) & 1)
                out[bit >> 3] |= (uint8_t)(1u << (bit & 7));
    }
};
struct BitReader
{
    const uint8_t* in;
    int bit = 0;
    uint32_t get(int bits)
    {
        uint32_t v = 0;
        for (int i = 0; i < bits; i++, bit++)
            if ((in[bit >> 3] >> (bit & 7)) & 1)
                v |= 1u << i;
        return v;
    }
};
inline uint8_t limit(uint32_t v, int count, int def)
{
    return (uint8_t)((int)v < count ? (int)v : def);
}
} // namespace

void pack(const Settings& s, uint8_t out[kPackedBytes])
{
    memset(out, 0, kPackedBytes);
    BitWriter w{ out };
    w.put(s.duckOn ? 1 : 0, 1);
    w.put(s.duckTrack & 7, 3);
    w.put(s.duckDepth & 15, 4);
    w.put(s.duckRelease & 3, 2);
    w.put(s.duckShape & 1, 1);
    w.put(s.duckNoise ? 1 : 0, 1);
    w.put(s.glue & 15, 4);
    w.put(s.glueStyle & 3, 2);
    for (int i = 0; i < kSlots; i++)
    {
        w.put(s.slot[i].type & 7, 3);
        for (int k = 0; k < kSlotParams; k++)
            w.put(s.slot[i].p[k] & 15, 4);
    }
}

Settings unpack(const uint8_t in[kPackedBytes])
{
    Settings s;
    BitReader r{ in };
    s.duckOn = r.get(1) != 0;
    s.duckTrack = (uint8_t)r.get(3);
    s.duckDepth = limit(r.get(4), 11, 6);
    s.duckRelease = (uint8_t)r.get(2);
    s.duckShape = (uint8_t)r.get(1);
    s.duckNoise = r.get(1) != 0;
    s.glue = limit(r.get(4), 11, 0);
    s.glueStyle = (uint8_t)r.get(2);
    for (int i = 0; i < kSlots; i++)
    {
        const uint32_t type = r.get(3);
        s.slot[i].type = (uint8_t)(type < (uint32_t)kColourCount ? type : (uint32_t)kColourOff);
        const ColourInfo& ci = kColours[s.slot[i].type];
        for (int k = 0; k < kSlotParams; k++)
        {
            const uint32_t v = r.get(4);
            s.slot[i].p[k] = k < ci.paramCount ? limit(v, ci.params[k].count, ci.defaults[k]) : 0;
        }
    }
    return s;
}

// ---------------------------------------------------------------- processor
struct SlotState
{
    // shared
    uint32_t rng = 0x9E3779B9u;
    float lpL = 0.0f, lpR = 0.0f;   // tone low-pass
    float lp2L = 0.0f, lp2R = 0.0f; // second pole (lo-fi)
    float hpL = 0.0f, hpR = 0.0f;   // wear high-pass state
    float noiseLp = 0.0f, noiseHp = 0.0f;
    float* delay = nullptr; // 2 x kDelayFrames
    int write = 0;
    float phase1 = 0.0f, phase2 = 0.0f, drift = 0.0f;
    Wander wanderA, wanderB; // random wow / wobble
    // cassette dropouts
    float dropEnv = 0.0f, dropTarget = 0.0f;
    int dropLeft = 0;
    // vinyl crackle
    float click = 0.0f, pop = 0.0f;
    // lo-fi
    float holdPhase = 1.0f, holdL = 0.0f, holdR = 0.0f;
    // width
    float sideLp = 0.0f;

    void reset(float* mem)
    {
        float* keep = mem;
        *this = SlotState();
        delay = keep;
        if (delay)
            memset(delay, 0, sizeof(float) * 2 * kDelayFrames);
    }
};

struct Engine::Impl
{
    SlotState slot[kSlots];
    uint8_t lastType[kSlots] = { 0xFF, 0xFF };
    float glueEnv = 0.0f; // detector (linear)
    float glueLpL = 0.0f, glueLpR = 0.0f; // the detector's high-pass
    float glueGain = 1.0f;
};

const uint32_t Engine::kMemoryBytes = (uint32_t)(sizeof(Engine::Impl) + 16 + kSlots * 2 * kDelayFrames * sizeof(float));

void Engine::attach(void* memory)
{
    if (memory == nullptr)
        return;
    uint8_t* m = (uint8_t*)memory;
    impl = new (m) Impl();
    float* d = (float*)(m + ((sizeof(Impl) + 15) & ~15u));
    for (int i = 0; i < kSlots; i++)
        impl->slot[i].reset(d + i * 2 * kDelayFrames);
}

void Engine::setSettings(const Settings& s)
{
    pending = s;
    changed = true;
}

void Engine::noteStarted(int track)
{
    if (live.duckOn && track == live.duckTrack)
        duckTriggers = duckTriggers + 1;
}

AUDIO_CORE_RAM float Engine::duckBlock(int n, float tempo)
{
    if (changed)
    {
        live = pending;
        changed = false;
    }
    if (duckTriggers != duckSeen)
    {
        duckSeen = duckTriggers;
        duckPos = 0.0f;
    }
    float g = 1.0f;
    if (live.duckOn && live.duckDepth > 0)
    {
        const float stepSamples = kSr * 60.0f / (tempo > 20.0f ? tempo : 120.0f) / 4.0f;
        const float R = duckReleaseSteps(live.duckRelease) * stepSamples;
        const float x = duckPos / R;
        const float d = (float)live.duckDepth / 10.0f;
        if (x < 1.0f)
        {
            if (live.duckShape == 0)
            {
                const float u = 1.0f - x;
                g = 1.0f - d * u * u; // smooth: down at once, back in a curve
            }
            else
            {
                // hard: held down for half the release, then back quickly
                g = x < 0.5f ? 1.0f - d : 1.0f - d * 0.5f * (1.0f + cosf(kPi * (x - 0.5f) * 2.0f));
            }
        }
    }
    duckPos += (float)n;
    if (duckPos > 1e8f)
        duckPos = 1e8f;
    duckNow = g;
    return g;
}

AUDIO_CORE_RAM void Engine::process(float* l, float* r, int n)
{
    if (impl == nullptr)
        return;
    Impl& s = *impl;
    const Settings& cfg = live;
    const float noiseGain = cfg.duckNoise ? duckNow : 1.0f;

    for (int k = 0; k < kSlots; k++)
    {
        const Slot& sl = cfg.slot[k];
        SlotState& st = s.slot[k];
        if (sl.type != s.lastType[k])
        {
            // a new colour starts from clean state (its delay line empty)
            st.reset(st.delay);
            s.lastType[k] = sl.type;
        }
        switch (sl.type)
        {
            case kColourCassette:
            {
                const float tape = sl.p[0] / 10.0f, hiss = sl.p[1] / 10.0f, wob = shaped(sl.p[2]),
                            drop = (float)sl.p[3] / 5.0f;
                const float drive = 1.0f + tape * 0.9f;      // up to about +5.6 dB into the tape
                const float makeup = 1.0f + tape * 0.12f;    // tape keeps its loudness
                const float tone = onePole(16000.0f - tape * 11000.0f);
                const float hissLp = onePole(9000.0f);
                for (int i = 0; i < n; i++)
                {
                    float xl = l[i], xr = r[i];
                    // wow and flutter: a gently moving delay
                    if (st.delay && wob > 0.0f)
                    {
                        st.delay[st.write] = xl;
                        st.delay[kDelayFrames + st.write] = xr;
                        // wow that wanders in speed and depth, flutter on top, a slow drift
                        const float wa = st.wanderA.step(st.rng, 30000), wb = st.wanderB.step(st.rng, 22000);
                        const float d = 300.0f + wob * (180.0f * (0.6f + 0.4f * wb) * lfo(st.phase1, 0.5f * (1.0f + 0.5f * wa)) +
                                                         8.0f * (0.7f + 0.3f * wb) * lfo(st.phase2, 6.5f) + 60.0f * wa);
                        const float pos = (float)st.write - d;
                        int i0 = (int)floorf(pos);
                        const float fr = pos - (float)i0;
                        const int a = i0 & kDelayMask, b = (i0 + 1) & kDelayMask;
                        xl = st.delay[a] + (st.delay[b] - st.delay[a]) * fr;
                        xr = st.delay[kDelayFrames + a] + (st.delay[kDelayFrames + b] - st.delay[kDelayFrames + a]) * fr;
                        st.write = (st.write + 1) & kDelayMask;
                    }
                    // tape: saturation, then the top end goes
                    if (tape > 0.0f)
                    {
                        xl = sat(xl * drive) / drive * makeup;
                        xr = sat(xr * drive) / drive * makeup;
                    }
                    // dropouts: now and then the level dips for a moment
                    if (drop > 0.0f)
                    {
                        // more often, deeper and longer as the setting goes up (5: about 2 a second)
                        const float perSecond = 0.15f + 1.8f * drop * drop;
                        if (st.dropLeft <= 0 && (xorshift(st.rng) & 0xFFFFFF) < (uint32_t)(perSecond * 16777216.0f / kSr))
                        {
                            st.dropLeft = (int)(kSr * (0.04f + 0.08f * ((xorshift(st.rng) & 0xFFFF) / 65536.0f) + 0.15f * drop));
                            st.dropTarget = 0.3f + 0.6f * drop;
                        }
                        const float target = st.dropLeft > 0 ? st.dropTarget : 0.0f;
                        st.dropEnv += (target - st.dropEnv) * 0.005f;
                        if (st.dropLeft > 0)
                            st.dropLeft--;
                    }
                    const float dark = st.dropEnv;
                    const float c = tone * (1.0f - 0.7f * dark);
                    st.lpL += (xl - st.lpL) * c;
                    st.lpR += (xr - st.lpR) * c;
                    float yl = st.lpL * (1.0f - 0.6f * dark), yr = st.lpR * (1.0f - 0.6f * dark);
                    // hiss
                    if (hiss > 0.0f)
                    {
                        const float w = noise(st.rng);
                        st.noiseLp += (w - st.noiseLp) * hissLp;
                        const float h = (w - st.noiseLp * 0.6f) * hiss * 0.012f * noiseGain;
                        yl += h;
                        yr += h * 0.8f + noise(st.rng) * hiss * 0.003f * noiseGain;
                    }
                    l[i] = yl;
                    r[i] = yr;
                }
                break;
            }
            case kColourVinyl:
            {
                const float crackle = sl.p[0] / 10.0f, nz = sl.p[1] / 10.0f, wow = shaped(sl.p[2]),
                            wear = sl.p[3] / 10.0f;
                const float tone = onePole(18000.0f - wear * 12000.0f);
                const float hp = onePole(15.0f + wear * 45.0f); // up to 60 Hz at full wear
                const float rumbleLp = onePole(40.0f);
                const float narrow = 1.0f - wear * 0.5f;
                const uint32_t clickChance = (uint32_t)(crackle * 14.0f * 16777216.0f / kSr);
                const uint32_t popChance = (uint32_t)(crackle * 0.5f * 16777216.0f / kSr);
                for (int i = 0; i < n; i++)
                {
                    float xl = l[i], xr = r[i];
                    if (st.delay && wow > 0.0f)
                    {
                        st.delay[st.write] = xl;
                        st.delay[kDelayFrames + st.write] = xr;
                        // 33 rpm, off-centre and warped: the speed and depth wander
                        const float wa = st.wanderA.step(st.rng, 26000), wb = st.wanderB.step(st.rng, 40000);
                        const float d = 340.0f + wow * (220.0f * (0.7f + 0.3f * wb) * lfo(st.phase1, 0.55f * (1.0f + 0.25f * wa)) +
                                                         40.0f * sinf(4.0f * kPi * st.phase1) + 50.0f * wa);
                        const float pos = (float)st.write - d;
                        int i0 = (int)floorf(pos);
                        const float fr = pos - (float)i0;
                        const int a = i0 & kDelayMask, b = (i0 + 1) & kDelayMask;
                        xl = st.delay[a] + (st.delay[b] - st.delay[a]) * fr;
                        xr = st.delay[kDelayFrames + a] + (st.delay[kDelayFrames + b] - st.delay[kDelayFrames + a]) * fr;
                        st.write = (st.write + 1) & kDelayMask;
                    }
                    // wear: narrower, duller, no deep lows
                    const float m = 0.5f * (xl + xr), sd = 0.5f * (xl - xr) * narrow;
                    xl = m + sd;
                    xr = m - sd;
                    st.lpL += (xl - st.lpL) * tone;
                    st.lpR += (xr - st.lpR) * tone;
                    st.hpL += (st.lpL - st.hpL) * hp;
                    st.hpR += (st.lpR - st.hpR) * hp;
                    float yl = st.lpL - (wear > 0.0f ? st.hpL : 0.0f), yr = st.lpR - (wear > 0.0f ? st.hpR : 0.0f);
                    // crackle and pops
                    if (crackle > 0.0f)
                    {
                        const uint32_t roll = xorshift(st.rng) & 0xFFFFFF;
                        if (roll < clickChance)
                            st.click += (0.02f + 0.12f * ((xorshift(st.rng) & 0xFFFF) / 65536.0f)) *
                                        ((xorshift(st.rng) & 1) ? 1.0f : -1.0f);
                        else if (roll > 0xFFFFFF - popChance)
                            st.pop += 0.25f;
                        const float c = (st.click + st.pop * 0.5f) * noiseGain;
                        st.click *= -0.55f;
                        st.pop *= 0.93f;
                        yl += c;
                        yr += c * 0.9f;
                    }
                    // rumble and surface noise
                    if (nz > 0.0f)
                    {
                        const float w = noise(st.rng);
                        st.noiseLp += (w - st.noiseLp) * rumbleLp;
                        const float v = (st.noiseLp * nz * 0.08f + (w - st.noiseHp) * nz * 0.004f) * noiseGain;
                        st.noiseHp = w;
                        yl += v;
                        yr += v;
                    }
                    l[i] = yl;
                    r[i] = yr;
                }
                break;
            }
            case kColourLofi:
            {
                const float rate = kRateHz[sl.p[0] < 6 ? sl.p[0] : 0] / kSr;
                const int bits = kBitsN[sl.p[1] < 6 ? sl.p[1] : 0];
                const float q = (float)(1 << (bits - 1));
                const float c = onePole(kLofiFilterHz[sl.p[2] < 4 ? sl.p[2] : 0]);
                for (int i = 0; i < n; i++)
                {
                    st.holdPhase += rate;
                    if (st.holdPhase >= 1.0f)
                    {
                        st.holdPhase -= 1.0f;
                        st.holdL = l[i];
                        st.holdR = r[i];
                    }
                    float yl = st.holdL, yr = st.holdR;
                    if (bits < 16)
                    {
                        yl = floorf(yl * q + 0.5f) / q;
                        yr = floorf(yr * q + 0.5f) / q;
                    }
                    if (c < 1.0f)
                    {
                        st.lpL += (yl - st.lpL) * c;
                        st.lpR += (yr - st.lpR) * c;
                        st.lp2L += (st.lpL - st.lp2L) * c;
                        st.lp2R += (st.lpR - st.lp2R) * c;
                        yl = st.lp2L;
                        yr = st.lp2R;
                    }
                    l[i] = yl;
                    r[i] = yr;
                }
                break;
            }
            case kColourWidth:
            {
                const float width = (float)(sl.p[0] < 11 ? sl.p[0] : 5) * 0.2f;
                const float c = onePole(kMonoBassHz[sl.p[1] < 4 ? sl.p[1] : 0]);
                const bool mono = sl.p[1] > 0;
                // wider than 100%: most Mini samples are mono (no sides to widen), so
                // the sides also get a short, bass-free echo of the middle (14 ms)
                const float spread = width > 1.0f ? (width - 1.0f) * 0.7f : 0.0f;
                const float hp = onePole(350.0f);
                const int haas = 617;
                for (int i = 0; i < n; i++)
                {
                    const float m = 0.5f * (l[i] + r[i]);
                    float sd = 0.5f * (l[i] - r[i]);
                    sd *= width;
                    if (st.delay && spread > 0.0f)
                    {
                        st.delay[st.write] = m;
                        const float late = st.delay[(st.write - haas) & kDelayMask];
                        st.write = (st.write + 1) & kDelayMask;
                        st.noiseLp += (late - st.noiseLp) * hp;
                        sd += (late - st.noiseLp) * spread;
                    }
                    if (mono)
                    {
                        st.sideLp += (sd - st.sideLp) * c;
                        sd -= st.sideLp; // the bass in the middle
                    }
                    l[i] = m + sd;
                    r[i] = m - sd;
                }
                break;
            }
            default: break;
        }
    }

    // glue: a bus compressor with a genre preset; the limiter after it catches the rest
    if (cfg.glue > 0)
    {
        const GlueStyleInfo& gs = glueStyleInfo(cfg.glueStyle);
        const float thresholdDb = -2.4f * (float)cfg.glue;
        const float slope = 1.0f - 1.0f / gs.ratio;
        const float makeupDb = -thresholdDb * slope * 0.4f;
        const float att = 1.0f - expf(-1.0f / (kSr * gs.attackMs * 0.001f));
        const float rel = 1.0f - expf(-1.0f / (kSr * gs.releaseMs * 0.001f));
        const float hp = onePole(gs.detectorHpHz);
        float grMax = 0.0f;
        // the detector runs every sample; the gain is worked out every 16
        // samples and slides there (log and pow are too slow per sample)
        for (int i0 = 0; i0 < n; i0 += 16)
        {
            const int i1 = i0 + 16 < n ? i0 + 16 : n;
            for (int i = i0; i < i1; i++)
            {
                // the detector hears the mix without its lowest bass
                float dl = l[i], dr = r[i];
                if (hp < 1.0f)
                {
                    s.glueLpL += (dl - s.glueLpL) * hp;
                    s.glueLpR += (dr - s.glueLpR) * hp;
                    dl -= s.glueLpL;
                    dr -= s.glueLpR;
                }
                const float x = fabsf(dl) > fabsf(dr) ? fabsf(dl) : fabsf(dr);
                s.glueEnv += (x - s.glueEnv) * (x > s.glueEnv ? att : rel);
            }
            const float envDb = 20.0f * log10f(s.glueEnv + 1e-9f);
            float over = envDb - thresholdDb;
            float grDb = 0.0f;
            if (over > -3.0f)
            {
                // 6 dB soft knee
                over = over < 3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : over;
                grDb = over * slope;
            }
            const float target = powf(10.0f, (makeupDb - grDb) / 20.0f);
            const float step = (target - s.glueGain) / (float)(i1 - i0);
            for (int i = i0; i < i1; i++)
            {
                s.glueGain += step;
                l[i] *= s.glueGain;
                r[i] *= s.glueGain;
            }
            grMax = grDb > grMax ? grDb : grMax;
        }
        glueGrDb = grMax;
    }
    else
    {
        glueGrDb = 0.0f;
    }

    // never let a colour effect leave bad values
    for (int i = 0; i < n; i++)
    {
        if (!(l[i] == l[i]) || l[i] > 8.0f || l[i] < -8.0f)
            l[i] = 0.0f;
        if (!(r[i] == r[i]) || r[i] > 8.0f || r[i] < -8.0f)
            r[i] = 0.0f;
    }
}

} // namespace master
} // namespace gibgob
