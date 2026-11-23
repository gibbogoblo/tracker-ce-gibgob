# Merged main screen (main sequencer + Gibbon vision): one layout, three looks
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Inter Display, Inter, Liberation Sans, sans-serif"


def t(x, y, s, size, fill, anchor="start", weight=600, extra=""):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>')


# ---------------------------------------------------------------- the song on screen
STEPS = 16
# kind: d = drum, s = slice, m = melodic ; notes: (start step, length steps, row 0..1 for melodic/slice, vel)
TRACKS = [
    ("Kick", "d", [(0, 1, 0, 1), (4, 1, 0, 1), (8, 1, 0, 1), (11, 1, 0, .6), (12, 1, 0, 1)], False),
    ("Snare", "d", [(4, 1, 0, 1), (12, 1, 0, 1), (15, 1, 0, .5)], False),
    ("Hats", "d", [(i, 1, 0, 1 if i % 2 == 0 else .55) for i in range(16)], False),
    ("Break", "s", [(0, 1, .1, 1), (2, 1, .5, 1), (3, 1, .3, .7), (6, 1, .8, 1), (8, 1, .1, 1), (10, 1, .6, 1), (13, 1, .4, .8), (14, 1, .9, 1)], False),
    ("Bass", "m", [(0, 2, .7, 1), (3, 1, .7, .7), (6, 2, .45, 1), (10, 2, .9, 1), (13, 3, .6, 1)], False),
    ("Keys", "m", [(0, 6, .2, 1), (0, 6, .45, 1), (8, 6, .3, 1), (8, 6, .55, 1)], False),
    ("Lead", "m", [(2, 2, .3, 1), (5, 1, .1, 1), (9, 3, .4, 1)], True),
    ("Vox", "m", [(4, 3, .5, 1), (12, 2, .35, 1)], False),
]
CUR = (1, 4)      # Snare, step 5
PLAY = 6          # playhead
SOUNDING = {0, 2, 4}

# geometry
HY, LX0, LX1, LY0, LY1 = 62, 6, 530, 66, 420
HEADW = 96
RX0, RX1 = 538, 794


def layout(th, shift=False):
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480" viewBox="0 0 800 480"><defs>{th["defs"]}</defs>']
    s.append(th["background"]())
    s.append(th["header"](shift))
    laneH = (LY1 - LY0) / 8
    gx0, gx1 = LX0 + HEADW + 6, LX1
    cw = (gx1 - gx0) / STEPS
    for i, (name, kind, notes, muted) in enumerate(TRACKS):
        y0 = LY0 + i * laneH + 2; y1 = y0 + laneH - 4
        s.append(th["lane"](i, name, kind, muted, y0, y1, gx0, gx1, cw, i == CUR[0], i in SOUNDING))
        for (st, ln, row, v) in notes:
            x0 = gx0 + st * cw + 2; x1 = gx0 + (st + ln) * cw - 2
            s.append(th["note"](kind, muted, x0, x1, y0, y1, row, v))
    # cursor + playhead
    cy0 = LY0 + CUR[0] * laneH + 2
    s.append(th["cursor"](gx0 + CUR[1] * cw, cy0, cw, laneH - 4))
    s.append(th["playhead"](gx0 + PLAY * cw + cw * .3, LY0, LY1))
    s.append(th["side"](shift))
    s.append(th["bar"](shift))
    s.append('</svg>')
    return "\n".join(s)


