#include "gibgob/LaneControl.h"
#include "gibgob/PianoRollControl.h"
#include "gibgob/TableControl.h"
#include "gibgob/DrumSeqControl.h"
#include "gibgob/PerformControl.h"
#include "gibgob/VisionControl.h"
#include "gibgob/PerformFx.h"
#include <algorithm>
#include "gibgob/gibgobHelp.h"
#include "gibgob/MelodicTrack.h"
#include "FT812.h"
#include "display.h"
#include "core/interface.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "interruptsControl.h"
#include "fileManager.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
using namespace gibgob;

FileManagerStub newFileManager;
ModuleBase instrumentEditor;
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
static int beginPrim = 0;
static uint32_t lastColor = 0; static int borderRects = 0;
void API_COLOR(uint32_t col) { words++; lastColor = col; }
void API_COLOR_A(uint8_t) { words++; }
void API_BEGIN(int prim) { words++; inBegin = true; beginPrim = prim; }
void API_END() { words++; inBegin = false; }
void API_LINE_WIDTH(int) { words++; }
void API_CMD_SETBITMAP(uint32_t, uint16_t, uint16_t, uint16_t) { words += 5; }
void API_BLEND_FUNC(int, int) { words++; }
void API_VERTEX2F(int16_t x, int16_t y)
{
    words++;
    if (beginPrim == RECTS) verts++; // rectangles take vertices in pairs (a bitmap takes one)
    if (lastColor == 0xB85548 || lastColor == 0xC97F75) borderRects++; // plain, or lightened by Shift
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
static int render(cDisplayControl& c, const char* name, int limit, bool expectBorder = true)
{
    borderRects = 0;
    words = 0; verts = 0; inBegin = false;
    c.update();
    if (inBegin) { printf("%s: BEGIN left open\n", name); fails++; }
    // the Shift border (4 rectangles, drawn last) must always survive
    if (expectBorder && borderRects < 8) { printf("%s: shift border missing (%d)\n", name, borderRects); fails++; }
    if (verts % 2) { printf("%s: odd vertex count\n", name); fails++; }
    int est = draw::used();
    printf("%-34s %4d words (helper counted %4d)\n", name, words, est);
    if (words > limit) { printf("  OVER LIMIT %d\n", limit); fails++; }
    if (est < words) { printf("  helper under-counts\n"); fails++; }
    if (draw::skipped() > 0 && strstr(name, "full") == nullptr) { printf("  %d drawing calls dropped\n", draw::skipped()); fails++; }
    if (draw::skipped() > 0) printf("  (%d note details dropped to stay inside the limit)\n", draw::skipped());
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

    // Gibbon vision: every track full, everything flashing, every tag shown
    {
        VisionView vv;
        for (int t = 0; t < 8; t++)
        {
            VisionTile& tile = vv.tiles[t];
            snprintf(tile.name, sizeof(tile.name), "%d WWWWWWWWW", t + 1);
            snprintf(tile.info, sizeof(tile.info), "128 st  bars 5-8");
            tile.color = (t % 2) ? 0x5F53B8 : 0xB85548;
            tile.muted = (t == 3);
            tile.soloed = (t == 5);
            tile.flash = true;
            tile.playX = 150;
            buildVisionTile(&pat, t, 48 * 7, tile);
            if (tile.count > VisionTile::kMaxMarks) { printf("  vision tile over its marks\n"); fails++; }
        }
        snprintf(vv.title, sizeof(vv.title), "Pattern 255   Bar 128.4");
        memcpy(vv.params, pb, sizeof(pb)); vv.bar = bar; vv.shift = true; vv.cursor = 4;
        vv.pictureHint = "/GibGob/Vision/1.jpg + 2.jpg";
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &vv;
        VisionControl vc(&prop);
        render(vc, "gibbon vision, no picture", kLimit);
        vv.pictureLoaded = true; vv.frameW = kVisionTileW; vv.frameH = kVisionTileH; vv.frameAddress[0] = 350000; vv.frameAddress[1] = 410000;
        render(vc, "gibbon vision, picture", kLimit);
    }
    // a 1-step-long hit on a 16-step drum track shows four times in the 4 bars
    {
        static Sequencer::strPattern one;
        for (auto& t : one.track) { t.length = 15; }
        setTrackType(&one, 0, TrackType::Drum);
        Note n{}; n.start = 0; n.length = 48; n.pitch = 60; n.velocity = 100;
        addNote(&one, 0, n);
        VisionTile tile;
        buildVisionTile(&one, 0, 0, tile);
        bool ok = tile.count == 4;
        for (int i = 0; ok && i < 4; i++)
            ok = tile.marks[i].x0 == i * 64;
        if (!ok) { printf("  vision: a 16-step track does not repeat across the 4 bars (%d marks)\n", tile.count); fails++; }
        // starting 4 steps in: the hit is 12 steps later
        buildVisionTile(&one, 0, 4 * 48, tile);
        if (tile.count != 4 || tile.marks[0].x0 != 48) { printf("  vision: window start not followed (%d)\n", tile.count ? tile.marks[0].x0 : -1); fails++; }
    }

    for (int zoomSteps : { 4, 16, 32 })
    {
        LaneView lv; lv.pattern = &pat; lv.stepsVisible = zoomSteps; lv.patternTicks = 64 * 48; lv.firstTick = 48 * 8;
        lv.cursorTrack = 7; lv.cursorTick = 48 * 9; lv.shift = true; lv.nameCursor = zoomSteps == 16;
        for (int t = 0; t < 8; t++) { snprintf(lv.lanes[t].name, 16, "%d WWWWWWWWW", t + 1); lv.lanes[t].melodic = t % 2; lv.lanes[t].slice = (t == 4); lv.lanes[t].muted = false; lv.lanes[t].sounding = true;
            lv.lanes[t].steps = 12 + t * 7; lv.lanes[t].playhead = 48 * 9 + t * 11; }
        snprintf(lv.title, sizeof(lv.title), "Pat 255   Track 8   Step 128/128"); snprintf(lv.mode, 12, "REC"); lv.modeColor = 0xFF0000;
        memcpy(lv.params, pb, sizeof(pb)); lv.bar = bar;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &lv;
        LaneControl lc(&prop);
        char nm[40]; snprintf(nm, 40, "main sequencer, %d steps, full", zoomSteps);
        render(lc, nm, kLimit);
        if (zoomSteps == 16)
        {
            lv.welcome.active = true; lv.welcome.choice = 2; lv.shift = false; // full-screen window, no Shift border
            render(lc, "welcome window", kLimit, false);
            lv.welcome.active = false;
        }
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
        {
            // slice editor: 48 slice rows, labels and button markers
            PianoRollView sv = pv; sv.sliceCount = 48; sv.topPitch = 24 + 47; sv.cursorPitch = 40;
            for (int b = 0; b < 8; b++) sv.sliceButton[b] = (int8_t)(b * 6);
            prop.data = &sv; PianoRollControl sc(&prop);
            char sn[40]; snprintf(sn, 40, "slice editor, %dx%d, full", sv.stepsVisible, sv.rowsVisible);
            render(sc, sn, kLimit);
            prop.data = &pv;
        }
        if (zoom == 48)
        {
            // menus and the note picker over the fullest piano roll
            pv.menu.active = true; pv.menu.cols = 1; pv.menu.rows = 8; pv.menu.cursor = 3;
            snprintf(pv.menu.title, 48, "Arp  -  track 8");
            snprintf(pv.menu.hint, 64, "Plays live. 7: Apply as notes  6: Keep arp  8: Back");
            for (int r = 0; r < 8; r++) { snprintf(pv.menu.row[r].label, 20, "Ape: rhythmWW"); snprintf(pv.menu.row[r].value, 20, "On  (Middle: new)"); pv.menu.row[r].isAction = r == 4; }
            render(pc, "arp menu over full roll", kLimit);
            pv.menu.cols = 2; pv.menu.rows = 1; pv.menu.cursor = -1; pv.menu.colCursor = 1;
            snprintf(pv.menu.colTitle[0], 14, "Quantize"); snprintf(pv.menu.colTitle[1], 14, "Humanize");
            render(pc, "quantize menu over full roll", kLimit);
            pv.menu.active = false; pv.picker.active = true; pv.picker.firstPitch = 92; pv.picker.cursor = 127;
            for (int b = 0; b < 16; b++) pv.picker.mask[b] = 0xAA;
            render(pc, "note picker", kLimit);
        }
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

    {
        // perform page: every cell lit, longest names and settings
        PerformView pv; pv.cursor = 7; pv.shift = true; pv.bar = bar;
        for (int i = 0; i < 8; i++)
        {
            PerformView::CellView& c = pv.cells[i];
            snprintf(c.name, sizeof(c.name), "Filter sweep");
            // the longest real setting line ("Pattern  Trance 6"); checked below
            for (int l = 0; l < 3; l++) snprintf(c.line[l], sizeof(c.line[l]), "Pattern  Trance 6");
            c.color = performFxColor(i + 1); c.active = (i % 2) == 0; c.off = false;
        }
        snprintf(pv.title, sizeof(pv.title), "Perform  -  button 8");
        memcpy(pv.params, pb, sizeof(pb)); pv.selectedParam = 3;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &pv;
        // no real setting line is longer than the one tested
        size_t longest = 0;
        for (int fx = 1; fx < gibgob::perform::kFxCount; fx++)
        {
            const gibgob::perform::FxInfo& in = gibgob::perform::fxInfo(fx);
            if (strlen(in.name) > 12) { printf("  effect name too long: %s\n", in.name); fails++; }
            for (int k = 0; k < in.paramCount; k++)
                for (int v = 0; v < in.params[k].count; v++)
                {
                    char val[16]; gibgob::perform::paramText(fx, k, v, val, sizeof(val));
                    longest = std::max(longest, strlen(in.params[k].name) + 2 + strlen(val));
                }
        }
        if (longest > strlen("Pattern  Trance 6")) { printf("  a setting line is longer than tested (%zu)\n", longest); fails++; }
        PerformControl pfc(&prop);
        render(pfc, "perform page", kLimit);
        pv.help.active = true; pv.help.title = "Perform - shortcuts";
        pv.help.lines = kHelpPerform; pv.help.count = (int)(sizeof(kHelpPerform) / sizeof(kHelpPerform[0]));
        words = 0; verts = 0; pfc.update();
        printf("%-34s %4d words\n", "help: perform", words);
        if (words > kLimit || draw::skipped()) { printf("  help too big\n"); fails++; }
    }

    {
        // drum sequencer, every cell busy
        DrumSeqView dv; dv.firstRow = 2; dv.cursorStep = 23; dv.playStep = 20; dv.steps = 128; dv.shift = true;
        dv.rows = 4;
        for (int k = 0; k < 32; k++) { DrumCell& cl = dv.cell[k]; cl.exists = true; cl.on = true; cl.velocity = 30 + k * 3; cl.chance = 55; cl.nudge = (k % 5) - 2; snprintf(cl.pitch, 6, "G#10"); }
        snprintf(dv.title, sizeof(dv.title), "Step 128/128   (empty)"); snprintf(dv.mode, 12, "REC");
        memcpy(dv.params, pb, sizeof(pb)); dv.bar = bar;
        strControlProperties prop; prop.y = 29; prop.w = 800; prop.h = 451; prop.data = &dv;
        DrumSeqControl dc(&prop);
        render(dc, "drum sequencer", kLimit);
        // zoomed out: 8 rows of 8, every step on with all its marks
        dv.rows = 8; dv.firstRow = 0;
        for (int k = 0; k < 64; k++) { DrumCell& cl = dv.cell[k]; cl.exists = true; cl.on = true; cl.velocity = 30 + k; cl.chance = 55; cl.nudge = (k % 5) - 2; snprintf(cl.pitch, 6, "G#10"); }
        render(dc, "drum sequencer, 8 rows", kLimit);
        dv.rows = 2;
        render(dc, "drum sequencer, 2 rows", kLimit);
        dv.rows = 4;
        // help windows
        dv.help.active = true; dv.help.title = "Main sequencer - shortcuts";
        dv.help.lines = kHelpMain; dv.help.count = (int)(sizeof(kHelpMain) / sizeof(kHelpMain[0]));
        words = 0; verts = 0; dc.update();
        printf("%-34s %4d words\n", "help: main sequencer", words);
        if (words > kLimit || draw::skipped()) { printf("  help too big\n"); fails++; }
        dv.help.lines = kHelpPianoRoll; dv.help.count = (int)(sizeof(kHelpPianoRoll) / sizeof(kHelpPianoRoll[0]));
        words = 0; dc.update();
        printf("%-34s %4d words\n", "help: piano roll", words);
        if (words > kLimit || draw::skipped()) { printf("  help too big\n"); fails++; }
        if ((int)(sizeof(kHelpMain) / sizeof(kHelpMain[0])) > 32 || (int)(sizeof(kHelpPianoRoll) / sizeof(kHelpPianoRoll[0])) > 32) { printf("  help list longer than 32 lines\n"); fails++; }
    }

    printf(fails ? "UI TEST FAILED: %d\n" : "ALL SCREEN TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
