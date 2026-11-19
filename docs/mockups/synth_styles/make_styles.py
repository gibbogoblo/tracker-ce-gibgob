# Synth page mockups in four styles (Main + Amp each), 800x480 like the Mini's screen.
import math, os
OUT = os.path.dirname(os.path.abspath(__file__))
W, H = 800, 480
PAGES = ['Main', 'Osc', 'Filter', 'Amp', 'LFO', 'Env', 'FX', 'Patch']
SHIFT = ['Init pg', 'Ape pg', 'Copy pg', 'Paste pg', 'Compare', 'Undo', 'Latch', 'Save']
SANS = 'Liberation Sans, Arial, sans-serif'
MONO = 'JetBrains Mono, DejaVu Sans Mono, monospace'

def esc(t): return t.replace('&', '&amp;').replace('<', '&lt;')

class S:
    def __init__(s): s.e = []; s.defs = []
    def r(s, x, y, w, h, f, rx=0, op=1, st=None, sw=0, extra=''):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<rect x='{x:.1f}' y='{y:.1f}' width='{w:.1f}' height='{h:.1f}' rx='{rx}' fill='{f}' fill-opacity='{op}'{k} {extra}/>")
    def t(s, x, y, txt, c, sz=13, b=False, a='start', font=SANS, ls=0, it=False):
        s.e.append(f"<text x='{x:.1f}' y='{y:.1f}' fill='{c}' font-family='{font}' font-size='{sz}' font-weight='{'bold' if b else 'normal'}' "
                   f"font-style='{'italic' if it else 'normal'}' letter-spacing='{ls}' text-anchor='{a}'>{esc(txt)}</text>")
    def p(s, d, c, w=2, f='none', op=1, dash=None):
        da = f" stroke-dasharray='{dash}'" if dash else ''
        s.e.append(f"<path d='{d}' stroke='{c}' stroke-width='{w}' fill='{f}' fill-opacity='{op}' stroke-linejoin='round' stroke-linecap='round'{da}/>")
    def c(s, x, y, r, f, st=None, sw=0, op=1):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<circle cx='{x:.1f}' cy='{y:.1f}' r='{r}' fill='{f}' fill-opacity='{op}'{k}/>")
    def poly(s, pts, f, st=None, sw=0, op=1):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<polygon points='{' '.join(f'{a:.1f},{b:.1f}' for a, b in pts)}' fill='{f}' fill-opacity='{op}'{k}/>")
    def save(s, name):
        open(f'{OUT}/{name}.svg', 'w').write(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}'><defs>{''.join(s.defs)}</defs>" + ''.join(s.e) + '</svg>')

def path(pts): return 'M' + ' L'.join(f'{a:.1f},{b:.1f}' for a, b in pts)
def saw(x, y, w, h, n=3):
    pts = []
    for i in range(n):
        x0 = x + i * w / n; pts += [(x0, y + h), (x0 + w / n, y)]
    return pts
def adsr(x, y, w, h, a=.08, d=.22, s=.7, sl=.35):
    xa = x + w * a; xd = xa + w * d; xs = xd + w * sl
    return [(x, y + h), (xa, y), (xd, y + h * (1 - s)), (xs, y + h * (1 - s)), (x + w, y + h)]

MAIN = [('MAMA', 'SAW'), ('BABY', 'SAW +7'), ('PAPA', '40'), ('DRIFT', '15'),
        ('CUTOFF', '2.4K'), ('RESO', '25'), ('F.ENV', '+35'), ('DRIVE', '0'),
        ('ATTACK', '3MS'), ('RELEASE', '250'), ('DELAY', '20'), ('REVERB', '30')]

