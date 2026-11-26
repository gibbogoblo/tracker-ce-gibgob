// GibGob OS whistle to notes: made-up whistles with known notes, and the
// four takes recorded on the Mini (gibgob-tests/whistles/*.wav)
#include "gibgob/Whistle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gibgob;
using namespace gibgob::whistle;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static Scratch* g_work = new Scratch;
static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static std::vector<Reading> track(const std::vector<int16_t>& pcm)
{
    std::vector<Reading> r(kMaxHops);
    PitchTracker t;
    t.begin(r.data(), kMaxHops);
    // fed in pieces, as while recording
    for (uint32_t upTo = 0; upTo < pcm.size(); upTo += 3000)
        t.feed(pcm.data(), upTo);
    t.feed(pcm.data(), (uint32_t)pcm.size());
    r.resize(t.count());
    return r;
}

static bool readWav(const char* path, std::vector<int16_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        b.insert(b.end(), buf, buf + n);
    fclose(f);
    size_t p = 12;
    int channels = 1;
    while (p + 8 <= b.size())
    {
        uint32_t len = b[p + 4] | b[p + 5] << 8 | b[p + 6] << 16 | (uint32_t)b[p + 7] << 24;
        if (!memcmp(&b[p], "fmt ", 4))
            channels = b[p + 10];
        if (!memcmp(&b[p], "data", 4))
        {
            for (size_t i = p + 8; i + 1 < p + 8 + len && i + 1 < b.size(); i += 2 * channels)
                out.push_back((int16_t)(b[i] | b[i + 1] << 8));
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

struct Made
{
    float semis; // MIDI
    float seconds;
    bool glide;  // glides in over 150 ms
    float gap;   // silence after
};

// a made-up whistle: sine with vibrato, a little breath noise, soft edges
static std::vector<int16_t> makeWhistle(const Made* m, int count, float tuning, uint32_t seed)
{
    std::vector<int16_t> pcm(kRate / 4, 0); // a quarter second of silence first
    double phase = 0.0;
    float prev = m[0].semis;
    for (int k = 0; k < count; k++)
    {
        const int len = (int)(m[k].seconds * kRate);
        const int glide = m[k].glide ? (int)(0.15f * kRate) : (int)(0.02f * kRate);
        for (int i = 0; i < len; i++)
        {
            float p = m[k].semis;
            if (i < glide && k > 0 && m[k - 1].gap == 0.0f)
                p = prev + (m[k].semis - prev) * (float)i / (float)glide;
            p += tuning + 0.12f * sinf(6.2831853f * 5.5f * (float)i / kRate); // vibrato
            const double hz = 440.0 * pow(2.0, (p - 69.0) / 12.0);
            phase += 6.283185307 * hz / kRate;
            float env = 1.0f;
            const int edge = (int)(0.015f * kRate);
            if (m[k].gap > 0.0f || k == count - 1)
                env = i > len - edge ? (float)(len - i) / edge : env;
            if (k == 0 || m[k - 1].gap > 0.0f)
                env = i < edge ? (float)i / edge : env;
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((int)(seed >> 9) % 2001 - 1000) / 1000.0f * 0.01f;
            pcm.push_back((int16_t)((0.3f * env * (float)sin(phase) + noise) * 32767.0f));
        }
        prev = m[k].semis;
        for (int i = 0; i < (int)(m[k].gap * kRate); i++)
        {
            seed = seed * 1664525u + 1013904223u;
            pcm.push_back((int16_t)(((int)(seed >> 9) % 2001 - 1000) / 1000.0f * 0.003f * 32767.0f));
        }
    }
    pcm.resize(pcm.size() + kRate / 4, 0);
    return pcm;
}

static void printTake(const char* name, const Take& t)
{
    printf("  %s: %d notes, tuning %+.2f, key %s %s\n", name, t.count, t.tuning, kNames[t.key], t.minor ? "minor" : "major");
    for (int k = 0; k < t.count; k++)
    {
        const Found& f = t.note[k];
        const int s = (int)lroundf(f.pitch - t.tuning);
        printf("    %5.2fs-%5.2fs  %-3s%d (%.2f)  level %.3f%s\n", f.startHop * kHopSeconds, f.endHop * kHopSeconds,
               kNames[s % 12], s / 12 - 1, f.pitch, f.level, f.slideIn ? "  slide" : "");
    }
}

int main(int argc, char** argv)
{
    // ---- the pitch of a pure tone, all over the whistle range ----
    for (float semis = 70.0f; semis <= 106.0f; semis += 1.37f)
    {
        std::vector<int16_t> pcm(kNeed);
        const double hz = 440.0 * pow(2.0, (semis - 69.0) / 12.0);
        for (int i = 0; i < kNeed; i++)
            pcm[i] = (int16_t)(12000.0 * sin(6.283185307 * hz * i / kRate));
        Reading r = PitchTracker::analyse(pcm.data());
        if (fabsf(r.pitch - semis) > 0.05f || r.clarity < 0.9f)
        {
            printf("FAIL pure tone %.2f: read %.3f, clarity %.2f\n", semis, r.pitch, r.clarity);
            failures++;
        }
    }

    // ---- made-up whistles: the notes come back exactly ----
    {
        const Made tune[] = { { 84, 0.40f, false, 0.10f }, { 86, 0.40f, false, 0.10f }, { 88, 0.30f, false, 0.0f },
                              { 91, 0.50f, false, 0.20f }, { 89, 0.25f, false, 0.08f }, { 88, 0.25f, false, 0.08f },
                              { 86, 0.60f, false, 0.3f } };
        for (float tuning : { 0.0f, 0.31f, -0.42f, 0.45f })
        {
            std::vector<int16_t> pcm = makeWhistle(tune, 7, tuning, 7);
            std::vector<Reading> r = track(pcm);
            static Take t;
            findNotes(r.data(), (int)r.size(), t, *g_work);
            bool ok = t.count == 7;
            for (int k = 0; ok && k < 7; k++)
                ok = lroundf(t.note[k].pitch - t.tuning) == (long)tune[k].semis && !t.note[k].slideIn;
            if (!ok || fabsf(t.tuning - tuning) > 0.1f)
            {
                printf("FAIL made-up tune, tuning %+.2f\n", tuning);
                printTake("made-up", t);
                failures++;
            }
            // the key guessed holds every note (C major, or a key with the same notes)
            for (int k = 0; k < t.count; k++)
                CHECK(snapToScale(t.note[k].pitch - t.tuning, t.minor ? kSnapMinor : kSnapMajor, t.key) == lroundf(t.note[k].pitch - t.tuning));
        }
    }
    // ---- a glide becomes a slide; a quick change does not ----
    {
        const Made tune[] = { { 84, 0.5f, false, 0.0f }, { 89, 0.5f, true, 0.0f }, { 91, 0.4f, false, 0.2f } };
        std::vector<int16_t> pcm = makeWhistle(tune, 3, 0.2f, 3);
        std::vector<Reading> r = track(pcm);
        static Take t;
        findNotes(r.data(), (int)r.size(), t, *g_work);
        CHECK(t.count == 3);
        if (t.count == 3)
        {
            CHECK(!t.note[0].slideIn);
            CHECK(t.note[1].slideIn);
            CHECK(!t.note[2].slideIn);
            CHECK(t.note[0].endHop == t.note[1].startHop);
        }
        if (failures)
            printTake("glide", t);

        // onto the grid: 120 BPM, 1/16 grid, two octaves down, the slide kept
        Placement pl{};
        pl.hopsPerTick = (float)kRate / kHop / (120.0f / 60.0f * 4.0f * kTicksPerStep);
        pl.anchorHop = (int)(0.25f / kHopSeconds); // tick 0 where the first note starts
        pl.patternTicks = 32 * kTicksPerStep;
        pl.gridTicks = kTicksPerStep;
        pl.octaves = autoOctaves(t, 60);
        pl.snap = kSnapChromatic;
        pl.slides = true;
        Note out[16];
        const int n = toNotes(t, pl, out, 16);
        CHECK(pl.octaves == -2);
        CHECK(n == 3);
        if (n == 3)
        {
            CHECK(out[0].start == 0 && out[0].pitch == 60);
            CHECK(out[1].pitch == 65 && (out[1].flags & kNoteFlagSlide));
            CHECK(out[0].start + out[0].length == out[1].start); // joined for the slide
            CHECK(out[2].pitch == 67 && !(out[2].flags & kNoteFlagSlide));
            for (int k = 0; k < n; k++)
                CHECK(out[k].start % kTicksPerStep == 0 && out[k].length % kTicksPerStep == 0);
        }
        pl.slides = false;
        Note flat[16];
        const int n2 = toNotes(t, pl, flat, 16);
        CHECK(n2 == 3 && !(flat[1].flags & kNoteFlagSlide));
    }
    // ---- scale snap ----
    CHECK(snapToScale(61.2f, kSnapMajor, 0) == 62); // C# leaning up -> D in C major
    CHECK(snapToScale(60.9f, kSnapMajor, 0) == 60 || snapToScale(60.9f, kSnapMajor, 0) == 62);
    CHECK(snapToScale(63.0f, kSnapMinor, 0) == 63); // Eb is in C minor
    CHECK(snapToScale(66.4f, kSnapMajor, 7) == 66); // F# in G major
    CHECK(snapToScale(70.0f, kSnapChromatic, 0) == 70);
    // ---- silence and noise: nothing ----
    {
        std::vector<int16_t> pcm(kRate * 2);
        uint32_t seed = 1;
        for (auto& s : pcm)
        {
            seed = seed * 1664525u + 1013904223u;
            s = (int16_t)(((int)(seed >> 9) % 2001 - 1000) * 3);
        }
        std::vector<Reading> r = track(pcm);
        static Take t;
        findNotes(r.data(), (int)r.size(), t, *g_work);
        CHECK(t.count == 0);
    }

    // ---- the takes recorded on the Mini ----
    const char* dir = argc > 1 ? argv[1] : "whistles";
    const struct { const char* file; int minNotes, maxNotes; int slides; } takes[] = {
        { "clean.wav", 5, 9, -1 }, { "long.wav", 6, 30, -1 }, { "offkey.wav", 8, 16, -1 }, { "slides.wav", 4, 9, 2 },
    };
    for (const auto& tk : takes)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, tk.file);
        std::vector<int16_t> pcm;
        if (!readWav(path, pcm))
        {
            printf("FAIL could not read %s\n", path);
            failures++;
            continue;
        }
        std::vector<Reading> r = track(pcm);
        static Take t;
        findNotes(r.data(), (int)r.size(), t, *g_work);
        printTake(tk.file, t);
        if (t.count < tk.minNotes || t.count > tk.maxNotes)
        {
            printf("FAIL %s: %d notes\n", tk.file, t.count);
            failures++;
        }
        int slides = 0;
        for (int k = 0; k < t.count; k++)
            slides += t.note[k].slideIn;
        if (tk.slides >= 0 && slides < tk.slides)
        {
            printf("FAIL %s: %d slides\n", tk.file, slides);
            failures++;
        }
    }

    if (failures)
    {
        printf("WHISTLE TESTS: %d FAILED\n", failures);
        return 1;
    }
    printf("ALL WHISTLE TESTS PASSED\n");
    return 0;
}
