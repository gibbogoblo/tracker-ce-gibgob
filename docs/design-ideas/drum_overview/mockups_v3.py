# Drum overview round 3: the Safari layout (selected row thicker), organic shapes, three themes
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))


def t(x, y, s, size=14, fill="#fff", anchor="start", weight=400, extra=""):
    s = s.replace("&", "&amp;")
    return (f'<text x="{x}" y="{y}" font-family="Inter Display, Inter, Liberation Sans, sans-serif" font-size="{size}" '
            f'fill="{fill}" text-anchor="{anchor}" font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>')


PAT = [
    [1, 0, 0, 0, .9, 0, .6, 0],
    [0, 0, 0, 0, 1, 0, 0, .4],
    [.8, .4, .7, .4, .8, .4, .7, .5],
    [0, 0, .7, 0, 0, 0, .6, 0],
    [0, .5, 0, .6, 0, 0, .5, 0],
    [0, 0, 0, 0, 1, 0, 0, 0],
    [.5, .3, .5, .3, .5, .3, .5, .3],
    [0, 0, 0, 0, 0, 0, .9, .9],
]
TAGS = {(1, 7): "50%", (5, 4): "1:2", (7, 6): "FILL", (7, 7): "FILL", (0, 6): "2:4"}
MUTED = {6}
CUR = (1, 4)
PLAY = 6
X0, CW, GY0 = 124, 83.5, 66
SMALL, BIG = 36, 68
BTN_X = [8 + i * 98.5 for i in range(8)]
BTN_W = 91


def rows_layout():
    out, y = [], GY0
    for r in range(8):
        h = BIG if r == CUR[0] else SMALL
        out.append((r, y, h)); y += h
    return out, y


def wave_band(y, h, amp, phase, col, wl=160):
    pts = " ".join(f"L {x} {y + amp * math.sin((x + phase) * 2 * math.pi / wl):.1f}" for x in range(0, 801, 20))
    return f'<path d="M0 {y} {pts} L 800 {y+h} L 0 {y+h} Z" fill="{col}"/>'


def layout(th):
    """The shared screen: header, rows (selected one thick), steps, playhead, cursor, ground strip, bar."""
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480"><defs>{th["defs"]}</defs>']
    s.append(th["background"]())
    rows, gy1 = rows_layout()
    for (r, y, h) in rows:
        s.append(th["band"](r, y, h, r == CUR[0]))
    px = X0 + PLAY * CW
    s.append(th["playcol"](px, GY0, gy1))
    for (r, y, h) in rows:
        big = r == CUR[0]; muted = r in MUTED
        s.append(th["name"](r, y, h, big, muted))
        for st in range(8):
            cx = X0 + st * CW + CW / 2; cy = y + h / 2
            v = PAT[r][st]
            s.append(th["step"](cx, cy, v, big, muted) if v > 0 else th["empty"](cx, cy, big, muted))
            tag = TAGS.get((r, st))
            if tag:
                s.append(th["tag"](cx, y, h, tag))
        if big:
            s.append(th["cursor"](X0 + CUR[1] * CW + CW / 2, y + h / 2, h))
    s.append(th["ground"](gy1))
    s.append(th["walker"](px + CW / 2, 428))
    # header
    s.append(th["header"]())
    for k, (l, v) in enumerate([("prob", "1:2"), ("vel", "96"), ("nudge", "0"), ("pitch", "0")]):
        s.append(th["qbox"](300 + k * 84, l, v, k))
    for i in range(8):
        s.append(th["button"](BTN_X[i], i, PAT[CUR[0]][i] > 0))
    s.append('</svg>')
    return "\n".join(s)


def pages(x, y, on_col, off_col, shape="circle"):
    out = ""
    for p in range(8):
        c = on_col if p == 0 else off_col
        out += f'<circle cx="{x + p * 13}" cy="{y}" r="4" fill="{c}"/>'
    return out


