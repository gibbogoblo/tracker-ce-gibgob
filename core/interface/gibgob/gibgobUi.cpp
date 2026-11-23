#include "gibgob/gibgobUi.h"

#include "FT812.h"
#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/modulesBase.h"
#include "elapsedMillis.h"
#include "fileManager.h"
#include "gibgob/MelodicTrack.h"
#include "instrumentEditor/instrumentEditor.h"
#include "mtStructs.h"

#include <stdio.h>

#include <string.h>

namespace gibgob
{

bool g_shiftLatched = false;
bool g_recArmed = false;

static elapsedMillis s_uiClock;
static uint32_t s_shiftPressedAt = 0;
static uint32_t s_lastToggleAt = 0;
static bool s_shiftDown = false;

bool shiftActive()
{
    return g_shiftLatched;
}

bool shiftButton(uint8_t state)
{
    constexpr uint32_t kTapMs = 400;      // longer = held for something else
    constexpr uint32_t kDebounceMs = 250; // ignore the button's double click
    uint32_t now = (uint32_t)s_uiClock;

    if (state == buttonPress)
    {
        if (now - s_lastToggleAt < kDebounceMs)
            return false; // bounce of the last press
        s_shiftDown = true;
        s_shiftPressedAt = now;
        return false;
    }
    if (state == buttonRelease && s_shiftDown)
    {
        s_shiftDown = false;
        if (now - s_shiftPressedAt < kTapMs)
        {
            g_shiftLatched = !g_shiftLatched;
            s_lastToggleAt = now;
            return true;
        }
    }
    return false;
}

// A screen opened by a Middle press must not act on that same press: the
// Mini repeats a held Middle as more presses, which would land on the new
// screen (the library loaded the sample under its cursor right away).
static bool s_middleBlocked = false;

void screenOpened()
{
    g_shiftLatched = false;
    s_shiftDown = false;
    s_middleBlocked = mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonMiniMiddle);
}

bool middleBlocked(uint8_t state)
{
    if (!s_middleBlocked)
        return false;
    if (state == buttonRelease)
        s_middleBlocked = false; // let go: from the next press on it counts
    return true;
}

static int s_soundTrack = 0;

int soundParametersTrack()
{
    const Sequencer::strPattern* p = current();
    const int instr = mtProject.values.lastUsedInstrument;
    if (getSound(p, s_soundTrack) == instr)
        return s_soundTrack;
    for (int t = 0; t < kTracks; t++)
        if (getSound(p, t) == instr)
            return t;
    return s_soundTrack;
}

void openSoundParameters(ModuleBase* from, int track)
{
    s_soundTrack = track & 7;
    uint8_t sound = getSound(current(), track);
    mtProject.values.lastUsedInstrument = sound;
    mtInterface.deactivateModule(from);
    // option 1: the page that fits the instrument (sample / synth / MIDI)
    mtInterface.activateModule(&instrumentEditor, 1);
}

void playFromStop()
{
    if (g_recArmed)
        sequencer.startCountIn();
    else
        sequencer.playPattern();
}

int snapLength(int steps, int dir)
{
    static const int kStops[5] = { 16, 32, 48, 64, 128 };
    int exact = -1;
    for (int i = 0; i < 5; i++)
        if (kStops[i] == steps)
            exact = i;
    if (exact < 0)
    {
        // not on a stop yet: go to the nearest one
        int best = 0;
        for (int i = 1; i < 5; i++)
        {
            int d = kStops[i] - steps, bd = kStops[best] - steps;
            if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd))
                best = i;
        }
        return kStops[best];
    }
    int next = exact + (dir > 0 ? 1 : -1);
    if (next < 0)
        next = 0;
    if (next > 4)
        next = 4;
    return kStops[next];
}

void toggleHelp(HelpView& help, InputProcessor* ip)
{
    help.active = !help.active;
    if (help.active)
        ip->blockAllInputsExcept(interfaceButtonMiniMenu);
    else
        ip->unblockAllInputs();
    g_shiftLatched = false;
}

static bool s_recDown = false;
static bool s_recLongDone = false;
static uint32_t s_recPressedAt = 0;
static bool s_wasPlaying = false;