def gibbon(cx, cy, sc, body, face, frame=0):
    """A dancing gibbon (placeholder for the user's own two pictures)."""
    X = lambda d: cx + d * sc
    Y = lambda d: cy + d * sc
    up = -1 if frame == 0 else 1
    g = (f'<path d="M{X(-6)} {Y(-6)} Q {X(-30)} {Y(-30)} {X(-46)} {Y(-46*up if up>0 else -10)}" stroke="{body}" stroke-width="{7*sc}" fill="none" stroke-linecap="round"/>'
         f'<path d="M{X(6)} {Y(-6)} Q {X(30)} {Y(-30)} {X(46)} {Y(-46)}" stroke="{body}" stroke-width="{7*sc}" fill="none" stroke-linecap="round"/>'
         f'<path d="M{X(-6)} {Y(18)} L {X(-16)} {Y(40)} M{X(6)} {Y(18)} L {X(18)} {Y(38)}" stroke="{body}" stroke-width="{7*sc}" stroke-linecap="round"/>'
         f'<ellipse cx="{X(0)}" cy="{Y(6)}" rx="{14*sc}" ry="{18*sc}" fill="{body}"/>'
         f'<circle cx="{X(0)}" cy="{Y(-20)}" r="{14*sc}" fill="{body}"/>'
         f'<ellipse cx="{X(0)}" cy="{Y(-18)}" rx="{9*sc}" ry="{8*sc}" fill="{face}"/>'
         f'<circle cx="{X(-3.5)}" cy="{Y(-20)}" r="{1.8*sc}" fill="#1A1420"/><circle cx="{X(3.5)}" cy="{Y(-20)}" r="{1.8*sc}" fill="#1A1420"/>'
         f'<path d="M{X(-4)} {Y(-14)} Q {X(0)} {Y(-10)} {X(4)} {Y(-14)}" stroke="#1A1420" stroke-width="{1.6*sc}" fill="none" stroke-linecap="round"/>')
    return g


TOP_ROW = [n for n, _, _, _ in TRACKS]
CHIMP = ["Roll", "Stutter", "Scratch", "Gate", "Sweep", "Delay", "Reverb", "Vinyl"]


