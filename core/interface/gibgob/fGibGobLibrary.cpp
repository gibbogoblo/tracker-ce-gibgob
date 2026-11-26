// GibGob OS - sample library / track assign (shortcuts: gibgobHelp.h, Shift + Menu)
//
// Everything it needs lives in the free end of the sample memory while it is
// open (the index can hold thousands of files); the index is kept on the card
// in /GibGob/library.idx so it opens at once, and the folders are scanned
// again in the background each time.

#include "gibgob/gibgobLibrary.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "elapsedMillis.h"
#include "fileManager.h"
#include "gibgob/Library.h"
#include "gibgob/MelodicTrack.h"
#include "gibgob/gibgobHelp.h"
#include "gibgob/gibgobHome.h"
#include "gibgob/gibgobPages.h"
#include "gibgob/gibgobUi.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#if __has_include("cr_section_macros.h")
#include "cr_section_macros.h"
#endif

cGibGobLibrary gibgobLibrary;
static cGibGobLibrary* LB = &gibgobLibrary;

using namespace gibgob;
namespace lib = gibgob::library;

static void functPlay(uint8_t state);
static void functInsert(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functMiddle(uint8_t state);
static void functShift(uint8_t state);
static void functTopRow(uint8_t button, uint8_t state);
static void functColour(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPattern(uint8_t state);
static void functSwitchModule(uint8_t button);
static void functMenu(uint8_t state);

namespace
{
constexpr const char* kIndexFolder = "/GibGob";
constexpr const char* kIndexPath = "/GibGob/library.idx";
constexpr int kVisibleRows = 10;
constexpr int kPathMax = 160;
constexpr int kNameMax = 120;

// the small things: in the external memory (the fast memory is full)
struct Small
{
    TableView view;
    ParamBox params[4];
    char barText[8][16];
    lib::Index idx;
    // the big things: borrowed from the free end of the sample memory
    lib::Entry* entries;
    char* pool;
    uint16_t* list;
    char* io;
    uint32_t ioBytes;
    char* names;
    uint32_t namesBytes;
    char** nameList;
    int nameListMax;
    char (*stack)[kPathMax];
    int stackMax;
    int maxEntries;
    bool memoryOk;

    int shelf;
    int category;
    int kindFilter;        // Insert: all / one-shots / loops
    int measureAt;         // the background reading of lengths: where it goes on
    uint32_t nextMeasureAt;
    int measuredSinceShow;
    int listCount;
    int cursor;
    int top;
    int8_t held; // 1: Note (genre), 2: Instr (category)
    bool scanning;
    bool rootsListed;
    int stackN;
    int dirsSinceFilter;
    uint32_t previewAt; // when to play the sample under the cursor (0: no)
    bool previewing;
};
__NOINIT(EXTERNAL_RAM) alignas(8) uint8_t s_smallMemory[sizeof(Small)];
Small* S = nullptr;
elapsedMillis s_clock;

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int cursorEntry()
{
    return S->listCount > 0 ? S->list[clampInt(S->cursor, 0, S->listCount - 1)] : -1;
}

FileSystem* fileSystem()
{
    return TrackerImpl::getInstance()->getPlatform()->getFileSystem();
}

// the free end of the sample memory, split up; false when there is too little
bool borrowMemory()
{
    const uint32_t margin = 64 * 1024;
    const uint32_t used = mtProject.used_memory + margin;
    // up to the sample editor's buffer at the very end (left alone)
    if (used + 4096 >= EDIT_BUFF_OFFSET)
        return false;
    uint32_t avail = EDIT_BUFF_OFFSET - used;
    uint8_t* base = reinterpret_cast<uint8_t*>(externalRamSampleMemory) + used;
    base = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(base) + 7u) & ~(uintptr_t)7u);
    avail -= 8;
    // a full size library, or half of it when the samples leave less room
    for (int scale = 1; scale <= 4; scale *= 2)
    {
        const int entries = 6000 / scale;
        // io holds the whole index file: every path (at most the pool) plus
        // "g;c;f;recent;len;" and a line end (at most 19) for each file
        const uint32_t pool = 393216u / scale, io = pool + entries * 22u + 64u, names = 32768u;
        const int nameListMax = 1024, stackMax = 256;
        const uint32_t need = entries * sizeof(lib::Entry) + pool + entries * sizeof(uint16_t) + io + names +
                              nameListMax * sizeof(char*) + stackMax * kPathMax + 64;
        if (need > avail)
            continue;
        uint8_t* p = base;
        auto take = [&p](uint32_t bytes)
        {
            uint8_t* r = p;
            p += (bytes + 7u) & ~7u;
            return r;
        };
        S->entries = reinterpret_cast<lib::Entry*>(take(entries * sizeof(lib::Entry)));
        S->pool = reinterpret_cast<char*>(take(pool));
        S->list = reinterpret_cast<uint16_t*>(take(entries * sizeof(uint16_t)));
        S->io = reinterpret_cast<char*>(take(io));
        S->ioBytes = io;
        S->names = reinterpret_cast<char*>(take(names));
        S->namesBytes = names;
        S->nameList = reinterpret_cast<char**>(take(nameListMax * sizeof(char*)));
        S->nameListMax = nameListMax;
        S->stack = reinterpret_cast<char (*)[kPathMax]>(take(stackMax * kPathMax));
        S->stackMax = stackMax;
        S->maxEntries = entries;
        S->idx.init(S->pool, pool, S->entries, entries);
        return true;
    }
    return false;
}

void loadIndex()
{
    FileSystem* fs = fileSystem();
    if (fs == nullptr || !fs->exists(kIndexPath))
        return;
    auto [ok, fh] = fs->openFile(kIndexPath, FileSystem::kRead);
    if (!ok || fh == nullptr)
        return;
    const int32_t got = fs->readFile(S->io, S->ioBytes - 1, fh);
    fs->closeFile(fh);
    if (got > 0)
        S->idx.load(S->io, got);
}

void saveIndex()
{
    if (!S->idx.dirty)
        return;
    FileSystem* fs = fileSystem();
    if (fs == nullptr || !fs->isSdCardInserted())
        return;
    if (!fs->exists(kIndexFolder))
        fs->mkdir(0, kIndexFolder);
    const int len = S->idx.save(S->io, (int)S->ioBytes, true);
    fs->remove(kIndexPath);
    auto [ok, fh] = fs->openFile(kIndexPath, FileSystem::kWriteCreateNew);
    if (!ok || fh == nullptr)
        return;
    const bool good = fs->writeFile(S->io, (size_t)len, fh) == len;
    fs->closeFile(fh);
    if (good)
        S->idx.dirty = false;
}

bool isWav(const char* name)
{
    const size_t l = strlen(name);
    return l > 4 && strcasecmp(name + l - 4, ".wav") == 0 && name[0] != '.';
}

bool isRoot(const char* folder) // without the leading '/'
{
    return strcasecmp(folder, "Instruments") == 0 || strcasecmp(folder, "Samples") == 0 ||
           strncasecmp(folder, "Recordings", 10) == 0;
}

// the card's lister writes one byte more than it counts for each folder name:
// it is given less than the buffer, so it can never run past it
int namesRoom()
{
    return (int)S->namesBytes - S->nameListMax - 16;
}

void push(const char* path)
{
    if (S->stackN < S->stackMax && strlen(path) < (size_t)kPathMax)
        snprintf(S->stack[S->stackN++], kPathMax, "%s", path);
}

// one step of the scan: the library folders at first, then one folder a step
void scanStep()
{
    FileSystem* fs = fileSystem();
    if (fs == nullptr || !fs->isSdCardInserted())
    {
        S->scanning = false;
        return;
    }
    int count = S->nameListMax;
    if (!S->rootsListed)
    {
        S->rootsListed = true;
        fs->listDirContents("/", FileSystem::kFolders, nullptr, S->names, namesRoom(), kNameMax, count,
                            S->nameList);
        for (int i = 0; i < count; i++)
            if (isRoot(S->nameList[i] + 1))
                push(S->nameList[i]); // "/Instruments"
        return;
    }
    if (S->stackN == 0)
    {
        // the files that are gone are dropped and the rest move up: stay on
        // the same sample (by its path)
        char keep[kPathMax] = { 0 };
        const int was = cursorEntry();
        if (was >= 0)
            snprintf(keep, sizeof(keep), "%s", S->idx.path(was));
        S->idx.endScan();
        S->scanning = false;
        S->listCount = 0;
        LB->refilter();
        const int now = keep[0] ? S->idx.find(keep) : -1;
        for (int k = 0; now >= 0 && k < S->listCount; k++)
            if (S->list[k] == now)
            {
                S->cursor = k;
                if (S->cursor < S->top || S->cursor >= S->top + kVisibleRows)
                    S->top = S->cursor > kVisibleRows / 2 ? S->cursor - kVisibleRows / 2 : 0;
                break;
            }
        saveIndex();
        return;
    }
    char dir[kPathMax];
    snprintf(dir, sizeof(dir), "%s", S->stack[--S->stackN]);
    char path[kPathMax + kNameMax];
    // its folders (names come with a leading '/')
    count = S->nameListMax;
    fs->listDirContents(dir, FileSystem::kFolders, nullptr, S->names, namesRoom(), kNameMax, count,
                        S->nameList);
    for (int i = count - 1; i >= 0; i--) // backwards: the stack gives them in order
    {
        snprintf(path, sizeof(path), "%s%s", dir, S->nameList[i]);
        push(path);
    }
    // its WAV files
    count = S->nameListMax;
    fs->listDirContents(dir, FileSystem::kFiles, [](const char* n) { return isWav(n); }, S->names,
                        namesRoom(), kNameMax, count, S->nameList);
    for (int i = 0; i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, S->nameList[i]);
        if (strlen(path) < (size_t)kPathMax)
            S->idx.add(path);
    }
    if (++S->dirsSinceFilter >= 8)
    {
        S->dirsSinceFilter = 0;
        LB->refilter();
    }
}

