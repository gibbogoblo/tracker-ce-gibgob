#include "gibgob/MelodicTrack.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "interruptsControl.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>

using namespace gibgob;

Sequencer sequencer;
strMtProject mtProject;
playerEngine instrumentPlayer[8];

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

// ---- fakes ----
static int irqDepth = 0;
void testDisableIrq() { if (irqDepth != 0) { printf("FAIL nested lock\n"); failures++; } irqDepth++; }
void testEnableIrq() { if (irqDepth != 1) { printf("FAIL unlock without lock\n"); failures++; } irqDepth = 0; }

bool isSampleInstrument(int i) { return i < 48; }
bool isSynthInstrument(int i) { return i >= 64 && i < 67; }
int instrumentIdxToMidiChannel(int i) { return i < 48 ? -1 : (i < 64 ? i - 48 + 1 : i - 64 + 1); }

struct Ev { int tick; char kind; int a; int b; };  // S on (voice,pitch) s off(voice) M on(ch,pitch) m off(ch,pitch)
static std::vector<Ev> events;
static int nowTick = 0;
static std::map<int,int> synthOn; // ch*128+pitch -> count
static bool voiceSounding[8];

static int lastSampleVolume = -1;
uint8_t playerEngine::noteOn(uint8_t instr, int8_t note, int8_t vel, uint8_t, uint8_t, uint8_t, uint8_t)
{
    lastSampleVolume = vel;
    if (irqDepth) { printf("FAIL noteOn under lock\n"); failures++; }
    (void)instr; (void)vel;
    events.push_back({nowTick, 'S', index, note});
    voiceSounding[index] = true;
    return 1;
}
void playerEngine::noteOff(int8_t)
{
    if (irqDepth) { printf("FAIL noteOff under lock\n"); failures++; }
    events.push_back({nowTick, 's', index, 0});
    voiceSounding[index] = false;
}
void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t ch, bool)
{
    if (note > 127 || velo == 0 || velo > 127) { printf("FAIL bad midi on %d %d\n", note, velo); failures++; }
    events.push_back({nowTick, 'M', ch, note});
    synthOn[ch * 128 + note]++;
}
void sendCC(uint8_t index, uint8_t value, uint8_t ch, bool toSynth)
{
    if (!toSynth || value > 127) { printf("FAIL bad cc %d %d\n", index, value); failures++; }
    events.push_back({nowTick, 'C', ch, value});
}
void sendMidiNoteOff(uint8_t note, uint8_t, uint8_t ch, bool)
{
    events.push_back({nowTick, 'm', ch, note});
    if (synthOn[ch * 128 + note] <= 0) { printf("FAIL midi off without on: %d\n", note); failures++; }
    else synthOn[ch * 128 + note]--;
}

static Sequencer::strPattern pat;

static void freshPattern(int steps = 16)
{
    // like a new tracker pattern: empty steps are not all-zero
    for (auto& t : pat.track)
    {
        t.length = steps - 1;
        for (auto& s : t.step) { s.note = -1; s.instrument = 0; s.fx[0] = {0,0}; s.fx[1] = {0,0}; s.volume = -1; s.micromove = -1; }
    }
    sequencer.pattern = &pat;
    stopAll();
    events.clear(); synthOn.clear();
    memset(voiceSounding, 0, sizeof(voiceSounding));
    memset(&mtProject, 0, sizeof(mtProject));
    for (auto& i : mtProject.instrument) { i.isActive = 1; i.volume = 50; }
    for (int i = 0; i < 8; i++) instrumentPlayer[i].index = i;
    setQuantizedRecording(false);
}

static void runTicks(int from, int count, int steps = 16)
{
    int total = steps * 48;
    for (int i = 0; i < count; i++)
    {
        nowTick = from + i;
        for (int t = 0; t < 8; t++)
            playMicroStep(t, nowTick % total, total);
    }
}

static int stuckSynth()
{
    int n = 0;
    for (auto& kv : synthOn) n += kv.second;
    return n;
}
static int countKind(char k) { int n = 0; for (auto& e : events) if (e.kind == k) n++; return n; }

static Note mk(int start, int len, int pitch, int vel = 100)
{
    Note n{}; n.start = start; n.length = len; n.pitch = pitch; n.velocity = vel; return n;
}