# ---------------------------------------------------------------- figures
def painted_ape(x, y, sc, c="#B5452A", flip=False, pose=0):
    """A cave-painting ape (stick figure) banging sticks / rocks."""
    f = -1 if flip else 1
    X = lambda d: x + d * sc * f
    Y = lambda d: y + d * sc
    w = 3.2 * sc
    arm = (-30, -46) if pose == 0 else (-26, -40)
    return (f'<g stroke="{c}" stroke-width="{w}" stroke-linecap="round" fill="none">'
            f'<path d="M{X(0)} {Y(-14)} Q {X(4)} {Y(-26)} {X(10)} {Y(-30)}"/>'                       # back
            f'<path d="M{X(0)} {Y(-14)} L {X(-6)} {Y(0)} M{X(0)} {Y(-14)} L {X(8)} {Y(0)}"/>'        # legs
            f'<path d="M{X(9)} {Y(-28)} L {X(20)} {Y(-38)} L {X(arm[0]+50)} {Y(arm[1])}"/>'          # arm up, stick
            f'<path d="M{X(9)} {Y(-28)} L {X(-2)} {Y(-24)} L {X(-12)} {Y(-34)}"/>'                   # other arm
            f'</g><circle cx="{X(15)}" cy="{Y(-36)}" r="{5*sc}" fill="{c}"/>'
            f'<ellipse cx="{X(-13)}" cy="{Y(-36)}" rx="{4.5*sc}" ry="{3.5*sc}" fill="{c}"/>')        # rock in hand


def handprint(x, y, sc, c, rot=0, op=.55):
    g = f'<g transform="rotate({rot} {x} {y})" fill="{c}" opacity="{op}"><ellipse cx="{x}" cy="{y}" rx="{7*sc}" ry="{8*sc}"/>'
    for i, (dx, l) in enumerate([(-6, 8), (-2.2, 11), (2, 11.5), (5.8, 9.5)]):
        g += f'<rect x="{x+dx*sc-1.8*sc}" y="{y-8*sc-l*sc}" width="{3.6*sc}" height="{l*sc+3*sc}" rx="{1.8*sc}"/>'
    g += f'<rect x="{x+6*sc}" y="{y-3*sc}" width="{3.4*sc}" height="{9*sc}" rx="{1.7*sc}" transform="rotate(-40 {x+7*sc} {y})"/></g>'
    return g


def monkey(x, y, sc=1.0, drum=True):
    """A cartoon monkey sitting, drumming on a coconut in front of it (y = ground)."""
    br, face = "#7A4A2A", "#EBC79C"
    X = lambda d: x + d * sc
    Y = lambda d: y + d * sc
    g = (f'<path d="M{X(-12)} {Y(-6)} Q {X(-30)} {Y(-8)} {X(-28)} {Y(-24)} Q {X(-26)} {Y(-32)} {X(-20)} {Y(-28)}" stroke="{br}" stroke-width="{3.5*sc}" fill="none" stroke-linecap="round"/>'
         f'<ellipse cx="{X(0)}" cy="{Y(-14)}" rx="{13*sc}" ry="{14*sc}" fill="{br}"/>'
         f'<ellipse cx="{X(0)}" cy="{Y(-11)}" rx="{8*sc}" ry="{9*sc}" fill="{face}"/>'
         f'<circle cx="{X(-13)}" cy="{Y(-36)}" r="{5*sc}" fill="{br}"/><circle cx="{X(13)}" cy="{Y(-36)}" r="{5*sc}" fill="{br}"/>'
         f'<circle cx="{X(-13)}" cy="{Y(-36)}" r="{2.6*sc}" fill="{face}"/><circle cx="{X(13)}" cy="{Y(-36)}" r="{2.6*sc}" fill="{face}"/>'
         f'<circle cx="{X(0)}" cy="{Y(-36)}" r="{11*sc}" fill="{br}"/>'
         f'<path d="M{X(-7)} {Y(-38)} Q {X(-7)} {Y(-30)} {X(0)} {Y(-28)} Q {X(7)} {Y(-30)} {X(7)} {Y(-38)} Q {X(0)} {Y(-34)} {X(-7)} {Y(-38)} Z" fill="{face}"/>'
         f'<ellipse cx="{X(0)}" cy="{Y(-31)}" rx="{7*sc}" ry="{4.5*sc}" fill="{face}"/>'
         f'<circle cx="{X(-3.5)}" cy="{Y(-38)}" r="{1.6*sc}" fill="#2A160C"/><circle cx="{X(3.5)}" cy="{Y(-38)}" r="{1.6*sc}" fill="#2A160C"/>'
         f'<path d="M{X(-3)} {Y(-30)} Q {X(0)} {Y(-27.5)} {X(3)} {Y(-30)}" stroke="#2A160C" stroke-width="{1.2*sc}" fill="none" stroke-linecap="round"/>')
    if drum:   # arms: one up, one striking the coconut
        g += (f'<path d="M{X(-10)} {Y(-20)} Q {X(-22)} {Y(-30)} {X(-18)} {Y(-40)}" stroke="{br}" stroke-width="{4.5*sc}" fill="none" stroke-linecap="round"/>'
              f'<circle cx="{X(-18)}" cy="{Y(-41)}" r="{3.2*sc}" fill="{face}"/>'
              f'<circle cx="{X(20)}" cy="{Y(-7)}" r="{8*sc}" fill="url(#coco)"/>'
              f'<path d="M{X(10)} {Y(-20)} Q {X(18)} {Y(-24)} {X(19)} {Y(-15)}" stroke="{br}" stroke-width="{4.5*sc}" fill="none" stroke-linecap="round"/>'
              f'<circle cx="{X(19)}" cy="{Y(-15)}" r="{3.2*sc}" fill="{face}"/>'
              f'<path d="M{X(28)} {Y(-22)} l {5*sc} {-4*sc} M{X(30)} {Y(-15)} l {6*sc} 0 M{X(27)} {Y(-28)} l {3*sc} {-5*sc}" stroke="#FFFFFF" stroke-width="{2*sc}" stroke-linecap="round"/>')
    return g