constexpr float kPreviewTrim = 0.355f; // -9 dB

uint32_t le32(const uint8_t* b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// a WAV's length from its header, in tenths of a second (kLengthBad if it can't be read)
uint32_t wavTenths(const char* path)
{
    FileSystem* fs = fileSystem();
    if (fs == nullptr)
        return lib::kLengthBad;
    auto [ok, fh] = fs->openFile(path, FileSystem::kRead);
    if (!ok || fh == nullptr)
        return lib::kLengthBad;
    uint8_t h[24];
    uint32_t result = lib::kLengthBad;
    const uint32_t fileSize = (uint32_t)fs->getFileSize(fh);
    auto tenthsOf = [](uint64_t bytes, uint32_t rate)
    {
        const uint64_t t = bytes * 10u / rate;
        return t >= lib::kLengthBad ? (uint32_t)lib::kLengthBad - 1 : (uint32_t)t;
    };
    if (fs->readFile(h, 12, fh) == 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0)
    {
        uint32_t pos = 12, byteRate = 0;
        for (int k = 0; k < 24; k++) // the chunks, up to "data"
        {
            if (!fs->seekFile(pos, fh) || fs->readFile(h, 8, fh) != 8)
                break;
            const uint32_t size = le32(h + 4);
            if (memcmp(h, "fmt ", 4) == 0)
            {
                if (fs->readFile(h, 16, fh) != 16)
                    break;
                byteRate = le32(h + 8);
            }
            else if (memcmp(h, "data", 4) == 0)
            {
                if (byteRate > 0)
                {
                    // a size of 0 or past the end (a recording cut short): what the file holds
                    uint32_t bytes = size;
                    if (fileSize > pos + 8 && (bytes == 0 || bytes > fileSize - pos - 8))
                        bytes = fileSize - pos - 8;
                    result = tenthsOf(bytes, byteRate);
                }
                break;
            }
            if (size > 0x7FFFFFFFu)
                break;
            pos += 8 + size + (size & 1u);
        }
        // no data chunk found: the rest of the file at the format's rate
        if (result == lib::kLengthBad && byteRate > 0 && fileSize > pos)
            result = tenthsOf(fileSize - 44u, byteRate);
    }
    // not a WAV header we understand: guess 16-bit stereo 44.1 kHz from the size
    if (result == lib::kLengthBad && fileSize > 44u)
        result = tenthsOf(fileSize - 44u, 176400u);
    fs->closeFile(fh);
    return result;
}

// lengths in the background, what is on screen first: a few milliseconds'
// worth each time (one file at a time while a preview streams from the card)
void measureStep()
{
    if ((uint32_t)s_clock < S->nextMeasureAt)
        return;
    const bool busy = S->previewing;
    S->nextMeasureAt = (uint32_t)s_clock + (busy ? 60u : 2u);
    const uint32_t until = (uint32_t)s_clock + 6u;
    for (int k = 0; k < (busy ? 1 : 24); k++)
    {
        int i = S->idx.nextUnmeasuredIn(S->shelf, S->category);
        if (i < 0)
            i = S->idx.nextUnmeasured(S->measureAt);
        if (i < 0)
            return;
        S->idx.setLength(i, wavTenths(S->idx.path(i)));
        S->measureAt = i + 1;
        S->measuredSinceShow++;
        if ((int32_t)((uint32_t)s_clock - until) >= 0)
            break;
    }
}

void stopPreview()
{
    S->previewAt = 0;
    if (S->previewing)
    {
        if (auto* sd = engine.getSdPlayer())
            sd->stop();
        S->previewing = false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
void cGibGobLibrary::initDisplayControls()
{
    if (S == nullptr)
    {
        S = new (s_smallMemory) Small();
        memset(S->barText, 0, sizeof(S->barText));
        S->memoryOk = false;
    }
    strControlProperties prop;
    prop.x = 0;
    prop.y = kScreenTop;
    prop.w = kScreenW;
    prop.h = kScreenH;
    prop.style = controlStyleShow;
    prop.data = &S->view;
    table = display.createControl<TableControl>(&prop);
}

void cGibGobLibrary::start(uint32_t options)
{
    moduleRefresh = 1;
    screenOpened();
    track = (int)(options & 7);
    origin = (int)((options >> 4) & 0xF);
    TableView& v = S->view;
    v.help.active = false;
    v.help.title = "Sample library - shortcuts";
    v.help.lines = kHelpLibrary;
    v.help.count = (int)(sizeof(kHelpLibrary) / sizeof(kHelpLibrary[0]));
    v.params = S->params;

    S->held = 0;
    S->previewAt = 0;
    S->previewing = false;
    // previews straight from the card skip the tracks' mix: about 9 dB down
    // so they sit with what's playing
    engine.setPreviewTrim(kPreviewTrim);
    S->memoryOk = borrowMemory();
    S->listCount = 0;
    if (S->memoryOk)
    {
        S->shelf = clampInt(S->shelf, 0, lib::kShelfCount - 1);
        S->category = clampInt(S->category, 0, lib::kCatCount - 1);
        S->kindFilter = clampInt(S->kindFilter, 0, lib::kKindFilterCount - 1);
        S->measureAt = 0;
        S->nextMeasureAt = 0;
        S->measuredSinceShow = 0;
        loadIndex(); // what was there last time, at once
        S->idx.beginScan();
        S->scanning = true;
        S->rootsListed = false;
        S->stackN = 0;
        S->dirsSinceFilter = 0;
        refilter();
    }

    songTimer.hide();
    snprintf(titleText, sizeof(titleText), "Sample library");
    titleBar->show();
    titleBar->setTitle(titleText);

    setDefaultScreenFunct();
    refresh();
    display.synchronizeRefresh();
}

void cGibGobLibrary::stop()
{
    engine.setPreviewTrim(1.0f); // the other browsers keep their own level
    if (S->memoryOk)
    {
        stopPreview();
        // a scan cut short keeps what it had (nothing is thrown away)
        if (S->scanning)
        {
            for (int i = 0; i < S->idx.count(); i++)
                const_cast<lib::Entry&>(S->idx.entry(i)).flags |= lib::kFlagSeen;
            S->scanning = false;
        }
        saveIndex();
    }
    S->memoryOk = false; // the sample memory is the samples' again
    moduleRefresh = 0;
}

void cGibGobLibrary::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();
    ip->setButtonObj(interfaceButtonPlay, functPlay);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);
    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiddle);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functTopRow);
    ip->setButtonObj(interfaceButtonNote, functColour);
    ip->setButtonObj(interfaceButtonInstr, functColour);
    ip->setButtonObj(physFx1Button(), functColour);
    ip->setButtonObj(physFx2Button(), functColour);
    bindModuleSwitchButtons(ip, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonInsert, functInsert); // all / one-shots / loops
    ip->setPotObj(interfacePot0, functEncoder, false);
}

