#include "gibgob/GrainRecorder.h"

#include "Platform.h"
#include "Recorder.h"
#include "TrackerImpl.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"
#include "sampleRecorder/sampleRecorder.h"

#include "cr_section_macros.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gibgob
{
namespace grainrec
{

namespace
{
// in the external RAM (the fast RAM is full); cleared on first use
__NOINIT(EXTERNAL_RAM) State s_stateMem;
bool s_stateReady = false;
State& st()
{
    if (!s_stateReady)
    {
        memset(&s_stateMem, 0, sizeof(s_stateMem));
        s_stateReady = true;
    }
    return s_stateMem;
}
int16_t* s_buffer = nullptr;
uint32_t s_seen = 0;   // frames already turned into peaks
bool s_micMuted = false;
volatile bool s_fullFlag = false;

constexpr uint32_t kRate = 44100;
constexpr uint32_t kMarginBytes = 64 * 1024; // kept clear after the project's samples

Sgtl5000* codec()
{
    return TrackerImpl::getInstance()->getPlatform()->getCodecAudio();
}

// the input the Sample Recorder uses, at its gain
void applyInput(bool& mic, bool& usb)
{
    const uint8_t src = mtProject.values.source;
    mic = src == cSampleRecorder::sourceTypeMicLG || src == cSampleRecorder::sourceTypeMicHG;
#ifdef PLATFORM_IMX
    usb = src == cSampleRecorder::sourceTypeUsb;
#else
    usb = false;
#endif
    Sgtl5000* c = codec();
    if (usb)
    {
#ifdef PLATFORM_IMX
        const float g = 1.0f + (3.98f - 1.0f) * (float)mtProject.values.recorderGainUsb / 100.0f;
        engine.getAudioInput()->getUsbRecordingAmplifier(AudioInput::InputChannels::kLeft)->gain(g);
        engine.getAudioInput()->getUsbRecordingAmplifier(AudioInput::InputChannels::kRight)->gain(g);
#endif
    }
    else if (mic)
    {
        // as the Sample Recorder: the headphones off, or the mic hears itself
        c->setHeadphoneMuted(true);
        s_micMuted = true;
        c->setInputType(Sgtl5000::InputType::kMic);
        const uint8_t gain = src == cSampleRecorder::sourceTypeMicLG ? mtProject.values.gainMicLow
                                                                     : mtProject.values.gainMicHigh;
        c->setMicGain((int)(gain * 44.0f / 100.0f));
    }
    else
    {
        c->setInputType(Sgtl5000::InputType::kLineIn);
        c->setLineInLevel(mtProject.values.gainLineIn * 15 / 100);
    }
}

void restoreInput()
{
    if (s_micMuted)
    {
        codec()->setHeadphoneMuted(false);
        s_micMuted = false;
    }
#ifdef PLATFORM_IMX
    engine.refreshOutputSourceOutsideTheRecorder();
    engine.forceUsbGainOutsideTheRecorder();
#endif
}

void writeLE16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
void writeLE32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
// Cuts the silence off both ends of the take and brings its loudest point up
// to just under full scale (at most +24 dB, so a near-silent take does not
// turn into loud hiss). Silence = quieter than 1/64 of the loudest point
// (-36 dB) and than ~-50 dBFS. A little is kept before the first sound (5 ms,
// so the attack stays whole) and after the last (150 ms, so it can ring out).
// false if there is nothing but silence.
bool trimAndNormalize(int16_t*& take, uint32_t& frames)
{
    int peak = 0;
    for (uint32_t i = 0; i < frames; i++)
    {
        const int a = take[i] < 0 ? -take[i] : take[i];
        peak = a > peak ? a : peak;
    }
    if (peak < 60) // about -55 dBFS: nothing came in
        return false;
    int threshold = peak / 64;
    threshold = threshold < 100 ? 100 : threshold;
    threshold = threshold > peak ? peak : threshold; // a very quiet take: its loudest point still counts
    uint32_t first = 0, last = frames - 1;
    while (first < frames && (take[first] < 0 ? -take[first] : take[first]) < threshold)
        first++;
    while (last > first && (take[last] < 0 ? -take[last] : take[last]) < threshold)
        last--;
    const uint32_t pre = kRate / 200, post = kRate * 15 / 100;
    first = first > pre ? first - pre : 0;
    last = last + post < frames ? last + post : frames - 1;
    const uint32_t kept = last - first + 1;

    // louder: up to -0.3 dBFS, at most 16x (+24 dB)
    float gain = 31700.0f / (float)peak;
    gain = gain > 16.0f ? 16.0f : (gain < 1.0f ? 1.0f : gain);
    if (gain > 1.01f)
    {
        for (uint32_t i = first; i <= last; i++)
        {
            int v = (int)((float)take[i] * gain);
            take[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
    }
    State& s = st();
    s.keptSeconds = kept / (float)kRate;
    s.trimmedSeconds = (frames - kept) / (float)kRate;
    s.gainDb = 20.0f * log10f(gain);
    take += first;
    frames = kept;
    return true;
}
} // namespace

const State& state()
{
    return st();
}

bool start(char* why, int whyLen)
{
    if (st().active)
        return true;
    if (recorder.isRecordingActive())
    {
        snprintf(why, (size_t)whyLen, "The recorder is busy");
        return false;
    }
    // the free end of the sample memory, after the project's samples
    const uint32_t used = mtProject.used_memory + kMarginBytes;
    if (used + kRate * 2 >= SAMPLE_MEMORY_SIZE)
    {
        snprintf(why, (size_t)whyLen, "Sample memory is full");
        return false;
    }
    uint32_t bytes = SAMPLE_MEMORY_SIZE - used;
    const uint32_t maxBytes = kMaxSeconds * kRate * 2;
    bytes = bytes > maxBytes ? maxBytes : bytes;
    bytes &= ~(uint32_t)511;
    s_buffer = externalRamSampleMemory + (used + 1) / 2; // bytes to 16-bit samples

    bool mic = false, usb = false;
    applyInput(mic, usb);

    Recorder::Config cfg;
    cfg.destinationBufferAddress = s_buffer;
    cfg.destinationBufferSize = bytes;
    cfg.destinationChannels = 1; // mono: half the memory, plenty for grains
    cfg.sourceChannels = mic ? 1 : 2;
    cfg.source = usb ? Recorder::Config::Source::kUsb : Recorder::Config::Source::kI2s;
    s_fullFlag = false;
    recorder.setFullMemoryCallback([]() { s_fullFlag = true; });
    recorder.setConfig(cfg);
    recorder.record();

    memset(&st(), 0, sizeof(State));
    st().active = true;
    st().maxFrames = bytes / 2;
    s_seen = 0;
    return true;
}

void poll()
{
    if (!st().active)
        return;
    const uint32_t len = recorder.getLength();
    st().frames = len;
    // peaks of what came in since last time (and the level now)
    int peak = 0;
    for (uint32_t f = s_seen; f < len; f++)
    {
        int a = s_buffer[f];
        a = a < 0 ? -a : a;
        peak = a > peak ? a : peak;
        const uint32_t bin = (uint32_t)((uint64_t)f * kPeakBins / (st().maxFrames ? st().maxFrames : 1));
        if (bin < (uint32_t)kPeakBins)
        {
            const uint8_t v = (uint8_t)(a > 32767 ? 255 : a >> 7);
            if (v > st().peaks[bin])
                st().peaks[bin] = v;
        }
    }
    if (len > s_seen)
        st().level = peak / 32768.0f;
    s_seen = len;
    if (s_fullFlag || !recorder.isRecordingActive())
        st().full = true;
}

void cancel()
{
    if (!st().active)
        return;
    if (recorder.isRecordingActive())
        recorder.stop();
    restoreInput();
    st().active = false;
}

bool stopAndSave(const char* folder, char* path, int pathLen, char* why, int whyLen)
{
    path[0] = 0;
    if (!st().active)
    {
        snprintf(why, (size_t)whyLen, "Not recording");
        return false;
    }
    if (recorder.isRecordingActive())
        recorder.stop();
    restoreInput();
    st().active = false;
    uint32_t frames = recorder.getLength();
    if (frames < kRate / 10)
    {
        snprintf(why, (size_t)whyLen, "Too short: nothing kept");
        return false;
    }

    // trim the silence at both ends, then make it louder (see trimAndNormalize)
    int16_t* take = s_buffer;
    uint32_t total = frames;
    if (!trimAndNormalize(take, total))
    {
        snprintf(why, (size_t)whyLen, "Only silence: nothing kept");
        return false;
    }
    frames = total;

    // short fades so the ends do not click
    const uint32_t fade = frames > 4410 ? 220 : frames / 20;
    for (uint32_t i = 0; i < fade; i++)
    {
        take[i] = (int16_t)((int32_t)take[i] * (int32_t)i / (int32_t)fade);
        take[frames - 1 - i] = (int16_t)((int32_t)take[frames - 1 - i] * (int32_t)i / (int32_t)fade);
    }

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr || !fs->isSdCardInserted())
    {
        snprintf(why, (size_t)whyLen, "No SD card");
        return false;
    }
    if (!fs->exists(folder))
        fs->mkdir(0, folder);
    char file[96];
    int n = 1;
    for (; n <= 99; n++)
    {
        snprintf(file, sizeof(file), "%s/REC_%02d.wav", folder, n);
        if (!fs->exists(file))
            break;
    }
    if (n > 99)
    {
        snprintf(why, (size_t)whyLen, "100 recordings already: delete some");
        return false;
    }

    auto [ok, fh] = fs->openFile(file, FileSystem::kWriteCreateNew);
    if (!ok || fh == nullptr)
    {
        snprintf(why, (size_t)whyLen, "Could not write to the card");
        return false;
    }
    uint8_t h[44];
    const uint32_t dataBytes = frames * 2;
    memcpy(h, "RIFF", 4);
    writeLE32(h + 4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    writeLE32(h + 16, 16);
    writeLE16(h + 20, 1); // PCM
    writeLE16(h + 22, 1); // mono
    writeLE32(h + 24, kRate);
    writeLE32(h + 28, kRate * 2);
    writeLE16(h + 32, 2);
    writeLE16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    writeLE32(h + 40, dataBytes);
    bool good = fs->writeFile(h, sizeof(h), fh) == (int32_t)sizeof(h);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(take);
    for (uint32_t off = 0; good && off < dataBytes; off += 16384)
    {
        const uint32_t chunk = dataBytes - off < 16384 ? dataBytes - off : 16384;
        good = fs->writeFile(data + off, chunk, fh) == (int32_t)chunk;
    }
    fs->closeFile(fh);
    if (!good)
    {
        fs->remove(file);
        snprintf(why, (size_t)whyLen, "Card full or write error");
        return false;
    }
    snprintf(path, (size_t)pathLen, "%s", file);
    return true;
}

} // namespace grainrec
} // namespace gibgob
