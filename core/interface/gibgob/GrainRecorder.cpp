#include "gibgob/GrainRecorder.h"

#include "Platform.h"
#include "Recorder.h"
#include "TrackerImpl.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"
#include "sampleRecorder/sampleRecorder.h"

#include "cr_section_macros.h"

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
    const uint32_t frames = recorder.getLength();
    if (frames < kRate / 10)
    {
        snprintf(why, (size_t)whyLen, "Too short: nothing kept");
        return false;
    }

    // short fades so the ends do not click
    const uint32_t fade = frames > 4410 ? 220 : frames / 20;
    for (uint32_t i = 0; i < fade; i++)
    {
        s_buffer[i] = (int16_t)((int32_t)s_buffer[i] * (int32_t)i / (int32_t)fade);
        s_buffer[frames - 1 - i] = (int16_t)((int32_t)s_buffer[frames - 1 - i] * (int32_t)i / (int32_t)fade);
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
    const uint8_t* data = reinterpret_cast<const uint8_t*>(s_buffer);
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
