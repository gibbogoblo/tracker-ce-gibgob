# The original GibGob synth page layouts (Main, Filter, Amp) in four themes.
import math, os, random
OUT = os.path.dirname(os.path.abspath(__file__))
W, H = 800, 480
PAGES = ['Main', 'Osc', 'Filter', 'Amp', 'LFO', 'Env', 'FX', 'Patch']
SANS = 'Liberation Sans, Arial, sans-serif'
SERIF = 'Liberation Serif, serif'
MONO = 'JetBrains Mono, DejaVu Sans Mono, monospace'

def esc(t): return t.replace('&', '&amp;').replace('<', '&lt;')
class S:
    def __init__(s): s.e = []; s.d = []
    def r(s, x, y, w, h, f, rx=0, op=1, st=None, sw=0):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<rect x='{x:.1f}' y='{y:.1f}' width='{w:.1f}' height='{h:.1f}' rx='{rx}' fill='{f}' fill-opacity='{op}'{k}/>")
    def t(s, x, y, txt, c, sz=13, b=False, a='start', font=SANS, it=False, ls=0, glow=None):
        g = f" filter='url(#{glow})'" if glow else ''
        s.e.append(f"<text x='{x:.1f}' y='{y:.1f}' fill='{c}' font-family='{font}' font-size='{sz}' font-weight='{'bold' if b else 'normal'}' font-style='{'italic' if it else 'normal'}' letter-spacing='{ls}' text-anchor='{a}'{g}>{esc(txt)}</text>")
    def p(s, d, c, w=2, f='none', op=1, glow=None):
        g = f" filter='url(#{glow})'" if glow else ''
        s.e.append(f"<path d='{d}' stroke='{c}' stroke-width='{w}' fill='{f}' fill-opacity='{op}' stroke-linejoin='round' stroke-linecap='round'{g}/>")
    def c(s, x, y, r, f, st=None, sw=0, op=1):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<circle cx='{x:.1f}' cy='{y:.1f}' r='{r}' fill='{f}' fill-opacity='{op}'{k}/>")
    def poly(s, pts, f, st=None, sw=0, op=1):
        k = f" stroke='{st}' stroke-width='{sw}'" if st else ''
        s.e.append(f"<polygon points='{' '.join(f'{a:.1f},{b:.1f}' for a, b in pts)}' fill='{f}' fill-opacity='{op}'{k}/>")
    def save(s, name):
        open(f'{OUT}/{name}.svg', 'w').write(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}'><defs>{''.join(s.d)}</defs>" + ''.join(s.e) + '</svg>')
def path(pts): return 'M' + ' L'.join(f'{a:.1f},{b:.1f}' for a, b in pts)

# ---------------------------------------------------------------- themes
class Theme:
    font = SANS
    def __init__(self, s): self.s = s

