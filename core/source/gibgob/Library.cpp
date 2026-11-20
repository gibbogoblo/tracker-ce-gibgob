#include "gibgob/Library.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gibgob
{
namespace library
{

const char* const kGenreNames[kGenreCount] = { "-", "Hiphop", "Garage", "Jungle", "Vocal", "Mine" };
const char* const kCategoryNames[kCatCount] = { "-", "Kicks", "Snares", "Hats", "Perc",
                                                "Bass", "Keys", "Vocals", "FX" };
const char* const kShelfNames[kShelfCount] = { "All",  "Hiphop", "Garage",     "Jungle",
                                               "Vocal", "Mine",  "Favourites", "Recent" };
const char* const kRoots[kRootCount] = { "/Instruments", "/Samples", "/Recordings" };

namespace
{
// does `hay` contain `word` (no case), with no letter right before it
bool hasWord(const char* hay, const char* word)
{
    const size_t wl = strlen(word);
    for (const char* p = hay; *p; ++p)
    {
        if (p != hay && isalpha((unsigned char)p[-1]))
            continue;
        size_t k = 0;
        while (k < wl && p[k] && tolower((unsigned char)p[k]) == word[k])
            ++k;
        // short words ("ch", "sd", "808") only as a whole word: "chord" is no hat
        if (k == wl)
        {
            const unsigned char next = (unsigned char)p[wl];
            const bool plural = tolower(next) == 's' && !isalpha((unsigned char)p[wl + 1]); // "808s"
            if (wl > 3 || !isalpha(next) || plural)
                return true;
        }
    }
    return false;
}

bool anyWord(const char* hay, const char* const* words)
{
    for (int i = 0; words[i]; ++i)
        if (hasWord(hay, words[i]))
            return true;
    return false;
}

// "/Recordings", "/Recordings 2" ...: the Mini's own recordings
bool isRecording(const char* path)
{
    return strncmp(path, "/Recordings", 11) == 0 || strstr(path, "/REC_") != nullptr;
}
} // namespace

uint32_t Index::hashOf(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s)
        h = (h ^ (uint8_t)tolower((unsigned char)*s)) * 16777619u;
    return h;
}

int Index::guessCategory(const char* path)
{
    // order matters: "snare" before "hat" etc.; the file name counts most
    static const char* const kKick[] = { "kick", "kik", "bd", "bassdrum", nullptr };
    static const char* const kSnare[] = { "snare", "snr", "sd", "clap", "rim", nullptr };
    static const char* const kHat[] = { "hat", "hh", "hihat", "cymbal", "ride", "crash", "oh", "ch", nullptr };
    static const char* const kPerc[] = { "perc", "tom", "shaker", "conga", "bongo", "clave", "cowbell",
                                         "tamb", "block", nullptr };
    static const char* const kBass[] = { "bass", "808", "sub", "reese", nullptr };
    static const char* const kKeys[] = { "key", "keys", "piano", "rhodes", "chord", "pad", "synth", "organ",
                                         "stab", "lead", "pluck", nullptr };
    static const char* const kVox[] = { "vox", "vocal", "voice", "acapella", "adlib", "chant", nullptr };
    static const char* const kFx[] = { "fx", "sfx", "riser", "sweep", "noise", "impact", "loop", "foley", nullptr };
    struct Rule
    {
        const char* const* words;
        int cat;
    };
    static const Rule kRules[] = { { kKick, kCatKicks }, { kSnare, kCatSnares }, { kHat, kCatHats },
                                   { kPerc, kCatPerc },   { kBass, kCatBass },     { kVox, kCatVocals },
                                   { kKeys, kCatKeys },   { kFx, kCatFx } };
    // the file name first, then the whole path (its folders)
    const char* slash = strrchr(path, '/');
    const char* file = slash ? slash + 1 : path;
    for (const Rule& r : kRules)
        if (anyWord(file, r.words))
            return r.cat;
    for (const Rule& r : kRules)
        if (anyWord(path, r.words))
            return r.cat;
    return kCatNone;
}

int Index::guessGenre(const char* path)
{
    if (isRecording(path))
        return kGenreMine;
    static const char* const kHip[] = { "hiphop", "hip hop", "hip-hop", "boombap", "boom bap", "trap", "lofi", nullptr };
    static const char* const kGar[] = { "garage", "ukg", "2step", "2-step", "speed garage", nullptr };
    static const char* const kJun[] = { "jungle", "dnb", "drum and bass", "drum&bass", "breakbeat", "amen", nullptr };
    static const char* const kVoc[] = { "vocal", "vocals", "acapella", nullptr };
    if (anyWord(path, kHip))
        return kGenreHiphop;
    if (anyWord(path, kGar))
        return kGenreGarage;
    if (anyWord(path, kJun))
        return kGenreJungle;
    if (anyWord(path, kVoc))
        return kGenreVocal;
    return kGenreNone;
}

void Index::init(char* p, uint32_t bytes, Entry* entries, int max)
{
    pool = p;
    poolBytes = bytes;
    e = entries;
    maxEntries = max;
    clear();
}

void Index::clear()
{
    n = 0;
    poolUsed = 0;
    recentCounter = 0;
    full = false;
    dirty = false;
}

const char* Index::name(int i) const
{
    static char out[64];
    const char* p = path(i);
    const char* slash = strrchr(p, '/');
    snprintf(out, sizeof(out), "%s", slash ? slash + 1 : p);
    const size_t l = strlen(out);
    if (l > 4 && (strcmp(out + l - 4, ".wav") == 0 || strcmp(out + l - 4, ".WAV") == 0))
        out[l - 4] = 0;
    return out;
}

int Index::find(const char* p) const
{
    const uint32_t h = hashOf(p);
    for (int i = 0; i < n; ++i)
        if (e[i].hash == h && strcasecmp(pool + e[i].pathOff, p) == 0)
            return i;
    return -1;
}

void Index::guess(Entry& x) const
{
    const char* p = pool + x.pathOff;
    if (!(x.flags & kFlagGenreSet))
        x.genre = (uint8_t)guessGenre(p);
    if (!(x.flags & kFlagCatSet))
        x.category = (uint8_t)guessCategory(p);
}

int Index::addRaw(const char* p, uint32_t h)
{
    const uint32_t len = (uint32_t)strlen(p) + 1;
    if (n >= maxEntries || poolUsed + len > poolBytes)
    {
        full = true;
        return -1;
    }
    memcpy(pool + poolUsed, p, len);
    Entry& x = e[n];
    memset(&x, 0, sizeof(x));
    x.pathOff = poolUsed;
    x.hash = h;
    poolUsed += len;
    guess(x);
    return n++;
}

int Index::add(const char* p)
{
    int i = find(p);
    if (i < 0)
    {
        i = addRaw(p, hashOf(p));
        if (i < 0)
            return -1;
        dirty = true;
    }
    e[i].flags |= kFlagSeen;
    return i;
}

void Index::setGenre(int i, int genre)
{
    if (i < 0 || i >= n)
        return;
    e[i].genre = (uint8_t)(genre % kGenreCount);
    e[i].flags |= kFlagGenreSet;
    dirty = true;
}

void Index::setCategory(int i, int category)
{
    if (i < 0 || i >= n)
        return;
    e[i].category = (uint8_t)(category % kCatCount);
    e[i].flags |= kFlagCatSet;
    dirty = true;
}

void Index::toggleFavourite(int i)
{
    if (i < 0 || i >= n)
        return;
    e[i].flags ^= kFlagFavourite;
    dirty = true;
}

void Index::markUsed(int i)
{
    if (i < 0 || i >= n)
        return;
    if (recentCounter == 0xFFFF)
    {
        // start the count again, keeping the order
        for (int k = 0; k < n; ++k)
            e[k].recent = e[k].recent > 0x8000 ? (uint16_t)(e[k].recent - 0x8000) : 0;
        recentCounter = 0x7FFF;
    }
    e[i].recent = ++recentCounter;
    dirty = true;
}

void Index::beginScan()
{
    for (int i = 0; i < n; ++i)
        e[i].flags &= (uint8_t)~kFlagSeen;
}

void Index::endScan()
{
    // keep the files found, packed again (their names too)
    uint32_t used = 0;
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!(e[i].flags & kFlagSeen))
        {
            dirty = true;
            continue;
        }
        const char* src = pool + e[i].pathOff;
        const uint32_t len = (uint32_t)strlen(src) + 1;
        memmove(pool + used, src, len); // never overlaps forwards: used <= pathOff
        Entry x = e[i];
        x.pathOff = used;
        e[k++] = x;
        used += len;
    }
    n = k;
    poolUsed = used;
    full = false;
}