void cGibGobLibrary::update()
{
    if (!S->memoryOk)
        return;
    if (S->scanning)
    {
        const bool was = S->scanning;
        scanStep();
        if (was && !S->scanning)
            refresh(); // the scan is done
        else if (S->dirsSinceFilter == 0)
            refresh();
    }
    measureStep();
    if (S->measuredSinceShow >= 24 || (S->measuredSinceShow > 0 && S->idx.nextUnmeasuredIn(S->shelf, S->category) < 0))
    {
        // new lengths: the Type column, and the list if it is filtered by type
        S->measuredSinceShow = 0;
        if (S->kindFilter != lib::kKindAll)
            refilter();
        refresh();
    }
    if (S->previewAt != 0 && (uint32_t)s_clock >= S->previewAt)
    {
        S->previewAt = 0;
        const int i = cursorEntry();
        if (i >= 0)
            if (auto* sd = engine.getSdPlayer())
            {
                sd->stop();
                sd->play(S->idx.path(i), 0, MAX_16BIT);
                S->previewing = true;
            }
    }
}

void cGibGobLibrary::refilter()
{
    const int keep = cursorEntry();
    S->listCount = S->idx.filter(S->shelf, S->category, S->list, S->maxEntries, S->kindFilter);
    // stay on the same sample when it is still there
    if (keep >= 0)
        for (int k = 0; k < S->listCount; k++)
            if (S->list[k] == keep)
            {
                S->cursor = k;
                break;
            }
    S->cursor = clampInt(S->cursor, 0, S->listCount > 0 ? S->listCount - 1 : 0);
    S->top = clampInt(S->top, 0, S->listCount > kVisibleRows ? S->listCount - kVisibleRows : 0);
    if (S->cursor < S->top)
        S->top = S->cursor;
    if (S->cursor >= S->top + kVisibleRows)
        S->top = S->cursor - kVisibleRows + 1;
}

