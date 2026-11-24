#include "core/interfaceDefs.h"
#include "gibgob/PianoRollControl.h"
#include "gibgob/TableControl.h"
#include "gibgob/DrumSeqControl.h"
#include "gibgob/PerformControl.h"
#include "gibgob/VisionControl.h"
#include "gibgob/DrumOverviewControl.h"
#include "gibgob/PerformFx.h"
#include "gibgob/gibgobChordList.h"
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
void sendCC(uint8_t, uint8_t, uint8_t, bool) {}
void sendMidiNoteOff(uint8_t, uint8_t, uint8_t, bool) {}

// ---- exact word counting, like the firmware's display list capture ----
static int words = 0, fails = 0, verts = 0;
static bool inBegin = false;
static int g_minY = 27; // top of what a screen may draw on
static int beginPrim = 0;
static uint32_t lastColor = 0; static int borderRects = 0;
// optional picture of a frame (SVG) to compare with a design: set g_svg
static FILE* g_svg = nullptr;
static int g_alpha = 255, g_vx = 0, g_vy = 0, g_vn = 0;
static uint32_t g_bmpAddr = 0; static int g_bmpW = 0, g_bmpH = 0;
static const char* bitmapFile(uint32_t a)
{
    switch (a)
    {
        case 350000: return "frame1.jpg"; // vision frames / the singer pictures
        case 410000: return "frame2.jpg";
        case 450000: return "singer_sing.jpg";
        case 550000: return "duo.jpg";
        case 650000: return "duo_sing.jpg";
        default: return "logo.jpg";
    }
}
void API_COLOR(uint32_t col) { words++; lastColor = col; }
void API_COLOR_A(uint8_t a) { words++; g_alpha = a; }
// strips (lines, filled edges) and the scissor box, for the SVG picture
static int g_lineW = 16;
static int g_scX = 0, g_scY = 0, g_scW = 2048, g_scH = 2048;
struct Ctx { int scX, scY, scW, scH, lineW, alpha; uint32_t color; };
static Ctx g_ctxStack[8]; static int g_ctxN = 0;
static int g_strip[2 * 256]; static int g_stripN = 0; static int g_clipId = 0;
static void flushStrip()
{
    if (!g_svg || g_stripN < 2) { g_stripN = 0; return; }
    const int n = g_stripN;
    if (beginPrim == LINE_STRIP)
    {
        fprintf(g_svg, "<polyline fill='none' stroke='#%06X' stroke-opacity='%.2f' stroke-width='%.1f' stroke-linejoin='round' points='", lastColor & 0xFFFFFF,
                g_alpha / 255.0, g_lineW / 16.0 * 2);
        for (int i = 0; i < n; i++) fprintf(g_svg, "%d,%d ", g_strip[2 * i], g_strip[2 * i + 1]);
        fprintf(g_svg, "'/>\n");
    }
    else if (beginPrim == EDGE_STRIP_R || beginPrim == EDGE_STRIP_L || beginPrim == EDGE_STRIP_B)
    {
        const int x2 = g_scX + g_scW, y2 = g_scY + g_scH;
        fprintf(g_svg, "<clipPath id='c%d'><rect x='%d' y='%d' width='%d' height='%d'/></clipPath>", g_clipId, g_scX, g_scY, g_scW, g_scH);
        fprintf(g_svg, "<polygon clip-path='url(#c%d)' fill='#%06X' fill-opacity='%.2f' points='", g_clipId++, lastColor & 0xFFFFFF, g_alpha / 255.0);
        for (int i = 0; i < n; i++) fprintf(g_svg, "%d,%d ", g_strip[2 * i], g_strip[2 * i + 1]);
        const int xa = g_strip[0], ya = g_strip[1], xb = g_strip[2 * n - 2], yb = g_strip[2 * n - 1];
        if (beginPrim == EDGE_STRIP_R) fprintf(g_svg, "%d,%d %d,%d", x2 + 900, yb, x2 + 900, ya);
        else if (beginPrim == EDGE_STRIP_L) fprintf(g_svg, "%d,%d %d,%d", -900, yb, -900, ya);
        else fprintf(g_svg, "%d,%d %d,%d", xb, y2 + 900, xa, y2 + 900);
        fprintf(g_svg, "'/>\n");
    }
    g_stripN = 0;
}
void API_BEGIN(int prim) { words++; inBegin = true; beginPrim = prim; g_stripN = 0; }
void API_END() { words++; flushStrip(); inBegin = false; }
void API_LINE_WIDTH(int w) { words++; g_lineW = w; }
static int g_pointSize = 16;
void API_POINT_SIZE(uint16_t s) { words++; g_pointSize = s; }
void API_SAVE_CONTEXT() { words++; if (g_ctxN < 8) g_ctxStack[g_ctxN++] = { g_scX, g_scY, g_scW, g_scH, g_lineW, g_alpha, lastColor }; }
void API_RESTORE_CONTEXT()
{
    words++;
    if (g_ctxN <= 0) { printf("restore without save\n"); fails++; return; }
    Ctx c = g_ctxStack[--g_ctxN]; g_scX = c.scX; g_scY = c.scY; g_scW = c.scW; g_scH = c.scH; g_lineW = c.lineW; g_alpha = c.alpha; lastColor = c.color;
}
void API_SCISSOR_XY(uint16_t x, uint16_t y) { words++; g_scX = x; g_scY = y; }
void API_SCISSOR_SIZE(uint16_t w, uint16_t h) { words++; g_scW = w; g_scH = h; }
void API_CMD_GRADIENT(int16_t x0, int16_t y0, uint32_t c0, int16_t x1, int16_t y1, uint32_t c1)
{
    words += 27; // the most the firmware's capture writes for one
    if (inBegin) { printf("gradient inside BEGIN\n"); fails++; }
    if (g_svg)
    {
        fprintf(g_svg, "<linearGradient id='g%d' gradientUnits='userSpaceOnUse' x1='%d' y1='%d' x2='%d' y2='%d'><stop offset='0' stop-color='#%06X'/><stop offset='1' stop-color='#%06X'/></linearGradient>",
                g_clipId, x0, y0, x1, y1, c0 & 0xFFFFFF, c1 & 0xFFFFFF);
        fprintf(g_svg, "<rect x='%d' y='%d' width='%d' height='%d' fill='url(#g%d)'/>\n", g_scX, g_scY, g_scW, g_scH, g_clipId++);
    }
}
void API_CMD_SETBITMAP(uint32_t a, uint16_t, uint16_t w, uint16_t h) { words += 5; g_bmpAddr = a; g_bmpW = w; g_bmpH = h; }
static bool g_additive = false;
void API_BLEND_FUNC(int a, int b) { words++; g_additive = (a == ONE && b == ONE); }
void API_BITMAP_HANDLE(uint8_t) { words++; }
void API_BITMAP_SOURCE(uint32_t a) { words++; g_bmpAddr = a; }
void API_BITMAP_LAYOUT(uint8_t, uint16_t, uint16_t) { words++; }
void API_BITMAP_SIZE(uint8_t, uint8_t, uint8_t, uint16_t w, uint16_t h) { words++; g_bmpW = w; g_bmpH = h; }
void API_VERTEX2F(int16_t x, int16_t y)
{
    words++;
    if (beginPrim == RECTS) verts++; // rectangles take vertices in pairs (a bitmap takes one)
    if (beginPrim == LINE_STRIP || beginPrim == EDGE_STRIP_R || beginPrim == EDGE_STRIP_L || beginPrim == EDGE_STRIP_B)
        if (g_stripN < 256) { g_strip[2 * g_stripN] = x; g_strip[2 * g_stripN + 1] = y; g_stripN++; }
    if (g_svg && beginPrim == FTPOINTS)
        fprintf(g_svg, "<circle cx='%d' cy='%d' r='%.1f' fill='#%06X' fill-opacity='%.2f'/>\n", x, y, g_pointSize / 16.0, lastColor & 0xFFFFFF, g_alpha / 255.0);
    if (g_svg && beginPrim == RECTS)
    {
        if (g_vn == 0) { g_vx = x; g_vy = y; g_vn = 1; }
        else
        {
            const int r = g_lineW > 16 ? g_lineW / 16 : 0; // grown and rounded by the line width
            fprintf(g_svg, "<rect x='%d' y='%d' width='%d' height='%d' rx='%d' fill='#%06X' fill-opacity='%.2f'%s/>\n", std::min<int>(g_vx, x) - r,
                    std::min<int>(g_vy, y) - r, std::abs(x - g_vx) + 1 + 2 * r, std::abs(y - g_vy) + 1 + 2 * r, r, lastColor & 0xFFFFFF, g_alpha / 255.0,
                    g_additive ? " style='mix-blend-mode:plus-lighter'" : "");
            g_vn = 0;
        }
    }
    if (g_svg && beginPrim == BITMAPS && g_bmpAddr == 116000) // the Shift frame's stripes
        fprintf(g_svg, "<defs><pattern id='hz' width='16' height='16' patternUnits='userSpaceOnUse'><path d='M0 8 L8 0 L16 0 L0 16 Z M8 16 L16 8 L16 16 Z' fill='#%06X'/></pattern></defs>"
                       "<rect x='%d' y='%d' width='%d' height='%d' fill='url(#hz)'/>\n", lastColor & 0xFFFFFF, x, y, g_bmpW, g_bmpH);
    else if (g_svg && beginPrim == BITMAPS)
        fprintf(g_svg, "<image x='%d' y='%d' width='%d' height='%d' href='%s'/>\n", x, y, g_bmpW, g_bmpH, bitmapFile(g_bmpAddr));
    // the Shift look: the blue wash (2 corners) and the 6 striped edges
    if (lastColor == 0x1C1648 || (beginPrim == BITMAPS && g_bmpAddr == 116000)) borderRects++;
    if (!inBegin) { printf("vertex outside BEGIN\n"); fails++; }
    if (x < -2 || x > 801 || y < g_minY || y > 481) { printf("vertex off screen %d,%d\n", x, y); fails++; }
}
void API_CMD_TEXT(int16_t x, int16_t y, int16_t font, uint16_t options, const char* s)
{
    if (inBegin) { printf("text inside BEGIN\n"); fails++; }
    if (g_svg)
    {
        const char* anchor = (options & OPT_CENTERX) ? "middle" : ((options & OPT_RIGHTX) ? "end" : "start");
        const int fy = (options & OPT_CENTERY) ? y + 5 : y + 12;
        static const int kSize[5] = { 14, 17, 14, 17, 12 };
        const int fi = (font >= 26 && font <= 30) ? font - 26 : 1;
        fprintf(g_svg, "<text x='%d' y='%d' fill='#%06X' fill-opacity='%.2f' font-family='Liberation Sans, Arial' font-size='%d' font-weight='%s' text-anchor='%s'>%s</text>\n",
                x, (options & OPT_CENTERY) ? y + kSize[fi] * 3 / 8 : y + kSize[fi], lastColor & 0xFFFFFF, g_alpha / 255.0, kSize[fi], (fi == 2 || fi == 3) ? "bold" : "normal", anchor, s);
        (void)fy;
    }
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

#include "synth_screen.inc"

void gibgobMiddleEvent(uint8_t value);

// Middle opening a screen must not act on the new screen: the press (and
// the Mini's repeats of it while held) is ignored there until it is let go
#include "elapsedMillis.h"
static void testTopRowBounce()
{
    g_testMillis = 1000;
    bool ok = !gibgob::topRowBounce(2, buttonPress);
    g_testMillis = 1300;
    ok = ok && !gibgob::topRowBounce(2, buttonRelease);
    // the contact bounces as the finger lifts: press + release again within a few ms
    g_testMillis = 1305;
    ok = ok && gibgob::topRowBounce(2, buttonPress);
    g_testMillis = 1309;
    ok = ok && gibgob::topRowBounce(2, buttonRelease);
    // another button is not affected; a real press later is a press
    ok = ok && !gibgob::topRowBounce(3, buttonPress) && !gibgob::topRowBounce(3, buttonRelease);
    g_testMillis = 1500;
    ok = ok && !gibgob::topRowBounce(2, buttonPress) && !gibgob::topRowBounce(2, buttonRelease);
    if (!ok) { printf("top row: a bounce on release played again (or a real press was lost)\n"); fails++; }
    g_testMillis = 0;
}

static void testMiddleCarryOver()
{
    // Middle pressed: the event is noted first, then the old screen opens the new one
    gibgobMiddleEvent(buttonPress);
    gibgob::screenOpened();
    if (!gibgob::middleBlocked(buttonPress)) { printf("middle: the opening press reached the new screen\n"); fails++; }
    // held: the Mini sends it again as a press
    gibgobMiddleEvent(buttonHold);
    if (!gibgob::middleBlocked(buttonPress)) { printf("middle: a held repeat reached the new screen\n"); fails++; }
    // let go: still not acted on, and from now on it counts
    gibgobMiddleEvent(buttonRelease);
    if (!gibgob::middleBlocked(buttonRelease)) { printf("middle: the release reached the new screen\n"); fails++; }
    gibgobMiddleEvent(buttonPress);
    if (gibgob::middleBlocked(buttonPress)) { printf("middle: a new press was ignored\n"); fails++; }
    gibgobMiddleEvent(buttonRelease);
    if (gibgob::middleBlocked(buttonRelease)) { printf("middle: a new release was ignored\n"); fails++; }
    // a screen opened by another button, Middle up: nothing ignored
    gibgob::screenOpened();
    gibgobMiddleEvent(buttonPress);
    if (gibgob::middleBlocked(buttonPress)) { printf("middle: blocked without a carried-over press\n"); fails++; }
    gibgobMiddleEvent(buttonRelease);
    gibgob::middleBlocked(buttonRelease);
}

int main()
{
    const int kLimit = 1500; // whole frame may hold about 2040; title bar and popups need the rest
    testMiddleCarryOver();
    testTopRowBounce();
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
        snprintf(vv.patText, sizeof(vv.patText), "Pat 255"); snprintf(vv.title, sizeof(vv.title), "Track 8   Step 128/128");
        snprintf(vv.mode, sizeof(vv.mode), "Q REC"); vv.modeColor = 0xE0604F;
        memcpy(vv.params, pb, sizeof(pb)); vv.selectedParam = 2;
        vv.bar = bar; vv.shift = true; vv.cursor = 4;
        vv.pictureHint = "/GibGob/Vision/1.jpg + 2.jpg";
        strControlProperties prop; prop.y = 0; prop.w = 800; prop.h = 480; prop.data = &vv;
        VisionControl vc(&prop);
        render(vc, "main screen, no picture, full", kLimit);
        vv.pictureLoaded = true; vv.frameW = kVisionTileW; vv.frameH = kVisionTileH; vv.frameAddress[0] = 350000; vv.frameAddress[1] = 410000;
        render(vc, "main screen, picture, full", kLimit);
        vv.welcome.active = true; vv.welcome.choice = 2;
        render(vc, "welcome window", kLimit, false);
        vv.welcome.active = false;
        if (const char* out = getenv("VISION_SVG"))
        {
            // a picture of the frame, unlit (no Shift), playing
            vv.shift = false; vv.cursor = 4;
            static const char* const kNames[8] = { "1 Kick", "2 Snare", "3 Hats", "4 Perc", "5 Chords", "6 Bass", "7 Lead", "8 Break" };
            for (int t = 0; t < 8; t++)
            {
                snprintf(vv.tiles[t].name, sizeof(vv.tiles[t].name), "%s", kNames[t]);
                snprintf(vv.tiles[t].info, sizeof(vv.tiles[t].info), t == 4 || t == 6 ? "64 st  bars 1-4" : "16 st");
                vv.tiles[t].flash = (t == 0 || t == 2); vv.tiles[t].soloed = false; vv.tiles[t].muted = (t == 3);
            }
            snprintf(vv.patText, sizeof(vv.patText), "Pat 3"); snprintf(vv.title, sizeof(vv.title), "Track 2   Step 7/16");
            static const char* const kL[4] = { "Len", "Vel", "Snd", "Note" }; static const char* const kV[4] = { "16", "100", "snr_jun", "C3" };
            static const uint32_t kC[4] = { 0x53B886, 0xB8AA53, 0x7B6FD6, 0x5AC8FA };
            for (int i = 0; i < 4; i++) { snprintf(vv.params[i].label, 10, "%s", kL[i]); snprintf(vv.params[i].value, 10, "%s", kV[i]); vv.params[i].color = kC[i]; }
            vv.selectedParam = -1;
            // the main screen: the top row plays the tracks, recording is on
            static const char* const kTr[8] = { "Kick", "Snare", "Hats", "Perc", "Chords", "Bass", "Lead", "Break" };
            static const uint32_t kTrCol[8] = { 0xB85548, 0xB85548, 0xB85548, 0x3C3C42, 0x5F53B8, 0x5F53B8, 0x5F53B8, 0xD9A23A };
            for (int i = 0; i < 8; i++) { vv.bar.text[i] = kTr[i]; vv.bar.fill[i] = kTrCol[i]; vv.bar.highlighted[i] = (i == 0 || i == 2); }
            vv.cursor = 1; snprintf(vv.mode, sizeof(vv.mode), "REC");
            vv.pictureLoaded = false;
            g_svg = fopen(out, "w");
            fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
            words = 0; vc.update();
            fprintf(g_svg, "</svg>\n");
            fclose(g_svg); g_svg = nullptr;
            // Shift on: the top row is CHIMP OUT; FX1 held: BPM lit
            static const char* const kFx[8] = { "Roll 1/16", "Roll 1/32", "Stutter", "Filter", "Delay", "Reverb", "Halftime", "Vinyl" };
            static const uint32_t kFxCol[8] = { 0xE07B39, 0xE07B39, 0xD9A23A, 0x7B6FD6, 0x3FA7B5, 0x5AC8FA, 0x4FA3E0, 0xB8AA53 };
            for (int i = 0; i < 8; i++) { vv.bar.text[i] = kFx[i]; vv.bar.fill[i] = kFxCol[i]; vv.bar.highlighted[i] = (i == 2); }
            vv.shift = true; vv.mode[0] = 0;
            static const char* const kSL[4] = { "All len", "", "BPM", "Swing" }; static const char* const kSV[4] = { "16", "", "140", "54%" };
            for (int i = 0; i < 4; i++) { snprintf(vv.params[i].label, 10, "%s", kSL[i]); snprintf(vv.params[i].value, 10, "%s", kSV[i]); }
            vv.selectedParam = 2;
            char out2[256]; snprintf(out2, sizeof(out2), "%s.shift.svg", out);
            g_svg = fopen(out2, "w");
            fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
            words = 0; vc.update();
            fprintf(g_svg, "</svg>\n");
            fclose(g_svg); g_svg = nullptr;
            // the Shift contents without the Shift look (for trying other looks on top)
            vv.shift = false;
            snprintf(out2, sizeof(out2), "%s.shiftbase.svg", out);
            g_svg = fopen(out2, "w");
            fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
            words = 0; vc.update();
            fprintf(g_svg, "</svg>\n");
            fclose(g_svg); g_svg = nullptr;
            vv.shift = true;
        }
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
            // the Edit menu (Shift + Note): quantize / humanize and the tool grid; a retuned row lit
            PianoRollView ev = pv; ev.highlightPitch = 60; ev.highlightColor = 0x53B886;
            ev.menu.active = true; ev.menu.cols = 2; ev.menu.rows = 1; ev.menu.toolCount = 9; ev.menu.toolCursor = 3;
            snprintf(ev.menu.title, 48, "Edit  -  track 8");
            for (int t = 0; t < 9; t++) snprintf(ev.menu.tool[t], 12, "Sel all");
            for (int c = 0; c < 2; c++) { snprintf(ev.menu.colTitle[c], 14, "Humanize"); snprintf(ev.menu.row[c].label, 20, "Amount"); snprintf(ev.menu.row[c].value, 20, "10"); }
            snprintf(ev.menu.hint, 64, "Left/Right: column  Down: amount, tools  7: Apply");
            prop.data = &ev; PianoRollControl ec(&prop);
            char enm[48]; snprintf(enm, 48, "piano roll + Edit menu, %dx%d, full", pv.stepsVisible, pv.rowsVisible);
            render(ec, enm, kLimit);
            prop.data = &pv;
            // a singing synth: every note labelled with its syllable
            PianoRollView sv2 = pv; sv2.syllables = kSyllableNames; sv2.ghostSyllable = 3;
            prop.data = &sv2; PianoRollControl sc2(&prop);
            char snm[48]; snprintf(snm, 48, "piano roll + syllables, %dx%d, full", pv.stepsVisible, pv.rowsVisible);
            render(sc2, snm, kLimit);
            prop.data = &pv;
        }
        {
            // ghost notes on: every other melodic track full under this one
            PianoRollView gv = pv; gv.otherTracks = true;
            prop.data = &gv; PianoRollControl gc(&prop);
            char gn[40]; snprintf(gn, 40, "piano roll + ghosts, %dx%d, full", gv.stepsVisible, gv.rowsVisible);
            render(gc, gn, kLimit);
            prop.data = &pv;
        }
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
        {
            // Master FX page: 4 columns, the longest cells the page writes
            TableView mv; mv.rows = 6; mv.cols = 4; mv.cursorRow = 5; mv.cursorCol = 3; mv.shift = true;
            const char* mh[4] = { "Duck (kick)", "Glue", "Colour 1", "Colour 2" };
            for (int c = 0; c < 4; c++) { mv.colX[c] = 16 + c * 196; mv.header[c] = mh[c]; }
            for (int r = 0; r < 6; r++)
                for (int c = 0; c < 4; c++)
                    snprintf(mv.cell[r][c], 20, "%-9s %.9s", "Dropouts", "Cassette0");
            for (int r = 0; r < 12; r++) mv.rowColor[r] = 0xE07B39;
            snprintf(mv.title, sizeof(mv.title), "Pattern 255  (song's - edit to make own)"); snprintf(mv.mode, 12, "GR 12.5");
            mv.bar = bar;
            prop.data = &mv; TableControl mc(&prop);
            render(mc, "master fx page", kLimit);
            prop.data = &tv;
        }
        {
            // sample library: 10 rows of the longest cells, the 4 coloured boxes, a held one
            TableView lv; lv.rows = 10; lv.cols = 4; lv.cursorRow = 9; lv.cursorCol = 0;
            const char* lh[4] = { "Sample", "Genre", "Category", "Fav" };
            const int lx[4] = { 24, 430, 560, 700 };
            for (int c = 0; c < 4; c++) { lv.colX[c] = lx[c]; lv.header[c] = lh[c]; }
            for (int r = 0; r < 10; r++)
            {
                snprintf(lv.cell[r][0], 20, "WWWWWWWWWWWWWWWWWWW"); snprintf(lv.cell[r][1], 20, "Hiphop");
                snprintf(lv.cell[r][2], 20, "Snares"); snprintf(lv.cell[r][3], 20, "*"); lv.rowColor[r] = 0xFFFFFF;
            }
            snprintf(lv.title, sizeof(lv.title), "Track 8  Favourites > Vocals  (6000)"); snprintf(lv.mode, 12, "SCAN");
            ParamBox lp[4];
            const char* ll[4] = { "Genre", "Categ", "Fav", "Folders" };
            for (int k = 0; k < 4; k++) { snprintf(lp[k].label, sizeof(lp[k].label), "%s", ll[k]); snprintf(lp[k].value, sizeof(lp[k].value), "%s", "Hiphop"); lp[k].color = 0x39A0E0; }
            lv.params = lp; lv.selectedParam = 1;
            lv.bar = bar;
            prop.data = &lv; TableControl lc(&prop);
            render(lc, "sample library", kLimit, false); // Shift only swaps the top row here: no frame
        }
        {
            // drum overview: 1..8 tracks, every step a hit, every lock and fill tag shown, Shift
            static DrumOverviewView dv;
            static const char* const kNm[8] = { "kick", "snare", "hi-hat", "clap", "rim", "tom", "perc", "shaker" };
            auto fillAll = [&](int count, bool worst)
            {
                dv = DrumOverviewView();
                dv.count = count; dv.sel = count > 1 ? 1 : 0; dv.cursor = 2; dv.play = 4;
                for (int t = 0; t < count; t++)
                {
                    snprintf(dv.tracks[t].name, sizeof(dv.tracks[t].name), "%s", worst ? "WWWWWWWW" : kNm[t]);
                    dv.tracks[t].exists = 0xFF;
                    dv.tracks[t].hit = worst ? 0xFF : (uint8_t)(0x11 << (t % 4));
                    dv.tracks[t].fillOnly = worst ? 0x0F : 0x40;
                    dv.tracks[t].noFill = worst ? 0xF0 : 0x00;
                    dv.tracks[t].muted = worst && t == 6;
                }
                snprintf(dv.labelTop[0], 10, "prob"); snprintf(dv.labelTop[1], 10, "vel");
                snprintf(dv.labelBot[0], 10, "nudge"); snprintf(dv.labelBot[1], 10, "pitch");
                for (int st = 0; st < 8; st++)
                    if (worst || st == 2)
                    {
                        snprintf(dv.lockTop[st][0], 8, worst ? "NO FIL" : "1:2"); snprintf(dv.lockTop[st][1], 8, worst ? "127" : "100");
                        snprintf(dv.lockBot[st][0], 8, worst ? "-24" : "-3"); snprintf(dv.lockBot[st][1], 8, worst ? "C#10" : "+2");
                    }
                static const char* const kCl[4] = { "prob", "vel", "nudge", "pitch" };
                for (int k = 0; k < 4; k++) { snprintf(dv.cloudLabel[k], 10, "%s", kCl[k]); snprintf(dv.cloudValue[k], 10, "%s", worst ? "NO FILL" : (k == 0 ? "1:2" : "96")); }
                snprintf(dv.lengthText, 12, worst ? "128 steps" : "64 steps"); snprintf(dv.pageText, 8, worst ? "16/16" : "1/8");
                dv.bar = bar;
            };
            strControlProperties dp; dp.y = 0; dp.w = 800; dp.h = 480;
            dp.data = &dv;
            DrumOverviewControl dc2(&dp);
            g_minY = 0; // this screen draws its own header (no title bar)
            for (int count : { 1, 2, 4, 8 })
            {
                fillAll(count, true);
                char nm[48]; snprintf(nm, sizeof(nm), "drum overview, %d tracks, full", count);
                render(dc2, nm, kLimit, false);
                dv.shift = true; dv.fillOn = true; dv.cloudLit = 2;
                snprintf(nm, sizeof(nm), "drum overview, %d tracks, shift, full", count);
                render(dc2, nm, kLimit, true);
            }
            fillAll(4, true); dv.cursor = -1; // the cursor on the track name
            render(dc2, "drum overview, cursor on the name, full", kLimit, false);
            fillAll(4, false); snprintf(dv.message, sizeof(dv.message), "No drum tracks in this pattern");
            render(dc2, "drum overview, no drum tracks", kLimit, false);
            dv.help.active = true; dv.help.lines = kHelpDrumOverview; dv.help.count = (int)(sizeof(kHelpDrumOverview) / sizeof(kHelpDrumOverview[0])); dv.help.title = "Drum overview - shortcuts";
            words = 0; dc2.update(); // help: checked like the other help lists (it starts its own count)
            printf("%-34s %4d words\n", "help: drum overview", words);
            if (words > kLimit || draw::skipped()) { printf("  help too big\n"); fails++; }
            dv.help.active = false;
            if (const char* out = getenv("DRUM_SVG"))
            {
                // pictures: 4 tracks as in the design, 8 tracks, 2 tracks, Shift + fill
                const int counts[4] = { 4, 8, 2, 4 };
                for (int k = 0; k < 4; k++)
                {
                    fillAll(counts[k], false);
                    if (k == 3) { dv.shift = true; dv.fillOn = true; dv.cloudLit = 0; dv.tracks[1].noFill = 0x10; dv.tracks[1].hit |= 0x10; }
                    char path[256]; snprintf(path, sizeof(path), "%s.%d.svg", out, k);
                    g_svg = fopen(path, "w");
                    fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
                    words = 0; dc2.update();
                    fprintf(g_svg, "</svg>\n");
                    fclose(g_svg); g_svg = nullptr;
                }
            }
            g_minY = 27;
            prop.data = &tv;
        }

        TableView pv2; pv2.rows = 8; pv2.cols = 5; int cx[5] = { 24, 130, 240, 400, 540 };
        const char* hd[5] = { "Button", "Root", "Type", "Inversion", "Voicing" };
        for (int c = 0; c < 5; c++) { pv2.colX[c] = cx[c]; pv2.header[c] = hd[c]; }
        for (int r = 0; r < 8; r++) for (int c = 0; c < 5; c++) snprintf(pv2.cell[r][c], 20, c == 0 ? "8" : (c == 1 ? "G#10" : (c == 2 ? "m add9" : (c == 3 ? "4" : "Drop 2"))));
        pv2.cursorRow = 7; pv2.cursorCol = 4; pv2.shift = true; pv2.bar = bar;
        snprintf(pv2.title, sizeof(pv2.title), "Chord palette   Bank B   (plays on track 8)");
        prop.data = &pv2;
        TableControl tc2(&prop);
        render(tc2, "chord palette page", kLimit);
        // the Chord Canopy: each way of playing; the busiest case is a wide
        // 8-note chord, all sounding, with the arp's 8 values and Shift on
        {
            static CanopyView cv;
            TableView ct; ct.canopy = &cv; ct.shift = true;
            prop.data = &ct;
            TableControl cc(&prop);
            snprintf(cv.title, sizeof(cv.title), "Track 8");
            snprintf(cv.chordName, sizeof(cv.chordName), "G#m maj9 add11");
            cv.chordIndex = 7;
            const uint8_t wide[8] = { 36, 43, 49, 56, 61, 66, 70, 71 };
            for (int i = 0; i < 8; i++) { cv.notes[i] = wide[i]; cv.sounding[i] = wide[i]; }
            cv.noteCount = 8; cv.soundingCount = 8;
            static const char* const kModes[4] = { "Chord", "Strum", "Arp", "Human" };
            static const uint32_t kCols[4] = { 0x53B886, 0xB8AA53, 0x7B6FD6, 0x5AC8FA };
            for (int m = 0; m < 4; m++) { snprintf(cv.params[m].label, 10, "%s", kModes[m]); snprintf(cv.params[m].value, 10, "%s", m == 0 ? "all" : (m == 1 ? "10" : (m == 2 ? "1/16 T" : "5/5"))); cv.params[m].color = kCols[m]; }
            static const char* const kLabels[8] = { "Root", "Type", "Inversion", "Voicing", "Mode", "Rate", "Octaves", "Gate" };
            static const char* const kValues[8] = { "G#10", "m maj9", "4", "Drop 2", "As placed", "1/16 T", "4", "100%" };
            for (int i = 0; i < 8; i++)
            {
                snprintf(cv.fields[i].label, 10, "%s", kLabels[i]); snprintf(cv.fields[i].value, 12, "%s", kValues[i]);
                cv.fields[i].color = 0x7B6FD6; cv.fields[i].meter = 3; cv.fields[i].meterMax = (i == 6 ? 4 : 5);
            }
            for (int i = 0; i < 8; i++) { cv.bar.text[i] = "G#m maj9"; cv.bar.fill[i] = 0x2A7340; cv.bar.highlighted[i] = (i == 7); }
            for (int mode = 0; mode < 4; mode++)
            {
                cv.mode = mode; cv.fieldCount = mode == 0 ? 4 : (mode == 2 ? 8 : 6); cv.cursor = cv.fieldCount - 1;
                char name[48]; snprintf(name, sizeof(name), "chord canopy, %s", kModes[mode]);
                render(cc, name, kLimit);
                if (mode == 2 && getenv("PALETTE_SVG2"))
                {
                    g_svg = fopen(getenv("PALETTE_SVG2"), "w");
                    fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
                    render(cc, "chord canopy, Arp (svg)", kLimit);
                    fprintf(g_svg, "</svg>\n");
                    fclose(g_svg); g_svg = nullptr;
                }
            }
            // a 2-octave chord, nothing sounding, cursor on a chord value
            const uint8_t triad[3] = { 60, 63, 67 };
            for (int i = 0; i < 3; i++) cv.notes[i] = triad[i];
            cv.noteCount = 3; cv.soundingCount = 1; cv.sounding[0] = 63; cv.mode = 1; cv.fieldCount = 6; cv.cursor = 0; ct.shift = false;
            snprintf(cv.chordName, sizeof(cv.chordName), "Cm"); cv.chordIndex = 2;
            snprintf(cv.fields[4].label, 10, "Speed"); snprintf(cv.fields[4].value, 12, "7"); cv.fields[4].meter = 4; cv.fields[4].color = 0xB8AA53;
            snprintf(cv.fields[5].label, 10, "Direction"); snprintf(cv.fields[5].value, 12, "Alt"); cv.fields[5].meterMax = 0; cv.fields[5].color = 0xB8AA53;
            for (int i = 0; i < 4; i++) { cv.fields[i].meterMax = 0; cv.fields[i].color = 0xFFC83A; }
            render(cc, "chord canopy, triad", kLimit, false);
            int lowC, oct;
            canopyKeyRange(wide, 8, lowC, oct); if (lowC != 36 || oct != 3) { printf("  canopy range wrong %d %d\n", lowC, oct); fails++; }
            canopyKeyRange(triad, 3, lowC, oct); if (lowC != 60 || oct != 2) { printf("  canopy range wrong %d %d\n", lowC, oct); fails++; }
            const uint8_t top[2] = { 120, 127 };
            canopyKeyRange(top, 2, lowC, oct); if (lowC + 12 * oct > 128) { printf("  canopy range past 127\n"); fails++; }
            if (const char* out = getenv("PALETTE_SVG"))
            {
                g_svg = fopen(out, "w");
                fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
                render(cc, "chord canopy (svg)", kLimit, false);
                fprintf(g_svg, "</svg>\n");
                fclose(g_svg); g_svg = nullptr;
            }
            // the chord list open: every row full (the longest names), Shift on and off
            {
                cv.listOpen = true;
                cv.listCount = kChordListCount;
                snprintf(cv.listKey, sizeof(cv.listKey), "Db");
                cv.listShown = CanopyView::kListRows;
                cv.listIndex = 4; cv.listCursor = 4;
                for (int i = 0; i < CanopyView::kListRows; i++)
                {
                    CanopyView::ListRow& r = cv.listRows[i];
                    const ChordListEntry& e = kChordList[(i * 7 + 2) % kChordListCount]; // a mix of them
                    snprintf(r.name, sizeof(r.name), "%s", e.name);
                    r.style = e.style;
                    // the widest key of this progression (most letters)
                    size_t widest = 0;
                    for (int key = 0; key < 12; key++)
                    {
                        char line[64];
                        int len = 0;
                        for (int k = 0; k < 8; k++)
                        {
                            char nm[12];
                            chordListShortName(key + e.root[k], e.type[k], nm, sizeof(nm));
                            len += snprintf(line + len, sizeof(line) - (size_t)len, k ? " %s" : "%s", nm);
                        }
                        if (strlen(line) > widest) { widest = strlen(line); snprintf(r.chords, sizeof(r.chords), "%s", line); }
                    }
                    // the row must fit before the panel's right edge (~9 px a letter, bold ~11)
                    if (302 + (int)strlen(r.chords) * 9 > 787) { printf("  chord list row too wide: %s\n", r.chords); fails++; }
                    if (124 + (int)strlen(r.name) * 11 > 296) { printf("  chord list name too wide: %s\n", r.name); fails++; }
                }
                snprintf(cv.title, sizeof(cv.title), "Track 8   Insert: close the list");
                ct.shift = true;
                render(cc, "chord canopy, chord list, shift", kLimit);
                ct.shift = false;
                render(cc, "chord canopy, chord list", kLimit, false);
                if (const char* out = getenv("PALETTE_SVG3"))
                {
                    // the real list for the picture
                    for (int i = 0; i < CanopyView::kListRows; i++)
                    {
                        CanopyView::ListRow& r = cv.listRows[i];
                        const ChordListEntry& e = kChordList[i];
                        int len = 0;
                        for (int k = 0; k < 8; k++)
                        {
                            char nm[12];
                            chordListShortName(1 + e.root[k], e.type[k], nm, sizeof(nm));
                            len += snprintf(r.chords + len, sizeof(r.chords) - (size_t)len, k ? " %s" : "%s", nm);
                        }
                    }
                    g_svg = fopen(out, "w");
                    fprintf(g_svg, "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'>\n");
                    render(cc, "chord list (svg)", kLimit, false);
                    fprintf(g_svg, "</svg>\n");
                    fclose(g_svg); g_svg = nullptr;
                }
                cv.listOpen = false;
            }
            prop.data = &pv2;
    }
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
        {
            // every other help list fits too
            struct H { const char* name; const char* const* lines; int count; };
            const H lists[] = { { "help: drum", kHelpDrum, (int)(sizeof(kHelpDrum) / sizeof(kHelpDrum[0])) },
                                { "help: track page", kHelpTrackPage, (int)(sizeof(kHelpTrackPage) / sizeof(kHelpTrackPage[0])) },
                                { "help: palette", kHelpPalette, (int)(sizeof(kHelpPalette) / sizeof(kHelpPalette[0])) },
                                { "help: library", kHelpLibrary, (int)(sizeof(kHelpLibrary) / sizeof(kHelpLibrary[0])) },
                                { "help: master fx", kHelpMasterFx, (int)(sizeof(kHelpMasterFx) / sizeof(kHelpMasterFx[0])) },
                                { "help: synth editor", kHelpSynth, (int)(sizeof(kHelpSynth) / sizeof(kHelpSynth[0])) } };
            for (const H& h : lists)
            {
                dv.help.lines = h.lines; dv.help.count = h.count;
                words = 0; verts = 0; dc.update();
                printf("%-34s %4d words\n", h.name, words);
                if (words > kLimit || draw::skipped() || h.count > 32) { printf("  help too big\n"); fails++; }
            }
        }
        if ((int)(sizeof(kHelpMain) / sizeof(kHelpMain[0])) > 32 || (int)(sizeof(kHelpPianoRoll) / sizeof(kHelpPianoRoll[0])) > 32) { printf("  help list longer than 32 lines\n"); fails++; }
    }

    synthTests(kLimit);
    printf(fails ? "UI TEST FAILED: %d\n" : "ALL SCREEN TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
