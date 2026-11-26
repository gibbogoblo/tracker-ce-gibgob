// CHILLAMANG - GibGob OS synth plugin (.psyn): a mellow tine electric piano.
// Thin glue between the Polyend synth SDK and the DSP core in
// chillamang/chill_dsp.h (also used for the WAV previews and the tests).
// The GibGob section is named "Angelic": the Y2K cyber angelic look.
// MIT License, part of GibGob OS.

#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "chillamang/chill_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace polysynth
{

// Parameter ids are stored in saved patches: only ever add at the end.
enum class ChillParamId : int
{
    // Tines
    kTineLevel = 0,
    kTineDecay,
    kBark,
    kBody,
    kPickup,
    kHammer,
    kBeat,
    kTune,
    kFine,
    // Tone
    kBrightness,
    kVelBright,
    kDrive,
    kAge,
    kKeyTrack,
    // Keys
    kDecay,
    kRelease,
    kFelt,
    kVelToAmp,
    kVolume,
    kPan,
    kSpread,
    // Tremolo / vibrato
    kTremRate,
    kTremSync,
    kTremDepth,
    kTremMode,
    kVibRate,
    kVibDepth,
    // Chorus / phaser
    kChorusMix,
    kChorusRate,
    kChorusDepth,
    kPhaserMix,
    kPhaserRate,
    kPhaserFeedback,
    // Delay / reverb
    kDelayType,
    kDelayTime,
    kDelayFeedback,
    kDelayMix,
    kReverbType,
    kReverbDecay,
    kReverbTone,
    kReverbMix,
    // added in the second version (warmer, rounder)
    kTineTone,
    kWarmth,
};

namespace
{

constexpr const char* kTremModeNames[chill::kTremModeCount] = { "Stereo", "Volume" };
constexpr const char* kSyncNames[morph::kLfoSyncCount] = {
    "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/4D",
};
constexpr const char* kDelayTypeNames[morph::kDelayTypeCount] = { "Off", "Digital", "Tape", "Ping-pong" };
constexpr const char* kDelayTimeNames[morph::kDelayTimeCount] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2",
};
constexpr const char* kReverbTypeNames[morph::kReverbTypeCount] = { "Off", "Plate", "Dark", "Shimmer" };

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

auto panFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int pct = static_cast<int>(std::lround(value * 100.0f));
        if (pct == 0)
            return std::string("Centre");
        return pct < 0 ? fmt::format("L {:d}", -pct) : fmt::format("R {:d}", pct);
    };
}

// "hollow" .. "full"
auto pickupFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int pct = static_cast<int>(std::lround(value * 100.0f));
        if (pct < 15)
            return fmt::format("Hollow {:d}", pct);
        if (pct > 85)
            return fmt::format("Full {:d}", pct);
        return fmt::format("{:d} %", pct);
    };
}

// "Glassy" .. "Round"
auto tineToneFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int pct = static_cast<int>(std::lround(value * 100.0f));
        if (pct <= 10)
            return fmt::format("Glassy {:d}", pct);
        if (pct >= 90)
            return fmt::format("Round {:d}", pct);
        return fmt::format("{:d} %", pct);
    };
}

} // namespace

class ChillamangVoice : public sdk::CustomSynthVoice<ChillamangVoice, ChillParamId>
{
  public:
    // the host creates the voices with their number; voice 0 runs the
    // instrument's effects (see renderPostProcessAudio)
    explicit ChillamangVoice(int index) : voiceIndex(index)
    {
        static uint32_t s_seed = 1;
        dsp.init(s_seed++);
    }

    ~ChillamangVoice()
    {
        delete[] modMemory;
        delete[] reverbMemory;
        delete[] delayMemory;
    }

    // the effects' memory (chorus 16 KB, reverb 150 KB, delay 256 KB): taken here,
    // outside the audio, when the patch loads or an effect is switched on
    void effectsChanged()
    {
        if (voiceIndex != 0)
            return;
        if (params.wantsModFx() && !mod.ready() && !modAllocFailed)
            takeModMemory();
        if (params.wantsSpaceFx() && fxMissing())
            takeFxMemory();
    }

