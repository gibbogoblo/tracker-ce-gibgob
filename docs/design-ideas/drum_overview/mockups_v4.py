# Drum overview round 4: from the user's beach sketch (sunset sky, sun + bongos, clouds, wavy track bands,
# the selected track wide with its locks, coconut buttons). Three states: normal, Shift, FX1 list open.
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))

INK = "#1D1B2B"
HAND = "Patrick Hand"
MARK = "Permanent Marker"

# from the sky down to the sand (8 colours; fewer tracks spread over them)
BANDS = ["#A0569A", "#7B5DA6", "#5E63D6", "#4F72E6", "#79A3EC", "#BAB9DF", "#DCC6AE", "#EBB98A"]


def t(x, y, s, size, fill=INK, anchor="start", fam=HAND, extra=""):
    s = s.replace("&", "&amp;")
    return (f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
            f'dominant-baseline="middle" {extra}>{s}</text>')


def wave(y, amp, phase, wl=230):
    return [(x, y + amp * math.sin((x + phase) * 2 * math.pi / wl) + amp * .35 * math.sin((x + phase * 3) / 37))
            for x in range(-10, 811, 10)]


def path_of(pts):
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts)


def band_y(pts, x):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x0 <= x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return pts[-1][1]


def cloud(cx, cy, w, h):
    # a lumpy cloud: overlapping bumps, inked outline drawn with the wobble filter
    bumps = [(-.36, .1, .3), (-.14, -.18, .36), (.14, -.12, .32), (.36, .08, .28), (0, .14, .42)]
    shape = "".join(f'<ellipse cx="{cx+dx*w:.0f}" cy="{cy+dy*h:.0f}" rx="{r*w:.0f}" ry="{r*h*1.25:.0f}"/>' for dx, dy, r in bumps)
    base = f'<rect x="{cx-w*.48:.0f}" y="{cy:.0f}" width="{w*.96:.0f}" height="{h*.42:.0f}" rx="{h*.2:.0f}"/>'
    return (f'<g filter="url(#wob)"><g fill="none" stroke="{INK}" stroke-width="5">{shape}{base}</g>'
            f'<g fill="#F8E2E2">{shape}{base}</g></g>')


def bongos(x, y):
    """A pair of bongos (side view, tilted), as in the sketch."""
    def drum(dx, dy, rx, hgt):
        cx, cy = x + dx, y + dy
        body = f'M{cx-rx} {cy} L {cx-rx*.72} {cy+hgt} Q {cx} {cy+hgt+6} {cx+rx*.72} {cy+hgt} L {cx+rx} {cy} Z'
        return (f'<path d="{body}" fill="#B9683A" stroke="{INK}" stroke-width="3"/>'
                f'<path d="M{cx-rx*.86} {cy+hgt*.45} Q {cx} {cy+hgt*.45+6} {cx+rx*.86} {cy+hgt*.45}" stroke="{INK}" stroke-width="2" fill="none"/>'
                f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{rx*.42}" fill="#BFD8F2" stroke="{INK}" stroke-width="3"/>'
                f'<ellipse cx="{cx-rx*.25}" cy="{cy-rx*.08}" rx="{rx*.35}" ry="{rx*.12}" fill="#FFFFFF" opacity=".7"/>')
    return (f'<g transform="rotate(-14 {x} {y})" filter="url(#wob)">'
            f'<path d="M{x-6} {y+8} L {x+14} {y+8}" stroke="{INK}" stroke-width="5"/>'
            + drum(-22, 0, 22, 26) + drum(28, -4, 18, 22) + '</g>')


def coconut_button(cx, cy, n, on):
    g = ""
    if on:
        g += f'<circle cx="{cx}" cy="{cy}" r="40" fill="#F0462A" opacity=".85" filter="url(#soft)"/>'
    g += (f'<g filter="url(#wob)"><circle cx="{cx}" cy="{cy}" r="31" fill="#6B3424" stroke="{INK}" stroke-width="3"/>'
          f'<circle cx="{cx}" cy="{cy}" r="24" fill="#8C4C34"/>'
          f'<circle cx="{cx}" cy="{cy}" r="19" fill="#FFFDF6" stroke="#5A2A1E" stroke-width="2"/></g>'
          + t(cx, cy + 2, str(n), 24, INK, "middle", MARK))
    return g


