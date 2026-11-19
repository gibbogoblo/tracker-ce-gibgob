// GibGob OS master effects: host tests and listening demos.
//   ./build/master_test            tests
//   ./build/master_test <dir>      tests + demo WAVs in <dir>
#include "gibgob/MasterFx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace gibgob::master;

static int failures = 0;
#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static const float kSr = 44100.0f;
static const float kPi = 3.14159265f;

struct Music
{
    std::vector<float> l, r;
    float bpm = 120.0f;
    std::vector<int> kicks; // sample positions of the kick hits
};

// a little loop: kick on the beats, off-beat hats, a stereo chord pad
static Music makeLoop(float bpm, int bars)
{
    Music m;
    m.bpm = bpm;
    const float step = kSr * 60.0f / bpm / 4.0f;
    const int n = (int)(step * 16 * bars);
    m.l.assign(n, 0.0f);
    m.r.assign(n, 0.0f);
    uint32_t rng = 12345;
    for (int s = 0; s < 16 * bars; s++)
    {
        const int at = (int)(s * step);
        if (s % 4 == 0)
        {
            m.kicks.push_back(at);
            float ph = 0;
            for (int i = 0; i < (int)(0.25f * kSr) && at + i < n; i++)
            {
                const float t = i / kSr;
                ph += 2 * kPi * (50.0f + 120.0f * expf(-t * 30.0f)) / kSr;
                const float v = 0.7f * sinf(ph) * expf(-t * 9.0f);
                m.l[at + i] += v;
                m.r[at + i] += v;
            }
        }
        if (s % 4 == 2)
            for (int i = 0; i < (int)(0.05f * kSr) && at + i < n; i++)
            {
                rng = rng * 1664525u + 1013904223u;
                const float v = 0.15f * ((int32_t)rng / 2147483648.0f) * expf(-i / (0.012f * kSr));
                m.l[at + i] += v * 0.7f;
                m.r[at + i] += v;
            }
    }
    const float notes[3] = { 220.0f, 261.63f, 329.63f };
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 3; k++)
        {
            const float v = 0.07f * sinf(2 * kPi * notes[k] * i / kSr);
            m.l[i] += v * (k == 0 ? 1.0f : 0.6f);
            m.r[i] += v * (k == 2 ? 1.0f : 0.6f);
        }
    return m;
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

static std::vector<uint8_t> g_mem;

// runs the engine over the music like the device does: per 128-sample block,
// the duck gain of the block before scales everything but the kick
static void run(const Music& m, const Settings& s, std::vector<float>& L, std::vector<float>& R,
                const Music* kickOnly = nullptr)
{
    g_mem.assign(Engine::kMemoryBytes, 0);
    static Engine* e = nullptr;
    delete e;
    e = new Engine();
    e->attach(g_mem.data());
    e->setSettings(s);
    const int n = (int)m.l.size();
    L.assign(n, 0.0f);
    R.assign(n, 0.0f);
    size_t nextKick = 0;
    float duck = 1.0f;
    for (int pos = 0; pos < n; pos += 128)
    {
        const int len = pos + 128 <= n ? 128 : n - pos;
        while (nextKick < m.kicks.size() && m.kicks[nextKick] < pos + len)
        {
            e->noteStarted(s.duckTrack);
            nextKick++;
        }
        for (int i = 0; i < len; i++)
        {
            float kl = 0, kr = 0;
            if (kickOnly)
            {
                kl = kickOnly->l[pos + i];
                kr = kickOnly->r[pos + i];
            }
            // everything but the kick is ducked (by the gain worked out a block ago)
            L[pos + i] = (m.l[pos + i] - kl) * duck + kl;
            R[pos + i] = (m.r[pos + i] - kr) * duck + kr;
        }
        duck = e->duckBlock(len, m.bpm);
        e->process(&L[pos], &R[pos], len);
    }
}

