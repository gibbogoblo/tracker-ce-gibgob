# Drum overview mockups, round 2: three different approaches, 8 steps on screen (one per top button)
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))

def t(x, y, s, size=14, fill="#fff", anchor="start", weight=400, fam="Inter Display", extra=""):
    s = s.replace("&", "&amp;")
    return (f'<text x="{x}" y="{y}" font-family="{fam}, Inter, Liberation Sans, sans-serif" font-size="{size}" '
            f'fill="{fill}" text-anchor="{anchor}" font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>')

# steps 1-8 of a 64-step pattern; velocity 0..1 (0 = off)
PAT = [
    [1, 0, 0, 0, .9, 0, .6, 0],          # kick
    [0, 0, 0, 0, 1, 0, 0, .4],           # snare
    [.8, .4, .7, .4, .8, .4, .7, .5],    # hat
    [0, 0, .7, 0, 0, 0, .6, 0],          # 4
    [0, .5, 0, .6, 0, 0, .5, 0],         # 5
    [0, 0, 0, 0, 1, 0, 0, 0],            # clap
    [.5, .3, .5, .3, .5, .3, .5, .3],    # 7 (muted)
    [0, 0, 0, 0, 0, 0, .9, .9],          # perc fills
]
TAGS = {(1, 7): "50%", (5, 4): "1:2", (7, 6): "FILL", (7, 7): "FILL", (0, 6): "2:4"}
MUTED = {6}
CUR = (1, 4)   # snare, step 5
PLAY = 6       # playhead on step 7
BTN_X = [8 + i * 98.5 for i in range(8)]   # the on-screen bar over the 8 buttons
BTN_W = 91


def svg_open(defs=""):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480"><defs>{defs}</defs>']


# =============================================================== 1. Bongo Dreams
def coconut(cx, cy, r, kind="whole", op=1.0):
    if kind == "empty":
        return (f'<circle cx="{cx}" cy="{cy}" r="5" fill="#FFFFFF" opacity=".75"/>'
                f'<circle cx="{cx}" cy="{cy}" r="5" fill="none" stroke="#E9BFA0" stroke-opacity=".7"/>')
    g = f'<g opacity="{op}">'
    if kind == "open":   # cracked open: the white inside
        g += (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#coco)"/>'
              f'<circle cx="{cx}" cy="{cy}" r="{r*.8}" fill="#FFFDF4"/>'
              f'<circle cx="{cx}" cy="{cy}" r="{r*.8}" fill="none" stroke="#EADCC2" stroke-width="1.5"/>'
              f'<circle cx="{cx}" cy="{cy}" r="{r*.42}" fill="#FFF3DA" opacity=".9"/>')
    else:
        g += f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#coco)"/>'
        for a in (-40, 10, 60):   # fibres
            x1 = cx + r * .75 * math.cos(math.radians(a)); y1 = cy + r * .75 * math.sin(math.radians(a))
            g += f'<path d="M{cx} {cy} Q {(cx+x1)/2+3} {(cy+y1)/2-3} {x1:.1f} {y1:.1f}" stroke="#4A2A18" stroke-opacity=".25" fill="none"/>'
        for (dx, dy) in ((-.24, -.34), (.24, -.34), (0, -.06)):   # the three eyes
            g += f'<circle cx="{cx+dx*r:.1f}" cy="{cy+dy*r:.1f}" r="{max(1.6, r*.11):.1f}" fill="#3A2014"/>'
    return g + '</g>'