def pebble(cx, cy, r, big):
    # a white hit (bigger = louder), inked
    return (f'<g filter="url(#wob)"><circle cx="{cx}" cy="{cy}" r="{r}" fill="#FFFFFF" stroke="{INK}" stroke-width="{3.2 if big else 2.6}"/>'
            f'<path d="M{cx-r*.55} {cy+r*.45} Q {cx} {cy+r*.85} {cx+r*.6} {cy+r*.35}" stroke="#C9C9DE" stroke-width="{max(2, r*.25):.1f}" fill="none" stroke-linecap="round"/></g>')


def dot(cx, cy):
    return f'<circle cx="{cx}" cy="{cy}" r="4.5" fill="#FFFFFF" stroke="{INK}" stroke-width="2.2"/>'


TRACKS = [  # name, velocities for steps 1-8
    ("kick",   [1, 0, 0, 0, .9, 0, 0, 0]),
    ("SNARE",  [0, 0, 1, 0, 0, 0, .8, 0]),
    ("clap",   [0, 0, .8, 0, 0, 0, .8, 0]),
    ("hi-hat", [.4, .6, .7, .5, .6, .8, .6, .6]),
]
SEL = 1
CUR_STEP = 2   # step 3
PLAY = 4       # step 5


def screen(state="normal", TRACKS=TRACKS):
    random.seed(4)
    shift = state in ("shift", "fx")
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480" viewBox="0 0 800 480"><defs>',
         '<filter id="wob"><feTurbulence type="fractalNoise" baseFrequency="0.035" numOctaves="2" seed="3"/>'
         '<feDisplacementMap in="SourceGraphic" scale="3.2"/></filter>',
         '<filter id="soft"><feGaussianBlur stdDeviation="5"/></filter>',
         '<filter id="grain" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="2" seed="8"/>'
         '<feColorMatrix values="0 0 0 0 1  0 0 0 0 1  0 0 0 0 1  0 0 0 .55 -.2"/></filter>',
         '<filter id="streak" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.004 0.08" numOctaves="2" seed="5"/>'
         '<feColorMatrix values="0 0 0 0 1  0 0 0 0 1  0 0 0 0 1  0 0 0 .5 -.18"/></filter>',
         '<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E8432C"/><stop offset="1" stop-color="#F0703A"/></linearGradient>',
         '</defs>']
    TOP, BOT = 66, 404
    n = len(TRACKS)
    # heights: the selected track wide, the others share the rest (2 tracks still fill the screen)
    big = 172 if n > 2 else (BOT - TOP) * .6
    small = (BOT - TOP - big) / (n - 1) if n > 1 else 0
    lo = 0 if n >= 7 else 1
    cols = [BANDS[round(lo + i * (7 - lo) / (n - 1))] if n > 1 else BANDS[3] for i in range(n)]

    s.append('<rect width="800" height="480" fill="url(#sky)"/>')
    # sun with rays (top left), bongos on it, the page in it
    s.append('<circle cx="66" cy="26" r="56" fill="#F7A54C"/>')
    s.append('<g filter="url(#wob)" stroke="' + INK + '" stroke-width="3" stroke-linecap="round">'
             + "".join(f'<path d="M{66+66*math.cos(math.radians(a)):.0f} {26+66*math.sin(math.radians(a)):.0f} L {66+(90+(i%2)*20)*math.cos(math.radians(a)):.0f} {26+(90+(i%2)*20)*math.sin(math.radians(a)):.0f}"/>'
                       for i, a in enumerate(range(-20, 40, 15)))
             + '<path d="M10 26 A 56 56 0 0 0 122 26" fill="none"/></g>')
    s.append(f'<g transform="translate(14 8) scale(.72)">{bongos(40, 30)}</g>')
    s.append(t(92, 48, "1/8", 22, INK, "middle", MARK))
    if shift:
        s.append(f'<g transform="rotate(-6 210 38)">{t(210, 38, "shift", 24, "#FFF3C4", "middle", MARK)}</g>')

    # track bands, from the top
    edges = [TOP]
    for i in range(n):
        edges.append(edges[-1] + (big if i == SEL else small))
    amp = min(7, small * .14) if n > 1 else 7
    waves = [wave(e, amp if 0 < k < n else 3, k * 90) for k, e in enumerate(edges)]
    waves[0] = [(x, y - 4) for x, y in waves[0]]
    waves[-1] = [(x, BOT + 4 + 5 * math.sin(x / 60)) for x, y in waves[-1]]
    for i in range(n):
        poly = path_of(waves[i]) + " " + " ".join(f"L{x:.1f} {y:.1f}" for x, y in reversed(waves[i + 1])) + " Z"
        s.append(f'<path d="{poly}" fill="{cols[i]}"/>')
    s.append('<rect y="60" width="800" height="360" filter="url(#streak)" opacity=".55"/>')
    # sand under the buttons
    s.append(f'<path d="{path_of(waves[-1])} L 810 480 L -10 480 Z" fill="#D9985F"/>')
    # inked wave lines (broken, like the sketch)
    for k in range(1, n + 1):
        pts = waves[k]
        segs, cur = [], []
        for j, p in enumerate(pts):
            cur.append(p)
            if j % 23 == 22:
                segs.append(cur); cur = [p]
        segs.append(cur)
        for j, sg in enumerate(segs):
            if len(sg) > 2:
                off = 3 if j % 2 else -2
                s.append(f'<path d="{path_of([(x, y + off) for x, y in sg[1:]])}" stroke="{INK}" stroke-width="2.6" fill="none" filter="url(#wob)" stroke-linecap="round"/>')

    # playhead: a soft shimmer down the step being played
    X0, STEPX = 168, 86.5
    px = X0 + PLAY * STEPX
    s.append(f'<rect x="{px-22}" y="{TOP+4}" width="44" height="{BOT-TOP-8}" rx="22" fill="#FFFFFF" opacity=".13"/>')

    labels_top = ("ratchet", "sample") if shift else ("prob", "vel")
    labels_bot = ("fx1", "fx2") if shift else ("nudge", "pitch")
    locks = ({2: ("x3", "snr 2", "dly 40", "6 bit"), 6: ("x2", "", "", "")} if shift
             else {2: ("1:2", "100", "-3", "+2"), 6: ("", "80", "", "")})

    for i, (name, vel) in enumerate(TRACKS):
        y0, y1 = edges[i], edges[i + 1]
        mid = (y0 + y1) / 2
        sel = i == SEL
        light = cols[i] in ("#BAB9DF", "#DCC6AE", "#EBB98A", "#79A3EC")
        if sel:
            s.append(t(18, mid + 2, name, 40, INK, fam=MARK))
            for k, lab in enumerate(labels_top):
                s.append(t(20, y0 + 26 + k * 22, lab, 19, INK))
            for k, lab in enumerate(labels_bot):
                s.append(t(20, y1 - 46 + k * 22, lab, 19, INK))
        else:
            s.append(t(16, mid + 2, name, min(32, (y1 - y0) * .78), INK))
        for st in range(8):
            cx = X0 + st * STEPX
            cy = mid + (0 if sel else random.uniform(-1, 1) * min(6, (y1 - y0) * .12))
            v = vel[st]
            if v > 0:
                r = min((9 + 9 * v) * (1.35 if sel else 1), (y1 - y0) * .4)
                if sel:
                    r = min(r, (y1 - y0 - 104) / 2)
                s.append(pebble(cx, cy, r, sel))
            else:
                s.append(dot(cx, cy))
            if sel and st == CUR_STEP:
                s.append(f'<ellipse cx="{cx}" cy="{cy}" rx="36" ry="33" fill="none" stroke="#FFD84A" stroke-width="4" filter="url(#wob)" stroke-dasharray="40 7 60 5"/>')
            if sel and st in locks:
                a, b, c, d = locks[st]
                for k, val in enumerate((a, b)):
                    if val:
                        s.append(t(cx, y0 + 26 + k * 22, val, 21, INK, "middle", MARK if not shift else HAND))
                for k, val in enumerate((c, d)):
                    if val:
                        s.append(t(cx, y1 - 46 + k * 22, val, 21, INK, "middle", MARK if not shift else HAND))
    # texture over everything painted
    s.append('<rect width="800" height="480" filter="url(#grain)" opacity=".35"/>')

    # clouds: the 4 colour-button values, then the length
    vals = ([("ratchet", "x3"), ("sample", "2"), ("fx1", "delay"), ("fx2", "crush")] if shift
            else [("prob", "1:2"), ("vel", "96"), ("nudge", "0"), ("pitch", "0")])
    for k, (lab, val) in enumerate(vals):
        cx = 330 + k * 92
        s.append(cloud(cx, 28, 86, 44))
        s.append(t(cx, 18, lab, 14, INK, "middle"))
        s.append(t(cx, 38, val, 19 if len(val) < 4 else 15, INK, "middle", MARK))
    s.append(cloud(728, 30, 120, 44))
    s.append(t(728, 20, "length", 14, INK, "middle"))
    s.append(t(728, 39, "64 steps", 18, INK, "middle", MARK))

    # coconut buttons: lit where the selected track has a hit
    for i in range(8):
        s.append(coconut_button(53.5 + i * 98.5, 444, i + 1, TRACKS[SEL][1][i] > 0))

    if state == "fx":   # FX1 held: the effect list drops from its cloud
        fx = ["off", "delay", "reverb", "chorus", "crush", "reverse", "comb", "filter", "drive"]
        x0, y0, w = 470, 58, 196
        hgt = 30 + len(fx) * 25 + 64
        s.append(f'<g filter="url(#wob)"><rect x="{x0}" y="{y0}" width="{w}" height="{hgt}" rx="22" fill="#F8E2E2" stroke="{INK}" stroke-width="3.5"/></g>')
        s.append(t(x0 + 18, y0 + 20, "fx1 for step 3", 16, "#7A4A5A"))
        for k, name in enumerate(fx):
            yy = y0 + 46 + k * 25
            if name == "delay":
                s.append(f'<rect x="{x0+10}" y="{yy-12}" width="{w-20}" height="24" rx="12" fill="#FFD84A" filter="url(#wob)"/>')
            s.append(t(x0 + 24, yy + 1, name, 19, INK, fam=MARK if name == "delay" else HAND))
        yb = y0 + 46 + len(fx) * 25 + 4
        s.append(f'<path d="M{x0+14} {yb-8} H {x0+w-14}" stroke="{INK}" stroke-width="1.5" stroke-dasharray="4 4"/>')
        s.append(t(x0 + 18, yb + 12, "small +/-   time  3/16", 16, INK))
        s.append(t(x0 + 18, yb + 34, "big +/-       mix  40%", 16, INK))
    s.append('</svg>')
    return "\n".join(s)