class Y2K(Theme):
    name = 'y2k'; font = MONO
    text, dim, dark, sel = '#FFFFFF', '#A898FF', '#0B0420', '#00F0FF'
    btn = ['#00F0FF', '#C6FF00', '#C04DFF', '#FF3DB4']
    curve, fill = 'url(#holo)', 'url(#holo)'
    graph_bg = '#05010F'
    def defs(self):
        self.s.d.append("<linearGradient id='bg' x1='0' y1='0' x2='1' y2='1'><stop offset='0' stop-color='#0B0420'/><stop offset='1' stop-color='#1A0638'/></linearGradient>"
                        "<linearGradient id='chrome' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#F4F7FF'/><stop offset='.45' stop-color='#9AA6C7'/><stop offset='.55' stop-color='#5D6788'/><stop offset='1' stop-color='#DDE3F5'/></linearGradient>"
                        "<linearGradient id='holo' x1='0' y1='0' x2='1' y2='0'><stop offset='0' stop-color='#00F0FF'/><stop offset='.5' stop-color='#C04DFF'/><stop offset='1' stop-color='#FF3DB4'/></linearGradient>")
    def background(self):
        s = self.s; s.r(0, 0, W, H, 'url(#bg)')
        for i in range(0, W, 32): s.r(i, 0, 1, H, '#2A1458', op=.5)
        for j in range(0, H, 32): s.r(0, j, W, 1, '#2A1458', op=.5)
    def header(self, page):
        s = self.s
        s.poly([(0, 0), (440, 0), (424, 29), (0, 29)], 'url(#chrome)')
        s.t(10, 21, 'MORPHANGUTAN', '#120530', 17, True, it=True, font=SANS, ls=1)
        s.t(450, 20, '// TRK 05 // HAIRY LEAD', '#00F0FF', 12, True, font=MONO)
        s.t(790, 20, f'{PAGES.index(page) + 1:02d}/08 {page.upper()}', '#FF3DB4', 12, True, 'end', MONO)
    def panel(self, x, y, w, h, sel=False):
        c = 10
        self.s.poly([(x + c, y), (x + w, y), (x + w, y + h - c), (x + w - c, y + h), (x, y + h), (x, y + c)], '#150632', self.sel if sel else '#4A2390', 2 if sel else 1.5)
    def row(self, x, y, w, h, sel=False):
        self.s.poly([(x + 6, y), (x + w, y), (x + w - 6, y + h), (x, y + h)], '#22094A' if sel else '#1A0740', self.sel if sel else '#3D1A7A', 2 if sel else 1)
    def graph(self, x, y, w, h):
        self.s.r(x, y, w, h, self.graph_bg, 8, 1, 'url(#chrome)', 2)
    def box(self, i, x, y, w, h, lab, val):
        s = self.s; col = self.btn[i]
        s.poly([(x + 8, y), (x + w, y), (x + w - 8, y + h), (x, y + h)], '#0B0420', col, 2)
        s.t(x + 14, y + 19, lab, col, 10, True, font=MONO); s.t(x + w - 14, y + 19, val, '#FFFFFF', 12, True, 'end', MONO)
    def tab(self, i, x, y, w, h, lab, on):
        s = self.s
        s.poly([(x + 10, y), (x + w, y), (x + w - 10, y + h), (x, y + h)], 'url(#holo)' if on else '#1E0B44', '#C04DFF', 1.5)
        s.t(x + w / 2, y + 27, lab.upper(), '#0B0420' if on else '#CFC4FF', 12, True, 'middle', MONO)
    def bar(self, x, y, w, frac, col):
        for k in range(12):
            self.s.r(x + k * (w / 12), y, w / 12 - 3, 8, col if k < frac * 12 else '#2D1660', 1)
    def handle(self, x, y, col, lab):
        self.s.poly([(x, y - 11), (x + 11, y), (x, y + 11), (x - 11, y)], col); self.s.t(x, y + 4, lab, '#0B0420', 11, True, 'middle', MONO)
    accent = ['#00F0FF', '#C04DFF', '#FF3DB4', '#C6FF00']

class Win95(Theme):
    name = 'win95'
    text, dim, dark, sel = '#000000', '#404040', '#000000', '#000080'
    btn = ['#008000', '#808000', '#800080', '#008080']
    curve, fill = '#000080', '#000080'
    graph_bg = '#FFFFFF'
    def defs(self):
        self.s.d.append("<linearGradient id='title' x1='0' y1='0' x2='1' y2='0'><stop offset='0' stop-color='#000080'/><stop offset='1' stop-color='#1084D0'/></linearGradient>")
    def bevel(self, x, y, w, h, inset=False, face='#C0C0C0'):
        s = self.s; hi, lo = ('#808080', '#FFFFFF') if inset else ('#FFFFFF', '#808080')
        s.r(x, y, w, h, face)
        s.r(x, y, w, 2, hi); s.r(x, y, 2, h, hi)
        s.r(x, y + h - 2, w, 2, lo); s.r(x + w - 2, y, 2, h, lo)
        if not inset:
            s.r(x, y + h - 1, w, 1, '#000000'); s.r(x + w - 1, y, 1, h, '#000000')
    def background(self):
        self.s.r(0, 0, W, H, '#008080'); self.bevel(2, 2, W - 4, H - 4)
    def header(self, page):
        s = self.s
        s.r(5, 5, W - 10, 22, 'url(#title)')
        s.r(10, 9, 14, 14, '#C0C0C0'); s.c(17, 16, 4, '#FF0000')
        s.t(30, 21, 'MORPHANGUTAN.EXE - Hairy Lead (Track 5)', '#FFFFFF', 13, True)
        for k, g in enumerate(['_', '□', '×']):
            x = W - 64 + k * 19; self.bevel(x, 8, 17, 16); s.t(x + 8.5, 21, g, '#000000', 12, True, 'middle')
        s.t(790, 46, f'Page {PAGES.index(page) + 1} of 8', '#000000', 12, a='end')
    def panel(self, x, y, w, h, sel=False):
        self.bevel(x, y, w, h)
        if sel: self.s.r(x + 3, y + 3, w - 6, h - 6, 'none', 0, 1, '#000000', 1)
    def row(self, x, y, w, h, sel=False):
        self.bevel(x, y, w, h, inset=True, face='#000080' if sel else '#FFFFFF')
    def graph(self, x, y, w, h):
        self.bevel(x, y, w, h, inset=True, face='#FFFFFF')
    def box(self, i, x, y, w, h, lab, val):
        s = self.s; self.bevel(x, y, w, h)
        s.r(x + 6, y + 8, 10, 10, self.btn[i], 0, 1, '#000000', 1)
        s.t(x + 22, y + 18, lab, '#000000', 11); s.t(x + w - 8, y + 18, val, '#000000', 12, True, 'end')
    def tab(self, i, x, y, w, h, lab, on):
        self.bevel(x, y, w, h, inset=on, face='#C0C0C0')
        self.s.t(x + w / 2 + (1 if on else 0), y + 27 + (1 if on else 0), lab, '#000000', 13, on, 'middle')
        if on: self.s.r(x + 6, y + 6, w - 12, h - 12, 'none', 0, 1, '#000000', 1)
    def bar(self, x, y, w, frac, col):
        self.bevel(x, y - 2, w, 12, inset=True, face='#FFFFFF')
        n = int(frac * 14)
        for k in range(n): self.s.r(x + 3 + k * (w - 6) / 14, y + 1, (w - 6) / 14 - 2, 6, '#000080')
    def handle(self, x, y, col, lab):
        self.bevel(x - 9, y - 9, 18, 18); self.s.t(x, y + 4, lab, '#000000', 11, True, 'middle')
    accent = ['#000080', '#800000', '#008000', '#808000']

