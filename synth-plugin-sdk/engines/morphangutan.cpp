// MORPHANGUTAN - GibGob OS synth plugin (.psyn).
// Thin glue between the Polyend synth SDK and the DSP core in
// morphangutan/morph_dsp.h (which is also used to render WAV previews).
// MIT License, part of GibGob OS.

#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "morphangutan/morph_dsp.h"

// Hardware test switches (normal build: none defined)
//   MORPH_MONO      mono output instead of stereo
//   MORPH_NO_FX     no instrument effects (no post process)
//   MORPH_TEST_SINE plain sine wave instead of the MORPHANGUTAN voice

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace polysynth
{

// Parameter ids are stored in saved patches: only ever add at the end.
enum class MorphParamId : int
{
    // MAMA osc
    kMamaMorph = 0,
    kMamaTune,
    kMamaFine,
    kMamaLevel,
    // BABY osc
    kBabyMorph,
    kBabyTune,
    kBabyFine,
    kBabyLevel,
    // PAPA sub osc
    kPapaMorph,
    kPapaOctave,
    kPapaLevel,
    kNoiseLevel,
    kDrift,
    // filter
    kFilterType,
    kCutoff,
    kResonance,
    kDrive,
    kKeyTrack,
    // amp
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kVolume,
    kPan,
    kVelToAmp,
    kGlideMode,
    kGlideTime,
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

constexpr const char* kShapeNames[4] = { "Sine", "Tri", "Saw", "Square" };
constexpr const char* kFilterNames[morph::kFilterTypeCount] = {
    "LP 12", "LP 24", "Band pass", "High pass", "Comb +", "Comb -",
};
constexpr const char* kLfoShapeNames[morph::kLfoShapeCount] = {
    "Sine", "Triangle", "Saw up", "Saw down", "Square", "S&H", "Smooth rnd",
};
constexpr const char* kLfoSyncNames[morph::kLfoSyncCount] = {
    "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/4D",
};
constexpr const char* kDestNames[morph::kDestCount] = {
    "Off",       "Pitch",      "MAMA pitch", "BABY pitch", "MAMA morph", "BABY morph",
    "PAPA morph", "MAMA vol",  "BABY vol",   "PAPA vol",   "Noise vol",  "Cutoff",
    "Resonance", "Drive",      "Volume",     "Pan",        "LFO1 rate",  "LFO2 rate",
    "Delay mix", "Delay fdbk", "Reverb mix", "Reverb decay",
};
constexpr const char* kDelayTypeNames[morph::kDelayTypeCount] = { "Off", "Digital", "Tape", "Ping-pong" };
constexpr const char* kDelayTimeNames[morph::kDelayTimeCount] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2",
};
constexpr const char* kReverbTypeNames[morph::kReverbTypeCount] = { "Off", "Plate", "Dark", "Shimmer" };
constexpr const char* kPapaOctaveNames[3] = { "-2 oct", "-1 oct", "0 oct" };
constexpr const char* kGlideModeNames[3] = { "Always", "Overlap", "Legato" };
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

// "Saw", or "Tri>Saw 40%" between two shapes
auto morphFormatter()
{
    return [](const Patch& /*patch*/, Patch::paramvalue_t value)
    {
        const float v = std::clamp(value, 0.0f, 3.0f);
        const int idx = std::min(2, static_cast<int>(v));
        const float frac = v - static_cast<float>(idx);
        if (frac < 0.02f)
        {
            return std::string(kShapeNames[idx]);
        }
        if (frac > 0.98f)
        {
            return std::string(kShapeNames[idx + 1]);
        }
        return fmt::format("{}>{} {:.0f}%", kShapeNames[idx], kShapeNames[idx + 1], frac * 100.0f);
    };
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
        {
            return std::string("Centre");
        }
        return pct < 0 ? fmt::format("L {:d}", -pct) : fmt::format("R {:d}", pct);
    };
}

} // namespace

class MorphangutanVoice : public sdk::CustomSynthVoice<MorphangutanVoice, MorphParamId>
{
  public:
    MorphangutanVoice()
    {
        static uint32_t s_seed = 1;
        dsp.init(s_seed++);
    }

