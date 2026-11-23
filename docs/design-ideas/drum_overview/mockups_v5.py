# Drum overview round 5: the beach-sketch layout, clean and flat (no sketchy look), plain type,
# same-size hits, solid coconut highlight, flat-bottomed fluffy clouds (each one different).
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Inter Display, Inter, Liberation Sans, sans-serif"
DARK = "#2B2238"

# from the sky down to the sand (fewer tracks spread over these)
BANDS = ["#A0569A", "#7B5DA6", "#5E63D6", "#4F72E6", "#79A3EC", "#BAB9DF", "#DCC6AE", "#EBB98A"]


def t(x, y, s, size, fill=DARK, anchor="start", weight=600, extra=""):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>')


def is_dark(hexcol):
    r, g, b = (int(hexcol[i:i + 2], 16) for i in (1, 3, 5))
    return (0.299 * r + 0.587 * g + 0.114 * b) < 150


def wave(y, amp, phase, wl=230):
    return [(x, y + amp * math.sin((x + phase) * 2 * math.pi / wl) + amp * .3 * math.sin((x + phase * 3) / 37))
            for x in range(-10, 811, 10)]


def path_of(pts):
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts)


_cid = [0]


def cloud(cx, base, w, h, seed, col="#FCE9E6"):
    """Flat bottom, round fluffy top; the bumps differ per cloud."""
    rnd = random.Random(seed)
    _cid[0] += 1
    cid = f"cl{_cid[0]}"
    x0, x1 = cx - w / 2, cx + w / 2
    n = rnd.choice([3, 4, 4, 5])
    bumps = []
    for i in range(n):
        f = (i + .5) / n + rnd.uniform(-.06, .06)
        mid = 1 - abs(f - .5) * 1.6                      # taller towards the middle
        r = h * (.30 + .32 * mid) * rnd.uniform(.88, 1.12)
        bx = min(max(x0 + f * w, x0 + r * .7), x1 - r * .7)
        bumps.append((bx, base - r * rnd.uniform(.4, .6), r))
    shapes = "".join(f'<circle cx="{bx:.1f}" cy="{by:.1f}" r="{r:.1f}"/>' for bx, by, r in bumps)
    shapes += f'<rect x="{x0}" y="{base - h*.35:.1f}" width="{w}" height="{h*.35:.1f}" rx="{h*.17:.1f}"/>'
    return (f'<clipPath id="{cid}"><rect x="{x0-40}" y="{base-h*2}" width="{w+80}" height="{h*2}"/></clipPath>'
            f'<g clip-path="url(#{cid})" fill="{col}">{shapes}</g>')


def bongos(x, y, sc=1.0):
    def drum(dx, dy, rx, hgt):
        cx, cy = x + dx * sc, y + dy * sc
        rx *= sc; hgt *= sc
        return (f'<path d="M{cx-rx} {cy} L {cx-rx*.72} {cy+hgt} Q {cx} {cy+hgt+5*sc} {cx+rx*.72} {cy+hgt} L {cx+rx} {cy} Z" fill="#B9683A"/>'
                f'<path d="M{cx-rx*.86} {cy+hgt*.42} Q {cx} {cy+hgt*.42+5*sc} {cx+rx*.86} {cy+hgt*.42} L {cx+rx*.84} {cy+hgt*.55} Q {cx} {cy+hgt*.55+5*sc} {cx-rx*.84} {cy+hgt*.55} Z" fill="#7A3E1E"/>'
                f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{rx*.4}" fill="#7A3E1E"/>'
                f'<ellipse cx="{cx}" cy="{cy-1*sc}" rx="{rx*.88}" ry="{rx*.33}" fill="#F6E6CB"/>')
    return (f'<g transform="rotate(-12 {x} {y})">'
            f'<rect x="{x-6*sc}" y="{y+6*sc}" width="{22*sc}" height="{5*sc}" rx="{2*sc}" fill="#7A3E1E"/>'
            + drum(-20, 0, 20, 24) + drum(26, -3, 16, 20) + '</g>')


def coconut_button(cx, cy, n, on):
    g = f'<circle cx="{cx}" cy="{cy}" r="37" fill="#F0462A"/>' if on else ""
    g += (f'<circle cx="{cx}" cy="{cy}" r="31" fill="#5E2E20"/>'
          f'<circle cx="{cx}" cy="{cy}" r="25" fill="#8C4C34"/>'
          f'<circle cx="{cx}" cy="{cy}" r="20" fill="#FFF8EC"/>'
          + t(cx, cy + 1, str(n), 20, DARK, "middle", 800))
    return g


