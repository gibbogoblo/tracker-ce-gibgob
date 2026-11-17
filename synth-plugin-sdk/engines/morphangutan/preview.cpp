// Renders MORPHANGUTAN demo patches to WAV files on the computer, using the
// same DSP code (morph_dsp.h) as the plugin on the Tracker.
//
//   g++ -O2 -std=c++20 preview.cpp -o preview && ./preview out_dir

#include "morph_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace morph;

namespace
{

struct Ev
{
    float t;   // start, in beats
    int note;
    float vel; // 0..1
    float len; // beats
};

struct Song
{
    const char* file;
    const char* title;
    float bpm;
    float beats; // length incl. tail
    bool mono;   // one voice, legato (bass / lead)
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
    const uint32_t dataBytes = n * 4;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(44100);
    u32(44100 * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (float v : { l[i], r[i] })
        {
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            int16_t s = (int16_t)lrintf(v * 32767.0f);
            fwrite(&s, 2, 1, f);
        }
    }
    fclose(f);
}

void render(const Song& s, const std::string& dir)
{
    constexpr int kVoices = 8;
    constexpr int kBlock = 128;
    static Voice voices[kVoices];
    for (int v = 0; v < kVoices; ++v)
    {
        voices[v] = Voice();
        voices[v].init((uint32_t)v + 1);
    }
    static std::vector<uint8_t> fxMem(Fx::kMemoryBytes);
    Fx fx;
    fx.attach(fxMem.data());

    const float spb = 60.0f / s.bpm * kSampleRate; // samples per beat
    const int total = (int)(s.beats * spb);
    std::vector<float> L(total), R(total);

    struct Pending
    {
        int at;
        bool on;
        int note;
        float vel;
    };
    std::vector<Pending> q;
    for (const Ev& e : s.ev)
    {
        q.push_back({ (int)(e.t * spb), true, e.note, e.vel });
        q.push_back({ (int)((e.t + e.len) * spb), false, e.note, 0.0f });
    }
    std::sort(q.begin(), q.end(), [](const Pending& a, const Pending& b)
              { return a.at != b.at ? a.at < b.at : (!a.on && b.on); });

    int voiceNote[kVoices];
    int age[kVoices] = { 0 };
    for (int& n : voiceNote)
    {
        n = -1;
    }
    std::vector<int> monoHeld;
    int clock = 0;
    size_t qi = 0;
    float tmpL[kBlock], tmpR[kBlock];
    float peak = 0.0f;

    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = std::min(kBlock, total - pos);
        // events are applied at the start of the block they fall in (as on the Tracker)
        while (qi < q.size() && q[qi].at < pos + n)
        {
            const Pending& e = q[qi++];
            if (s.mono)
            {
                if (e.on)
                {
                    const bool legato = !monoHeld.empty();
                    monoHeld.push_back(e.note);
                    voices[0].noteOn(s.p, (float)e.note, e.vel, legato);
                }
                else
                {
                    for (size_t i = 0; i < monoHeld.size(); ++i)
                    {
                        if (monoHeld[i] == e.note)
                        {
                            monoHeld.erase(monoHeld.begin() + (long)i);
                            break;
                        }
                    }
                    if (monoHeld.empty())
                    {
                        voices[0].noteOff();
                    }
                    else
                    {
                        voices[0].noteOn(s.p, (float)monoHeld.back(), 0.8f, true);
                    }
                }
                continue;
            }
            if (e.on)
            {
                // free voice, else the oldest one
                int best = -1;
                for (int v = 0; v < kVoices && best < 0; ++v)
                {
                    if (!voices[v].active())
                    {
                        best = v;
                    }
                }
                if (best < 0)
                {
                    best = 0;
                    for (int v = 1; v < kVoices; ++v)
                    {
                        if (age[v] < age[best])
                        {
                            best = v;
                        }
                    }
                }
                voiceNote[best] = e.note;
                age[best] = ++clock;
                voices[best].noteOn(s.p, (float)e.note, e.vel, false);
            }
            else
            {
                for (int v = 0; v < kVoices; ++v)
                {
                    if (voiceNote[v] == e.note)
                    {
                        voices[v].noteOff();
                        voiceNote[v] = -1;
                    }
                }
            }
        }

        float* outL = &L[pos];
        float* outR = &R[pos];
        for (int i = 0; i < n; ++i)
        {
            outL[i] = outR[i] = 0.0f;
        }
        for (int v = 0; v < kVoices; ++v)
        {
            if (!voices[v].active())
            {
                voices[v].tickIdle(s.p, s.bpm, n);
                continue;
            }
            voices[v].render(s.p, s.bpm, 0.0f, tmpL, tmpR, n);
            for (int i = 0; i < n; ++i)
            {
                outL[i] += tmpL[i];
                outR[i] += tmpR[i];
            }
        }
        fx.process(s.p, s.bpm, voices[0].fxMod, outL, outR, n);
        for (int i = 0; i < n; ++i)
        {
            peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i]))
            {
                printf("  NaN/inf in %s at %d\n", s.file, pos + i);
                exit(1);
            }
        }
    }

    // fade out the last 50 ms
    const int fade = 2205;
    for (int i = 0; i < fade && i < total; ++i)
    {
        const float g = (float)i / fade;
        L[total - 1 - i] *= g;
        R[total - 1 - i] *= g;
    }
    writeWav(dir + "/" + s.file, L, R);
    printf("  %-28s peak %.2f (%.1f dBFS)  %s\n", s.file, peak, 20.0f * std::log10(peak + 1e-9f),
           s.title);
}

