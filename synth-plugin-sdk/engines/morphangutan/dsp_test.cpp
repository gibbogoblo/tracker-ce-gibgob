// Host tests for the MORPHANGUTAN DSP core.
//   g++ -O2 -std=c++20 dsp_test.cpp -o dsp_test && ./dsp_test

#include "morph_dsp.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace morph;

static int failures = 0;
#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static Params cleanParams()
{
    Params p;
    p.mamaLevel = 1.0f;
    p.babyLevel = 0.0f;
    p.papaLevel = 0.0f;
    p.noiseLevel = 0.0f;
    p.drift = 0.0f;
    p.filterType = kFilterLp12;
    p.cutoff = 20000.0f;
    p.resonance = 0.0f;
    p.keyTrack = 0.0f;
    p.amp = { 0.0005f, 0.1f, 1.0f, 0.1f };
    p.velToAmp = 0.0f;
    p.env[0].slot[0] = {};
    return p;
}

static std::vector<float> renderVoice(const Params& p, float note, int samples, int releaseAt = -1)
{
    static Voice v;
    v = Voice();
    v.init(1);
    v.noteOn(p, note, 1.0f, false);
    std::vector<float> out(samples);
    float l[128], r[128];
    for (int pos = 0; pos < samples; pos += 128)
    {
        if (releaseAt >= 0 && pos >= releaseAt && pos < releaseAt + 128)
        {
            v.noteOff();
        }
        const int n = std::min(128, samples - pos);
        v.render(p, 120.0f, 0.0f, l, r, n);
        for (int i = 0; i < n; ++i)
        {
            out[pos + i] = 0.5f * (l[i] + r[i]);
        }
    }
    return out;
}

// amplitude of harmonic h of frequency f (Goertzel over a whole number of cycles)
static float harmonic(const std::vector<float>& x, int from, int len, float f)
{
    std::complex<double> acc = 0;
    for (int i = 0; i < len; ++i)
    {
        const double ph = 2.0 * M_PI * f * i / kSampleRate;
        acc += (double)x[from + i] * std::complex<double>(cos(ph), -sin(ph));
    }
    return (float)(2.0 * std::abs(acc) / len);
}

// pitch from the autocorrelation peak
static float pitchOf(const std::vector<float>& x, int from, int len, float lo, float hi)
{
    // shortest lag whose correlation is a local peak within 90 % of the best one
    const int lo_ = (int)(kSampleRate / hi), hi_ = (int)(kSampleRate / lo) + 1;
    std::vector<double> acv(hi_ + 2, 0.0);
    double best = -1e30;
    for (int lag = lo_ - 1; lag <= hi_ + 1; ++lag)
    {
        double s = 0;
        for (int i = 0; i < len; ++i)
        {
            s += (double)x[from + i] * x[from + i + lag];
        }
        acv[lag] = s;
        if (lag >= lo_ && lag <= hi_)
        {
            best = std::max(best, s);
        }
    }
    int bestLag = lo_;
    for (int lag = lo_; lag <= hi_; ++lag)
    {
        if (acv[lag] >= 0.9 * best && acv[lag] >= acv[lag - 1] && acv[lag] >= acv[lag + 1])
        {
            bestLag = lag;
            break;
        }
    }
    // parabolic refinement
    auto ac = [&](int lag)
    {
        double s = 0;
        for (int i = 0; i < len; ++i)
        {
            s += (double)x[from + i] * x[from + i + lag];
        }
        return s;
    };
    const double a = ac(bestLag - 1), b = ac(bestLag), c = ac(bestLag + 1);
    const double off = 0.5 * (a - c) / (a - 2 * b + c);
    return (float)(kSampleRate / (bestLag + off));
}

static void testShapes()
{
    // A4 = 440 Hz; analyse 0.5 s after the attack
    struct Expect
    {
        float morph;
        const char* name;
        float h2, h3; // relative to h1
        float tol;
    };
    const Expect ex[] = {
        { 0.0f, "sine", 0.0f, 0.0f, 0.01f },
        { 1.0f, "triangle", 0.0f, 1.0f / 9.0f, 0.02f },
        { 2.0f, "saw", 0.5f, 1.0f / 3.0f, 0.03f },
        { 3.0f, "square", 0.0f, 1.0f / 3.0f, 0.03f },
    };
    for (const Expect& e : ex)
    {
        Params p = cleanParams();
        p.mamaMorph = e.morph;
        auto x = renderVoice(p, 69.0f, 44100);
        const int len = 44100 / 2; // 220 whole cycles of 440 Hz... (22050 * 440 / 44100 = 220)
        const float h1 = harmonic(x, 8000, len, 440.0f);
        const float h2 = harmonic(x, 8000, len, 880.0f) / h1;
        const float h3 = harmonic(x, 8000, len, 1320.0f) / h1;
        printf("  %-8s h1 %.3f  h2/h1 %.3f  h3/h1 %.3f\n", e.name, h1, h2, h3);
        CHECK(std::fabs(h2 - e.h2) < e.tol, "%s h2 %.3f expected %.3f", e.name, h2, e.h2);
        CHECK(std::fabs(h3 - e.h3) < e.tol, "%s h3 %.3f expected %.3f", e.name, h3, e.h3);
        CHECK(h1 > 0.2f && h1 < 0.8f, "%s level %.3f", e.name, h1);
        const float f = pitchOf(x, 8000, 4000, 200, 1000);
        CHECK(std::fabs(f - 440.0f) < 1.0f, "%s pitch %.2f", e.name, f);
    }
    // in-between morphs never get louder than the shapes they blend
    for (float m = 0.0f; m <= 3.0f; m += 0.25f)
    {
        Params p = cleanParams();
        p.mamaMorph = m;
        auto x = renderVoice(p, 57.0f, 22050);
        float pk = 0;
        for (int i = 4000; i < 22050; ++i)
        {
            pk = std::max(pk, std::fabs(x[i]));
        }
        CHECK(pk < 0.62f, "morph %.2f peak %.3f", m, pk);
    }
}

