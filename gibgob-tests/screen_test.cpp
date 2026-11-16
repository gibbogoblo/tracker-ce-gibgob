#include "gibgob/LaneControl.h"
#include "gibgob/PianoRollControl.h"
#include "gibgob/TableControl.h"
#include "gibgob/MelodicTrack.h"
#include "FT812.h"
#include "display.h"
#include "core/interface.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "interruptsControl.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
using namespace gibgob;

Sequencer sequencer; strMtProject mtProject; playerEngine instrumentPlayer[8]; Interface mtInterface;
strFont fonts[5] = { {26}, {27}, {28}, {29}, {30} };
char notesTable[128][5];
extern const char mtNotes[128][5];
const char mtNotes[128][5] = {
#define N(o) "C" #o, "C#" #o, "D" #o, "D#" #o, "E" #o, "F" #o, "F#" #o, "G" #o, "G#" #o, "A" #o, "A#" #o, "B" #o
    N(0), N(1), N(2), N(3), N(4), N(5), N(6), N(7), N(8), N(9), "C10", "C#10", "D10", "D#10", "E10", "F10", "F#10", "G10" };
void testDisableIrq() {} void testEnableIrq() {}
bool isSampleInstrument(int i) { return i < 48; }
bool isSynthInstrument(int i) { return i >= 64; }
int instrumentIdxToMidiChannel(int) { return 1; }
uint8_t playerEngine::noteOn(uint8_t, int8_t, int8_t, uint8_t, uint8_t, uint8_t, uint8_t) { return 1; }
void playerEngine::noteOff(int8_t) {}
void sendMidiNoteOn(uint8_t, uint8_t, uint8_t, bool) {}
void sendMidiNoteOff(uint8_t, uint8_t, uint8_t, bool) {}

// ---- exact word counting, like the firmware's display list capture ----
static int words = 0, fails = 0, verts = 0;
static bool inBegin = false;
void API_COLOR(uint32_t) { words++; }
void API_COLOR_A(uint8_t) { words++; }
void API_BEGIN(int) { words++; inBegin = true; }
void API_END() { words++; inBegin = false; }
void API_LINE_WIDTH(int) { words++; }
void API_BLEND_FUNC(int, int) { words++; }
void API_VERTEX2F(int16_t x, int16_t y)
{
    words++; verts++;
    if (!inBegin) { printf("vertex outside BEGIN\n"); fails++; }
    if (x < -2 || x > 801 || y < 27 || y > 481) { printf("vertex off screen %d,%d\n", x, y); fails++; }
}
void API_CMD_TEXT(int16_t x, int16_t y, int16_t, uint16_t options, const char* s)
{
    if (inBegin) { printf("text inside BEGIN\n"); fails++; }
    int len = (int)strlen(s);
    const int cw = 9; // wide glyph, worst case
    int width = len * cw - 1;
    if (options & OPT_CENTERX) x -= width / 2;
    if (options & OPT_RIGHTX) x -= width;
    if (options & OPT_CENTERY) y -= 8;
    words += 5;
    for (int i = 0; i < len; i++)
    {
        words += (x >= 0 && x < 512 && y >= 0 && y < 512) ? 1 : 2;
        if (x < -4 || x > 800) { printf("text off screen: '%s' x=%d\n", s, x); fails++; break; }
        x += cw;
    }
}

static Sequencer::strPattern pat;
static int render(cDisplayControl& c, const char* name, int limit)
{
    words = 0; verts = 0; inBegin = false;
    c.update();
    if (inBegin) { printf("%s: BEGIN left open\n", name); fails++; }
    if (verts % 2) { printf("%s: odd vertex count\n", name); fails++; }
    int est = draw::used();
    printf("%-34s %4d words (helper counted %4d)\n", name, words, est);
    if (words > limit) { printf("  OVER LIMIT %d\n", limit); fails++; }
    if (est < words) { printf("  helper under-counts\n"); fails++; }
    return words;
}

