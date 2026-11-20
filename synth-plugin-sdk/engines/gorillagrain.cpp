// GORILLAGRAIN - GibGob OS granular synth plugin (.psyn).
// Glue between the Polyend synth SDK and the DSP core in gorillagrain/grain_dsp.h.
// The sound comes from the sample slot: /SynthSamples/GORILLA/ (the engine's
// own GORILLA.wav by default, or any WAV put in that folder).
// MIT License, part of GibGob OS.

#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "gorillagrain/grain_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace polysynth
{

// Parameter ids are stored in saved patches: only ever add at the end.
enum class GrainParamId : int
{
    kPosition = 0,
    kScan,
    kSize,
    kDensity,
    kSpray,
    kJitter,
    kSparkle,
    kReverse,
    kWindow,
    kSpread,
    kTune,
    kFine,
    // filter
    kFilterType,
    kCutoff,
    kResonance,
    kKeyTrack,
    // amp
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kVolume,
    kPan,
    kVelToAmp,
    // LFO 1
    kLfo1Shape,
    kLfo1Rate,
    kLfo1Sync,
    kLfo1Retrig,
    kLfo1DestA,
    kLfo1AmtA,
    kLfo1DestB,
    kLfo1AmtB,
    // LFO 2
    kLfo2Shape,
    kLfo2Rate,
    kLfo2Sync,
    kLfo2Retrig,
    kLfo2DestA,
    kLfo2AmtA,
    kLfo2DestB,
    kLfo2AmtB,
    // Env 1
    kEnv1Attack,
    kEnv1Decay,
    kEnv1Sustain,
    kEnv1Release,
    kEnv1DestA,
    kEnv1AmtA,
    kEnv1DestB,
    kEnv1AmtB,
    // Env 2
    kEnv2Attack,
    kEnv2Decay,
    kEnv2Sustain,
    kEnv2Release,
    kEnv2DestA,
    kEnv2AmtA,
    kEnv2DestB,
    kEnv2AmtB,
    // FX
    kDelayType,
    kDelayTime,
    kDelayFeedback,
    kDelayMix,
    kReverbType,
    kReverbDecay,
    kReverbTone,
    kReverbMix,
};

namespace
{

constexpr const char* kWindowNames[grain::kWindowCount] = { "Smooth", "Soft", "Pluck", "Swell" };
constexpr const char* kFilterNames[grain::kFilterTypeCount] = { "LP 12", "LP 24", "Band pass", "High pass" };
constexpr const char* kLfoShapeNames[morph::kLfoShapeCount] = {
    "Sine", "Triangle", "Saw up", "Saw down", "Square", "S&H", "Smooth rnd",
};
constexpr const char* kLfoSyncNames[morph::kLfoSyncCount] = {
    "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/4D",
};
constexpr const char* kDestNames[grain::kDestCount] = {
    "Off",       "Pitch",     "Position", "Scan",      "Size",      "Density",   "Spray",
    "Jitter",    "Spread",    "Cutoff",   "Resonance", "Volume",    "Pan",       "LFO1 rate",
    "LFO2 rate", "Delay mix", "Reverb mix", "Reverb decay",
};
constexpr const char* kDelayTypeNames[morph::kDelayTypeCount] = { "Off", "Digital", "Tape", "Ping-pong" };
constexpr const char* kDelayTimeNames[morph::kDelayTimeCount] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2",
};
constexpr const char* kReverbTypeNames[morph::kReverbTypeCount] = { "Off", "Plate", "Dark", "Shimmer" };
constexpr const char* kOnOffNames[2] = { "Free", "Retrig" };

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

auto bipolarPercentFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int pct = static_cast<int>(std::lround(value * 100.0f));
        return pct > 0 ? fmt::format("+{:d} %", pct) : fmt::format("{:d} %", pct);
    };
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