    ~MorphangutanVoice()
    {
        delete[] fxMemory;
    }

#if defined(MORPH_DIAG)
    // diagnosis build: stays active while the key is held, so a code can be beeped
    bool isActive() final { return diagGate || dsp.active() || diagLevel > 0.0001f; }
    bool diagGate = false;
    float diagLevel = 0.0f, diagPhase = 0.0f;
    uint32_t diagSamples = 0;
#elif defined(MORPH_TEST_SINE)
    bool isActive() final { return sineGate || sineLevel > 0.0001f; }
    bool sineGate = false;
    float sineLevel = 0.0f, sinePhase = 0.0f, sineHz = 440.0f;
#else
    bool isActive() final { return dsp.active(); }
#endif

    bool hasTickIdle() const final { return true; }

    void tickIdle(const InstrumentVoiceContext& ctx) final
    {
        dsp.tickIdle(params, ctx.tempo, static_cast<int>(kAudioBufferSize));
    }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float velocity, bool isLegatoStep) final
    {
        params.glideMode = static_cast<uint8_t>(playMode);
        dsp.noteOn(params, static_cast<float>(note), velocity, isLegatoStep);
#ifdef MORPH_DIAG
        diagGate = true;
        diagSamples = 0;
        dsp.dbgReset();
#endif
#ifdef MORPH_TEST_SINE
        sineGate = true;
        sineHz = 440.0f * sdk::fastPow2((static_cast<float>(note) - 69.0f) / 12.0f);
#endif
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        dsp.noteOff();
#ifdef MORPH_DIAG
        diagGate = false;
#endif
#ifdef MORPH_TEST_SINE
        sineGate = false;
#endif
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer) final
    {
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
#ifdef MORPH_TEST_SINE
        (void)ctx;
        if (sineGate || sineLevel > 0.0001f)
        {
            for (int i = 0; i < n; ++i)
            {
                sineLevel += ((sineGate ? 0.4f : 0.0f) - sineLevel) * 0.002f;
                sinePhase += sineHz * (1.0f / 44100.0f);
                if (sinePhase >= 1.0f)
                {
                    sinePhase -= 1.0f;
                }
                left[i] = right[i] = sdk::fastSine(sinePhase) * sineLevel;
            }
        }
        else
        {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
        }
#else
        dsp.render(params, ctx.tempo, pitchBendSemitones, left.data(), right.data(), n);
#endif
#ifdef MORPH_DIAG
        {
            // the engine's own sound when it makes one; otherwise a beep code:
            // 1 beep: the envelope never opens   2: the oscillators are silent
            // 3: the filter kills it             4: volume / velocity is zero
            // 5: broken numbers (NaN)            6: the pan is zero
            // 7 (a steady tone): every stage works but the output is zero
            float peak = 0.0f;
            bool finite = true;
            for (int i = 0; i < n; ++i)
            {
                const float a = left[i] < 0 ? -left[i] : left[i];
                peak = a > peak ? a : peak;
                if (!(left[i] == left[i]) || !(right[i] == right[i]))
                    finite = false;
            }
            int code = 0;
            if (!finite || dsp.dbgNan)
                code = 5;
            else if (peak < 1e-5f)
            {
                if (dsp.dbgE < 1e-6f)
                    code = 1;
                else if (dsp.dbgS < 1e-7f)
                    code = 2;
                else if (dsp.dbgY < 1e-7f)
                    code = 3;
                else if (dsp.dbgAmp < 1e-6f)
                    code = 4;
                else if (dsp.dbgPan < 1e-6f)
                    code = 6;
                else
                    code = 7;
            }
            if (code != 0 || diagLevel > 0.0001f)
            {
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t t = diagSamples % 52920u; // 1.2 s cycle
                    const uint32_t pulse = t / 8820u;         // 0.2 s slots
                    bool on = diagGate && code != 0 &&
                              (code == 7 || (pulse < (uint32_t)code && (t % 8820u) < 4410u));
                    diagLevel += ((on ? 0.35f : 0.0f) - diagLevel) * 0.01f;
                    diagPhase += 880.0f * (1.0f / 44100.0f);
                    if (diagPhase >= 1.0f)
                        diagPhase -= 1.0f;
                    const float b = sdk::fastSine(diagPhase) * diagLevel;
                    left[i] = (code != 0 ? 0.0f : left[i]) + b;
                    right[i] = (code != 0 ? 0.0f : right[i]) + b;
                    ++diagSamples;
                }
            }
            else
            {
                diagSamples += (uint32_t)n;
            }
        }