static bool finite(const std::vector<float>& v)
{
    for (float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}
static double rms(const std::vector<float>& v)
{
    double e = 0;
    for (float x : v)
        e += x * x;
    return std::sqrt(e / (v.empty() ? 1 : v.size()));
}

static Settings colour(int type, int a = -1, int b = -1, int c = -1, int d = -1)
{
    Settings s;
    s.slot[0] = defaultSlot(type);
    const int v[4] = { a, b, c, d };
    for (int k = 0; k < 4; k++)
        if (v[k] >= 0)
            s.slot[0].p[k] = (uint8_t)v[k];
    return s;
}

int main(int argc, char** argv)
{
    // the device keeps 40 KB of external RAM for the effects
    CHECK(Engine::kMemoryBytes <= 40u * 1024u, "master fx needs %u bytes, more than the 40 KB kept for it", Engine::kMemoryBytes);

    // 1. packing: every field survives
    {
        uint32_t rng = 7;
        for (int t = 0; t < 2000; t++)
        {
            Settings s;
            auto rnd = [&](int n) {
                rng = rng * 1664525u + 1013904223u;
                return (int)((rng >> 8) % (uint32_t)n);
            };
            s.duckOn = rnd(2);
            s.duckTrack = (uint8_t)rnd(8);
            s.duckDepth = (uint8_t)rnd(11);
            s.duckRelease = (uint8_t)rnd(4);
            s.duckShape = (uint8_t)rnd(2);
            s.duckNoise = rnd(2);
            s.glue = (uint8_t)rnd(11);
            s.glueStyle = (uint8_t)rnd(kGlueStyleCount);
            for (int k = 0; k < kSlots; k++)
            {
                s.slot[k] = defaultSlot(rnd(kColourCount));
                const ColourInfo& ci = colourInfo(s.slot[k].type);
                for (int p = 0; p < ci.paramCount; p++)
                    s.slot[k].p[p] = (uint8_t)rnd(ci.params[p].count);
            }
            uint8_t b[kPackedBytes];
            pack(s, b);
            const Settings u = unpack(b);
            uint8_t b2[kPackedBytes];
            pack(u, b2);
            CHECK(memcmp(b, b2, kPackedBytes) == 0 && u.duckTrack == s.duckTrack && u.glue == s.glue &&
                    u.slot[1].type == s.slot[1].type && u.slot[1].p[3] == s.slot[1].p[3],
                  "settings did not survive packing (%d)", t);
        }
        // garbage bytes never give values out of range
        for (int t = 0; t < 256; t++)
        {
            uint8_t b[kPackedBytes];
            for (int i = 0; i < kPackedBytes; i++)
                b[i] = (uint8_t)(t * 37 + i * 101);
            const Settings u = unpack(b);
            for (int k = 0; k < kSlots; k++)
            {
                const ColourInfo& ci = colourInfo(u.slot[k].type);
                for (int p = 0; p < ci.paramCount; p++)
                    CHECK(u.slot[k].p[p] < ci.params[p].count, "garbage gave a bad value");
            }
            CHECK(u.glue <= 10 && u.duckDepth <= 10, "garbage gave a bad glue/duck value");
        }
    }

    const Music loop = makeLoop(128.0f, 4);

    // 2. everything off: the mix passes untouched (bit for bit)
    {
        std::vector<float> L, R;
        run(loop, Settings(), L, R);
        bool same = true;
        for (size_t i = 0; i < L.size(); i++)
            same = same && L[i] == loop.l[i] && R[i] == loop.r[i];
        CHECK(same, "with everything off the mix changed");
    }

    // 3. every colour, every value of its first setting and the extremes: sane output
    for (int type = 1; type < kColourCount; type++)
    {
        const ColourInfo& ci = colourInfo(type);
        for (int p = 0; p < ci.paramCount; p++)
            for (int v : { 0, (int)ci.params[p].count - 1 })
            {
                Settings s = colour(type);
                s.slot[0].p[p] = (uint8_t)v;
                s.slot[1] = defaultSlot(type == kColourWidth ? kColourCassette : kColourWidth);
                std::vector<float> L, R;
                run(loop, s, L, R);
                const double ratio = rms(L) / rms(loop.l);
                CHECK(finite(L) && finite(R), "%s %s=%d: bad values", ci.name, ci.params[p].name, v);
                CHECK(ratio > 0.25 && ratio < 2.5, "%s %s=%d: level way off (%.2f)", ci.name, ci.params[p].name, v, ratio);
            }
    }

    // 4. width 0% is mono; mono bass leaves no low end in the sides
    {
        std::vector<float> L, R;
        run(loop, colour(kColourWidth, 0, 0), L, R);
        double d = 0;
        for (size_t i = 0; i < L.size(); i++)
            d = std::max(d, (double)std::fabs(L[i] - R[i]));
        CHECK(d < 1e-6, "width 0%% is not mono (%.6f)", d);
        Music bass;
        bass.bpm = 120;
        const int n = 44100;
        bass.l.resize(n);
        bass.r.resize(n);
        for (int i = 0; i < n; i++)
        {
            const float v = 0.5f * sinf(2 * kPi * 50.0f * i / kSr);
            bass.l[i] = v;
            bass.r[i] = -v; // all side
        }
        run(bass, colour(kColourWidth, 5, 3), L, R); // 100%, mono under 200 Hz
        double side = 0;
        for (int i = n / 2; i < n; i++)
            side = std::max(side, (double)std::fabs(0.5f * (L[i] - R[i])));
        CHECK(side < 0.2, "mono bass left 50 Hz in the sides (%.3f)", side);
    }

    // 4b. width over 100% widens a mono mix too
    {
        Music mono = loop;
        for (size_t i = 0; i < mono.l.size(); i++)
            mono.r[i] = mono.l[i];
        std::vector<float> L, R;
        run(mono, colour(kColourWidth, 10, 0), L, R); // 200%
        double side = 0;
        for (size_t i = 0; i < L.size(); i++)
            side += 0.25 * (L[i] - R[i]) * (L[i] - R[i]);
        side = std::sqrt(side / L.size());
        CHECK(side > 0.02, "width 200%% did nothing to a mono mix (side %.4f)", side);
    }
    // 4c. wow, wobble at 5: the pitch really moves, and never jumps
    for (int type : { (int)kColourVinyl, (int)kColourCassette })
    {
        Music tone;
        tone.bpm = 120;
        const int n = 4 * 44100;
        tone.l.resize(n);
        tone.r.resize(n);
        for (int i = 0; i < n; i++)
            tone.l[i] = tone.r[i] = 0.5f * sinf(2 * kPi * 1000.0f * i / kSr);
        Settings s = type == kColourVinyl ? colour(kColourVinyl, 0, 0, 5, 0) : colour(kColourCassette, 0, 0, 5, 0);
        std::vector<float> L, R;
        run(tone, s, L, R);
        // zero crossings per 0.1 s: the pitch moves by more than 1%
        double lo = 1e9, hi = 0, maxJump = 0;
        for (int w = 5; w < 39; w++)
        {
            int z = 0;
            for (int i = w * 4410 + 1; i < (w + 1) * 4410; i++)
                if ((L[i - 1] < 0) != (L[i] < 0))
                    z++;
            lo = std::min(lo, (double)z);
            hi = std::max(hi, (double)z);
        }
        for (int i = 44100; i < n; i++)
            maxJump = std::max(maxJump, (double)std::fabs(L[i] - L[i - 1]));
        CHECK(hi / lo > 1.01, "%s at 5: pitch barely moves (%.0f..%.0f)", colourInfo(type).name, lo, hi);
        CHECK(maxJump < 0.12, "%s at 5: jumps (%.3f)", colourInfo(type).name, maxJump);
    }

    // 5. lo-fi at 4 bits: only 16 levels (before its filter)
    {
        std::vector<float> L, R;
        run(loop, colour(kColourLofi, 0, 5, 0), L, R);
        bool onGrid = true;
        for (float x : L)
        {
            const float q = x * 8.0f;
            onGrid = onGrid && std::fabs(q - std::round(q)) < 1e-4f;
        }
        CHECK(onGrid, "lo-fi 4 bits is not on the 4-bit grid");
    }

    // 6. the duck: down on the kick track's notes only, back by the release
    {
        Engine e;
        Settings s;
        s.duckOn = true;
        s.duckTrack = 2;
        s.duckDepth = 10;
        s.duckRelease = 1; // 1/8
        e.setSettings(s);
        CHECK(e.duckBlock(128, 120.0f) == 1.0f, "duck down without a kick");
        e.noteStarted(3);
        CHECK(e.duckBlock(128, 120.0f) == 1.0f, "duck followed another track");
        e.noteStarted(2);
        const float g0 = e.duckBlock(128, 120.0f);
        CHECK(g0 < 0.02f, "duck not down on the kick (%.3f)", g0);
        // 1/8 at 120 BPM = 11025 samples
        float g = 0;
        for (int i = 0; i < 11025 / 128 + 2; i++)
            g = e.duckBlock(128, 120.0f);
        CHECK(g == 1.0f, "duck not back after its release (%.3f)", g);
        // half depth, hard shape: half way down, holding for half the release
        s.duckDepth = 5;
        s.duckShape = 1;
        e.setSettings(s);
        e.duckBlock(128, 120.0f);
        e.noteStarted(2);
        const float h0 = e.duckBlock(128, 120.0f);
        float h1 = 0;
        for (int i = 0; i < 30; i++)
            h1 = e.duckBlock(128, 120.0f);
        CHECK(std::fabs(h0 - 0.5f) < 1e-3f && std::fabs(h1 - 0.5f) < 1e-3f, "hard duck not held (%.3f %.3f)", h0, h1);
    }

    // 7. glue: loud parts come down, quiet parts come up a little (make-up)
    {
        Music m;
        m.bpm = 120;
        const int n = 44100;
        m.l.resize(n);
        m.r.resize(n);
        for (int i = 0; i < n; i++)
        {
            const float a = i < n / 2 ? 0.9f : 0.05f;
            m.l[i] = m.r[i] = a * sinf(2 * kPi * 220.0f * i / kSr);
        }
        Settings s;
        s.glue = 10;
        s.glueStyle = kGlueGarage; // fast release: back up within the quiet half
        std::vector<float> L, R;
        run(m, s, L, R);
        double loud = 0, quiet = 0;
        for (int i = n / 4; i < n / 2; i++)
            loud = std::max(loud, (double)std::fabs(L[i]));
        for (int i = n - n / 8; i < n; i++)
            quiet = std::max(quiet, (double)std::fabs(L[i]));
        CHECK(loud < 0.7 && loud > 0.2, "glue did not bring the loud part down (%.3f)", loud);
        CHECK(quiet > 0.05, "glue lost the quiet part (%.3f)", quiet);
        CHECK(finite(L), "glue bad values");
    }

    // 8. glue presets: Jungle squashes hardest, Hip hop lets the hit's front through
    {
        Music m;
        m.bpm = 120;
        const int gap = 66150; // 1.5 s: every preset has let go before the next hit
        const int n = 4 * gap;
        m.l.assign(n, 0.0f);
        m.r.assign(n, 0.0f);
        // a loud 1 kHz hit decaying over 100 ms
        for (int at = 0; at < n; at += gap)
            for (int i = 0; i < 4410 && at + i < n; i++)
                m.l[at + i] = m.r[at + i] = 0.9f * sinf(2 * kPi * 1000.0f * i / kSr) * expf(-i / 2000.0f);
        double front[kGlueStyleCount], body[kGlueStyleCount];
        for (int st = 0; st < kGlueStyleCount; st++)
        {
            Settings s;
            s.glue = 8;
            s.glueStyle = (uint8_t)st;
            std::vector<float> L, R;
            run(m, s, L, R);
            CHECK(finite(L), "glue %s bad values", glueStyleInfo(st).name);
            // the last hit: its first 2 ms, and 20-60 ms in
            const int at = 3 * gap;
            front[st] = 0;
            body[st] = 0;
            for (int i = 0; i < 88; i++)
                front[st] = std::max(front[st], (double)std::fabs(L[at + i]));
            for (int i = 882; i < 2646; i++)
                body[st] = std::max(body[st], (double)std::fabs(L[at + i]) / (0.9 * std::exp(-i / 2000.0)));
        }
        CHECK(body[kGlueJungle] < body[kGlueGarage], "jungle not squashing harder than garage (%.2f %.2f)", body[kGlueJungle], body[kGlueGarage]);
        // (jungle even gets more make-up gain, so a louder front is the slow attack at work)
        CHECK(front[kGlueHipHop] > front[kGlueJungle] * 1.1, "hip hop does not keep more of the hit's front than jungle (%.2f %.2f)",
              front[kGlueHipHop], front[kGlueJungle]);
        // the detector's high-pass: a deep sub alone barely moves the hip hop glue
        Music sub;
        sub.bpm = 120;
        sub.l.resize(n);
        sub.r.resize(n);
        for (int i = 0; i < n; i++)
            sub.l[i] = sub.r[i] = 0.8f * sinf(2 * kPi * 40.0f * i / kSr);
        Settings hh;
        hh.glue = 8;
        hh.glueStyle = kGlueHipHop;
        Settings cl = hh;
        cl.glueStyle = kGlueClean;
        std::vector<float> L1, R1, L2, R2;
        run(sub, hh, L1, R1);
        run(sub, cl, L2, R2);
        CHECK(rms(L1) > rms(L2) * 1.2, "hip hop glue pumps on the sub (%.3f vs clean %.3f)", rms(L1), rms(L2));
    }

    // demos
    if (argc > 1)
    {
        const std::string dir = argv[1];
        const Music m = makeLoop(128.0f, 8);
        Music kick = m; // the kick on its own, so the demo ducks everything else
        {
            Music empty = makeLoop(128.0f, 8);
            // rebuild: only the kicks
            kick.l.assign(m.l.size(), 0.0f);
            kick.r.assign(m.r.size(), 0.0f);
            for (int at : m.kicks)
            {
                float ph = 0;
                for (int i = 0; i < (int)(0.25f * kSr) && at + i < (int)kick.l.size(); i++)
                {
                    const float t = i / kSr;
                    ph += 2 * kPi * (50.0f + 120.0f * expf(-t * 30.0f)) / kSr;
                    const float v = 0.7f * sinf(ph) * expf(-t * 9.0f);
                    kick.l[at + i] += v;
                    kick.r[at + i] += v;
                }
            }
            (void)empty;
        }
        struct Demo
        {
            const char* file;
            Settings s;
        };
        std::vector<Demo> demos;
        demos.push_back({ "m00_dry.wav", Settings() });
        demos.push_back({ "m01_cassette.wav", colour(kColourCassette) });
        demos.push_back({ "m02_cassette_worn.wav", colour(kColourCassette, 9, 7, 5, 4) });
        demos.push_back({ "m03_vinyl.wav", colour(kColourVinyl) });
        demos.push_back({ "m04_vinyl_old.wav", colour(kColourVinyl, 9, 8, 5, 9) });
        demos.push_back({ "m05_lofi_12bit_22k.wav", colour(kColourLofi, 2, 1, 1) });
        demos.push_back({ "m06_lofi_8bit_11k.wav", colour(kColourLofi, 4, 3, 2) });
        demos.push_back({ "m07_width_wide_monobass.wav", colour(kColourWidth, 9, 2) });
        Settings glue;
        glue.glue = 7;
        demos.push_back({ "m08_glue.wav", glue });
        for (int st = 0; st < kGlueStyleCount; st++)
        {
            static const char* const kFiles[kGlueStyleCount] = { "m08a_glue_hiphop.wav", "m08b_glue_garage.wav",
                                                                 "m08c_glue_jungle.wav", "m08d_glue_clean.wav" };
            Settings g;
            g.glue = 7;
            g.glueStyle = (uint8_t)st;
            demos.push_back({ kFiles[st], g });
        }
        Settings duck;
        duck.duckOn = true;
        duck.duckDepth = 8;
        duck.duckRelease = 2;
        demos.push_back({ "m09_duck_quarter.wav", duck });
        Settings combo = colour(kColourCassette, 6, 4, 2, 1);
        combo.slot[1] = defaultSlot(kColourLofi);
        combo.duckOn = true;
        combo.duckDepth = 7;
        combo.duckRelease = 1;
        combo.duckNoise = true;
        combo.glue = 5;
        demos.push_back({ "m10_combo_tape_lofi_duck_glue.wav", combo });
        for (const Demo& d : demos)
        {
            std::vector<float> L, R;
            run(m, d.s, L, R, &kick);
            writeWav(dir + "/" + d.file, L, R);
        }
    }

    printf(failures ? "%d MASTER FX FAILURES\n" : "ALL MASTER FX TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