int Index::filter(int shelf, int category, uint16_t* out, int max) const
{
    int c = 0;
    for (int i = 0; i < n && c < max; ++i)
    {
        const Entry& x = e[i];
        bool ok;
        switch (shelf)
        {
            case kShelfAll: ok = true; break;
            case kShelfFavourites: ok = (x.flags & kFlagFavourite) != 0; break;
            case kShelfRecent: ok = x.recent != 0; break;
            default: ok = x.genre == (uint8_t)shelf; break; // Hiphop..Mine match their genre number
        }
        if (ok && category != kCatNone)
            ok = x.category == (uint8_t)category;
        if (ok)
            out[c++] = (uint16_t)i;
    }
    // sort: by path, or the latest used first (insertion sort keeps it small and allocation free)
    for (int a = 1; a < c; ++a)
    {
        const uint16_t v = out[a];
        int b = a - 1;
        while (b >= 0)
        {
            bool after;
            if (shelf == kShelfRecent)
                after = e[out[b]].recent < e[v].recent;
            else
                after = strcasecmp(pool + e[out[b]].pathOff, pool + e[v].pathOff) > 0;
            if (!after)
                break;
            out[b + 1] = out[b];
            --b;
        }
        out[b + 1] = v;
    }
    return c;
}

int Index::save(char* out, int outLen, bool all) const
{
    int w = 0;
    w += snprintf(out + w, (size_t)(outLen - w), "GIBGOBLIB 1\n");
    for (int i = 0; i < n && w < outLen; ++i)
    {
        const Entry& x = e[i];
        const uint8_t keep = x.flags & (kFlagFavourite | kFlagGenreSet | kFlagCatSet);
        if (!all && keep == 0 && x.recent == 0)
            continue; // nothing set on the Mini: found again by the next scan
        const int r = snprintf(out + w, (size_t)(outLen - w), "%d;%d;%d;%d;%s\n",
                               (x.flags & kFlagGenreSet) ? x.genre : 0, (x.flags & kFlagCatSet) ? x.category : 0,
                               keep, x.recent, pool + x.pathOff);
        if (r < 0 || w + r >= outLen)
            break;
        w += r;
    }
    return w;
}