void cGibGobLibrary::refresh()
{
    TableView& v = S->view;
    v.shift = false; // Shift only swaps the top row here (no red frame)
    v.keyboard = false;
    v.canopy = nullptr;
    v.cols = 4;
    v.colX[0] = 24;
    v.colX[1] = 400;
    v.colX[2] = 520;
    v.colX[3] = 650;
    v.header[0] = "Sample";
    v.header[1] = "Genre";
    v.header[2] = "Category";
    v.header[3] = "Type";
    v.mode[0] = 0;
    const bool genres = !shiftActive();

    if (!S->memoryOk)
    {
        v.rows = 1;
        v.cursorRow = 0;
        v.cursorCol = 0;
        snprintf(v.cell[0][0], 20, "Sample memory full");
        for (int c = 1; c < 4; c++)
            v.cell[0][c][0] = 0;
        snprintf(v.title, sizeof(v.title), "Track %d - no room for the library", track + 1);
        for (int i = 0; i < 8; i++)
        {
            v.bar.text[i] = nullptr;
            v.bar.highlighted[i] = false;
        }
        v.params = nullptr;
        display.refreshControl(table);
        return;
    }
    v.params = S->params;

    // the list
    if (S->listCount == 0)
    {
        v.rows = 1;
        v.cursorRow = 0;
        v.cursorCol = 0;
        const int unread = S->kindFilter != lib::kKindAll ? S->idx.unmeasuredIn(S->shelf, S->category) : 0;
        if (unread > 0)
            snprintf(v.cell[0][0], 20, "Reading %d...", unread); // the lengths, to sort loops / shots
        else
            snprintf(v.cell[0][0], 20, "%s", S->scanning ? "Looking..." : "Nothing here yet");
        for (int c = 1; c < 4; c++)
            v.cell[0][c][0] = 0;
    }
    else
    {
        const int rows = S->listCount - S->top < kVisibleRows ? S->listCount - S->top : kVisibleRows;
        v.rows = rows;
        v.cursorRow = S->cursor - S->top;
        v.cursorCol = 0;
        for (int r = 0; r < rows; r++)
        {
            const int i = S->list[S->top + r];
            const lib::Entry& e = S->idx.entry(i);
            snprintf(v.cell[r][0], 20, "%.19s", S->idx.name(i));
            snprintf(v.cell[r][1], 20, "%s", lib::kGenreNames[e.genre % lib::kGenreCount]);
            snprintf(v.cell[r][2], 20, "%s", lib::kCategoryNames[e.category % lib::kCatCount]);
            // the type and the length it was read from ("Shot 0.4s", "Loop 8s")
            char len[8] = "";
            if (e.tenths != lib::kLengthUnread && e.tenths != lib::kLengthBad)
            {
                if (e.tenths < 100)
                    snprintf(len, sizeof(len), " %d.%ds", e.tenths / 10, e.tenths % 10);
                else
                    snprintf(len, sizeof(len), " %ds", e.tenths / 10);
            }
            snprintf(v.cell[r][3], 20, "%s%s%s", lib::kKindNames[e.kind % lib::kKindCount], len,
                     (e.flags & lib::kFlagFavourite) ? " *" : "");
            v.rowColor[r] = color::kBox;
        }
    }

    // where we are
    char where[48];
    static const char* const kKindFilterNames[lib::kKindFilterCount] = { "", " > Shots", " > Loops" };
    if (S->category != lib::kCatNone)
        snprintf(where, sizeof(where), "%s > %s%s", lib::kShelfNames[S->shelf], lib::kCategoryNames[S->category],
                 kKindFilterNames[S->kindFilter % lib::kKindFilterCount]);
    else
        snprintf(where, sizeof(where), "%s%s", lib::kShelfNames[S->shelf],
                 kKindFilterNames[S->kindFilter % lib::kKindFilterCount]);
    snprintf(v.title, sizeof(v.title), "Track %d  %s  (%d)", track + 1, where, S->listCount);
    if (S->scanning)
    {
        snprintf(v.mode, sizeof(v.mode), "SCAN");
        v.modeColor = color::kBox;
    }
    else if (S->idx.full)
    {
        snprintf(v.mode, sizeof(v.mode), "FULL");
        v.modeColor = color::kRec;
    }

    // the coloured boxes: the sample under the cursor
    const int i = cursorEntry();
    const lib::Entry* e = i >= 0 ? &S->idx.entry(i) : nullptr;
    static const char* const kLabels[4] = { "Genre", "Categ", "Fav", "Folders" };
    static const uint32_t kColors[4] = { color::kNoteBtn, color::kInstrBtn, color::kFx1Btn, color::kFx2Btn };
    for (int k = 0; k < 4; k++)
    {
        snprintf(S->params[k].label, sizeof(S->params[k].label), "%s", kLabels[k]);
        S->params[k].value[0] = 0;
        S->params[k].color = kColors[k];
    }
    if (e != nullptr)
    {
        snprintf(S->params[0].value, sizeof(S->params[0].value), "%.9s", lib::kGenreNames[e->genre % lib::kGenreCount]);
        snprintf(S->params[1].value, sizeof(S->params[1].value), "%.9s", lib::kCategoryNames[e->category % lib::kCatCount]);
        snprintf(S->params[2].value, sizeof(S->params[2].value), "%s", (e->flags & lib::kFlagFavourite) ? "yes" : "-");
    }
    v.selectedParam = (int8_t)(S->held == 1 ? 0 : (S->held == 2 ? 1 : -1));

    // the top row: genres (and Favourites / Recent), or the categories
    for (int b = 0; b < 8; b++)
    {
        if (genres)
        {
            snprintf(S->barText[b], sizeof(S->barText[b]), "%s", b == lib::kShelfFavourites ? "Favs" : lib::kShelfNames[b]);
            v.bar.fill[b] = color::kMelodic;
            v.bar.highlighted[b] = (b == S->shelf);
        }
        else
        {
            snprintf(S->barText[b], sizeof(S->barText[b]), "%s", lib::kCategoryNames[b + 1]);
            v.bar.fill[b] = color::kDrum;
            v.bar.highlighted[b] = (b + 1 == S->category);
        }
        v.bar.text[b] = S->barText[b];
    }
    display.refreshControl(table);
}

