import wave, struct, math, random, os, sys
OUT = os.path.dirname(os.path.abspath(__file__))
w = wave.open('/home/user/src/tracker-ce/synth-plugin-sdk/engines/gorillagrain/GORILLA.wav')
n = w.getnframes(); data = struct.unpack('<%dh' % n, w.readframes(n))
BINS = 190
peaks = []
for b in range(BINS):
    seg = data[b*n//BINS:(b+1)*n//BINS]
    peaks.append(max(abs(x) for x in seg) / 32768)

ROWS = [
 ("Playhead", [("Sample","GORILLA"),("Position","20%"),("Scan","0.15x"),("Spray","12%")]),
 ("Grains",   [("Size","140 ms"),("Density","22.0 /s"),("Window","Smooth"),("Jitter","8%")]),
 ("Colour",   [("Sparkle","0%"),("Reverse","0%"),("Spread","60%"),("Tune","0 st"),("Fine","0 ct")]),
]
QUICK = [("Position","20%"),("Size","140 ms"),("Density","22.0/s"),("Spray","12%")]
TABS = ["MAIN","GRAINS","FILTER","AMP","LFO","ENV","FX","PATCH"]
CURSOR = (1,0)  # Grains column, Size row
FONT = "Liberation Sans, Arial, Helvetica, sans-serif"
random.seed(7)
GRAINS = [(0.20 + random.uniform(-0.06,0.06), random.uniform(0.2,1.0), random.uniform(-1,1)) for _ in range(9)]
PLAYHEAD = 0.20

def t(x,y,s,size=14,fill="#fff",anchor="start",weight="normal",extra="",mono=False):
    s = s.replace("&","&amp;")
    fam = "Roboto Mono, monospace" if mono else FONT
    return f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>'

def wavepath(x0,y0,w_,h_):
    top=[]; bot=[]
    for i,p in enumerate(peaks):
        x = x0 + i*w_/(BINS-1)
        top.append(f"{x:.1f},{y0+h_/2 - p*h_/2*0.95:.1f}")
        bot.append(f"{x:.1f},{y0+h_/2 + p*h_/2*0.95:.1f}")
    return "M" + " L".join(top) + " L" + " L".join(reversed(bot)) + " Z"

def layout(cols_y=194, col_h=228):
    # three columns
    cols=[]
    xs=[10,272,534]; cw=256
    for ci,(name,rows) in enumerate(ROWS):
        cells=[]
        for ri,(lab,val) in enumerate(rows):
            cells.append((xs[ci]+10, cols_y+28+ri*39, cw-20, 33, lab, val, (ci,ri)==CURSOR))
        cols.append((xs[ci], cols_y, cw, col_h, name, cells))
    return cols

# ------------------------------------------------------------ 1. Night jungle
def jungle():
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480">',
       '<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0A1F26"/><stop offset="1" stop-color="#040C0A"/></linearGradient>',
       '<linearGradient id="river" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0E3A3A"/><stop offset="1" stop-color="#06201C"/></linearGradient></defs>',
       '<rect width="800" height="480" fill="url(#sky)"/>']
    # moon
    s.append('<circle cx="742" cy="30" r="40" fill="#E8F7D0" opacity="0.10"/><circle cx="742" cy="30" r="18" fill="#E8F7D0" opacity="0.85"/><circle cx="750" cy="25" r="16" fill="#0A1F26"/>')
    # leaf silhouettes corners
    def leaf(cx,cy,rx,ry,rot,col):
        return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" transform="rotate({rot} {cx} {cy})" fill="{col}"/>'
    for (cx,cy,rx,ry,rot) in [(20,40,60,16,35),(-5,90,55,14,10),(60,8,50,13,-20),(785,200,50,13,-60),(790,260,45,12,-80)]:
        s.append(leaf(cx,cy,rx,ry,rot,"#0F2D1E"))
    # header
    s.append(t(18,24,"GORILLAGRAIN",22,"#CFF5D8",weight="bold"))
    s.append(t(220,24,"TRK 5  ·  Grains",13,"#5FA88A"))
    # quick boxes as leaves
    for k,(l,v) in enumerate(QUICK):
        x=330+k*96
        s.append(f'<rect x="{x}" y="10" width="88" height="34" rx="17" fill="#0F2D1E" stroke="{["#53B886","#B8AA53","#7B6FD6","#5AC8FA"][k]}" stroke-width="2"/>')
        s.append(t(x+44,20,l,10,"#7FBF9F","middle")); s.append(t(x+44,34,v,12,"#E8F7D0","middle","bold"))
    # river grain view
    gx,gy,gw,gh=14,56,772,128
    s.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="14" fill="url(#river)"/>')
    s.append(f'<path d="{wavepath(gx+10,gy+12,gw-20,gh-24)}" fill="#1E6B5A" opacity="0.75"/>')
    # moon reflection streaks
    for i in range(6):
        s.append(f'<rect x="{gx+40+i*120}" y="{gy+gh/2-1}" width="{50-i*4}" height="2" fill="#CFF5D8" opacity="0.15"/>')
    px=gx+10+PLAYHEAD*(gw-20)
    s.append(f'<rect x="{px-1}" y="{gy+6}" width="2" height="{gh-12}" fill="#E8F7D0"/>')
    s.append(f'<circle cx="{px}" cy="{gy+8}" r="5" fill="#E8F7D0"/>')
    for (p,a,yy) in GRAINS:
        x=gx+10+p*(gw-20); y=gy+gh/2+yy*40
        s.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{10*a+4:.0f}" fill="#D7F25A" opacity="{0.18*a:.2f}"/><circle cx="{x:.0f}" cy="{y:.0f}" r="{2.5+2*a:.1f}" fill="#F2FF9E" opacity="{0.5+0.5*a:.2f}"/>')
    s.append(t(gx+gw-14,gy+gh-14,"9 fireflies",11,"#5FA88A","end"))
    # columns
    for (x,y,w_,h_,name,cells) in layout():
        s.append(f'<rect x="{x}" y="{y}" width="{w_}" height="{h_}" rx="18" fill="#0B1E17" stroke="#18402E" stroke-width="1.5"/>')
        s.append(f'<path d="M{x+14} {y+14} q8 -10 18 0 q-8 10 -18 0z" fill="#3FA06A"/>')
        s.append(t(x+40,y+15,name,13,"#9FE0B5",weight="bold"))
        for (cx,cy,cw,ch,lab,val,cur) in cells:
            if cur:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="#D7F25A" opacity="0.18"/><rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="none" stroke="#D7F25A" stroke-width="2"/>')
            else:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="#10281E"/>')
            s.append(t(cx+14,cy+ch/2,lab,13,"#F2FF9E" if cur else "#7FBF9F"))
            s.append(t(cx+cw-14,cy+ch/2,val,13,"#FFFFFF" if cur else "#CFF5D8","end","bold"))
    # tabs
    for i,tb in enumerate(TABS):
        x=8+i*99; on=i==1
        s.append(f'<rect x="{x}" y="426" width="92" height="44" rx="22" fill="{"#2E7D4F" if on else "#0B1E17"}" stroke="{"#D7F25A" if on else "#18402E"}" stroke-width="{2 if on else 1.5}"/>')
        s.append(t(x+46,448,tb,13,"#F2FF9E" if on else "#7FBF9F","middle","bold"))
    s.append('</svg>'); return "\n".join(s)