# ====================================================================== 1. Y2K
def y2k(page, shift=False):
    s = S()
    s.defs.append("<linearGradient id='bg' x1='0' y1='0' x2='1' y2='1'><stop offset='0' stop-color='#0B0420'/><stop offset='1' stop-color='#1A0638'/></linearGradient>"
                  "<linearGradient id='chrome' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#F4F7FF'/><stop offset='.45' stop-color='#9AA6C7'/><stop offset='.55' stop-color='#5D6788'/><stop offset='1' stop-color='#DDE3F5'/></linearGradient>"
                  "<linearGradient id='holo' x1='0' y1='0' x2='1' y2='0'><stop offset='0' stop-color='#00F0FF'/><stop offset='.5' stop-color='#C04DFF'/><stop offset='1' stop-color='#FF3DB4'/></linearGradient>")
    s.r(0, 0, W, H, 'url(#bg)')
    for i in range(0, W, 32): s.r(i, 0, 1, H, '#2A1458', op=.6)
    for j in range(0, H, 32): s.r(0, j, W, 1, '#2A1458', op=.6)
    # header with cut corner
    s.poly([(0, 0), (520, 0), (500, 34), (0, 34)], 'url(#chrome)')
    s.t(14, 25, 'MORPHANGUTAN', '#120530', 22, True, it=True, ls=1)
    s.t(510, 22, '//  HAIRY LEAD  //  TRK 05', '#00F0FF', 12, True, font=MONO)
    s.t(790, 22, f'{PAGES.index(page) + 1:02d}/{len(PAGES)}', '#FF3DB4', 14, True, 'end', MONO)
    cols = ['#00F0FF', '#C6FF00', '#C04DFF', '#FF3DB4']
    # bottom page tabs, slanted
    bw = 96
    for i, pg in enumerate(PAGES):
        x = 4 + i * 99; on = pg == page and not shift
        lab = SHIFT[i] if shift else pg.upper()
        s.poly([(x + 10, 432), (x + bw, 432), (x + bw - 10, 474), (x, 474)], 'url(#holo)' if on else '#1E0B44', '#C04DFF', 1.5)
        s.t(x + bw / 2, 459, lab, '#0B0420' if on else ('#FFB3F0' if shift else '#CFC4FF'), 12, True, 'middle', MONO)
    if page == 'Main':
        # scope in a chrome bevel
        s.r(14, 48, 300, 178, '#05010F', 10, 1, 'url(#chrome)', 3)
        s.p(path(saw(30, 80, 268, 110, 4)), 'url(#holo)', 3)
        s.t(26, 70, 'OUT >>', '#C6FF00', 11, True, font=MONO)
        # 12 params as stat rows with segment bars
        for i, (a, b) in enumerate(MAIN):
            col = i // 6; row = i % 6
            x = 330 + col * 232; y = 50 + row * 36
            sel = i == 4
            s.poly([(x, y), (x + 222, y), (x + 214, y + 30), (x - 8, y + 30)], '#2B0F5E' if sel else '#160733', cols[i % 4] if sel else '#3D1A7A', 1.5)
            s.t(x + 4, y + 20, a, '#9F8CFF', 12, True, font=MONO)
            for k in range(10):
                on = k < [7, 7, 4, 2, 6, 3, 7, 0, 1, 3, 2, 3][i]
                s.r(x + 92 + k * 9, y + 9, 6, 13, cols[i % 4] if on else '#2D1660', 1)
            s.t(x + 216, y + 20, b, '#FFFFFF', 12, True, 'end', MONO)
        # colored button bar
        for i, (lab, val) in enumerate([('CUT', '2.4K'), ('RES', '25'), ('ATK', '3MS'), ('REL', '250')]):
            x = 14 + i * 76; s.r(x, 240, 70, 52, '#0B0420', 6, 1, cols[i], 2)
            s.t(x + 35, 258, lab, cols[i], 11, True, 'middle', MONO); s.t(x + 35, 282, val, '#FFFFFF', 15, True, 'middle', MONO)
        s.t(14, 318, 'SIGNAL >> OSC > FLT > AMP > FX', '#00F0FF', 12, True, font=MONO)
        s.t(14, 340, 'HOLD MIDDLE = PLAY NOTE', '#FF3DB4', 11, True, font=MONO)
    else:
        s.r(14, 48, 500, 300, '#05010F', 10, 1, 'url(#chrome)', 3)
        pts = adsr(40, 80, 450, 220)
        s.p(path(pts) + ' Z', 'none', 0, 'url(#holo)', .25)
        s.p(path(pts), 'url(#holo)', 4)
        for (px, py), cc, l in zip(pts[1:], cols, 'ADSR'):
            s.poly([(px, py - 11), (px + 11, py), (px, py + 11), (px - 11, py)], cc)
            s.t(px, py + 4, l, '#0B0420', 11, True, 'middle', MONO)
        s.t(30, 70, 'AMP.ENV', '#C6FF00', 12, True, font=MONO)
        for i, (lab, val) in enumerate([('ATTACK', '3 MS'), ('DECAY', '400 MS'), ('SUSTAIN', '75 %'), ('RELEASE', '250 MS')]):
            x = 530; y = 48 + i * 62
            s.poly([(x, y), (x + 256, y), (x + 248, y + 54), (x - 8, y + 54)], '#160733', cols[i], 2)
            s.t(x + 6, y + 20, lab, cols[i], 12, True, font=MONO); s.t(x + 240, y + 44, val, '#FFFFFF', 22, True, 'end', MONO)
        for i, (lab, val) in enumerate([('VOL', '80'), ('PAN', 'C'), ('VEL', '70'), ('GLIDE', 'OFF')]):
            x = 14 + i * 126; s.r(x, 360, 118, 58, '#160733', 6, 1, '#3D1A7A', 1.5)
            s.t(x + 10, 380, lab, '#9F8CFF', 11, True, font=MONO); s.t(x + 108, 406, val, '#FFFFFF', 18, True, 'end', MONO)
        s.t(530, 318, 'TOGGLE A/D/S/R ON THE', '#9F8CFF', 10, True, font=MONO)
        s.t(530, 334, 'COLOURED BUTTONS', '#9F8CFF', 10, True, font=MONO)
    if shift:
        s.r(0, 0, W, H, 'none', 0, 1, '#FF3DB4', 6)
    return s

