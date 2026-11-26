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
namespace gibgob { bool g_recArmed = false; }

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
static std::vector<int> sInstr; // the instrument of each sample note-on
static uint8_t lastFx[4];       // the effects of the last sample note-on
uint8_t playerEngine::noteOn(uint8_t instr, int8_t note, int8_t vel, uint8_t f1, uint8_t v1, uint8_t f2, uint8_t v2)
{
    lastFx[0] = f1; lastFx[1] = v1; lastFx[2] = f2; lastFx[3] = v2;
    lastSampleVolume = vel;
    if (irqDepth) { printf("FAIL noteOn under lock\n"); failures++; }
    (void)instr; (void)vel;
    events.push_back({nowTick, 'S', index, note});
    sInstr.push_back(instr);
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

    // T1d: a slide on a synth: CC 84 with the note before, just before the note
    {
        freshPattern();
        events.clear(); stopAll();
        setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
        Note a = mk(0, 4 * 48, 60); addNote(&pat, 5, a);
        Note b = mk(4 * 48, 4 * 48, 67); b.flags |= kNoteFlagSlide; addNote(&pat, 5, b);
        Note c = mk(8 * 48, 4 * 48, 65); addNote(&pat, 5, c); // no slide: no CC
        runTicks(0, 16 * 48);
        int ccs = 0; bool ok = true;
        for (size_t i = 0; i < events.size(); i++)
            if (events[i].kind == 'C')
            {
                ccs++;
                ok = ok && events[i].b == 60 && i + 1 < events.size() && events[i + 1].kind == 'M' &&
                     events[i + 1].b == 67 && events[i + 1].tick == events[i].tick;
            }
        CHECK(ccs == 1 && ok);
        CHECK(countKind('M') == 3);
        events.clear(); stopAll();
        CHECK(stuckSynth() == 0);
    }

    // T1c: the top buttons' notes: a minor scale from the base note, then what was picked;
    // a new pattern takes them over; the slice map and the notes share a slot without mixing
    {
        freshPattern();
        resetDefaults();
        setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 4);
        static const int kMinor[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };
        bool scale = true;
        for (int b = 0; b < 8; b++) scale = scale && buttonNote(&pat, 5, b) == kDefaultBaseNote + kMinor[b];
        CHECK(scale);
        setButtonNote(&pat, 5, 2, 64);
        CHECK(buttonNote(&pat, 5, 2) == 64 && buttonNote(&pat, 5, 3) == kDefaultBaseNote + 5);
        static Sequencer::strPattern other;
        memcpy(&other, &pat, sizeof(other));
        for (auto& t : other.track) for (auto& st : t.step) { st.note = -1; st.instrument = 0; }
        adoptDefaults(&pat);
        CHECK(buttonNote(&other, 5, 2) == 64); // an empty pattern: the track's picks
        setSliceForButton(&pat, 5, 0, 3);      // the slot now holds a slice map
        CHECK(sliceForButton(&pat, 5, 0) == 0 || sliceForButton(&pat, 5, 0) == 3);
        CHECK(buttonNote(&pat, 5, 2) == 64);   // the notes fall back to the remembered picks
        setButtonNote(&pat, 5, 7, 200);
        CHECK(buttonNote(&pat, 5, 7) == 127);
        resetDefaults();
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

    // T3d: trig conditions: 1:2 plays on passes 1, 3, ...; 3:4 on the 3rd of every 4
    {
        freshPattern();
        stopAll();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        Note a = mk(0, 24, 60); setNoteCond(a, 0); addNote(&pat, 0, a);        // 1:2
        Note b = mk(4 * 48, 24, 62); setNoteCond(b, 7); addNote(&pat, 0, b);   // 3:4
        CHECK(noteCond(getNote(&pat, 0, 0)) == 0 && getNote(&pat, 0, 0).chance() == 100 && noteFill(getNote(&pat, 0, 0)) == 0);
        events.clear();
        std::vector<int> passA, passB;
        for (int pass = 0; pass < 8; pass++)
        {
            const size_t before = events.size();
            runTicks(pass * 16 * 48, 16 * 48);
            for (size_t e = before; e < events.size(); e++)
                if (events[e].kind == 'S') (events[e].b == 60 ? passA : passB).push_back(pass);
        }
        CHECK(passA == std::vector<int>({ 0, 2, 4, 6 }));
        CHECK(passB == std::vector<int>({ 2, 6 }));
        // after a stop it counts from the first pass again
        stopAll(); events.clear();
        runTicks(0, 16 * 48);
        int n = 0; for (auto& e : events) if (e.kind == 'S' && e.b == 60) n++;
        CHECK(n == 1);
        stopAll(); events.clear();
    }

    // T3c: drum steps: two effects per step, kept in the tracker step's spare bytes
    {
        freshPattern();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        enableDrumSteps(&pat, 0);
        addNote(&pat, 0, mk(0, 48, 60));
        Note r = mk(2 * 48, 48, 60); setNoteRatchet(r, 1); addNote(&pat, 0, r); // 2 hits
        addNote(&pat, 0, mk(4 * 48, 48, 60));
        StepFx fx{ { kStepFxReverb, kStepFxLowpass }, { 70, 30 } };
        setStepFx(&pat, 0, 0, fx);
        StepFx f2{ { kStepFxPitchDown, kStepFxOff }, { 99, 0 } };
        setStepFx(&pat, 0, 2, f2);
        CHECK(hasStepFx(&pat, 0, 0) && hasStepFx(&pat, 0, 2) && !hasStepFx(&pat, 0, 4));
        StepFx back = getStepFx(&pat, 0, 0);
        CHECK(back.kind[0] == kStepFxReverb && back.kind[1] == kStepFxLowpass && back.value[0] == 70 && back.value[1] == 30);
        CHECK(pat.track[0].step[0].note == Sequencer::STEP_NOTE_EMPTY && pat.track[0].step[0].fx[0].type == 0 &&
              pat.track[0].step[0].fx[1].type == 0); // still an empty tracker step
        // the notes are untouched by it
        CHECK(noteCount(&pat, 0) == 3 && getNote(&pat, 0, 0).pitch == 60);
        events.clear();
        std::vector<std::vector<uint8_t>> fxAt;
        for (int t = 0; t < 5 * 48; t++)
        {
            const size_t before = events.size();
            runTicks(t, 1);
            for (size_t e = before; e < events.size(); e++)
                if (events[e].kind == 'S') fxAt.push_back({ lastFx[0], lastFx[1], lastFx[2], lastFx[3] });
        }
        CHECK(fxAt.size() == 4); // step 1, the ratchet's 2 hits, step 5
        if (fxAt.size() == 4)
        {
            CHECK(fxAt[0] == std::vector<uint8_t>({ Sequencer::strFxConsts::FX_TYPE_REVERB_SEND, 70, Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS, 30 }));
            CHECK(fxAt[1][0] == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN && fxAt[1][1] == 99 && fxAt[1][2] == 0);
            CHECK(fxAt[2] == fxAt[1]); // the ratchet's second hit too
            CHECK(fxAt[3] == std::vector<uint8_t>({ 0, 0, 0, 0 }));
        }
        // off again: the tracker step goes back to empty
        setStepFx(&pat, 0, 0, StepFx{ { kStepFxOff, kStepFxOff }, { 0, 0 } });
        CHECK(!hasStepFx(&pat, 0, 0) && pat.track[0].step[0].volume == Sequencer::STEP_VELO_DEFAULT &&
              pat.track[0].step[0].micromove == -1);
        // doubling the track takes the effects along
        CHECK(duplicateTrack(&pat, 0));
        CHECK(hasStepFx(&pat, 0, 2) && hasStepFx(&pat, 0, 2 + 16) && getStepFx(&pat, 0, 18).kind[0] == kStepFxPitchDown);
        clearAllStepFx(&pat, 0);
        CHECK(!hasStepFx(&pat, 0, 2) && !hasStepFx(&pat, 0, 18));
        // a track that is not a GibGob track reads no effects from old tracker steps
        freshPattern();
        pat.track[3].step[0].micromove = 0x47; pat.track[3].step[0].volume = 0x21;
        CHECK(!hasStepFx(&pat, 3, 0));
        stopAll(); events.clear();
    }

    // T3b: drum steps: a ratchet, an extra sample per step
    {
        freshPattern();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        Note n = mk(0, 48, 60);
        setNoteRatchet(n, 3); // 4 hits a step
        addNote(&pat, 0, n);
        runTicks(0, 96);
        CHECK(countKind('S') == 1); // drum steps not switched on: the bits mean nothing (an old track)
        freshPattern();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        n = mk(0, 96, 60); // 2 steps long
        setNoteRatchet(n, 3);
        addNote(&pat, 0, n);
        enableDrumSteps(&pat, 0); // switching on clears what was in the bits ...
        CHECK(noteRatchet(getNote(&pat, 0, 0)) == 0);
        setNote(&pat, 0, 0, n);   // ... so set it again
        CHECK(drumStepsOn(&pat, 0));
        events.clear();
        runTicks(0, 96);
        std::vector<int> at;
        for (auto& e : events) if (e.kind == 'S') at.push_back(e.tick);
        CHECK(at == std::vector<int>({ 0, 12, 24, 36, 48, 60, 72, 84 })); // 4 a step for 2 steps
        // a new hit stops the roll
        freshPattern();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        enableDrumSteps(&pat, 0);
        n = mk(0, 192, 60); setNoteRatchet(n, 1); addNote(&pat, 0, n); // 2 a step for 4 steps
        addNote(&pat, 0, mk(48, 48, 62));
        events.clear();
        runTicks(0, 192);
        int hits = 0; for (auto& e : events) if (e.kind == 'S') hits++;
        CHECK(hits == 3); // 0, 24, then the hit at 48 (no more of the roll)
        // an extra sample on a step
        freshPattern();
        setTrackType(&pat, 0, TrackType::Drum); setSound(&pat, 0, 0);
        enableDrumSteps(&pat, 0);
        CHECK(altSlotFor(&pat, 0, 0) == 0);  // its own sound
        const int k = altSlotFor(&pat, 0, 7);
        CHECK(k == 1 && altSample(&pat, 0, 1) == 7);
        CHECK(altSlotFor(&pat, 0, 7) == 1);  // the same one again
        CHECK(altSlotFor(&pat, 0, 8) == 2 && altSlotFor(&pat, 0, 9) == 3);
        n = mk(0, 48, 60); setNoteSample(n, 2); addNote(&pat, 0, n);
        n = mk(96, 48, 60); setNoteSample(n, 3); addNote(&pat, 0, n);
        n = mk(144, 48, 60); setNoteSample(n, 1); addNote(&pat, 0, n);
        CHECK(altSlotFor(&pat, 0, 10) == -1); // all 3 in use by notes
        removeNote(&pat, 0, 2);                // sample 7 is not used any more
        CHECK(altSlotFor(&pat, 0, 10) == 1 && altSample(&pat, 0, 1) == 10);
        events.clear(); sInstr.clear();
        runTicks(0, 192);
        CHECK(sInstr == std::vector<int>({ 8, 9 }));
        // an empty sample slot falls back to the track's sound
        mtProject.instrument[9].isActive = 0;
        events.clear(); sInstr.clear();
        stopAll(); runTicks(0, 192);
        CHECK(sInstr == std::vector<int>({ 8, 0 }));
        // to melodic: the drum bits are cleared (no accidental slides / envelopes)
        setTrackType(&pat, 0, TrackType::Melodic);
        CHECK(!drumStepsOn(&pat, 0) && noteSample(getNote(&pat, 0, 0)) == 0 && noteRatchet(getNote(&pat, 0, 0)) == 0);
        stopAll();
    }

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

    // T22b: a slice track's FX lane: FX notes make no sound, they change the slices under them
    {
        freshPattern();
        stopAll();
        mtProject.instrument[7].playMode = playModeBeatSlice;
        mtProject.instrument[7].sliceNumber = 12;
        mtProject.instrument[7].isActive = 1;
        setTrackType(&pat, 1, TrackType::Drum); setSound(&pat, 1, 7);
        CHECK(isSliceTrack(&pat, 1));
        CHECK(pitchOfSliceFx(kSfxRoll) == kFirstSliceNote - 1 && sliceFxOfPitch(kSliceFxFirstNote) == kSfxFade);
        CHECK(isSliceFxPitch(kSliceFxFirstNote) && !isSliceFxPitch(kFirstSliceNote) && !isSliceFxPitch(kSliceFxFirstNote - 1));
        addNote(&pat, 1, mk(0, 48, kFirstSliceNote + 2));          // slice 3, plain
        addNote(&pat, 1, mk(4 * 48, 48, kFirstSliceNote + 5));     // slice 6 under Reverse
        Note rev = mk(4 * 48, 4 * 48, pitchOfSliceFx(kSfxReverse)); rev.velocity = 100;
        addNote(&pat, 1, rev);
        addNote(&pat, 1, mk(8 * 48, 48, kFirstSliceNote + 1));     // slice 2 under Roll 1/32 for 2 steps
        Note roll = mk(8 * 48, 96, pitchOfSliceFx(kSfxRoll)); roll.velocity = 60; // 1/32
        addNote(&pat, 1, roll);
        events.clear();
        std::vector<std::pair<int, std::vector<uint8_t>>> hits; // tick, effects
        for (int t = 0; t < 16 * 48; t++)
        {
            const size_t before = events.size();
            runTicks(t, 1);
            for (size_t e = before; e < events.size(); e++)
                if (events[e].kind == 'S')
                {
                    CHECK(events[e].b >= kFirstSliceNote); // never an FX row as a sound
                    hits.push_back({ t, { lastFx[0], lastFx[1], lastFx[2], lastFx[3] } });
                }
        }
        // 0: plain; 192: reversed; 384 + the roll's hits every 24 for 2 steps (384..480)
        std::vector<int> at;
        for (auto& h : hits) at.push_back(h.first);
        CHECK(at == std::vector<int>({ 0, 192, 384, 408, 432, 456 }));
        if (hits.size() >= 2)
        {
            CHECK(hits[0].second[0] == 0);
            CHECK(hits[1].second[0] == Sequencer::strFxConsts::FX_TYPE_REVERSE_PLAYBACK);
        }
        // a chance of 0 %: the effect does not come on
        stopAll(); events.clear();
        Note r2 = getNote(&pat, 1, 1).pitch == pitchOfSliceFx(kSfxReverse) ? getNote(&pat, 1, 1) : rev;
        (void)r2;
        for (int i = 0; i < noteCount(&pat, 1); i++)
        {
            Note n = getNote(&pat, 1, i);
            if (n.pitch == pitchOfSliceFx(kSfxReverse)) { n.setChance(0); setNote(&pat, 1, i, n); }
        }
        bool reversed = false;
        for (int t = 0; t < 8 * 48; t++)
        {
            const size_t before = events.size();
            runTicks(t, 1);
            for (size_t e = before; e < events.size(); e++)
                if (events[e].kind == 'S' && lastFx[0] == Sequencer::strFxConsts::FX_TYPE_REVERSE_PLAYBACK) reversed = true;
        }
        CHECK(!reversed);
        // live (Shift + top held): Speed half while held, gone when let go
        stopAll(); events.clear();
        setLiveSliceFx(1, kSfxSpeed, true, 20);
        runTicks(0, 1);
        CHECK(countKind('S') == 1 && lastFx[0] == Sequencer::strFxConsts::FX_TYPE_TUNE);
        setLiveSliceFx(1, kSfxSpeed, false, 20);
        stopAll(); events.clear();
        runTicks(0, 1);
        CHECK(countKind('S') == 1 && lastFx[0] == 0);
        // the settings' texts
        char txt[16];
        sliceFxSettingText(kSfxRoll, 60, txt, sizeof(txt)); CHECK(strcmp(txt, "1/32") == 0);
        sliceFxSettingText(kSfxSpeed, 20, txt, sizeof(txt)); CHECK(strcmp(txt, "half") == 0);
        sliceFxSettingText(kSfxFilter, 100, txt, sizeof(txt)); CHECK(strncmp(txt, "HP", 2) == 0);
        sliceFxSettingText(kSfxPitch, 127, txt, sizeof(txt)); CHECK(strcmp(txt, "+12") == 0);
        sliceFxSettingText(kSfxPitch, 1, txt, sizeof(txt)); CHECK(strcmp(txt, "-12") == 0);
        stopAll(); events.clear();
        mtProject.instrument[7].playMode = 0;
    }

    // T23: live chord feel (Shift + top in the piano roll): strum, arp, humanize
    {
        freshPattern();
        sequencer.playing = false;
        setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 64);
        TrackSettings s = getSettings(&pat, 5);
        CHECK(!s.feel.any());
        CHECK(s.feel.mode == kFeelChord && s.feel.strum == 5 && s.feel.arpMode == kArpUp && s.feel.humTime == 0);
        s.feel.mode = kFeelArp; s.feel.strum = 10; s.feel.strumDir = kStrumAlt; s.feel.humTime = 4; s.feel.humVel = 3; s.feel.arpMode = kArpAsPlaced;
        setSettings(&pat, 5, s);
        TrackSettings r = getSettings(&pat, 5);
        CHECK(r.feel.mode == kFeelArp && r.feel.strum == 10 && r.feel.strumDir == kStrumAlt && r.feel.arpMode == kArpAsPlaced && r.feel.humTime == 4 && r.feel.humVel == 3);
        CHECK(r.arpMode == kArpOff); // the live arp leaves the pattern's arp off
        CHECK(r.apeSeed == s.apeSeed);
        // every combination survives being stored
        for (int m = 0; m < kFeelModeCount; m++)
            for (int st = 1; st <= kStrumMax; st++)
                for (int d = 0; d < kStrumDirCount; d++)
                    for (int a = 1; a < kArpModeCount; a++)
                        for (int h = 0; h <= kHumanMax; h++)
                        {
                            TrackSettings q = s;
                            q.feel.mode = m; q.feel.strum = st; q.feel.strumDir = d; q.feel.arpMode = a;
                            q.feel.humTime = h; q.feel.humVel = kHumanMax - h;
                            setSettings(&pat, 5, q);
                            ChordFeel w = getSettings(&pat, 5).feel;
                            CHECK(w.mode == m && w.strum == st && w.strumDir == d && w.arpMode == a && w.humTime == h && w.humVel == kHumanMax - h);
                        }
        // strum up: 13 ms between notes, low first; then Alt turns round
        s.feel = ChordFeel{}; s.feel.mode = kFeelStrum; s.feel.strum = 10; s.feel.strumDir = kStrumAlt;
        s.feel.humTime = 5; // kept for Human, unused while strumming
        setSettings(&pat, 5, s);
        const uint8_t ch[3] = { 60, 64, 67 };
        auto ons = [&]() { std::vector<int> v; for (auto& e : events) if (e.kind == 'M') v.push_back(e.b); return v; };
        chordPoll(1000);
        events.clear();
        chordPress(5, 2, ch, 3, 100);
        CHECK(ons() == std::vector<int>({60}));
        chordPoll(1012); CHECK(ons().size() == 1);
        chordPoll(1013); CHECK(ons() == std::vector<int>({60, 64}));
        chordPoll(1026); CHECK(ons() == std::vector<int>({60, 64, 67}));
        CHECK(chordHeld(5));
        chordRelease(5, 3); CHECK(chordHeld(5)); // another button: not this chord
        chordRelease(5, 2); CHECK(!chordHeld(5));
        CHECK(stuckSynth() == 0);
        events.clear();
        chordPoll(2000);
        chordPress(5, 2, ch, 3, 100);
        chordPoll(2030);
        CHECK(ons() == std::vector<int>({67, 64, 60})); // down this time
        // released mid-strum: the rest never starts
        chordRelease(5, 2);
        events.clear();
        chordPoll(3000);
        chordPress(5, 2, ch, 3, 100);
        chordRelease(5, 2);
        chordPoll(3100);
        CHECK(ons().size() == 1 && stuckSynth() == 0);
        // humanize: every note within 80 ms at Time 5, nothing stuck; the delays spread
        s.feel = ChordFeel{}; s.feel.mode = kFeelHuman; s.feel.humTime = 5; s.feel.humVel = 5;
        setSettings(&pat, 5, s);
        int lateCount = 0;
        for (int i = 0; i < 50; i++)
        {
            events.clear();
            const uint32_t t0 = 5000 + i * 100;
            chordPoll(t0);
            chordPress(5, 1, ch, 3, 100);
            chordPoll(t0 + 40);
            if (ons().size() == 3) lateCount += 0; else lateCount++;
            chordPoll(t0 + 80);
            CHECK(ons().size() == 3);
            chordRelease(5, 1);
        }
        CHECK(lateCount > 10); // with up to 80 ms, many chords still have a note to come at +40 ms
        CHECK(stuckSynth() == 0);
        // a sample track strummed while every other track has notes of its own:
        // the earlier notes of the strum keep sounding (none is cut by the next)
        {
            static Sequencer::strPattern saved;
            memcpy(&saved, &pat, sizeof(saved));
            freshPattern();
            setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 4);
            for (int t = 0; t < 8; t++)
                if (t != 5) addNote(&pat, t, mk(0, 10, 60));
            TrackSettings ss = getSettings(&pat, 5);
            ss.feel = ChordFeel{}; ss.feel.mode = kFeelStrum; ss.feel.strum = 5; ss.feel.strumDir = kStrumUp;
            setSettings(&pat, 5, ss);
            const uint8_t ch4[4] = { 60, 64, 67, 71 };
            chordPoll(40000);
            events.clear();
            chordPress(5, 0, ch4, 4, 100);
            for (uint32_t t = 40000; t <= 40400; t += 5) chordPoll(t);
            int started = 0, cut = 0;
            for (auto& e : events) { if (e.kind == 'S') started++; if (e.kind == 's') cut++; }
            int sounding = 0; for (bool b : voiceSounding) sounding += b;
            printf("  strum on a busy sample track: %d started, %d cut, %d sounding\n", started, cut, sounding);
            CHECK(started == 4 && cut == 0);
            chordRelease(5, 0);
            // and the plain chord the same
            events.clear();
            ss.feel.mode = kFeelChord; setSettings(&pat, 5, ss);
            chordPoll(41000);
            chordPress(5, 0, ch4, 4, 100);
            chordPoll(41010);
            started = cut = 0;
            for (auto& e : events) { if (e.kind == 'S') started++; if (e.kind == 's') cut++; }
            printf("  chord on a busy sample track: %d started, %d cut\n", started, cut);
            chordRelease(5, 0);
            memcpy(&pat, &saved, sizeof(pat)); // back to the synth track the next tests use
            for (int i = 0; i < 8; i++) instrumentPlayer[i].index = i;
            events.clear();
        }
        // humanize, the release: nothing starts once the button is let go, at any
        // moment of the chord (synth track 5 here, then a sample track)
        for (int kind = 0; kind < 2; kind++)
        {
            static Sequencer::strPattern saved2;
            memcpy(&saved2, &pat, sizeof(saved2));
            if (kind == 1) { setTrackType(&pat, 5, TrackType::Melodic); setSound(&pat, 5, 4); }
            TrackSettings hs = getSettings(&pat, 5);
            hs.feel = ChordFeel{}; hs.feel.mode = kFeelHuman; hs.feel.humTime = 5; hs.feel.humVel = 5;
            setSettings(&pat, 5, hs);
            const uint8_t ch5[5] = { 48, 55, 60, 64, 67 };
            int lateStarts = 0;
            for (int i = 0; i < 200; i++)
            {
                const uint32_t t0 = 60000 + i * 500;
                chordPoll(t0);
                chordPress(5, 3, ch5, 5, 100);
                const uint32_t hold = (uint32_t)(i * 7 % 120); // let go at 0 .. 119 ms
                for (uint32_t t = t0; t <= t0 + hold; t += 3) chordPoll(t);
                chordRelease(5, 3);
                events.clear();
                for (uint32_t t = t0 + hold; t <= t0 + 400; t += 3) chordPoll(t);
                for (auto& e : events) if (e.kind == 'S' || e.kind == 'M') lateStarts++;
            }
            int sounding = 0; for (bool b : voiceSounding) sounding += b;
            printf("  humanize release (%s): %d notes started after the release, %d voices left on\n",
                   kind ? "sample" : "synth", lateStarts, sounding);
            CHECK(lateStarts == 0);
            if (kind == 0) CHECK(stuckSynth() == 0);
            memcpy(&pat, &saved2, sizeof(pat));
            events.clear();
        }
        setSettings(&pat, 5, s);
        // arp, free running at 120 bpm: 1/16 = 125 ms, gate 50 %
        s.feel = ChordFeel{}; s.feel.mode = kFeelArp; s.feel.arpMode = kArpUp; s.feel.strum = 10;
        s.arpMode = kArpOff; s.arpRate = 3; s.arpOctaves = 2; s.arpGate = 50;
        setSettings(&pat, 5, s);
        events.clear();
        chordPoll(20000);
        chordPress(5, 4, ch, 3, 100);
        for (uint32_t t = 20000; t <= 20000 + 125 * 6 + 10; t += 5)
            chordPoll(t);
        CHECK(ons() == std::vector<int>({60, 64, 67, 72, 76, 79, 60}));
        CHECK(stuckSynth() == 1); // only the note started at +750 ms still sounds
        chordRelease(5, 4);
        CHECK(stuckSynth() == 0);
        // arp following the playing sequencer's grid; recorded with Rec on
        sequencer.playing = true;
        g_recArmed = true;
        stopAll();
        events.clear();
        runTicks(0, 1);
        chordPoll(30000);
        chordPress(5, 0, ch, 3, 100);
        uint32_t ms = 30000;
        for (int tick = 1; tick < 48 * 4; tick++)
        {
            runTicks(tick, 1);
            ms += 1;
            chordPoll(ms);
        }
        chordRelease(5, 0);
        g_recArmed = false;
        CHECK(noteCount(&pat, 5) >= 4); // the arp notes were recorded
        stopAll();
        CHECK(stuckSynth() == 0);
    }

    // fills: a fill-only step plays only while the fill is held, a left-out step only without it
    {
        freshPattern();
        Note always = mk(0, 24, 60), only = mk(4 * 48, 24, 62), out = mk(8 * 48, 24, 64);
        setNoteFill(only, 1);
        setNoteFill(out, -1);
        CHECK(only.chance() == 100 && out.chance() == 100); // a mark is not a chance
        CHECK(noteFill(only) == 1 && noteFill(out) == -1 && noteFill(always) == 0);
        addNote(&pat, 0, always); addNote(&pat, 0, only); addNote(&pat, 0, out);
        auto played = [](int pitch) { int n = 0; for (auto& e : events) if (e.kind == 'S' && e.b == pitch) n++; return n; };
        g_fillActive = false;
        runTicks(0, 16 * 48);
        CHECK(played(60) == 1 && played(62) == 0 && played(64) == 1);
        stopAll(); events.clear();
        g_fillActive = true;
        runTicks(0, 16 * 48);
        CHECK(played(60) == 1 && played(62) == 1 && played(64) == 0);
        g_fillActive = false;
        stopAll(); events.clear();
        // changing the chance ends the mark
        Note n = getNote(&pat, 0, 1);
        n.setChance(n.chance() - 5);
        CHECK(noteFill(n) == 0 && n.chance() == 95);
    }

    printf(failures ? "FAILED: %d\n" : "ALL ENGINE TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
