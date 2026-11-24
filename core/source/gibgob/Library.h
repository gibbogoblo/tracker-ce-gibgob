#ifndef GIBGOB_LIBRARY_H_
#define GIBGOB_LIBRARY_H_

// GibGob OS sample library: every WAV under the library folders, with a genre
// and a category (set on the Mini, or guessed from the file and folder
// names), favourites and the recently used ones. Kept in /GibGob/library.idx
// (text). No hardware here: the screen scans the card and hands paths in.

#include <stdint.h>

namespace gibgob
{
namespace library
{

enum Genre : uint8_t
{
    kGenreNone = 0,
    kGenreHiphop,
    kGenreGarage,
    kGenreJungle,
    kGenreVocal,
    kGenreMine, // recorded on the Mini
    kGenreCount
};

enum Category : uint8_t
{
    kCatNone = 0,
    kCatKicks,
    kCatSnares,
    kCatHats,
    kCatPerc,
    kCatBass,
    kCatKeys,
    kCatVocals,
    kCatFx,
    kCatCount
};

// what the 8 top buttons pick first
enum Shelf : uint8_t
{
    kShelfAll = 0,
    kShelfHiphop,
    kShelfGarage,
    kShelfJungle,
    kShelfVocal,
    kShelfMine,
    kShelfFavourites,
    kShelfRecent,
    kShelfCount
};

extern const char* const kGenreNames[kGenreCount];
extern const char* const kCategoryNames[kCatCount];
extern const char* const kShelfNames[kShelfCount];

// the folders that are scanned (and where the Mini's recordings go)
static constexpr int kRootCount = 3;
extern const char* const kRoots[kRootCount];

enum EntryFlags : uint8_t
{
    kFlagFavourite = 0x01,
    kFlagGenreSet = 0x02, // the genre was picked on the Mini (not guessed)
    kFlagCatSet = 0x04,   // the category was picked on the Mini
    kFlagSeen = 0x08,     // found by the scan that is running
};

// loop or one-shot: the name says it ("loop", "92bpm", "one shot"), else the
// length does (3 s or longer: a loop)
enum Kind : uint8_t
{
    kKindUnknown = 0, // the length is not read yet
    kKindShot,
    kKindLoop,
    kKindCount
};
extern const char* const kKindNames[kKindCount];
// the filter on the type: everything, only one-shots, only loops
enum KindFilter : uint8_t
{
    kKindAll = 0,
    kKindOnlyShots,
    kKindOnlyLoops,
    kKindFilterCount
};
static constexpr uint16_t kLengthUnread = 0;      // tenths of a second, 0: not read yet
static constexpr uint16_t kLengthBad = 0xFFFF;    // could not be read
static constexpr uint16_t kLoopTenths = 30;       // 3 s or longer: a loop

struct Entry
{
    uint32_t pathOff; // into the name pool
    uint32_t hash;
    uint16_t recent;  // 0: never used; higher: used more recently
    uint8_t genre;
    uint8_t category;
    uint8_t flags;
    uint8_t kind;     // Kind
    uint16_t tenths;  // the length in tenths of a second (kLengthUnread / kLengthBad)
};

class Index
{
  public:
    // the memory is the caller's (the Mini: external RAM)
    void init(char* pool, uint32_t poolBytes, Entry* entries, int maxEntries);
    void clear();

    int count() const { return n; }
    const Entry& entry(int i) const { return e[i]; }
    const char* path(int i) const { return pool + e[i].pathOff; }
    const char* name(int i) const; // the file name without its folders and ".wav"

    int find(const char* path) const;
    // a file found by a scan: adds it (tags guessed) or marks it seen; -1 when full
    int add(const char* path);

    void setGenre(int i, int genre);
    void setCategory(int i, int category);
    // the same for every file in the folder of file i; returns how many
    int setGenreFolder(int i, int genre);
    int setCategoryFolder(int i, int category);
    void toggleFavourite(int i);
    // the length read from the file (tenths of a second, kLengthBad if unreadable)
    void setLength(int i, uint32_t tenths);
    // the next entry from `from` on whose length is not read yet (-1: none)
    int nextUnmeasured(int from) const;
    // the same, only among the entries on this shelf / in this category (what
    // is being looked at goes first)
    int nextUnmeasuredIn(int shelf, int category) const;
    // how many of them are still to be read
    int unmeasuredIn(int shelf, int category) const;
    void markUsed(int i);

    // a scan starts: nothing seen yet; it ends: the files not seen are gone
    void beginScan();
    void endScan();

    // the entries on a shelf, in a category (kCatNone: all), into `out`;
    // sorted by path (Recent: the latest first)
    int filter(int shelf, int category, uint16_t* out, int max, int kindFilter = kKindAll) const;

    // the index file: one line per file "genre;category;flags;recent;path"
    // (only the parts set on the Mini are kept; guesses are made again)
    // all = every file (a cache for opening fast), else only what was set on the Mini
    int save(char* out, int outLen, bool all = false) const;
    void load(const char* text, int len);

    bool dirty = false;
    bool full = false;

    static int guessCategory(const char* path);
    static int guessGenre(const char* path);
    // loop / one-shot from the name and the length (tenths; kLengthUnread: name only)
    static int guessKind(const char* path, uint16_t tenths);
    static uint32_t hashOf(const char* s);

  private:
    bool onShelf(const Entry& x, int shelf, int category) const;
    int addRaw(const char* path, uint32_t h);
    void guess(Entry& x) const;

    char* pool = nullptr;
    uint32_t poolBytes = 0, poolUsed = 0;
    Entry* e = nullptr;
    int maxEntries = 0;
    int n = 0;
    uint16_t recentCounter = 0;
};

} // namespace library
} // namespace gibgob

#endif