RecAction recButton(uint8_t state)
{
    uint32_t now = (uint32_t)s_uiClock;
    if (state == buttonPress)
    {
        if (shiftActive())
            return RecAction::Undo;
        s_recDown = true;
        s_recLongDone = false;
        s_recPressedAt = now;
        return RecAction::None;
    }
    if (state == buttonRelease && s_recDown)
    {
        s_recDown = false;
        if (!s_recLongDone)
        {
            setRecArmed(!g_recArmed);
            return RecAction::Toggled;
        }
    }
    return RecAction::None;
}

bool recPoll()
{
    bool changed = false;
    if (s_recDown && !s_recLongDone && (uint32_t)s_uiClock - s_recPressedAt >= 1000)
    {
        s_recLongDone = true;
        bool q = !quantizedRecording();
        setQuantizedRecording(q);
        if (q)
            setRecArmed(true);
        changed = true;
    }
    // playback stopped while recording: live recording ends
    bool playing = sequencer.isPlay();
    if (s_wasPlaying && !playing && !sequencer.isCountingIn() && g_recArmed)
    {
        setRecArmed(false);
        changed = true;
    }
    s_wasPlaying = playing;
    return changed;
}

const char* recModeText()
{
    if (!g_recArmed)
        return "";
    return quantizedRecording() ? "Q REC" : "REC";
}

uint32_t homeOptions(int track)
{
    return 0x100u | (uint32_t)(track & 7);
}

void setRecArmed(bool on)
{
    g_recArmed = on;
    setLiveMetronome(on);
}

uint8_t physFx1Button()
{
    return interfaceButtonFx2;
}

uint8_t physFx2Button()
{
    return interfaceButtonFx1;
}

static int s_lastEditKind = -1;
static uint32_t s_lastEditAt = 0;

void beginEdit(int kind)
{
    uint32_t now = (uint32_t)s_uiClock;
    if (kind == s_lastEditKind && now - s_lastEditAt < 1500)
    {
        s_lastEditAt = now;
        return;
    }
    s_lastEditKind = kind;
    s_lastEditAt = now;
    newFileManager.storePatternUndoRevision();
}

void undoEdit()
{
    stopAll();
    s_lastEditKind = -1;
    newFileManager.undoPattern();
}

void trackDisplayName(int track, char* out, int outLen)
{
    if (outLen <= 0)
        return;
    out[0] = 0;
    if (track < 0 || track >= kTracks)
        return;
    const char* name = mtProject.values.TrackNames[track];
    char def[12];
    snprintf(def, sizeof(def), "Track %d", track + 1);
    if (name[0] != 0 && strcmp(name, def) != 0)
    {
        snprintf(out, (size_t)outLen, "%s", name);
        return;
    }
    uint8_t sound = getSound(current(), track);
    if (isSampleInstrument(sound))
    {
        const strInstrument& in = mtProject.instrument[sound];
        if (in.isActive && in.sample.file_name[0])
        {
            int n = 0;
            while (n < outLen - 1 && n < 31 && in.sample.file_name[n] && in.sample.file_name[n] != '.')
            {
                out[n] = in.sample.file_name[n];
                n++;
            }
            out[n] = 0;
            return;
        }
        snprintf(out, (size_t)outLen, "Track %d", track + 1);
        return;
    }
    if (isSynthInstrument(sound))
        snprintf(out, (size_t)outLen, "Synth %d", instrumentIdxToMidiChannel(sound));
    else
        snprintf(out, (size_t)outLen, "MIDI %d", instrumentIdxToMidiChannel(sound));
}

void bindModuleSwitchButtons(InputProcessor* ip, void (*switchFunct)(uint8_t))
{
    static const uint8_t kButtons[] = {
        interfaceButtonPerformance, interfaceButtonParams,     interfaceButtonFile,
        interfaceButtonConfig,      interfaceButtonMaster,     interfaceButtonSamplePlay,
        interfaceButtonSampleEdit,  interfaceButtonSampleRec,  interfaceButtonSampleLoad,
        interfaceButtonSong,
    };
    for (uint8_t b : kButtons)
        ip->setButtonObj(b, buttonPress, switchFunct);
}

