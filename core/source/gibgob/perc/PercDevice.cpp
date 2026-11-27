// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS PERC on the Mini (PercDevice.h).
#include "gibgob/perc/PercDevice.h"

#include "TrackerImpl.h"
#include "fileManager.h"
#include "fileTransfer.h"
#include "gibgob/MelodicTrack.h"
#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "ampLogarythmicValues.h"
#include <stdio.h>
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

namespace gibgob
{
namespace perc
{

namespace
{
__BSS(SRAM_OC) Engine s_engine;
bool s_ready = false;
bool s_loaded = false; // a project's PERC is in the engine (or its defaults)

// the project's PERC state (with the sounds in the engine)
bool s_mode = false;
bool s_assigned = false;   // the drum tracks got kick, snare, hat, cowbell
uint8_t s_sampleMask = 0;  // drum tracks whose source is their sample
bool s_dirty = false;
uint8_t s_look = 1;         // the editor's look (1 BLAST, 2 CLUB, 3 RED)

// hits for the audio interrupt: voice -> 0x8000 | note << 8 | velocity (the last one wins)
__BSS(SRAM_OC) volatile uint32_t s_pending[Engine::kVoices];
__BSS(SRAM_OC) volatile uint32_t s_pendingLocks[Engine::kVoices]; // its locks: kind, value, kind, value (a byte each)

const char* const kWorkspaceFile = "Workspace/perc.bin";
const char* const kFileName = "perc.bin";
constexpr uint32_t kMagic = 0x43524550; // "PERC"
constexpr uint8_t kVersion = 1;

struct FileHeader
{
    uint32_t magic;
    uint8_t version;
    uint8_t mode;
    uint8_t sampleMask;
    uint8_t paramCount; // the values stored per sound (more can be added: the rest keep defaults)
    uint8_t flags;      // 1: the default sounds were given
    uint8_t look;       // the editor's look (0: the first, BLAST)
    uint8_t reserved[6];
};
static_assert(sizeof(FileHeader) == 16, "perc.bin header");

struct FileSound
{
    uint8_t type;
    uint8_t hatOpen;
    char name[12];
};

struct Lock
{
    Lock() { DISABLE_AUDIO_INTERRUPTS(); }
    ~Lock() { ENABLE_AUDIO_INTERRUPTS(); }
};

inline bool validTrack(int t)
{
    return t >= 0 && t < Engine::kVoices;
}

FileSystem* fs()
{
    return TrackerImpl::getInstance()->getPlatform()->getFileSystem();
}

bool isDrumTrack(int t)
{
    const Sequencer::strPattern* p = gibgob::current();
    return p != nullptr && !gibgob::isMelodic(p, t) && !gibgob::isSliceTrack(p, t);
}

bool hookHit(int track, uint8_t pitch, uint8_t velocity, const uint8_t* locks)
{
    if (!playsPerc(track))
        return false;
    const Sequencer::strPattern* p = gibgob::current();
    const int note = 60 + (int)pitch - (int)gibgob::laneNote(p, track);
    play(track, velocity, note, locks);
    return true;
}

void defaults()
{
    Lock lock;
    s_engine.init();
    for (int t = 0; t < Engine::kVoices; t++)
    {
        Sound s;
        if (t < kDefaultTracks)
            defaultSound(t, s);
        else
            initSound(s, kDrum);
        s_engine.setSound(t, s);
        s_pending[t] = 0;
    }
    s_mode = false;
    s_assigned = false;
    s_sampleMask = 0;
    s_look = 1;
    s_loaded = true;
}
} // namespace

void init()
{
    if (s_ready)
        return;
    if (!s_loaded)
        defaults();
    s_ready = true;
    g_percHit = hookHit;
}

bool modeOn()
{
    return s_mode;
}

void setMode(bool on)
{
    if (on && !s_assigned)
    {
        // the first time: the drum tracks in order get kick, snare, hat, cowbell
        int k = 0;
        for (int t = 0; t < Engine::kVoices && k < kDefaultTracks; t++)
        {
            if (!isDrumTrack(t))
                continue;
            Sound s;
            defaultSound(k++, s);
            setSound(t, s);
        }
        s_assigned = true;
    }
    if (!on)
    {
        Lock lock;
        for (int t = 0; t < Engine::kVoices; t++)
            s_engine.stop(t);
    }
    s_mode = on;
    markChanged();
}

bool trackSource(int track)
{
    return validTrack(track) && !(s_sampleMask & (1u << track));
}

void setTrackSource(int track, bool perc)
{
    if (!validTrack(track))
        return;
    if (perc)
        s_sampleMask = (uint8_t)(s_sampleMask & ~(1u << track));
    else
        s_sampleMask = (uint8_t)(s_sampleMask | (1u << track));
    markChanged();
}

bool playsPerc(int track)
{
    return s_ready && s_mode && validTrack(track) && trackSource(track) && isDrumTrack(track);
}

const Sound& sound(int track)
{
    return s_engine.sound(validTrack(track) ? track : 0);
}

void setSound(int track, const Sound& s)
{
    if (!validTrack(track))
        return;
    {
        Lock lock;
        s_engine.setSound(track, s);
        s_pending[track] = 0;
    }
    markChanged();
}

void setValue(int track, int param, int value)
{
    if (!validTrack(track))
        return;
    {
        Lock lock;
        s_engine.setValue(track, param, value);
    }
    markChanged();
}

void setHatOpen(int track, bool open)
{
    if (!validTrack(track))
        return;
    {
        Lock lock;
        s_engine.setHatOpen(track, open);
    }
    markChanged();
}

void loadKit(int kit)
{
    static const uint8_t kOrder[kKitSlotCount] = { kSlotD1,     kSlotSnare,    kSlotClosedHat, kSlotD2,
                                                   kSlotD3,     kSlotOpenHat,  kSlotCymbal };
    int k = 0;
    for (int t = 0; t < Engine::kVoices && k < kKitSlotCount; t++)
    {
        if (!isDrumTrack(t) || !trackSource(t))
            continue;
        Sound s;
        soundFromKit(kit, kOrder[k++], s);
        setSound(t, s);
    }
}

void play(int track, uint8_t velocity, int note, const uint8_t* locks)
{
    if (!validTrack(track))
        return;
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity);
    const uint32_t l = locks == nullptr ? 0u
                                        : (uint32_t)locks[0] | ((uint32_t)locks[1] << 8) | ((uint32_t)locks[2] << 16) |
                                            ((uint32_t)locks[3] << 24);
    __atomic_store_n(&s_pendingLocks[track], l, __ATOMIC_RELAXED); // before the hit itself (released with it)
    __atomic_store_n(&s_pending[track], 0x8000u | ((uint32_t)note << 8) | velocity, __ATOMIC_RELEASE);
}

float level(int track)
{
    return validTrack(track) ? s_engine.level(track) : 0.f;
}

bool sounding(int track)
{
    return validTrack(track) && s_engine.sounding(track);
}

AUDIO_CORE_RAM void render(float* left, float* right, int n)
{
    if (!s_ready)
        return;
    s_engine.setTempo(sequencer.getCurrentTempo());
    // the mixer's level for each track: its volume and the dry mix, mute / solo / drum cut (the
    // voice's mute), scaled so the default settings play PERC at its own level
    static const float kUnity = 1.f / (mixAmpLogValues[DEFAULT_TRACK_VOLUME] * mixAmpLogValues[100]);
    for (int t = 0; t < Engine::kVoices; t++)
    {
        const uint32_t hit = __atomic_exchange_n(&s_pending[t], 0u, __ATOMIC_ACQ_REL);
        if (hit & 0x8000u)
        {
            const uint32_t l = __atomic_load_n(&s_pendingLocks[t], __ATOMIC_RELAXED);
            const uint8_t locks[4] = { (uint8_t)l, (uint8_t)(l >> 8), (uint8_t)(l >> 16), (uint8_t)(l >> 24) };
            s_engine.trigger(t, (uint8_t)(hit & 0x7F), (int)((hit >> 8) & 0x7F), l ? locks : nullptr);
        }
        float g = 0.f;
        if (!mtProject.values.dryMixMute && !instrumentPlayer[t].gibgobMuted())
        {
            const uint8_t tv = mtProject.values.trackVolume[t] > 100 ? 100 : mtProject.values.trackVolume[t];
            const uint8_t dv = mtProject.values.dryMixVolume > 100 ? 100 : mtProject.values.dryMixVolume;
            g = mixAmpLogValues[tv] * mixAmpLogValues[dv] * kUnity;
        }
        s_engine.setGain(t, g);
    }
    s_engine.render(left, right, n);
}

int look()
{
    return s_look;
}

void setLook(int l)
{
    s_look = (uint8_t)(l >= 1 && l <= 3 ? l : 1);
    markChanged();
}

// ---- the file ----
void markChanged()
{
    s_dirty = true;
    newFileManager.setProjectStructChanged();
}

void newProject()
{
    defaults();
    s_dirty = true;
    saveWorkspace();
}

void loadWorkspace()
{
    defaults();
    s_dirty = false;
    FileSystem* f = fs();
    if (f == nullptr || !f->exists(kWorkspaceFile))
        return;
    auto [ok, h] = f->openFile(kWorkspaceFile, FileSystem::FileOpenMode::kRead);
    if (!ok)
        return;
    FileHeader hd;
    memset(&hd, 0, sizeof(hd));
    bool good = f->readFile(&hd, sizeof(hd), h) == (int32_t)sizeof(hd) && hd.magic == kMagic && hd.version == kVersion &&
                hd.paramCount > 0;
    if (good)
    {
        Sound sounds[Engine::kVoices];
        uint8_t values[255];
        for (int t = 0; t < Engine::kVoices && good; t++)
        {
            FileSound fsnd;
            good = f->readFile(&fsnd, sizeof(fsnd), h) == (int32_t)sizeof(fsnd) &&
                   f->readFile(values, hd.paramCount, h) == (int32_t)hd.paramCount;
            if (!good)
                break;
            initSound(sounds[t], fsnd.type < kVoiceTypeCount ? (int)fsnd.type : (int)kDrum);
            sounds[t].hatOpen = fsnd.hatOpen ? 1 : 0;
            memcpy(sounds[t].name, fsnd.name, sizeof(sounds[t].name));
            sounds[t].name[sizeof(sounds[t].name) - 1] = 0;
            const int count = hd.paramCount < (int)kParamCount ? (int)hd.paramCount : (int)kParamCount;
            for (int p = 0; p < count; p++)
                sounds[t].value[p] = values[p]; // setSound puts each back in its range
        }
        if (good)
        {
            {
                Lock lock;
                for (int t = 0; t < Engine::kVoices; t++)
                    s_engine.setSound(t, sounds[t]);
            }
            s_mode = hd.mode != 0;
            s_sampleMask = hd.sampleMask;
            s_assigned = (hd.flags & 1) != 0;
            s_look = hd.look >= 1 && hd.look <= 3 ? hd.look : 1;
        }
    }
    f->closeFile(h);
}

void saveWorkspace()
{
    FileSystem* f = fs();
    if (f == nullptr)
        return;
    if (!s_dirty && f->exists(kWorkspaceFile))
        return;
    if (f->exists(kWorkspaceFile))
        f->remove(kWorkspaceFile);
    auto [ok, h] = f->openFile(kWorkspaceFile, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
        return;
    FileHeader hd;
    memset(&hd, 0, sizeof(hd));
    hd.magic = kMagic;
    hd.version = kVersion;
    hd.mode = s_mode ? 1 : 0;
    hd.sampleMask = s_sampleMask;
    hd.paramCount = kParamCount;
    hd.flags = s_assigned ? 1 : 0;
    hd.look = s_look;
    bool good = f->writeFile(&hd, sizeof(hd), h) == (int32_t)sizeof(hd);
    for (int t = 0; t < Engine::kVoices && good; t++)
    {
        const Sound& s = s_engine.sound(t);
        FileSound fsnd;
        memset(&fsnd, 0, sizeof(fsnd));
        fsnd.type = s.type;
        fsnd.hatOpen = s.hatOpen;
        memcpy(fsnd.name, s.name, sizeof(fsnd.name));
        good = f->writeFile(&fsnd, sizeof(fsnd), h) == (int32_t)sizeof(fsnd) &&
               f->writeFile(s.value, kParamCount, h) == (int32_t)kParamCount;
    }
    f->closeFile(h);
    if (good)
        s_dirty = false;
}

void copyProjectFile(const char* fromFolder, const char* toFolder)
{
    FileSystem* f = fs();
    if (f == nullptr || fromFolder == nullptr || toFolder == nullptr)
        return;
    char src[256], dst[256];
    snprintf(src, sizeof(src), "%s/%s", fromFolder, kFileName);
    snprintf(dst, sizeof(dst), "%s/%s", toFolder, kFileName);
    if (!f->exists(src))
    {
        // a project without PERC: none in the destination either (not the last project's)
        if (f->exists(dst))
            f->remove(dst);
        return;
    }
    cFileTransfer::copyFile(f, src, dst);
}

} // namespace perc
} // namespace gibgob