def beach():
    defs = ('<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF6DE"/>'
            '<stop offset=".38" stop-color="#FFE3C4"/><stop offset=".6" stop-color="#FFCFC6"/><stop offset=".63" stop-color="#FFE7D6"/></linearGradient>'
            '<linearGradient id="sea" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#9EE2E0"/><stop offset="1" stop-color="#D9F7F1"/></linearGradient>'
            '<radialGradient id="glow"><stop offset="0" stop-color="#FFFBE6"/><stop offset=".35" stop-color="#FFF2BF" stop-opacity=".85"/><stop offset="1" stop-color="#FFE6B0" stop-opacity="0"/></radialGradient>'
            '<radialGradient id="coco" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#C89167"/><stop offset=".55" stop-color="#94603D"/><stop offset="1" stop-color="#5E3520"/></radialGradient>'
            '<linearGradient id="ray" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF1A8" stop-opacity=".85"/><stop offset="1" stop-color="#FFF1A8" stop-opacity=".2"/></linearGradient>')
    s = svg_open(defs)
    s.append('<rect width="800" height="480" fill="url(#sky)"/>')
    # big hazy sun low over the sea
    s.append('<circle cx="560" cy="300" r="260" fill="url(#glow)"/>')
    s.append('<circle cx="560" cy="300" r="92" fill="#FFF8DC"/>')
    s.append('<rect y="300" width="800" height="120" fill="url(#sea)"/>')
    for i in range(9):   # sun glitter on the water
        w = 120 - i * 11
        s.append(f'<rect x="{560-w/2}" y="{306+i*10}" width="{w}" height="2.5" rx="1.2" fill="#FFFFFF" opacity="{.85-i*.07:.2f}"/>')
    s.append('<path d="M0 412 Q 200 398 400 410 T 800 404 V 480 H 0 Z" fill="#FAE6C4"/>')
    s.append('<path d="M0 416 Q 200 404 400 414 T 800 409" stroke="#FFFFFF" stroke-width="3" fill="none" opacity=".8"/>')
    random.seed(11)
    for _ in range(14):   # bokeh
        s.append(f'<circle cx="{random.uniform(0,800):.0f}" cy="{random.uniform(0,300):.0f}" r="{random.uniform(5,20):.0f}" fill="#FFFFFF" opacity="{random.uniform(.12,.3):.2f}"/>')
    # palm in the top right, with its coconuts
    s.append('<path d="M812 420 Q 760 240 770 40" stroke="#D9A9A6" stroke-width="11" fill="none" stroke-linecap="round" opacity=".85"/>')
    for a in (200, 230, 255, 290, 320, 345):
        x2 = 770 + 92 * math.cos(math.radians(a)); y2 = 36 + 50 * math.sin(math.radians(a)) + 34
        s.append(f'<path d="M770 36 Q {(770+x2)/2} {min(36,y2)-26} {x2:.0f} {y2:.0f}" stroke="#D9A9A6" stroke-width="8" fill="none" stroke-linecap="round" opacity=".85"/>')
    s.append(coconut(761, 44, 7) + coconut(777, 45, 7) + coconut(769, 54, 7))
    # title
    s.append(t(16, 25, "bongo dreams", 30, "#C2607A", weight=200, extra='letter-spacing="1"'))
    s.append(t(18, 48, "pattern 03  ·  steps 1–8 of 64  ·  128 bpm", 12, "#B57D8A", weight=300))
    for k, (l, v, c) in enumerate([("prob", "1:2", "#F28B7D"), ("vel", "96", "#E7A84E"), ("nudge", "0", "#7BC8C3"), ("pitch", "0", "#B59BE0")]):
        x = 404 + k * 84
        s.append(f'<rect x="{x}" y="10" width="76" height="40" rx="20" fill="#FFFFFF" fill-opacity=".8" stroke="{c}" stroke-width="1.5"/>')
        s.append(t(x + 38, 22, l, 10, c, "middle", 500)); s.append(t(x + 38, 38, v, 15, "#6B3F4A", "middle", 600))
    # frosted panel
    s.append('<rect x="6" y="60" width="788" height="340" rx="26" fill="#FFFFFF" fill-opacity=".42" stroke="#FFFFFF" stroke-opacity=".9" stroke-width="1.5"/>')
    X0, CW, Y0, RH = 150, 80, 68, 40
    names = ["Kick", "Snare", "Hat", "Bongo hi", "Bongo lo", "Clap", "Shaker", "Conga"]
    s.append(f'<rect x="{X0+PLAY*CW+6}" y="{Y0-2}" width="{CW-12}" height="{8*RH+2}" rx="22" fill="url(#ray)"/>')
    s.append(f'<line x1="{X0+4*CW}" y1="{Y0+4}" x2="{X0+4*CW}" y2="{Y0+8*RH-4}" stroke="#FFFFFF" stroke-width="2" stroke-dasharray="2 6" stroke-linecap="round"/>')
    for r in range(8):
        y = Y0 + r * RH; cy = y + RH / 2
        if r == CUR[0]:
            s.append(f'<rect x="12" y="{y+5}" width="128" height="{RH-10}" rx="15" fill="#FF8C7A"/>')
        col = "#FFFFFF" if r == CUR[0] else ("#D2AEB8" if r in MUTED else "#8A5566")
        s.append(t(26, cy, names[r], 15, col, weight=600 if r == CUR[0] else 400))
        if r in MUTED:
            s.append(t(132, cy, "muted", 10, "#D2AEB8", "end", 400))
        for st in range(8):
            v = PAT[r][st]; cx = X0 + st * CW + CW / 2
            if v <= 0:
                s.append(coconut(cx, cy, 0, "empty"))
            else:
                s.append(coconut(cx, cy, 7 + 8 * v, "whole", .3 if r in MUTED else 1))
                if v >= .95 and r not in MUTED:
                    sx, sy, k = cx + 13, cy - 12, 6
                    s.append(f'<path d="M{sx} {sy-k} L{sx+k*.25} {sy-k*.25} L{sx+k} {sy} L{sx+k*.25} {sy+k*.25} L{sx} {sy+k} L{sx-k*.25} {sy+k*.25} L{sx-k} {sy} L{sx-k*.25} {sy-k*.25}Z" fill="#FFFFFF"/>')
            tag = TAGS.get((r, st))
            if tag:
                s.append(f'<rect x="{cx+8}" y="{y+3}" width="{8+len(tag)*6}" height="13" rx="6.5" fill="#FF8C7A"/>')
                s.append(t(cx + 12 + len(tag) * 3, y + 9.5, tag, 9, "#FFFFFF", "middle", 700))
    cx = X0 + CUR[1] * CW; cy = Y0 + CUR[0] * RH
    s.append(f'<rect x="{cx+8}" y="{cy+1}" width="{CW-16}" height="{RH-2}" rx="18" fill="none" stroke="#FFFFFF" stroke-width="6" opacity=".9"/>')
    s.append(f'<rect x="{cx+8}" y="{cy+1}" width="{CW-16}" height="{RH-2}" rx="18" fill="none" stroke="#FF7F6E" stroke-width="2.5"/>')
    # below the grid: hint + pages as little coconuts
    s.append(t(18, 413, "top row: snare steps 1–8   ·   shift: chimp out", 11, "#9B6A78", weight=400))
    s.append(t(600, 413, "page", 11, "#9B6A78", "end", 400))
    for p in range(8):
        s.append(coconut(614 + p * 22, 413, 6, "whole", 1 if p == 0 else .28))
    for i in range(8):
        x = BTN_X[i]; on = PAT[CUR[0]][i] > 0
        s.append(f'<rect x="{x}" y="428" width="{BTN_W}" height="44" rx="22" fill="{"#FF8C7A" if on else "#FFFFFF"}" fill-opacity="{1 if on else .78}" stroke="#F4C7B2" stroke-width="1.5"/>')
        if on:
            s.append(coconut(x + 30, 450, 8, "whole"))
            s.append(t(x + 56, 451, str(i + 1), 17, "#FFFFFF", "middle", 700))
        else:
            s.append(t(x + BTN_W / 2, 451, str(i + 1), 17, "#C79AA6", "middle", 300))
    s.append('</svg>')
    return "\n".join(s)