void Index::load(const char* text, int len)
{
    clear();
    int pos = 0;
    // the first line is the header
    while (pos < len && text[pos] != '\n')
        ++pos;
    ++pos;
    char line[300];
    while (pos < len)
    {
        int l = 0;
        while (pos < len && text[pos] != '\n' && l < (int)sizeof(line) - 1)
            line[l++] = text[pos++];
        while (pos < len && text[pos] != '\n')
            ++pos; // too long: cut
        ++pos;
        line[l] = 0;
        if (l < 9)
            continue;
        int genre = 0, cat = 0, flags = 0, recent = 0, used = 0;
        if (sscanf(line, "%d;%d;%d;%d;%n", &genre, &cat, &flags, &recent, &used) < 4 || used <= 0 ||
            line[used] != '/')
            continue;
        const int i = addRaw(line + used, hashOf(line + used));
        if (i < 0)
            break;
        Entry& x = e[i];
        x.flags = (uint8_t)(flags & (kFlagFavourite | kFlagGenreSet | kFlagCatSet));
        if (x.flags & kFlagGenreSet)
            x.genre = (uint8_t)(genre >= 0 && genre < kGenreCount ? genre : 0);
        if (x.flags & kFlagCatSet)
            x.category = (uint8_t)(cat >= 0 && cat < kCatCount ? cat : 0);
        x.recent = (uint16_t)(recent < 0 ? 0 : (recent > 0xFFFF ? 0xFFFF : recent));
        if (x.recent > recentCounter)
            recentCounter = x.recent;
        guess(x);
    }
    dirty = false;
}

} // namespace library
} // namespace gibgob
