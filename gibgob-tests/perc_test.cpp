// GibGob OS PERC: the LXR-02 voices as GibGob drum sounds (core/source/gibgob/perc, core/libs/lxr).
// That the voices sound as the LXR's own was checked against Erica Synths' code (the LXR-02 firmware
// built for the computer): 817 of the 819 kit voices sample for sample, the other two use LXR user
// samples. This test keeps the engine around them honest: kits, modulation, tempo, levels.
#include "gibgob/perc/Perc.h"
#include "lxr/Soundengine/DSPAudio/random.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gibgob::perc;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static Engine* g_engine = new Engine;

struct Take
{
    std::vector<float> l, r;
    float peak = 0;
    double rms = 0;
    int length = 0; // samples until it fell under -50 dBFS for good
};

static Take play(const Sound& s, int seconds10 = 10, uint8_t vel = 110, int note = 60, int voice = 0)
{
    Engine& e = *g_engine;
    e.init();
    e.setSound(voice, s);
    lxr_seedRandom(1234);
    e.trigger(voice, vel, note);
    Take t;
    const int n = 44100 * seconds10 / 10 / 64 * 64;
    t.l.assign(n, 0);
    t.r.assign(n, 0);
    for (int off = 0; off < n; off += 512)
        e.render(&t.l[off], &t.r[off], n - off < 512 ? n - off : 512);
    double en = 0;
    for (int i = 0; i < n; i++)
    {
        const float a = fmaxf(fabsf(t.l[i]), fabsf(t.r[i]));
        t.peak = fmaxf(t.peak, a);
        en += t.l[i] * t.l[i];
        if (a > 0.00316f)
            t.length = i + 1;
    }
    t.rms = sqrt(en / n);
    return t;
}

static bool finite(const Take& t)
{
    for (size_t i = 0; i < t.l.size(); i++)
        if (!std::isfinite(t.l[i]) || !std::isfinite(t.r[i]))
            return false;
    return true;
}

static double crossingsHz(const Take& t)
{
    int zc = 0;
    const int n = t.length > 64 ? t.length : (int)t.l.size();
    for (int i = 1; i < n; i++)
        zc += (t.l[i - 1] < 0) != (t.l[i] < 0);
    return zc / 2.0 / n * 44100;
}

static double magnitude(const Take& t, int from, int len, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; i++)
    {
        re += t.l[from + i] * cos(2 * M_PI * hz * i / 44100);
        im += t.l[from + i] * sin(2 * M_PI * hz * i / 44100);
    }
    return sqrt(re * re + im * im) / len;
}