# =============================================================== 2. Rainforest Rhythm (focus view)
def leaf_path(cx, top, bot, w):
    return (f'M{cx} {top} C {cx+w*.62} {top+(bot-top)*.28} {cx+w*.52} {bot-(bot-top)*.22} {cx} {bot} '
            f'C {cx-w*.52} {bot-(bot-top)*.22} {cx-w*.62} {top+(bot-top)*.28} {cx} {top} Z')


def drop(cx, cy, k):
    return (f'<path d="M{cx} {cy-14*k} C {cx+3*k} {cy-7*k} {cx+9*k} {cy} {cx+9*k} {cy+5*k} A {9*k} {9*k} 0 0 1 {cx-9*k} {cy+5*k} '
            f'C {cx-9*k} {cy} {cx-3*k} {cy-7*k} {cx} {cy-14*k} Z" fill="url(#drop)"/>'
            f'<circle cx="{cx-3*k:.1f}" cy="{cy+3*k:.1f}" r="{2*k:.1f}" fill="#FFFFFF" opacity=".85"/>')


def rain():
    defs = ('<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0C3527"/><stop offset="1" stop-color="#03140D"/></linearGradient>'
            '<linearGradient id="lf" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5BE39C"/><stop offset="1" stop-color="#0E7650"/></linearGradient>'
            '<radialGradient id="drop" cx=".38" cy=".6" r=".7"><stop offset="0" stop-color="#F0FFFB"/><stop offset=".55" stop-color="#9CF5DA"/><stop offset="1" stop-color="#3CB7A0"/></radialGradient>')
    s = svg_open(defs)
    s.append('<rect width="800" height="480" fill="url(#bg)"/>')
    random.seed(4)
    for _ in range(80):
        x = random.uniform(0, 820); y = random.uniform(0, 480); l = random.uniform(8, 20)
        s.append(f'<line x1="{x:.0f}" y1="{y:.0f}" x2="{x-3:.0f}" y2="{y+l:.0f}" stroke="#BFF5E0" stroke-opacity=".16"/>')
    # header
    s.append(t(16, 24, "RAINFOREST", 24, "#E2FFF0", weight=900, extra='letter-spacing="3"'))
    s.append(t(16, 47, "RHYTHM  ·  PATTERN 03  ·  128 BPM", 11, "#6FC49E", weight=600, extra='letter-spacing="2"'))
    for k, (l, v, c) in enumerate([("PROB", "1:2", "#7CF2C4"), ("VEL", "96", "#F2E27C"), ("NUD", "0", "#7CC8F2"), ("PITCH", "0", "#C9A2F2")]):
        x = 432 + k * 90
        s.append(f'<rect x="{x}" y="9" width="82" height="40" rx="6" fill="#06231A" stroke="{c}" stroke-width="1.5"/>')
        s.append(t(x + 10, 21, l, 9, c, "start", 700, extra='letter-spacing="1.5"')); s.append(t(x + 72, 36, v, 17, "#F2FFF8", "end", 800))
    # canopy map: all 8 tracks, small
    names = ["KICK", "SNARE", "HAT", "RIM", "WOOD", "CLAP", "RAIN", "PERC"]
    MX0, MCW, MY0, MRH = 112, 84, 62, 17
    s.append(f'<rect x="8" y="{MY0-3}" width="784" height="{8*MRH+6}" rx="10" fill="#021009" opacity=".5"/>')
    s.append(f'<rect x="{MX0+PLAY*MCW+10}" y="{MY0-3}" width="{MCW-20}" height="{8*MRH+6}" fill="#7CF2C4" opacity=".08"/>')
    for r in range(8):
        y = MY0 + r * MRH; cy = y + MRH / 2
        if r == CUR[0]:
            s.append(f'<rect x="8" y="{y}" width="784" height="{MRH}" fill="#2FB69A" opacity=".28"/>')
            s.append(f'<path d="M10 {cy-5} L 17 {cy} L 10 {cy+5} Z" fill="#7CF2C4"/>')
        col = "#7CF2C4" if r == CUR[0] else ("#3B6153" if r in MUTED else "#89BFA7")
        s.append(t(24, cy, names[r], 10, col, weight=800 if r == CUR[0] else 600, extra='letter-spacing="1.5"'))
        s.append(f'<line x1="{MX0}" y1="{cy}" x2="{MX0+8*MCW-8}" y2="{cy}" stroke="#1F5A44" stroke-width="1"/>')
        for st in range(8):
            v = PAT[r][st]; cx = MX0 + st * MCW + MCW / 2
            if v > 0:
                c = "#3B6153" if r in MUTED else ("#E2FFF0" if r == CUR[0] else "#5BE39C")
                s.append(f'<ellipse cx="{cx}" cy="{cy}" rx="{5+5*v:.1f}" ry="{2.5+1.5*v:.1f}" transform="rotate(-25 {cx} {cy})" fill="{c}"/>')
    # vine divider
    s.append('<path d="M0 214 Q 100 204 200 214 T 400 214 T 600 214 T 800 214" stroke="#2F7A62" stroke-width="3" fill="none"/>')
    for x in range(40, 800, 90):
        s.append(f'<ellipse cx="{x}" cy="{212 + (6 if (x//90)%2 else -6)}" rx="9" ry="4" transform="rotate({-30 if (x//90)%2 else 30} {x} 212)" fill="#2F7A62"/>')
    # the focused track, big: 8 leaves right above the 8 buttons
    s.append(t(16, 240, "SNARE", 30, "#E2FFF0", weight=900, extra='letter-spacing="2"'))
    s.append(t(140, 242, "snr_jungle_04", 13, "#6FC49E", weight=500))
    s.append(t(784, 240, "STEPS 1–8 / 64   ▸ PAGE 1 OF 8", 10, "#6FC49E", "end", 700, extra='letter-spacing="1.5"'))
    TOP, BOT = 264, 392
    for i in range(8):
        cx = BTN_X[i] + BTN_W / 2; v = PAT[CUR[0]][i]
        if i == PLAY:   # rain falling on the leaf being played
            for dx in (-18, 0, 18):
                s.append(f'<line x1="{cx+dx}" y1="{TOP-14}" x2="{cx+dx-2}" y2="{TOP+4}" stroke="#BFF5E0" stroke-width="2" stroke-linecap="round" opacity=".8"/>')
        if v > 0:
            s.append(f'<path d="{leaf_path(cx, TOP, BOT, 74)}" fill="url(#lf)"/>')
            s.append(f'<path d="M{cx} {TOP+8} L {cx} {BOT}" stroke="#0A4A33" stroke-width="2"/>')
            for j in range(1, 4):
                yy = TOP + (BOT - TOP) * j / 4.3
                s.append(f'<path d="M{cx} {yy+10} L {cx+22} {yy}" stroke="#0A4A33" stroke-width="1.5"/><path d="M{cx} {yy+10} L {cx-22} {yy}" stroke="#0A4A33" stroke-width="1.5"/>')
            s.append(drop(cx, TOP + (BOT - TOP) * .56, .7 + .9 * v))
            s.append(t(cx, BOT - 12, str(int(v * 100)), 11, "#E2FFF0", "middle", 800))
        else:
            s.append(f'<path d="{leaf_path(cx, TOP+18, BOT-8, 60)}" fill="#0B2E23" fill-opacity=".7" stroke="#24684F" stroke-width="1.5"/>')
        if i == CUR[1]:
            s.append(f'<path d="{leaf_path(cx, TOP-6, BOT+6, 88)}" fill="none" stroke="#F2FFF8" stroke-width="3"/>')
        tag = TAGS.get((CUR[0], i))
        if tag:
            s.append(f'<rect x="{cx-20}" y="{BOT+4}" width="40" height="15" rx="4" fill="#F2E27C"/>')
            s.append(t(cx, BOT + 12, tag, 10, "#04201A", "middle", 800))
        if i == PLAY:   # the frog sits on the leaf being played
            fy = TOP + 22
            s.append(f'<ellipse cx="{cx}" cy="{fy+6}" rx="14" ry="9" fill="#9BE86A"/><circle cx="{cx-8}" cy="{fy-2}" r="5" fill="#9BE86A"/><circle cx="{cx+8}" cy="{fy-2}" r="5" fill="#9BE86A"/>'
                     f'<circle cx="{cx-8}" cy="{fy-2}" r="2.2" fill="#04201A"/><circle cx="{cx+8}" cy="{fy-2}" r="2.2" fill="#04201A"/>')
    for i in range(8):
        x = BTN_X[i]; on = PAT[CUR[0]][i] > 0
        s.append(f'<rect x="{x}" y="430" width="{BTN_W}" height="42" rx="8" fill="{"#2FB69A" if on else "#06231A"}" stroke="{"#7CF2C4" if on else "#1F5A44"}" stroke-width="1.5"/>')
        s.append(t(x + BTN_W / 2, 451, str(i + 1), 17, "#04201A" if on else "#4E8A72", "middle", 900))
    s.append('</svg>')
    return "\n".join(s)