    bool isActive() final { return gate || dsp.active(); }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float velocity, bool /*isLegatoStep*/) final
    {
        dsp.noteOn(params, static_cast<float>(note), velocity);
        gate = true;
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        dsp.noteOff(params);
        gate = false;
    }

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/, AudioBufferStereoRef outputBuffer) final
    {
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        std::fill(left.begin(), left.begin() + n, 0.0f);
        std::fill(right.begin(), right.begin() + n, 0.0f);
        dsp.render(params, bendForBlock(n), left.data(), right.data(), n);
        for (int i = 0; i < n && i < static_cast<int>(lastGraphSamples.size()); ++i)
            lastGraphSamples[i] = 0.5f * (left[i] + right[i]);
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
        renderVoiceAudio(ctx, outputBuffer);
    }

    // tremolo / chorus / phaser, then delay / reverb, once on the instrument's
    // mix (the host calls this on the first voice)
    void renderPostProcessAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer) final
    {
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        // the last resort when another voice runs the effects (normally voice 0 took the memory already)
        if (params.wantsModFx() && !mod.ready() && !modAllocFailed)
            takeModMemory();
        mod.process(params, ctx.tempo, left.data(), right.data(), n); // the tremolo needs no memory

        params.fillFx(fxParams);
        if (!params.wantsSpaceFx())
        {
            if (fx.ready())
                fx.process(fxParams, ctx.tempo, fxMod, nullptr, nullptr, 0); // clears the tails
            return;
        }
        if (fxMissing())
            takeFxMemory();
        if (fxAllocFailed)
            return; // no memory for the effects: dry
        fx.process(fxParams, ctx.tempo, fxMod, left.data(), right.data(), n);
    }

    bool hasPostProcess() const final { return true; }

    void renderGraph(std::span<float> points) override
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            const size_t src = (i * lastGraphSamples.size()) / std::max<size_t>(1, points.size());
            points[i] = std::clamp(lastGraphSamples[src], -1.0f, 1.0f);
        }
    }

    static void createMetadata(const Patch& patch, Patch::Metadata& metadata);

    chill::Params params;

  private:
    void takeModMemory()
    {
        modMemory = new (std::nothrow) uint8_t[chill::ModFx::kMemoryBytes];
        if (modMemory == nullptr)
            modAllocFailed = true;
        else
            mod.attach(modMemory);
    }
    // each effect takes its own memory, only once it is switched on (the
    // synths share one pool: a reverb alone needs less than half)
    void takeFxMemory()
    {
        if (params.reverbType != morph::kReverbOff && !fx.reverbReady() && !reverbAllocFailed)
        {
            reverbMemory = new (std::nothrow) uint8_t[morph::Fx::kReverbBytes];
            if (reverbMemory == nullptr)
                reverbAllocFailed = true;
            else
                fx.attachReverb(reverbMemory); // all clear
        }
        if (params.delayType != morph::kDelayOff && !fx.delayReady() && !delayAllocFailed)
        {
            delayMemory = new (std::nothrow) uint8_t[morph::Fx::kDelayBytes];
            if (delayMemory == nullptr)
                delayAllocFailed = true;
            else
                fx.attachDelay(delayMemory);
        }
        fxAllocFailed = !fx.ready(); // neither effect has memory: dry
    }
    // a switched-on effect still without memory (and not failed yet)
    bool fxMissing() const
    {
        return (params.reverbType != morph::kReverbOff && !fx.reverbReady() && !reverbAllocFailed) ||
               (params.delayType != morph::kDelayOff && !fx.delayReady() && !delayAllocFailed);
    }

    int voiceIndex = 0;
    chill::Voice dsp;
    chill::ModFx mod;
    morph::Fx fx;
    morph::Params fxParams;
    float fxMod[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint8_t* modMemory = nullptr;
    uint8_t* reverbMemory = nullptr;
    uint8_t* delayMemory = nullptr;
    bool reverbAllocFailed = false;
    bool delayAllocFailed = false;
    bool modAllocFailed = false;
    bool fxAllocFailed = false;
    bool gate = false;
    std::array<float, kAudioBufferSize> lastGraphSamples{};
};

// ---------------------------------------------------------------- parameters

#define CP(id) (*ChillParamId::id)
#define ADD_FLOAT(id, field, ...)                                                                  \
    helper.addParam(CP(id), Param{ __VA_ARGS__ },                                                  \
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.field = *x; })
// a mix knob that switches an effect on: the effect's memory is taken now (outside the audio)
#define ADD_FX_FLOAT(id, field, ...)                                                               \
    helper.addParam(CP(id), Param{ __VA_ARGS__ },                                                  \
                    [](auto& v, Patch::optional_paramvalue_t x)                                    \
                    {                                                                              \
                        v.params.field = *x;                                                       \
                        v.effectsChanged();                                                        \
                    })
