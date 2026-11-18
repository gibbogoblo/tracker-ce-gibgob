// Host tests + WAV demos for the GibGob performance effects.
#include "gibgob/PerformFx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace gibgob::perform;

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

static const float kSr = 44100.0f;

// ---------------------------------------------------------------- test music
struct Music
{
    float bpm;
    int frames;
    std::vector<float> l, r;
};

static float rnd()
{
    static uint32_t s = 22222;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(int32_t)s / 2147483648.0f;
}

// an amen-ish break + a chord pad
static Music makeJungle(float bpm, int bars)
{
    Music m;
    m.bpm = bpm;
    const float S = kSr * 60.0f / bpm / 4.0f;
    m.frames = (int)(S * 16 * bars);
    m.l.assign(m.frames, 0.0f);
    m.r.assign(m.frames, 0.0f);
    // kick K, snare S, ghost g, hat on every 1/8
    const char* pat = "K.K...S..gK.S..g" "..K...S..KK...S.";
    for (int step = 0; step < 16 * bars; step++)
    {
        const char c = pat[step % 32];
        const int start = (int)(step * S);
        auto add = [&](int len, auto fn)
        {
            for (int i = 0; i < len && start + i < m.frames; i++)
            {
                const float v = fn(i);
                m.l[start + i] += v;
                m.r[start + i] += v;
            }
        };
        if (c == 'K')
        {
            float ph = 0;
            add(9000, [&](int i) {
                const float t = i / kSr;
                ph += (50.0f + 120.0f * expf(-t * 30.0f)) / kSr;
                return 0.55f * sinf(6.2831853f * ph) * expf(-t * 9.0f);
            });
        }
        if (c == 'S' || c == 'g')
        {
            const float a = c == 'S' ? 0.4f : 0.12f;
            add(7000, [&](int i) {
                const float t = i / kSr;
                return a * (0.7f * rnd() * expf(-t * 18.0f) + 0.5f * sinf(6.2831853f * 190.0f * t) * expf(-t * 25.0f));
            });
        }
        if (step % 2 == 0)
            add(1800, [&](int i) {
                const float t = i / kSr;
                return 0.08f * rnd() * expf(-t * 60.0f);
            });
    }
    // pad: Am9 with slow drift, a bit to each side
    const float f[] = { 110.0f, 164.81f, 196.0f, 261.63f, 246.94f };
    for (int i = 0; i < m.frames; i++)
    {
        const float t = i / kSr;
        float v = 0;
        for (int k = 0; k < 5; k++)
            v += sinf(6.2831853f * f[k] * t * (1.0f + 0.002f * k)) * 0.035f;
        m.l[i] += v * 1.1f;
        m.r[i] += v * 0.9f;
    }
    return m;
}

// smooth signal: any jump in the read position shows as a click
static Music makePad(float bpm, int bars)
{
    Music m;
    m.bpm = bpm;
    const float S = kSr * 60.0f / bpm / 4.0f;
    m.frames = (int)(S * 16 * bars);
    m.l.resize(m.frames);
    m.r.resize(m.frames);
    for (int i = 0; i < m.frames; i++)
    {
        const float t = i / kSr;
        m.l[i] = 0.3f * sinf(6.2831853f * 220.0f * t) + 0.2f * sinf(6.2831853f * 331.0f * t);
        m.r[i] = 0.3f * sinf(6.2831853f * 221.0f * t) + 0.2f * sinf(6.2831853f * 330.0f * t);
    }
    return m;
}

// ---------------------------------------------------------------- runner
struct Press
{
    int cell;
    float fromStep, toStep; // in 1/16 steps of the song
};
struct TwistAt
{
    int cell;
    float atStep;
    int dir;
};
static std::vector<TwistAt> g_twists;

static std::vector<uint8_t> g_mem;

