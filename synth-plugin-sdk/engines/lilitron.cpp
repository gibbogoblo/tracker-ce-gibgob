// LILITRON - GibGob OS gag singer (.psyn).
// A tiny robot girl sings one of 8 syllables on any note; the cat joins in a
// fifth higher with "nyaa" when it is switched on. The voice comes from
// /SynthSamples/LILI/LILITRON.wav (made by lilitron/make_voice.py); where each
// syllable starts and loops is in lilitron/lilitron_table.h.
// The syllable of a note arrives as MIDI CC 3 just before it (the GibGob
// sequencer sends it from the note); a note plays the syllable it finds.
// MIT License, part of GibGob OS.

#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "lilitron/lilitron_table.h"

#include <algorithm>
#include <cmath>

namespace polysynth
{

// Parameter ids are stored in saved patches: only ever add at the end.
enum class LiliParamId : int
{
    kSyllable = 0,
    kCat,
};

namespace
{
constexpr const char* kSyllableNames[8] = { "tah", "dee", "umm", "lee", "aah", "bah", "so", "lah" };
constexpr const char* kCatNames[2] = { "Off", "On" };
constexpr int kCatSegment = 8;
constexpr uint8_t kSyllableCC = 3;
constexpr float kOutRate = 44100.0f;
constexpr float kAttackStep = 1.0f / (0.004f * kOutRate);  // 4 ms in
constexpr float kReleaseStep = 1.0f / (0.025f * kOutRate); // 25 ms out: a very short release
constexpr float kVibratoDelay = 0.18f;                     // seconds before the vibrato starts
constexpr float kVibratoRamp = 0.25f;                      // and grows to full in
constexpr float kVibratoDepth = 0.25f;                     // semitones (25 cents)
constexpr float kVibratoRate = 5.6f;                       // Hz

inline int toIndex(float v, int count)
{
    return std::clamp<int>(static_cast<int>(std::lround(v)), 0, count - 1);
}

template<size_t N>
auto enumFormatter(const char* const (&names)[N])
{
    const char* const* table = names;
    return [table](const Patch& /*patch*/, Patch::paramvalue_t value)
    { return std::string(table[toIndex(value, static_cast<int>(N))]); };
}

// the recording sung nearest to the note (the cat's are a fifth up)
const lili::Segment& segmentFor(int syllable, int note)
{
    int best = 0;
    int bestDist = 1000;
    for (int r = 0; r < lili::kRoots; ++r)
    {
        const int d = std::abs(note - static_cast<int>(lili::kSegments[r][syllable].root));
        if (d < bestDist)
        {
            bestDist = d;
            best = r;
        }
    }
    return lili::kSegments[best][syllable];
}

// one reading position in the voice recording
struct Head
{
    bool on = false;
    double pos = 0.0;
    float step = 1.0f; // recording frames per output sample
    uint32_t loopStart = 0, loopEnd = 0;
    float gain = 1.0f;

    void start(const lili::Segment& s, int note, float rateRatio)
    {
        on = true;
        pos = s.start;
        loopStart = s.loopStart;
        loopEnd = s.loopEnd;
        step = std::pow(2.0f, (note - static_cast<int>(s.root)) / 12.0f) * rateRatio;
    }

    // the next sample (the held vowel loops for as long as it is needed)
    inline float next(const int16_t* data, uint32_t size, float vibrato)
    {
        if (!on)
            return 0.0f;
        const uint32_t i = static_cast<uint32_t>(pos);
        if (i + 1 >= size)
        {
            on = false;
            return 0.0f;
        }
        const float f = static_cast<float>(pos - i);
        const float a = data[i] * (1.0f / 32768.0f);
        const float b = data[i + 1] * (1.0f / 32768.0f);
        pos += step * vibrato;
        if (pos >= loopEnd)
            pos -= static_cast<double>(loopEnd - loopStart);
        return (a + (b - a) * f) * gain;
    }
};
} // namespace

class LilitronVoice : public sdk::CustomSynthVoice<LilitronVoice, LiliParamId>
{
  public:
    struct Params
    {
        int syllable = 0;
        bool cat = false;
    } params;

    static void createMetadata(const Patch& patch, Patch::Metadata& metadata);