// "1.00x", "Frozen", "-0.50x"
auto scanFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int h = static_cast<int>(std::lround(value * 100.0f));
        if (h == 0)
            return std::string("Frozen");
        const int a = h < 0 ? -h : h;
        return fmt::format("{}{:d}.{:d}{:d}x", h < 0 ? "-" : "", a / 100, (a / 10) % 10, a % 10);
    };
}

auto msFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    { return fmt::format("{:d} ms", static_cast<int>(std::lround(value))); };
}

auto perSecondFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const int t = static_cast<int>(std::lround(value * 10.0f));
        return fmt::format("{:d}.{:d} /s", t / 10, t % 10);
    };
}

} // namespace

class GorillaGrainVoice : public sdk::CustomSynthVoice<GorillaGrainVoice, GrainParamId>
{
  public:
    GorillaGrainVoice()
    {
        static uint32_t s_seed = 1;
        dsp.init(s_seed++);
    }

    ~GorillaGrainVoice() { delete[] fxMemory; }

    bool isActive() final { return gate || dsp.active(); }
    bool hasTickIdle() const final { return true; }
    void tickIdle(const InstrumentVoiceContext& ctx) final
    {
        dsp.tickIdle(params, ctx.tempo, static_cast<int>(kAudioBufferSize));
    }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float velocity, bool /*isLegatoStep*/) final
    {
        refreshSource();
        dsp.noteOn(params, src, static_cast<float>(note), velocity);
        gate = true;
#ifdef GORILLA_DIAG
        dsp.dbgReset();
        diagBlocks = 0;
#endif
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        dsp.noteOff();
        gate = false;
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer) final
    {
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        // the sample can change while a note plays (a new one picked): look every block
        refreshSource();
        dsp.setSource(src);
        if (src.data == nullptr)
        {
            // no sample on the card: a soft double beep while a key is held
            for (int i = 0; i < n; ++i)
            {
                const uint32_t t = blip % 22050u;
                const bool on = gate && (t < 2205u || (t > 4410u && t < 6615u));
                beepPhase += 659.25f / 44100.0f;
                if (beepPhase >= 1.0f)
                    beepPhase -= 1.0f;
                const float b = on ? sdk::fastSine(beepPhase) * 0.15f : 0.0f;
                left[i] = right[i] = b;
                ++blip;
            }
            dsp.render(params, ctx.tempo, pitchBendSemitones, scratchL.data(), scratchR.data(), n);
            return;
        }
        blip = 0;
        dsp.render(params, ctx.tempo, pitchBendSemitones, left.data(), right.data(), n);
#ifdef GORILLA_DIAG
        // after ~0.3 s of a held note, a stage that never made sound beeps its number:
        // 1 the grains are silent  2 the filter kills them  3 the envelope / volume is 0
        // 4 broken numbers (NaN)
        lastVoicePeak = dsp.dbgOut;
        if (gate && ++diagBlocks > 100)
        {
            int code = 0;
            if (dsp.dbgNan)
                code = 4;
            else if (dsp.dbgGrains < 1e-5f)
                code = 1;
            else if (dsp.dbgFilter < 1e-5f)
                code = 2;
            else if (dsp.dbgEnv < 1e-5f || dsp.dbgOut < 1e-6f)
                code = 3;
            if (code != 0)
                beepCode(code, left.data(), right.data(), n);
        }
#endif
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

    // delay + reverb once on the instrument's mix (the host calls this on the first voice)
    void renderPostProcessAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer) final
    {
        params.fillFx(fxParams);
        if (fxParams.delayType == morph::kDelayOff && fxParams.reverbType == morph::kReverbOff)
        {
            if (fx.ready())
                fx.process(fxParams, ctx.tempo, dsp.fxMod, nullptr, nullptr, 0); // clears the tails
            return;
        }
        if (!fx.ready() && !fxAllocFailed)
        {
            fxMemory = new (std::nothrow) uint8_t[morph::Fx::kMemoryBytes];
            if (fxMemory == nullptr)
                fxAllocFailed = true;
            else
                fx.attach(fxMemory);
        }
#ifdef GORILLA_DIAG
        {
            // 5 the voice sounds but the mix handed to the effects is silent
            // 6 no memory for the effects   7 the effects break the sound
            auto left = outputBuffer[0];
            auto right = outputBuffer[1];
            const int n = static_cast<int>(std::min(left.size(), right.size()));
            float inPeak = 0.0f;
            for (int i = 0; i < n; ++i)
                inPeak = std::max(inPeak, std::fabs(left[i]));
            int code = 0;
            if (fxAllocFailed)
                code = 6;
            else if (gate && diagBlocks > 100 && lastVoicePeak > 1e-4f && inPeak < 1e-6f)
                code = 5;
            if (code == 0 && !fxAllocFailed)
            {
                fx.process(fxParams, ctx.tempo, dsp.fxMod, left.data(), right.data(), n);
                bool bad = false;
                float outPeak = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    if (!(left[i] == left[i]) || !(right[i] == right[i]))
                        bad = true;
                    outPeak = std::max(outPeak, std::fabs(left[i]));
                }
                if (bad || (inPeak > 1e-3f && outPeak < 1e-6f))
                    fxBroken = true;
                if (fxBroken)
                    code = 7;
                else
                    return;
            }
            beepCode(code, left.data(), right.data(), n);
            return;
        }