static void run(const Music& in, const Cell cells[8], const std::vector<Press>& presses, std::vector<float>& L,
                std::vector<float>& R, int extraFrames = 0)
{
    if (g_mem.size() != Engine::kMemoryBytes)
        g_mem.assign(Engine::kMemoryBytes, 0);
    static Engine* e = nullptr;
    delete e;
    e = new Engine();
    for (int i = 0; i < 8; i++)
        e->setCell(i, cells[i]);
    e->attach(g_mem.data());
    const float S = kSr * 60.0f / in.bpm / 4.0f;
    const int total = in.frames + extraFrames;
    L.assign(total, 0.0f);
    R.assign(total, 0.0f);
    for (int pos = 0; pos < total; pos += 128)
    {
        const int n = std::min(128, total - pos);
        uint8_t mask = 0;
        const float stepNow = pos / S;
        for (const Press& p : presses)
            if (stepNow >= p.fromStep && stepNow < p.toStep)
                mask |= (uint8_t)(1 << p.cell);
        e->setHeld(mask);
        for (const TwistAt& t : g_twists)
            if (t.atStep >= stepNow && t.atStep < (pos + n) / S)
                e->twist(t.cell, t.dir);
        for (int i = 0; i < n; i++)
        {
            L[pos + i] = pos + i < in.frames ? in.l[pos + i] : 0.0f;
            R[pos + i] = pos + i < in.frames ? in.r[pos + i] : 0.0f;
        }
        Clock c;
        c.playing = true;
        c.tempo = in.bpm;
        c.ticks = (uint32_t)(pos / (S / 48.0f));
        e->process(&L[pos], &R[pos], n, c);
    }
}

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return;
    const uint32_t n = (uint32_t)l.size(), bytes = n * 4;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(44100);
    u32(44100 * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);
    for (uint32_t i = 0; i < n; i++)
        for (float v : { l[i], r[i] })
        {
            v = v > 1 ? 1 : (v < -1 ? -1 : v);
            int16_t s = (int16_t)lrintf(v * 32767.0f);
            fwrite(&s, 2, 1, f);
        }
    fclose(f);
}

static float maxDiff(const std::vector<float>& x, int from, int to)
{
    float m = 0;
    for (int i = std::max(1, from); i < to && i < (int)x.size(); i++)
        m = std::max(m, std::fabs(x[i] - x[i - 1]));
    return m;
}

static bool finite(const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite(v) || std::fabs(v) > 8.0f)
            return false;
    return true;
}

// ---------------------------------------------------------------- tests
static void testRollTiming()
{
    // marker clicks only on step 16 (bar 2): a 1/16 roll pressed there must
    // repeat it exactly every 1/16, also when pressed 20 ms late
    const float bpm = 170.0f;
    const float S = kSr * 60.0f / bpm / 4.0f;
    for (float late : { 0.0f, 0.020f, -0.020f })
    {
        Music m;
        m.bpm = bpm;
        m.frames = (int)(S * 48);
        m.l.assign(m.frames, 0.0f);
        m.r.assign(m.frames, 0.0f);
        const int mark = (int)(16 * S);
        for (int i = 0; i < 40; i++)
            m.l[mark + i] = m.r[mark + i] = 0.8f * (1.0f - i / 40.0f);
        Cell cells[8];
        cells[0] = defaultCell(kFxRoll); // 1/16
        std::vector<float> L, R;
        const float pressStep = 16.0f + late * kSr / S;
        run(m, cells, { { 0, pressStep, 24.0f } }, L, R);
        // repeats at mark + k*S for k = 1..7
        int hits = 0;
        for (int k = 1; k < 8; k++)
        {
            const int at = mark + (int)(k * S);
            float peak = 0;
            int peakAt = 0;
            for (int i = at - 300; i < at + 300; i++)
                if (std::fabs(L[i]) > peak)
                {
                    peak = std::fabs(L[i]);
                    peakAt = i;
                }
            if (peak > 0.5f && std::abs(peakAt - at) <= 2)
                hits++;
            else if (getenv("PERF_DEBUG"))
                printf("   repeat %d: expected at %d, peak %.3f at %d\n", k, at, peak, peakAt);
        }
        CHECK(hits == 7, "roll pressed %+.0f ms: %d of 7 repeats on the grid", late * 1000, hits);
        // after release (step 24) the marker is gone: silence
        float after = 0;
        for (int i = (int)(24.5f * S); i < (int)(30 * S); i++)
            after = std::max(after, std::fabs(L[i]));
        CHECK(after < 0.01f, "roll still sounding after release (%.3f)", after);
    }
}