int main()
{
    const int kLimit = 1500; // whole frame may hold about 2040; title bar and popups need the rest
    for (auto& t : pat.track) { t.length = 63; }
    memset(&mtProject, 0, sizeof(mtProject));
    for (auto& i : mtProject.instrument) i.isActive = 1;
    sequencer.pattern = &pat; sequencer.playing = true;

    // worst case content: every track full
    for (int t = 0; t < 8; t++)
    {
        setTrackType(&pat, t, (t % 2) ? TrackType::Melodic : TrackType::Drum);
        for (int i = 0; i < kMaxNotes; i++)
        {
            Note n{}; n.start = (i % 64) * 48 + (i % 5); n.length = 20 + (i * 7) % 90; n.pitch = 40 + (i * 5) % 36;
            n.velocity = 30 + (i * 37) % 97; n.flags = (i % 2) ? kNoteFlagSelected : 0; n.setChance(i % 3 ? 100 : 50);
            addNote(&pat, t, n);
        }
    }

    BottomBar bar;
    static char bt[8][16];
    for (int i = 0; i < 8; i++) { snprintf(bt[i], 16, "G#10 m add9"); bar.text[i] = bt[i]; bar.fill[i] = 0x3FB8AF; bar.highlighted[i] = true; }
    ParamBox pb[4];
    for (int i = 0; i < 4; i++) { snprintf(pb[i].label, 10, "Nudge"); snprintf(pb[i].value, 10, "-100%%"); pb[i].color = 0x4CD964; }

    for (int zoomSteps : { 4, 16, 32 })
    {
        LaneView lv; lv.pattern = &pat; lv.stepsVisible = zoomSteps; lv.patternTicks = 64 * 48; lv.firstTick = 48 * 8;
        lv.cursorTrack = 7; lv.cursorTick = 48 * 9; lv.playheadTick = 48 * 10 + 7; lv.shift = true;
        for (int t = 0; t < 8; t++) { snprintf(lv.lanes[t].name, 16, "%d WWWWWWWWW", t + 1); lv.lanes[t].melodic = t % 2; lv.lanes[t].muted = false; lv.lanes[t].sounding = true; }
        snprintf(lv.title, sizeof(lv.title), "Pat 255   Track 8   Step 128/128"); snprintf(lv.mode, 12, "REC"); lv.modeColor = 0xFF0000;
        memcpy(lv.params, pb, sizeof(pb)); lv.bar = bar;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &lv;
        LaneControl lc(&prop);
        char nm[40]; snprintf(nm, 40, "main sequencer, %d steps, full", zoomSteps);
        render(lc, nm, kLimit);
    }

    for (int zoom : { -12, 0, 24, 48 })
    {
        PianoRollView pv; pv.pattern = &pat; pv.track = 1;
        pv.stepsVisible = 16 + zoom < 4 ? 4 : (16 + zoom > 64 ? 64 : 16 + zoom);
        pv.rowsVisible = 12 + zoom < 4 ? 4 : (12 + zoom > 36 ? 36 : 12 + zoom);
        pv.patternTicks = 64 * 48; pv.firstTick = 0; pv.topPitch = 76; pv.cursorTick = 48 * 2; pv.cursorPitch = 60;
        pv.ghostLength = 400; pv.ghostCount = 6; for (int i = 0; i < 6; i++) pv.ghostPitches[i] = 50 + i * 4;
        pv.boxActive = true; pv.boxTick1 = 0; pv.boxTick2 = 48 * 3; pv.boxPitch1 = 70; pv.boxPitch2 = 50;
        pv.playheadTick = 100; pv.shift = true;
        snprintf(pv.title, sizeof(pv.title), "G#10 m add9  Step 128  Len 127.99"); snprintf(pv.mode, 12, "MOVE 125"); pv.modeColor = 0xFFB347;
        memcpy(pv.params, pb, sizeof(pb)); pv.bar = bar;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &pv;
        PianoRollControl pc(&prop);
        char nm[40]; snprintf(nm, 40, "piano roll, %dx%d, full", pv.stepsVisible, pv.rowsVisible);
        render(pc, nm, kLimit);
    }

    {
        TableView tv; tv.rows = 12; tv.cols = 2; tv.colX[0] = 24; tv.colX[1] = 300; tv.cursorRow = 11; tv.cursorCol = 1; tv.shift = true;
        for (int r = 0; r < 12; r++) { snprintf(tv.cell[r][0], 20, "Pattern length WW"); snprintf(tv.cell[r][1], 20, "WWWWWWWWWWWWWWWWWWW"); tv.rowColor[r] = 0xB46CFF; }
        snprintf(tv.title, sizeof(tv.title), "Track 8 settings   (125 notes)"); tv.bar = bar;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &tv;
        TableControl tc(&prop);
        render(tc, "track page", kLimit);

        TableView pv2; pv2.rows = 8; pv2.cols = 5; int cx[5] = { 24, 130, 240, 400, 540 };
        const char* hd[5] = { "Button", "Root", "Type", "Inversion", "Voicing" };
        for (int c = 0; c < 5; c++) { pv2.colX[c] = cx[c]; pv2.header[c] = hd[c]; }
        for (int r = 0; r < 8; r++) for (int c = 0; c < 5; c++) snprintf(pv2.cell[r][c], 20, c == 0 ? "8" : (c == 1 ? "G#10" : (c == 2 ? "m add9" : (c == 3 ? "4" : "Drop 2"))));
        pv2.cursorRow = 7; pv2.cursorCol = 4; pv2.shift = true; pv2.bar = bar;
        snprintf(pv2.title, sizeof(pv2.title), "Chord palette   Bank B   (plays on track 8)");
        prop.data = &pv2;
        TableControl tc2(&prop);
        render(tc2, "chord palette page", kLimit);
    }

    printf(fails ? "UI TEST FAILED: %d\n" : "ALL SCREEN TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