# ================================================================ 1. GibGob dark (today's colours)
def dark():
    C = dict(bg="#121218", panel="#26262E", line="#585764", text="#E6E9EC", dim="#8A949C", darkt="#101418",
             d="#B85548", m="#5F53B8", sl="#D9A23A", sel="#FFFFFF", play="#FF4F5E", box="#FFB347")
    kc = {"d": C["d"], "m": C["m"], "s": C["sl"]}

    def background():
        return f'<rect width="800" height="480" fill="{C["bg"]}"/>'

    def header(shift):
        g = ""
        for (x, lab, val) in ((10, "Pattern", "3"), (122, "Bar", "2.3")):
            g += f'<rect x="{x}" y="10" width="106" height="40" rx="6" fill="{C["panel"]}"/>' + t(x + 10, 30, lab, 12, C["dim"], weight=500) + t(x + 96, 30, val, 17, C["text"], "end", 800)
        g += f'<rect x="268" y="4" width="264" height="54" rx="6" fill="{C["panel"]}"/>' + t(400, 31, "GIBBON VISION", 22, "#F5002A", "middle", 900, 'letter-spacing="2"')
        for (x, lab, val) in ((568, "Swing", "54%"), (680, "BPM", "92.0")):
            g += f'<rect x="{x}" y="10" width="106" height="40" rx="6" fill="{C["panel"]}"/>' + t(x + 10, 30, lab, 12, C["dim"], weight=500) + t(x + 96, 30, val, 17, C["text"], "end", 800)
        if shift:
            g += f'<rect x="1" y="1" width="798" height="478" fill="none" stroke="#B85548" stroke-width="4"/>'
        return g

    def lane(i, name, kind, muted, y0, y1, gx0, gx1, cw, cur, sounding):
        col = "#2A2A2E" if muted else kc[kind]
        g = f'<rect x="{LX0}" y="{y0}" width="{HEADW}" height="{y1-y0}" rx="5" fill="{col}" opacity="{1 if cur else .62}"/>'
        g += f'<rect x="{gx0}" y="{y0}" width="{gx1-gx0}" height="{y1-y0}" rx="5" fill="{"#0C0C0E" if muted else ("#1F2932" if cur else "#151B21")}"/>'
        for b in range(1, 4):
            x = gx0 + b * 4 * cw
            g += f'<rect x="{x:.1f}" y="{y0+3}" width="1" height="{y1-y0-6}" fill="#3A4752"/>'
        g += t(LX0 + 8, (y0 + y1) / 2, f"{i+1} {name}", 13, C["dim"] if muted else (C["darkt"] if cur else C["text"]), weight=800 if cur else 600)
        if sounding and not muted:
            g += f'<circle cx="{LX0+HEADW-9}" cy="{y0+8}" r="3.5" fill="{C["play"]}"/>'
        if muted:
            g += f'<rect x="{LX0+HEADW-38}" y="{y1-15}" width="32" height="12" rx="3" fill="#585764"/>' + t(LX0 + HEADW - 22, y1 - 8.5, "MUTE", 8, C["darkt"], "middle", 800)
        return g

    def note(kind, muted, x0, x1, y0, y1, row, v):
        col = "#3C3C42" if muted else kc[kind]
        op = 1 if v > .9 else .7
        if kind == "m":
            yy = y1 - 6 - row * (y1 - y0 - 12)
            return f'<rect x="{x0:.1f}" y="{yy-2:.1f}" width="{x1-x0:.1f}" height="5" rx="2" fill="{col}" opacity="{op}"/>'
        if kind == "s":
            yy = y0 + 5 + row * (y1 - y0 - 16)
            return f'<rect x="{x0:.1f}" y="{yy:.1f}" width="{x1-x0:.1f}" height="7" rx="2" fill="{col}" opacity="{op}"/>'
        return f'<rect x="{x0:.1f}" y="{y0+5}" width="{x1-x0:.1f}" height="{y1-y0-10}" rx="3" fill="{col}" opacity="{op}"/>'

    def cursor(x, y, w, h):
        return f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" rx="4" fill="none" stroke="{C["sel"]}" stroke-width="2.5"/>'

    def playhead(x, y0, y1):
        return f'<rect x="{x:.1f}" y="{y0}" width="2" height="{y1-y0}" fill="{C["play"]}"/>'

    def side(shift):
        g = f'<rect x="{RX0}" y="{LY0}" width="{RX1-RX0}" height="150" rx="8" fill="#2E213B"/>'
        g += gibbon(RX0 + 128, LY0 + 80, 1.15, "#8C6248", "#E9C9A2")
        labels = [("Length", "16", "#53B886"), ("Velocity", "100", "#B8AA53"), ("Nudge", "0", "#7B6FD6"), ("Pitch", "C3", "#5AC8FA")]
        for k, (l, v, c) in enumerate(labels):
            x = RX0 + (k % 2) * 130; y = LY0 + 160 + (k // 2) * 52
            g += f'<rect x="{x}" y="{y}" width="124" height="46" rx="6" fill="{C["panel"]}" stroke="{c}" stroke-width="{3 if k==1 else 1.5}"/>'
            g += t(x + 10, y + 15, l, 11, c, weight=700) + t(x + 114, y + 31, v, 17, C["text"], "end", 800)
        y = LY0 + 268
        g += f'<rect x="{RX0}" y="{y}" width="{RX1-RX0}" height="86" rx="6" fill="{C["panel"]}"/>'
        g += t(RX0 + 12, y + 20, "2 Snare", 16, C["text"], weight=800) + t(RX1 - 12, y + 20, "REC", 12, C["dim"], "end", 800)
        g += t(RX0 + 12, y + 44, "snr_jungle_04", 13, C["dim"], weight=500)
        g += t(RX0 + 12, y + 66, "16 steps  ·  zoom 16", 12, C["dim"], weight=500)
        return g

    def bar(shift):
        g = ""
        labels = CHIMP if shift else TOP_ROW
        for i in range(8):
            x = 8 + i * 98.5
            on = (not shift) and i in SOUNDING
            g += f'<rect x="{x}" y="428" width="91" height="46" rx="6" fill="{("#FFB347" if on else C["panel"]) if not shift else "#3A2228"}"/>'
            g += t(x + 45.5, 451, labels[i], 14, C["darkt"] if on else C["text"], "middle", 700)
        return g

    return dict(defs="", background=background, header=header, lane=lane, note=note, cursor=cursor, playhead=playhead, side=side, bar=bar)


# ================================================================ 2. Y2K cyber angelic
def angelic():
    P = dict(t1="#3D2F7A", t2="#5A4A92", t3="#9A8CC8", row="#F6F2FF", gold="#F5C542")
    kc = {"d": "#FF9ED8", "m": "#A99CFF", "s": "#7FD8F5"}
    defs = ('<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FBF7FF"/><stop offset=".5" stop-color="#EEE8FF"/><stop offset="1" stop-color="#E6F4FF"/></linearGradient>'
            '<linearGradient id="iri" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#FFC6EA"/><stop offset=".5" stop-color="#D6C8FF"/><stop offset="1" stop-color="#BDEBFF"/></linearGradient>'
            '<linearGradient id="iri2" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFB3E2"/><stop offset="1" stop-color="#9FD8FF"/></linearGradient>')

    def star(x, y, r, c=P["gold"]):
        return f'<path d="M{x} {y-r} L{x+r*.22} {y-r*.22} L{x+r} {y} L{x+r*.22} {y+r*.22} L{x} {y+r} L{x-r*.22} {y+r*.22} L{x-r} {y} L{x-r*.22} {y-r*.22}Z" fill="{c}"/>'

    def background():
        return '<rect width="800" height="480" fill="url(#bg)"/>' + star(250, 14, 5) + star(552, 50, 4) + star(790, 60, 5, "#C9B6FF")

    def header(shift):
        g = ""
        for (x, lab, val) in ((10, "pattern", "3"), (122, "bar", "2.3")):
            g += f'<rect x="{x}" y="12" width="106" height="38" rx="19" fill="#FFFFFF" stroke="url(#iri2)" stroke-width="2"/>' + t(x + 16, 31, lab, 12, P["t3"], weight=500) + t(x + 92, 31, val, 17, P["t1"], "end", 800)
        for i in range(5):   # wings round the logo
            g += f'<ellipse cx="{300-i*8}" cy="{24+i*3}" rx="{48-i*6}" ry="6" transform="rotate({-16+i*7} 300 28)" fill="#FFFFFF" stroke="#D6C8FF"/>'
            g += f'<ellipse cx="{500+i*8}" cy="{24+i*3}" rx="{48-i*6}" ry="6" transform="rotate({16-i*7} 500 28)" fill="#FFFFFF" stroke="#D6C8FF"/>'
        g += f'<ellipse cx="400" cy="9" rx="40" ry="5" fill="none" stroke="{P["gold"]}" stroke-width="2.5"/>'
        g += t(400, 33, "gibbon vision", 22, "#6B5AA6", "middle", 800, 'letter-spacing="1"')
        for (x, lab, val) in ((568, "swing", "54%"), (680, "bpm", "92.0")):
            g += f'<rect x="{x}" y="12" width="106" height="38" rx="19" fill="#FFFFFF" stroke="url(#iri2)" stroke-width="2"/>' + t(x + 16, 31, lab, 12, P["t3"], weight=500) + t(x + 92, 31, val, 17, P["t1"], "end", 800)
        if shift:
            g += '<rect x="2" y="2" width="796" height="476" rx="16" fill="none" stroke="url(#iri2)" stroke-width="4"/>'
        return g

    def lane(i, name, kind, muted, y0, y1, gx0, gx1, cw, cur, sounding):
        col = "#E2DDEE" if muted else kc[kind]
        g = f'<rect x="{LX0}" y="{y0}" width="{HEADW}" height="{y1-y0}" rx="{(y1-y0)/2}" fill="{"url(#iri)" if cur else "#FFFFFF"}" stroke="{col}" stroke-width="2"/>'
        g += f'<rect x="{gx0}" y="{y0}" width="{gx1-gx0}" height="{y1-y0}" rx="{(y1-y0)/2}" fill="{"#FFFFFF" if cur else P["row"]}" fill-opacity="{.95 if cur else .8}"/>'
        for b in range(1, 4):
            x = gx0 + b * 4 * cw
            g += f'<circle cx="{x:.1f}" cy="{(y0+y1)/2}" r="1.6" fill="#D6C8FF"/>'
        g += t(LX0 + 14, (y0 + y1) / 2, name, 13, "#B9B2CC" if muted else (P["t1"] if cur else P["t2"]), weight=800 if cur else 600)
        if sounding and not muted:
            g += star(LX0 + HEADW - 12, (y0 + y1) / 2, 5)
        if muted:
            g += t(LX0 + HEADW - 10, (y0 + y1) / 2, "mute", 9, "#B9B2CC", "end", 700)
        return g

    def note(kind, muted, x0, x1, y0, y1, row, v):
        col = "#DAD5E6" if muted else kc[kind]
        op = 1 if v > .9 else .6
        if kind == "m":
            yy = y1 - 7 - row * (y1 - y0 - 14)
            return f'<rect x="{x0:.1f}" y="{yy-2.5:.1f}" width="{x1-x0:.1f}" height="5" rx="2.5" fill="{col}" opacity="{op}"/>'
        if kind == "s":
            yy = y0 + 6 + row * (y1 - y0 - 18)
            return f'<rect x="{x0:.1f}" y="{yy:.1f}" width="{x1-x0:.1f}" height="6" rx="3" fill="{col}" opacity="{op}"/>'
        cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
        return f'<circle cx="{cx:.1f}" cy="{cy}" r="{(y1-y0)/2-6}" fill="{col}" opacity="{op}"/>'

    def cursor(x, y, w, h):
        return (f'<rect x="{x+1:.1f}" y="{y-1}" width="{w-2:.1f}" height="{h+2}" rx="{h/2}" fill="none" stroke="url(#iri2)" stroke-width="3"/>'
                f'<ellipse cx="{x+w/2:.1f}" cy="{y-3}" rx="9" ry="3" fill="none" stroke="{P["gold"]}" stroke-width="2"/>')

    def playhead(x, y0, y1):
        return f'<rect x="{x:.1f}" y="{y0}" width="2" height="{y1-y0}" rx="1" fill="#B9A3F0"/><ellipse cx="{x+1:.1f}" cy="{y0-2}" rx="8" ry="2.5" fill="none" stroke="{P["gold"]}" stroke-width="2"/>'

    def side(shift):
        g = f'<rect x="{RX0}" y="{LY0}" width="{RX1-RX0}" height="150" rx="22" fill="#FFFFFF" stroke="url(#iri)" stroke-width="3"/>'
        g += gibbon(RX0 + 128, LY0 + 80, 1.15, "#C9B6FF", "#FFF4FB")
        g += star(RX0 + 30, LY0 + 30, 7) + star(RX1 - 30, LY0 + 120, 6, "#FFB3E2")
        labels = [("length", "16"), ("velocity", "100"), ("nudge", "0"), ("pitch", "C3")]
        for k, (l, v) in enumerate(labels):
            x = RX0 + (k % 2) * 130; y = LY0 + 160 + (k // 2) * 52
            sel = k == 1
            g += f'<rect x="{x}" y="{y}" width="124" height="46" rx="23" fill="{"url(#iri)" if sel else "#FFFFFF"}" stroke="#D6C8FF" stroke-width="1.5"/>'
            g += t(x + 16, y + 16, l, 11, P["t3"] if not sel else P["t2"], weight=600) + t(x + 108, y + 30, v, 17, P["t1"], "end", 800)
        y = LY0 + 268
        g += f'<rect x="{RX0}" y="{y}" width="{RX1-RX0}" height="86" rx="22" fill="#FFFFFF" fill-opacity=".8" stroke="#D6C8FF" stroke-width="1.5"/>'
        g += f'<ellipse cx="{RX0+22}" cy="{y+12}" rx="9" ry="3" fill="none" stroke="{P["gold"]}" stroke-width="2"/>'
        g += t(RX0 + 16, y + 26, "snare", 17, P["t1"], weight=800) + t(RX1 - 16, y + 26, "✧ track 2", 12, P["t3"], "end", 600)
        g += t(RX0 + 16, y + 48, "snr_jungle_04", 13, P["t2"], weight=500)
        g += t(RX0 + 16, y + 68, "16 steps · zoom 16", 12, P["t3"], weight=500)
        return g

    def bar(shift):
        g = ""
        labels = CHIMP if shift else TOP_ROW
        for i in range(8):
            x = 8 + i * 98.5
            on = (not shift) and i in SOUNDING
            g += f'<rect x="{x}" y="428" width="91" height="46" rx="23" fill="{"url(#iri)" if on else "#FFFFFF"}" stroke="#D6C8FF" stroke-width="1.5"/>'
            g += t(x + 45.5, 451, labels[i].lower(), 14, P["t1"] if on else P["t3"], "middle", 700)
        return g

    return dict(defs=defs, background=background, header=header, lane=lane, note=note, cursor=cursor, playhead=playhead, side=side, bar=bar)


# ================================================================ 3. Beach (matches the drum overview)
def beach():
    BANDS = ["#A0569A", "#8A5BA3", "#7B5DA6", "#5E63D6", "#4F72E6", "#79A3EC", "#BAB9DF", "#DCC6AE"]
    DARK = "#2B2238"
    defs = '<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E8432C"/><stop offset="1" stop-color="#F27A3C"/></linearGradient>'

    def is_dark(h):
        r, g_, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return .299 * r + .587 * g_ + .114 * b < 150

    def cloud(cx, base, w, h, seed):
        rnd = random.Random(seed)
        x0, x1 = cx - w / 2, cx + w / 2
        n = rnd.choice([3, 4, 5])
        out = f'<clipPath id="c{seed}"><rect x="{x0-30}" y="0" width="{w+60}" height="{base}"/></clipPath><g clip-path="url(#c{seed})" fill="#FCE9E6">'
        for i in range(n):
            f = (i + .5) / n
            mid = 1 - abs(f - .5) * 1.6
            r = h * (.30 + .32 * mid) * rnd.uniform(.88, 1.12)
            bx = min(max(x0 + f * w, x0 + r * .7), x1 - r * .7)
            out += f'<circle cx="{bx:.1f}" cy="{base - r*rnd.uniform(.4,.6):.1f}" r="{r:.1f}"/>'
        return out + f'<rect x="{x0}" y="{base-h*.35}" width="{w}" height="{h*.35}" rx="{h*.17}"/></g>'

    def background():
        return '<rect width="800" height="480" fill="url(#sky)"/><rect y="62" width="800" height="362" fill="#2E2240"/>'

    def header(shift):
        g = '<circle cx="40" cy="12" r="44" fill="#F9A94F"/><circle cx="40" cy="12" r="36" fill="#FBB862"/>'
        for (cx, w, lab, val, sd) in ((150, 96, "pattern", "3", 3), (250, 84, "bar", "2.3", 9), (600, 84, "swing", "54%", 17), (700, 96, "bpm", "92.0", 23)):
            g += cloud(cx, 56, w, 48, sd) + t(cx, 31, lab, 11, "#A0626A", "middle", 600) + t(cx, 45, val, 17, DARK, "middle", 800)
        g += t(424, 32, "GIBBON VISION", 22, "#FFF3D6", "middle", 900, 'letter-spacing="2"')
        if shift:
            g += f'<rect x="60" y="22" width="56" height="22" rx="11" fill="#FFF3D6"/>' + t(88, 33, "SHIFT", 11, "#E8432C", "middle", 900)
        return g

    def lane(i, name, kind, muted, y0, y1, gx0, gx1, cw, cur, sounding):
        col = "#4A4452" if muted else BANDS[i]
        ink = "#FFFFFF" if is_dark(col) else DARK
        g = f'<rect x="0" y="{y0-2}" width="{LX1+2}" height="{y1-y0+4}" fill="{col}"/>'
        if cur:
            g += f'<rect x="0" y="{y0-2}" width="{LX1+2}" height="{y1-y0+4}" fill="#FFFFFF" opacity=".14"/>'
        for b in range(1, 4):
            x = gx0 + b * 4 * cw
            g += f'<rect x="{x:.1f}" y="{y0+4}" width="1.5" height="{y1-y0-8}" fill="#FFFFFF" opacity=".25"/>'
        g += t(LX0 + 8, (y0 + y1) / 2, name, 15 if cur else 13, "#9C96A6" if muted else ink, weight=900 if cur else 700)
        if sounding and not muted:
            g += f'<circle cx="{LX0+HEADW-6}" cy="{(y0+y1)/2}" r="4" fill="#FFD84A"/>'
        if muted:
            g += t(LX0 + HEADW - 2, (y0 + y1) / 2, "mute", 10, "#9C96A6", "end", 700)
        return g

    def note(kind, muted, x0, x1, y0, y1, row, v):
        col = "#6E6878" if muted else "#FFFFFF"
        op = 1 if v > .9 else .7
        if kind == "m":
            yy = y1 - 7 - row * (y1 - y0 - 14)
            return f'<rect x="{x0:.1f}" y="{yy-2.5:.1f}" width="{x1-x0:.1f}" height="5" rx="2.5" fill="{col}" opacity="{op}"/>'
        if kind == "s":
            yy = y0 + 6 + row * (y1 - y0 - 18)
            return f'<rect x="{x0:.1f}" y="{yy:.1f}" width="{x1-x0:.1f}" height="6" rx="3" fill="{col}" opacity="{op}"/>'
        cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
        return f'<circle cx="{cx:.1f}" cy="{cy+1}" r="{(y1-y0)/2-7}" fill="#000000" opacity=".14"/><circle cx="{cx:.1f}" cy="{cy}" r="{(y1-y0)/2-7}" fill="{col}" opacity="{op}"/>'

    def cursor(x, y, w, h):
        return f'<rect x="{x+1:.1f}" y="{y}" width="{w-2:.1f}" height="{h}" rx="{min(10, h/2)}" fill="none" stroke="#FFD84A" stroke-width="3"/>'

    def playhead(x, y0, y1):
        return f'<rect x="{x-6:.1f}" y="{y0}" width="14" height="{y1-y0}" rx="7" fill="#FFFFFF" opacity=".18"/>'

    def side(shift):
        g = f'<rect x="{RX0-4}" y="62" width="{RX1-RX0+10}" height="362" fill="#EBB98A"/>'
        # the picture as a polaroid
        g += f'<g transform="rotate(-2 {RX0+128} {LY0+80})"><rect x="{RX0+6}" y="{LY0+4}" width="244" height="150" rx="4" fill="#FFFDF6"/>'
        g += f'<rect x="{RX0+14}" y="{LY0+12}" width="228" height="118" fill="#8FD3F0"/>' + gibbon(RX0 + 128, LY0 + 76, 1.0, "#8C6248", "#E9C9A2") + '</g>'
        labels = [("length", "16", "#53B886"), ("velocity", "100", "#E7A818"), ("nudge", "0", "#7B6FD6"), ("pitch", "C3", "#3FA8E0")]
        for k, (l, v, c) in enumerate(labels):
            x = RX0 + (k % 2) * 130; y = LY0 + 166 + (k // 2) * 50
            g += f'<rect x="{x}" y="{y}" width="124" height="44" rx="22" fill="#FFF7E8" stroke="{c}" stroke-width="{3 if k==1 else 1.5}"/>'
            g += t(x + 16, y + 15, l, 11, c, weight=700) + t(x + 108, y + 29, v, 17, DARK, "end", 800)
        y = LY0 + 272
        g += t(RX0 + 8, y + 18, "SNARE", 22, DARK, weight=900) + t(RX1 - 6, y + 18, "track 2", 12, "#8A5A3C", "end", 700)
        g += t(RX0 + 8, y + 44, "snr_jungle_04", 13, "#6A4A3A", weight=500)
        g += t(RX0 + 8, y + 64, "16 steps · zoom 16", 12, "#8A5A3C", weight=500)
        return g

    def bar(shift):
        g = '<rect y="424" width="800" height="56" fill="#D9985F"/>'
        labels = CHIMP if shift else TOP_ROW
        for i in range(8):
            x = 8 + i * 98.5
            on = (not shift) and i in SOUNDING
            g += f'<rect x="{x}" y="430" width="91" height="44" rx="22" fill="{"#F0462A" if on else "#FFF7E8"}"/>'
            g += t(x + 45.5, 452, labels[i].lower(), 14, "#FFFFFF" if on else "#8A5A3C", "middle", 800)
        return g

    return dict(defs=defs, background=background, header=header, lane=lane, note=note, cursor=cursor, playhead=playhead, side=side, bar=bar)


for name, f in [("1_gibgob_dark", dark), ("2_cyber_angelic", angelic), ("3_beach", beach)]:
    open(os.path.join(OUT, name + ".svg"), "w").write(layout(f()))
print("ok")