void cGibGobLibrary::move(int delta)
{
    if (!S->memoryOk || S->listCount == 0)
        return;
    S->cursor = clampInt(S->cursor + delta, 0, S->listCount - 1);
    if (S->cursor < S->top)
        S->top = S->cursor;
    if (S->cursor >= S->top + kVisibleRows)
        S->top = S->cursor - kVisibleRows + 1;
    stopPreview();
    S->previewAt = (uint32_t)s_clock + 180; // plays once the cursor rests a moment
    refresh();
}

void cGibGobLibrary::topButton(int b)
{
    if (!S->memoryOk)
        return;
    if (!shiftActive())
    {
        // a genre: then the categories (Shift brings the genres back)
        S->shelf = clampInt(b, 0, lib::kShelfCount - 1);
        S->category = lib::kCatNone;
        g_shiftLatched = true;
    }
    else
    {
        const int cat = b + 1;
        S->category = (S->category == cat) ? lib::kCatNone : cat; // again: all of them
    }
    S->cursor = 0;
    S->top = 0;
    stopPreview();
    refilter();
    refresh();
}

// held Note / Instr + small +/-: this sample's genre / category;
// + big +/-: every sample in its folder takes the next / previous one
void cGibGobLibrary::changeTag(int delta, bool folder)
{
    const int i = cursorEntry();
    if (i < 0)
        return;
    const lib::Entry& e = S->idx.entry(i);
    char msg[48];
    if (S->held == 1)
    {
        const int g = ((int)e.genre + delta + lib::kGenreCount) % lib::kGenreCount;
        if (folder)
        {
            snprintf(msg, sizeof(msg), "Folder: %d samples now %s", S->idx.setGenreFolder(i, g), lib::kGenreNames[g]);
            mtPopups.show(0, msg, 1);
        }
        else
            S->idx.setGenre(i, g);
    }
    else if (S->held == 2)
    {
        const int c = ((int)e.category + delta + lib::kCatCount) % lib::kCatCount;
        if (folder)
        {
            snprintf(msg, sizeof(msg), "Folder: %d samples now %s", S->idx.setCategoryFolder(i, c),
                     lib::kCategoryNames[c]);
            mtPopups.show(0, msg, 1);
        }
        else
            S->idx.setCategory(i, c);
    }
    refresh(); // the list is filtered again when the button is let go
}