HTML = """<!doctype html><html><head><meta charset="utf-8"><style>
@font-face {{ font-family: 'Patrick Hand'; src: url('fonts/Patrick_Hand.woff2'); }}
@font-face {{ font-family: 'Permanent Marker'; src: url('fonts/Permanent_Marker.woff2'); }}
html, body {{ margin: 0; padding: 0; background: #000; }}
</style></head><body>{svg}</body></html>"""

TWO = [("kick", [1, 0, 0, 0, .9, 0, 0, 0]), ("SNARE", [0, 0, 1, 0, 0, 0, .8, 0])]
EIGHT = [("kick", [1, 0, 0, 0, .9, 0, 0, 0]), ("SNARE", [0, 0, 1, 0, 0, 0, .8, 0]), ("clap", [0, 0, .8, 0, 0, 0, .8, 0]),
         ("rim", [0, .6, 0, 0, 0, .6, 0, 0]), ("tom", [0, 0, 0, .7, 0, 0, 0, .7]), ("perc", [.5, 0, .5, 0, .5, 0, .5, 0]),
         ("shaker", [.4, .4, .4, .4, .4, .4, .4, .4]), ("hi-hat", [.4, .6, .7, .5, .6, .8, .6, .6])]
for name, st, tr in [("v4_1_beach_sketch", "normal", TRACKS), ("v4_2_beach_sketch_shift", "shift", TRACKS),
                     ("v4_3_beach_sketch_fx1", "fx", TRACKS), ("v4_4_two_tracks", "normal", TWO), ("v4_5_eight_tracks", "normal", EIGHT)]:
    open(os.path.join(OUT, name + ".html"), "w").write(HTML.format(svg=screen(st, tr)))
print("ok")