static void testTuning()
{
    Params p = cleanParams();
    p.mamaMorph = 0.0f;
    p.mamaTune = 7.0f;
    p.mamaFine = 0.0f;
    auto x = renderVoice(p, 60.0f, 30000);
    const float f = pitchOf(x, 8000, 4000, 200, 600);
    CHECK(std::fabs(f - 392.0f) < 1.0f, "MAMA +7 st from C4: %.2f Hz, expected 392", f);

    // PAPA one octave down from MAMA
    p = cleanParams();
    p.mamaLevel = 0.0f;
    p.papaLevel = 1.0f;
    p.papaMorph = 0.0f;
    p.papaOctave = -1;
    x = renderVoice(p, 69.0f, 30000);
    const float fp = pitchOf(x, 8000, 4000, 100, 400);
    CHECK(std::fabs(fp - 220.0f) < 1.0f, "PAPA -1 oct: %.2f Hz", fp);
    p.papaOctave = -2;
    x = renderVoice(p, 69.0f, 30000);
    const float fp2 = pitchOf(x, 8000, 6000, 60, 200);
    CHECK(std::fabs(fp2 - 110.0f) < 0.5f, "PAPA -2 oct: %.2f Hz", fp2);
}

static void testComb()
{
    // a burst through Comb+ with 100 % key track rings at the note's pitch
    for (int note : { 48, 60, 67, 72 })
    {
        Params p = cleanParams();
        p.mamaLevel = 0.0f;
        p.noiseLevel = 0.0f;
        p.env[0].env = { 0.001f, 0.02f, 0.0f, 0.02f };
        p.env[0].slot[0] = { kDestNoiseVol, 1.0f };
        p.filterType = kFilterCombPlus;
        p.cutoff = 261.63f;
        p.keyTrack = 1.0f;
        p.resonance = 0.95f;
        auto x = renderVoice(p, (float)note, 30000);
        const float want = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
        const float f = pitchOf(x, 6000, 6000, want * 0.7f, want * 1.4f);
        const float cents = 1200.0f * std::log2(f / want);
        printf("  comb note %d: %.1f Hz (want %.1f, %+.0f cents)\n", note, f, want, cents);
        CHECK(std::fabs(cents) < 15.0f, "comb note %d off by %.0f cents", note, cents);
    }
}

static void testEnvelope()
{
    Params p = cleanParams();
    p.mamaMorph = 0.0f;
    p.amp = { 0.2f, 0.1f, 0.5f, 0.3f };
    auto x = renderVoice(p, 69.0f, 44100 * 2, 44100);
    // peak envelope per 10 ms
    auto envAt = [&](float t)
    {
        const int c = (int)(t * kSampleRate);
        float pk = 0;
        for (int i = c; i < c + 441; ++i)
        {
            pk = std::max(pk, std::fabs(x[i]));
        }
        return pk;
    };
    const float full = envAt(0.2f);
    const float sus = envAt(0.8f);
    printf("  env: 0.1s %.2f  0.2s %.2f  sustain %.2f  rel+0.15 %.2f  rel+0.35 %.3f\n",
           envAt(0.1f), full, sus, envAt(1.15f), envAt(1.35f));
    CHECK(envAt(0.1f) < full * 0.8f && envAt(0.1f) > full * 0.3f, "attack shape");
    CHECK(std::fabs(sus / full - 0.5f) < 0.08f, "sustain ratio %.2f", sus / full);
    CHECK(envAt(1.35f) < sus * 0.05f, "release did not finish");
}