std::vector<Ev> chordSeq(const std::vector<std::vector<int>>& chords, float each, float len, float vel)
{
    std::vector<Ev> ev;
    for (size_t c = 0; c < chords.size(); ++c)
    {
        for (int n : chords[c])
        {
            ev.push_back({ (float)c * each, n, vel, len });
        }
    }
    return ev;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<Song> songs;

    // 1. The morph: one note, MAMA morphs sine > tri > saw > square and back,
    //    BABY does the opposite. Filter wide open, no FX.
    {
        Song s{ "01_morph_sweep.wav", "MAMA + BABY morph sweep (LFOs on morph)", 120, 18, false, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 1.5f;
        p.babyMorph = 1.5f;
        p.babyLevel = 0.5f;
        p.papaLevel = 0.3f;
        p.filterType = kFilterLp12;
        p.cutoff = 16000.0f;
        p.resonance = 0.0f;
        p.keyTrack = 0.0f;
        p.amp = { 0.05f, 0.2f, 1.0f, 0.6f };
        p.env[0].slot[0] = {};
        p.lfo[0] = LfoParams{ kLfoTriangle, 0.125f, 0, true, { { kDestMamaMorph, 0.5f }, {} } };
        p.lfo[1] = LfoParams{ kLfoTriangle, 0.125f, 0, true, { { kDestBabyMorph, -0.5f }, {} } };
        s.ev = { { 0, 45, 0.9f, 16 } };
        songs.push_back(s);
    }

    // 2. Ape bass: saw + square, PAPA sub an octave down, LP24 + filter envelope, glide
    {
        Song s{ "02_ape_bass.wav", "Bass: LP24, filter env, PAPA sub, legato glide", 124, 17, true, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 2.0f;
        p.babyMorph = 3.0f;
        p.babyTune = -12.0f;
        p.babyFine = 4.0f;
        p.babyLevel = 0.5f;
        p.papaMorph = 0.0f;
        p.papaOctave = -1;
        p.papaLevel = 0.7f;
        p.filterType = kFilterLp24;
        p.cutoff = 260.0f;
        p.resonance = 0.55f;
        p.drive = 0.35f;
        p.keyTrack = 0.5f;
        p.amp = { 0.002f, 0.3f, 0.8f, 0.08f };
        p.env[0].env = { 0.001f, 0.22f, 0.0f, 0.15f };
        p.env[0].slot[0] = { kDestCutoff, 0.62f };
        p.glideMode = 2;
        p.glideTime = 0.06f;
        const int line[] = { 33, 33, 45, 33, 36, 33, 43, 31, 33, 33, 45, 40, 38, 36, 45, 31 };
        for (int bar = 0; bar < 2; ++bar)
        {
            for (int i = 0; i < 16; ++i)
            {
                const bool tie = (i == 3 || i == 11);
                s.ev.push_back({ bar * 8 + i * 0.5f, line[i], (i % 4 == 0) ? 1.0f : 0.75f,
                                 tie ? 0.6f : 0.35f });
            }
        }
        songs.push_back(s);
    }

    // 3. Shimmer pad: slow attack, detuned saws, LFO on cutoff, shimmer reverb
    {
        Song s{ "03_shimmer_pad.wav", "Pad: slow attack, detune, SHIMMER reverb", 90, 22, false, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 2.0f;
        p.babyMorph = 1.6f;
        p.babyFine = 12.0f;
        p.babyLevel = 0.7f;
        p.papaLevel = 0.2f;
        p.drift = 0.6f;
        p.filterType = kFilterLp12;
        p.cutoff = 1500.0f;
        p.resonance = 0.15f;
        p.amp = { 1.2f, 1.0f, 0.8f, 2.5f };
        p.volume = 0.55f;
        p.env[0].slot[0] = {};
        p.lfo[0] = LfoParams{ kLfoSine, 0.2f, 0, false, { { kDestCutoff, 0.18f }, { kDestPan, 0.3f } } };
        p.lfo[1] = LfoParams{ kLfoSmoothRandom, 0.5f, 0, false, { { kDestMamaMorph, 0.12f }, {} } };
        p.reverbType = kReverbShimmer;
        p.reverbDecay = 0.85f;
        p.reverbTone = 0.6f;
        p.reverbMix = 0.55f;
        s.ev = chordSeq({ { 50, 57, 62, 65, 69 }, { 46, 53, 58, 62, 69 }, { 43, 50, 55, 58, 67 },
                          { 45, 52, 57, 61, 64 } },
                        4, 3.6f, 0.7f);
        songs.push_back(s);
    }

    // 4. Plate keys: triangle-ish pluck, plate reverb + ping-pong delay
    {
        Song s{ "04_plate_keys.wav", "Keys: tri/saw pluck, PLATE reverb + PING-PONG delay", 110, 20, false, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 1.2f;
        p.babyMorph = 0.0f;
        p.babyTune = 12.0f;
        p.babyFine = 0.0f;
        p.babyLevel = 0.35f;
        p.papaLevel = 0.0f;
        p.filterType = kFilterLp12;
        p.cutoff = 1800.0f;
        p.resonance = 0.2f;
        p.amp = { 0.002f, 0.9f, 0.0f, 0.5f };
        p.env[0].env = { 0.001f, 0.4f, 0.1f, 0.4f };
        p.env[0].slot[0] = { kDestCutoff, 0.4f };
        p.delayType = kDelayPingPong;
        p.delayTime = 4; // 1/8 dotted
        p.delayFeedback = 0.45f;
        p.delayMix = 0.35f;
        p.reverbType = kReverbPlate;
        p.reverbDecay = 0.55f;
        p.reverbMix = 0.3f;
        const int mel[] = { 69, 72, 76, 74, 72, 69, 67, 64, 65, 69, 72, 77, 76, 72, 74, 71 };
        for (int i = 0; i < 16; ++i)
        {
            s.ev.push_back({ i * 0.75f, mel[i], 0.6f + 0.3f * (i % 3 == 0), 0.3f });
        }
        s.ev.push_back({ 0, 45, 0.6f, 5.5f });
        s.ev.push_back({ 6, 41, 0.6f, 5.5f });
        songs.push_back(s);
    }

    // 5. Dark lead: square/saw, vibrato via LFO, TAPE delay + DARK reverb
    {
        Song s{ "05_dark_lead.wav", "Lead: vibrato, glide, TAPE delay + DARK reverb", 96, 19, true, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 2.6f;
        p.babyMorph = 2.0f;
        p.babyFine = -9.0f;
        p.babyLevel = 0.6f;
        p.papaLevel = 0.25f;
        p.filterType = kFilterLp24;
        p.cutoff = 1400.0f;
        p.resonance = 0.3f;
        p.amp = { 0.03f, 0.5f, 0.85f, 0.4f };
        p.env[0].env = { 0.08f, 0.6f, 0.4f, 0.5f };
        p.env[0].slot[0] = { kDestCutoff, 0.3f };
        p.lfo[0] = LfoParams{ kLfoSine, 5.5f, 0, true, { { kDestPitch, 0.02f }, {} } };
        p.glideMode = 2;
        p.glideTime = 0.09f;
        p.delayType = kDelayTape;
        p.delayTime = 7; // 1/4 dotted
        p.delayFeedback = 0.5f;
        p.delayMix = 0.35f;
        p.reverbType = kReverbDark;
        p.reverbDecay = 0.8f;
        p.reverbTone = 0.4f;
        p.reverbMix = 0.4f;
        s.ev = { { 0, 62, 0.8f, 1.6f },  { 1.5f, 65, 0.8f, 0.6f }, { 2, 69, 0.9f, 2.0f },
                 { 4, 67, 0.8f, 0.9f },  { 5, 65, 0.8f, 0.9f },    { 6, 64, 0.8f, 2.0f },
                 { 8, 62, 0.8f, 1.0f },  { 9, 60, 0.8f, 0.6f },    { 9.5f, 62, 0.8f, 2.5f },
                 { 12, 57, 0.7f, 2.5f } };
        songs.push_back(s);
    }

    // 6. Comb pluck: noise + saw burst through a key-tracked COMB+ (plucked string)
    {
        Song s{ "06_comb_pluck.wav", "COMB+ filter, key tracked: plucked string", 100, 14, false, {}, {} };
        Params& p = s.p;
        // only a short burst of noise + saw (from the two envelopes) excites the comb
        p.mamaMorph = 2.0f;
        p.mamaLevel = 0.0f;
        p.babyLevel = 0.0f;
        p.papaLevel = 0.0f;
        p.noiseLevel = 0.0f;
        p.filterType = kFilterCombPlus;
        p.cutoff = 261.63f; // C4: with 100 % key track the comb plays the note
        p.keyTrack = 1.0f;
        p.resonance = 0.97f;
        p.amp = { 0.001f, 1.6f, 0.0f, 0.8f };
        p.env[0].env = { 0.001f, 0.03f, 0.0f, 0.03f };
        p.env[0].slot[0] = { kDestNoiseVol, 1.0f };
        p.env[1].slot[0] = { kDestMamaVol, 0.6f };
        p.reverbType = kReverbPlate;
        p.reverbDecay = 0.4f;
        p.reverbMix = 0.2f;
        const int arp[] = { 60, 64, 67, 72, 71, 67, 64, 62, 57, 60, 64, 69, 67, 64, 60, 55 };
        for (int i = 0; i < 16; ++i)
        {
            s.ev.push_back({ i * 0.5f, arp[i], 0.85f, 0.4f });
        }
        s.ev.push_back({ 8.5f, 48, 0.9f, 3.0f });
        s.ev.push_back({ 8.5f, 55, 0.9f, 3.0f });
        s.ev.push_back({ 8.5f, 64, 0.9f, 3.0f });
        songs.push_back(s);
    }

    // 7. Comb- sweep: a saw through COMB- with an LFO sweeping its tuning (flanger-like)
    {
        Song s{ "07_comb_minus_sweep.wav", "COMB- filter swept by an LFO (jet / flanger)", 120, 14, false, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 2.0f;
        p.babyMorph = 2.0f;
        p.babyFine = 15.0f;
        p.papaLevel = 0.3f;
        p.filterType = kFilterCombMinus;
        p.cutoff = 600.0f;
        p.keyTrack = 0.0f;
        p.resonance = 0.8f;
        p.amp = { 0.01f, 0.5f, 0.9f, 0.6f };
        p.env[0].slot[0] = {};
        p.lfo[0] = LfoParams{ kLfoTriangle, 0.25f, 0, true, { { kDestCutoff, 0.35f }, {} } };
        p.volume = 0.6f;
        s.ev = chordSeq({ { 45, 52, 57 }, { 43, 50, 55 }, { 41, 48, 53 } }, 4, 3.8f, 0.8f);
        songs.push_back(s);
    }

    // 8. Ape pan: LFOs move pan and the oscillator volumes (MAMA / BABY trade places)
    {
        Song s{ "08_osc_volume_and_pan_mod.wav", "Mod: LFO > pan, LFOs > MAMA/BABY volume", 120, 14, false, {}, {} };
        Params& p = s.p;
        p.mamaMorph = 3.0f;
        p.babyMorph = 0.6f;
        p.babyTune = 7.0f;
        p.mamaLevel = 0.5f;
        p.babyLevel = 0.5f;
        p.papaLevel = 0.3f;
        p.filterType = kFilterLp12;
        p.cutoff = 3000.0f;
        p.amp = { 0.01f, 0.3f, 0.9f, 0.4f };
        p.env[0].slot[0] = {};
        p.lfo[0] = LfoParams{ kLfoSine, 0.0f, 4, true, { { kDestPan, 0.9f }, {} } }; // synced, 2 beats
        p.lfo[1] = LfoParams{ kLfoSquare, 0.0f, 9, true, { { kDestMamaVol, 0.5f }, { kDestBabyVol, -0.5f } } };
        p.delayType = kDelayDigital;
        p.delayTime = 2;
        p.delayFeedback = 0.3f;
        p.delayMix = 0.2f;
        s.ev = chordSeq({ { 48, 60 }, { 46, 58 } }, 6, 5.5f, 0.8f);
        songs.push_back(s);
    }

    printf("MORPHANGUTAN previews -> %s\n", dir.c_str());
    for (const Song& s : songs)
    {
        render(s, dir);
    }
    return 0;
}