def coconut(cx, cy, r, op=1.0):
    g = f'<g opacity="{op}"><circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#coco)"/>'
    for a in (-40, 10, 60):
        x1 = cx + r * .75 * math.cos(math.radians(a)); y1 = cy + r * .75 * math.sin(math.radians(a))
        g += f'<path d="M{cx} {cy} Q {(cx+x1)/2+3} {(cy+y1)/2-3} {x1:.1f} {y1:.1f}" stroke="#4A2A18" stroke-opacity=".25" fill="none"/>'
    for (dx, dy) in ((-.24, -.34), (.24, -.34), (0, -.06)):
        g += f'<circle cx="{cx+dx*r:.1f}" cy="{cy+dy*r:.1f}" r="{max(1.5, r*.11):.1f}" fill="#3A2014"/>'
    return g + '</g>'


def spark(x, y, k, c):
    return f'<path d="M{x} {y-k} L{x+k*.25} {y-k*.25} L{x+k} {y} L{x+k*.25} {y+k*.25} L{x} {y+k} L{x-k*.25} {y+k*.25} L{x-k} {y} L{x-k*.25} {y-k*.25}Z" fill="{c}"/>'


# ================================================================ 1. Rhythm Cave (warm, sunny)
def cave():
    random.seed(21)
    defs = ('<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF0C2"/><stop offset="1" stop-color="#FFD58E"/></linearGradient>'
            '<radialGradient id="stone" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#FBF6EC"/><stop offset=".6" stop-color="#CFC2AE"/><stop offset="1" stop-color="#8E7F6C"/></radialGradient>'
            '<linearGradient id="beam" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFFBE8" stop-opacity=".55"/><stop offset="1" stop-color="#FFFBE8" stop-opacity="0"/></linearGradient>'
            '<linearGradient id="col" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF6D0" stop-opacity=".6"/><stop offset="1" stop-color="#FFF6D0" stop-opacity=".15"/></linearGradient>')

    def background():
        g = '<rect width="800" height="480" fill="url(#sky)"/>'
        # the rock shelter: a warm sandstone overhang, open to the sun
        g += '<path d="M0 0 H 800 V 58 Q 700 70 600 60 Q 480 50 380 62 Q 260 74 160 60 Q 80 50 0 62 Z" fill="#E9A866"/>'
        g += '<path d="M0 0 H 800 V 40 Q 640 52 420 42 Q 220 34 0 46 Z" fill="#D98F4E"/>'
        g += '<rect y="60" width="800" height="420" fill="#F3C98E"/>'
        return g

    def band(r, y, h, big):
        col = "#FBE0B0" if big else ("#F2C68B" if r % 2 == 0 else "#ECBB7D")
        g = wave_band(y, h, 2.5, r * 37, col, 210)
        g += f'<path d="M0 {y+h-1} ' + " ".join(f"L {x} {y+h-1+1.5*math.sin((x+r*50)/40):.1f}" for x in range(0, 801, 20)) + '" stroke="#D49A5C" stroke-width="1.5" fill="none"/>'
        if big:
            g += f'<path d="M0 {y} L 800 {y}" stroke="#FFFFFF" stroke-opacity=".6" stroke-width="2"/>'
        return g

    def playcol(px, y0, y1):
        g = f'<rect x="{px+8}" y="{y0}" width="{CW-16}" height="{y1-y0}" rx="20" fill="url(#col)"/>'
        # sunbeams across the wall
        for (x, w) in ((520, 70), (610, 40), (680, 90)):
            g += f'<path d="M{x} 40 L {x+w} 40 L {x+w-260} 430 L {x-300} 430 Z" fill="url(#beam)" opacity=".5"/>'
        return g

    def name(r, y, h, big, muted):
        if big:
            g = (f'<path d="M12 {y+12} Q 20 {y+4} 60 {y+7} Q 104 {y+4} 112 {y+14} Q 118 {y+h/2} 110 {y+h-12} '
                 f'Q 70 {y+h-4} 22 {y+h-8} Q 8 {y+h/2} 12 {y+12} Z" fill="#B5452A"/>')
            return g + t(22, y + h / 2, "SNARE" if r == 1 else "?", 20, "#FFF1D6", weight=900, extra='letter-spacing="1"')
        names = ["KICK", "SNARE", "HAT", "WOOD", "STICKS", "CLAP", "SHAKER", "ROCKS"]
        c = "#C9A07A" if muted else "#A8402A"
        g = t(20, y + h / 2, names[r], 13, c, weight=800, extra='letter-spacing="1.5"')
        if muted:
            g += t(110, y + h / 2, "M", 10, c, "end", 800)
        return g

    def empty(cx, cy, big, muted):
        return f'<ellipse cx="{cx}" cy="{cy+1}" rx="{8 if big else 6}" ry="{4 if big else 3}" fill="#C98A52" opacity="{.25 if muted else .45}"/>'

    def step(cx, cy, v, big, muted):
        k = (1.6 if big else 1.0)
        rx = (7 + 9 * v) * k; ry = (5 + 6 * v) * k
        rot = random.uniform(-15, 15)
        if muted:
            return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" transform="rotate({rot:.0f} {cx} {cy})" fill="#E2CDB0"/>'
        g = (f'<ellipse cx="{cx+1}" cy="{cy+3}" rx="{rx}" ry="{ry}" transform="rotate({rot:.0f} {cx} {cy})" fill="#B07A48" opacity=".45"/>'
             f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" transform="rotate({rot:.0f} {cx} {cy})" fill="url(#stone)"/>')
        if v >= .95:   # clack!
            g += f'<path d="M{cx+rx+3} {cy-ry} l 6 -5 M{cx+rx+5} {cy-ry+7} l 8 -1 M{cx+rx-2} {cy-ry-5} l 2 -7" stroke="#B5452A" stroke-width="2" stroke-linecap="round"/>'
        if big:
            g += t(cx, cy + 1, str(int(v * 100)), 13, "#5A3A22", "middle", 900)
        return g

    def tag(cx, y, h, s):
        w = 12 + len(s) * 6; x = min(cx + 12, 796 - w)
        return (f'<path d="M{x} {y+4} Q {x+w/2} {y+1} {x+w} {y+5} Q {x+w+2} {y+11} {x+w-2} {y+16} Q {x+w/2} {y+19} {x+1} {y+16} Q {x-2} {y+10} {x} {y+4} Z" fill="#B5452A"/>'
                + t(x + w / 2, y + 10.5, s, 9, "#FFF1D6", "middle", 800))

    def cursor(cx, cy, h):
        r = h / 2 - 4
        return (f'<path d="M{cx-r} {cy} Q {cx-r} {cy-r-2} {cx} {cy-r} Q {cx+r+3} {cy-r+1} {cx+r} {cy+2} Q {cx+r-1} {cy+r+2} {cx-2} {cy+r} Q {cx-r-2} {cy+r-1} {cx-r+1} {cy-4}" '
                f'fill="none" stroke="#B5452A" stroke-width="4" stroke-linecap="round"/>')

    def ground(y):
        g = f'<path d="M0 {y+6} Q 200 {y-2} 400 {y+6} T 800 {y+4} V 428 H 0 Z" fill="#E3AE6E"/>'
        for x in (40, 180, 330, 470, 610, 750):   # pebbles on the floor
            g += f'<ellipse cx="{x}" cy="{y+28}" rx="7" ry="4" fill="#CFA372"/>'
        return g

    def walker(x, y):
        return painted_ape(x - 8, y - 2, .95, "#8E3220")

    def header():
        g = t(14, 22, "RHYTHM CAVE", 25, "#7E2E1A", weight=900, extra='letter-spacing="1"')
        g += t(15, 45, "pattern 03 · 128 bpm · page", 11, "#8E4A28", weight=600) + pages(186, 45, "#B5452A", "#E8B583")
        # cave paintings on the overhang: hands and drumming apes
        g += handprint(650, 30, .8, "#8E3220", -12) + handprint(684, 26, .7, "#8E3220", 10)
        g += painted_ape(730, 50, .8, "#8E3220") + painted_ape(778, 50, .7, "#8E3220", True, 1)
        return g

    def qbox(x, l, v, k):
        c = ["#B5452A", "#C98A12", "#6E8A3A", "#7A5A8E"][k]
        return (f'<path d="M{x+4} {y0} Q {x+38} {y0-4} {x+74} {y0+2} Q {x+80} {y0+18} {x+74} {y0+36} Q {x+38} {y0+40} {x+3} {y0+35} Q {x-3} {y0+18} {x+4} {y0} Z" fill="#FFF3DA" stroke="{c}" stroke-width="1.5"/>'
                + t(x + 38, y0 + 11, l, 9, c, "middle", 700) + t(x + 38, y0 + 26, v, 15, "#5A2A16", "middle", 900))
    y0 = 7

    def button(x, i, on):
        return (f'<path d="M{x+6} 432 Q {x+45} 426 {x+86} 433 Q {x+93} 451 {x+86} 469 Q {x+45} 475 {x+5} 469 Q {x-2} 451 {x+6} 432 Z" '
                f'fill="{"#B5452A" if on else "#F6DDB4"}" stroke="#C98A52" stroke-width="1.2"/>'
                + t(x + BTN_W / 2, 451, str(i + 1), 18, "#FFF1D6" if on else "#B07A48", "middle", 900))

    return layout(dict(defs=defs, background=background, band=band, playcol=playcol, name=name, empty=empty, step=step,
                       tag=tag, cursor=cursor, ground=ground, walker=walker, header=header, qbox=qbox, button=button))


# ================================================================ 2. Beach Bongo (chill)
def beach():
    random.seed(8)
    defs = ('<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E4F7F5"/><stop offset="1" stop-color="#FDEFD9"/></linearGradient>'
            '<radialGradient id="skin" cx=".4" cy=".35" r=".75"><stop offset="0" stop-color="#FFF9EC"/><stop offset="1" stop-color="#EAD6B0"/></radialGradient>'
            '<linearGradient id="col" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFFFFF" stop-opacity=".55"/><stop offset="1" stop-color="#FFFFFF" stop-opacity=".1"/></linearGradient>')
    sea = ["#C8F0EA", "#B6EAE3", "#A4E2DC", "#93D9D5", "#82D0CE", "#73C6C7", "#65BCC0", "#58B1B9"]

    def background():
        g = '<rect width="800" height="480" fill="url(#sky)"/>'
        g += '<circle cx="652" cy="36" r="22" fill="#FFE9B0"/><circle cx="652" cy="36" r="36" fill="#FFE9B0" opacity=".3"/>'
        g += '<rect y="60" width="800" height="420" fill="#58B1B9"/>'
        return g

    def band(r, y, h, big):
        col = "#E0FAF5" if big else sea[r]
        g = wave_band(y, h, 3.5, r * 60, col, 140)
        g += (f'<path d="M0 {y} ' + " ".join(f"L {x} {y + 3.5*math.sin((x + r*60)*2*math.pi/140):.1f}" for x in range(0, 801, 10)) +
              f'" stroke="#FFFFFF" stroke-opacity="{.95 if big else .5}" stroke-width="{3 if big else 1.5}" fill="none"/>')
        return g

    def playcol(px, y0, y1):
        return f'<rect x="{px+10}" y="{y0}" width="{CW-20}" height="{y1-y0}" rx="24" fill="url(#col)"/>'

    boards = ["#FF9E80", "#FFD27A", "#9FE0A8", "#FFB3C7", "#B7A6F0", "#FFCF9E", "#CFE3E2", "#8FD3F0"]

    def name(r, y, h, big, muted):
        names = ["kick", "snare", "hat", "bongo hi", "bongo lo", "clap", "shaker", "conga"]
        c = "#DCE9E8" if muted else boards[r]
        top, bot = y + (6 if big else 5), y + h - (6 if big else 5)
        mid = (top + bot) / 2
        g = (f'<path d="M14 {top+4} Q 14 {top} 22 {top} H 92 Q 118 {top} 122 {mid} Q 118 {bot} 92 {bot} H 22 Q 14 {bot} 14 {bot-4} Z" fill="{c}"/>'
             f'<path d="M24 {mid} H 100" stroke="#FFFFFF" stroke-opacity=".6" stroke-width="1.5"/>')
        g += t(26, mid, names[r], 19 if big else 13, "#9AB3B3" if muted else "#1E5F66", weight=800 if big else 600)
        return g

    def empty(cx, cy, big, muted):
        r = 6 if big else 4
        return f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#FFFFFF" stroke-opacity="{.35 if muted else .7}" stroke-width="1.5"/>'

    def step(cx, cy, v, big, muted):
        R = 15 if big else 10.5     # the bongo head; the ring round it is how loud
        if muted:
            return f'<circle cx="{cx}" cy="{cy}" r="{R}" fill="#D7E6E3"/>'
        ring = R + 4
        circ = 2 * math.pi * ring
        g = (f'<circle cx="{cx}" cy="{cy}" r="{ring}" fill="none" stroke="#FFFFFF" stroke-opacity=".5" stroke-width="3"/>'
             f'<circle cx="{cx}" cy="{cy}" r="{ring}" fill="none" stroke="#FF8A70" stroke-width="3.5" stroke-linecap="round" '
             f'stroke-dasharray="{circ*v:.1f} {circ:.1f}" transform="rotate(-90 {cx} {cy})"/>'
             f'<circle cx="{cx}" cy="{cy}" r="{R}" fill="#C98E5A"/><circle cx="{cx}" cy="{cy}" r="{R-3}" fill="url(#skin)"/>')
        if big:
            g += t(cx, cy + 1, str(int(v * 100)), 12, "#1E5F66", "middle", 800)
        return g

    def tag(cx, y, h, s):
        w = 10 + len(s) * 6; x = min(cx + 14, 796 - w)
        return f'<rect x="{x}" y="{y+3}" width="{w}" height="14" rx="7" fill="#FF8A70"/>' + t(x + w / 2, y + 10.5, s, 9, "#FFFFFF", "middle", 800)

    def cursor(cx, cy, h):
        r = h / 2 - 3
        return (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#FFFFFF" stroke-width="5" opacity=".9"/>'
                f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#FF8A70" stroke-width="2" stroke-dasharray="6 5"/>')

    def ground(y):
        return (f'<path d="M0 {y+4} Q 200 {y-4} 400 {y+4} T 800 {y+2} V 428 H 0 Z" fill="#F7E6C4"/>'
                f'<path d="M0 {y+4} Q 200 {y-4} 400 {y+4} T 800 {y+2}" stroke="#FFFFFF" stroke-width="3" fill="none"/>'
                + "".join(f'<circle cx="{random.uniform(10,790):.0f}" cy="{random.uniform(y+12,424):.0f}" r="1.3" fill="#E2C99A"/>' for _ in range(40)))

    def walker(x, y):   # a little crab strolling with the beat
        cy = y - 12
        return (f'<g><path d="M{x-14} {cy+4} l -6 6 M{x-12} {cy+7} l -4 7 M{x+14} {cy+4} l 6 6 M{x+12} {cy+7} l 4 7" stroke="#E8604A" stroke-width="2.5" stroke-linecap="round"/>'
                f'<path d="M{x-12} {cy-2} Q {x-22} {cy-10} {x-20} {cy-18} M{x+12} {cy-2} Q {x+22} {cy-10} {x+20} {cy-18}" stroke="#E8604A" stroke-width="3" fill="none" stroke-linecap="round"/>'
                f'<circle cx="{x-20}" cy="{cy-20}" r="5" fill="#FF7A5E"/><circle cx="{x+20}" cy="{cy-20}" r="5" fill="#FF7A5E"/>'
                f'<ellipse cx="{x}" cy="{cy}" rx="15" ry="10" fill="#FF7A5E"/>'
                f'<line x1="{x-5}" y1="{cy-8}" x2="{x-5}" y2="{cy-15}" stroke="#E8604A" stroke-width="2"/><line x1="{x+5}" y1="{cy-8}" x2="{x+5}" y2="{cy-15}" stroke="#E8604A" stroke-width="2"/>'
                f'<circle cx="{x-5}" cy="{cy-16}" r="3" fill="#FFFFFF"/><circle cx="{x+5}" cy="{cy-16}" r="3" fill="#FFFFFF"/>'
                f'<circle cx="{x-5}" cy="{cy-16}" r="1.5" fill="#1E3A3E"/><circle cx="{x+5}" cy="{cy-16}" r="1.5" fill="#1E3A3E"/>'
                f'<path d="M{x-4} {cy+2} Q {x} {cy+5} {x+4} {cy+2}" stroke="#B8402E" stroke-width="1.5" fill="none"/></g>')

    def header():
        g = t(14, 23, "beach bongo", 26, "#1E5F66", weight=800)
        g += t(15, 46, "chill · pattern 03 · 96 bpm · page", 11, "#4E8E92", weight=500) + pages(212, 46, "#FF8A70", "#BFDCDA")
        # hammock between two palms
        g += '<path d="M676 60 Q 672 30 682 6" stroke="#B08A68" stroke-width="5" fill="none" stroke-linecap="round"/>'
        g += '<path d="M786 60 Q 790 30 780 6" stroke="#B08A68" stroke-width="5" fill="none" stroke-linecap="round"/>'
        for (bx, d) in ((682, -1), (780, 1)):
            for a in (0, 40, 80, 130, 170):
                ang = math.radians(200 + a if d < 0 else -20 - a)
                g += f'<path d="M{bx} 6 q {14*math.cos(ang):.0f} {-6:.0f} {30*math.cos(ang):.0f} {12*abs(math.sin(ang))+4:.0f}" stroke="#7FB88E" stroke-width="4" fill="none" stroke-linecap="round"/>'
        g += '<path d="M680 26 Q 731 52 782 26" stroke="#FF8A70" stroke-width="3" fill="none"/><path d="M680 26 Q 731 46 782 26" stroke="#FFD27A" stroke-width="2" fill="none"/>'
        g += '<path d="M600 50 q 5 -5 10 0 q 5 -5 10 0" stroke="#4E8E92" stroke-width="1.5" fill="none"/><path d="M612 30 q 4 -4 8 0 q 4 -4 8 0" stroke="#4E8E92" stroke-width="1.5" fill="none"/>'
        return g

    def qbox(x, l, v, k):
        c = ["#FF8A70", "#E7A84E", "#4FB3A9", "#9A86D6"][k]
        return (f'<rect x="{x}" y="8" width="76" height="38" rx="19" fill="#FFFFFF" fill-opacity=".85" stroke="{c}" stroke-width="1.5"/>'
                + t(x + 38, 19, l, 9, c, "middle", 600) + t(x + 38, 34, v, 15, "#1E5F66", "middle", 800))

    def button(x, i, on):
        return (f'<rect x="{x}" y="430" width="{BTN_W}" height="42" rx="21" fill="{"#FF8A70" if on else "#FFF7E8"}" stroke="#F0D9B0" stroke-width="1.5"/>'
                + t(x + BTN_W / 2, 451, str(i + 1), 17, "#FFFFFF" if on else "#7FA9A8", "middle", 800 if on else 500))

    return layout(dict(defs=defs, background=background, band=band, playcol=playcol, name=name, empty=empty, step=step,
                       tag=tag, cursor=cursor, ground=ground, walker=walker, header=header, qbox=qbox, button=button))


# ================================================================ 3. Coconut Tree
def coconut_tree():
    random.seed(13)
    defs = ('<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7FD3F7"/><stop offset="1" stop-color="#D4F4FF"/></linearGradient>'
            '<radialGradient id="coco" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#C89167"/><stop offset=".55" stop-color="#94603D"/><stop offset="1" stop-color="#5E3520"/></radialGradient>'
            '<linearGradient id="col" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF7B0" stop-opacity=".7"/><stop offset="1" stop-color="#FFF7B0" stop-opacity=".2"/></linearGradient>')
    leaf = ["#5DBB4C", "#53AE43"]

    def background():
        g = '<rect width="800" height="480" fill="url(#sky)"/>'
        g += '<circle cx="610" cy="34" r="22" fill="#FFF6C2"/>'
        g += '<rect y="60" width="800" height="380" fill="#3E8E3A"/>'
        return g

    def band(r, y, h, big):
        # each row is a palm leaf: pointed at the ends, a midrib, leaflets
        col = "#8EDB5E" if big else leaf[r % 2]
        top, bot = y + 2, y + h - 2; mid = (top + bot) / 2
        g = (f'<path d="M2 {mid} Q 30 {top} 120 {top} H 700 Q 780 {top} 798 {mid} Q 780 {bot} 700 {bot} H 120 Q 30 {bot} 2 {mid} Z" fill="{col}"/>')
        g += f'<path d="M6 {mid} H 794" stroke="#2F7A2E" stroke-opacity=".35" stroke-width="{2 if big else 1.2}"/>'
        for x in range(30, 790, 26):
            g += f'<path d="M{x} {mid} L {x+14} {top+3}" stroke="#2F7A2E" stroke-opacity=".18"/><path d="M{x} {mid} L {x+14} {bot-3}" stroke="#2F7A2E" stroke-opacity=".18"/>'
        return g

    def playcol(px, y0, y1):
        return f'<rect x="{px+10}" y="{y0}" width="{CW-20}" height="{y1-y0}" rx="24" fill="url(#col)"/>'

    def name(r, y, h, big, muted):
        names = ["KICK", "SNARE", "HAT", "COCO HI", "COCO LO", "CLAP", "SHAKER", "CONGA"]
        c = "#A9CFA0" if muted else ("#1F4D1C" if big else "#F4FFE8")
        g = ""
        if big:
            g += f'<rect x="12" y="{y+12}" width="102" height="{h-24}" rx="{(h-24)/2}" fill="#FFD93B"/>'
        g += t(22, y + h / 2, names[r], 19 if big else 12, c, weight=900 if big else 800, extra='letter-spacing="1"')
        if muted:
            g += t(110, y + h / 2, "M", 10, c, "end", 800)
        return g

    def empty(cx, cy, big, muted):
        return f'<circle cx="{cx}" cy="{cy}" r="{5 if big else 3.5}" fill="#E9FFD8" opacity="{.25 if muted else .55}"/>'

    def step(cx, cy, v, big, muted):
        r = (7 + 7 * v) * (1.55 if big else 1)
        g = coconut(cx, cy, r, .35 if muted else 1)
        if v >= .95 and not muted:
            g += spark(cx + r + 2, cy - r + 2, 6, "#FFF6C2")
        if big:
            g += (f'<rect x="{cx-15}" y="{cy+r-6}" width="30" height="15" rx="7.5" fill="#FFFFFF" opacity=".9"/>'
                  + t(cx, cy + r + 1.5, str(int(v * 100)), 11, "#5E3520", "middle", 800))
        return g

    def tag(cx, y, h, s):
        w = 10 + len(s) * 6; x = min(cx + 12, 796 - w)
        return f'<rect x="{x}" y="{y+3}" width="{w}" height="14" rx="7" fill="#FFD93B"/>' + t(x + w / 2, y + 10.5, s, 9, "#1F4D1C", "middle", 900)

    def cursor(cx, cy, h):
        r = h / 2 - 3
        return (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#1F4D1C" stroke-width="6" opacity=".5"/>'
                f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#FFD93B" stroke-width="3.5"/>')

    def ground(y):
        g = f'<rect y="{y}" width="800" height="{428-y}" fill="#2F7A2E"/>'
        # the palm trunk: ringed, running along under the leaves
        g += f'<rect y="{y+16}" width="800" height="22" fill="#A8794E"/>'
        for x in range(0, 800, 24):
            g += f'<path d="M{x} {y+16} q 12 11 0 22" stroke="#7E5634" stroke-width="2" fill="none"/>'
        return g

    def walker(x, y):
        return monkey(x - 10, y - 6, .95)

    def header():
        g = t(14, 23, "COCONUT TREE", 25, "#1F4D1C", weight=900, extra='letter-spacing="1"')
        g += t(15, 46, "pattern 03 · 128 bpm · page", 11, "#2F6A2C", weight=700) + pages(190, 46, "#7A4A2A", "#A7D7E8")
        # palm crown with coconuts in the top right
        for a in (190, 215, 240, 300, 325, 350, 270):
            x2 = 742 + 70 * math.cos(math.radians(a)); y2 = 18 + 34 * math.sin(math.radians(a)) + 30
            g += f'<path d="M742 18 Q {(742+x2)/2} {min(18, y2)-24} {x2:.0f} {y2:.0f}" stroke="#3E9E3A" stroke-width="9" fill="none" stroke-linecap="round"/>'
        g += coconut(732, 30, 9) + coconut(752, 31, 9) + coconut(742, 45, 9)
        return g

    def qbox(x, l, v, k):
        c = ["#E8604A", "#E7A818", "#3E9E3A", "#7A5AB8"][k]
        return (f'<rect x="{x}" y="8" width="76" height="38" rx="14" fill="#FFFFFF" fill-opacity=".9" stroke="{c}" stroke-width="2"/>'
                + t(x + 38, 19, l, 9, c, "middle", 700) + t(x + 38, 34, v, 15, "#1F4D1C", "middle", 900))

    def button(x, i, on):
        g = f'<rect x="{x}" y="430" width="{BTN_W}" height="42" rx="16" fill="{"#FFD93B" if on else "#2F7A2E"}" stroke="{"#E7B818" if on else "#4E9E46"}" stroke-width="1.5"/>'
        if on:
            g += coconut(x + 28, 451, 8) + t(x + 56, 451, str(i + 1), 18, "#1F4D1C", "middle", 900)
        else:
            g += t(x + BTN_W / 2, 451, str(i + 1), 18, "#A9D99E", "middle", 800)
        return g

    s = layout(dict(defs=defs, background=background, band=band, playcol=playcol, name=name, empty=empty, step=step,
                    tag=tag, cursor=cursor, ground=ground, walker=walker, header=header, qbox=qbox, button=button))
    return s.replace('</svg>', '<rect y="472" width="800" height="8" fill="#2F7A2E"/></svg>') if False else s


for name, f in [("v3_1_rhythm_cave", cave), ("v3_2_beach_bongo_chill", beach), ("v3_3_coconut_tree", coconut_tree)]:
    open(os.path.join(OUT, name + ".svg"), "w").write(f())
print("ok")