#endif
        if (fxAllocFailed)
            return; // no memory for the effects: dry
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        fx.process(fxParams, ctx.tempo, dsp.fxMod, left.data(), right.data(), n);
    }

#ifdef GORILLA_DIAG
    // code beeps (0.2 s each) in a 1.6 s cycle, replacing the sound
    void beepCode(int code, float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const uint32_t t = diagSamples % 70560u;
            const uint32_t slot = t / 8820u;
            const bool on = slot < (uint32_t)code && (t % 8820u) < 4410u;
            diagPhase += 880.0f / 44100.0f;
            if (diagPhase >= 1.0f)
                diagPhase -= 1.0f;
            l[i] = r[i] = on ? sdk::fastSine(diagPhase) * 0.3f : 0.0f;
            ++diagSamples;
        }
    }
    uint32_t diagSamples = 0, diagBlocks = 0;
    float diagPhase = 0.0f, lastVoicePeak = 0.0f;
    bool fxBroken = false;
#endif

    bool hasPostProcess() const final { return true; }

    // The GibGob editor's grain view reads this: [0] the marker, [1] the
    // playhead (0..1), [2] how many grains, then for each grain where (0..1),
    // how loud (0..1) and its pan (-1..1). Smaller buffers get the old scope.
    static constexpr float kGrainViewMarker = 62062.0f;
    void renderGraph(std::span<float> points) override
    {
        constexpr int kMax = grain::Voice::kMaxGrains;
        if (points.size() >= static_cast<size_t>(3 + 3 * kMax))
        {
            float pos[kMax], lvl[kMax], pan[kMax];
            const int n = dsp.active() ? dsp.grainView(pos, lvl, pan, kMax) : 0;
            points[0] = kGrainViewMarker;
            points[1] = dsp.playhead();
            points[2] = static_cast<float>(n);
            for (int i = 0; i < n; ++i)
            {
                points[3 + i * 3] = pos[i];
                points[4 + i * 3] = lvl[i];
                points[5 + i * 3] = pan[i];
            }
            for (size_t i = 3 + static_cast<size_t>(n) * 3; i < points.size(); ++i)
                points[i] = 0.0f;
            return;
        }
        for (size_t i = 0; i < points.size(); ++i)
        {
            const size_t s = (i * lastGraphSamples.size()) / std::max<size_t>(1, points.size());
            points[i] = std::clamp(lastGraphSamples[s], -1.0f, 1.0f);
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata);

    grain::Params params;

  private:
    void refreshSource()
    {
        const Sample* s = getPatch() != nullptr ? getPatch()->getSample(0) : nullptr;
        const bool ok = s != nullptr && s->ready && (s->format.channels == 1 || s->format.channels == 2) &&
                        s->data.size() >= static_cast<size_t>(64 * s->format.channels);
        if (!ok)
        {
            src = grain::Source{};
            return;
        }
        src.data = s->data.data();
        src.channels = s->format.channels;
        src.frames = static_cast<uint32_t>(s->data.size() / static_cast<size_t>(s->format.channels));
        src.rateRatio = s->format.sampleRate > 0 ? static_cast<float>(s->format.sampleRate) / 44100.0f : 1.0f;
        src.rootNote = static_cast<float>(s->basePitch);
    }

    grain::Voice dsp;
    grain::Source src;
    morph::Fx fx;
    morph::Params fxParams;
    uint8_t* fxMemory = nullptr;
    bool fxAllocFailed = false;
    bool gate = false;
    uint32_t blip = 0;
    float beepPhase = 0.0f;
    std::array<float, kAudioBufferSize> lastGraphSamples{};
    std::array<float, kAudioBufferSize> scratchL{}, scratchR{};
};