# ------------------------------------------------------------ 2. Y2K futurism
def y2k():
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480">',
       '<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0B1B3A"/><stop offset="1" stop-color="#020814"/></linearGradient>',
       '<linearGradient id="chrome" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#F4F8FC"/><stop offset="0.45" stop-color="#A9B6C6"/><stop offset="0.55" stop-color="#7D8B9C"/><stop offset="1" stop-color="#DCE4EE"/></linearGradient>',
       '<linearGradient id="aqua" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8FF0FF"/><stop offset="1" stop-color="#0077E6"/></linearGradient>',
       '<linearGradient id="panel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#16305C"/><stop offset="1" stop-color="#0A1A36"/></linearGradient></defs>',
       '<rect width="800" height="480" fill="url(#bg)"/>']
    # perspective grid floor behind grain view
    for i in range(0,13):
        y=60+i*11
        s.append(f'<line x1="0" y1="{y}" x2="800" y2="{y}" stroke="#1E5BFF" stroke-opacity="{0.08+i*0.012:.2f}"/>')
    for i in range(-8,17):
        s.append(f'<line x1="{400+i*20}" y1="56" x2="{400+i*80}" y2="200" stroke="#1E5BFF" stroke-opacity="0.12"/>')
    # chrome header bar
    s.append('<rect x="0" y="0" width="800" height="50" fill="url(#chrome)"/><rect x="0" y="0" width="800" height="22" fill="#FFFFFF" opacity="0.35"/>')
    s.append(t(18,26,"GORILLAGRAIN",22,"#0B1B3A",weight="bold",extra='letter-spacing="3"'))
    s.append(t(232,26,"TRK 5 // GRAINS.EXE",12,"#24508F",mono=True))
    for k,(l,v) in enumerate(QUICK):
        x=410+k*96
        s.append(f'<rect x="{x}" y="8" width="88" height="34" rx="17" fill="url(#aqua)"/><rect x="{x+6}" y="10" width="76" height="12" rx="6" fill="#FFFFFF" opacity="0.5"/>')
        s.append(t(x+44,20,l,10,"#04254F","middle")); s.append(t(x+44,33,v,12,"#FFFFFF","middle","bold"))
    # grain view: glossy capsule
    gx,gy,gw,gh=14,58,772,126
    s.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="20" fill="#061532" stroke="url(#chrome)" stroke-width="3"/>')
    s.append(f'<path d="{wavepath(gx+12,gy+12,gw-24,gh-24)}" fill="#0F6BFF" opacity="0.55"/>')
    px=gx+12+PLAYHEAD*(gw-24)
    s.append(f'<rect x="{px-2}" y="{gy+4}" width="4" height="{gh-8}" fill="#C8FF00"/>')
    s.append(f'<rect x="{px-30}" y="{gy+gh-22}" width="60" height="16" rx="8" fill="#C8FF00"/>')
    s.append(t(px,gy+gh-14,"PLAY",10,"#061532","middle","bold"))
    for (p,a,yy) in GRAINS:
        x=gx+12+p*(gw-24); y=gy+gh/2+yy*38; r=6+8*a
        s.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{r:.0f}" fill="url(#aqua)" opacity="0.9"/><circle cx="{x-r*0.3:.0f}" cy="{y-r*0.35:.0f}" r="{r*0.35:.1f}" fill="#FFFFFF" opacity="0.8"/>')
    s.append(t(gx+gw-18,gy+18,"GRAINS: 09",11,"#8FF0FF","end",mono=True))
    for (x,y,w_,h_,name,cells) in layout():
        s.append(f'<rect x="{x}" y="{y}" width="{w_}" height="{h_}" rx="20" fill="url(#panel)" stroke="#5E7CA8" stroke-width="1.5"/>')
        s.append(f'<rect x="{x+12}" y="{y+6}" width="{w_-24}" height="20" rx="10" fill="url(#chrome)"/>')
        s.append(t(x+w_/2,y+16,name.upper(),12,"#0B1B3A","middle","bold",'letter-spacing="2"'))
        for (cx,cy,cw,ch,lab,val,cur) in cells:
            if cur:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="url(#aqua)"/><rect x="{cx+8}" y="{cy+2}" width="{cw-16}" height="12" rx="6" fill="#FFF" opacity="0.45"/>')
            else:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="#0D2247" stroke="#294A7E"/>')
            s.append(t(cx+14,cy+ch/2+1,lab,13,"#04254F" if cur else "#8FB4E8"))
            s.append(t(cx+cw-14,cy+ch/2+1,val,13,"#FFFFFF" if cur else "#E6F0FF","end","bold"))
    for i,tb in enumerate(TABS):
        x=8+i*99; on=i==1
        s.append(f'<rect x="{x}" y="426" width="92" height="44" rx="22" fill="{"url(#aqua)" if on else "url(#chrome)"}"/><rect x="{x+8}" y="428" width="76" height="14" rx="7" fill="#FFF" opacity="0.45"/>')
        s.append(t(x+46,449,tb,13,"#FFFFFF" if on else "#0B1B3A","middle","bold"))
    s.append('</svg>'); return "\n".join(s)

# ------------------------------------------------------------ 3. Y2K cyber angelic
def angelic():
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480">',
       '<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FBF7FF"/><stop offset="0.5" stop-color="#EEE8FF"/><stop offset="1" stop-color="#E6F4FF"/></linearGradient>',
       '<linearGradient id="iri" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#FFC6EA"/><stop offset="0.5" stop-color="#D6C8FF"/><stop offset="1" stop-color="#BDEBFF"/></linearGradient>',
       '<linearGradient id="iri2" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFB3E2"/><stop offset="1" stop-color="#9FD8FF"/></linearGradient></defs>',
       '<rect width="800" height="480" fill="url(#bg)"/>']
    # wings behind the title (feather ellipses)
    for i in range(6):
        s.append(f'<ellipse cx="{70-i*9}" cy="{26+i*3}" rx="{58-i*7}" ry="7" transform="rotate({-18+i*7} 70 30)" fill="#FFFFFF" stroke="#D6C8FF" stroke-width="1"/>')
        s.append(f'<ellipse cx="{238+i*9}" cy="{26+i*3}" rx="{58-i*7}" ry="7" transform="rotate({18-i*7} 238 30)" fill="#FFFFFF" stroke="#D6C8FF" stroke-width="1"/>')
    s.append('<ellipse cx="154" cy="9" rx="44" ry="6" fill="none" stroke="#F5C542" stroke-width="2.5"/>')
    s.append(t(154,30,"GORILLAGRAIN",21,"#6B5AA6","middle","bold",'letter-spacing="2"'))
    s.append(t(154,46,"✧ trk 5 · grains ✧",11,"#A08ACF","middle"))
    for k,(l,v) in enumerate(QUICK):
        x=410+k*96
        s.append(f'<rect x="{x}" y="10" width="88" height="34" rx="17" fill="#FFFFFF" stroke="url(#iri2)" stroke-width="2"/>')
        s.append(t(x+44,20,l,10,"#A08ACF","middle")); s.append(t(x+44,34,v,12,"#5A4A92","middle","bold"))
    # sparkles
    def star(x,y,r,c="#F5C542"):
        return f'<path d="M{x} {y-r} L{x+r*0.22} {y-r*0.22} L{x+r} {y} L{x+r*0.22} {y+r*0.22} L{x} {y+r} L{x-r*0.22} {y+r*0.22} L{x-r} {y} L{x-r*0.22} {y-r*0.22}Z" fill="{c}"/>'
    for (x,y,r) in [(330,18,6),(372,40,4),(786,60,5),(14,200,5),(790,410,4)]:
        s.append(star(x,y,r))
    gx,gy,gw,gh=14,58,772,126
    s.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="22" fill="#FFFFFF" stroke="url(#iri)" stroke-width="3"/>')
    s.append(f'<path d="{wavepath(gx+12,gy+12,gw-24,gh-24)}" fill="url(#iri)" opacity="0.9"/>')
    px=gx+12+PLAYHEAD*(gw-24)
    s.append(f'<rect x="{px-1}" y="{gy+18}" width="2" height="{gh-30}" fill="#B9A3F0"/>')
    s.append(f'<ellipse cx="{px}" cy="{gy+12}" rx="13" ry="4" fill="none" stroke="#F5C542" stroke-width="2.5"/>')
    for (p,a,yy) in GRAINS:
        x=gx+12+p*(gw-24); y=gy+gh/2+yy*38
        s.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{9+8*a:.0f}" fill="#FFFFFF" opacity="0.75"/>' + star(round(x),round(y),5+6*a,"#F5C542" if a>0.6 else "#C9B6FF"))
    s.append(t(gx+gw-18,gy+gh-14,"9 blessings",11,"#A08ACF","end"))
    for (x,y,w_,h_,name,cells) in layout():
        s.append(f'<rect x="{x}" y="{y}" width="{w_}" height="{h_}" rx="22" fill="#FFFFFF" fill-opacity="0.75" stroke="#D6C8FF" stroke-width="1.5"/>')
        s.append(f'<ellipse cx="{x+24}" cy="{y+12}" rx="10" ry="3.5" fill="none" stroke="#F5C542" stroke-width="2"/>')
        s.append(t(x+42,y+16,name,13,"#6B5AA6",weight="bold"))
        for (cx,cy,cw,ch,lab,val,cur) in cells:
            if cur:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="url(#iri)"/><rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="none" stroke="#FFFFFF" stroke-width="2"/>')
            else:
                s.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="17" fill="#F6F2FF"/>')
            s.append(t(cx+14,cy+ch/2,lab,13,"#5A4A92" if cur else "#9A8CC8"))
            s.append(t(cx+cw-14,cy+ch/2,val,13,"#3D2F7A" if cur else "#5A4A92","end","bold"))
    for i,tb in enumerate(TABS):
        x=8+i*99; on=i==1
        s.append(f'<rect x="{x}" y="426" width="92" height="44" rx="22" fill="{"url(#iri)" if on else "#FFFFFF"}" stroke="#D6C8FF" stroke-width="1.5"/>')
        s.append(t(x+46,448,tb,13,"#3D2F7A" if on else "#9A8CC8","middle","bold"))
    s.append('</svg>'); return "\n".join(s)

for name,f in [("1_night_jungle",jungle),("2_y2k_futurism",y2k),("3_cyber_angelic",angelic)]:
    open(os.path.join(OUT,name+".svg"),"w").write(f())
print("ok")
