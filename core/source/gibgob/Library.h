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

struct Entry
{
    uint32_t pathOff; // into the name pool
    uint32_t hash;
    uint16_t recent;  // 0: never used; higher: used more recently
    uint8_t genre;
    uint8_t category;
    uint8_t flags;
    uint8_t reserved[3];
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
    void toggleFavourite(int i);
    void markUsed(int i);

    // a scan starts: nothing seen yet; it ends: the files not seen are gone
    void beginScan();
    void endScan();

    // the entries on a shelf, in a category (kCatNone: all), into `out`;
    // sorted by path (Recent: the latest first)
    int filter(int shelf, int category, uint16_t* out, int max) const;

    // the index file: one line per file "genre;category;flags;recent;path"
    // (only the parts set on the Mini are kept; guesses are made again)
    // all = every file (a cache for opening fast), else only what was set on the Mini
    int save(char* out, int outLen, bool all = false) const;
    void load(const char* text, int len);

    bool dirty = false;
    bool full = false;

    static int guessCategory(const char* path);
    static int guessGenre(const char* path);
    static uint32_t hashOf(const char* s);

  private:
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
