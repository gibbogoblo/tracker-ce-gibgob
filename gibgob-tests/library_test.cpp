// GibGob OS sample library: tags, guesses, shelves, the index file
#include "gibgob/Library.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gibgob::library;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    // guesses from file and folder names
    CHECK(Index::guessCategory("/Samples/Drums/Kick 01.wav") == kCatKicks);
    CHECK(Index::guessCategory("/Samples/x/BD_hard.wav") == kCatKicks);
    CHECK(Index::guessCategory("/Samples/x/chord stab.wav") == kCatKeys); // "ch" is no hat inside "chord"
    CHECK(Index::guessCategory("/Samples/x/ch_01.wav") == kCatHats);
    CHECK(Index::guessCategory("/Samples/x/Closed Hat.wav") == kCatHats);
    CHECK(Index::guessCategory("/Samples/808s/boom.wav") == kCatBass);
    CHECK(Index::guessCategory("/Samples/Vox/hey.wav") == kCatVocals);
    CHECK(Index::guessCategory("/Samples/Snares/one.wav") == kCatSnares); // from the folder
    CHECK(Index::guessCategory("/Samples/misc/thing.wav") == kCatNone);
    // the folder beats the file name: a vocal called "rim" stays a vocal
    CHECK(Index::guessCategory("/Samples/Vocals/rim vox.wav") == kCatVocals);
    CHECK(Index::guessCategory("/Samples/Vocals/Rim_01.wav") == kCatVocals);
    // the innermost folder first
    CHECK(Index::guessCategory("/Samples/Vocal Kit/Kicks/shout kick.wav") == kCatKicks);
    CHECK(Index::guessCategory("/Samples/Kicks/Vocals/kick me.wav") == kCatVocals);
    // no folder says anything: the file name
    CHECK(Index::guessCategory("/Samples/Pack 3/rim 2.wav") == kCatSnares);
    CHECK(Index::guessGenre("/Samples/UKG/hat.wav") == kGenreGarage);
    CHECK(Index::guessGenre("/Samples/Amen breaks/a.wav") == kGenreJungle);
    CHECK(Index::guessGenre("/Recordings 2/take.wav") == kGenreMine);
    CHECK(Index::guessGenre("/Samples/plain/a.wav") == kGenreNone);

    static char pool[4096];
    static Entry entries[64];
    Index idx;
    idx.init(pool, sizeof(pool), entries, 64);
    idx.beginScan();
    const int k = idx.add("/Samples/Garage/Kick 1.wav");
    const int h = idx.add("/Samples/Garage/Hat 2.wav");
    const int r = idx.add("/Recordings/take 1.wav");
    const int v = idx.add("/Samples/misc/vox thing.wav");
    idx.endScan();
    CHECK(idx.count() == 4 && k >= 0 && h >= 0 && r >= 0 && v >= 0);
    CHECK(strcmp(idx.name(k), "Kick 1") == 0);
    CHECK(idx.entry(k).genre == kGenreGarage && idx.entry(k).category == kCatKicks);
    CHECK(idx.entry(r).genre == kGenreMine);

    uint16_t list[64];
    CHECK(idx.filter(kShelfAll, kCatNone, list, 64) == 4);
    CHECK(idx.filter(kShelfGarage, kCatNone, list, 64) == 2);
    CHECK(idx.filter(kShelfGarage, kCatHats, list, 64) == 1 && list[0] == h);
    CHECK(idx.filter(kShelfMine, kCatNone, list, 64) == 1 && list[0] == r);
    // sorted by path
    const int all = idx.filter(kShelfAll, kCatNone, list, 64);
    for (int i = 1; i < all; i++) CHECK(strcasecmp(idx.path(list[i - 1]), idx.path(list[i])) < 0);

    // tags set on the Mini win over guesses and survive the file
    idx.setGenre(v, kGenreVocal);
    idx.setCategory(v, kCatVocals);
    idx.toggleFavourite(h);
    idx.markUsed(k);
    idx.markUsed(h);
    CHECK(idx.filter(kShelfFavourites, kCatNone, list, 64) == 1 && list[0] == h);
    CHECK(idx.filter(kShelfRecent, kCatNone, list, 64) == 2 && list[0] == h && list[1] == k); // latest first
    static char text[8192];
    const int len = idx.save(text, sizeof(text));
    CHECK(len > 0 && strncmp(text, "GIBGOBLIB 2\n", 12) == 0);
    CHECK(strstr(text, "/Recordings/take 1.wav") == nullptr); // nothing set: not stored

    {
        static char all[8192];
        const int la = idx.save(all, sizeof(all), true);
        CHECK(strstr(all, "/Recordings/take 1.wav") != nullptr); // the cache keeps every file
        Index c;
        static char pc[4096];
        static Entry ec[64];
        c.init(pc, sizeof(pc), ec, 64);
        c.load(all, la);
        CHECK(c.count() == 4);
        const int rr = c.find("/Recordings/take 1.wav");
        CHECK(rr >= 0 && c.entry(rr).genre == kGenreMine); // guessed again
    }
    Index back;
    static char pool2[4096];
    static Entry entries2[64];
    back.init(pool2, sizeof(pool2), entries2, 64);
    back.load(text, len);
    CHECK(back.count() == 3);
    const int v2 = back.find("/samples/MISC/vox thing.wav"); // no case
    CHECK(v2 >= 0 && back.entry(v2).genre == kGenreVocal && back.entry(v2).category == kCatVocals);
    const int h2 = back.find("/Samples/Garage/Hat 2.wav");
    CHECK(h2 >= 0 && (back.entry(h2).flags & kFlagFavourite));
    // a new scan: files kept, a missing one goes, a new one comes, tags stay
    back.beginScan();
    back.add("/Samples/Garage/Hat 2.wav");
    back.add("/Samples/misc/vox thing.wav");
    back.add("/Samples/New/Snare.wav");
    back.endScan();
    CHECK(back.count() == 3 && back.find("/Samples/Garage/Kick 1.wav") < 0);
    CHECK(back.find("/Samples/New/Snare.wav") >= 0);
    CHECK(back.filter(kShelfRecent, kCatNone, list, 64) == 1); // the hat is still recent
    const int v3 = back.find("/Samples/misc/vox thing.wav");
    CHECK(v3 >= 0 && back.entry(v3).genre == kGenreVocal);
    CHECK(strcmp(back.path(v3), "/Samples/misc/vox thing.wav") == 0); // names packed correctly

    // full: says so, nothing breaks
    Index tiny;
    static char pool3[40];
    static Entry entries3[2];
    tiny.init(pool3, sizeof(pool3), entries3, 2);
    CHECK(tiny.add("/Samples/a.wav") >= 0);
    CHECK(tiny.add("/Samples/b.wav") >= 0);
    CHECK(tiny.add("/Samples/c.wav") == -1 && tiny.full);
    // broken index lines are skipped
    const char* junk = "GIBGOBLIB 1\nnot a line\n1;2;3;4\n9;9;2;5;/Samples/ok.wav\n";
    tiny.load(junk, (int)strlen(junk));
    CHECK(tiny.count() == 1 && tiny.entry(0).genre == 0); // genre 9 is out of range: none

    // a tempo in the name gives a genre when no genre word does
    CHECK(Index::guessGenre("/Samples/Loops/break_174bpm.wav") == kGenreJungle);
    CHECK(Index::guessGenre("/Samples/Loops/Drums 92 BPM.wav") == kGenreHiphop);
    CHECK(Index::guessGenre("/Samples/Loops/shuffle-132-bpm.wav") == kGenreGarage);
    CHECK(Index::guessGenre("/Samples/Drums/kick 808.wav") == kGenreNone);   // a number, but no bpm
    CHECK(Index::guessGenre("/Samples/Loops/pad 120bpm.wav") == kGenreNone); // a tempo outside the three
    CHECK(Index::guessGenre("/Samples/Trap/loop 174bpm.wav") == kGenreHiphop); // a genre word wins

    // a whole folder at once
    {
        static char fpool[4096];
        static Entry fentries[16];
        Index f;
        f.init(fpool, sizeof(fpool), fentries, 16);
        f.beginScan();
        const int a1 = f.add("/Samples/Kit A/kick.wav");
        f.add("/Samples/Kit A/snare.wav");
        f.add("/Samples/Kit A/hat.wav");
        const int b1 = f.add("/Samples/Kit A/More/kick.wav"); // a folder inside: not this folder
        const int c1 = f.add("/Samples/Kit B/kick.wav");
        f.endScan();
        CHECK(f.setGenreFolder(a1, kGenreJungle) == 3);
        uint16_t fl[16];
        CHECK(f.filter(kShelfJungle, kCatNone, fl, 16) == 3);
        CHECK(f.entry(b1).genre == kGenreNone && f.entry(c1).genre == kGenreNone);
        CHECK(f.setCategoryFolder(c1, kCatPerc) == 1 && f.entry(c1).category == kCatPerc);
        CHECK(f.dirty);
    }

    // loops and one-shots: the name first, then the length (3 s or longer: a loop)
    {
        CHECK(Index::guessKind("/Samples/Loops/thing.wav", kLengthUnread) == kKindLoop);
        // a pack's folder says loops or a tempo, but the sample is short: the length wins
        CHECK(Index::guessKind("/Samples/Boom Bap Kit 90 BPM/kick.wav", 4) == kKindShot);
        CHECK(Index::guessKind("/Samples/Loops and Shots/snare.wav", 3) == kKindShot);
        CHECK(Index::guessKind("/Samples/Boom Bap Kit 90 BPM/kick.wav", kLengthUnread) == kKindLoop); // until read
        CHECK(Index::guessKind("/Samples/x/drum loop.wav", 14) == kKindLoop); // a 1.4 s loop: its name
        // cymbals are hits even when they ring for seconds
        CHECK(Index::guessKind("/Samples/Drums/Crash 01.wav", 45) == kKindShot);
        CHECK(Index::guessKind("/Samples/Drums/big_cymbal.wav", 52) == kKindShot);
        CHECK(Index::guessKind("/Samples/Drums/Ride.wav", 38) == kKindShot);
        CHECK(Index::guessKind("/Samples/Drums/override pad.wav", 45) == kKindLoop); // "ride" inside a word is no ride
        // a "shot" this long is a phrase
        CHECK(Index::guessKind("/Samples/Vocal Shots/hook.wav", 120) == kKindLoop);
        CHECK(Index::guessKind("/Samples/x/vocal shot.wav", 120) == kKindLoop);
        CHECK(Index::guessKind("/Samples/Vocal Shots/hey.wav", 40) == kKindShot);
        CHECK(Index::guessKind("/Samples/x/drums 92bpm.wav", kLengthUnread) == kKindLoop);
        CHECK(Index::guessKind("/Samples/x/amen break.wav", 12) == kKindLoop);
        CHECK(Index::guessKind("/Samples/One Shots/pad.wav", 70) == kKindShot);
        CHECK(Index::guessKind("/Samples/x/Kick Hit.wav", 60) == kKindShot);
        CHECK(Index::guessKind("/Samples/x/white noise.wav", 10) == kKindShot); // "hit" inside "white" is no hit
        CHECK(Index::guessKind("/Samples/x/pad.wav", 45) == kKindLoop);
        CHECK(Index::guessKind("/Samples/x/pad.wav", 29) == kKindShot);
        CHECK(Index::guessKind("/Samples/x/pad.wav", kLengthUnread) == kKindUnknown);
        CHECK(Index::guessKind("/Samples/x/pad.wav", kLengthBad) == kKindUnknown);

        static char kp[4096];
        static Entry ke[32];
        static uint16_t out[32];
        Index k;
        k.init(kp, sizeof(kp), ke, 32);
        const int a = k.add("/Samples/a/kick.wav");
        const int b = k.add("/Samples/a/vocal phrase.wav");
        const int c = k.add("/Samples/Loops/beat.wav");
        CHECK(k.nextUnmeasured(0) == a);
        CHECK(k.unmeasuredIn(kShelfAll, kCatNone) == 3);
        CHECK(k.nextUnmeasuredIn(kShelfAll, kCatVocals) == b); // what is looked at first
        CHECK(k.unmeasuredIn(kShelfAll, kCatVocals) == 1);
        k.setLength(a, 4);
        k.setLength(b, 65);
        CHECK(k.nextUnmeasured(0) == c);
        k.setLength(c, kLengthBad);
        CHECK(k.nextUnmeasured(0) == -1);
        CHECK(k.entry(a).kind == kKindShot && k.entry(b).kind == kKindLoop && k.entry(c).kind == kKindLoop);
        CHECK(k.filter(kShelfAll, kCatNone, out, 32, kKindOnlyShots) == 1 && out[0] == a);
        CHECK(k.filter(kShelfAll, kCatNone, out, 32, kKindOnlyLoops) == 2);
        CHECK(k.filter(kShelfAll, kCatNone, out, 32) == 3);
        // the lengths are kept in the index file (version 2) ...
        static char io[2048];
        const int len = k.save(io, sizeof(io), true);
        Index k2;
        static char kp2[4096];
        static Entry ke2[32];
        k2.init(kp2, sizeof(kp2), ke2, 32);
        k2.load(io, len);
        const int b2 = k2.find("/Samples/a/vocal phrase.wav");
        CHECK(b2 >= 0 && k2.entry(b2).tenths == 65 && k2.entry(b2).kind == kKindLoop);
        CHECK(k2.nextUnmeasured(0) == -1);
        // ... and an old index file (version 1, no lengths) still loads; its lengths are read again
        const char* old = "GIBGOBLIB 1\n0;2;4;0;/Samples/a/kick.wav\n";
        k2.load(old, (int)strlen(old));
        CHECK(k2.count() == 1 && k2.entry(0).category == kCatSnares && k2.entry(0).tenths == kLengthUnread);
        CHECK(k2.nextUnmeasured(0) == 0);
    }
    printf(failures ? "FAILED: %d\n" : "ALL LIBRARY TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
