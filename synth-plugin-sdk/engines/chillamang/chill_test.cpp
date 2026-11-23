// CHILLAMANG DSP tests (on the computer):
//   g++ -O2 -std=c++20 -I.. chill_test.cpp -o t && ./t
#include "chillamang/chill_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace chill;

static int failures = 0;
#define CHECK(c, ...)                                                                                        \
    do                                                                                                       \
    {                                                                                                        \
        if (!(c))                                                                                            \
        {                                                                                                    \
            printf("FAIL line %d: %s  ", __LINE__, #c);                                                       \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

// one note, mono sum, `holdSec` held then released, `totalSec` long
static std::vector<float> note(const Params& p, int nn, float vel, float holdSec, float totalSec, bool* endedOut = nullptr)
{
    Voice v;
    v.init(7);
    v.noteOn(p, (float)nn, vel);
    const int total = (int)(totalSec * 44100), hold = (int)(holdSec * 44100);
    std::vector<float> out(total, 0.0f);
    bool off = false;
    for (int s = 0; s < total; s += 32)
    {
        if (!off && s >= hold)
        {
            v.noteOff(p);
            off = true;
        }
        float l[32] = { 0 }, r[32] = { 0 };
        const int n = std::min(32, total - s);
        v.render(p, 0.0f, l, r, n);
        for (int i = 0; i < n; ++i)
            out[s + i] = 0.5f * (l[i] + r[i]);
    }
    if (endedOut)
        *endedOut = !v.active();
    return out;
}

// brightness: share of energy above the fundamental's band (simple first difference vs signal)
static float brightness(const std::vector<float>& x, int from, int to)
{
    double e = 0, d = 0;
    for (int i = from + 1; i < to; ++i)
    {
        e += x[i] * x[i];
        const float dd = x[i] - x[i - 1];
        d += dd * dd;
    }
    return e > 0 ? (float)std::sqrt(d / e) : 0.0f;
}

static float rms(const std::vector<float>& x, int from, int to)
{
    double e = 0;
    for (int i = from; i < to; ++i)
        e += x[i] * x[i];
    return (float)std::sqrt(e / std::max(1, to - from));
}

// amplitude of a frequency (one bin of a DFT)
static float bin(const std::vector<float>& x, int from, int n, float hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * M_PI * hz * i / 44100.0;
        // Hann window
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        re += x[from + i] * w * std::cos(ph);
        im += x[from + i] * w * std::sin(ph);
    }
    return (float)std::sqrt(re * re + im * im) * 2.0f / n;
}

int main()
{
    Params p;

    // a note sounds, stays finite, and its numbers are sane
    {
        bool ended = false;
        auto x = note(p, 60, 0.8f, 1.0f, 2.5f, &ended);
        float peak = 0;
        bool finite = true;
        for (float s : x)
        {
            peak = std::max(peak, std::fabs(s));
            finite = finite && std::isfinite(s);
        }
        CHECK(finite, "broken numbers");
        CHECK(peak > 0.05f && peak < 0.6f, "peak %.3f", peak);
        CHECK(ended, "still playing 1.5 s after the key was let go");
        // no DC left in the ringing part
        double mean = 0;
        for (int i = 4410; i < 44100; ++i)
            mean += x[i];
        mean /= (44100 - 4410);
        CHECK(std::fabs(mean) < 0.002, "DC %.4f", mean);
        // the start does not click: it begins at silence and rises over the first ms
        float early = 0, jump = 0;
        for (int i = 0; i < 4; ++i)
            early = std::max(early, std::fabs(x[i]));
        {
            Params q = p;
            q.hammer = 0.0f; // the knock is noise and the ping a high tone on purpose: look at the body alone
            q.tineLevel = 0.0f;
            auto t = note(q, 60, 0.8f, 0.1f, 0.1f);
            // a click = the start moves much faster than the tone does later
            float later = 0;
            for (int i = 1; i < 220; ++i)
                jump = std::max(jump, std::fabs(t[i] - t[i - 1]));
            for (int i = 2205; i < 2425; ++i)
                later = std::max(later, std::fabs(t[i] - t[i - 1]));
            jump /= std::max(later, 1e-6f);
        }
        CHECK(early < peak * 0.05f, "first samples %.3f (peak %.3f)", early, peak);
        CHECK(jump < 1.5f, "the start jumps %.2fx faster than the tone later", jump);
        // after the damper: silence
        CHECK(rms(x, (int)(2.2 * 44100), (int)(2.5 * 44100)) < 1e-4f, "tail %.6f", rms(x, (int)(2.2 * 44100), (int)(2.5 * 44100)));
    }

    // in tune: A4 has its strongest component at 440 Hz (not 439 / 441 off by much)
    {
        Params q = p;
        q.tineLevel = 0.0f;
        q.beat = 0.0f;
        q.hammer = 0.0f;
        auto x = note(q, 69, 0.5f, 1.0f, 1.0f);
        const float a440 = bin(x, 8820, 16384, 440.0f);
        const float a430 = bin(x, 8820, 16384, 430.0f), a450 = bin(x, 8820, 16384, 450.0f);
        CHECK(a440 > 3.0f * a430 && a440 > 3.0f * a450, "440 %.4f 430 %.4f 450 %.4f", a440, a430, a450);
        // the tune knob: +12 = an octave up
        q.tune = 12.0f;
        auto y = note(q, 57, 0.5f, 1.0f, 1.0f);
        CHECK(bin(y, 8820, 16384, 440.0f) > 3.0f * bin(y, 8820, 16384, 220.0f), "tune +12 is not an octave");
    }

    // harder = brighter, and louder
    {
        auto soft = note(p, 60, 0.25f, 1.0f, 1.0f);
        auto hard = note(p, 60, 0.95f, 1.0f, 1.0f);
        const float bs = brightness(soft, 441, 4410), bh = brightness(hard, 441, 4410);
        CHECK(bh > bs * 1.4f, "brightness soft %.3f hard %.3f", bs, bh);
        CHECK(rms(hard, 441, 8820) > rms(soft, 441, 8820) * 1.5f, "hard is not louder");
        // and it mellows as it rings
        CHECK(brightness(hard, 441, 2205) > brightness(hard, 30870, 35280) * 1.2f, "does not mellow");
    }

    // bark: a hard hit adds overtones that a soft one lacks (relative to the fundamental)
    {
        Params q = p;
        q.bark = 1.0f;
        q.tineLevel = 0.0f;
        q.hammer = 0.0f;
        q.brightness = 16000.0f; // keep the overtones to measure them
        auto soft = note(q, 48, 0.2f, 1.0f, 1.0f);
        auto hard = note(q, 48, 1.0f, 1.0f, 1.0f);
        const float f = 130.81f;
        const float rs = (bin(soft, 2205, 8192, 2 * f) + bin(soft, 2205, 8192, 3 * f)) / bin(soft, 2205, 8192, f);
        const float rh = (bin(hard, 2205, 8192, 2 * f) + bin(hard, 2205, 8192, 3 * f)) / bin(hard, 2205, 8192, f);
        CHECK(rh > rs * 1.5f, "overtones soft %.3f hard %.3f", rs, rh);
        if (getenv("SHOW")) printf("bark soft %.3f hard %.3f\n", rs, rh);
    }

    // the tine tone: glassy = the inharmonic ping (7.11 x the note), round =
    // a pure partial two octaves up with no clang left
    {
        Params q = p;
        q.tineLevel = 1.0f;
        q.tineDecay = 0.8f;
        q.body = 0.0f;
        q.bark = 0.0f;
        q.hammer = 0.0f;
        q.brightness = 16000.0f;
        q.warmth = 0.0f;
        const float f = 261.63f;
        q.tineTone = 0.0f;
        auto glassy = note(q, 60, 0.9f, 1.0f, 0.5f);
        q.tineTone = 1.0f;
        auto round = note(q, 60, 0.9f, 1.0f, 0.5f);
        const float gClang = bin(glassy, 220, 4096, 7.11f * f), gPure = bin(glassy, 220, 4096, 4.0f * f);
        const float rClang = bin(round, 220, 4096, 7.11f * f), rPure = bin(round, 220, 4096, 4.0f * f);
        CHECK(gClang > gPure, "glassy: clang %.4f pure %.4f", gClang, gPure);
        CHECK(rPure > 20.0f * rClang, "round: clang %.5f pure %.4f", rClang, rPure);
        // and the round ping swells in instead of striking
        CHECK(brightness(round, 0, 220) < brightness(glassy, 0, 220) * 0.8f, "round attack %.3f glassy %.3f",
              brightness(round, 0, 220), brightness(glassy, 0, 220));
    }

    // warmth: exact and identical at 0; at 1 no two hits alike, and darker
    {
        Params q = p;
        q.warmth = 0.0f;
        q.age = 0.0f;    // its wobble runs on between hits
        q.hammer = 0.0f; // the knock is noise
        auto a0 = note(q, 60, 0.7f, 1.0f, 0.6f);
        Voice v;
        v.init(7);
        auto hit = [&](Voice& vo, const Params& pp)
        {
            vo.noteOn(pp, 60.0f, 0.7f);
            std::vector<float> x(13230);
            for (int s0 = 0; s0 < 13230; s0 += 30)
            {
                float l[30] = { 0 }, r[30] = { 0 };
                vo.render(pp, 0.0f, l, r, 30);
                for (int i = 0; i < 30; ++i)
                    x[s0 + i] = l[i] + r[i];
            }
            vo.noteOff(pp);
            for (int k = 0; k < 2000 && vo.active(); ++k)
            {
                float l[32] = { 0 }, r[32] = { 0 };
                vo.render(pp, 0.0f, l, r, 32);
            }
            return x;
        };
        auto diff = [](const std::vector<float>& x, const std::vector<float>& y)
        {
            double d = 0, e = 0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                d += (x[i] - y[i]) * (x[i] - y[i]);
                e += x[i] * x[i];
            }
            return (float)std::sqrt(d / std::max(e, 1e-12));
        };
        auto h1 = hit(v, q), h2 = hit(v, q);
        CHECK(diff(h1, h2) < 0.02f, "warmth 0: two hits differ by %.3f", diff(h1, h2));
        Params w = q;
        w.warmth = 1.0f;
        auto w1 = hit(v, w), w2 = hit(v, w);
        CHECK(diff(w1, w2) > 0.05f, "warmth 1: two hits alike (%.3f)", diff(w1, w2));
        auto a1 = note(w, 60, 0.7f, 1.0f, 0.6f);
        // the upper harmonics (5th to 12th) against the fundamental
        auto top = [&](const std::vector<float>& x)
        {
            float t = 0;
            for (int k = 5; k <= 12; ++k)
                t += bin(x, 2205, 8192, 261.63f * k);
            return t / bin(x, 2205, 8192, 261.63f);
        };
        CHECK(top(a1) < top(a0) * 0.7f, "warmth: upper harmonics %.4f vs %.4f", top(a1), top(a0));
    }

    // low notes ring longer than high ones (held, no release)
    {
        auto lo = note(p, 36, 0.7f, 10.0f, 6.0f);
        auto hi = note(p, 84, 0.7f, 10.0f, 6.0f);
        auto ratio = [](const std::vector<float>& x) { return rms(x, 4 * 44100, (int)(4.5 * 44100)) / rms(x, 4410, 8820); };
        CHECK(ratio(lo) > ratio(hi) * 2.0f, "low %.4f high %.4f", ratio(lo), ratio(hi));
        // the Decay knob makes notes ring longer
        Params q = p;
        q.decay = 1.0f;
        auto longer = note(q, 60, 0.7f, 10.0f, 6.0f);
        auto normal = note(p, 60, 0.7f, 10.0f, 6.0f);
        CHECK(ratio(longer) > ratio(normal) * 1.3f, "decay knob");
    }

    // the release knob: longer release = longer tail
    {
        Params q = p;
        q.release = 1.0f;
        auto x = note(q, 60, 0.7f, 0.5f, 1.5f);
        auto y = note(p, 60, 0.7f, 0.5f, 1.5f);
        CHECK(rms(x, (int)(0.65 * 44100), (int)(0.8 * 44100)) > rms(y, (int)(0.65 * 44100), (int)(0.8 * 44100)) * 2.0f, "release");
    }

    // felt: a softer attack (less of the first 3 ms' edge)
    {
        Params q = p;
        q.felt = 1.0f;
        auto f = note(q, 60, 0.9f, 0.5f, 0.5f);
        auto h = note(p, 60, 0.9f, 0.5f, 0.5f);
        CHECK(rms(f, 0, 132) < rms(h, 0, 132) * 0.8f, "felt attack %.4f hammer %.4f", rms(f, 0, 132), rms(h, 0, 132));
    }

    // the tremolo moves the sound between left and right
    {
        static uint8_t mem[ModFx::kMemoryBytes];
        ModFx m;
        m.attach(mem);
        Params q = p;
        q.tremDepth = 1.0f;
        q.tremRate = 4.0f;
        q.chorusMix = 0.0f;
        std::vector<float> l(44100, 0.3f), r(44100, 0.3f);
        for (int s = 0; s < 44100; s += 64)
            m.process(q, 120.0f, &l[s], &r[s], std::min(64, 44100 - s));
        // a quarter of a cycle apart, the louder side changes
        float minL = 1, maxL = 0;
        for (int i = 0; i < 44100; ++i)
        {
            minL = std::min(minL, l[i]);
            maxL = std::max(maxL, l[i]);
        }
        CHECK(maxL > 0.28f && minL < 0.03f, "left swings %.3f..%.3f", minL, maxL);
        bool opposite = true;
        for (int i = 0; i < 44100; i += 997)
            if (std::fabs(l[i] + r[i] - 0.3f) > 0.02f && std::fabs((l[i] + r[i]) - 0.3f) > 0.02f)
                opposite = false; // with full depth L + R stays the same (one goes down as the other comes up)
        CHECK(opposite, "left and right do not swap");
    }

    // chorus / phaser / tremolo survive garbage and stay finite
    {
        static uint8_t mem[ModFx::kMemoryBytes];
        ModFx m;
        m.attach(mem);
        Params q = p;
        q.chorusMix = 1.0f;
        q.phaserMix = 1.0f;
        q.phaserFeedback = 0.9f;
        std::vector<float> l(4096), r(4096);
        uint32_t st = 1;
        for (int k = 0; k < 4096; ++k)
        {
            st = st * 1664525u + 1013904223u;
            l[k] = ((int)(st >> 8) % 2000 - 1000) / 1000.0f;
            r[k] = -l[k];
        }
        l[100] = NAN;
        for (int s = 0; s < 4096; s += 64)
            m.process(q, 120.0f, &l[s], &r[s], 64);
        std::vector<float> a(4096, 0.1f), b(4096, 0.1f);
        for (int s = 0; s < 4096; s += 64)
            m.process(q, 120.0f, &a[s], &b[s], 64);
        bool finite = true;
        for (int k = 2048; k < 4096; ++k)
            finite = finite && std::isfinite(a[k]) && std::isfinite(b[k]);
        CHECK(finite, "the effects stay broken after a bad number");
    }

    // a key hit again while it rings: no click (phase kept)
    {
        Voice v;
        v.init(3);
        v.noteOn(p, 60, 0.7f);
        std::vector<float> x;
        for (int s = 0; s < 8820; s += 32)
        {
            if (s == 4416)
                v.noteOn(p, 60, 0.7f);
            float l[32] = { 0 }, r[32] = { 0 };
            v.render(p, 0.0f, l, r, 32);
            for (int i = 0; i < 32; ++i)
                x.push_back(l[i]);
        }
        float jump = 0, peak = 0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            jump = std::max(jump, std::fabs(x[i] - x[i - 1]));
            peak = std::max(peak, std::fabs(x[i]));
        }
        CHECK(jump < peak * 0.4f, "retrigger jump %.3f peak %.3f", jump, peak);
    }

    printf(failures ? "CHILLAMANG TESTS FAILED: %d\n" : "ALL CHILLAMANG TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