static bool same(const Take& a, const Take& b)
{
    if (a.l.size() != b.l.size())
        return false;
    for (size_t i = 0; i < a.l.size(); i++)
        if (fabsf(a.l[i] - b.l[i]) > 1e-6f)
            return false;
    return true;
}

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    const uint32_t n = (uint32_t)l.size(), bytes = n * 4;
    const uint32_t hdr[] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 176400, 0x00100004, 0x61746164, bytes };
    fwrite(hdr, 4, 11, f);
    for (uint32_t i = 0; i < n; i++)
    {
        const int16_t s[2] = { (int16_t)lrintf(fmaxf(-1, fminf(1, l[i])) * 32767), (int16_t)lrintf(fmaxf(-1, fminf(1, r[i])) * 32767) };
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

static void testKits()
{
    CHECK(kitCount() == 117);
    CHECK(kitIsFactory(0) && kitIsFactory(84) && !kitIsFactory(85));
    CHECK(strcmp(kitName(0), "Badonka") == 0);
    CHECK(slotType(kSlotD3) == kDrum && slotType(kSlotSnare) == kSnare && slotType(kSlotCymbal) == kCymbal);
    CHECK(slotType(kSlotClosedHat) == kHihat && slotType(kSlotOpenHat) == kHihat);
    Sound s;
    soundFromKit(0, kSlotD1, s);
    CHECK(strcmp(s.name, "Badonka D1") == 0);
    CHECK(s.type == kDrum && !s.hatOpen);
    soundFromKit(3, kSlotOpenHat, s);
    CHECK(s.type == kHihat && s.hatOpen);

    // every voice of every kit: in range, finite, sounding, and not louder than full scale
    int silent = 0, loud = 0, endless = 0, bad = 0;
    for (int kit = 0; kit < kitCount(); kit++)
        for (int slot = 0; slot < kKitSlotCount; slot++)
        {
            soundFromKit(kit, slot, s);
            for (int p = 0; p < kParamCount; p++)
                if (s.value[p] > paramInfo(p).max)
                    bad++;
            const Take t = play(s, 40);
            if (!finite(t))
                bad++;
            if (t.peak < 0.003f)
                silent++;
            if (t.peak > 1.0f)
                loud++;
            if (t.length >= (int)t.l.size() - 64)
                endless++;
        }
    printf("kits: %d voices, %d silent, %d over full scale, %d still sounding after 4 s\n", kitCount() * kKitSlotCount, silent, loud, endless);
    CHECK(bad == 0);
    CHECK(silent <= 8);  // some kit slots are left empty (volume 0) by their makers
    CHECK(loud == 0);
    CHECK(endless <= 25); // drones and long cymbals
}

static void testDefaults()
{
    Sound kick, snare, hat, bell;
    defaultSound(0, kick);
    defaultSound(1, snare);
    defaultSound(2, hat);
    defaultSound(3, bell);
    CHECK(kick.type == kDrum && snare.type == kSnare && hat.type == kHihat && bell.type == kDrum);
    CHECK(strcmp(bell.name, "Cowbell") == 0);
    const Take k = play(kick), b = play(bell), h = play(hat), sn = play(snare);
    printf("defaults: kick %.0f Hz %.2f s peak %.2f, snare %.2f s, hat %.2f s, cowbell %.2f s peak %.2f\n", crossingsHz(k), k.length / 44100.0, k.peak, sn.length / 44100.0, h.length / 44100.0, b.length / 44100.0, b.peak);
    CHECK(crossingsHz(k) < 120);         // a kick: low
    CHECK(k.length < 44100 * 6 / 10);
    CHECK(h.length < 44100 * 3 / 10);    // a closed hat: short
    CHECK(crossingsHz(h) > 3000);        // and bright
    CHECK(b.length > 44100 / 5 && b.length < 44100 * 8 / 10);
    // the cowbell: its two square waves, not much else
    const double m554 = magnitude(b, 256, 4096, 554.4), m784 = magnitude(b, 256, 4096, 784.0), m300 = magnitude(b, 256, 4096, 300), m1000 = magnitude(b, 256, 4096, 1000);
    CHECK(m554 > 10 * m300 && m784 > 10 * m300 && m554 > 10 * m1000);
    for (const Take* t : { &k, &sn, &h, &b })
        CHECK(t->peak > 0.08f && t->peak < 1.0f);

    // a little groove to listen to: build/perc_defaults.wav
    Engine& e = *g_engine;
    e.init();
    e.setSound(0, kick);
    e.setSound(1, snare);
    e.setSound(2, hat);
    e.setSound(3, bell);
    const int step = 44100 * 60 / 120 / 4 / 64 * 64; // 16ths at 120 (to the block)
    std::vector<float> l(step * 32, 0), r(step * 32, 0);
    const char* pat[4] = { "x...x...x...x..x", "....x.......x...", "x.x.x.x.x.x.x.xx", "..x.....x.x....." };
    for (int s = 0; s < 32; s++)
    {
        for (int v = 0; v < 4; v++)
            if (pat[v][s % 16] == 'x')
                e.trigger(v, v == 2 && s % 2 ? 70 : 110, 60);
        e.render(&l[s * step], &r[s * step], step);
    }
    writeWav("build/perc_defaults.wav", l, r);
}

static void testModulation()
{
    Sound bell;
    defaultSound(3, bell);
    const Take plain = play(bell);

    // LFO 1 on the cutoff: another sound
    Sound wob = bell;
    wob.value[pLfoDest] = pFilterFreq + 1;
    wob.value[pLfoAmount] = 100;
    wob.value[pLfoFreq] = 90;
    wob.value[pLfoSync] = 0;
    const Take w = play(wob);
    CHECK(!same(plain, w));

    // the same on LFO 2
    Sound wob2 = bell;
    wob2.value[pLfo2Dest] = pFilterFreq + 1;
    wob2.value[pLfo2Amount] = 100;
    wob2.value[pLfo2Freq] = 90;
    CHECK(!same(plain, play(wob2)));

    // no amount: no change
    Sound none = wob;
    none.value[pLfoAmount] = 0;
    CHECK(same(plain, play(none)));

    // taking the destination away gives the sound back as it was
    Engine& e = *g_engine;
    e.init();
    e.setSound(0, wob);
    e.trigger(0, 110, 60);
    std::vector<float> l(64 * 200, 0), r(64 * 200, 0);
    e.render(l.data(), r.data(), (int)l.size());
    e.setValue(0, pLfoDest, 0);
    std::fill(l.begin(), l.end(), 0.f);
    std::fill(r.begin(), r.end(), 0.f);
    e.render(l.data(), r.data(), (int)l.size()); // one block puts the cutoff back
    e.trigger(0, 110, 60);
    Take again;
    again.l.assign(plain.l.size(), 0);
    again.r.assign(plain.l.size(), 0);
    for (size_t off = 0; off < again.l.size(); off += 512)
        e.render(&again.l[off], &again.r[off], (int)std::min<size_t>(512, again.l.size() - off));
    CHECK(same(plain, again));

    // velocity on the pitch: a harder hit is higher
    Sound vp = bell;
    vp.value[pVelDest] = pCoarse + 1;
    vp.value[pVelAmount] = 40;
    vp.value[pVelVolume] = 0;
    CHECK(crossingsHz(play(vp, 3, 127)) > crossingsHz(play(vp, 3, 20)) * 1.5);

    // velocity sets the level, or not
    Sound vv = bell;
    CHECK(play(vv, 5, 30).peak < play(vv, 5, 127).peak * 0.4f);
    vv.value[pVelVolume] = 0;
    CHECK(fabsf(play(vv, 5, 30).peak - play(vv, 5, 127).peak) < 0.001f);

    // the note: an octave up doubles the pitch
    const double c = crossingsHz(play(bell, 3, 110, 60)), up = crossingsHz(play(bell, 3, 110, 72));
    CHECK(up > c * 1.8 && up < c * 2.2);

    // velocity moving LFO 1's amount: a soft hit wobbles less
    Sound va = wob;
    va.value[pLfoAmount] = 60;
    va.value[pVelDest] = pLfoAmount + 1;
    va.value[pVelAmount] = 60;
    va.value[pVelVolume] = 0;
    CHECK(!same(play(va, 5, 127), play(va, 5, 1)));
}

static void testEngine()
{
    Engine& e = *g_engine;
    Sound bell, hat;
    defaultSound(3, bell);
    defaultSound(2, hat);

    // stop: silence at once
    e.init();
    e.setSound(5, bell);
    e.trigger(5, 110, 60);
    std::vector<float> l(64 * 8, 0), r(64 * 8, 0);
    e.render(l.data(), r.data(), (int)l.size());
    CHECK(e.sounding(5) && e.level(5) > 0.01f);
    e.stop(5);
    std::fill(l.begin(), l.end(), 0.f);
    e.render(l.data(), r.data(), (int)l.size());
    float peak = 0;
    for (float x : l)
        peak = fmaxf(peak, fabsf(x));
    CHECK(peak == 0 && !e.sounding(5));

    // a voice that ends stops being rendered
    e.init();
    e.setSound(0, hat);
    e.trigger(0, 110, 60);
    std::vector<float> big(44100 / 64 * 64, 0), big2(big.size(), 0);
    e.render(big.data(), big2.data(), (int)big.size());
    CHECK(!e.sounding(0));

    // pan
    Sound left = bell;
    left.value[pPan] = 0;
    const Take tl = play(left);
    double el = 0, er = 0;
    for (size_t i = 0; i < tl.l.size(); i++)
    {
        el += tl.l[i] * tl.l[i];
        er += tl.r[i] * tl.r[i];
    }
    CHECK(el > 0 && er == 0);

    // the hat: open is longer than closed
    Sound open = hat;
    open.hatOpen = 1;
    CHECK(play(open).length > play(hat).length * 2);

    // the sample-rate reducer: steps
    Sound crush = bell;
    crush.value[pSampleRate] = 20;
    const Take tc = play(crush, 3);
    int held = 0;
    for (size_t i = 1; i < 4096; i++)
        held += tc.l[i] == tc.l[i - 1];
    CHECK(held > 2048);

    // a synced LFO follows the tempo: 1/4 at 120 = 2 Hz, at 60 = 1 Hz
    Sound syn = bell;
    syn.value[pLfoSync] = 6; // 1/4
    syn.value[pLfoDest] = pVolume + 1;
    syn.value[pLfoAmount] = 127;
    syn.value[pLfoWave] = 4; // square
    syn.value[pDecay] = 127;
    syn.value[pVolume] = 64;
    e.init();
    e.setTempo(120);
    e.setSound(0, syn);
    e.trigger(0, 110, 60);
    // count the square's edges over 2 s: about 2 a second (1 per half period... 2 per period)
    auto edges = [&](float bpm) {
        e.setTempo(bpm);
        e.init();
        e.setSound(0, syn);
        e.trigger(0, 110, 60);
        std::vector<float> a(44100 * 2 / 64 * 64, 0), b(a.size(), 0);
        e.render(a.data(), b.data(), (int)a.size());
        int n = 0;
        bool high = false;
        for (size_t blk = 0; blk + 64 <= a.size(); blk += 64)
        {
            float m = 0;
            for (int i = 0; i < 64; i++)
                m = fmaxf(m, fabsf(a[blk + i]));
            const bool h = m > 0.02f;
            n += h != high;
            high = h;
        }
        return n;
    };
    const int at120 = edges(120), at60 = edges(60);
    printf("synced LFO edges in 2 s: %d at 120 bpm, %d at 60 bpm\n", at120, at60);
    CHECK(at120 >= 7 && at120 <= 9);
    CHECK(at60 >= 3 && at60 <= 5);
    e.setTempo(120);

    // the values as text
    char t[16];
    paramText(pPan, 64, t, sizeof t);
    CHECK(strcmp(t, "C") == 0);
    paramText(pPan, 0, t, sizeof t);
    CHECK(strcmp(t, "L64") == 0);
    paramText(pCoarse, 60, t, sizeof t);
    CHECK(strcmp(t, "C4") == 0);
    paramText(pFilterType, 0, t, sizeof t);
    CHECK(strcmp(t, "LP") == 0);
    paramText(pLfoDest, 0, t, sizeof t);
    CHECK(strcmp(t, "None") == 0);
    paramText(pLfoDest, pFilterFreq + 1, t, sizeof t);
    CHECK(strcmp(t, "Cutoff") == 0);
    paramText(pLfoSync, 10, t, sizeof t);
    CHECK(strcmp(t, "1/16") == 0);
    paramText(pTransWave, 13, t, sizeof t);
    CHECK(strcmp(t, "Snap 2") == 0);
    paramText(pFine, 64, t, sizeof t);
    CHECK(strcmp(t, "+0") == 0);
    CHECK(paramUsed(pNoiseMix, kSnare) && !paramUsed(pNoiseMix, kDrum));
    CHECK(paramUsed(pDecayOpen, kHihat) && !paramUsed(pDecayOpen, kCymbal));
    printf("engine: %zu bytes (8 voices)\n", sizeof(Engine));
    CHECK(sizeof(Engine) <= 4096);
}

int main()
{
    testKits();
    testDefaults();
    testModulation();
    testEngine();
    if (failures)
        printf("perc_test: %d FAILED\n", failures);
    else
        printf("perc_test: all passed\n");
    return failures != 0;
}