uint32_t scaleColor(uint32_t c, int num, int den)
{
    uint32_t r = ((c >> 16) & 0xFF) * (uint32_t)num / (uint32_t)den;
    uint32_t g = ((c >> 8) & 0xFF) * (uint32_t)num / (uint32_t)den;
    uint32_t b = (c & 0xFF) * (uint32_t)num / (uint32_t)den;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
namespace draw
{
namespace
{
int s_budget = 0;
int s_used = 0;
int s_skipped = 0;
bool s_lighten = false;
bool s_inRects = false;
bool s_haveColor = false;
uint32_t s_color = 0;
int s_alpha = 255;
int s_lineWidth = 16;
} // namespace

void begin(int budgetWords)
{
    s_budget = budgetWords;
    s_used = 2;
    s_skipped = 0;
    s_lighten = false;
    s_inRects = false;
    s_haveColor = false;
    s_alpha = 255;
    s_lineWidth = 16;
    API_LINE_WIDTH(16);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
}

int used()
{
    return s_used;
}

int skipped()
{
    return s_skipped;
}

bool room(int words)
{
    if (s_used + words <= s_budget)
        return true;
    s_skipped++;
    return false;
}

void setLighten(bool on)
{
    // Shift no longer lightens the colours: drawShiftBorder() adds a blue
    // wash and a striped frame over the whole screen instead
    (void)on;
    s_lighten = false;
}

void color(uint32_t c)
{
    c &= 0xFFFFFF;
    if (s_lighten)
    {
        // a quarter of the way towards white
        uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        r += (255 - r) / 4;
        g += (255 - g) / 4;
        b += (255 - b) / 4;
        c = (r << 16) | (g << 8) | b;
    }
    if (s_haveColor && c == s_color)
        return;
    if (!room(1))
        return;
    API_COLOR(c);
    s_color = c;
    s_haveColor = true;
    s_used += 1;
}

void alpha(int a)
{
    if (a == s_alpha || !room(1))
        return;
    API_COLOR_A((uint8_t)a);
    s_alpha = a;
    s_used += 1;
}

void lineWidth(int sixteenths);

void rect(int x1, int y1, int x2, int y2)
{
    if (!room(3))
        return;
    // rectangles are rounded by the line width: keep them at 1 pixel
    if (s_lineWidth != 16)
        lineWidth(16);
    if (x2 < x1)
    {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (!s_inRects)
    {
        API_BEGIN(RECTS);
        s_inRects = true;
        s_used += 1;
    }
    API_VERTEX2F((int16_t)x1, (int16_t)y1);
    API_VERTEX2F((int16_t)x2, (int16_t)y2);
    s_used += 2;
}

void frame(int x1, int y1, int x2, int y2, int t)
{
    rect(x1, y1, x2, y1 + t - 1);
    rect(x1, y2 - t + 1, x2, y2);
    rect(x1, y1, x1 + t - 1, y2);
    rect(x2 - t + 1, y1, x2, y2);
}

void text(int x, int y, int fontHandle, uint16_t options, const char* str)
{
    if (str == nullptr || str[0] == 0)
        return;
    int len = (int)strlen(str);

    // glyphs right of x = 512 cost two commands each; estimate generously
    int startX = x;
    if (options & OPT_CENTERX)
        startX -= len * 4;
    if (options & OPT_RIGHTX)
        startX -= len * 8;
    int cost = 5;
    for (int i = 0; i < len; i++)
        cost += (startX + (i + 1) * 9 >= 512) ? 2 : 1;
    if (!room(cost))
        return;

    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
    API_CMD_TEXT((int16_t)x, (int16_t)y, (int16_t)fontHandle, options, str);
    s_used += cost;
}

void bitmap(uint32_t address, int w, int h, int x, int y)
{
    // colour + set-bitmap (5 list words) + begin, vertex, end
    if (!room(9))
        return;
    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
    API_COLOR(0xFFFFFF); // no tint
    API_CMD_SETBITMAP(address, RGB565, (uint16_t)w, (uint16_t)h);
    API_BEGIN(BITMAPS);
    API_VERTEX2F((int16_t)x, (int16_t)y);
    API_END();
    s_haveColor = false;
    s_used += 9;
}

namespace detail
{
// for drawing that talks to the chip directly (the Shift look): close an open
// RECTS, and afterwards forget the cached colour / alpha and count the words
void endRectsNow()
{
    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
}
void forgetState(int words)
{
    s_haveColor = false;
    s_alpha = 255;
    s_used += words;
}
} // namespace detail

namespace
{
void endRects()
{
    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
}

// the scissor box, inside a saved context (restored by unclip)
void clip(int x1, int y1, int x2, int y2)
{
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    API_SAVE_CONTEXT();
    API_SCISSOR_XY((uint16_t)x1, (uint16_t)y1);
    API_SCISSOR_SIZE((uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1));
    s_used += 3;
}

void unclip(uint32_t savedColor, bool hadColor, int savedAlpha, int savedLine)
{
    API_RESTORE_CONTEXT();
    s_used += 1;
    // the chip is back to what it had at the save
    s_color = savedColor;
    s_haveColor = hadColor;
    s_alpha = savedAlpha;
    s_lineWidth = savedLine;
}

// a filled edge strip between two points, filling to the given side
void edge(int prim, int xa, int ya, int xb, int yb)
{
    API_BEGIN((uint8_t)prim);
    API_VERTEX2F((int16_t)xa, (int16_t)ya);
    API_VERTEX2F((int16_t)xb, (int16_t)yb);
    API_END();
    s_used += 4;
}
} // namespace

void lineWidth(int sixteenths)
{
    if (sixteenths == s_lineWidth || !room(1))
        return;
    endRects();
    API_LINE_WIDTH((uint16_t)sixteenths);
    s_lineWidth = sixteenths;
    s_used += 1;
}

void lineStrip(const int16_t* xy, int n)
{
    if (n < 2 || !room(n + 3))
        return;
    endRects();
    API_BEGIN(LINE_STRIP);
    for (int i = 0; i < n; i++)
        API_VERTEX2F(xy[2 * i], xy[2 * i + 1]);
    API_END();
    s_used += n + 2;
}

void fillUnder(const int16_t* xy, int n, int clipX1, int clipY1, int clipX2, int clipY2)
{
    if (n < 2 || !room(n + 9))
        return;
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    clip(clipX1, clipY1, clipX2, clipY2);
    API_BEGIN(EDGE_STRIP_B);
    for (int i = 0; i < n; i++)
        API_VERTEX2F(xy[2 * i], xy[2 * i + 1]);
    API_END();
    s_used += n + 2;
    unclip(c, hc, a, lw);
}

void slant(int x1, int y1, int x2, int y2, int slantPx, uint32_t fill, uint32_t under)
{
    if (!room(17))
        return;
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    clip(x1, y1, x2, y2);
    color(fill);
    // everything right of the left edge
    edge(EDGE_STRIP_R, x1 + slantPx, y1, x1, y2 + 1);
    // the corner right of the right edge goes back to the colour around
    color(under);
    edge(EDGE_STRIP_R, x2 + 1, y1, x2 + 1 - slantPx, y2 + 1);
    unclip(c, hc, a, lw);
}

void slantFade(int x1, int y1, int x2, int y2, int slantPx, uint32_t c0, uint32_t c1, uint32_t under)
{
    if (!room(17 + kFadeWords))
        return;
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    clip(x1, y1, x2, y2);
    if (s_lighten)
    {
        // the same lightening as color()
        auto lift = [](uint32_t v)
        {
            uint32_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
            r += (255 - r) / 4;
            g += (255 - g) / 4;
            b += (255 - b) / 4;
            return (r << 16) | (g << 8) | b;
        };
        c0 = lift(c0);
        c1 = lift(c1);
    }
    API_CMD_GRADIENT((int16_t)x1, (int16_t)y1, c0, (int16_t)x2, (int16_t)y1, c1);
    s_used += kFadeWords;
    s_haveColor = false; // the fade sets its own colours
    color(under);
    edge(EDGE_STRIP_L, x1 + slantPx, y1, x1, y2 + 1);
    edge(EDGE_STRIP_R, x2 + 1, y1, x2 + 1 - slantPx, y2 + 1);
    unclip(c, hc, a, lw);
}

void roundRect(int x1, int y1, int x2, int y2, int radius)
{
    if (x2 < x1 || y2 < y1)
        return;
    const int maxR = ((x2 - x1) < (y2 - y1) ? (x2 - x1) : (y2 - y1)) / 2;
    if (radius > maxR)
        radius = maxR;
    if (radius < 1)
        radius = 1;
    if (!room(6))
        return;
    // the chip draws a rectangle grown by the line width, its corners rounded
    // by it: shrink the corners in by the radius
    endRects();
    API_LINE_WIDTH((uint16_t)(radius * 16));
    API_BEGIN(RECTS);
    API_VERTEX2F((int16_t)(x1 + radius), (int16_t)(y1 + radius));
    API_VERTEX2F((int16_t)(x2 - radius), (int16_t)(y2 - radius));
    API_END();
    s_lineWidth = radius * 16;
    s_used += 5;
}

void dots(const int16_t* xyr, int n)
{
    if (n <= 0 || !room(3 + 2 * n))
        return;
    endRects();
    API_BEGIN(FTPOINTS);
    for (int i = 0; i < n; i++)
    {
        API_POINT_SIZE((uint16_t)(xyr[3 * i + 2] * 16));
        API_VERTEX2F(xyr[3 * i], xyr[3 * i + 1]);
    }
    API_END();
    s_used += 2 + 2 * n;
}

void slantEnd(int x1, int y1, int x2, int y2, uint32_t under)
{
    if (!room(11))
        return;
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    clip(x1, y1, x2, y2);
    color(under);
    edge(EDGE_STRIP_R, x2 + 1, y1, x1, y2 + 1);
    unclip(c, hc, a, lw);
}

void slantOutline(int x1, int y1, int x2, int y2, int slantPx, uint32_t c, int sixteenths)
{
    if (!room(10))
        return;
    color(c);
    lineWidth(sixteenths);
    const int16_t pts[10] = { (int16_t)(x1 + slantPx), (int16_t)y1, (int16_t)x2, (int16_t)y1,
                              (int16_t)(x2 - slantPx), (int16_t)y2, (int16_t)x1, (int16_t)y2,
                              (int16_t)(x1 + slantPx), (int16_t)y1 };
    lineStrip(pts, 5);
}

void fadeRect(int x1, int y1, int x2, int y2, uint32_t c0, uint32_t c1, bool vertical)
{
    if (!room(5 + kFadeWords))
        return;
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    clip(x1, y1, x2, y2);
    if (vertical)
        API_CMD_GRADIENT((int16_t)x1, (int16_t)y1, c0, (int16_t)x1, (int16_t)y2, c1);
    else
        API_CMD_GRADIENT((int16_t)x1, (int16_t)y1, c0, (int16_t)x2, (int16_t)y1, c1);
    s_used += kFadeWords;
    unclip(c, hc, a, lw);
}

void cutBox(int x1, int y1, int x2, int y2, int cut, uint32_t fill, uint32_t under)
{
    if (!room(26))
        return;
    color(fill);
    rect(x1, y1, x2, y2);
    endRects();
    const uint32_t c = s_color;
    const bool hc = s_haveColor;
    const int a = s_alpha, lw = s_lineWidth;
    color(under);
    clip(x1, y1, x1 + cut, y1 + cut);
    edge(EDGE_STRIP_L, x1 + cut, y1, x1, y1 + cut);
    unclip(c, hc, a, lw);
    color(under);
    clip(x2 - cut, y2 - cut, x2, y2);
    edge(EDGE_STRIP_R, x2 + 1, y2 - cut, x2 - cut, y2 + 1);
    unclip(c, hc, a, lw);
}

void end()
{
    if (s_inRects)
    {
        API_END();
        s_inRects = false;
        s_used += 1;
    }
    if (s_alpha != 255)
    {
        API_COLOR_A(255);
        s_alpha = 255;
        s_used += 1;
    }
}
} // namespace draw

// ---------------------------------------------------------------------------
static void barBox(int i, int& x1, int& x2)
{
    const int w = kScreenW / 8;
    x1 = i * w + 3;
    x2 = (i + 1) * w - 4;
}

void drawBottomBar(const BottomBar& bar, int fontHandle)
{
    draw::color(color::kBg);
    draw::rect(0, kBottomBarY - 3, kScreenW - 1, kBottomBarY + kBottomBarH + 1);

    for (int i = 0; i < 8; i++)
    {
        int x1, x2;
        barBox(i, x1, x2);
        if (bar.highlighted[i])
        {
            draw::color(color::kSelect);
            draw::rect(x1 - 2, kBottomBarY - 2, x2 + 2, kBottomBarY + kBottomBarH + 1);
        }
        uint32_t fill = bar.fill[i] ? bar.fill[i] : color::kPanel;
        if (!bar.highlighted[i] && bar.fill[i])
            fill = scaleColor(fill, 45, 100); // unselected coloured boxes are dimmed
        draw::color(fill);
        draw::rect(x1, kBottomBarY, x2, kBottomBarY + kBottomBarH - 1);
    }

    for (int i = 0; i < 8; i++)
    {
        if (bar.text[i] == nullptr || bar.text[i][0] == 0)
            continue;
        int x1, x2;
        barBox(i, x1, x2);
        bool darkText = bar.highlighted[i] && bar.fill[i];
        draw::color(darkText ? color::kTextDark : color::kText);
        draw::text((x1 + x2) / 2, kBottomBarY + kBottomBarH / 2, fontHandle, OPT_CENTERX | OPT_CENTERY,
                   bar.text[i]);
    }
}

const int kWelcomeBpm[kWelcomeChoices] = { 80, 140, 170 };
const char* const kWelcomeGenre[kWelcomeChoices] = { "Hip hop", "Garage", "Jungle" };
static bool s_welcomePending = false;

void requestWelcome()
{
    s_welcomePending = true;
}

bool takeWelcome()
{
    bool w = s_welcomePending;
    s_welcomePending = false;
    return w;
}

bool drawWelcome(const WelcomeView& w, int fontRegular, int fontBold)
{
    if (!w.active)
        return false;
    static const uint32_t kChoiceColor[kWelcomeChoices] = { color::kInstrBtn, color::kFx2Btn, color::kDrum };

    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);
    draw::color(color::kText);
    draw::text(kScreenW / 2, kScreenTop + 70, fontBold, OPT_CENTER, "What are we feeling today?");

    const int boxW = 200, boxH = 190, gap = 40;
    const int x0 = (kScreenW - 3 * boxW - 2 * gap) / 2;
    const int y1 = kScreenTop + 130;
    for (int i = 0; i < kWelcomeChoices; i++)
    {
        const int x1 = x0 + i * (boxW + gap);
        const bool sel = i == w.choice;
        draw::color(sel ? kChoiceColor[i] : color::kPanel);
        draw::rect(x1, y1, x1 + boxW - 1, y1 + boxH - 1);
        if (sel)
        {
            draw::color(color::kSelect);
            draw::frame(x1 - 4, y1 - 4, x1 + boxW + 3, y1 + boxH + 3, 3);
        }
    }
    for (int i = 0; i < kWelcomeChoices; i++)
    {
        const int cx = x0 + i * (boxW + gap) + boxW / 2;
        const bool sel = i == w.choice;
        char bpm[8];
        snprintf(bpm, sizeof(bpm), "%d", kWelcomeBpm[i]);
        draw::color(sel ? color::kTextDark : color::kText);
        draw::text(cx, y1 + 72, fontBold, OPT_CENTER, bpm); // regular fonts only
        draw::text(cx, y1 + 122, fontRegular, OPT_CENTER, "BPM");
        draw::text(cx, y1 + 156, fontBold, OPT_CENTER, kWelcomeGenre[i]);
    }
    draw::color(color::kTextDim);
    draw::text(kScreenW / 2, y1 + boxH + 50, fontRegular, OPT_CENTER, "Left / Right: choose      Middle: let's go");
    return true;
}

bool drawHelp(const HelpView& help, int fontRegular, int fontBold)
{
    if (!help.active || help.lines == nullptr)
        return false;
    // the help window is all there is on the screen: it may use more of the
    // display list than a normal screen (still under the test limit of 1500)
    draw::begin(1480);
    draw::color(color::kBg);
    draw::rect(0, kScreenTop, kScreenW - 1, kScreenTop + kScreenH - 1);
    draw::color(color::kPanel);
    draw::rect(8, kScreenTop + 6, kScreenW - 9, kScreenTop + kScreenH - 7);
    draw::color(color::kText);
    draw::text(24, kScreenTop + 26, fontBold, OPT_CENTERY, help.title ? help.title : "Shortcuts");
    draw::color(color::kTextDim);
    draw::text(kScreenW - 24, kScreenTop + 26, fontRegular, OPT_CENTERY | OPT_RIGHTX, "Menu: close");

    // two columns of up to 16 lines
    const int lineH = 22;
    const int top = kScreenTop + 56;
    for (int i = 0; i < help.count && i < 32; i++)
    {
        int colX = i < 16 ? 24 : kScreenW / 2 + 8;
        int y = top + (i % 16) * lineH;
        const char* line = help.lines[i];
        // "Keys: what it does" - keys in bright text
        draw::color(color::kText);
        draw::text(colX, y, fontRegular, OPT_CENTERY, line);
    }
    return true;
}

void drawShiftBorder(int top)
{
    // Shift: a subtle blue-purple added over the screen (additive: nothing is
    // covered, everything lifts towards blue), then a yellow / black striped
    // frame (one 16 x 16 tile repeated along the edges). Always drawn: it is
    // small and fixed, and must never be the part that is left out.
    constexpr uint32_t kWash = 0x1C1648;
    constexpr uint32_t kStripe = 0xFFB347;
    constexpr uint32_t kTileAddress = 116000; // display.cpp: bitmaps[kBitmapGibGobHazard]
    constexpr int t = 6;                      // frame thickness
    const int y1 = top, y2 = kScreenTop + kScreenH - 1;
    const int h = y2 - y1 + 1;

    draw::detail::endRectsNow();
    API_COLOR_A(255);
    API_BLEND_FUNC(ONE, ONE);
    API_COLOR(kWash);
    API_BEGIN(RECTS);
    API_VERTEX2F(0, (int16_t)y1);
    API_VERTEX2F(kScreenW - 1, (int16_t)y2);
    API_END();
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    // black under the stripes
    API_COLOR(0x000000);
    API_BEGIN(RECTS);
    API_VERTEX2F(0, (int16_t)y1);
    API_VERTEX2F(kScreenW - 1, (int16_t)(y1 + t - 1));
    API_VERTEX2F(0, (int16_t)(y2 - t + 1));
    API_VERTEX2F(kScreenW - 1, (int16_t)y2);
    API_VERTEX2F(0, (int16_t)y1);
    API_VERTEX2F(t - 1, (int16_t)y2);
    API_VERTEX2F(kScreenW - t, (int16_t)y1);
    API_VERTEX2F(kScreenW - 1, (int16_t)y2);
    API_END();

    // the stripes: handle 0 (the fonts use 1 and up), repeated tile
    API_COLOR(kStripe);
    API_BITMAP_HANDLE(0);
    API_BITMAP_SOURCE(kTileAddress);
    API_BITMAP_LAYOUT(L4, 8, 16);
    API_BEGIN(BITMAPS);
    struct Seg
    {
        int16_t x, y, w, h;
    };
    const Seg segs[6] = {
        { 0, (int16_t)y1, 400, t },     { 400, (int16_t)y1, 400, t },               // top (under 512 wide each)
        { 0, (int16_t)(y2 - t + 1), 400, t }, { 400, (int16_t)(y2 - t + 1), 400, t }, // bottom
        { 0, (int16_t)y1, t, (int16_t)h },  { (int16_t)(kScreenW - t), (int16_t)y1, t, (int16_t)h },
    };
    for (const Seg& sg : segs)
    {
        API_BITMAP_SIZE(NEAREST, REPEAT, REPEAT, (uint16_t)sg.w, (uint16_t)sg.h);
        API_VERTEX2F(sg.x, sg.y);
    }
    API_END();
    draw::detail::forgetState(37);
}

void drawInfoStrip(int titleX,
                   const char* title,
                   const char* mode,
                   uint32_t modeColor,
                   const ParamBox* params,
                   int selectedParam,
                   int fontRegular,
                   int fontBold)
{
    const int boxW = 112;
    const int boxX0 = kScreenW - 4 * (boxW + 4) - 2;
    const int cy = kInfoY + kInfoH / 2;

    draw::color(color::kPanel);
    draw::rect(2, kInfoY, kScreenW - 3, kInfoY + kInfoH - 1);

    if (params != nullptr)
    {
        for (int i = 0; i < 4; i++)
        {
            int x1 = boxX0 + i * (boxW + 4);
            bool sel = (i == selectedParam);
            draw::color(sel ? params[i].color : scaleColor(params[i].color, 30, 100));
            draw::rect(x1, kInfoY + 3, x1 + boxW, kInfoY + kInfoH - 4);
        }
    }

    draw::color(color::kText);
    draw::text(titleX, cy, fontBold, OPT_CENTERY, title);
    if (mode != nullptr && mode[0])
    {
        draw::color(modeColor);
        draw::text((params ? boxX0 : kScreenW) - 8, cy, fontBold, OPT_CENTERY | OPT_RIGHTX, mode);
    }

    if (params != nullptr)
    {
        for (int i = 0; i < 4; i++)
        {
            int x1 = boxX0 + i * (boxW + 4);
            bool sel = (i == selectedParam);
            draw::color(sel ? color::kTextDark : params[i].color);
            draw::text(x1 + 5, cy, fontRegular, OPT_CENTERY, params[i].label);
            draw::text(x1 + boxW - 5, cy, fontBold, OPT_CENTERY | OPT_RIGHTX, params[i].value);
        }
    }
}

} // namespace gibgob