int main()
{
    sequencer.playing = true;

    // T0: data layer
    freshPattern();
    CHECK(!hasHeader(&pat, 0));
    CHECK(noteCount(&pat, 0) == 0);
    CHECK(getTrackType(&pat, 0) == TrackType::Drum);    // defaults: 1-4 drum
    CHECK(getTrackType(&pat, 5) == TrackType::Melodic); // 5-8 melodic
    CHECK(getSound(&pat, 3) == 3);
    // a changed layout carries into new patterns, but not into a new project
    setTrackType(&pat, 0, TrackType::Melodic);
    setTrackType(&pat, 6, TrackType::Drum);
    freshPattern();
    CHECK(getTrackType(&pat, 0) == TrackType::Melodic && getTrackType(&pat, 6) == TrackType::Drum);
    resetDefaults();
    freshPattern();
    for (int t = 0; t < 8; t++)
        CHECK(getTrackType(&pat, t) == (t < 4 ? TrackType::Drum : TrackType::Melodic));
    for (int i = 0; i < 130; i++)
    {
        int r = addNote(&pat, 2, mk(i * 3, 10, 60));
        CHECK(i < kMaxNotes ? r == i : r == -1);
    }
    CHECK(noteCount(&pat, 2) == kMaxNotes);
    // settings and palette slots must not have been touched by a full note list
    TrackSettings s0 = getSettings(&pat, 2);
    CHECK(s0.baseNote == 60 && s0.arpMode == kArpOff && s0.arpRate == 3 && s0.arpOctaves == 1 && s0.arpGate == 50);
    removeNote(&pat, 2, 0);
    CHECK(noteCount(&pat, 2) == kMaxNotes - 1);
    CHECK(getNote(&pat, 2, 0).start == 3);
    clearNotes(&pat, 2);
    CHECK(noteCount(&pat, 2) == 0);

    TrackSettings s; s.baseNote = 36; s.arpMode = kArpUpDown; s.arpRate = 5; s.arpOctaves = 3; s.arpGate = 80;
    s.apeOn = true; s.apeRhythm = 0; s.apeGate = 100; s.apeNotes = 77;
    setSettings(&pat, 2, s);
    TrackSettings r = getSettings(&pat, 2);
    CHECK(r.baseNote == 36 && r.arpMode == kArpUpDown && r.arpRate == 5 && r.arpOctaves == 3 && r.arpGate == 80);
    CHECK(r.apeOn && r.apeRhythm == 0 && r.apeGate == 100 && r.apeNotes == 77);
    CHECK(noteCount(&pat, 2) == 0);

    PaletteChord before5 = getPaletteChord(&pat, 5);
    PaletteChord pc{ 61, 9, 2, 3 };
    addNote(&pat, 7, mk(0, 48, 70));
    setPaletteChord(&pat, 14, pc);
    PaletteChord got = getPaletteChord(&pat, 14);
    CHECK(got.root == 61 && got.type == 9 && got.inversion == 2 && got.voicing == 3);
    PaletteChord after5 = getPaletteChord(&pat, 5);
    CHECK(after5.root == before5.root && after5.type == before5.type);
    CHECK(noteCount(&pat, 7) == 1 && getNote(&pat, 7, 0).pitch == 70); // palette write kept the notes
    for (int t = 0; t < 8; t++) CHECK(hasHeader(&pat, t));
    setTrackType(&pat, 0, TrackType::Melodic);
    setSound(&pat, 0, 64);
    CHECK(isMelodic(&pat, 0) && getSound(&pat, 0) == 64);
    // a new pattern inherits type, sound and palette
    adoptDefaults(&pat);
    freshPattern();
    CHECK(isMelodic(&pat, 0) && getSound(&pat, 0) == 64);
    CHECK(getPaletteChord(&pat, 14).root == 61);
    CHECK(!hasHeader(&pat, 0));

    // T1: synth chord, on/off balanced, off at the right time
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
    for (int k = 0; k < 4; k++) addNote(&pat, 5, mk(0, 96, 60 + k * 3));
    runTicks(0, 16 * 48 * 2);
    CHECK(countKind('M') == 8 && countKind('m') == 8);
    CHECK(stuckSynth() == 0);
    for (auto& e : events) if (e.kind == 'm') CHECK(e.tick % 768 == 96);

    // T1b: a singing synth gets each note's syllable as CC 3 just before the note
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 65);
    {
        Note a = mk(0, 96, 60); setNoteSyllable(a, 2); addNote(&pat, 5, a);
        Note b = mk(4 * 48, 96, 67); setNoteSyllable(b, 7); addNote(&pat, 5, b);
        g_singsSyllables = [](uint8_t instr) { return instr == 65; };
        runTicks(0, 16 * 48);
        int ccs = 0; bool ok = true;
        for (size_t i = 0; i < events.size(); i++)
            if (events[i].kind == 'C')
            {
                ccs++;
                const bool nextIsNote = i + 1 < events.size() && events[i + 1].kind == 'M' && events[i + 1].tick == events[i].tick;
                const int want = events[i + 1].b == 60 ? 2 * 127 / 7 : 127;
                if (!nextIsNote || events[i].b != want) ok = false;
            }
        CHECK(ccs == 2 && ok);
        // other synths get no CC
        g_singsSyllables = [](uint8_t) { return false; };
        events.clear(); stopAll();
        runTicks(0, 16 * 48);
        CHECK(countKind('C') == 0 && countKind('M') == 2);
        g_singsSyllables = nullptr;
        CHECK(stuckSynth() == 0);
        Note c{}; setNoteSyllable(c, 13); CHECK(noteSyllable(c) == 5);
        setNoteSyllable(c, 3); c.flags |= kNoteFlagAccent; CHECK(noteSyllable(c) == 3 && (c.flags & kNoteFlagAccent));
    }

    // T2: sample chord borrows voices of empty tracks
    freshPattern();
    setTrackType(&pat, 4, TrackType::Melodic); setSound(&pat, 4, 4);
    for (int k = 0; k < 4; k++) addNote(&pat, 4, mk(0, 96, 60 + k * 3));
    runTicks(0, 200);
    {
        std::map<int,int> voices;
        for (auto& e : events) if (e.kind == 'S') voices[e.a]++;
        CHECK(voices.size() == 4);
        CHECK(voices.count(4) == 1);
        CHECK(countKind('s') == 4);
        for (int v = 0; v < 8; v++) CHECK(!voiceSounding[v]);
    }
    // ...but not from tracks that have their own notes or are muted
    freshPattern();
    setTrackType(&pat, 4, TrackType::Melodic); setSound(&pat, 4, 4);
    for (int k = 0; k < 4; k++) addNote(&pat, 4, mk(0, 96, 60 + k * 3));
    for (int t = 0; t < 8; t++) if (t != 4) { if (t % 2) addNote(&pat, t, mk(400, 48, 60)); else mtProject.values.trackMute[t] = 1; }
    runTicks(0, 200);
    for (auto& e : events) if (e.kind == 'S') CHECK(e.a == 4);

    // T3: drum one-shot rings, stop silences it; long drum note is gated
    freshPattern();
    setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
    addNote(&pat, 0, mk(0, 48, 60));
    runTicks(0, 300);
    CHECK(countKind('S') == 1 && countKind('s') == 0);
    stopAll();
    CHECK(countKind('s') == 1);
    freshPattern();
    setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
    addNote(&pat, 0, mk(0, 96, 60));
    runTicks(0, 300);
    CHECK(countKind('S') == 1 && countKind('s') == 1);

    // T4: arp up, 3 held notes over 4 steps at 1/16
    freshPattern();
    setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
    s = TrackSettings(); s.arpMode = kArpUp; s.arpRate = 3; s.arpGate = 50;
    setSettings(&pat, 6, s);
    addNote(&pat, 6, mk(0, 192, 67)); addNote(&pat, 6, mk(0, 192, 60)); addNote(&pat, 6, mk(0, 192, 64));
    runTicks(0, 768);
    {
        std::vector<int> ons;
        for (auto& e : events) if (e.kind == 'M') ons.push_back(e.b);
        CHECK(ons.size() == 4);
        if (ons.size() == 4) CHECK(ons[0] == 60 && ons[1] == 64 && ons[2] == 67 && ons[3] == 60);
        CHECK(stuckSynth() == 0);
        for (auto& e : events) if (e.kind == 'm') CHECK(e.tick % 48 == 24);
    }
    // arp octaves + up-down
    freshPattern();
    setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
    s = TrackSettings(); s.arpMode = kArpUpDown; s.arpRate = 3; s.arpOctaves = 2;
    setSettings(&pat, 6, s);
    addNote(&pat, 6, mk(0, 384, 60)); addNote(&pat, 6, mk(0, 384, 64));
    runTicks(0, 768);
    {
        std::vector<int> ons;
        for (auto& e : events) if (e.kind == 'M') ons.push_back(e.b);
        int expect[8] = { 60, 64, 72, 76, 72, 64, 60, 64 };
        CHECK(ons.size() == 8);
        for (size_t i = 0; i < ons.size() && i < 8; i++) CHECK(ons[i] == expect[i]);
    }

    // T5: Go Ape stress on synth and sample tracks: nothing stuck, ever
    for (int round = 0; round < 40; round++)
    {
        freshPattern();
        int instr = (round % 2) ? 66 : 5;
        setTrackType(&pat, 7, TrackType::Melodic); setSound(&pat, 7, instr);
        s = TrackSettings(); s.arpMode = 1 + round % 5; s.arpRate = round % 6; s.arpOctaves = 1 + round % 4;
        s.arpGate = 10 + (round * 13) % 91; s.apeOn = true; s.apeRhythm = 100; s.apeGate = 100; s.apeNotes = 100;
        setSettings(&pat, 7, s);
        for (int k = 0; k < 10; k++) addNote(&pat, 7, mk((random32() % 16) * 48 + random32() % 48, 24 + random32() % 400, 1 + random32() % 126));
        // a second busy plain track
        setTrackType(&pat, 1, TrackType::Melodic); setSound(&pat, 1, 64);
        for (int k = 0; k < 40; k++) addNote(&pat, 1, mk(random32() % 768, 1 + random32() % 300, random32() % 128, 1 + random32() % 127));
        runTicks(0, 768 * 6);
        if (round % 3 == 0) mtProject.values.trackMute[7] = 1; // mute while notes sound
        runTicks(768 * 6, 2000);
        stopAll();
        // after stop everything that was held must be released by the countdowns/stop
        CHECK(stuckSynth() == 0);
        if (instr == 5) for (int v = 0; v < 8; v++) CHECK(!voiceSounding[v]);
    }

    // T6: probability 0 never plays, 100 always
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
    { Note n = mk(0, 24, 60); n.setChance(0); addNote(&pat, 5, n); Note m = mk(48, 24, 62); m.setChance(100); addNote(&pat, 5, m); }
    runTicks(0, 768 * 20);
    for (auto& e : events) if (e.kind == 'M') CHECK(e.b == 62);
    CHECK(countKind('M') == 20);

    // T7: live record: note lands at the play position with the held length
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
    runTicks(0, 100);
    { uint8_t ch[3] = { 60, 64, 67 }; CHECK(liveRecordOn(5, ch, 3, 90, true)); }
    CHECK(noteCount(&pat, 5) == 3 && getNote(&pat, 5, 0).start == 99);
    int onsBefore = countKind('M');
    runTicks(100, 150);
    CHECK(countKind('M') == onsBefore); // not re-triggered in the same pass
    { uint8_t two[2] = { 60, 64 }; liveRecordOff(5, two, 2); }
    CHECK(getNote(&pat, 5, 0).length == 150 && !(getNote(&pat, 5, 0).flags & kNoteFlagRecording));
    CHECK(getNote(&pat, 5, 2).flags & kNoteFlagRecording);
    runTicks(250, 700); // wraps past the pattern end
    liveRecordOff(5);
    CHECK(!(getNote(&pat, 5, 2).flags & kNoteFlagRecording));
    CHECK(getNote(&pat, 5, 2).length == (949 % 768) - 99 + 0 || getNote(&pat, 5, 2).length > 0);
    stopAll();
    CHECK(stuckSynth() == 0);

    // T8: audition (finger drumming / chords) with samples and synths
    freshPattern();
    setTrackType(&pat, 4, TrackType::Melodic); setSound(&pat, 4, 4);
    { uint8_t ch[4] = { 60, 64, 67, 71 }; auditionChord(4, ch, 4, 100); }
    CHECK(countKind('S') == 4 && isSounding(4));
    auditionOff(4);
    CHECK(countKind('s') == 4 && !isSounding(4));
    setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
    auditionOn(0, 60, 100); auditionOff(0);
    CHECK(countKind('s') == 4); // drum hit rings on
    setSound(&pat, 5, 64); setTrackType(&pat, 5, TrackType::Melodic);
    { uint8_t ch[4] = { 60, 64, 67, 71 }; auditionChord(5, ch, 4, 100); auditionChord(5, ch, 4, 100); }
    auditionOff(5);
    CHECK(stuckSynth() == 0);
    // empty sample slot: silent, no crash
    mtProject.instrument[9].isActive = 0; setSound(&pat, 3, 9);
    int sBefore = countKind('S');
    auditionOn(3, 60, 100); auditionOff(3);
    CHECK(countKind('S') == sBefore);

    // T9: tools
    freshPattern();
    addNote(&pat, 5, mk(47, 10, 60)); addNote(&pat, 5, mk(25, 10, 60)); addNote(&pat, 5, mk(760, 10, 60));
    quantizeTrack(&pat, 5);
    CHECK(getNote(&pat, 5, 0).start == 48 && getNote(&pat, 5, 1).start == 48 && getNote(&pat, 5, 2).start == 0);
    {
        AcidOptions o;
        for (int i = 0; i < 50; i++)
        {
            o.density = 1 + i % 10; o.slide = i % 11; o.accent = (i * 3) % 11; o.envelope = (i * 7) % 11;
            generateAcid(&pat, 6, o);
            int c = noteCount(&pat, 6);
            CHECK(c <= 16);
            for (int k = 0; k < c; k++)
            {
                Note n = getNote(&pat, 6, k);
                CHECK(n.start < 768 && n.start % 48 == 0 && n.pitch <= 127 && n.length > 0);
                if (k > 0) CHECK(n.start > getNote(&pat, 6, k - 1).start);
                if (n.flags & kNoteFlagSlide) CHECK(k > 0 && getNote(&pat, 6, k - 1).start + getNote(&pat, 6, k - 1).length > n.start);
                if (o.accent == 0) CHECK(!(n.flags & kNoteFlagAccent));
                if (o.envelope == 0) CHECK(noteEnv(n) == 0);
            }
        }
        o.density = 10; o.slide = 0;
        generateAcid(&pat, 6, o);
        CHECK(noteCount(&pat, 6) == 16);
        // "in use": follows the chord another melodic track holds at each step
        clearNotes(&pat, 5); setTrackType(&pat, 5, TrackType::Melodic); setTrackType(&pat, 6, TrackType::Melodic);
        addNote(&pat, 5, mk(0, 384, 60)); addNote(&pat, 5, mk(0, 384, 64)); addNote(&pat, 5, mk(0, 384, 67));   // C major, steps 1-8
        addNote(&pat, 5, mk(384, 384, 62)); addNote(&pat, 5, mk(384, 384, 65)); addNote(&pat, 5, mk(384, 384, 69)); // D minor, steps 9-16
        o.notes = AcidNotes::InUse;
        for (int r = 0; r < 20; r++)
        {
            generateAcid(&pat, 6, o);
            for (int k = 0; k < noteCount(&pat, 6); k++)
            {
                Note n = getNote(&pat, 6, k); int pc = n.pitch % 12;
                if (n.start < 384) CHECK(pc == 0 || pc == 4 || pc == 7);
                else CHECK(pc == 2 || pc == 5 || pc == 9);
            }
        }
        o.notes = AcidNotes::Pick; o.pickedCount = 2; o.picked[0] = 41; o.picked[1] = 48;
        generateAcid(&pat, 6, o);
        for (int k = 0; k < noteCount(&pat, 6); k++) CHECK(getNote(&pat, 6, k).pitch == 41 || getNote(&pat, 6, k).pitch == 48);
    }

    // T10: switching to a pattern where the track is now empty still releases held notes
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
    addNote(&pat, 5, mk(0, 500, 60));
    runTicks(0, 100);
    clearNotes(&pat, 5);
    runTicks(100, 600);
    CHECK(stuckSynth() == 0);

    // T11: track lengths and duplicate
    freshPattern();
    CHECK(patternSteps(&pat) == 16 && trackSteps(&pat, 2) == 16);
    setTrackSteps(&pat, 2, 12);
    CHECK(trackSteps(&pat, 2) == 12 && patternSteps(&pat) == 16 && trackSteps(&pat, 0) == 16);
    setTrackSteps(&pat, 3, 32);
    CHECK(patternSteps(&pat) == 32 && trackSteps(&pat, 0) == 16 && trackSteps(&pat, 2) == 12 && trackSteps(&pat, 3) == 32);
    for (int t = 0; t < 16; t++) CHECK(pat.track[t].length == 31);
    setTrackSteps(&pat, 3, 8);
    CHECK(patternSteps(&pat) == 16);
    // a 12-step track loops inside the 16-step pattern
    setTrackType(&pat, 2, TrackType::Melodic); setSound(&pat, 2, 64);
    addNote(&pat, 2, mk(0, 24, 60));
    runTicks(0, 48 * 48, 16); // 3 pattern loops
    // tracks restart with the pattern: plays at step 1 and step 13 of every loop
    { int n = 0; for (auto& e : events) if (e.kind == 'M') { int t = e.tick % 768; CHECK(t == 0 || t == 576); n++; } CHECK(n == 6); }
    stopAll();
    // duplicate
    freshPattern();
    addNote(&pat, 1, mk(0, 24, 60)); addNote(&pat, 1, mk(100, 30, 62));
    CHECK(duplicateTrack(&pat, 1));
    CHECK(trackSteps(&pat, 1) == 32 && noteCount(&pat, 1) == 4 && patternSteps(&pat) == 32);
    CHECK(getNote(&pat, 1, 2).start == 768 && getNote(&pat, 1, 3).start == 868 && getNote(&pat, 1, 3).pitch == 62);
    CHECK(trackSteps(&pat, 0) == 16);
    CHECK(duplicateTrack(&pat, 1) && duplicateTrack(&pat, 1) && trackSteps(&pat, 1) == 128);
    CHECK(!duplicateTrack(&pat, 1));
    freshPattern();
    for (int i = 0; i < 70; i++) addNote(&pat, 1, mk(i, 5, 60));
    CHECK(!duplicateTrack(&pat, 1) && trackSteps(&pat, 1) == 16 && noteCount(&pat, 1) == 70);

    // T12: quantize strength and humanize range
    freshPattern();
    addNote(&pat, 1, mk(48 + 10, 10, 60)); addNote(&pat, 1, mk(96 - 2, 10, 60)); addNote(&pat, 1, mk(0, 10, 60)); addNote(&pat, 1, mk(767, 10, 60));
    quantizeTrack(&pat, 1, 1);
    CHECK(getNote(&pat, 1, 0).start == 51 && getNote(&pat, 1, 1).start == 94 && getNote(&pat, 1, 2).start == 0);
    quantizeTrack(&pat, 1, 10);
    CHECK(getNote(&pat, 1, 0).start == 48 && getNote(&pat, 1, 1).start == 96 && getNote(&pat, 1, 3).start == 0);
    for (int r = 0; r < 200; r++)
    {
        freshPattern();
        addNote(&pat, 1, mk(0, 10, 60)); addNote(&pat, 1, mk(400, 10, 60)); addNote(&pat, 1, mk(767, 10, 60));
        humanizeTrack(&pat, 1, 10);
        CHECK(getNote(&pat, 1, 0).start <= 20);
        CHECK(getNote(&pat, 1, 1).start >= 380 && getNote(&pat, 1, 1).start <= 420);
        CHECK(getNote(&pat, 1, 2).start >= 747 && getNote(&pat, 1, 2).start <= 767);
        humanizeTrack(&pat, 1, 1);
    }

    // T13: Go Ape is repeatable, and Apply writes exactly what was heard
    for (int round = 0; round < 12; round++)
    {
        freshPattern();
        setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
        TrackSettings a; a.arpMode = 1 + round % 5; a.arpRate = round % 6; a.arpOctaves = 1 + round % 3; a.arpGate = 30 + round * 5;
        a.apeOn = true; a.apeSeed = 0x1234567u * (round + 1); a.apeRhythm = 60; a.apeGate = 50; a.apeNotes = 30 + round * 6;
        setSettings(&pat, 6, a);
        CHECK(getSettings(&pat, 6).apeSeed == a.apeSeed);
        addNote(&pat, 6, mk(0, 384, 60)); addNote(&pat, 6, mk(0, 384, 64)); addNote(&pat, 6, mk(0, 384, 67));
        addNote(&pat, 6, mk(384, 384, 62)); addNote(&pat, 6, mk(384, 384, 65)); addNote(&pat, 6, mk(384, 384, 69));
        runTicks(0, 64 * 48, 16);
        std::vector<std::pair<int,int>> heard;
        for (auto& e : events) if (e.kind == 'M') heard.push_back({ e.tick, e.b });
        // second playback: identical
        stopAll(); events.clear();
        runTicks(0, 64 * 48, 16);
        std::vector<std::pair<int,int>> again;
        for (auto& e : events) if (e.kind == 'M') again.push_back({ e.tick, e.b });
        CHECK(heard == again);
        stopAll();
        CHECK(stuckSynth() == 0);
        int written = renderArp(&pat, 6);
        CHECK(written == (int)heard.size() || (written == kMaxNotes && (int)heard.size() > kMaxNotes));
        CHECK(getSettings(&pat, 6).arpMode == kArpOff && trackSteps(&pat, 6) == 64);
        for (int k = 0; k < written && k < (int)heard.size(); k++)
        {
            Note n = getNote(&pat, 6, k);
            CHECK(n.start == heard[k].first && n.pitch == heard[k].second);
        }
    }
    // Apply also matches when the track is shorter than the pattern (12 of 16 steps)
    {
        freshPattern();
        setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
        TrackSettings a; a.arpMode = kArpUpDown; a.arpRate = 4; a.apeOn = true; a.apeSeed = 777; a.apeRhythm = 50; a.apeNotes = 70;
        setSettings(&pat, 6, a);
        setTrackSteps(&pat, 6, 12);
        addNote(&pat, 6, mk(0, 200, 60)); addNote(&pat, 6, mk(48 * 5, 300, 63)); addNote(&pat, 6, mk(48 * 5, 300, 67));
        runTicks(0, 64 * 48, 16);
        std::vector<std::pair<int,int>> heard;
        for (auto& e : events) if (e.kind == 'M') heard.push_back({ e.tick, e.b });
        stopAll();
        int written = renderArp(&pat, 6);
        CHECK(written == (int)heard.size());
        for (int k = 0; k < written && k < (int)heard.size(); k++)
            CHECK(getNote(&pat, 6, k).start == heard[k].first && getNote(&pat, 6, k).pitch == heard[k].second);
    }
    // apeNotes up to 50: only fifths, sevenths and octaves away from the held notes
    {
        freshPattern();
        setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
        TrackSettings a; a.arpMode = kArpUp; a.arpRate = 3; a.apeOn = true; a.apeSeed = 99; a.apeRhythm = 0; a.apeGate = 0; a.apeNotes = 50;
        setSettings(&pat, 6, a);
        addNote(&pat, 6, mk(0, 768, 60));
        runTicks(0, 64 * 48, 16);
        for (auto& e : events) if (e.kind == 'M') { int d = e.b - 60; CHECK(d == 0 || d == 7 || d == -5 || d == 10 || d == -2 || d == 12 || d == -12); }
        stopAll();
    }

    // T14: slide hands the voice over with a glide, accent raises velocity
    freshPattern();
    setTrackType(&pat, 4, TrackType::Melodic); setSound(&pat, 4, 4);
    { Note a = mk(0, 60, 48); addNote(&pat, 4, a); Note b = mk(48, 24, 55); b.flags = kNoteFlagSlide | kNoteFlagAccent; addNote(&pat, 4, b); }
    runTicks(0, 200);
    {
        std::vector<int> voices; for (auto& e : events) if (e.kind == 'S') voices.push_back(e.a);
        CHECK(voices.size() == 2 && voices[0] == voices[1]);
        CHECK(countKind('s') == 1); // only the final release
    }

    // T15: drum tracks are monophonic: a new hit cuts the ringing one
    freshPattern();
    setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
    addNote(&pat, 0, mk(0, 48, 60)); addNote(&pat, 0, mk(96, 48, 60));
    runTicks(0, 200);
    CHECK(countKind('S') == 2 && countKind('s') == 1); // first hit stopped when the second starts
    for (auto& ev : events) if (ev.kind == 's') CHECK(ev.tick == 96);
    stopAll();

    // T16: Go Ape notes never overlap
    for (int round = 0; round < 30; round++)
    {
        freshPattern();
        setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
        TrackSettings a; a.arpMode = 1 + round % 5; a.arpRate = round % 6; a.arpOctaves = 1 + round % 4; a.arpGate = 100;
        a.apeOn = true; a.apeSeed = 1000 + round; a.apeRhythm = 100; a.apeGate = 100; a.apeNotes = 100;
        setSettings(&pat, 6, a);
        addNote(&pat, 6, mk(0, 768, 60)); addNote(&pat, 6, mk(0, 768, 64)); addNote(&pat, 6, mk(0, 768, 67));
        runTicks(0, 64 * 48, 16);
        int open = 0, maxOpen = 0;
        for (auto& ev : events) { if (ev.kind == 'M') open++; if (ev.kind == 'm') open--; if (open > maxOpen) maxOpen = open; }
        // offs come before ons within a tick, so at most one note at a time
        CHECK(maxOpen <= 1);
        stopAll();
    }

    // T17: utilities and swing
    freshPattern();
    addNote(&pat, 5, mk(0, 10, 60)); addNote(&pat, 5, mk(96, 10, 64)); addNote(&pat, 5, mk(96, 10, 67));
    transposeNotes(&pat, 5, 12);
    CHECK(getNote(&pat, 5, 0).pitch == 72 && getNote(&pat, 5, 2).pitch == 79);
    { Note n = getNote(&pat, 5, 1); n.flags |= kNoteFlagSelected; setNote(&pat, 5, 1, n); }
    transposeNotes(&pat, 5, -1);
    CHECK(getNote(&pat, 5, 0).pitch == 72 && getNote(&pat, 5, 1).pitch == 75);
    legatoNotes(&pat, 5);
    CHECK(getNote(&pat, 5, 0).length == 96 && getNote(&pat, 5, 1).length == 768 - 96 && getNote(&pat, 5, 2).length == 768 - 96);
    { int idx[2] = { 1, 2 }; copyNotes(&pat, 5, idx, 2); }
    CHECK(hasClipboard());
    clearNotes(&pat, 2);
    CHECK(pasteNotes(&pat, 2, 48 * 3) == 2 && noteCount(&pat, 2) == 2);
    CHECK(getNote(&pat, 2, 0).start == 144 && getNote(&pat, 2, 1).start == 144 && getNote(&pat, 2, 0).pitch == 75);
    CHECK(!(getNote(&pat, 2, 0).flags & kNoteFlagSelected));
    CHECK(pasteNotes(&pat, 2, 767) == 2);          // fits at the very end
    clearNotes(&pat, 7);
    addNote(&pat, 7, mk(0, 48, 60)); addNote(&pat, 7, mk(48, 96, 62));
    reverseNotes(&pat, 7);
    CHECK(getNote(&pat, 7, 0).start == 720 && getNote(&pat, 7, 1).start == 624);
    CHECK(patternSwing(&pat) == 50);
    setPatternSwing(&pat, 62);
    CHECK(patternSwing(&pat) == 62);
    setPatternSwing(&pat, 99);
    CHECK(patternSwing(&pat) == 75);
    adoptDefaults(&pat);
    freshPattern();
    CHECK(patternSwing(&pat) == 75); // a new pattern starts with the last swing

    // T18: velocity scales the sample's own Volume (100 = as set)
    freshPattern();
    setTrackType(&pat, 3, TrackType::Drum); setSound(&pat, 3, 3);
    mtProject.instrument[3].volume = 40;
    addNote(&pat, 3, mk(0, 24, 60, 100)); addNote(&pat, 3, mk(96, 24, 60, 50));
    runTicks(0, 1); CHECK(lastSampleVolume == 40);
    runTicks(1, 100); CHECK(lastSampleVolume == 20);
    mtProject.instrument[3].volume = 100;
    { Note n = mk(200, 24, 60, 127); n.flags = kNoteFlagAccent; addNote(&pat, 3, n); }
    runTicks(101, 150); CHECK(lastSampleVolume == 100); // capped
    stopAll();

    // T19: Go Ape keeps the same basic rhythm whatever the amounts
    {
        std::vector<int> base;
        for (int amount : { 0, 60, 100 })
        {
            freshPattern();
            setTrackType(&pat, 6, TrackType::Melodic); setSound(&pat, 6, 65);
            TrackSettings a; a.arpMode = kArpUp; a.arpRate = 3; a.apeOn = true; a.apeSeed = 4242;
            a.apeRhythm = amount; a.apeGate = amount; a.apeNotes = amount;
            setSettings(&pat, 6, a);
            addNote(&pat, 6, mk(0, 768, 60)); addNote(&pat, 6, mk(0, 768, 64));
            runTicks(0, 64 * 48, 16);
            std::vector<int> stepsPlayed; // arp steps that start a note on the grid
            for (auto& ev : events) if (ev.kind == 'M' && ev.tick % 48 == 0) stepsPlayed.push_back(ev.tick);
            if (base.empty()) base = stepsPlayed; else CHECK(stepsPlayed == base);
            stopAll();
        }
        CHECK(!base.empty() && base.size() < 64); // some rests in the basic rhythm
    }

    // T20: quantized live recording lands on the step and is not heard twice
    freshPattern();
    setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
    setQuantizedRecording(true);
    runTicks(0, 46); // now at tick 45, just before step 2
    { uint8_t pch = 70; CHECK(liveRecordOn(5, &pch, 1, 100, true)); }
    CHECK(getNote(&pat, 5, 0).start == 48);
    int onsBefore2 = countKind('M');
    runTicks(46, 60);
    CHECK(countKind('M') == onsBefore2); // skipped once: it was heard live
    { uint8_t pch = 70; liveRecordOff(5, &pch, 1); }
    CHECK(getNote(&pat, 5, 0).length % 48 == 0 && getNote(&pat, 5, 0).length >= 48);
    runTicks(106, 768);
    CHECK(countKind('M') == onsBefore2 + 1); // plays on the next loop
    setQuantizedRecording(false);
    stopAll();

    // T21: step 1 can be nudged early into the end of the track
    CHECK(stepOffset(765, 0, 768) == -3 && startFromOffset(0, -3, 768) == 765);
    CHECK(belongsToStep(765, 0, 768) && !belongsToStep(765, 15, 768));
    CHECK(stepOffset(50, 1, 768) == 2 && startFromOffset(1, -24, 768) == 24);

    // T22: slice tracks: a beat-slice sample makes a track a Slice track,
    // whatever its stored type; top buttons map to slices and survive notes
    freshPattern();
    mtProject.instrument[7].isActive = 1;
    mtProject.instrument[7].playMode = playModeBeatSlice;
    mtProject.instrument[7].sliceNumber = 12;
    setTrackType(&pat, 2, TrackType::Melodic); setSound(&pat, 2, 7);
    setTrackType(&pat, 1, TrackType::Drum); setSound(&pat, 1, 7);
    CHECK(isSliceTrack(&pat, 2) && isSliceTrack(&pat, 1));
    CHECK(sliceCount(&pat, 2) == 12 && sliceCount(&pat, 0) == 0);
    CHECK(sliceForButton(&pat, 2, 0) == 0 && sliceForButton(&pat, 2, 7) == 7);
    CHECK(laneNote(&pat, 2) == kFirstSliceNote);
    addNote(&pat, 2, mk(0, 10, kFirstSliceNote + 3));
    setSliceForButton(&pat, 2, 0, 9);
    setSliceForButton(&pat, 2, 5, 11);
    CHECK(sliceForButton(&pat, 2, 0) == 9 && sliceForButton(&pat, 2, 5) == 11 && sliceForButton(&pat, 2, 1) == 1);
    CHECK(laneNote(&pat, 2) == kFirstSliceNote + 9);
    CHECK(noteCount(&pat, 2) == 1 && getNote(&pat, 2, 0).pitch == kFirstSliceNote + 3); // the note survives
    for (int i = 0; i < 130; i++) addNote(&pat, 2, mk((i % 16) * 48, 10, kFirstSliceNote + (i % 12)));
    CHECK(noteCount(&pat, 2) == kMaxNotes && sliceForButton(&pat, 2, 0) == 9); // a full track keeps the map
    // a normal sample is no slice track
    mtProject.instrument[7].playMode = 0;
    CHECK(!isSliceTrack(&pat, 2) && laneNote(&pat, 2) == getSettings(&pat, 2).baseNote);
    mtProject.instrument[7].playMode = playModeBeatSlice;
    mtProject.instrument[7].sliceNumber = 0;
    CHECK(!isSliceTrack(&pat, 2));

    printf(failures ? "FAILED: %d\n" : "ALL ENGINE TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