static void testNoClicks()
{
    // a smooth pad through every effect: the output may never jump much more
    // than the pad itself does
    const Music pad = makePad(140.0f, 6);
    const float S = kSr * 60.0f / 140.0f / 4.0f;
    const float dryMax = maxDiff(pad.l, 0, pad.frames);
    for (int fx = 1; fx < kFxCount; fx++)
    {
        const FxInfo& in = fxInfo(fx);
        // every value of the first setting
        const int variants = in.paramCount ? in.params[0].count : 1;
        for (int v = 0; v < variants; v++)
        {
            Cell cells[8];
            cells[3] = defaultCell(fx);
            if (in.paramCount)
                cells[3].p[0] = (uint8_t)v;
            // vinyl crackle and white noise are meant to be rough: test without
            if (fx == kFxCut)
                cells[3].p[0] = 0;
            std::vector<float> L, R;
            // press a little after a step, hold 1.6 bars, release mid-step
            run(pad, cells, { { 3, 16.3f, 42.55f } }, L, R);
            CHECK(finite(L) && finite(R), "%s v%d: bad values", in.name, v);
            // gate and cut are meant to chop: allow their (smoothed) edges
            if (fx == kFxRiser)
                continue; // a noise sweep: rough by design (like Cut's noise)
            // effects that chop, or that add echoes on top (louder = faster moving)
            const float limit = (fx == kFxGate || fx == kFxCut || fx == kFxComb || fx == kFxTalkbox || fx == kFxPump)
                                  ? dryMax * 6.0f
                                  : ((fx == kFxDelay || fx == kFxReverb) ? dryMax * 4.0f : dryMax * 2.2f);
            const float md = std::max(maxDiff(L, 0, pad.frames), maxDiff(R, 0, pad.frames));
            if (getenv("PERF_DEBUG") && md >= limit)
            {
                int at = 1;
                for (int i = 1; i < pad.frames; i++)
                    if (std::fabs(L[i] - L[i - 1]) > std::fabs(L[at] - L[at - 1]))
                        at = i;
                printf("   worst jump at sample %d = step %.3f\n", at, at / S);
            }
            CHECK(md < limit, "%s (%s): click, jump %.4f (pad %.4f)", in.name,
                  in.paramCount ? in.params[0].labels ? in.params[0].labels[v] : "n" : "-", md, dryMax);
        }
    }
    (void)S;
}

static void testTailsDie()
{
    const Music m = makeJungle(170.0f, 4);
    for (int fx : { (int)kFxReverb, (int)kFxDelayCut })
    {
        for (int type = 0; type < 3; type++)
        {
            Cell cells[8];
            cells[0] = defaultCell(fx);
            cells[0].p[1 - (fx == kFxReverb ? 1 : 0)] = (uint8_t)type; // reverb: p0 = type, delay: p1 = type
            cells[0].p[fx == kFxReverb ? 1 : 2] = 9;                    // longest / most feedback
            std::vector<float> L, R;
            run(m, cells, { { 0, 16, 32 } }, L, R, (int)(kSr * 20));
            CHECK(finite(L), "%s type %d: bad values", fxInfo(fx).name, type);
            float end = 0;
            for (size_t i = L.size() - 44100; i < L.size(); i++)
                end = std::max(end, std::fabs(L[i]));
            CHECK(end < 0.003f, "%s type %d: tail still %.4f after 20 s", fxInfo(fx).name, type, end);
        }
    }
}

static void testPassThrough()
{
    // nothing held: output identical to the input (bar the int16 history)
    const Music m = makeJungle(170.0f, 2);
    Cell cells[8];
    factoryCells(cells);
    std::vector<float> L, R;
    run(m, cells, {}, L, R);
    float d = 0;
    for (int i = 0; i < m.frames; i++)
        d = std::max(d, std::fabs(L[i] - m.l[i]));
    CHECK(d == 0.0f, "not pressed but changed the audio (%.6f)", d);
}

static void testStutterKeepsTime()
{
    // whatever a stutter does, letting go returns to the song exactly in time
    const Music m = makeJungle(170.0f, 4);
    const float S = kSr * 60.0f / 170.0f / 4.0f;
    for (int pat = 0; pat < 8; pat++)
    {
        Cell cells[8];
        cells[2] = defaultCell(kFxStutter);
        cells[2].p[0] = (uint8_t)pat;
        std::vector<float> L, R;
        run(m, cells, { { 2, 16.2f, 32.4f } }, L, R);
        double diff = 0;
        for (int i = (int)(33.5f * S); i < (int)(48 * S); i++)
            diff = std::max(diff, (double)std::fabs(L[i] - m.l[i]));
        CHECK(diff < 0.002, "stutter %d: not back in time after release (%.4f)", pat + 1, diff);
    }
}

