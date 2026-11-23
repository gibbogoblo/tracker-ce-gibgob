// Renders CHILLAMANG demo pieces to WAV files on the computer, with the same
// DSP code (chill_dsp.h) as the plugin on the Mini.
//
//   g++ -O2 -std=c++20 -I.. preview.cpp -o preview && ./preview out_dir

#include "chillamang/chill_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace chill;

namespace
{

struct Ev
{
    float t;   // start, beats
    int note;
    float vel; // 0..1
    float len; // beats
};

struct Piece
{
    const char* file;
    float bpm;
    float beats; // with the tail
    Params p;
    std::vector<Ev> ev;
};

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
    {
        printf("cannot write %s\n", path.c_str());
        exit(1);
    }
    const uint32_t n = (uint32_t)l.size();
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + n * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(44100);
    u32(44100 * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(n * 4);
    for (uint32_t i = 0; i < n; ++i)
    {
        const int16_t a = (int16_t)std::lround(std::clamp(l[i], -1.0f, 1.0f) * 32767.0f);
        const int16_t b = (int16_t)std::lround(std::clamp(r[i], -1.0f, 1.0f) * 32767.0f);
        fwrite(&a, 2, 1, f);
        fwrite(&b, 2, 1, f);
    }
    fclose(f);
}

// plays a piece through 8 voices (the Mini's synth voices), the instrument's
// effects and MORPHANGUTAN's delay / reverb; returns the peak
float render(const Piece& pc, std::vector<float>& L, std::vector<float>& R)
{
    const int total = (int)(pc.beats * 60.0f / pc.bpm * 44100.0f);
    L.assign(total, 0.0f);
    R.assign(total, 0.0f);
    constexpr int kVoices = 8;
    Voice v[kVoices];
    int owner[kVoices];
    double offAt[kVoices];
    uint32_t age[kVoices] = { 0 };
    for (int i = 0; i < kVoices; ++i)
    {
        v[i].init(i + 1);
        owner[i] = -1;
        offAt[i] = 1e18;
    }
    static uint8_t modMem[ModFx::kMemoryBytes];
    static uint8_t fxMem[morph::Fx::kMemoryBytes];
    ModFx mod;
    mod.attach(modMem);
    morph::Fx fx;
    fx.attach(fxMem);
    morph::Params fp;
    pc.p.fillFx(fp);
    float fxMod[4] = { 0, 0, 0, 0 };
    const double spb = 60.0 / pc.bpm * 44100.0;
    std::vector<int> started(pc.ev.size(), 0);
    constexpr int kBlock = 32;
    uint32_t clock = 0;
    for (int s0 = 0; s0 < total; s0 += kBlock)
    {
        const int n = std::min(kBlock, total - s0);
        for (int k = 0; k < kVoices; ++k)
            if (owner[k] >= 0 && s0 >= offAt[k])
            {
                v[k].noteOff(pc.p);
                owner[k] = -1;
            }
        for (size_t e = 0; e < pc.ev.size(); ++e)
        {
            if (started[e] || pc.ev[e].t * spb > s0)
                continue;
            started[e] = 1;
            // a free voice, else the quietest
            int best = -1;
            for (int k = 0; k < kVoices && best < 0; ++k)
                if (!v[k].active())
                    best = k;
            if (best < 0)
            {
                best = 0;
                for (int k = 1; k < kVoices; ++k)
                    if (age[k] < age[best])
                        best = k;
            }
            v[best].noteOn(pc.p, (float)pc.ev[e].note, pc.ev[e].vel);
            owner[best] = (int)e;
            age[best] = ++clock;
            offAt[best] = (pc.ev[e].t + pc.ev[e].len) * spb;
        }
        float bl[kBlock] = { 0 }, br[kBlock] = { 0 };
        for (int k = 0; k < kVoices; ++k)
            v[k].render(pc.p, 0.0f, bl, br, n);
        mod.process(pc.p, pc.bpm, bl, br, n);
        fx.process(fp, pc.bpm, fxMod, bl, br, n);
        for (int i = 0; i < n; ++i)
        {
            L[s0 + i] = bl[i];
            R[s0 + i] = br[i];
        }
    }
    float peak = 0.0f;
    for (int i = 0; i < total; ++i)
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
    return peak;
}

void chord(std::vector<Ev>& ev, float t, std::initializer_list<int> notes, float vel, float len, float strum = 0.012f)
{
    int i = 0;
    for (int nn : notes)
    {
        ev.push_back({ t + strum * (float)i, nn, std::clamp(vel + 0.03f * (float)((i * 7) % 3 - 1), 0.05f, 1.0f), len });
        ++i;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    std::vector<Piece> pieces;

    // 1. neo-soul chords: rootless 9ths / 13ths, suitcase tremolo, plate
    {
        Piece pc{ "1_chillamang_neo_soul_chords.wav", 78.0f, 36.0f, Params{}, {} };
        for (int rep = 0; rep < 2; ++rep)
        {
            const float b = rep * 16.0f;
            const float v = rep == 0 ? 0.55f : 0.62f;
            chord(pc.ev, b + 0, { 38, 53, 60, 64, 69 }, v, 3.6f);        // Dm9
            chord(pc.ev, b + 3.5f, { 43, 53, 59, 64, 69 }, v + 0.05f, 4.2f); // G13 (pushed)
            chord(pc.ev, b + 8, { 48, 52, 59, 62, 67 }, v, 3.6f);        // Cmaj9
            chord(pc.ev, b + 11.5f, { 45, 55, 61, 65 }, v + 0.08f, 4.0f); // A7(b13)
            // a little top line
            pc.ev.push_back({ b + 2.0f, 74, 0.5f, 0.5f });
            pc.ev.push_back({ b + 2.5f, 72, 0.45f, 0.5f });
            pc.ev.push_back({ b + 6.5f, 71, 0.5f, 1.0f });
            pc.ev.push_back({ b + 10.0f, 76, 0.52f, 0.5f });
            pc.ev.push_back({ b + 10.5f, 74, 0.45f, 1.0f });
            pc.ev.push_back({ b + 14.5f, 73, 0.55f, 1.2f });
        }
        chord(pc.ev, 32.0f, { 38, 53, 57, 60, 64, 69 }, 0.5f, 3.5f, 0.05f); // Dm9 to end
        pieces.push_back(pc);
    }
    // 2. mellow ballad: felt hammers, dark, slow tremolo, more room
    {
        Piece pc{ "2_chillamang_mellow_ballad.wav", 66.0f, 26.0f, Params{}, {} };
        Params& p = pc.p;
        p.felt = 0.55f;
        p.bark = 0.12f;
        p.body = 0.32f;
        p.brightness = 3400.0f;
        p.tremDepth = 0.22f;
        p.tremRate = 3.0f;
        p.reverbMix = 0.32f;
        p.reverbDecay = 0.72f;
        p.tineLevel = 0.25f;
        p.tineTone = 0.85f;
        p.warmth = 0.8f;
        const int prog[4][4] = { { 41, 57, 60, 64 }, { 40, 55, 59, 62 }, { 38, 53, 57, 60 }, { 43, 53, 59, 62 } };
        for (int c = 0; c < 4; ++c)
        {
            const float b = c * 5.0f;
            pc.ev.push_back({ b, prog[c][0], 0.45f, 4.8f });
            for (int k = 1; k < 4; ++k)
                pc.ev.push_back({ b + 0.5f * (float)k, prog[c][k], 0.38f + 0.04f * (float)k, 4.5f - 0.5f * (float)k });
        }
        const int mel[] = { 72, 71, 69, 67, 69, 71, 72, 74 };
        for (int k = 0; k < 8; ++k)
            pc.ev.push_back({ 2.0f + 2.5f * (float)k, mel[k], 0.42f + 0.05f * (float)(k % 3), 1.8f });
        chord(pc.ev, 20.0f, { 41, 57, 60, 64, 69 }, 0.4f, 5.0f, 0.12f);
        pieces.push_back(pc);
    }
    // 3. bark: the same chord from very soft to very hard, nearly dry
    {
        Piece pc{ "3_chillamang_bark_soft_to_hard.wav", 90.0f, 26.0f, Params{}, {} };
        Params& p = pc.p;
        p.bark = 0.7f;
        p.tremDepth = 0.0f;
        p.chorusMix = 0.0f;
        p.reverbMix = 0.1f;
        for (int k = 0; k < 8; ++k)
            chord(pc.ev, 2.0f * (float)k, { 48, 55, 58, 63 }, 0.15f + 0.12f * (float)k, 1.6f, 0.0f);
        for (int k = 0; k < 5; ++k)
            pc.ev.push_back({ 17.0f + 1.5f * (float)k, 36 + 12 * k, 0.85f, 1.2f });
        pieces.push_back(pc);
    }
    // 4. lo-fi dusty: aged, driven, wobbling, phaser and tape echoes
    {
        Piece pc{ "4_chillamang_lofi_dusty.wav", 84.0f, 26.0f, Params{}, {} };
        Params& p = pc.p;
        p.age = 0.6f;
        p.warmth = 1.0f;
        p.drive = 0.45f;
        p.vibDepth = 0.12f;
        p.vibRate = 0.9f;
        p.phaserMix = 0.35f;
        p.chorusMix = 0.1f;
        p.tremDepth = 0.15f;
        p.delayType = morph::kDelayTape;
        p.delayTime = 7; // 1/8D
        p.delayMix = 0.25f;
        p.delayFeedback = 0.35f;
        p.reverbType = morph::kReverbDark;
        p.reverbMix = 0.2f;
        for (int rep = 0; rep < 2; ++rep)
        {
            const float b = rep * 10.0f;
            chord(pc.ev, b + 0, { 41, 56, 60, 63, 67 }, 0.6f, 2.3f);  // Fm9
            chord(pc.ev, b + 2.5f, { 46, 56, 60, 62, 65 }, 0.55f, 2.3f); // Bb9
            chord(pc.ev, b + 5, { 39, 55, 58, 62, 65 }, 0.6f, 2.3f);  // Ebmaj9
            chord(pc.ev, b + 7.5f, { 44, 55, 60, 63, 67 }, 0.58f, 2.3f); // Abmaj9
        }
        chord(pc.ev, 20.0f, { 41, 56, 60, 63, 67 }, 0.5f, 4.0f, 0.08f);
        pieces.push_back(pc);
    }

    // 5. serene: a fully round ping, very warm, soft hammers, long hall-like plate
    {
        Piece pc{ "5_chillamang_serene.wav", 60.0f, 24.0f, Params{}, {} };
        Params& p = pc.p;
        p.tineTone = 1.0f;
        p.tineLevel = 0.45f;
        p.tineDecay = 0.55f;
        p.warmth = 1.0f;
        p.felt = 0.35f;
        p.bark = 0.1f;
        p.body = 0.35f;
        p.brightness = 3800.0f;
        p.hammer = 0.15f;
        p.decay = 0.75f;
        p.tremDepth = 0.25f;
        p.tremRate = 2.4f;
        p.chorusMix = 0.25f;
        p.reverbMix = 0.35f;
        p.reverbDecay = 0.8f;
        // Fmaj9 - Em7 - Dm9 - Cmaj7, each chord rolled slowly, with a few high bell notes
        const int prog[4][5] = { { 41, 57, 60, 64, 67 }, { 40, 55, 59, 62, 67 }, { 38, 53, 57, 60, 64 }, { 36, 52, 55, 59, 64 } };
        for (int c = 0; c < 4; ++c)
        {
            const float b = c * 5.0f;
            for (int k = 0; k < 5; ++k)
                pc.ev.push_back({ b + 0.18f * (float)k, prog[c][k], 0.42f + 0.03f * (float)(k % 2), 4.6f });
            pc.ev.push_back({ b + 2.0f, prog[c][4] + 12, 0.38f, 1.5f });
            pc.ev.push_back({ b + 3.0f, prog[c][3] + 12, 0.34f, 1.8f });
        }
        chord(pc.ev, 20.0f, { 41, 57, 60, 64, 69, 76 }, 0.4f, 4.0f, 0.2f);
        pieces.push_back(pc);
    }
    // 6. the tine tone: the same phrase glassy (a) and round (b), nearly dry
    for (int k = 0; k < 2; ++k)
    {
        Piece pc{ k == 0 ? "6a_chillamang_tine_glassy.wav" : "6b_chillamang_tine_round.wav", 92.0f, 14.0f, Params{}, {} };
        Params& p = pc.p;
        p.tineTone = k == 0 ? 0.0f : 1.0f;
        p.tineLevel = 0.6f;
        p.tremDepth = 0.0f;
        p.chorusMix = 0.0f;
        p.reverbMix = 0.12f;
        const int mel[8] = { 72, 76, 79, 83, 81, 79, 76, 74 };
        for (int i = 0; i < 8; ++i)
            pc.ev.push_back({ 0.75f * (float)i, mel[i], 0.65f + 0.05f * (float)(i % 3), 0.7f });
        chord(pc.ev, 6.5f, { 48, 55, 64, 71, 74 }, 0.6f, 5.0f, 0.03f);
        pieces.push_back(pc);
    }

    for (const Piece& pc : pieces)
    {
        std::vector<float> L, R;
        const float peak = render(pc, L, R);
        writeWav(out + "/" + pc.file, L, R);
        printf("%-40s peak %.3f%s\n", pc.file, peak, peak > 0.99f ? "  CLIPS" : "");
    }
    return 0;
}
