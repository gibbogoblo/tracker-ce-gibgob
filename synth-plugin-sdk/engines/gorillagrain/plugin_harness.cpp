// Runs the whole GORILLAGRAIN plugin (glue + DSP, the code that goes into
// GORILLA.psyn) on the computer through its C API, the way the Tracker's
// loader drives it: metadata, voices, every parameter's default, a note,
// audio blocks, the instrument effects. Reports where the sound stops.
//   g++ -std=c++20 ... plugin_harness.cpp   (see the Makefile: grain-plugin-test)
#include "../gorillagrain.cpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
struct FakePatch
{
    std::vector<float> values;
    std::vector<bool> has;
    std::vector<int16_t> pcm;
    int channels = 1;
    uint32_t rate = 44100;
    int basePitch = 60;
    bool sample = true;
} g_patch;

struct Meta
{
    int paramCount = 0;
    std::vector<PsynParamDesc> params;
    std::vector<std::string> names;
    int slots = 0;
} g_meta;

bool g_inAudio = false; // inside a render call: no memory may be taken there
int g_audioAllocs = 0;
size_t g_biggest = 0;
void* hAlloc(size_t size, size_t align)
{
    if (g_inAudio)
        g_audioAllocs++;
    if (size > g_biggest)
        g_biggest = size;
    if (align < 8)
        align = 8;
    size = (size + align - 1) / align * align;
    void* p = aligned_alloc(align, size);
    // like the Tracker's plugin pool: memory is not cleared (here: NaN bytes)
    if (p && getenv("GARBAGE"))
        memset(p, 0xFF, size);
    return p;
}
void hFree(void* p) { free(p); }
bool hGet(const void*, int id, float* out)
{
    if (id < 0 || id >= (int)g_patch.values.size() || !g_patch.has[id])
        return false;
    *out = g_patch.values[id];
    return true;
}
void hSet(void*, int id, bool hv, float v, int)
{
    if (id >= 0 && id < (int)g_patch.values.size())
    {
        g_patch.values[id] = v;
        g_patch.has[id] = hv;
    }
}
bool hSample(const void*, int slot, PsynSampleData* out)
{
    if (slot != 0 || !g_patch.sample)
        return false;
    out->data = g_patch.pcm.data();
    out->size = (uint32_t)g_patch.pcm.size();
    out->channels = (uint8_t)g_patch.channels;
    out->ready = (uint8_t)(1u | ((g_patch.basePitch & 0x7F) << 1));
    out->wave_size = 0;
    out->sample_rate = g_patch.rate;
    return true;
}
void mInfo(void*, int, bool, bool, int) {}
void mCount(void*, int n)
{
    g_meta.paramCount = n;
    g_meta.params.resize(n);
    g_meta.names.resize(n);
}
void mParam(void*, int id, const PsynParamDesc* d)
{
    if (id >= 0 && id < g_meta.paramCount)
    {
        g_meta.params[id] = *d;
        g_meta.names[id] = d->display_name ? d->display_name : "";
    }
}
void mSlot(void*, const PsynSampleSlotDesc*) { g_meta.slots++; }
void mFlat(void*, const char*, const char*, const PsynLayoutElement*, int) {}
int m33(void*, const char*, const char*, const PsynLayoutElement*, int) { return 0; }
void m33sub(void*, int, const char*, const PsynLayoutElement*, int) {}
void mMacro(void*, int, const PsynMacroDesc*) {}