static void testTwists()
{
    const float bpm = 170.0f;
    const float S = kSr * 60.0f / bpm / 4.0f;
    // roll 1/16, twisted up to 1/32 at step 20: the marker repeats every 1/32 after
    {
        Music m;
        m.bpm = bpm;
        m.frames = (int)(S * 40);
        m.l.assign(m.frames, 0.0f);
        m.r.assign(m.frames, 0.0f);
        const int mark = (int)(16 * S);
        for (int i = 0; i < 40; i++)
            m.l[mark + i] = m.r[mark + i] = 0.8f * (1.0f - i / 40.0f);
        Cell cells[8];
        cells[0] = defaultCell(kFxRoll);
        g_twists = { { 0, 20.0f, +1 } };
        std::vector<float> L, R;
        run(m, cells, { { 0, 16.0f, 28.0f } }, L, R);
        g_twists.clear();
        int hits = 0;
        for (int k = 9; k < 15; k++) // 1/32 grid from step 20.5 to 23.5
        {
            const int at = mark + (int)(k * S * 0.5f);
            float peak = 0;
            int peakAt = 0;
            for (int i = at - 200; i < at + 200; i++)
                if (std::fabs(L[i]) > peak)
                {
                    peak = std::fabs(L[i]);
                    peakAt = i;
                }
            if (peak > 0.5f && std::abs(peakAt - at) <= 2)
                hits++;
        }
        CHECK(hits == 6, "roll twisted to 1/32: %d of 6 repeats on the 1/32 grid", hits);
        // twists are forgotten on release: the cell is untouched
        CHECK(cells[0].p[0] == 3, "twist changed the saved button");
    }
    // vinyl: stop, then spin back up: it catches up and plays in time again (still held)
    {
        const Music m = makeJungle(bpm, 4);
        Cell cells[8];
        cells[0] = defaultCell(kFxVinyl); // 1/2 bar
        g_twists = { { 0, 28.0f, +1 } };
        std::vector<float> L, R;
        run(m, cells, { { 0, 16.0f, 60.0f } }, L, R);
        g_twists.clear();
        double diff = 0;
        for (int i = (int)(48 * S); i < (int)(58 * S); i++)
            diff = std::max(diff, (double)std::fabs(L[i] - m.l[i]));
        CHECK(diff < 0.01, "vinyl spun up but not back in time (%.4f)", diff);
    }
    // reverb freeze holds the tail while held
    {
        const Music m = makeJungle(bpm, 6);
        Cell cells[8];
        cells[0] = defaultCell(kFxReverb);
        cells[0].p[1] = 3; // short
        cells[1] = defaultCell(kFxCut); // mutes the mix underneath, so only the frozen tail is left
        g_twists = { { 0, 30.0f, -1 } };
        std::vector<float> L, R;
        run(m, cells, { { 0, 16.0f, 90.0f }, { 1, 32.0f, 90.0f } }, L, R);
        g_twists.clear();
        auto rms = [&](float a, float b)
        {
            double e = 0;
            for (int i = (int)(a * S); i < (int)(b * S); i++)
                e += L[i] * L[i];
            return std::sqrt(e / ((b - a) * S));
        };
        const double early = rms(36, 44), late = rms(76, 84);
        CHECK(early > 0.005 && late > early * 0.5, "frozen reverb decayed (%.4f -> %.4f)", early, late);
    }
    // stutter twisted to the next pattern switches exactly on a beat
    {
        const Music m = makeJungle(bpm, 4);
        Cell a[8], b[8];
        a[0] = defaultCell(kFxStutter);
        b[0] = defaultCell(kFxStutter);
        b[0].p[0] = 1;
        g_twists = { { 0, 21.5f, +1 } }; // mid beat: pattern 2 must start at step 24
        std::vector<float> L, R, L2, R2;
        run(m, a, { { 0, 16.0f, 32.0f } }, L, R);
        g_twists.clear();
        run(m, b, { { 0, 16.0f, 32.0f } }, L2, R2);
        double d = 0;
        for (int i = (int)(24.3f * S); i < (int)(31.5f * S); i++)
            d = std::max(d, (double)std::fabs(L[i] - L2[i]));
        double before = 0, ref = 0;
        std::vector<float> L3, R3;
        run(m, a, { { 0, 16.0f, 32.0f } }, L3, R3);
        for (int i = (int)(16.5f * S); i < (int)(23.5f * S); i++)
            before = std::max(before, (double)std::fabs(L[i] - L3[i]));
        (void)ref;
        CHECK(d < 0.002, "twisted stutter not on pattern 2 from the next beat (%.4f)", d);
        CHECK(before < 0.002, "twisted stutter changed before the beat (%.4f)", before);
    }
}

