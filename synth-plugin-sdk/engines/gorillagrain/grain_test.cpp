// GORILLAGRAIN DSP tests (host): make grain_test
#include "gorillagrain/grain_dsp.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static std::vector<int16_t> sine(float hz, int frames, int channels)
{
    std::vector<int16_t> d((size_t)frames * channels);
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < channels; c++)
            d[(size_t)i * channels + c] = (int16_t)(std::sin(2.0 * M_PI * hz * i / 44100.0) * 16000.0);
    return d;
}

struct Run
{
    std::vector<float> l, r;
    bool finite = true;
    float peak = 0.0f;
};

static Run play(grain::Params& p, const grain::Source& s, float note, int heldBlocks, int releaseBlocks,
                uint32_t seed = 1)
{
    grain::Voice v;
    v.init(seed);
    v.noteOn(p, s, note, 1.0f);
    Run out;
    float l[128], r[128];
    for (int b = 0; b < heldBlocks + releaseBlocks; b++)
    {
        if (b == heldBlocks)
            v.noteOff();
        v.render(p, 120.0f, 0.0f, l, r, 128);
        for (int i = 0; i < 128; i++)
        {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i]))
                out.finite = false;
            out.peak = std::fmax(out.peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
            out.l.push_back(l[i]);
            out.r.push_back(r[i]);
        }
    }
    return out;
}

static float rms(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; i++)
        s += x[i] * x[i];
    return (float)std::sqrt(s / (double)(to - from));
}