class Vapor(Theme):
    name = 'vaporwave'; font = SERIF
    text, dim, dark, sel = '#FFFFFF', '#FFB3E6', '#2D0B4E', '#01CDFE'
    btn = ['#05FFA1', '#FFFB96', '#B967FF', '#01CDFE']
    curve, fill = '#FF71CE', '#FF71CE'
    graph_bg = '#1B0B3A'
    def defs(self):
        self.s.d.append("<linearGradient id='sky' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#2D0B4E'/><stop offset='.55' stop-color='#7A1E7A'/><stop offset='1' stop-color='#FF71CE'/></linearGradient>"
                        "<linearGradient id='sun' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#FFFB96'/><stop offset='1' stop-color='#FF71CE'/></linearGradient>"
                        "<filter id='glow'><feGaussianBlur stdDeviation='2.2' result='b'/><feMerge><feMergeNode in='b'/><feMergeNode in='SourceGraphic'/></feMerge></filter>")
    def background(self):
        s = self.s; s.r(0, 0, W, H, 'url(#sky)')
        s.c(640, 300, 120, 'url(#sun)', op=.55)
        for k in range(6): s.r(520, 250 + k * 18, 240, 4 + k, '#2D0B4E', op=.55)
        for k in range(12):
            y = 330 + k * k * 1.6; s.r(0, y, W, 1, '#01CDFE', op=.35)
        for k in range(-10, 11):
            s.p(f'M{400 + k * 12},330 L{400 + k * 120},480', '#01CDFE', 1, op=.3)
            s.e[-1] = s.e[-1].replace("stroke-width='1'", "stroke-width='1' stroke-opacity='0.3'")
    def header(self, page):
        s = self.s; s.r(0, 0, W, 29, '#1B0B3A', 0, .8)
        s.t(10, 22, 'ＭＯＲＰＨＡＮＧＵＴＡＮ', '#FF71CE', 16, True, glow='glow')
        s.t(330, 21, 'ハ ゛ナナ  ·  hairy lead  ·  trk 5', '#01CDFE', 12, it=True)
        s.t(790, 21, f'{page} {PAGES.index(page) + 1}/8', '#FFFB96', 13, True, 'end', it=True)
    def panel(self, x, y, w, h, sel=False):
        self.s.r(x, y, w, h, '#1B0B3A', 2, .82, self.sel if sel else '#FF71CE', 2 if sel else 1.2)
    def row(self, x, y, w, h, sel=False):
        self.s.r(x, y, w, h, '#2D0B4E', 2, .85, self.sel if sel else '#B967FF', 2 if sel else 1)
    def graph(self, x, y, w, h):
        self.s.r(x, y, w, h, self.graph_bg, 2, .85, '#FF71CE', 1.5)
    def box(self, i, x, y, w, h, lab, val):
        s = self.s; s.r(x, y, w, h, '#1B0B3A', 2, .85, self.btn[i], 1.5)
        s.t(x + 8, y + 19, lab, self.btn[i], 12, it=True, font=SERIF); s.t(x + w - 8, y + 19, val, '#FFFFFF', 13, True, 'end')
    def tab(self, i, x, y, w, h, lab, on):
        s = self.s; s.r(x, y, w, h, '#FF71CE' if on else '#1B0B3A', 2, .9 if on else .75, '#01CDFE' if on else '#B967FF', 1.5)
        s.t(x + w / 2, y + 28, lab, '#2D0B4E' if on else '#FFFFFF', 15, True, 'middle', SERIF, it=True)
    def bar(self, x, y, w, frac, col):
        self.s.r(x, y, w, 6, '#4B1D6E', 3); self.s.r(x, y, w * frac, 6, col, 3)
    def handle(self, x, y, col, lab):
        self.s.c(x, y, 9, col, '#2D0B4E', 2); self.s.t(x, y + 4, lab, '#2D0B4E', 11, True, 'middle')
    accent = ['#FF71CE', '#01CDFE', '#05FFA1', '#B967FF']