# ====================================================================== 2. Minimal (TE / Elektron)
def minimal(page, shift=False):
    s = S()
    BGc, FG, MUTE, LIN, ORANGE = '#0E0E0E', '#F2F2F2', '#6E6E6E', '#2A2A2A', '#FF5A1F'
    s.r(0, 0, W, H, BGc)
    s.t(16, 24, 'MRPH', FG, 13, True, font=MONO, ls=2)
    s.t(78, 24, 'HAIRY LEAD', MUTE, 13, font=MONO, ls=1)
    s.t(784, 24, f'T5  {PAGES.index(page) + 1}/8', MUTE, 13, a='end', font=MONO)
    s.r(16, 34, 768, 1, LIN)
    # page tabs as numbered cells, thin
    for i, pg in enumerate(PAGES):
        x = 16 + i * 96; on = pg == page and not shift
        s.r(x, 440, 90, 30, ORANGE if on else BGc, 0, 1, None if on else LIN, 1)
        s.t(x + 8, 460, str(i + 1), BGc if on else MUTE, 11, font=MONO)
        s.t(x + 82, 460, (SHIFT[i] if shift else pg).upper(), BGc if on else FG, 11, True, 'end', MONO)
    if page == 'Main':
        # 4 x 3 grid: big numbers, tiny labels, one accent
        vals = [('MAMA', 'SAW'), ('BABY', 'SAW'), ('PAPA', '40'), ('DRIFT', '15'),
                ('CUTOFF', '2.4k'), ('RESO', '25'), ('F.ENV', '+35'), ('DRIVE', '0'),
                ('ATK', '3ms'), ('REL', '250'), ('DLY', '20'), ('VERB', '30')]
        for i, (a, b) in enumerate(vals):
            x = 16 + (i % 4) * 192; y = 50 + (i // 4) * 120; sel = i == 4
            s.r(x, y, 186, 112, BGc, 0, 1, ORANGE if sel else LIN, 2 if sel else 1)
            s.t(x + 12, y + 24, a, MUTE, 11, font=MONO, ls=1)
            s.t(x + 12, y + 80, b, ORANGE if sel else FG, 40, False, font=MONO)
            # thin value arc
            frac = [.66, .66, .4, .15, .55, .25, .67, 0, .05, .3, .2, .3][i]
            cx, cy, r = x + 158, y + 30, 14
            s.c(cx, cy, r, 'none', LIN, 2)
            a0 = math.pi * 0.75; a1 = a0 + frac * math.pi * 1.5
            if frac > 0:
                s.p(f'M{cx + r * math.cos(a0):.1f},{cy + r * math.sin(a0):.1f} A{r},{r} 0 {1 if frac > 0.66 else 0} 1 {cx + r * math.cos(a1):.1f},{cy + r * math.sin(a1):.1f}', ORANGE if sel else FG, 2)
        s.t(16, 428, 'HOLD MID  PLAY      +/-  VALUE      COLOUR  QUICK EDIT', MUTE, 10, font=MONO, ls=1)
    else:
        pts = adsr(16, 70, 768, 230, .06, .2, .65, .4)
        s.p(path(pts), FG, 2)
        for (px, py), l in zip(pts[1:], 'ADSR'):
            s.r(px - 5, py - 5, 10, 10, ORANGE if l == 'D' else BGc, 0, 1, ORANGE if l == 'D' else FG, 2)
        s.r(16, 312, 768, 1, LIN)
        for i, (lab, val) in enumerate([('A', '3ms'), ('D', '400ms'), ('S', '75'), ('R', '250ms')]):
            x = 16 + i * 192; sel = i == 1
            s.t(x, 340, lab, ORANGE if sel else MUTE, 13, True, font=MONO)
            s.t(x, 392, val, ORANGE if sel else FG, 38, font=MONO)
        for i, (lab, val) in enumerate([('VOL', '80'), ('PAN', 'C'), ('VEL', '70'), ('GLIDE', 'off')]):
            s.t(16 + i * 192, 424, f'{lab} {val}', MUTE, 12, font=MONO, ls=1)
    if shift:
        s.r(2, 2, W - 4, H - 4, 'none', 0, 1, ORANGE, 2)
    return s

# ====================================================================== 3. Analog panel (knobs)
def panel(page, shift=False):
    s = S()
    s.defs.append("<radialGradient id='kn' cx='.4' cy='.35' r='.7'><stop offset='0' stop-color='#5A5A60'/><stop offset='1' stop-color='#1B1B1F'/></radialGradient>"
                  "<linearGradient id='wood' x1='0' y1='0' x2='1' y2='0'><stop offset='0' stop-color='#3B2416'/><stop offset='.5' stop-color='#5A3820'/><stop offset='1' stop-color='#3B2416'/></linearGradient>")
    s.r(0, 0, W, H, '#2B2B30')
    s.r(0, 0, 10, H, 'url(#wood)'); s.r(W - 10, 0, 10, H, 'url(#wood)')
    s.r(10, 0, W - 20, 36, '#1C1C20')
    s.t(24, 25, 'MORPHANGUTAN', '#F0E6D2', 18, True, ls=3)
    s.t(250, 24, 'polyphonic morphing synthesizer', '#A89F8C', 12, it=True)
    s.t(776, 24, 'HAIRY LEAD', '#E8A33A', 13, True, 'end')
    def knob(x, y, r, frac, lab, val, col='#E8A33A', sel=False):
        for k in range(11):
            a = math.pi * 0.75 + k / 10 * math.pi * 1.5
            s.p(f'M{x + (r + 4) * math.cos(a):.1f},{y + (r + 4) * math.sin(a):.1f} L{x + (r + 8) * math.cos(a):.1f},{y + (r + 8) * math.sin(a):.1f}', '#8C8576', 1.5)
        s.c(x, y, r, 'url(#kn)', '#F0E6D2' if sel else '#0E0E10', 2.5 if sel else 1.5)
        a = math.pi * 0.75 + frac * math.pi * 1.5
        s.p(f'M{x + r * 0.25 * math.cos(a):.1f},{y + r * 0.25 * math.sin(a):.1f} L{x + r * 0.9 * math.cos(a):.1f},{y + r * 0.9 * math.sin(a):.1f}', col, 3)
        s.t(x, y + r + 24, lab, '#F0E6D2', 11, True, 'middle', ls=1)
        s.t(x, y + r + 38, val, col, 11, False, 'middle', MONO)
    sections = [('OSCILLATORS', '#6C8CD5'), ('FILTER', '#E8A33A'), ('AMP', '#7FB069'), ('FX', '#C06C84')]
    if page == 'Main':
        for i, (name, col) in enumerate(sections):
            x = 18 + i * 192; s.r(x, 46, 186, 374, '#232328', 4, 1, '#3A3A40', 1)
            s.r(x, 46, 186, 22, col); s.t(x + 93, 62, name, '#1B1B1F', 11, True, 'middle', ls=2)
            ks = [[('MAMA', 'SAW', .66), ('BABY', 'SAW +7', .66), ('PAPA', '40', .4)],
                  [('CUTOFF', '2.4K', .55), ('RESO', '25', .25), ('F.ENV', '+35', .67)],
                  [('ATTACK', '3MS', .05), ('RELEASE', '250', .3), ('VOLUME', '80', .8)],
                  [('DELAY', '20', .2), ('REVERB', '30', .3), ('DRIVE', '0', 0)]][i]
            for k, (a, b, f) in enumerate(ks):
                knob(x + 93, 116 + k * 108, 28, f, a, b, col, sel=(i == 1 and k == 0))
    else:
        s.r(18, 46, 764, 220, '#16161A', 4, 1, '#3A3A40', 1)
        # an "oscilloscope" look envelope
        for gx in range(18, 782, 38): s.r(gx, 46, 1, 220, '#22322A')
        for gy in range(46, 266, 44): s.r(18, gy, 764, 1, '#22322A')
        pts = adsr(40, 66, 720, 180)
        s.p(path(pts), '#9BE564', 3)
        s.p(path(pts), '#9BE564', 9, op=1)
        s.e[-1] = s.e[-1].replace("stroke-width='9'", "stroke-width='9' stroke-opacity='0.15'")
        s.t(30, 64, 'AMP ENVELOPE', '#9BE564', 11, True, ls=2)
        for i, (lab, val, f) in enumerate([('ATTACK', '3 ms', .05), ('DECAY', '400 ms', .4), ('SUSTAIN', '75 %', .75), ('RELEASE', '250 ms', .3)]):
            knob(110 + i * 150, 320, 30, f, lab, val, ['#7FB069', '#E8D36A', '#8E7CF0', '#6FC8F0'][i], sel=i == 2)
        knob(700, 320, 22, .8, 'VOLUME', '80 %', '#F0E6D2')
    for i, pg in enumerate(PAGES):
        x = 18 + i * 96; on = pg == page and not shift
        s.r(x, 434, 90, 38, '#E8A33A' if on else '#1C1C20', 3, 1, '#0E0E10', 1)
        s.t(x + 45, 458, (SHIFT[i] if shift else pg).upper(), '#1B1B1F' if on else '#F0E6D2', 11, True, 'middle', ls=1)
    if shift:
        s.r(12, 2, W - 24, H - 4, 'none', 0, 1, '#E8A33A', 3)
    return s

# ====================================================================== 4. Gibbon jungle (playful, the GibGob art)
def jungle(page, shift=False):
    s = S()
    PUR, DPUR, RED, YEL, GRN, CRM, INK = '#2E1E45', '#1D1230', '#E8344E', '#FFD23F', '#3DDC84', '#FFF4E0', '#140B22'
    s.r(0, 0, W, H, DPUR)
    # leaves in the corners
    for (cx, cy, rot, col) in [(-10, 470, -30, '#1E4D3A'), (40, 490, -60, '#2B6B4F'), (800, 470, 210, '#1E4D3A'), (770, 500, 240, '#2B6B4F')]:
        s.e.append(f"<ellipse cx='{cx}' cy='{cy}' rx='90' ry='28' fill='{col}' transform='rotate({rot} {cx} {cy})'/>")
    s.r(12, 8, 776, 38, PUR, 19, 1, INK, 3)
    s.c(36, 27, 13, RED, INK, 2); s.c(31, 25, 3, CRM); s.c(41, 25, 3, CRM)
    s.t(58, 34, 'MORPHANGUTAN', YEL, 20, True)
    s.t(774, 33, 'Hairy Lead · trk 5', CRM, 13, True, 'end')
    cols = [GRN, YEL, '#B18CFF', '#5AD7FF']
    if page == 'Main':
        s.r(16, 58, 290, 200, INK, 18, 1, YEL, 3)
        s.p(path(saw(34, 100, 254, 120, 3)), YEL, 4)
        s.t(36, 88, 'what it sounds like', CRM, 12, it=True)
        for i, (a, b) in enumerate(MAIN):
            x = 322 + (i % 3) * 156; y = 58 + (i // 3) * 92; sel = i == 4
            s.r(x, y, 148, 84, PUR, 16, 1, YEL if sel else INK, 3)
            s.t(x + 14, y + 26, a.title(), CRM, 12, True)
            s.t(x + 14, y + 64, b.lower(), cols[i % 4], 26, True)
        for i, (lab, val) in enumerate([('Cutoff', '2.4k'), ('Reso', '25'), ('Attack', '3ms'), ('Release', '250')]):
            x = 16 + i * 73; s.r(x, 270, 67, 64, cols[i], 14, 1, INK, 3)
            s.t(x + 33, 293, lab, INK, 11, True, 'middle'); s.t(x + 33, 320, val, INK, 17, True, 'middle')
        s.t(18, 362, 'hold Middle to hear it 🍌'.replace('🍌', ''), CRM, 13, it=True)
    else:
        s.r(16, 58, 500, 300, INK, 20, 1, GRN, 3)
        pts = adsr(44, 90, 444, 230)
        # a vine: smooth curve through the points
        d = f'M{pts[0][0]},{pts[0][1]} C{pts[0][0] + 10},{pts[1][1] + 40} {pts[1][0] - 15},{pts[1][1]} {pts[1][0]},{pts[1][1]} ' \
            f'S{pts[2][0] - 50},{pts[2][1]} {pts[2][0]},{pts[2][1]} L{pts[3][0]},{pts[3][1]} S{pts[4][0] - 40},{pts[4][1]} {pts[4][0]},{pts[4][1]}'
        s.p(d, GRN, 6)
        for (px, py), cc, l in zip(pts[1:], cols, 'ADSR'):
            s.e.append(f"<ellipse cx='{px}' cy='{py}' rx='14' ry='9' fill='{cc}' stroke='{INK}' stroke-width='3' transform='rotate(-30 {px} {py})'/>")
            s.t(px, py + 26, l, cc, 14, True, 'middle')
        for i, (lab, val) in enumerate([('Attack', '3 ms'), ('Decay', '400 ms'), ('Sustain', '75%'), ('Release', '250 ms')]):
            y = 58 + i * 74; s.r(530, y, 256, 66, PUR, 16, 1, cols[i] if i == 0 else INK, 3)
            s.t(546, y + 26, lab, cols[i], 13, True); s.t(772, y + 52, val, CRM, 22, True, 'end')
        for i, (lab, val) in enumerate([('Volume', '80%'), ('Pan', 'C'), ('Velocity', '70%')]):
            x = 16 + i * 168; s.r(x, 370, 160, 50, PUR, 14, 1, INK, 3)
            s.t(x + 14, 400, lab, CRM, 12, True); s.t(x + 148, 401, val, YEL, 15, True, 'end')
    for i, pg in enumerate(PAGES):
        x = 12 + i * 98; on = pg == page and not shift
        s.r(x, 432, 92, 40, YEL if on else PUR, 20, 1, INK, 3)
        s.t(x + 46, 457, SHIFT[i] if shift else pg, INK if on else CRM, 13, True, 'middle')
    if shift:
        s.r(3, 3, W - 6, H - 6, 'none', 22, 1, RED, 5)
    return s

for name, fn in [('y2k', y2k), ('minimal', minimal), ('analog', panel), ('jungle', jungle)]:
    fn('Main').save(f'{name}_main')
    fn('Amp').save(f'{name}_amp')
    fn('Amp', True).save(f'{name}_amp_shift')
print('ok')