# =============================================================== 3. Safari Cliffs (flat, angular, heavy)
def baboon(x, y, sc=1.0, flip=False, c="#2A1008"):
    f = -1 if flip else 1
    X = lambda dx: x + dx * sc * f
    Y = lambda dy: y + dy * sc
    return (f'<g fill="{c}">'
            f'<path d="M{X(-2)} {Y(0)} Q {X(-26)} {Y(-6)} {X(-22)} {Y(-30)} Q {X(-20)} {Y(-44)} {X(-34)} {Y(-40)}" stroke="{c}" stroke-width="{4*sc}" fill="none" stroke-linecap="round"/>'
            f'<ellipse cx="{X(0)}" cy="{Y(-16)}" rx="{13*sc}" ry="{17*sc}" transform="rotate({-12*f} {X(0)} {Y(-16)})"/>'
            f'<circle cx="{X(8)}" cy="{Y(-36)}" r="{9*sc}"/>'
            f'<ellipse cx="{X(17)}" cy="{Y(-33)}" rx="{8*sc}" ry="{4.5*sc}" transform="rotate({18*f} {X(17)} {Y(-33)})"/>'
            f'<ellipse cx="{X(-2)}" cy="{Y(-2)}" rx="{14*sc}" ry="{4*sc}"/>'
            '</g>')