// strongest frequency between lo and hi (Goertzel scan)
static float pitchOf(const std::vector<float>& x, size_t from, size_t n, float lo, float hi)
{
    float best = 0, bestHz = 0;
    for (float hz = lo; hz <= hi; hz += 0.25f)
    {
        const double w = 2.0 * M_PI * hz / 44100.0, c = 2.0 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (size_t i = 0; i < n; i++)
        {
            const double s0 = x[from + i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float pw = (float)(s1 * s1 + s2 * s2 - c * s1 * s2);
        if (pw > best)
        {
            best = pw;
            bestHz = hz;
        }
    }
    return bestHz;
}

int main()
{
    auto mono = sine(220.0f, 44100 * 2, 1);
    grain::Source s;
    s.data = mono.data();
    s.frames = (uint32_t)(mono.size());
    s.channels = 1;
    s.rootNote = 57.0f; // A3 = 220 Hz

    // T1: the root note plays at the recorded pitch; a fifth up is a fifth up
    {
        grain::Params p;
        p.jitter = 0.0f;
        p.amp.attack = 0.001f;
        p.density = 1.2f; // long single grains: the pitch inside a grain
        p.sizeMs = 800.0f;
        p.spray = 0.0f;
        p.window = grain::kWindowSoft;
        Run a = play(p, s, 57.0f, 400, 10);
        CHECK(a.finite);
        const float hz = pitchOf(a.l, 8820, 32768, 150.0f, 500.0f);
        CHECK(std::fabs(hz - 220.0f) < 3.0f);
        Run b = play(p, s, 64.0f, 400, 10);
        const float hz2 = pitchOf(b.l, 8820, 32768, 150.0f, 500.0f);
        CHECK(std::fabs(hz2 - 329.6f) < 4.0f);
        printf("pitch: %.1f Hz and %.1f Hz\n", hz, hz2);
    }

    // T2: steady level whatever the density / size (the overlap is evened out)
    {
        float levels[3];
        const float dens[3] = { 5.0f, 22.0f, 90.0f };
        const float size[3] = { 400.0f, 140.0f, 40.0f };
        for (int k = 0; k < 3; k++)
        {
            grain::Params p;
            p.density = dens[k];
            p.sizeMs = size[k];
            p.amp.attack = 0.001f;
            Run a = play(p, s, 57.0f, 400, 0, 3);
            levels[k] = rms(a.l, 22050, a.l.size());
            CHECK(a.finite && a.peak < 1.6f);
            CHECK(levels[k] > 0.05f);
        }
        printf("levels: %.3f %.3f %.3f\n", levels[0], levels[1], levels[2]);
        CHECK(levels[2] < levels[1] * 2.5f && levels[1] < levels[0] * 2.5f);
        CHECK(levels[0] < levels[1] * 2.5f);
    }

    // T3: released, it falls silent and the voice ends
    {
        grain::Params p;
        p.amp.release = 0.2f;
        grain::Voice v;
        v.init(5);
        v.noteOn(p, s, 57.0f, 0.8f);
        float l[128], r[128];
        for (int b = 0; b < 100; b++)
            v.render(p, 120, 0, l, r, 128);
        v.noteOff();
        int blocks = 0;
        while (v.active() && blocks < 2000)
        {
            v.render(p, 120, 0, l, r, 128);
            blocks++;
        }
        CHECK(!v.active());
        CHECK(blocks < 200); // 0.2 s release ends well within 0.6 s
    }

    // T4: every window, reverse, sparkle, stereo source, extreme settings, modulation: clean
    {
        auto st = sine(330.0f, 30000, 2);
        grain::Source s2 = s;
        s2.data = st.data();
        s2.frames = 30000;
        s2.channels = 2;
        s2.rateRatio = 48000.0f / 44100.0f;
        for (int w = 0; w < grain::kWindowCount; w++)
            for (int f = 0; f < grain::kFilterTypeCount; f++)
            {
                grain::Params p;
                p.window = (uint8_t)w;
                p.filterType = (uint8_t)f;
                p.reverse = 0.5f;
                p.sparkle = 0.5f;
                p.spray = 1.0f;
                p.jitter = 1.0f;
                p.scan = -2.0f;
                p.density = 100.0f;
                p.sizeMs = 1000.0f;
                p.resonance = 1.0f;
                p.lfo[0].slot[0] = { grain::kDestPosition, 1.0f };
                p.lfo[0].slot[1] = { grain::kDestSize, -1.0f };
                p.lfo[1].rateHz = 30.0f;
                p.lfo[1].slot[0] = { grain::kDestPitch, 1.0f };
                p.lfo[1].slot[1] = { grain::kDestDensity, 1.0f };
                Run a = play(p, (w & 1) ? s2 : s, 96.0f, 150, 30, (uint32_t)(w * 7 + f));
                CHECK(a.finite);
                CHECK(a.peak < 4.0f);
            }
    }

    // T5: no sample: silent, no crash, the voice still ends
    {
        grain::Params p;
        grain::Source none;
        Run a = play(p, none, 60.0f, 20, 400);
        CHECK(a.finite && a.peak == 0.0f);
    }

    // T6: Scan 0 freezes the playhead; Scan 1 moves through the sample in real time
    {
        grain::Params p;
        p.scan = 0.0f;
        p.lfo[0].slot[0].dest = grain::kDestOff;
        grain::Voice v;
        v.init(9);
        v.noteOn(p, s, 57.0f, 1.0f);
        float l[128], r[128];
        v.render(p, 120, 0, l, r, 128);
        const float h0 = v.playhead();
        for (int b = 0; b < 344; b++)
            v.render(p, 120, 0, l, r, 128);
        CHECK(std::fabs(v.playhead() - h0) < 1e-4f);
        p.scan = 1.0f;
        for (int b = 0; b < 344; b++) // ~1 s of a 2 s sample
            v.render(p, 120, 0, l, r, 128);
        const float moved = v.playhead() - h0;
        printf("scan 1 for 1 s moved the playhead %.3f of the sample\n", moved);
        CHECK(std::fabs(moved - 0.5f) < 0.02f);
    }

    // T6b: the grain view: grains near the playhead, levels and pans in range
    {
        grain::Params p;
        p.spray = 0.0f;
        p.lfo[0].slot[0].dest = grain::kDestOff;
        p.scan = 0.0f;
        p.position = 0.5f;
        grain::Voice v;
        v.init(4);
        v.noteOn(p, s, 57.0f, 1.0f);
        float l[128], r[128];
        for (int b = 0; b < 40; b++)
            v.render(p, 120, 0, l, r, 128);
        float pos[12], lvl[12], pan[12];
        const int n = v.grainView(pos, lvl, pan, 12);
        CHECK(n >= 1 && n <= 12);
        for (int i = 0; i < n; i++)
        {
            CHECK(pos[i] >= 0.49f && pos[i] < 0.58f); // from the frozen playhead, plus up to 140 ms of travel (7 % of 2 s)
            CHECK(lvl[i] >= 0.0f && lvl[i] <= 1.0f);
            CHECK(pan[i] >= -1.01f && pan[i] <= 1.01f);
        }
        CHECK(v.grainView(pos, lvl, pan, 2) <= 2);
    }

    // T7: the instrument FX take the grain settings (shimmer reverb on by default)
    {
        grain::Params p;
        morph::Params fx;
        p.fillFx(fx);
        CHECK(fx.reverbType == morph::kReverbShimmer && fx.delayType == morph::kDelayOff);
    }

    // T8: a broken number fed to the shimmer reverb heals at once, the sound comes back
    {
        grain::Params p;
        morph::Params fxp;
        p.fillFx(fxp);
        std::vector<uint8_t> mem(morph::Fx::kMemoryBytes);
        morph::Fx fx;
        fx.attach(mem.data());
        float mod[4] = { 0, 0, 0, 0 };
        float l[128], r[128];
        for (int b = 0; b < 50; b++)
        {
            for (int i = 0; i < 128; i++)
                l[i] = r[i] = 0.3f * std::sin(i * 0.1f + b);
            if (b == 10)
            {
                l[5] = std::numeric_limits<float>::quiet_NaN();
                r[7] = std::numeric_limits<float>::infinity();
            }
            fx.process(fxp, 120.0f, mod, l, r, 128);
            for (int i = 0; i < 128; i++)
                CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
        }
        float peak = 0;
        for (int i = 0; i < 128; i++)
            peak = std::fmax(peak, std::fabs(l[i]));
        CHECK(peak > 0.05f); // still sounding after the bad block
        // and a tank already full of NaN (memory spoiled behind its back) repairs itself
        float* f = (float*)mem.data();
        for (int i = 0; i < 2000; i++)
            f[i] = std::numeric_limits<float>::quiet_NaN();
        const uint32_t before = fx.repairs;
        for (int b = 0; b < 12; b++) // the poison reaches the output within a few blocks
        {
            for (int i = 0; i < 128; i++)
                l[i] = r[i] = 0.3f * std::sin(i * 0.1f);
            fx.process(fxp, 120.0f, mod, l, r, 128);
        }
        CHECK(fx.repairs > before);
        peak = 0;
        for (int i = 0; i < 128; i++)
        {
            CHECK(std::isfinite(l[i]));
            peak = std::fmax(peak, std::fabs(l[i]));
        }
        CHECK(peak > 0.05f);
    }

    printf(failures ? "FAILED: %d\n" : "ALL GORILLAGRAIN TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