// a new note over the last one's release still gets its whole attack
static void testRetriggerAttack()
{
    Params p = cleanParams();
    p.mamaMorph = 0.0f;
    p.amp = { 0.3f, 0.1f, 1.0f, 1.5f };
    static Voice v;
    v = Voice();
    v.init(1);
    std::vector<float> x(44100 * 2);
    float l[128], r[128];
    const int offAt = (int)(0.6f * kSampleRate), againAt = (int)(0.7f * kSampleRate);
    v.noteOn(p, 69.0f, 1.0f, false);
    for (int pos = 0; pos < (int)x.size(); pos += 128)
    {
        if (pos >= offAt && pos < offAt + 128)
            v.noteOff();
        if (pos >= againAt && pos < againAt + 128)
            v.noteOn(p, 72.0f, 1.0f, true); // even when the host calls it legato
        v.render(p, 120.0f, 0.0f, l, r, 128);
        for (int i = 0; i < 128 && pos + i < (int)x.size(); ++i)
            x[pos + i] = 0.5f * (l[i] + r[i]);
    }
    auto envAt = [&](float t)
    {
        const int c = (int)(t * kSampleRate);
        float pk = 0;
        for (int i = c; i < c + 441; ++i)
            pk = std::max(pk, std::fabs(x[i]));
        return pk;
    };
    const float full = envAt(0.5f);
    printf("  retrigger: before %.2f  tail %.2f  +10ms %.2f  +100ms %.2f  +300ms %.2f\n", full, envAt(0.69f),
           envAt(0.715f), envAt(0.8f), envAt(1.0f));
    CHECK(envAt(0.69f) > full * 0.5f, "the release tail should still be loud");
    CHECK(envAt(0.715f) < full * 0.25f, "the new note did not start from silence");
    CHECK(envAt(0.8f) < full * 0.8f, "the new note skipped its attack");
    CHECK(envAt(1.0f) > full * 0.85f, "the new note did not reach full level");
}

static void testFxStable()
{
    std::vector<uint8_t> mem(Fx::kMemoryBytes);
    for (int rt = kReverbPlate; rt < kReverbTypeCount; ++rt)
    {
        for (int dt = kDelayOff; dt < kDelayTypeCount; ++dt)
        {
            Fx fx;
            fx.attach(mem.data());
            Params p;
            p.reverbType = (uint8_t)rt;
            p.reverbDecay = 1.0f;
            p.reverbMix = 1.0f;
            p.reverbTone = 1.0f;
            p.delayType = (uint8_t)dt;
            p.delayFeedback = 0.95f;
            p.delayMix = 1.0f;
            p.delayTime = 0;
            const float mod[4] = { 0.0f, 0.5f, 0.0f, 0.5f }; // push fb + decay beyond max
            float l[128], r[128];
            Rng rng;
            float firstSec = 0, lastSec = 0, peak = 0;
            const int blocks = 44100 * 40 / 128;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const bool input = b < 344; // 1 s of loud noise, then silence
                    l[i] = input ? rng.bipolar() * 0.8f : 0.0f;
                    r[i] = input ? rng.bipolar() * 0.8f : 0.0f;
                }
                fx.process(p, 120.0f, mod, l, r, 128);
                for (int i = 0; i < 128; ++i)
                {
                    const float e = l[i] * l[i] + r[i] * r[i];
                    peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                    CHECK(std::isfinite(e), "NaN in fx");
                    if (b >= 344 && b < 344 + 345)
                    {
                        firstSec += e;
                    }
                    if (b >= blocks - 345)
                    {
                        lastSec += e;
                    }
                }
            }
            const float drop = 10.0f * std::log10((lastSec + 1e-12f) / (firstSec + 1e-12f));
            printf("  reverb %d delay %d: peak %.2f, after 40 s %.0f dB\n", rt, dt, peak, drop);
            CHECK(drop < -20.0f, "reverb %d delay %d does not die away (%.1f dB)", rt, dt, drop);
            CHECK(peak < 4.0f, "reverb %d delay %d too loud (%.2f)", rt, dt, peak);
        }
    }
}

static void testSyncedLfo()
{
    // LFO synced to 1 bar at 120 BPM -> 0.5 Hz: pan goes L -> R -> L in 2 s
    Lfo lfo;
    lfo.seed(1);
    lfo.retrigger();
    LfoParams lp;
    lp.shape = kLfoSawUp;
    lp.sync = 3; // 4 beats
    float v0 = lfo.advance(lp, 120.0f, 0.0f, 0);
    float v1 = lfo.advance(lp, 120.0f, 0.0f, 44100); // 1 s = half a bar
    CHECK(std::fabs(v0 + 1.0f) < 0.01f && std::fabs(v1) < 0.01f, "synced LFO %.3f %.3f", v0, v1);
}

int main()
{
    printf("shapes\n");
    testShapes();
    printf("tuning\n");
    testTuning();
    printf("comb\n");
    testComb();
    printf("envelope\n");
    testEnvelope();
    printf("retrigger\n");
    testRetriggerAttack();
    printf("lfo\n");
    testSyncedLfo();
    printf("fx stability (takes a moment)\n");
    testFxStable();
    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