#define ADD_ENUM(id, field, names, def, name, after)                                               \
    helper.addParam(CP(id),                                                                        \
                    Param{ .displayName = name,                                                    \
                           .defaultValue = static_cast<Patch::paramvalue_short_t>(def),            \
                           .minValue = 0,                                                          \
                           .maxValue = static_cast<Patch::paramvalue_short_t>(                     \
                             sizeof(names) / sizeof(names[0]) - 1),                                \
                           .isEnum = true,                                                         \
                           .valueFormatter = enumFormatter(names) },                               \
                    [](auto& v, Patch::optional_paramvalue_t x)                                    \
                    {                                                                              \
                        v.params.field = static_cast<uint8_t>(                                     \
                          toIndex(*x, static_cast<int>(sizeof(names) / sizeof(names[0]))));        \
                        after;                                                                     \
                    })

void ChillamangVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;
    using Curve = Patch::Metadata::Param::Curve;

    metadata.params.resize(enumutil::EnumMax<ChillParamId>::value + 1);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
        paramHandlers.resize(enumutil::EnumMax<ChillParamId>::value + 1);
    ParamHelper<ChillamangVoice> helper(metadata, paramHandlers);
    const chill::Params d; // the defaults

    // ---- tines
    ADD_FLOAT(kTineLevel, tineLevel, .displayName = "Tine", .defaultValue = d.tineLevel, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kTineDecay, tineDecay, .displayName = "Tine decay", .defaultValue = d.tineDecay, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 1, .valueFormatter = createPercentFormatter());
    helper.addParam(CP(kTineTone),
                    Param{ .displayName = "Tine tone", .defaultValue = d.tineTone, .minValue = 0, .maxValue = 1,
                           .valueFormatter = tineToneFormatter() },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.tineTone = *x; });
    ADD_FLOAT(kBark, bark, .displayName = "Bark", .defaultValue = d.bark, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 2, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kBody, body, .displayName = "Body", .defaultValue = d.body, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 3, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kPickup, pickup, .displayName = "Pickup", .defaultValue = d.pickup, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 4, .valueFormatter = pickupFormatter());
    ADD_FLOAT(kHammer, hammer, .displayName = "Hammer", .defaultValue = d.hammer, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 5, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kBeat, beat, .displayName = "Shimmer", .defaultValue = d.beat, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 6, .valueFormatter = createPercentFormatter());
    helper.addParam(CP(kTune),
                    Param{ .displayName = "Tune", .defaultValue = 0.0f, .minValue = -24, .maxValue = 24,
                           .valueFormatter = createSemitoneFormatter() },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.tune = std::round(*x); });
    ADD_FLOAT(kFine, fine, .displayName = "Fine", .defaultValue = 0.0f, .minValue = -100, .maxValue = 100,
              .valueFormatter = createCentsFormatter());

    // ---- tone
    ADD_FLOAT(kBrightness, brightness, .displayName = "Brightness", .defaultValue = d.brightness,
              .minValue = 400.0f, .maxValue = 16000.0f, .editCurve = Curve::kExponentialSteep,
              .midiCC = *MidiCCs::kFilterCutoff, .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kVelBright, velBright, .displayName = "Vel bright", .defaultValue = d.velBright, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDrive, drive, .displayName = "Drive", .defaultValue = d.drive, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 7, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kAge, age, .displayName = "Age", .defaultValue = d.age, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 8, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kWarmth, warmth, .displayName = "Warmth", .defaultValue = d.warmth, .minValue = 0, .maxValue = 1,
              .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kKeyTrack, keyTrack, .displayName = "Key track", .defaultValue = d.keyTrack, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());

    // ---- keys
    ADD_FLOAT(kDecay, decay, .displayName = "Decay", .defaultValue = d.decay, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kAmpEnvDecay, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kRelease, release, .displayName = "Release", .defaultValue = d.release, .minValue = 0.01f,
              .maxValue = 3.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvRelease,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kFelt, felt, .displayName = "Felt", .defaultValue = d.felt, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 9, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kVelToAmp, velToAmp, .displayName = "Velocity", .defaultValue = d.velToAmp, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kVelocityVolume, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kVolume, volume, .displayName = "Volume", .defaultValue = d.volume, .minValue = 0, .maxValue = 1.5f,
              .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
              .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kPan, pan, .displayName = "Pan", .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,
              .valueFormatter = panFormatter());
    ADD_FLOAT(kSpread, spread, .displayName = "Key spread", .defaultValue = d.spread, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());

    // ---- tremolo / vibrato
    ADD_FLOAT(kTremRate, tremRate, .displayName = "Trem rate", .defaultValue = d.tremRate, .minValue = 0.1f,
              .maxValue = 12.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kLfo1Frequency,
              .valueFormatter = createFrequencyFormatter());
    ADD_ENUM(kTremSync, tremSync, kSyncNames, 0, "Trem sync", (void)0);
    ADD_FLOAT(kTremDepth, tremDepth, .displayName = "Tremolo", .defaultValue = d.tremDepth, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 10, .valueFormatter = createPercentFormatter());
    ADD_ENUM(kTremMode, tremMode, kTremModeNames, chill::kTremPan, "Trem mode", (void)0);
    ADD_FLOAT(kVibRate, vibRate, .displayName = "Vib rate", .defaultValue = d.vibRate, .minValue = 0.1f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kLfo2Frequency,
              .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kVibDepth, vibDepth, .displayName = "Vibrato", .defaultValue = d.vibDepth, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());

    // ---- chorus / phaser
    ADD_FX_FLOAT(kChorusMix, chorusMix, .displayName = "Chorus", .defaultValue = d.chorusMix, .minValue = 0,
                 .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 11, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kChorusRate, chorusRate, .displayName = "Chorus rate", .defaultValue = d.chorusRate,
              .minValue = 0.05f, .maxValue = 5.0f, .editCurve = Curve::kExponential,
              .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kChorusDepth, chorusDepth, .displayName = "Chorus depth", .defaultValue = d.chorusDepth,
              .minValue = 0, .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FX_FLOAT(kPhaserMix, phaserMix, .displayName = "Phaser", .defaultValue = d.phaserMix, .minValue = 0,
                 .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kPhaserRate, phaserRate, .displayName = "Phaser rate", .defaultValue = d.phaserRate,
              .minValue = 0.05f, .maxValue = 5.0f, .editCurve = Curve::kExponential,
              .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kPhaserFeedback, phaserFeedback, .displayName = "Phaser fdbk", .defaultValue = d.phaserFeedback,
              .minValue = 0, .maxValue = 0.9f, .valueFormatter = createPercentFormatter());

    // ---- delay / reverb (a plate on by default)
    ADD_ENUM(kDelayType, delayType, kDelayTypeNames, d.delayType, "Delay", v.effectsChanged());
    ADD_ENUM(kDelayTime, delayTime, kDelayTimeNames, d.delayTime, "Delay time", (void)0);
    ADD_FLOAT(kDelayFeedback, delayFeedback, .displayName = "Delay fdbk", .defaultValue = d.delayFeedback,
              .minValue = 0, .maxValue = 0.95f, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDelayMix, delayMix, .displayName = "Delay mix", .defaultValue = d.delayMix, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kDelaySend, .valueFormatter = createPercentFormatter());
    ADD_ENUM(kReverbType, reverbType, kReverbTypeNames, d.reverbType, "Reverb", v.effectsChanged());
    ADD_FLOAT(kReverbDecay, reverbDecay, .displayName = "Rev decay", .defaultValue = d.reverbDecay, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbTone, reverbTone, .displayName = "Rev tone", .defaultValue = d.reverbTone, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbMix, reverbMix, .displayName = "Rev mix", .defaultValue = d.reverbMix, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kReverbSend, .valueFormatter = createPercentFormatter());

    // ---- layouts: layout33 drives the GibGob OS editor, the flat one the stock editor
    using E = EditorElement;
    auto el = [](ChillParamId id) { return E{ .type = E::Type::kParam, .id = *id }; };

    metadata.layout33 = Patch::Metadata::EditorLayout{
        .sections = {
          // the GibGob editor's Main page; its name picks the Y2K cyber angelic look
          { .name = "Angelic", .shortName = "GibGob",
            .elements = { el(ChillParamId::kBark), el(ChillParamId::kBrightness), el(ChillParamId::kTineLevel),
                          el(ChillParamId::kTineTone), el(ChillParamId::kDecay), el(ChillParamId::kRelease),
                          el(ChillParamId::kTremDepth), el(ChillParamId::kTremRate), el(ChillParamId::kChorusMix),
                          el(ChillParamId::kWarmth), el(ChillParamId::kDelayMix), el(ChillParamId::kReverbMix) } },
          { .name = "Tines", .shortName = "Osc",
            .subsections = {
              { .name = "Tine", .elements = { el(ChillParamId::kTineLevel), el(ChillParamId::kTineTone),
                                              el(ChillParamId::kTineDecay), el(ChillParamId::kHammer),
                                              el(ChillParamId::kBeat) } },
              { .name = "Body", .elements = { el(ChillParamId::kBody), el(ChillParamId::kBark),
                                              el(ChillParamId::kPickup), el(ChillParamId::kTune),
                                              el(ChillParamId::kFine) } },
            } },
          { .name = "Tone", .shortName = "Filter",
            .subsections = {
              { .elements = { el(ChillParamId::kWarmth), el(ChillParamId::kBrightness), el(ChillParamId::kVelBright),
                              el(ChillParamId::kKeyTrack), el(ChillParamId::kDrive), el(ChillParamId::kAge) } },
            } },
          { .name = "Keys", .shortName = "Amp",
            .subsections = {
              { .name = "Feel", .elements = { el(ChillParamId::kDecay), el(ChillParamId::kRelease),
                                              el(ChillParamId::kFelt), el(ChillParamId::kVelToAmp) } },
              { .name = "Out", .elements = { el(ChillParamId::kVolume), el(ChillParamId::kPan),
                                             el(ChillParamId::kSpread) } },
            } },
          { .name = "Trem", .shortName = "LFO",
            .subsections = {
              { .name = "Tremolo", .elements = { el(ChillParamId::kTremDepth), el(ChillParamId::kTremRate),
                                                 el(ChillParamId::kTremSync), el(ChillParamId::kTremMode) } },
              { .name = "Vibrato", .elements = { el(ChillParamId::kVibDepth), el(ChillParamId::kVibRate) } },
            } },
          { .name = "Chorus", .shortName = "Env",
            .subsections = {
              { .name = "Chorus", .elements = { el(ChillParamId::kChorusMix), el(ChillParamId::kChorusRate),
                                                el(ChillParamId::kChorusDepth) } },
              { .name = "Phaser", .elements = { el(ChillParamId::kPhaserMix), el(ChillParamId::kPhaserRate),
                                                el(ChillParamId::kPhaserFeedback) } },
            } },
          { .name = "Effects", .shortName = "FX",
            .subsections = {
              { .name = "Delay", .elements = { el(ChillParamId::kDelayType), el(ChillParamId::kDelayTime),
                                               el(ChillParamId::kDelayFeedback), el(ChillParamId::kDelayMix) } },
              { .name = "Reverb", .elements = { el(ChillParamId::kReverbType), el(ChillParamId::kReverbDecay),
                                                el(ChillParamId::kReverbTone), el(ChillParamId::kReverbMix) } },
            } },
        },
    };

    // flat layout: one section per page, same order
    Patch::Metadata::EditorLayout flat;
    for (const auto& section : metadata.layout33.sections)
    {
        if (section.shortName == "GibGob")
            continue; // the GibGob editor's Main page repeats parameters from the others
        EditorSection s{ .name = section.name, .shortName = section.shortName };
        for (const auto& sub : section.subsections)
            for (const auto& e : sub.elements)
                s.elements.push_back(e);
        flat.sections.push_back(s);
    }
    metadata.layoutFlat = flat;
    metadata.layout23 = flat;

    metadata.initDefaultMacros = [](auto& macros)
    {
        macros[0] = Patch::Metadata::Macro{ .displayName = "Bark", .modifiers = { { CP(kBark), 1.0f } }, .bipolar = true };
        macros[1] = Patch::Metadata::Macro{ .displayName = "Brightness",
                                            .modifiers = { { CP(kBrightness), 1.0f } },
                                            .bipolar = true };
        macros[2] = Patch::Metadata::Macro{ .displayName = "Tremolo",
                                            .modifiers = { { CP(kTremDepth), 1.0f } },
                                            .bipolar = false };
        macros[3] = Patch::Metadata::Macro{ .displayName = "Chorus",
                                            .modifiers = { { CP(kChorusMix), 1.0f } },
                                            .bipolar = false };
        macros[4] = Patch::Metadata::Macro{ .displayName = "Reverb mix",
                                            .modifiers = { { CP(kReverbMix), 1.0f } },
                                            .bipolar = false };
        macros[5] = Patch::Metadata::Macro{ .displayName = "Decay", .modifiers = { { CP(kDecay), 1.0f } }, .bipolar = true };
    };

    metadata.instrumentTypeId = 63;
    metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
}

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::ChillamangVoice)