TRACKS = [
    ("kick",   [1, 0, 0, 0, .9, 0, 0, 0]),
    ("SNARE",  [0, 0, 1, 0, 0, 0, .8, 0]),
    ("clap",   [0, 0, .8, 0, 0, 0, .8, 0]),
    ("hi-hat", [.4, .6, .7, .5, .6, .8, .6, .6]),
]
SEL, CUR_STEP, PLAY = 1, 2, 4


def screen(state="normal", TRACKS=TRACKS):
    _cid[0] = 0
    shift = state in ("shift", "fx")
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480" viewBox="0 0 800 480"><defs>'
         '<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E8432C"/><stop offset="1" stop-color="#F27A3C"/></linearGradient>'
         '</defs>']
    TOP, BOT = 66, 404
    n = len(TRACKS)
    big = 172 if n > 2 else (BOT - TOP) * .6
    small = (BOT - TOP - big) / (n - 1) if n > 1 else 0
    lo = 0 if n >= 7 else 1
    cols = [BANDS[round(lo + i * (7 - lo) / (n - 1))] if n > 1 else BANDS[3] for i in range(n)]

    s.append('<rect width="800" height="480" fill="url(#sky)"/>')
    # sun (top left) with rays, the bongos on it, the page in it
    s.append("".join(f'<path d="M{66+64*math.cos(math.radians(a)):.0f} {26+64*math.sin(math.radians(a)):.0f} L {66+(84+(i%2)*14)*math.cos(math.radians(a)):.0f} {26+(84+(i%2)*14)*math.sin(math.radians(a)):.0f}" stroke="#FFB35C" stroke-width="4" stroke-linecap="round"/>'
                     for i, a in enumerate(range(-25, 50, 15))))
    s.append('<circle cx="66" cy="26" r="56" fill="#F9A94F"/><circle cx="66" cy="26" r="46" fill="#FBB862"/>')
    s.append(bongos(36, 30, .78))
    s.append(t(96, 50, "1/8", 18, "#5A2A16", "middle", 800))
    if shift:
        s.append(f'<rect x="150" y="22" width="70" height="26" rx="13" fill="#FFF3D6"/>' + t(185, 35, "SHIFT", 13, "#E8432C", "middle", 900, 'letter-spacing="1.5"'))

    # track bands
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
    for k in range(1, n):   # a soft foam line on each wave
        s.append(f'<path d="{path_of(waves[k])}" stroke="#FFFFFF" stroke-opacity=".35" stroke-width="2" fill="none"/>')
    s.append(f'<path d="{path_of(waves[-1])} L 810 480 L -10 480 Z" fill="#D9985F"/>')

    X0, STEPX = 168, 86.5
    px = X0 + PLAY * STEPX
    s.append(f'<rect x="{px-22}" y="{TOP+4}" width="44" height="{BOT-TOP-8}" rx="22" fill="#FFFFFF" opacity=".16"/>')

    labels_top = ("ratchet", "sample") if shift else ("prob", "vel")
    labels_bot = ("fx1", "fx2") if shift else ("nudge", "pitch")
    locks = ({2: ("x3", "snare 2", "delay 40", "crush 6"), 6: ("x2", "", "", "")} if shift
             else {2: ("1:2", "100", "-3", "+2"), 6: ("", "80", "", "")})

    for i, (name, vel) in enumerate(TRACKS):
        y0, y1 = edges[i], edges[i + 1]
        mid = (y0 + y1) / 2
        sel = i == SEL
        dark = is_dark(cols[i])
        ink = "#FFFFFF" if dark else DARK
        soft = "#D9DCFF" if dark else "#6A5A6E"
        if sel:
            s.append(t(18, mid + 1, name, 38, ink, weight=900, extra='letter-spacing="1"'))
            for k, lab in enumerate(labels_top):
                s.append(t(20, y0 + 26 + k * 21, lab, 14, soft, weight=500))
            for k, lab in enumerate(labels_bot):
                s.append(t(20, y1 - 46 + k * 21, lab, 14, soft, weight=500))
        else:
            s.append(t(18, mid + 1, name, min(22, (y1 - y0) * .62), ink, weight=700))
        R = 17 if sel else min(10, (y1 - y0) * .32)    # every hit the same size
        for st in range(8):
            cx = X0 + st * STEPX
            cy = mid
            if vel[st] > 0:
                s.append(f'<circle cx="{cx}" cy="{cy+2}" r="{R}" fill="#000000" opacity=".14"/>'
                         f'<circle cx="{cx}" cy="{cy}" r="{R}" fill="#FFFFFF"/>')
            else:
                s.append(f'<circle cx="{cx}" cy="{cy}" r="{4 if sel else 3}" fill="#FFFFFF" opacity=".55"/>')
            if sel and st == CUR_STEP:
                s.append(f'<circle cx="{cx}" cy="{cy}" r="{R+9}" fill="none" stroke="#FFD84A" stroke-width="4"/>')
            if sel and st in locks:
                a, b, c, d = locks[st]
                for k, val in enumerate((a, b)):
                    if val:
                        s.append(t(cx, y0 + 26 + k * 21, val, 16, ink, "middle", 800))
                for k, val in enumerate((c, d)):
                    if val:
                        s.append(t(cx, y1 - 46 + k * 21, val, 16, ink, "middle", 800))

    # clouds: the 4 colour-button values, then the length
    vals = ([("ratchet", "x3"), ("sample", "2"), ("fx1", "delay"), ("fx2", "crush")] if shift
            else [("prob", "1:2"), ("vel", "96"), ("nudge", "0"), ("pitch", "0")])
    widths = [82, 74, 86, 78]
    xs = [300, 396, 492, 590]
    for k, (lab, val) in enumerate(vals):
        cx = xs[k]
        s.append(cloud(cx, 55, widths[k], 48, seed=k * 7 + 3))
        s.append(t(cx, 31, lab, 11, "#A0626A", "middle", 600))
        s.append(t(cx, 44, val, 17, DARK, "middle", 800))
    s.append(cloud(722, 55, 116, 50, seed=41))
    s.append(t(722, 31, "length", 11, "#A0626A", "middle", 600))
    s.append(t(722, 44, "64 steps", 17, DARK, "middle", 800))

    for i in range(8):
        s.append(coconut_button(53.5 + i * 98.5, 443, i + 1, TRACKS[SEL][1][i] > 0))

    if state == "fx":   # FX1 held: the effect list drops from its cloud
        fx = ["off", "delay", "reverb", "chorus", "crush", "reverse", "comb", "filter", "drive"]
        x0, y0, w = 434, 64, 200
        hgt = 34 + len(fx) * 25 + 62
        s.append(f'<rect x="{x0}" y="{y0+4}" width="{w}" height="{hgt}" rx="20" fill="#000000" opacity=".15"/>'
                 f'<rect x="{x0}" y="{y0}" width="{w}" height="{hgt}" rx="20" fill="#FCE9E6"/>')
        s.append(t(x0 + 18, y0 + 20, "FX1 · STEP 3", 11, "#A0626A", weight=800, extra='letter-spacing="1.5"'))
        for k, name in enumerate(fx):
            yy = y0 + 48 + k * 25
            if name == "delay":
                s.append(f'<rect x="{x0+10}" y="{yy-12}" width="{w-20}" height="24" rx="12" fill="#FFD84A"/>')
            s.append(t(x0 + 24, yy + 1, name, 16, DARK, weight=800 if name == "delay" else 500))
        yb = y0 + 48 + len(fx) * 25 + 2
        s.append(f'<rect x="{x0+14}" y="{yb-8}" width="{w-28}" height="1.5" fill="#E2C3C0"/>')
        s.append(t(x0 + 18, yb + 12, "small +/-", 13, "#A0626A", weight=600) + t(x0 + w - 18, yb + 12, "time 3/16", 14, DARK, "end", 800))
        s.append(t(x0 + 18, yb + 34, "big +/-", 13, "#A0626A", weight=600) + t(x0 + w - 18, yb + 34, "mix 40%", 14, DARK, "end", 800))
    s.append('</svg>')
    return "\n".join(s)


TWO = [("kick", [1, 0, 0, 0, .9, 0, 0, 0]), ("SNARE", [0, 0, 1, 0, 0, 0, .8, 0])]
EIGHT = [("kick", [1, 0, 0, 0, .9, 0, 0, 0]), ("SNARE", [0, 0, 1, 0, 0, 0, .8, 0]), ("clap", [0, 0, .8, 0, 0, 0, .8, 0]),
         ("rim", [0, .6, 0, 0, 0, .6, 0, 0]), ("tom", [0, 0, 0, .7, 0, 0, 0, .7]), ("perc", [.5, 0, .5, 0, .5, 0, .5, 0]),
         ("shaker", [.4, .4, .4, .4, .4, .4, .4, .4]), ("hi-hat", [.4, .6, .7, .5, .6, .8, .6, .6])]
for name, st, tr in [("v5_1_beach", "normal", TRACKS), ("v5_2_beach_shift", "shift", TRACKS), ("v5_3_beach_fx1", "fx", TRACKS),
                     ("v5_4_two_tracks", "normal", TWO), ("v5_5_eight_tracks", "normal", EIGHT)]:
    open(os.path.join(OUT, name + ".svg"), "w").write(screen(st, tr))
print("ok")
