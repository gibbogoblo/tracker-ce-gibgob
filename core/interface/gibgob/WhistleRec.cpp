#include "gibgob/WhistleRec.h"

#include "Platform.h"
#include "Recorder.h"
#include "TrackerImpl.h"
#include "gibgob/MelodicTrack.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "sampleRecorder/sampleRecorder.h"

#include <elapsedMillis.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{
namespace whistlerec
{

namespace
{
using namespace gibgob::whistle;

constexpr uint32_t kMarginBytes = 64 * 1024; // kept clear after the project's samples
constexpr uint32_t kArmMs = 250;             // the mic settles (no pop) before the count-in
constexpr uint32_t kTailFrames = kRate / 4;  // recorded past the end, for the last readings
constexpr int kLatencyFrames = 256;          // in and out of the codec: the whistle comes in this late
constexpr int kFeedPerPoll = 24;             // readings per poll while recording (the screen stays quick)

Phase s_phase = Phase::Off;
int s_track = 0;
int16_t* s_audio = nullptr;
uint32_t s_audioFrames = 0; // room
Reading* s_readings = nullptr;
Scratch* s_scratch = nullptr;
Take* s_take = nullptr;
PitchTracker s_tracker;
elapsedMillis s_armTimer;
bool s_full = false;

// set from the sequencer (audio context): the recorder's position when the
// track's first micro step plays, and when it comes round again
volatile int32_t s_anchorFrame = -1;
volatile int32_t s_endFrame = -1;
volatile int s_trackTicks = 0;
volatile bool s_hooked = false;
bool s_endMeasured = false;

void tickHook(int track, int tick, int trackTicks)
{
    if (!s_hooked || track != s_track || tick != 0)
        return;
    const int32_t now = (int32_t)recorder.getLength();
    if (s_anchorFrame < 0)
    {
        s_anchorFrame = now;
        s_trackTicks = trackTicks;
    }
    else if (s_endFrame < 0)
        s_endFrame = now;
}

Sgtl5000* codec()
{
    return TrackerImpl::getInstance()->getPlatform()->getCodecAudio();
}

// the Mini's own mic, at the Sample Recorder's mic gain; nothing of it in the
// mix (it would come out of the speaker and back in)
void micOn()
{
    Sgtl5000* c = codec();
    c->setHeadphoneMuted(true); // only while it switches (a pop): see poll()
    c->setInputType(Sgtl5000::InputType::kMic);
    const bool high = mtProject.values.source == cSampleRecorder::sourceTypeMicHG;
    const uint8_t gain = high ? mtProject.values.gainMicHigh : mtProject.values.gainMicLow;
    c->setMicGain((int)(gain * 44.0f / 100.0f));
    engine.disableLineInImpact();
}

void micOff()
{
    Sgtl5000* c = codec();
    c->setInputType(Sgtl5000::InputType::kLineIn);
    c->setLineInLevel(mtProject.values.gainLineIn * 15 / 100);
    c->setHeadphoneMuted(false);
    engine.enableLineInImpact();
#ifdef PLATFORM_IMX
    engine.refreshOutputSourceOutsideTheRecorder();
#endif
}

void stopRecorder()
{
    s_hooked = false;
    g_trackTickHook = nullptr;
    if (recorder.isRecordingActive())
        recorder.stop();
}

uint8_t* alignUp(uint8_t* p)
{
    return (uint8_t*)(((uintptr_t)p + 7) & ~(uintptr_t)7);
}
} // namespace

bool start(int track, char* why, int whyLen)
{
    if (s_phase != Phase::Off)
        cancel();
    if (recorder.isRecordingActive())
    {
        snprintf(why, (size_t)whyLen, "The recorder is busy");
        return false;
    }
    // the free end of the sample memory: readings, working memory, the take, then the sound
    const uint32_t used = mtProject.used_memory + kMarginBytes;
    const uint32_t tables = sizeof(Reading) * kMaxHops + sizeof(Scratch) + sizeof(Take) + 64;
    if (used + tables + kRate * 2 * 4 >= SAMPLE_MEMORY_SIZE)
    {
        snprintf(why, (size_t)whyLen, "Sample memory is full");
        return false;
    }
    uint8_t* p = alignUp((uint8_t*)externalRamSampleMemory + used);
    s_readings = (Reading*)p;
    p = alignUp(p + sizeof(Reading) * kMaxHops);
    s_scratch = (Scratch*)p;
    p = alignUp(p + sizeof(Scratch));
    s_take = (Take*)p;
    p = alignUp(p + sizeof(Take));
    s_audio = (int16_t*)p;
    uint32_t bytes = (uint32_t)(((uint8_t*)externalRamSampleMemory + SAMPLE_MEMORY_SIZE) - p);
    const uint32_t maxBytes = (uint32_t)kMaxHops * kHop * 2; // no more than the readings can follow
    bytes = bytes > maxBytes ? maxBytes : bytes;
    bytes &= ~(uint32_t)511;
    s_audioFrames = bytes / 2;
    memset(s_take, 0, sizeof(Take));

    if (sequencer.isPlay() || sequencer.isCountingIn())
        sequencer.stop();

    s_track = track;
    s_anchorFrame = -1;
    s_endFrame = -1;
    s_trackTicks = 0;
    s_endMeasured = false;
    s_full = false;
    s_tracker.begin(s_readings, kMaxHops);

    micOn();
    Recorder::Config cfg;
    cfg.destinationBufferAddress = s_audio;
    cfg.destinationBufferSize = bytes;
    cfg.destinationChannels = 1;
    cfg.sourceChannels = 1;
    cfg.source = Recorder::Config::Source::kI2s;
    recorder.setFullMemoryCallback([]() { s_full = true; });
    recorder.setConfig(cfg);
    recorder.record();

    g_trackTickHook = tickHook;
    s_hooked = true;
    s_armTimer = 0;
    s_phase = Phase::Arming;
    return true;
}

void poll()
{
    switch (s_phase)
    {
        case Phase::Off:
        case Phase::Done:
            return;
        case Phase::Arming:
            if (s_armTimer >= kArmMs)
            {
                codec()->setHeadphoneMuted(false); // hear the count-in
                setLiveMetronome(true);
                sequencer.startCountIn();
                s_phase = Phase::CountIn;
            }
            break;
        case Phase::CountIn:
            if (s_anchorFrame >= 0)
                s_phase = Phase::Taking;
            else if (!sequencer.isCountingIn() && !sequencer.isPlay())
            {
                cancel(); // stopped during the count-in
                return;
            }
            break;
        case Phase::Taking:
            break;
    }

    // follow the pitch (a few readings at a time)
    const uint32_t len = recorder.getLength();
    const uint32_t upTo = (uint32_t)(s_tracker.count() + kFeedPerPoll) * kHop + kNeed;
    s_tracker.feed(s_audio, len < upTo ? len : upTo);

    if (s_phase != Phase::Taking)
        return;
    // stopped by hand: what was whistled so far counts
    const bool stopped = !sequencer.isPlay();
    const bool ended = s_endFrame >= 0 && len >= (uint32_t)s_endFrame + kTailFrames;
    if (!(ended || stopped || s_full || len + kHop >= s_audioFrames))
        return;

    stopRecorder();
    setLiveMetronome(false);
    micOff();
    const uint32_t frames = recorder.getLength();
    s_tracker.feed(s_audio, frames); // the rest
    s_endMeasured = s_endFrame >= 0; // else cut short: the tempo from the sequencer (hopsPerTick)
    findNotes(s_tracker.readings(), s_tracker.count(), *s_take, *s_scratch);
    s_phase = Phase::Done;
}

Phase phase()
{
    return s_phase;
}

void cancel()
{
    if (s_phase == Phase::Off)
        return;
    const bool wasRecording = s_phase != Phase::Done;
    if (wasRecording)
    {
        stopRecorder();
        setLiveMetronome(false);
        if (sequencer.isCountingIn())
            sequencer.stop();
        micOff();
    }
    s_phase = Phase::Off;
}

void finish()
{
    s_phase = Phase::Off;
}

int track()
{
    return s_track;
}

float pitchNow()
{
    const int n = s_tracker.count();
    if (s_phase == Phase::Off || n == 0)
        return 0.0f;
    const Reading& r = s_tracker.readings()[n - 1];
    return (r.clarity > 0.72f && r.level > 0.004f && r.pitch >= 66.0f) ? r.pitch : 0.0f;
}

float levelNow()
{
    const int n = s_tracker.count();
    return (s_phase == Phase::Off || n == 0) ? 0.0f : s_tracker.readings()[n - 1].level;
}

float progress()
{
    if (s_phase != Phase::Taking || s_anchorFrame < 0)
        return 0.0f;
    const int ticks = s_trackTicks;
    const int now = playheadTick(s_track);
    return ticks > 0 && now >= 0 ? (float)now / (float)ticks : 0.0f;
}

const Take& take()
{
    return *s_take;
}

int32_t anchorHop()
{
    return (s_anchorFrame + kLatencyFrames + kHop / 2) / kHop;
}

float hopsPerTick()
{
    if (s_endMeasured && s_endFrame > s_anchorFrame && s_trackTicks > 0)
    {
        // measured: the frames one pass of the track took (swing and all)
        return (float)(s_endFrame - s_anchorFrame) / (float)s_trackTicks / (float)kHop;
    }
    // cut short (stopped by hand): from the tempo
    const float bpm = sequencer.getCurrentTempo();
    const float ticksPerSecond = (bpm > 1.0f ? bpm : 120.0f) / 60.0f * 4.0f * (float)kTicksPerStep;
    return (float)kRate / ticksPerSecond / (float)kHop;
}

int trackTicks()
{
    return s_trackTicks;
}

} // namespace whistlerec
} // namespace gibgob