void cGibGobLibrary::assign()
{
    const int i = cursorEntry();
    if (i < 0)
        return;
    stopPreview();
    S->idx.markUsed(i);
    cGibGobPages::queueImport(S->idx.path(i));
    back(false);
}

void cGibGobLibrary::back(bool folders)
{
    if (folders)
        cGibGobPages::queueFolderBrowser();
    const uint32_t options = cGibGobPages::makeOptions(cGibGobPages::kPageTrack, (cGibGobPages::Origin)origin, track);
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&gibgobPages, options);
}

// ---------------------------------------------------------------------------
static bool pressOrRepeat(uint8_t s)
{
    return s == buttonPress || s == buttonHold;
}

// Insert: all / only one-shots / only loops
static void functInsert(uint8_t state)
{
    if (state != buttonPress || !S->memoryOk)
        return;
    stopPreview();
    S->kindFilter = (S->kindFilter + 1) % lib::kKindFilterCount;
    S->top = 0;
    LB->refilter();
    LB->refresh();
}

static void functPlay(uint8_t state)
{
    if (state != buttonPress)
        return;
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP && !sequencer.isCountingIn())
        playFromStop();
    else
    {
        sequencer.stop();
        setRecArmed(false);
    }
}

static void functUp(uint8_t s)
{
    if (pressOrRepeat(s))
        LB->move(-1);
}
static void functDown(uint8_t s)
{
    if (pressOrRepeat(s))
        LB->move(1);
}
static void functLeft(uint8_t s)
{
    if (pressOrRepeat(s))
        LB->move(-kVisibleRows);
}
static void functRight(uint8_t s)
{
    if (pressOrRepeat(s))
        LB->move(kVisibleRows);
}