// ---------------------------------------------------------------- parameters

#define GP(id) (*GrainParamId::id)
#define ADD_FLOAT(id, field, ...)                                                                  \
    helper.addParam(GP(id), Param{ __VA_ARGS__ },                                                  \
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.field = *x; })
#define ADD_ENUM(id, field, type, names, ...)                                                      \
    helper.addParam(GP(id),                                                                        \
                    Param{ .displayName = __VA_ARGS__,                                             \
                           .defaultValue = 0,                                                      \
                           .minValue = 0,                                                          \
                           .maxValue = static_cast<Patch::paramvalue_short_t>(                     \
                             sizeof(names) / sizeof(names[0]) - 1),                                \
                           .isEnum = true,                                                         \
                           .valueFormatter = enumFormatter(names) },                               \
                    [](auto& v, Patch::optional_paramvalue_t x) {                                  \
                        v.params.field = static_cast<type>(                                        \
                          toIndex(*x, static_cast<int>(sizeof(names) / sizeof(names[0]))));        \
                    })

void GorillaGrainVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;
    using Curve = Patch::Metadata::Param::Curve;

    metadata.params.resize(enumutil::EnumMax<GrainParamId>::value + 1);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
        paramHandlers.resize(enumutil::EnumMax<GrainParamId>::value + 1);
    ParamHelper<GorillaGrainVoice> helper(metadata, paramHandlers);
    const grain::Params d; // the defaults

    // ---- grains
    ADD_FLOAT(kPosition, position, .displayName = "Position", .defaultValue = d.position, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kScan, scan, .displayName = "Scan", .defaultValue = d.scan, .minValue = -2, .maxValue = 2,
              .midiCC = *MidiCCs::kOscParam0 + 1, .valueFormatter = scanFormatter());
    ADD_FLOAT(kSize, sizeMs, .displayName = "Size", .defaultValue = d.sizeMs, .minValue = 10, .maxValue = 1000,
              .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kOscParam0 + 2, .valueFormatter = msFormatter());
    ADD_FLOAT(kDensity, density, .displayName = "Density", .defaultValue = d.density, .minValue = 1,
              .maxValue = 100, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kOscParam0 + 3,
              .valueFormatter = perSecondFormatter());
    ADD_FLOAT(kSpray, spray, .displayName = "Spray", .defaultValue = d.spray, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 4, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kJitter, jitter, .displayName = "Jitter", .defaultValue = d.jitter, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 5, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kSparkle, sparkle, .displayName = "Sparkle", .defaultValue = d.sparkle, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 6, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverse, reverse, .displayName = "Reverse", .defaultValue = d.reverse, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 7, .valueFormatter = createPercentFormatter());
    ADD_ENUM(kWindow, window, uint8_t, kWindowNames, "Window");
    ADD_FLOAT(kSpread, spread, .displayName = "Spread", .defaultValue = d.spread, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 8, .valueFormatter = createPercentFormatter());
    helper.addParam(GP(kTune),
                    Param{ .displayName = "Tune", .defaultValue = 0.0f, .minValue = -24, .maxValue = 24,
                           .midiCC = *MidiCCs::kOscParam0 + 9, .valueFormatter = createSemitoneFormatter() },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.tune = std::round(*x); });
    ADD_FLOAT(kFine, fine, .displayName = "Fine", .defaultValue = 0.0f, .minValue = -100, .maxValue = 100,
              .valueFormatter = createCentsFormatter());

    // ---- filter
    helper.addParam(GP(kFilterType),
                    Param{ .displayName = "Filter type", .defaultValue = d.filterType, .minValue = 0,
                           .maxValue = grain::kFilterTypeCount - 1, .isEnum = true,
                           .midiCC = *MidiCCs::kFilterType, .valueFormatter = enumFormatter(kFilterNames) },
                    [](auto& v, Patch::optional_paramvalue_t x)
                    { v.params.filterType = static_cast<uint8_t>(toIndex(*x, grain::kFilterTypeCount)); });
    ADD_FLOAT(kCutoff, cutoff, .displayName = "Cutoff", .defaultValue = d.cutoff, .minValue = 20.0f,
              .maxValue = 18000.0f, .editCurve = Curve::kExponentialSteep, .midiCC = *MidiCCs::kFilterCutoff,
              .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kResonance, resonance, .displayName = "Resonance", .defaultValue = d.resonance, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kFilterResonance, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kKeyTrack, keyTrack, .displayName = "Key track", .defaultValue = d.keyTrack, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());

    // ---- amp
    ADD_FLOAT(kAmpAttack, amp.attack, .displayName = "Attack", .defaultValue = d.amp.attack, .minValue = 0.0f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvAttack,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kAmpDecay, amp.decay, .displayName = "Decay", .defaultValue = d.amp.decay, .minValue = 0.001f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvDecay,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kAmpSustain, amp.sustain, .displayName = "Sustain", .defaultValue = d.amp.sustain, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kAmpEnvSustain, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kAmpRelease, amp.release, .displayName = "Release", .defaultValue = d.amp.release, .minValue = 0.001f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvRelease,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kVolume, volume, .displayName = "Volume", .defaultValue = d.volume, .minValue = 0, .maxValue = 1.5f,
              .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
              .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kPan, pan, .displayName = "Pan", .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,
              .valueFormatter = panFormatter());
    ADD_FLOAT(kVelToAmp, velToAmp, .displayName = "Velocity", .defaultValue = d.velToAmp, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kVelocityVolume, .valueFormatter = createPercentFormatter());

    // ---- LFOs (LFO 1 wanders the Position a little by default)