def chamfer(x, y, w, h, c):
    return f'M{x+c} {y} H {x+w-c} L {x+w} {y+c} V {y+h-c} L {x+w-c} {y+h} H {x+c} L {x} {y+h-c} V {y+c} Z'


def safari():
    s = svg_open('<pattern id="hatch" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(35)">'
                 '<rect width="3" height="8" fill="#FFE9B8" opacity=".35"/></pattern>')
    s.append('<rect width="800" height="480" fill="#F6C76B"/>')
    s.append('<circle cx="640" cy="30" r="22" fill="#FCE6A8"/>')
    s.append('<path d="M0 62 L 120 50 L 210 56 L 330 44 L 470 54 L 600 46 L 800 56 V 70 H 0 Z" fill="#E3A35A"/>')
    # header
    s.append(t(14, 24, "SAFARI CLIFFS", 27, "#2A1008", weight=900, extra='letter-spacing="-0.5"'))
    s.append(t(15, 47, "PTN 03 / 1–8 OF 64 / 128 BPM", 10, "#7A3416", weight=700, extra='letter-spacing="1"'))
    s.append('<path d="M392 64 L 396 34 L 384 26 M396 34 L 406 24" stroke="#2A1008" stroke-width="4" fill="none"/>'
             '<path d="M360 26 L 432 26 L 420 18 L 372 18 Z" fill="#2A1008"/>')
    s.append(baboon(262, 66, .85) + baboon(304, 66, .62, True) + baboon(340, 66, .5))
    for k, (l, v, c) in enumerate([("PROB", "1:2", "#B8451F"), ("VEL", "96", "#C98A12"), ("NUD", "0", "#5E7A2A"), ("PITCH", "0", "#7A4A8E")]):
        x = 448 + k * 86
        s.append(f'<path d="{chamfer(x, 5, 80, 36, 8)}" fill="#FBE7C0"/><rect x="{x}" y="12" width="6" height="22" fill="{c}"/>')
        s.append(t(x + 14, 16, l, 9, c, "start", 800, extra='letter-spacing="1"')); s.append(t(x + 72, 30, v, 17, "#2A1008", "end", 900))
    # cliff
    s.append('<rect x="0" y="66" width="800" height="414" fill="#4A1E0E"/>')
    names = ["KICK", "SNARE", "HAT", "DJEMBE", "TOM", "CLAP", "SHAKER", "PERC"]
    X0, CW = 124, 83.5
    heights = [64 if r == CUR[0] else 34 for r in range(8)]
    y = 70
    random.seed(9)
    rows = []
    for r in range(8):
        h = heights[r]
        big = r == CUR[0]
        col = "#B8653A" if big else ("#A0522D" if r % 2 == 0 else "#93492A")
        jag = " ".join(f"L {x} {y + random.choice([0, 2, 4]):.0f}" for x in range(0, 801, 50))
        s.append(f'<path d="M0 {y+3} {jag} L 800 {y+h} L 0 {y+h} Z" fill="{col}"/>')
        s.append(f'<rect x="0" y="{y+h-3}" width="800" height="3" fill="#2A1008"/>')
        rows.append((r, y, h))
        y += h
    gy0, gy1 = 70, y
    # playhead column: hatching, and a baboon walking the cliff top above it
    px = X0 + PLAY * CW
    s.append(f'<rect x="{px+6}" y="{gy0}" width="{CW-12}" height="{gy1-gy0}" fill="url(#hatch)"/>')
    s.append(baboon(px + CW / 2 - 6, 68, .5, c="#2A1008"))
    for (r, y, h) in rows:
        big = r == CUR[0]; muted = r in MUTED
        tw = 104
        s.append(f'<path d="{chamfer(10, y+5, tw, h-11, 6)}" fill="{"#F2A93B" if big else "#4A1E0E"}"/>')
        s.append(t(18, y + h / 2 - 1, names[r], 19 if big else 12, "#2A1008" if big else ("#8A6A55" if muted else "#FBE7C0"),
                   weight=900, extra='letter-spacing="1"'))
        if muted:
            s.append(t(tw + 2, y + h / 2 - 1, "M", 10, "#8A6A55", "end", 900))
        bh = h - 14; bw = 46 if not big else 58
        for st in range(8):
            v = PAT[r][st]; cx = X0 + st * CW + CW / 2; bx = cx - bw / 2; by = y + 5
            s.append(f'<path d="{chamfer(bx, by, bw, bh, 5)}" fill="{"#6A3A24" if muted else "#5E2A14"}"/>')
            if v > 0:
                fh = max(5, bh * v)
                fill = "#B89A80" if muted else ("#FFD27A" if v >= .95 else "#F2A93B")
                s.append(f'<rect x="{bx}" y="{by+bh-fh:.1f}" width="{bw}" height="{fh:.1f}" fill="{fill}"/>')
                s.append(f'<rect x="{bx}" y="{by+bh-fh:.1f}" width="{bw}" height="3" fill="#FFF4D6" opacity=".8"/>')
                if big:
                    s.append(t(cx, by + bh - 10, str(int(v * 100)), 13, "#2A1008", "middle", 900))
            tag = TAGS.get((r, st))
            if tag:
                w = 10 + len(tag) * 6
                tx = min(cx + bw / 2 - 4, 798 - w); ty = y + 3
                s.append(f'<path d="M{tx} {ty} H {tx+w} L {tx+w-5} {ty+7} L {tx+w} {ty+14} H {tx} Z" fill="#2A1008"/>')
                s.append(t(tx + 4 + len(tag) * 3, ty + 7.5, tag, 9, "#F2A93B", "middle", 900))
        if big:
            cx = X0 + CUR[1] * CW + CW / 2
            s.append(f'<path d="{chamfer(cx-bw/2-5, y+1, bw+10, bh+8, 7)}" fill="none" stroke="#2A1008" stroke-width="5"/>')
            s.append(f'<path d="{chamfer(cx-bw/2-5, y+1, bw+10, bh+8, 7)}" fill="none" stroke="#FFF4D6" stroke-width="2.5"/>')
    # pages as a row of stones, hint
    s.append(t(14, gy1 + 18, "TOP ROW: SNARE 1–8   /   SHIFT: CHIMP OUT", 10, "#E8B98A", weight=800, extra='letter-spacing="1"'))
    for p in range(8):
        s.append(f'<path d="{chamfer(560 + p * 28, gy1 + 11, 22, 14, 3)}" fill="{"#F2A93B" if p == 0 else "#6A3A24"}"/>')
    for i in range(8):
        x = BTN_X[i]; on = PAT[CUR[0]][i] > 0
        s.append(f'<path d="{chamfer(x, 430, BTN_W, 42, 10)}" fill="{"#F2A93B" if on else "#5E2A14"}"/>')
        s.append(t(x + BTN_W / 2, 452, str(i + 1), 20, "#2A1008" if on else "#C98A5E", "middle", 900))
    s.append('</svg>')
    return "\n".join(s)


for name, f in [("v2_1_bongo_dreams", beach), ("v2_2_rainforest_rhythm", rain), ("v2_3_safari_cliffs", safari)]:
    open(os.path.join(OUT, name + ".svg"), "w").write(f())
print("ok")
