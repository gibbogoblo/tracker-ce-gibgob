#include "gibgob/Perform.h"

#include "TrackerImpl.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include <string.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

namespace gibgob
{
namespace perform
{

namespace
{

__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t s_memory[kMemoryBudget];
Engine s_engine;
bool s_init = false;

volatile bool s_playing = false;
volatile uint32_t s_ticks = 0;

const char* const kDir = "/GibGob";
const char* const kFile = "/GibGob/perform.cfg";
constexpr uint32_t kMagic = 0x46524550; // "PERF"

struct SavedCells
{
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    Cell cells[kCells];
};

void load()
{
    Cell cells[kCells];
    factoryCells(cells);

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs != nullptr && fs->exists(kFile))
    {
        auto [ok, h] = fs->openFile(kFile, FileSystem::FileOpenMode::kRead);
        if (ok)
        {
            SavedCells saved;
            memset(&saved, 0, sizeof(saved));
            const int32_t n = fs->readFile(&saved, sizeof(saved), h);
            fs->closeFile(h);
            if (n == (int32_t)sizeof(saved) && saved.magic == kMagic && saved.version == 1)
                memcpy(cells, saved.cells, sizeof(cells));
        }
    }
    for (int i = 0; i < kCells; i++)
        s_engine.setCell(i, cells[i]); // setCell repairs anything out of range
}

} // namespace

Engine& engine()
{
    return s_engine;
}

void init()
{
    if (s_init)
        return;
    s_init = true;
    Cell cells[kCells];
    factoryCells(cells);
    for (int i = 0; i < kCells; i++)
        s_engine.setCell(i, cells[i]);
    s_engine.attach(s_memory);
}

void loadSaved()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    load();
}

void press(int cell)
{
    if (cell < 0 || cell >= kCells)
        return;
    s_engine.setHeld((uint8_t)(s_engine.heldMask() | (1u << cell)));
}

void release(int cell)
{
    if (cell < 0 || cell >= kCells)
        return;
    s_engine.setHeld((uint8_t)(s_engine.heldMask() & ~(1u << cell)));
}

void releaseAll()
{
    s_engine.releaseAll();
}

bool isHeld(int cell)
{
    return (s_engine.heldMask() >> (cell & 7)) & 1;
}

Cell getCell(int i)
{
    return s_engine.cell(i);
}

void setCell(int i, const Cell& c)
{
    s_engine.setCell(i, c);
}

void save()
{
    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr)
        return;
    if (!fs->exists(kDir))
        fs->mkdir(0, kDir);
    SavedCells saved;
    memset(&saved, 0, sizeof(saved));
    saved.magic = kMagic;
    saved.version = 1;
    for (int i = 0; i < kCells; i++)
        saved.cells[i] = s_engine.cell(i);
    auto [ok, h] = fs->openFile(kFile, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
        return;
    fs->writeFile(&saved, sizeof(saved), h);
    fs->closeFile(h);
}

void clockTick(uint32_t globalPos, uint8_t uStep)
{
    if (!s_playing)
    {
        // bars count from the step playback started on
        s_ticks = globalPos * 48u + (uStep > 0 ? uStep - 1u : 0u);
        s_playing = true;
    }
    else
    {
        s_ticks = s_ticks + 1u;
    }
}

void clockStop()
{
    s_playing = false;
}

Clock currentClock()
{
    Clock c;
    c.playing = s_playing;
    c.ticks = s_ticks;
    c.tempo = sequencer.getCurrentTempo();
    return c;
}

} // namespace perform
} // namespace gibgob