#define ADD_LFO(n, i, ccRate)                                                                      \
    ADD_ENUM(kLfo##n##Shape, lfo[i].shape, uint8_t, kLfoShapeNames, "LFO" #n " shape");             \
    ADD_FLOAT(kLfo##n##Rate, lfo[i].rateHz, .displayName = "LFO" #n " rate", .defaultValue = 2.0f,  \
              .minValue = 0.02f, .maxValue = 40.0f, .editCurve = Curve::kExponential,              \
              .midiCC = ccRate, .valueFormatter = createFrequencyFormatter());                     \
    ADD_ENUM(kLfo##n##Sync, lfo[i].sync, uint8_t, kLfoSyncNames, "LFO" #n " sync");                 \
    helper.addParam(GP(kLfo##n##Retrig),                                                           \
                    Param{ .displayName = "LFO" #n " trig", .defaultValue = 0, .minValue = 0,       \
                           .maxValue = 1, .isEnum = true, .valueFormatter = enumFormatter(kOnOffNames) }, \
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.lfo[i].retrig = toIndex(*x, 2) == 1; }); \
    ADD_ENUM(kLfo##n##DestA, lfo[i].slot[0].dest, uint8_t, kDestNames, "LFO" #n " dest A");         \
    ADD_FLOAT(kLfo##n##AmtA, lfo[i].slot[0].amount, .displayName = "LFO" #n " amt A",              \
              .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,                                 \
              .valueFormatter = bipolarPercentFormatter());                                        \
    ADD_ENUM(kLfo##n##DestB, lfo[i].slot[1].dest, uint8_t, kDestNames, "LFO" #n " dest B");         \
    ADD_FLOAT(kLfo##n##AmtB, lfo[i].slot[1].amount, .displayName = "LFO" #n " amt B",              \
              .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,                                 \
              .valueFormatter = bipolarPercentFormatter())

    ADD_LFO(1, 0, *MidiCCs::kLfo1Frequency);
    ADD_LFO(2, 1, *MidiCCs::kLfo2Frequency);
    metadata.params.at(GP(kLfo1Shape)).defaultValue = static_cast<float>(d.lfo[0].shape);
    metadata.params.at(GP(kLfo1Rate)).defaultValue = d.lfo[0].rateHz;
    metadata.params.at(GP(kLfo1DestA)).defaultValue = static_cast<float>(d.lfo[0].slot[0].dest);
    metadata.params.at(GP(kLfo1AmtA)).defaultValue = d.lfo[0].slot[0].amount;
    metadata.params.at(GP(kLfo2Rate)).defaultValue = d.lfo[1].rateHz;

#define ADD_ENV(n, i, ccA)                                                                         \
    ADD_FLOAT(kEnv##n##Attack, env[i].env.attack, .displayName = "Env" #n " attack",               \
              .defaultValue = d.env[i].env.attack, .minValue = 0.0f, .maxValue = 10.0f,            \
              .editCurve = Curve::kExponential, .midiCC = ccA, .valueFormatter = createTimeFormatter()); \
    ADD_FLOAT(kEnv##n##Decay, env[i].env.decay, .displayName = "Env" #n " decay",                  \
              .defaultValue = d.env[i].env.decay, .minValue = 0.001f, .maxValue = 10.0f,           \
              .editCurve = Curve::kExponential, .midiCC = ccA + 1,                                 \
              .valueFormatter = createTimeFormatter());                                            \
    ADD_FLOAT(kEnv##n##Sustain, env[i].env.sustain, .displayName = "Env" #n " sustain",            \
              .defaultValue = d.env[i].env.sustain, .minValue = 0, .maxValue = 1, .midiCC = ccA + 2, \
              .valueFormatter = createPercentFormatter());                                         \
    ADD_FLOAT(kEnv##n##Release, env[i].env.release, .displayName = "Env" #n " release",            \
              .defaultValue = d.env[i].env.release, .minValue = 0.001f, .maxValue = 10.0f,         \
              .editCurve = Curve::kExponential, .midiCC = ccA + 3,                                 \
              .valueFormatter = createTimeFormatter());                                            \
    ADD_ENUM(kEnv##n##DestA, env[i].slot[0].dest, uint8_t, kDestNames, "Env" #n " dest A");         \
    ADD_FLOAT(kEnv##n##AmtA, env[i].slot[0].amount, .displayName = "Env" #n " amt A",              \
              .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,                                 \
              .valueFormatter = bipolarPercentFormatter());                                        \
    ADD_ENUM(kEnv##n##DestB, env[i].slot[1].dest, uint8_t, kDestNames, "Env" #n " dest B");         \
    ADD_FLOAT(kEnv##n##AmtB, env[i].slot[1].amount, .displayName = "Env" #n " amt B",              \
              .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,                                 \
              .valueFormatter = bipolarPercentFormatter())

    ADD_ENV(1, 0, *MidiCCs::kUserEnv1Attack);
    ADD_ENV(2, 1, *MidiCCs::kUserEnv2Attack);

    // ---- FX (Shimmer reverb on by default: pads!)
    ADD_ENUM(kDelayType, delayType, uint8_t, kDelayTypeNames, "Delay");
    helper.addParam(GP(kDelayTime),
                    Param{ .displayName = "Delay time", .defaultValue = d.delayTime, .minValue = 0,
                           .maxValue = morph::kDelayTimeCount - 1, .isEnum = true,
                           .valueFormatter = enumFormatter(kDelayTimeNames) },
                    [](auto& v, Patch::optional_paramvalue_t x)
                    { v.params.delayTime = static_cast<uint8_t>(toIndex(*x, morph::kDelayTimeCount)); });
    ADD_FLOAT(kDelayFeedback, delayFeedback, .displayName = "Delay fdbk", .defaultValue = d.delayFeedback,
              .minValue = 0, .maxValue = 0.95f, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDelayMix, delayMix, .displayName = "Delay mix", .defaultValue = d.delayMix, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kDelaySend, .valueFormatter = createPercentFormatter());
    ADD_ENUM(kReverbType, reverbType, uint8_t, kReverbTypeNames, "Reverb");
    metadata.params.at(GP(kReverbType)).defaultValue = static_cast<float>(d.reverbType);
    ADD_FLOAT(kReverbDecay, reverbDecay, .displayName = "Rev decay", .defaultValue = d.reverbDecay, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbTone, reverbTone, .displayName = "Rev tone", .defaultValue = d.reverbTone, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbMix, reverbMix, .displayName = "Rev mix", .defaultValue = d.reverbMix, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kReverbSend, .valueFormatter = createPercentFormatter());

    metadata.sampleSlots = {
        Patch::Metadata::SampleSlot{ .displayName = "Sample",
                                     .type = Patch::Metadata::SampleSlot::kSample,
                                     .section = 1,
                                     .subsection = 0,
                                     .defaultSamplePath = "GORILLA/GORILLA.wav" },
    };

    // ---- layouts: layout33 drives the GibGob OS editor, the flat one the stock editor
    using E = EditorElement;
    auto el = [](GrainParamId id) { return E{ .type = E::Type::kParam, .id = *id }; };
    const E sample{ .type = E::Type::kSampleSlot, .id = 0 };

    metadata.layout33 = Patch::Metadata::EditorLayout{
        .sections = {
          { .name = "Jungle", .shortName = "GibGob", // the name picks the night jungle look
            .elements = { el(GrainParamId::kPosition), el(GrainParamId::kScan), el(GrainParamId::kSize),
                          el(GrainParamId::kDensity), el(GrainParamId::kSpray), el(GrainParamId::kJitter),
                          el(GrainParamId::kSparkle), el(GrainParamId::kReverse), el(GrainParamId::kCutoff),
                          el(GrainParamId::kAmpAttack), el(GrainParamId::kAmpRelease), el(GrainParamId::kReverbMix) } },
          { .name = "Grains", .shortName = "Osc",
            .subsections = {
              { .name = "Playhead", .elements = { sample, el(GrainParamId::kPosition), el(GrainParamId::kScan),
                                                  el(GrainParamId::kSpray) } },
              { .name = "Grains", .elements = { el(GrainParamId::kSize), el(GrainParamId::kDensity),
                                                el(GrainParamId::kWindow), el(GrainParamId::kJitter) } },
              { .name = "Colour", .elements = { el(GrainParamId::kSparkle), el(GrainParamId::kReverse),
                                                el(GrainParamId::kSpread), el(GrainParamId::kTune),
                                                el(GrainParamId::kFine) } },
            } },
          { .name = "Filter", .shortName = "Filter",
            .subsections = {
              { .elements = { el(GrainParamId::kFilterType), el(GrainParamId::kCutoff),
                              el(GrainParamId::kResonance), el(GrainParamId::kKeyTrack) } },
            } },
          { .name = "Amp", .shortName = "Amp",
            .subsections = {
              { .name = "Envelope", .elements = { el(GrainParamId::kAmpAttack), el(GrainParamId::kAmpDecay),
                                                  el(GrainParamId::kAmpSustain), el(GrainParamId::kAmpRelease) } },
              { .name = "Voice", .elements = { el(GrainParamId::kVolume), el(GrainParamId::kPan),
                                               el(GrainParamId::kVelToAmp) } },
            } },
          { .name = "LFOs", .shortName = "LFO",
            .subsections = {
              { .name = "LFO 1", .elements = { el(GrainParamId::kLfo1Shape), el(GrainParamId::kLfo1Rate),
                                               el(GrainParamId::kLfo1Sync), el(GrainParamId::kLfo1Retrig),
                                               el(GrainParamId::kLfo1DestA), el(GrainParamId::kLfo1AmtA),
                                               el(GrainParamId::kLfo1DestB), el(GrainParamId::kLfo1AmtB) } },
              { .name = "LFO 2", .elements = { el(GrainParamId::kLfo2Shape), el(GrainParamId::kLfo2Rate),
                                               el(GrainParamId::kLfo2Sync), el(GrainParamId::kLfo2Retrig),
                                               el(GrainParamId::kLfo2DestA), el(GrainParamId::kLfo2AmtA),
                                               el(GrainParamId::kLfo2DestB), el(GrainParamId::kLfo2AmtB) } },
            } },
          { .name = "Envelopes", .shortName = "Env",
            .subsections = {
              { .name = "Env 1", .elements = { el(GrainParamId::kEnv1Attack), el(GrainParamId::kEnv1Decay),
                                               el(GrainParamId::kEnv1Sustain), el(GrainParamId::kEnv1Release),
                                               el(GrainParamId::kEnv1DestA), el(GrainParamId::kEnv1AmtA),
                                               el(GrainParamId::kEnv1DestB), el(GrainParamId::kEnv1AmtB) } },
              { .name = "Env 2", .elements = { el(GrainParamId::kEnv2Attack), el(GrainParamId::kEnv2Decay),
                                               el(GrainParamId::kEnv2Sustain), el(GrainParamId::kEnv2Release),
                                               el(GrainParamId::kEnv2DestA), el(GrainParamId::kEnv2AmtA),
                                               el(GrainParamId::kEnv2DestB), el(GrainParamId::kEnv2AmtB) } },
            } },
          { .name = "Effects", .shortName = "FX",
            .subsections = {
              { .name = "Delay", .elements = { el(GrainParamId::kDelayType), el(GrainParamId::kDelayTime),
                                               el(GrainParamId::kDelayFeedback), el(GrainParamId::kDelayMix) } },
              { .name = "Reverb", .elements = { el(GrainParamId::kReverbType), el(GrainParamId::kReverbDecay),
                                                el(GrainParamId::kReverbTone), el(GrainParamId::kReverbMix) } },
            } },
        },
    };

    // flat layout for the stock editor: one section per page, same order
    Patch::Metadata::EditorLayout flat;
    for (const auto& section : metadata.layout33.sections)
    {
        if (section.shortName == "GibGob")
            continue;
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
        macros[0] = Patch::Metadata::Macro{ .displayName = "Position", .modifiers = { { GP(kPosition), 1.0f } }, .bipolar = true };
        macros[1] = Patch::Metadata::Macro{ .displayName = "Size", .modifiers = { { GP(kSize), 1.0f } }, .bipolar = true };
        macros[2] = Patch::Metadata::Macro{ .displayName = "Density", .modifiers = { { GP(kDensity), 1.0f } }, .bipolar = true };
        macros[3] = Patch::Metadata::Macro{ .displayName = "Spray", .modifiers = { { GP(kSpray), 1.0f } }, .bipolar = false };
        macros[4] = Patch::Metadata::Macro{ .displayName = "Cutoff", .modifiers = { { GP(kCutoff), 1.0f } }, .bipolar = true };
        macros[5] = Patch::Metadata::Macro{ .displayName = "Reverb mix", .modifiers = { { GP(kReverbMix), 1.0f } }, .bipolar = false };
    };

    metadata.instrumentTypeId = 62;
    metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
}

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::GorillaGrainVoice)