    bool isActive() final { return gate || level > 0.0f; }

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) override
    {
        // the syllable for the next note (the host maps CC 3 to the Syllable
        // parameter too; this also catches it when the parameter is not mapped)
        if (event.type == MidiEvent::Type::kControlChange && event.data.controlChange.id == kSyllableCC)
            params.syllable = toIndex(event.data.controlChange.value * 7.0f / 127.0f, 8);
        sdk::CustomSynthVoice<LilitronVoice, LiliParamId>::onMidiEvent(ctx, event);
    }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float velocity, bool /*isLegatoStep*/) final
    {
        const Sample* s = getPatch() != nullptr ? getPatch()->getSample(0) : nullptr;
        ready = s != nullptr && s->ready && s->format.channels == 1 && s->data.size() >= lili::kTotalFrames;
        data = ready ? s->data.data() : nullptr;
        size = ready ? static_cast<uint32_t>(s->data.size()) : 0u;
        const float rateRatio = ready ? static_cast<float>(s->format.sampleRate) / kOutRate : 1.0f;

        const int syl = std::clamp(params.syllable, 0, 7);
        singer.start(segmentFor(syl, note), note, rateRatio);
        singer.gain = 0.9f;
        if (params.cat)
        {
            cat.start(segmentFor(kCatSegment, note + 7), note + 7, rateRatio);
            cat.gain = 0.6f;
        }
        else
        {
            cat.on = false;
        }
        vel = 0.45f + 0.55f * std::clamp(velocity, 0.0f, 1.0f);
        gate = true;
        level = 0.0f; // every note sings from the start
        age = 0;
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final { gate = false; }

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/, AudioBufferStereoRef outputBuffer) final
    {
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        if (!ready)
        {
            // no voice file on the card: a soft double beep while a key is held
            for (int i = 0; i < n; ++i)
            {
                const uint32_t t = blip % 22050u;
                const bool on = gate && (t < 2205u || (t > 4410u && t < 6615u));
                beepPhase += 1318.5f / kOutRate;
                if (beepPhase >= 1.0f)
                    beepPhase -= 1.0f;
                const float b = on ? sdk::fastSine(beepPhase) * 0.15f : 0.0f;
                left[i] = right[i] = b;
                ++blip;
            }
            if (!gate)
                level = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            // envelope: quick in, held, a very short release
            if (gate)
                level = std::min(1.0f, level + kAttackStep);
            else
                level = std::max(0.0f, level - kReleaseStep);

            // vibrato: starts after a moment and grows in
            const float t = age * (1.0f / kOutRate);
            float vib = 1.0f;
            if (t > kVibratoDelay)
            {
                const float depth = std::min(1.0f, (t - kVibratoDelay) / kVibratoRamp) * kVibratoDepth;
                vibPhase += kVibratoRate / kOutRate;
                if (vibPhase >= 1.0f)
                    vibPhase -= 1.0f;
                vib = sdk::fastPow2(depth * sdk::fastSine(vibPhase) / 12.0f);
            }
            ++age;

            const float x = (singer.next(data, size, vib) + cat.next(data, size, vib)) * level * vel;
            left[i] = right[i] = x;
        }
        if (level <= 0.0f && !gate)
        {
            singer.on = false;
            cat.on = false;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
        renderVoiceAudio(ctx, outputBuffer);
    }

  private:
    Head singer, cat;
    const int16_t* data = nullptr;
    uint32_t size = 0;
    bool ready = false;
    bool gate = false;
    float level = 0.0f;
    float vel = 1.0f;
    uint32_t age = 0;
    float vibPhase = 0.0f;
    uint32_t blip = 0;
    float beepPhase = 0.0f;
};

// ---------------------------------------------------------------- parameters

#define LP(id) (*LiliParamId::id)

void LilitronVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;
    using Param = Patch::Metadata::Param;
    using EditorElement = Patch::Metadata::EditorLayout::EditorElement;
    using EditorSection = Patch::Metadata::EditorLayout::EditorSection;

    metadata.params.resize(enumutil::EnumMax<LiliParamId>::value + 1);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
        paramHandlers.resize(enumutil::EnumMax<LiliParamId>::value + 1);
    ParamHelper<LilitronVoice> helper(metadata, paramHandlers);

    helper.addParam(LP(kSyllable),
                    Param{ .displayName = "Syllable", .defaultValue = 0, .minValue = 0, .maxValue = 7, .isEnum = true,
                           .midiCC = kSyllableCC, .valueFormatter = enumFormatter(kSyllableNames) },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.syllable = toIndex(*x, 8); });
    helper.addParam(LP(kCat),
                    Param{ .displayName = "Cat", .defaultValue = 0, .minValue = 0, .maxValue = 1, .isEnum = true,
                           .valueFormatter = enumFormatter(kCatNames) },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.cat = toIndex(*x, 2) == 1; });

    metadata.sampleSlots = {
        Patch::Metadata::SampleSlot{ .displayName = "Voice",
                                     .type = Patch::Metadata::SampleSlot::kSample,
                                     .section = 0,
                                     .subsection = 0,
                                     .defaultSamplePath = "LILI/LILITRON.wav" },
    };

    using E = EditorElement;
    auto el = [](LiliParamId id) { return E{ .type = E::Type::kParam, .id = *id }; };
    metadata.layout33 = Patch::Metadata::EditorLayout{
        .sections = {
          { .name = "Main", .shortName = "GibGob", .elements = { el(LiliParamId::kCat) } },
          { .name = "Voice", .shortName = "Osc",
            .subsections = { { .name = "LILITRON", .elements = { el(LiliParamId::kSyllable), el(LiliParamId::kCat) } } } },
        },
    };
    Patch::Metadata::EditorLayout flat;
    flat.sections.push_back(EditorSection{ .name = "Voice", .shortName = "Voice",
                                           .elements = { el(LiliParamId::kSyllable), el(LiliParamId::kCat) } });
    metadata.layoutFlat = flat;

    metadata.instrumentTypeId = 61;
    metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
}

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::LilitronVoice)