#endif
#ifdef MORPH_MONO
        for (int i = 0; i < n; ++i)
        {
            left[i] = 0.5f * (left[i] + right[i]);
        }
#endif
        for (int i = 0; i < n && i < static_cast<int>(lastGraphSamples.size()); ++i)
        {
            lastGraphSamples[i] = 0.5f * (left[i] + right[i]);
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
        renderVoiceAudio(ctx, outputBuffer);
    }

#ifndef MORPH_NO_FX
    // Delay and reverb run once on the mix of all voices (the host calls this
    // on the instrument's first voice only). Their memory is taken the first
    // time an effect is switched on.
    void renderPostProcessAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer) final
    {
        if (params.delayType == morph::kDelayOff && params.reverbType == morph::kReverbOff)
        {
            if (fx.ready())
            {
                fx.process(params, ctx.tempo, dsp.fxMod, nullptr, nullptr, 0); // clears the tails
            }
            return;
        }
        if (!fx.ready())
        {
            if (fxAllocFailed)
            {
                return;
            }
            fxMemory = new (std::nothrow) uint8_t[morph::Fx::kMemoryBytes];
            if (fxMemory == nullptr)
            {
                fxAllocFailed = true;
                return;
            }
            fx.attach(fxMemory);
        }
        auto left = outputBuffer[0];
        auto right = outputBuffer[1];
        const int n = static_cast<int>(std::min(left.size(), right.size()));
        fx.process(params, ctx.tempo, dsp.fxMod, left.data(), right.data(), n);
    }

    bool hasPostProcess() const final { return true; }
#endif

    void renderGraph(std::span<float> points) override
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            const size_t src = (i * lastGraphSamples.size()) / std::max<size_t>(1, points.size());
            points[i] = std::clamp(lastGraphSamples[src], -1.0f, 1.0f);
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata);

    morph::Params params;

  private:
    morph::Voice dsp;
    morph::Fx fx;
    uint8_t* fxMemory = nullptr;
    bool fxAllocFailed = false;
    std::array<float, kAudioBufferSize> lastGraphSamples{};
};

// ---------------------------------------------------------------- parameters