std::vector<int16_t> loadWav(const char* path, int& ch, uint32_t& rate)
{
    std::vector<int16_t> out;
    FILE* f = fopen(path, "rb");
    if (!f)
        return out;
    char h[44];
    if (fread(h, 1, 44, f) == 44)
    {
        ch = *(int16_t*)(h + 22);
        rate = *(uint32_t*)(h + 24);
        out.resize(*(uint32_t*)(h + 40) / 2);
        if (fread(out.data(), 2, out.size(), f) != out.size())
            out.clear();
    }
    fclose(f);
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    int fails = 0;
    static PsynHostApi host{};
    host.api_version = 1;
    host.sample_rate = 44100;
    host.block_size = polysynth::kAudioBufferSize;
    host.slot_alloc = hAlloc;
    host.slot_free = hFree;
    host.patch_get_param_value = hGet;
    host.patch_get_param_final_value = hGet;
    host.patch_set_param_value = hSet;
    host.patch_get_sample = hSample;
    host.meta_set_info = mInfo;
    host.meta_set_param_count = mCount;
    host.meta_set_param = mParam;
    host.meta_add_sample_slot = mSlot;
    host.meta_add_flat_section = mFlat;
    host.meta_add_33_section = m33;
    host.meta_add_33_subsection = m33sub;
    host.meta_set_default_macro = mMacro;

    // (the plugin's memory routines serve this program too: connect first)
    const PsynPluginApi* api = psyn_plugin_entry(&host);
    if (!api)
    {
        printf("FAIL: no plugin api\n");
        return 1;
    }
    g_patch.pcm = loadWav(argc > 1 ? argv[1] : "GORILLA.wav", g_patch.channels, g_patch.rate);
    printf("sample: %zu values, %d ch, %u Hz\n", g_patch.pcm.size(), g_patch.channels, g_patch.rate);
    int dummyPatch = 0;
    api->create_metadata(&dummyPatch, &dummyPatch);
    printf("params: %d, sample slots: %d\n", g_meta.paramCount, g_meta.slots);
    g_patch.values.assign(g_meta.paramCount, 0.0f);
    g_patch.has.assign(g_meta.paramCount, false);
    for (int i = 0; i < g_meta.paramCount; i++)
        if (g_meta.params[i].has_default)
        {
            g_patch.values[i] = g_meta.params[i].default_value;
            g_patch.has[i] = true;
        }
        else
            printf("  param %d (%s) has no default\n", i, g_meta.names[i].c_str());

    // MUTE=<name>: that parameter at 0 (to check the diagnosis beeps)
    if (const char* mute = getenv("MUTE"))
        for (int i = 0; i < g_meta.paramCount; i++)
            if (g_meta.names[i] == mute)
                g_patch.values[i] = 0.0f;
    g_biggest = 0; // from here: what the plugin takes
    void* voice = api->create_voice(0);
    api->voice_set_patch(voice, &dummyPatch);
    for (int i = 0; i < g_meta.paramCount; i++)
        api->voice_on_param_change(voice, i, g_patch.has[i], g_patch.values[i]);

    printf("biggest memory taken before the first note: %zu bytes\n", g_biggest);
    // the reverb is on from the start, the delay is not: only the reverb's block
    if (g_biggest != (size_t)morph::Fx::kReverbBytes)
    {
        printf("FAIL: the reverb memory (%d) was not taken alone when the patch loaded (biggest %zu)\n",
               morph::Fx::kReverbBytes, g_biggest);
        fails++;
    }
    // the delay switched on (a parameter change, outside the audio): its block now
    for (int i = 0; i < g_meta.paramCount; i++)
        if (g_meta.names[i] == "Delay")
        {
            g_biggest = 0;
            g_patch.values[i] = 1.0f; // Digital
            api->voice_on_param_change(voice, i, true, 1.0f);
            if (g_biggest != (size_t)morph::Fx::kDelayBytes)
            {
                printf("FAIL: switching the delay on did not take its memory (biggest %zu)\n", g_biggest);
                fails++;
            }
        }
    PsynVoiceContext ctx{ 120.0f, 2.0f };
    PsynMidiEvent on{};
    on.type = PSYN_MIDI_NOTE_ON;
    on.data1 = 60;
    on.data2 = 100;
    api->voice_on_midi_event(voice, &ctx, &on);

    const int n = (int)polysynth::kAudioBufferSize;
    std::vector<float> L(n), R(n);
    double sum = 0, sumFx = 0;
    bool nan = false, nanFx = false;
    int blocks = (int)(2.0f * 44100 / n);
    for (int b = 0; b < blocks; b++)
    {
        std::fill(L.begin(), L.end(), 0.0f);
        std::fill(R.begin(), R.end(), 0.0f);
        g_inAudio = true;
        api->voice_render_audio(voice, &ctx, L.data(), R.data(), nullptr, 0);
        g_inAudio = false;
        for (int i = 0; i < n; i++)
        {
            nan = nan || !std::isfinite(L[i]) || !std::isfinite(R[i]);
            sum += L[i] * L[i] + R[i] * R[i];
        }
        g_inAudio = true;
        if (api->voice_render_post_process)
            api->voice_render_post_process(voice, &ctx, L.data(), R.data());
        g_inAudio = false;
        for (int i = 0; i < n; i++)
        {
            nanFx = nanFx || !std::isfinite(L[i]) || !std::isfinite(R[i]);
            sumFx += L[i] * L[i] + R[i] * R[i];
        }
    }
    const double rms = std::sqrt(sum / (2.0 * n * blocks)), rmsFx = std::sqrt(sumFx / (2.0 * n * blocks));
    printf("voice: rms %.4f%s   after effects: rms %.4f%s   active %d\n", rms, nan ? " NaN!" : "", rmsFx,
           nanFx ? " NaN!" : "", api->voice_is_active ? (int)api->voice_is_active(voice) : -1);
    if (rms < 0.01 || nan)
    {
        printf("FAIL: the voice is silent\n");
        fails++;
    }
    if (rmsFx < 0.01 || nanFx)
    {
        printf("FAIL: silent after the effects\n");
        fails++;
    }
    if (g_audioAllocs != 0)
    {
        printf("FAIL: %d memory requests inside the audio\n", g_audioAllocs);
        fails++;
    }
    // a second voice never takes effects memory
    {
        const size_t before = g_biggest;
        g_biggest = 0;
        void* v1 = api->create_voice(1);
        api->voice_set_patch(v1, &dummyPatch);
        for (int i = 0; i < g_meta.paramCount; i++)
            api->voice_on_param_change(v1, i, g_patch.has[i], g_patch.values[i]);
        if (g_biggest >= (size_t)morph::Fx::kMemoryBytes)
        {
            printf("FAIL: voice 1 took effects memory too\n");
            fails++;
        }
        api->destroy_voice(v1);
        g_biggest = before;
    }
    float graph[64];
    api->voice_render_graph(voice, graph, 64);
    printf("graph: marker %.0f playhead %.3f grains %.0f\n", graph[0], graph[1], graph[2]);
    api->destroy_voice(voice);
    printf(fails ? "PLUGIN HARNESS FAILED\n" : "PLUGIN HARNESS PASSED\n");
    return fails ? 1 : 0;
}