static void functMiddle(uint8_t state)
{
    if (middleBlocked(state))
        return;
    if (state == buttonPress && !S->view.help.active)
        LB->assign();
}

static void functShift(uint8_t state)
{
    if (shiftButton(state))
        LB->refresh();
}

static void functTopRow(uint8_t button, uint8_t state)
{
    const int idx = button - interfaceButton0;
    if (state == buttonPress && idx >= 0 && idx < 8)
        LB->topButton(idx);
}

static void functColour(uint8_t button, uint8_t state)
{
    if (!S->memoryOk || S->view.help.active)
        return;
    if (button == interfaceButtonNote || button == interfaceButtonInstr)
    {
        if (state == buttonPress)
            S->held = button == interfaceButtonNote ? 1 : 2;
        else if (state == buttonRelease)
        {
            S->held = 0;
            LB->refilter(); // a retagged sample may leave this shelf now
        }
        LB->refresh();
        return;
    }
    if (state != buttonPress)
        return;
    if (button == physFx1Button())
    {
        const int i = cursorEntry();
        if (i >= 0)
            S->idx.toggleFavourite(i);
        LB->refilter();
        LB->refresh();
    }
    else if (button == physFx2Button())
    {
        stopPreview();
        LB->back(true);
    }
}

static void functEncoder(int16_t value)
{
    const bool big = value >= 10 || value <= -10;
    const int dir = value > 0 ? 1 : -1;
    if (S->held != 0)
        LB->changeTag(dir, big); // big +/-: the whole folder
    else
        LB->move(dir * (big ? kVisibleRows : 1));
}

static void functPattern(uint8_t state)
{
    if (state == buttonPress)
        LB->back(false);
}

static void functSwitchModule(uint8_t button)
{
    LB->eventFunct(eventSwitchModule, LB, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress && !mtMenuPopup.isPopupVisible() && (shiftActive() || S->view.help.active))
    {
        toggleHelp(S->view.help, LB->ip);
        LB->refresh();
        return;
    }
    if (S->view.help.active)
        return;
    if (state == buttonPress)
        mtMenuPopup.showModulePopup(LB);
    else if (state == buttonRelease)
        mtMenuPopup.hideModulePopup(LB);
}