#define MP(id) (*MorphParamId::id)
#define ADD_FLOAT(id, field, ...)                                                                  \
    helper.addParam(MP(id), Param{ __VA_ARGS__ },                                                  \
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.field = *x; })
#define ADD_ENUM(id, field, type, names, ...)                                                      \
    helper.addParam(MP(id),                                                                        \
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

void MorphangutanVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;
    using Curve = Patch::Metadata::Param::Curve;

    metadata.params.resize(enumutil::EnumMax<MorphParamId>::value + 1);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
    {
        paramHandlers.resize(enumutil::EnumMax<MorphParamId>::value + 1);
    }
    ParamHelper<MorphangutanVoice> helper(metadata, paramHandlers);

    // ---- oscillators
    ADD_FLOAT(kMamaMorph, mamaMorph, .displayName = "MAMA shape", .defaultValue = 2.0f, .minValue = 0,
              .maxValue = 3, .midiCC = *MidiCCs::kOscParam0, .valueFormatter = morphFormatter());
    helper.addParam(MP(kMamaTune),
                    Param{ .displayName = "MAMA tune", .defaultValue = 0.0f, .minValue = -24,
                           .maxValue = 24, .midiCC = *MidiCCs::kOscParam0 + 1,
                           .valueFormatter = createSemitoneFormatter() },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.mamaTune = std::round(*x); });
    ADD_FLOAT(kMamaFine, mamaFine, .displayName = "MAMA fine", .defaultValue = 0.0f, .minValue = -100,
              .maxValue = 100, .valueFormatter = createCentsFormatter());
    ADD_FLOAT(kMamaLevel, mamaLevel, .displayName = "MAMA level", .defaultValue = 0.8f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 2, .valueFormatter = createPercentFormatter());

    ADD_FLOAT(kBabyMorph, babyMorph, .displayName = "BABY shape", .defaultValue = 2.0f, .minValue = 0,
              .maxValue = 3, .midiCC = *MidiCCs::kOscParam0 + 3, .valueFormatter = morphFormatter());
    helper.addParam(MP(kBabyTune),
                    Param{ .displayName = "BABY tune", .defaultValue = 0.0f, .minValue = -24,
                           .maxValue = 24, .midiCC = *MidiCCs::kOscParam0 + 4,
                           .valueFormatter = createSemitoneFormatter() },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.babyTune = std::round(*x); });
    ADD_FLOAT(kBabyFine, babyFine, .displayName = "BABY fine", .defaultValue = 7.0f, .minValue = -100,
              .maxValue = 100, .midiCC = *MidiCCs::kOscParam0 + 5, .valueFormatter = createCentsFormatter());
    ADD_FLOAT(kBabyLevel, babyLevel, .displayName = "BABY level", .defaultValue = 0.6f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 6, .valueFormatter = createPercentFormatter());

    ADD_FLOAT(kPapaMorph, papaMorph, .displayName = "PAPA shape", .defaultValue = 0.0f, .minValue = 0,
              .maxValue = 3, .midiCC = *MidiCCs::kOscParam0 + 7, .valueFormatter = morphFormatter());
    helper.addParam(MP(kPapaOctave),
                    Param{ .displayName = "PAPA octave", .defaultValue = 1, .minValue = 0, .maxValue = 2,
                           .isEnum = true, .valueFormatter = enumFormatter(kPapaOctaveNames) },
                    [](auto& v, Patch::optional_paramvalue_t x) { v.params.papaOctave = toIndex(*x, 3) - 2; });
    ADD_FLOAT(kPapaLevel, papaLevel, .displayName = "PAPA level", .defaultValue = 0.4f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 8, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kNoiseLevel, noiseLevel, .displayName = "Noise", .defaultValue = 0.0f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kOscParam0 + 9, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDrift, drift, .displayName = "Drift", .defaultValue = 0.15f, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 10, .valueFormatter = createPercentFormatter());

    // ---- filter
    helper.addParam(MP(kFilterType),
                    Param{ .displayName = "Filter type", .defaultValue = 1, .minValue = 0,
                           .maxValue = morph::kFilterTypeCount - 1, .isEnum = true,
                           .midiCC = *MidiCCs::kFilterType, .valueFormatter = enumFormatter(kFilterNames) },
                    [](auto& v, Patch::optional_paramvalue_t x)
                    { v.params.filterType = static_cast<uint8_t>(toIndex(*x, morph::kFilterTypeCount)); });
    ADD_FLOAT(kCutoff, cutoff, .displayName = "Cutoff", .defaultValue = 2400.0f, .minValue = 20.0f,
              .maxValue = 18000.0f, .editCurve = Curve::kExponentialSteep, .midiCC = *MidiCCs::kFilterCutoff,
              .valueFormatter = createFrequencyFormatter());
    ADD_FLOAT(kResonance, resonance, .displayName = "Resonance", .defaultValue = 0.25f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kFilterResonance, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDrive, drive, .displayName = "Drive", .defaultValue = 0.0f, .minValue = 0, .maxValue = 1,
              .midiCC = *MidiCCs::kOscParam0 + 11, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kKeyTrack, keyTrack, .displayName = "Key track", .defaultValue = 0.3f, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());

    // ---- amp
    ADD_FLOAT(kAmpAttack, amp.attack, .displayName = "Attack", .defaultValue = 0.003f, .minValue = 0.0f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvAttack,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kAmpDecay, amp.decay, .displayName = "Decay", .defaultValue = 0.4f, .minValue = 0.001f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvDecay,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kAmpSustain, amp.sustain, .displayName = "Sustain", .defaultValue = 0.75f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kAmpEnvSustain, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kAmpRelease, amp.release, .displayName = "Release", .defaultValue = 0.25f, .minValue = 0.001f,
              .maxValue = 10.0f, .editCurve = Curve::kExponential, .midiCC = *MidiCCs::kAmpEnvRelease,
              .valueFormatter = createTimeFormatter());
    ADD_FLOAT(kVolume, volume, .displayName = "Volume", .defaultValue = 0.8f, .minValue = 0, .maxValue = 1.5f,
              .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
              .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kPan, pan, .displayName = "Pan", .defaultValue = 0.0f, .minValue = -1, .maxValue = 1,
              .valueFormatter = panFormatter());
    ADD_FLOAT(kVelToAmp, velToAmp, .displayName = "Velocity", .defaultValue = 0.7f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kVelocityVolume, .valueFormatter = createPercentFormatter());
    helper.addParam(MP(kGlideMode),
                    Param{ .displayName = "Glide mode", .defaultValue = 0, .minValue = 0, .maxValue = 2,
                           .isEnum = true, .valueFormatter = enumFormatter(kGlideModeNames) },
                    [](auto& v, Patch::optional_paramvalue_t x)
                    {
                        v.playMode = static_cast<MorphangutanVoice::PlayMode>(toIndex(*x, 3));
                        v.params.glideMode = static_cast<uint8_t>(toIndex(*x, 3));
                    });
    ADD_FLOAT(kGlideTime, glideTime, .displayName = "Glide", .defaultValue = 0.0f, .minValue = 0,
              .maxValue = 2.0f, .editCurve = Curve::kExponential, .valueFormatter = createTimeFormatter());

    // ---- LFOs