// ---------------------------------------------------------------- demos
static void demos(const std::string& dir)
{
    const float bpm = 170.0f;
    const Music m = makeJungle(bpm, 8);
    writeWav(dir + "/00_dry_loop.wav", m.l, m.r);
    struct Demo
    {
        const char* file;
        Cell cell;
        std::vector<Press> presses;
    };
    auto C = [](int fx, int a = -1, int b = -1, int c = -1)
    {
        Cell x = defaultCell(fx);
        if (a >= 0)
            x.p[0] = (uint8_t)a;
        if (b >= 0)
            x.p[1] = (uint8_t)b;
        if (c >= 0)
            x.p[2] = (uint8_t)c;
        return x;
    };
    // presses in 1/16 steps: bar 2 = 16..32, bar 3 = 32..48 ...
    std::vector<Demo> list = {
        { "01_roll_1-16.wav", C(kFxRoll, 3), { { 0, 28, 32 }, { 0, 60, 64 }, { 0, 92, 96 } } },
        { "02_roll_1-32_filter_decay.wav", C(kFxRoll, 5, 3, 1), { { 0, 24, 32 }, { 0, 88, 96 } } },
        { "03_roll_1-8T.wav", C(kFxRoll, 2), { { 0, 24, 32 }, { 0, 56, 64 } } },
        { "04_stutter_patterns_1-8.wav", C(kFxStutter, 0), {} },
        { "05_filter_sweep_lp_down_1bar.wav", C(kFxFilter, 1, 2, 5), { { 0, 16, 48 } } },
        { "06_filter_sweep_hp_up_2bars.wav", C(kFxFilter, 3, 3, 4), { { 0, 16, 64 } } },
        { "07_filter_sweep_lp_backforth.wav", C(kFxFilter, 0, 1, 6), { { 0, 16, 64 } } },
        { "08_comb.wav", C(kFxComb, 6, 7), { { 0, 16, 48 } } },
        { "09_cut_vinyl_noise.wav", C(kFxCut, 2, 4), { { 0, 30, 32 }, { 0, 62, 64 }, { 0, 88, 96 } } },
        { "10_delay_cut_1-8D_tape.wav", C(kFxDelayCut, 3, 1, 6), { { 0, 28, 32 }, { 0, 88, 96 } } },
        { "11_delay_cut_pingpong.wav", C(kFxDelayCut, 2, 2, 6), { { 0, 24, 32 }, { 0, 92, 96 } } },
        { "12_reverb_shimmer.wav", C(kFxReverb, 2, 7, 6), { { 0, 24, 40 } } },
        { "13_reverb_dark.wav", C(kFxReverb, 1, 8, 7), { { 0, 30, 32 }, { 0, 62, 64 } } },
        { "14_reverse.wav", C(kFxReverse, 12), { { 0, 24, 32 }, { 0, 56, 64 } } },
        { "15_reverse_pitch_down.wav", C(kFxReverse, 7), { { 0, 24, 32 } } },
        { "16_talkbox_talk.wav", C(kFxTalkbox, 2, 1, 7), { { 0, 16, 48 } } },
        { "17_talkbox_wah.wav", C(kFxTalkbox, 0, 0, 8), { { 0, 16, 48 } } },
        { "18_vinyl_drop_1bar.wav", C(kFxVinyl, 2), { { 0, 48, 72 } } },
        { "19_gate_straight_1-16.wav", C(kFxGate, 1, 0, 9), { { 0, 16, 48 } } },
        { "20_gate_trance.wav", C(kFxGate, 1, 1, 9), { { 0, 16, 80 } } },
        { "21_halftime.wav", C(kFxHalftime, 0), { { 0, 16, 48 } } },
        { "22_bass_kill_into_drop.wav", C(kFxBassKill, 2, 9), { { 0, 24, 32 }, { 0, 56, 64 } } },
        { "23_riser_2_bars.wav", C(kFxRiser, 1, 6), { { 0, 16, 48 } } },
        { "24_scratch_baby.wav", C(kFxScratch, 0, 0), { { 0, 16, 32 } } },
        { "25_scratch_transformer.wav", C(kFxScratch, 2, 0), { { 0, 16, 32 } } },
        { "26_pump_quarter.wav", C(kFxPump, 0, 7), { { 0, 16, 48 } } },
        { "27_delay_held_then_cut_twist.wav", C(kFxDelay, 3, 0, 6), { { 0, 16, 32 } } },
        { "28_roll_twisted_16_32_64.wav", C(kFxRoll, 3), { { 0, 24, 32 } } },
        { "29_reverb_freeze_twist.wav", C(kFxReverb, 2, 6, 7), { { 0, 16, 48 } } },
        { "30_vinyl_stop_and_spin_up.wav", C(kFxVinyl, 1), { { 0, 16, 48 } } },
    };
    for (Demo& d : list)
    {
        Cell cells[8];
        cells[0] = d.cell;
        std::vector<Press> pr = d.presses;
        if (d.cell.fx == kFxStutter)
        {
            // each pattern for a bar, every other bar
            for (int p = 0; p < 8 && p < 4; p++)
            {
                cells[p] = C(kFxStutter, p);
                pr.push_back({ p, 16.0f + 32.0f * p, 32.0f + 32.0f * p });
            }
        }
        std::vector<float> L, R;
        // demos of the twists
        const std::string f = d.file;
        if (f.rfind("27_", 0) == 0)
            g_twists = { { 0, 24.0f, +1 } }; // the mix cut, echoes go on
        else if (f.rfind("28_", 0) == 0)
            g_twists = { { 0, 26.0f, +1 }, { 0, 28.0f, +1 } }; // 1/16 > 1/32 > 1/64
        else if (f.rfind("29_", 0) == 0)
            g_twists = { { 0, 24.0f, -1 }, { 0, 25.0f, +1 } }; // freeze + cut the mix
        else if (f.rfind("30_", 0) == 0)
            g_twists = { { 0, 28.0f, +1 } }; // spin back up
        run(m, cells, pr, L, R, (int)(kSr * 2));
        g_twists.clear();
        writeWav(dir + "/" + d.file, L, R);
    }
    // stutter 5-8 in a second file
    {
        Cell cells[8];
        std::vector<Press> pr;
        for (int p = 0; p < 4; p++)
        {
            cells[p] = C(kFxStutter, p + 4);
            pr.push_back({ p, 16.0f + 32.0f * p, 32.0f + 32.0f * p });
        }
        std::vector<float> L, R;
        run(m, cells, pr, L, R, (int)(kSr * 2));
        writeWav(dir + "/04b_stutter_patterns_5-8.wav", L, R);
    }
    for (int half = 0; half < 2; half++)
    {
        Cell cells[8];
        std::vector<Press> pr;
        for (int p = 0; p < 4; p++)
        {
            cells[p] = C(kFxStutter, 8 + half * 4 + p);
            pr.push_back({ p, 16.0f + 32.0f * p, 32.0f + 32.0f * p });
        }
        std::vector<float> L, R;
        run(m, cells, pr, L, R, (int)(kSr * 2));
        writeWav(dir + (half ? "/04d_stutter_patterns_13-16.wav" : "/04c_stutter_patterns_9-12.wav"), L, R);
    }
}

int main(int argc, char** argv)
{
    printf("perform fx: roll timing\n");
    testRollTiming();
    printf("perform fx: pass-through\n");
    testPassThrough();
    printf("perform fx: no clicks\n");
    testNoClicks();
    printf("perform fx: stutter returns in time\n");
    testStutterKeepsTime();
    printf("perform fx: twists\n");
    testTwists();
    printf("perform fx: tails die away\n");
    testTailsDie();
    if (argc > 1)
    {
        printf("perform fx: demos -> %s\n", argv[1]);
        demos(argv[1]);
    }
    printf(failures ? "%d PERFORM FX FAILURES\n" : "ALL PERFORM FX TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