class Aero(Theme):
    name = 'aero'
    text, dim, dark, sel = '#0B2A4A', '#3F6A8F', '#0B2A4A', '#0078D7'
    btn = ['#3CB043', '#F2C200', '#8E5CD9', '#1FA2E8']
    curve, fill = '#0A84D6', '#0A84D6'
    graph_bg = '#FFFFFF'
    def defs(self):
        self.s.d.append("<linearGradient id='sky' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#4FB3F2'/><stop offset='.6' stop-color='#BEE7FF'/><stop offset='1' stop-color='#7FD48A'/></linearGradient>"
                        "<linearGradient id='glass' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#FFFFFF' stop-opacity='.85'/><stop offset='.5' stop-color='#FFFFFF' stop-opacity='.55'/><stop offset='.51' stop-color='#E3F3FF' stop-opacity='.5'/><stop offset='1' stop-color='#FFFFFF' stop-opacity='.7'/></linearGradient>"
                        "<linearGradient id='aqua' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#B9E8FF'/><stop offset='.5' stop-color='#3FA9F5'/><stop offset='.51' stop-color='#1478D0'/><stop offset='1' stop-color='#4FC3FF'/></linearGradient>"
                        "<linearGradient id='gbtn' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#FFFFFF' stop-opacity='.9'/><stop offset='.5' stop-color='#E9F4FB' stop-opacity='.8'/><stop offset='.51' stop-color='#CFE6F5' stop-opacity='.8'/><stop offset='1' stop-color='#F4FBFF' stop-opacity='.9'/></linearGradient>")
    def background(self):
        s = self.s; s.r(0, 0, W, H, 'url(#sky)')
        random.seed(11)
        for _ in range(14):
            x, y, r = random.uniform(0, W), random.uniform(40, 420), random.uniform(6, 26)
            s.c(x, y, r, '#FFFFFF', '#FFFFFF', 1, .18)
        s.e.append("<ellipse cx='120' cy='480' rx='260' ry='60' fill='#5CC36A' fill-opacity='.7'/><ellipse cx='700' cy='490' rx='300' ry='70' fill='#4DB85C' fill-opacity='.7'/>")
    def header(self, page):
        s = self.s; s.r(0, 0, W, 29, 'url(#aqua)')
        s.r(0, 0, W, 14, '#FFFFFF', 0, .25)
        s.t(12, 21, 'MORPHANGUTAN', '#FFFFFF', 15, True)
        s.t(150, 20, 'Hairy Lead · Track 5', '#EAF7FF', 12)
        s.t(790, 20, f'{page} · {PAGES.index(page) + 1}/8', '#FFFFFF', 12, True, 'end')
    def panel(self, x, y, w, h, sel=False):
        self.s.r(x, y, w, h, 'url(#glass)', 10, 1, self.sel if sel else '#FFFFFF', 2 if sel else 1.2)
    def row(self, x, y, w, h, sel=False):
        self.s.r(x, y, w, h, 'url(#gbtn)', 8, 1, self.sel if sel else '#9CCBEA', 2 if sel else 1)
    def graph(self, x, y, w, h):
        self.s.r(x, y, w, h, '#FFFFFF', 8, .8, '#9CCBEA', 1.2)
    def box(self, i, x, y, w, h, lab, val):
        s = self.s; s.r(x, y, w, h, 'url(#gbtn)', 9, 1, self.btn[i], 2)
        s.c(x + 12, y + 15, 5, self.btn[i]); s.t(x + 22, y + 19, lab, '#3F6A8F', 11); s.t(x + w - 8, y + 19, val, '#0B2A4A', 12, True, 'end')
    def tab(self, i, x, y, w, h, lab, on):
        s = self.s; s.r(x, y, w, h, 'url(#aqua)' if on else 'url(#gbtn)', 14, 1, '#1478D0' if on else '#9CCBEA', 1.2)
        s.r(x + 3, y + 2, w - 6, h / 2 - 2, '#FFFFFF', 12, .35)
        s.t(x + w / 2, y + 28, lab, '#FFFFFF' if on else '#0B2A4A', 14, True, 'middle')
    def bar(self, x, y, w, frac, col):
        self.s.r(x, y, w, 8, '#D6EAF8', 4, 1, '#9CCBEA', 1); self.s.r(x, y, w * frac, 8, col, 4); self.s.r(x, y, w * frac, 3, '#FFFFFF', 2, .5)
    def handle(self, x, y, col, lab):
        self.s.c(x, y, 10, col, '#FFFFFF', 2); self.s.c(x - 3, y - 3, 3, '#FFFFFF', op=.7); self.s.t(x, y + 4, lab, '#FFFFFF', 11, True, 'middle')
    accent = ['#0A84D6', '#F28C28', '#3CB043', '#8E5CD9']