#define ADD_LFO(n, i, ccRate)                                                                      \
    ADD_ENUM(kLfo##n##Shape, lfo[i].shape, uint8_t, kLfoShapeNames, "LFO" #n " shape");             \
    ADD_FLOAT(kLfo##n##Rate, lfo[i].rateHz, .displayName = "LFO" #n " rate", .defaultValue = 2.0f,  \
              .minValue = 0.02f, .maxValue = 40.0f, .editCurve = Curve::kExponential,              \
              .midiCC = ccRate, .valueFormatter = createFrequencyFormatter());                     \
    ADD_ENUM(kLfo##n##Sync, lfo[i].sync, uint8_t, kLfoSyncNames, "LFO" #n " sync");                 \
    helper.addParam(MP(kLfo##n##Retrig),                                                           \
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
    metadata.params.at(MP(kLfo2Rate)).defaultValue = 0.3f;

    // ---- modulation envelopes (Env 1 -> Cutoff +35 % by default: a filter envelope)
#define ADD_ENV(n, i, a, d, s, r, ccA)                                                             \
    ADD_FLOAT(kEnv##n##Attack, env[i].env.attack, .displayName = "Env" #n " attack",               \
              .defaultValue = a, .minValue = 0.0f, .maxValue = 10.0f,                              \
              .editCurve = Curve::kExponential, .midiCC = ccA, .valueFormatter = createTimeFormatter()); \
    ADD_FLOAT(kEnv##n##Decay, env[i].env.decay, .displayName = "Env" #n " decay",                  \
              .defaultValue = d, .minValue = 0.001f, .maxValue = 10.0f,                            \
              .editCurve = Curve::kExponential, .midiCC = ccA + 1,                                 \
              .valueFormatter = createTimeFormatter());                                            \
    ADD_FLOAT(kEnv##n##Sustain, env[i].env.sustain, .displayName = "Env" #n " sustain",            \
              .defaultValue = s, .minValue = 0, .maxValue = 1, .midiCC = ccA + 2,                  \
              .valueFormatter = createPercentFormatter());                                         \
    ADD_FLOAT(kEnv##n##Release, env[i].env.release, .displayName = "Env" #n " release",            \
              .defaultValue = r, .minValue = 0.001f, .maxValue = 10.0f,                            \
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

    ADD_ENV(1, 0, 0.002f, 0.35f, 0.2f, 0.3f, *MidiCCs::kUserEnv1Attack);
    ADD_ENV(2, 1, 0.5f, 0.5f, 0.5f, 0.5f, *MidiCCs::kUserEnv2Attack);
    metadata.params.at(MP(kEnv1DestA)).defaultValue = static_cast<float>(morph::kDestCutoff);
    metadata.params.at(MP(kEnv1AmtA)).defaultValue = 0.35f;

    // ---- FX
    ADD_ENUM(kDelayType, delayType, uint8_t, kDelayTypeNames, "Delay");
    helper.addParam(MP(kDelayTime),
                    Param{ .displayName = "Delay time", .defaultValue = 5, .minValue = 0,
                           .maxValue = morph::kDelayTimeCount - 1, .isEnum = true,
                           .valueFormatter = enumFormatter(kDelayTimeNames) },
                    [](auto& v, Patch::optional_paramvalue_t x)
                    { v.params.delayTime = static_cast<uint8_t>(toIndex(*x, morph::kDelayTimeCount)); });
    ADD_FLOAT(kDelayFeedback, delayFeedback, .displayName = "Delay fdbk", .defaultValue = 0.4f,
              .minValue = 0, .maxValue = 0.95f, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kDelayMix, delayMix, .displayName = "Delay mix", .defaultValue = 0.3f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kDelaySend, .valueFormatter = createPercentFormatter());
    ADD_ENUM(kReverbType, reverbType, uint8_t, kReverbTypeNames, "Reverb");
    ADD_FLOAT(kReverbDecay, reverbDecay, .displayName = "Rev decay", .defaultValue = 0.6f, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbTone, reverbTone, .displayName = "Rev tone", .defaultValue = 0.5f, .minValue = 0,
              .maxValue = 1, .valueFormatter = createPercentFormatter());
    ADD_FLOAT(kReverbMix, reverbMix, .displayName = "Rev mix", .defaultValue = 0.3f, .minValue = 0,
              .maxValue = 1, .midiCC = *MidiCCs::kReverbSend, .valueFormatter = createPercentFormatter());

    // ---- layouts (the stock editor; GibGob OS gets its own editor next)
    using E = EditorElement;
    auto el = [](MorphParamId id) { return E{ .type = E::Type::kParam, .id = *id }; };
    const E empty{ .type = E::Type::kEmpty };

    metadata.layout33 = Patch::Metadata::EditorLayout{
        .sections = {
          { .name = "Oscillators", .shortName = "Osc",
            .subsections = {
              { .name = "MAMA", .elements = { el(MorphParamId::kMamaMorph), el(MorphParamId::kMamaTune),
                                              el(MorphParamId::kMamaFine), el(MorphParamId::kMamaLevel) } },
              { .name = "BABY", .elements = { el(MorphParamId::kBabyMorph), el(MorphParamId::kBabyTune),
                                              el(MorphParamId::kBabyFine), el(MorphParamId::kBabyLevel) } },
              { .name = "PAPA", .elements = { el(MorphParamId::kPapaMorph), el(MorphParamId::kPapaOctave),
                                              el(MorphParamId::kPapaLevel), el(MorphParamId::kNoiseLevel),
                                              el(MorphParamId::kDrift) } },
            } },
          { .name = "Filter", .shortName = "Filter",
            .subsections = {
              { .elements = { el(MorphParamId::kFilterType), el(MorphParamId::kCutoff),
                              el(MorphParamId::kResonance), el(MorphParamId::kDrive),
                              el(MorphParamId::kKeyTrack) } },
            } },
          { .name = "Amp", .shortName = "Amp",
            .subsections = {
              { .name = "Envelope", .elements = { el(MorphParamId::kAmpAttack), el(MorphParamId::kAmpDecay),
                                                  el(MorphParamId::kAmpSustain), el(MorphParamId::kAmpRelease) } },
              { .name = "Voice", .elements = { el(MorphParamId::kVolume), el(MorphParamId::kPan),
                                               el(MorphParamId::kVelToAmp), el(MorphParamId::kGlideMode),
                                               el(MorphParamId::kGlideTime) } },
            } },
          { .name = "LFOs", .shortName = "LFO",
            .subsections = {
              { .name = "LFO 1", .elements = { el(MorphParamId::kLfo1Shape), el(MorphParamId::kLfo1Rate),
                                               el(MorphParamId::kLfo1Sync), el(MorphParamId::kLfo1Retrig),
                                               el(MorphParamId::kLfo1DestA), el(MorphParamId::kLfo1AmtA),
                                               el(MorphParamId::kLfo1DestB), el(MorphParamId::kLfo1AmtB) } },
              { .name = "LFO 2", .elements = { el(MorphParamId::kLfo2Shape), el(MorphParamId::kLfo2Rate),
                                               el(MorphParamId::kLfo2Sync), el(MorphParamId::kLfo2Retrig),
                                               el(MorphParamId::kLfo2DestA), el(MorphParamId::kLfo2AmtA),
                                               el(MorphParamId::kLfo2DestB), el(MorphParamId::kLfo2AmtB) } },
            } },
          { .name = "Envelopes", .shortName = "Env",
            .subsections = {
              { .name = "Env 1", .elements = { el(MorphParamId::kEnv1Attack), el(MorphParamId::kEnv1Decay),
                                               el(MorphParamId::kEnv1Sustain), el(MorphParamId::kEnv1Release),
                                               el(MorphParamId::kEnv1DestA), el(MorphParamId::kEnv1AmtA),
                                               el(MorphParamId::kEnv1DestB), el(MorphParamId::kEnv1AmtB) } },
              { .name = "Env 2", .elements = { el(MorphParamId::kEnv2Attack), el(MorphParamId::kEnv2Decay),
                                               el(MorphParamId::kEnv2Sustain), el(MorphParamId::kEnv2Release),
                                               el(MorphParamId::kEnv2DestA), el(MorphParamId::kEnv2AmtA),
                                               el(MorphParamId::kEnv2DestB), el(MorphParamId::kEnv2AmtB) } },
            } },
          { .name = "Effects", .shortName = "FX",
            .subsections = {
              { .name = "Delay", .elements = { el(MorphParamId::kDelayType), el(MorphParamId::kDelayTime),
                                               el(MorphParamId::kDelayFeedback), el(MorphParamId::kDelayMix) } },
              { .name = "Reverb", .elements = { el(MorphParamId::kReverbType), el(MorphParamId::kReverbDecay),
                                                el(MorphParamId::kReverbTone), el(MorphParamId::kReverbMix) } },
            } },
        },
    };
    (void)empty;

    // flat layout: one section per page, same order
    Patch::Metadata::EditorLayout flat;
    for (const auto& section : metadata.layout33.sections)
    {
        EditorSection s{ .name = section.name, .shortName = section.shortName };
        for (const auto& sub : section.subsections)
        {
            for (const auto& e : sub.elements)
            {
                s.elements.push_back(e);
            }
        }
        flat.sections.push_back(s);
    }
    metadata.layoutFlat = flat;
    metadata.layout23 = flat;

    metadata.initDefaultMacros = [](auto& macros)
    {
        macros[0] = Patch::Metadata::Macro{
            .displayName = "Morph",
            .modifiers = { { MP(kMamaMorph), 0.8f }, { MP(kBabyMorph), 0.8f } },
            .bipolar = true,
        };
        macros[1] = Patch::Metadata::Macro{
            .displayName = "Cutoff",
            .modifiers = { { MP(kCutoff), 1.0f } },
            .bipolar = true,
        };
        macros[2] = Patch::Metadata::Macro{
            .displayName = "Resonance",
            .modifiers = { { MP(kResonance), 1.0f } },
            .bipolar = true,
        };
        macros[3] = Patch::Metadata::Macro{
            .displayName = "Env 1 amount",
            .modifiers = { { MP(kEnv1AmtA), 1.0f } },
            .bipolar = true,
        };
        macros[4] = Patch::Metadata::Macro{
            .displayName = "Delay mix",
            .modifiers = { { MP(kDelayMix), 1.0f } },
            .bipolar = false,
        };
        macros[5] = Patch::Metadata::Macro{
            .displayName = "Reverb mix",
            .modifiers = { { MP(kReverbMix), 1.0f } },
            .bipolar = false,
        };
    };

    metadata.instrumentTypeId = 60;
#ifdef MORPH_MONO
    metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
#else
    metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
#endif
}

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::MorphangutanVoice)
