// GORILLAGRAIN previews (host): renders WAVs with the real DSP core.
//   preview <GORILLA.wav> <out dir>
#include "gorillagrain/grain_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<int16_t> loadWav(const char* path, int& channels, int& rate)
{
    std::vector<int16_t> out;
    FILE* f = fopen(path, "rb");
    if (!f)
        return out;
    char hdr[44];
    if (fread(hdr, 1, 44, f) != 44)
    {
        fclose(f);
        return out;
    }
    channels = *(int16_t*)(hdr + 22);
    rate = *(int32_t*)(hdr + 24);
    const int32_t bytes = *(int32_t*)(hdr + 40);
    out.resize((size_t)bytes / 2);
    if (fread(out.data(), 2, out.size(), f) != out.size())
        out.clear();
    fclose(f);
    return out;
}

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = fopen(path.c_str(), "wb");
    const uint32_t n = (uint32_t)l.size(), data = n * 4, riff = 36 + data;
    const uint16_t ch = 2, bits = 16, align = 4, fmt = 1;
    const uint32_t sr = 44100, br = sr * 4, fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtLen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < n; i++)
    {
        int16_t s[2] = { (int16_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, l[i])) * 32000.0f),
                         (int16_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, r[i])) * 32000.0f) };
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

struct NoteEv
{
    float at, len;
    int note;
};

static void render(const char* name, const std::string& dir, grain::Params p, const grain::Source& src,
                   const std::vector<NoteEv>& notes, float seconds)
{
    constexpr int kVoices = 4, kBlock = 128;
    grain::Voice v[kVoices];
    int voiceNote[kVoices], voiceAge[kVoices];
    for (int i = 0; i < kVoices; i++)
    {
        v[i].init((uint32_t)i + 1);
        voiceNote[i] = -1;
        voiceAge[i] = 0;
    }
    morph::Fx fx;
    std::vector<uint8_t> mem(morph::Fx::kMemoryBytes);
    fx.attach(mem.data());
    morph::Params fxp;
    p.fillFx(fxp);
    const int blocks = (int)(seconds * 44100 / kBlock);
    std::vector<float> L, R;
    float bl[kBlock], br[kBlock], ml[kBlock], mr[kBlock];
    for (int b = 0; b < blocks; b++)
    {
        const float t0 = (float)(b * kBlock) / 44100.0f, t1 = (float)((b + 1) * kBlock) / 44100.0f;
        for (const auto& e : notes)
        {
            if (e.at >= t0 && e.at < t1)
            {
                int pick = 0;
                for (int i = 0; i < kVoices; i++)
                    if (voiceNote[i] < 0 && !v[i].active())
                    {
                        pick = i;
                        break;
                    }
                    else if (voiceAge[i] > voiceAge[pick])
                        pick = i;
                v[pick].noteOn(p, src, (float)e.note, 0.9f);
                voiceNote[pick] = e.note;
                voiceAge[pick] = 0;
            }
            if (e.at + e.len >= t0 && e.at + e.len < t1)
                for (int i = 0; i < kVoices; i++)
                    if (voiceNote[i] == e.note)
                    {
                        v[i].noteOff();
                        voiceNote[i] = -1;
                    }
        }
        for (int i = 0; i < kBlock; i++)
            ml[i] = mr[i] = 0.0f;
        for (int k = 0; k < kVoices; k++)
        {
            v[k].render(p, 100.0f, 0.0f, bl, br, kBlock);
            voiceAge[k]++;
            for (int i = 0; i < kBlock; i++)
            {
                ml[i] += bl[i] * 0.5f;
                mr[i] += br[i] * 0.5f;
            }
        }
        fx.process(fxp, 100.0f, v[0].fxMod, ml, mr, kBlock);
        for (int i = 0; i < kBlock; i++)
        {
            L.push_back(ml[i]);
            R.push_back(mr[i]);
        }
    }
    float peak = 0;
    for (size_t i = 0; i < L.size(); i++)
        peak = std::fmax(peak, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
    writeWav(dir + "/" + name + ".wav", L, R);
    printf("%-22s peak %.2f\n", name, peak);
}

int main(int argc, char** argv)
{
    if (argc < 3)
        return 1;
    int ch = 1, rate = 44100;
    auto data = loadWav(argv[1], ch, rate);
    if (data.empty())
    {
        printf("no sample\n");
        return 1;
    }
    grain::Source src;
    src.data = data.data();
    src.channels = ch;
    src.frames = (uint32_t)(data.size() / (size_t)ch);
    src.rateRatio = (float)rate / 44100.0f;
    src.rootNote = 60.0f;
    const std::string dir = argv[2];

    // 1: the default patch, a slow minor-9 pad
    {
        grain::Params p;
        render("1_default_pad", dir, p, src,
               { { 0.2f, 5.5f, 48 }, { 0.2f, 5.5f, 55 }, { 0.2f, 5.5f, 63 }, { 0.2f, 5.5f, 70 },
                 { 6.0f, 4.0f, 44 }, { 6.0f, 4.0f, 51 }, { 6.0f, 4.0f, 60 }, { 6.0f, 4.0f, 67 } },
               13.0f);
    }
    // 2: frozen shimmer: Scan 0, sparkle and reverse, long smooth grains
    {
        grain::Params p;
        p.scan = 0.0f;
        p.position = 0.55f;
        p.sizeMs = 380.0f;
        p.density = 14.0f;
        p.sparkle = 0.35f;
        p.reverse = 0.3f;
        p.spray = 0.2f;
        p.reverbMix = 0.45f;
        p.reverbDecay = 0.85f;
        render("2_frozen_shimmer", dir, p, src, { { 0.2f, 6.0f, 60 }, { 0.2f, 6.0f, 67 }, { 2.0f, 4.2f, 75 } }, 10.0f);
    }
    // 3: plucky grain arp: Pluck window, dense short grains, delay
    {
        grain::Params p;
        p.window = grain::kWindowPluck;
        p.sizeMs = 60.0f;
        p.density = 9.0f;
        p.spray = 0.05f;
        p.jitter = 0.0f;
        p.amp = { 0.002f, 0.4f, 0.0f, 0.3f };
        p.delayType = morph::kDelayPingPong;
        p.delayMix = 0.3f;
        p.reverbType = morph::kReverbPlate;
        p.reverbMix = 0.2f;
        std::vector<NoteEv> ev;
        const int seq[8] = { 60, 63, 67, 70, 72, 70, 67, 63 };
        for (int i = 0; i < 24; i++)
            ev.push_back({ 0.1f + i * 0.3f, 0.25f, seq[i % 8] });
        render("3_pluck_arp", dir, p, src, ev, 9.5f);
    }
    // 4: tape-scan texture: scanning backwards slowly, wide spray, dark filter swell from Env 1
    {
        grain::Params p;
        p.scan = -0.35f;
        p.spray = 0.6f;
        p.jitter = 0.3f;
        p.sizeMs = 220.0f;
        p.density = 30.0f;
        p.filterType = grain::kFilterLp24;
        p.cutoff = 900.0f;
        p.resonance = 0.3f;
        p.env[0].env = { 2.5f, 3.0f, 0.4f, 2.0f };
        p.env[0].slot[0] = { grain::kDestCutoff, 0.6f };
        render("4_backwards_texture", dir, p, src, { { 0.2f, 7.0f, 48 }, { 0.2f, 7.0f, 60 } }, 10.0f);
    }
    return 0;
}