# ---------------------------------------------------------------- the original layouts
def frame(T, page, title, boxes):
    s = S(); th = T(s); th.defs(); th.background(); th.header(page)
    s.t(12, 53, title, th.text if T is not Aero else '#0B2A4A', 15, True, font=th.font, it=(T is Vapor))
    for i, (lab, val) in enumerate(boxes):
        th.box(i, W - 4 * 116 - 2 + i * 116, 33, 112, 28, lab, val)
    bw = (W - 8 - 28) / 8
    for i, pg in enumerate(PAGES):
        th.tab(i, 4 + i * (bw + 4), 430, bw, 44, pg, pg == page)
    return s, th

def page_main(T):
    s, th = frame(T, 'Main', 'Overview', [('Cutoff', '2.4k'), ('Reso', '25%'), ('Attack', '3ms'), ('Release', '250ms')])
    th.graph(10, 72, 300, 148); s.t(20, 90, 'Output', th.dim, 11, font=th.font)
    pts = []
    for k in range(4): x0 = 22 + k * 70; pts += [(x0, 205), (x0 + 70, 100)]
    s.p(path(pts), th.curve, 2.5)
    labels = [('MAMA shape', 'Saw', .66), ('BABY shape', 'Saw +7c', .66), ('PAPA level', '40%', .4), ('Drift', '15%', .15),
              ('Cutoff', '2.4 kHz', .55), ('Resonance', '25%', .25), ('Filter env', '+35%', .67), ('Drive', '0%', 0),
              ('Attack', '3 ms', .05), ('Release', '250 ms', .3), ('Delay mix', '20%', .2), ('Reverb mix', '30%', .3)]
    for i, (a, b, f) in enumerate(labels):
        cx = 322 + (i % 3) * 158; cy = 72 + (i // 3) * 60
        th.row(cx, cy, 150, 52, sel=(i == 4))
        s.t(cx + 10, cy + 19, a, th.dim, 11, font=th.font); s.t(cx + 140, cy + 19, b, th.text if not (i == 4 and T is Win95) else '#FFFFFF', 13, True, 'end', th.font)
        if T is Win95 and i == 4:
            s.e[-2] = s.e[-2].replace(f"fill='{th.dim}'", "fill='#FFFFFF'")
        th.bar(cx + 10, cy + 32, 130, f, th.accent[i // 3])
    th.panel(10, 230, 300, 104); s.t(20, 250, 'Signal path', th.dim, 11, font=th.font)
    for i, lab in enumerate(['OSC', 'FILTER', 'AMP', 'FX']):
        x = 22 + i * 72; c = th.accent[i]
        if T is Win95: th.bevel(x, 264, 60, 34); s.t(x + 30, 286, lab, '#000000', 12, True, 'middle')
        else:
            s.r(x, 264, 60, 34, c, 6 if T is not Y2K else 0); s.t(x + 30, 286, lab, th.dark if T is not Aero else '#FFFFFF', 12, True, 'middle', th.font)
        if i < 3: s.p(f'M{x + 60},281 L{x + 72},281', th.dim, 2)
    s.t(20, 322, 'MAMA + BABY + PAPA › LP 24 › ADSR › FX', th.dim, 11, font=th.font)
    s.t(12, 352, 'Hold Middle: play a note · Shift + top: page tools', th.dim if T is not Vapor else '#FFFFFF', 11, font=th.font)
    s.save(f'{th.name}_1_main')

def page_filter(T):
    s, th = frame(T, 'Filter', 'Filter', [('Cutoff', '2.4k'), ('Reso', '25%'), ('Drive', '0%'), ('Env amt', '+35%')])
    th.graph(10, 72, 500, 350); s.t(22, 92, 'Response', th.dim, 11, font=th.font)
    gx, gy, gw, gh = 40, 110, 450, 260
    grid = '#C0C0C0' if T is Win95 else ('#D6EAF8' if T is Aero else '#3A2A5E')
    for f in [100, 1000, 10000]:
        xx = gx + math.log10(f / 20) / 3 * gw; s.r(xx, gy, 1, gh, grid); s.t(xx, gy + gh + 16, f'{f // 1000}k' if f >= 1000 else str(f), th.dim, 10, a='middle', font=th.font)
    pts = []; fc, q = 2400, 1.6
    for i in range(201):
        f = 20 * 10 ** (i / 200 * 3); x = f / fc
        mag = 1 / math.sqrt((1 - x * x) ** 2 + (x / q) ** 2); db = max(-48, min(12, 40 * math.log10(mag + 1e-9)))
        pts.append((gx + i / 200 * gw, min(gy + gh, gy + 30 + (-db) / 48 * (gh - 30))))
    d = path(pts)
    s.p(d + f' L{gx + gw},{gy + gh} L{gx},{gy + gh} Z', 'none', 0, th.fill, .18); s.p(d, th.curve, 2.5)
    rows = [('Type', 'LP 24'), ('Cutoff', '2.4 kHz'), ('Resonance', '25%'), ('Drive', '0%'), ('Key track', '50%'), ('Env 1 → cutoff', '+35%')]
    for k, (a, b) in enumerate(rows):
        y = 72 + k * 50; th.row(520, y, 270, 44, sel=(k == 1))
        tc = '#FFFFFF' if (T is Win95 and k == 1) else th.text
        s.t(532, y + 27, a, '#FFFFFF' if (T is Win95 and k == 1) else th.dim, 12, font=th.font); s.t(780, y + 27, b, tc, 14, True, 'end', th.font)
    s.save(f'{th.name}_3_filter')

def page_amp(T):
    s, th = frame(T, 'Amp', 'Amp envelope', [('Attack', '3 ms'), ('Decay', '400 ms'), ('Sustain', '75%'), ('Release', '250 ms')])
    th.graph(10, 72, 520, 300)
    ax, ay, aw, ah = 34, 96, 472, 240
    A = (ax + 40, ay); D = (ax + 170, ay + ah * .25); Sx = (ax + 330, ay + ah * .25); R = (ax + aw, ay + ah)
    d = f'M{ax},{ay + ah} Q{ax + 18},{ay + 20} {A[0]},{A[1]} Q{A[0] + 40},{D[1]} {D[0]},{D[1]} L{Sx[0]},{Sx[1]} Q{Sx[0] + 50},{R[1]} {R[0]},{R[1]}'
    s.p(d + ' Z', 'none', 0, th.fill, .16); s.p(d, th.curve, 3)
    for (px, py), col, l in [(A, th.btn[0]), (D, th.btn[1]), (Sx, th.btn[2]), (R, th.btn[3])] and zip([A, D, Sx, R], th.btn, 'ADSR'):
        th.handle(px, py, col, l)
    s.t(ax, ay + ah + 24, 'Coloured buttons pick A / D / S / R (toggle)', th.dim, 11, font=th.font)
    rows = [('Volume', '80%'), ('Pan', 'C'), ('Velocity', '70%'), ('Glide mode', 'Always'), ('Glide time', '0 ms')]
    for k, (a, b) in enumerate(rows):
        y = 72 + k * 60; th.row(540, y, 250, 52)
        s.t(552, y + 31, a, th.dim, 12, font=th.font); s.t(780, y + 31, b, th.text, 14, True, 'end', th.font)
    s.save(f'{th.name}_4_amp')

for T in (Y2K, Win95, Vapor, Aero):
    page_main(T); page_filter(T); page_amp(T)
print('ok')
