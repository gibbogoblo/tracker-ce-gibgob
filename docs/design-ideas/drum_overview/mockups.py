# Drum overview mockups: three looks over the same drum grid (800 x 480, the Mini's screen)
import os, math, random
OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Liberation Sans, Arial, Helvetica, sans-serif"

def t(x,y,s,size=14,fill="#fff",anchor="start",weight="normal",extra=""):
    s = s.replace("&","&amp;")
    return f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}" dominant-baseline="middle" {extra}>{s}</text>'

# the pattern: velocity 0..1 per step (0 = off), with condition tags
PAT = {
 0: [1,0,0,0, .9,0,0,.5, 1,0,0,0, .9,0,.6,0],              # kick
 1: [0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,.4],                 # snare
 2: [.8,.4,.7,.4, .8,.4,.7,.4, .8,.4,.7,.4, .8,.4,.7,.5],   # hat
 3: [0,0,.7,0, 0,0,.6,0, 0,.7,0,0, 0,0,.8,0],              # drum 4
 4: [0,0,0,.6, 0,0,.5,0, 0,0,0,.6, 0,.5,0,0],              # drum 5
 5: [0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0],                  # clap
 6: [.5,.3,.5,.3, .5,.3,.5,.3, .5,.3,.5,.3, .5,.3,.5,.3],   # shaker (muted)
 7: [0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,.9,.9],                # fill perc
}
TAGS = {(1,15):"50%", (5,12):"1:2", (7,14):"FILL", (7,15):"FILL", (0,14):"2:4"}
MUTED = {6}
CUR = (1,4)        # snare, step 5
PLAY = 6           # playhead on step 7
GX, GY, ROWH, STEPW, NAMEW = 136, 66, 40, 40.5, 120

def grid_geom():
    cells=[]
    for r in range(8):
        for s in range(16):
            x = GX + s*STEPW + (s//4)*1.5
            cells.append((r,s,x,GY+r*ROWH))
    return cells

def common(th, names, bg, step_empty, step_hit, playhead, row_band=None, extra_top=""):
    s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480">', bg]
    # header
    s.append(t(16,22,th["title"],20,th["title_c"],weight="bold",extra='letter-spacing="1.5"'))
    s.append(t(16,42,"Pattern 03  ·  64 steps  ·  128 BPM",12,th["sub_c"]))
    s.append(extra_top)
    q=[("Prob","1:2"),("Vel","96"),("Nud","0"),("Pitch","0")]
    for k,(l,v) in enumerate(q):
        x=432+k*90
        s.append(f'<rect x="{x}" y="9" width="82" height="40" rx="{th["r"]}" fill="{th["box_bg"]}" stroke="{th["qc"][k]}" stroke-width="2"/>')
        s.append(t(x+41,21,l,10,th["qc"][k],"middle","bold")); s.append(t(x+41,37,v,14,th["box_t"],"middle","bold"))
    # rows
    for r in range(8):
        y=GY+r*ROWH
        if row_band: s.append(row_band(r,y))
        if r==CUR[0]:
            s.append(f'<rect x="8" y="{y+3}" width="{NAMEW}" height="{ROWH-6}" rx="{th["r"]}" fill="{th["sel_bg"]}"/>')
        nc = th["mute_c"] if r in MUTED else (th["name_sel"] if r==CUR[0] else th["name_c"])
        s.append(t(18,y+ROWH/2,names[r],14,nc,weight="bold"))
        if r in MUTED: s.append(t(NAMEW-2,y+ROWH/2,"M",11,th["mute_c"],"end","bold"))
    s.append(playhead(GX+PLAY*STEPW+(PLAY//4)*1.5))
    for (r,st,x,y) in grid_geom():
        v=PAT[r][st]; cx=x+STEPW/2; cy=y+ROWH/2
        muted = r in MUTED
        s.append(step_hit(cx,cy,v,muted) if v>0 else step_empty(cx,cy,muted))
        tag=TAGS.get((r,st))
        if tag:
            s.append(f'<rect x="{cx-15}" y="{y+ROWH-13}" width="30" height="12" rx="6" fill="{th["tag_bg"]}"/>')
            s.append(t(cx,y+ROWH-7,tag,9,th["tag_c"],"middle","bold"))
    # cursor
    cx=GX+CUR[1]*STEPW+(CUR[1]//4)*1.5; cy=GY+CUR[0]*ROWH
    s.append(f'<rect x="{cx+1}" y="{cy+2}" width="{STEPW-2}" height="{ROWH-4}" rx="{min(th["r"],10)}" fill="none" stroke="{th["cur"]}" stroke-width="3"/>')
    # page line
    py=GY+8*ROWH+14
    s.append(t(18,py,"Steps 1-16",12,th.get("low_c",th["sub_c"]),weight="bold"))
    for p in range(4):
        x=GX+p*164
        s.append(f'<rect x="{x+2}" y="{py-4}" width="160" height="8" rx="4" fill="{th["page_on"] if p==0 else th["page_off"]}"/>')
    # top-row bar: steps 1-8 of the cursor row
    s.append(t(400,py+22,"Top row: Snare steps 1-8   ·   Shift: CHIMP OUT",11,th.get("low_c",th["sub_c"]),"middle"))
    for i in range(8):
        x=8+i*98.5; on=PAT[CUR[0]][i]>0
        s.append(f'<rect x="{x}" y="430" width="91" height="42" rx="{th["r"]}" fill="{th["bar_on"] if on else th["bar_off"]}" stroke="{th["bar_line"]}" stroke-width="1.5"/>')
        s.append(t(x+45.5,451,str(i+1),15,th["bar_on_t"] if on else th["bar_t"],"middle","bold"))
    s.append('</svg>')
    return "\n".join(s)

# ------------------------------------------------------------ 1. Sunset beach bongo dreams
def beach():
    th=dict(title="BONGO DREAMS",title_c="#FFE7B8",sub_c="#FFC9A8",r=14,box_bg="#3A1E45",box_t="#FFF3DC",
            qc=["#FF9E6B","#FFD36B","#FF7AA8","#B79CFF"],sel_bg="#FF8A5B",name_c="#FFD9B8",name_sel="#2A1030",
            mute_c="#9A7A92",tag_bg="#2A1030",tag_c="#FFD36B",cur="#FFFFFF",page_on="#FFE3C4",page_off="#6A3E70",
            bar_on="#FF9E6B",bar_off="#3A1E45",bar_line="#FFB38A",bar_on_t="#2A1030",bar_t="#FFC9A8",low_c="#FFF0E0")
    bg=['<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2B1446"/><stop offset=".45" stop-color="#8E2E6A"/><stop offset=".72" stop-color="#F0625A"/><stop offset=".86" stop-color="#FFB25B"/><stop offset="1" stop-color="#F7D9A2"/></linearGradient>',
        '<radialGradient id="bongo" cx=".4" cy=".35" r=".7"><stop offset="0" stop-color="#FFF1D2"/><stop offset=".7" stop-color="#F2C089"/><stop offset="1" stop-color="#C77A3A"/></radialGradient>',
        '<linearGradient id="foam" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFFFFF" stop-opacity=".0"/><stop offset=".5" stop-color="#FFE3F0" stop-opacity=".35"/><stop offset="1" stop-color="#FFFFFF" stop-opacity=".0"/></linearGradient></defs>',
        '<rect width="800" height="480" fill="url(#sky)"/>']
    # retro sun with stripes, setting behind the grid
    bg.append('<circle cx="640" cy="300" r="150" fill="#FFB347" opacity=".55"/>')
    for i in range(7):
        bg.append(f'<rect x="480" y="{300+i*20}" width="320" height="{3+i*2}" fill="#F0625A" opacity=".7"/>')
    # sea and sand behind the bar
    bg.append('<rect y="408" width="800" height="72" fill="#3C2A6B" opacity=".55"/>')
    for i in range(5):
        bg.append(f'<path d="M0 {414+i*12} Q 100 {409+i*12} 200 {414+i*12} T 400 {414+i*12} T 600 {414+i*12} T 800 {414+i*12}" stroke="#FFC9A8" stroke-opacity=".35" fill="none"/>')
    # palm on the right of the header
    bg.append('<path d="M772 64 Q 765 30 776 0" stroke="#2A1030" stroke-width="7" fill="none"/>')
    for a in [-150,-120,-60,-30,-95]:
        x2=776+46*math.cos(math.radians(a)); y2=4+46*math.sin(math.radians(a))+30
        bg.append(f'<path d="M776 4 Q {(776+x2)/2} {y2-30} {x2:.0f} {y2:.0f}" stroke="#2A1030" stroke-width="6" fill="none" stroke-linecap="round"/>')
    # grid panel (sunset shows through)
    bg.append('<rect x="4" y="60" width="792" height="354" rx="18" fill="#22102E" opacity=".62"/>')
    def empty(cx,cy,m):
        return f'<circle cx="{cx}" cy="{cy}" r="7" fill="none" stroke="#FFC9A8" stroke-opacity="{.18 if m else .35}" stroke-width="1.5"/>'
    def hit(cx,cy,v,m):
        rr=8+8*v
        if m: return f'<circle cx="{cx}" cy="{cy}" r="{rr}" fill="#6E5470"/>'
        return (f'<circle cx="{cx}" cy="{cy}" r="{rr+2}" fill="#7A3B1E"/>'
                f'<circle cx="{cx}" cy="{cy}" r="{rr}" fill="url(#bongo)"/>'
                f'<circle cx="{cx}" cy="{cy}" r="{rr*.45}" fill="none" stroke="#C77A3A" stroke-opacity=".5"/>')
    def ph(x):
        return f'<rect x="{x}" y="{GY-2}" width="{STEPW}" height="{8*ROWH+4}" rx="12" fill="url(#foam)"/><rect x="{x}" y="{GY-2}" width="{STEPW}" height="{8*ROWH+4}" rx="12" fill="none" stroke="#FFE3F0" stroke-opacity=".5"/>'
    names=["Kick","Snare","Hat","Bongo hi","Bongo lo","Clap","Shaker","Conga"]
    return common(th,names,"\n".join(bg),empty,hit,ph)

# ------------------------------------------------------------ 2. Rain forest rhythm
def rain():
    random.seed(3)
    th=dict(title="RAINFOREST RHYTHM",title_c="#CFFBE6",sub_c="#8FD8B8",r=12,box_bg="#0B2E23",box_t="#E9FFF5",
            qc=["#7CF2C4","#F2E27C","#7CC8F2","#C9A2F2"],sel_bg="#2FB69A",name_c="#A9EBD0",name_sel="#04201A",
            mute_c="#4E7A69",tag_bg="#04201A",tag_c="#F2E27C",cur="#E9FFF5",page_on="#2FB69A",page_off="#164A3B",
            bar_on="#2FB69A",bar_off="#0B2E23",bar_line="#2F7A62",bar_on_t="#04201A",bar_t="#8FD8B8")
    bg=['<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0D3A2C"/><stop offset=".6" stop-color="#062419"/><stop offset="1" stop-color="#03140E"/></linearGradient>',
        '<radialGradient id="drop" cx=".38" cy=".6" r=".7"><stop offset="0" stop-color="#E8FFF8"/><stop offset=".5" stop-color="#7CF2C4"/><stop offset="1" stop-color="#1E8E74"/></radialGradient>',
        '<linearGradient id="mist" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#BFF5E0" stop-opacity="0"/><stop offset=".5" stop-color="#BFF5E0" stop-opacity=".10"/><stop offset="1" stop-color="#BFF5E0" stop-opacity="0"/></linearGradient></defs>',
        '<rect width="800" height="480" fill="url(#sky)"/>']
    for y in (120,250,370): bg.append(f'<rect y="{y}" width="800" height="40" fill="url(#mist)"/>')
    # big leaves in the corners
    def leaf(cx,cy,rx,ry,rot,c):
        return (f'<g transform="rotate({rot} {cx} {cy})"><ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{c}"/>'
                f'<line x1="{cx-rx}" y1="{cy}" x2="{cx+rx}" y2="{cy}" stroke="#062419" stroke-width="2"/>'
                + "".join(f'<line x1="{cx-rx+i*rx/3}" y1="{cy}" x2="{cx-rx+i*rx/3+12}" y2="{cy-ry*.9}" stroke="#062419" stroke-width="1.5"/>' for i in range(1,6)) + '</g>')
    for a in [(690,20,70,22,-12,"#145C40"),(760,52,60,18,30,"#0F4A34"),(-10,470,80,24,-25,"#0F4A34"),(790,470,70,20,200,"#145C40")]:
        bg.append(leaf(*a))
    # rain
    for i in range(70):
        x=random.uniform(0,820); y=random.uniform(0,480); l=random.uniform(10,22)
        bg.append(f'<line x1="{x:.0f}" y1="{y:.0f}" x2="{x-4:.0f}" y2="{y+l:.0f}" stroke="#BFF5E0" stroke-opacity=".22" stroke-width="1.2"/>')
    bg.append('<rect x="4" y="60" width="792" height="354" rx="16" fill="#031A12" opacity=".55"/>')
    def empty(cx,cy,m):
        return f'<ellipse cx="{cx}" cy="{cy+2}" rx="6" ry="3" fill="#7CF2C4" fill-opacity="{.08 if m else .18}"/>'
    def hit(cx,cy,v,m):
        k=.55+.6*v
        path=f'M{cx} {cy-15*k} C {cx+3*k} {cy-8*k} {cx+10*k} {cy-1*k} {cx+10*k} {cy+5*k} A {10*k} {10*k} 0 0 1 {cx-10*k} {cy+5*k} C {cx-10*k} {cy-1*k} {cx-3*k} {cy-8*k} {cx} {cy-15*k} Z'
        if m: return f'<path d="{path}" fill="#2E5246"/>'
        return f'<path d="{path}" fill="url(#drop)"/><circle cx="{cx-3*k:.1f}" cy="{cy+2*k:.1f}" r="{2*k:.1f}" fill="#FFFFFF" opacity=".8"/>'
    def ph(x):
        cx=x+STEPW/2
        out=f'<rect x="{x}" y="{GY-2}" width="{STEPW}" height="{8*ROWH+4}" rx="10" fill="#7CF2C4" opacity=".10"/>'
        for rr in (8,14,20): out+=f'<ellipse cx="{cx}" cy="{GY+8*ROWH+8}" rx="{rr}" ry="{rr/3.5}" fill="none" stroke="#7CF2C4" stroke-opacity="{.9-rr/30}"/>'
        # little frog riding the playhead
        fy=GY-6
        out+=(f'<ellipse cx="{cx}" cy="{fy}" rx="11" ry="7" fill="#3FD18C"/><circle cx="{cx-6}" cy="{fy-6}" r="4" fill="#3FD18C"/><circle cx="{cx+6}" cy="{fy-6}" r="4" fill="#3FD18C"/>'
              f'<circle cx="{cx-6}" cy="{fy-6}" r="1.8" fill="#04201A"/><circle cx="{cx+6}" cy="{fy-6}" r="1.8" fill="#04201A"/>')
        return out
    names=["Kick","Snare","Hat","Rim","Woodblock","Clap","Rainstick","Perc"]
    return common(th,names,"\n".join(bg),empty,hit,ph)

# ------------------------------------------------------------ 3. Safari cliff atmosphere
def baboon(x,y,sc=1.0,flip=False,c="#3A1E12"):
    f=-1 if flip else 1
    def X(dx): return x+dx*sc*f
    def Y(dy): return y+dy*sc
    return (f'<g fill="{c}">'
            f'<path d="M{X(-2)} {Y(0)} Q {X(-26)} {Y(-6)} {X(-22)} {Y(-30)} Q {X(-20)} {Y(-44)} {X(-34)} {Y(-40)}" stroke="{c}" stroke-width="{4*sc}" fill="none" stroke-linecap="round"/>'  # arched tail
            f'<ellipse cx="{X(0)}" cy="{Y(-16)}" rx="{13*sc}" ry="{17*sc}" transform="rotate({-12*f} {X(0)} {Y(-16)})"/>'   # body
            f'<circle cx="{X(8)}" cy="{Y(-36)}" r="{9*sc}"/>'                       # head
            f'<ellipse cx="{X(17)}" cy="{Y(-33)}" rx="{8*sc}" ry="{4.5*sc}" transform="rotate({18*f} {X(17)} {Y(-33)})"/>'  # long muzzle
            f'<ellipse cx="{X(-2)} " cy="{Y(-2)}" rx="{14*sc}" ry="{4*sc}"/>'      # sitting haunches
            f'<rect x="{X(6) - (3*sc if not flip else 0)}" y="{Y(-14)}" width="{3*sc}" height="{14*sc}" rx="{1.5*sc}"/>'  # arm
            '</g>')

def safari():
    th=dict(title="SAFARI CLIFFS",title_c="#3A1E12",sub_c="#6B3A1E",r=8,box_bg="#F7E2B5",box_t="#3A1E12",
            qc=["#B8451F","#C98A12","#5E7A2A","#7A4A8E"],sel_bg="#F2B04C",name_c="#F7E2B5",name_sel="#3A1E12",
            mute_c="#B0876A",tag_bg="#3A1E12",tag_c="#F2B04C",cur="#FFF4D6",page_on="#F2B04C",page_off="#7A4428",
            bar_on="#F2B04C",bar_off="#5A2C18",bar_line="#8E4A2B",bar_on_t="#3A1E12",bar_t="#E8C08A")
    th["sub_c"]="#5A2C18"; th["low_c"]="#F2D3A0"
    bg=['<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#F6D28A"/><stop offset="1" stop-color="#E89A55"/></linearGradient>',
        '<radialGradient id="stone" cx=".4" cy=".35" r=".7"><stop offset="0" stop-color="#FFE4A6"/><stop offset=".6" stop-color="#F2B04C"/><stop offset="1" stop-color="#B8701F"/></radialGradient></defs>',
        '<rect width="800" height="480" fill="url(#sky)"/>',
        '<circle cx="610" cy="40" r="26" fill="#FFF0C4" opacity=".8"/>',
        '<path d="M0 60 Q 120 46 260 56 T 520 52 T 800 58 V 70 H 0 Z" fill="#C98451" opacity=".6"/>']
    # acacia tree on the cliff top
    bg.append('<path d="M330 62 L 334 30 L 320 22 M334 30 L 346 20" stroke="#3A1E12" stroke-width="4" fill="none"/>'
              '<ellipse cx="334" cy="20" rx="40" ry="8" fill="#3A1E12"/><ellipse cx="354" cy="15" rx="20" ry="5" fill="#3A1E12"/>')
    # cliff body: the rows are rock ledges
    bg.append('<rect x="0" y="60" width="800" height="420" fill="#7A3E22"/>')
    def band(r,y):
        c=["#9D5532","#8E4A2B"][r%2]
        jag=" ".join(f"L {x} {y+random.uniform(-2,2):.1f}" for x in range(0,801,40))
        return (f'<path d="M0 {y} {jag} L 800 {y+ROWH} L 0 {y+ROWH} Z" fill="{c}"/>'
                f'<path d="M0 {y+ROWH-1} H 800" stroke="#5A2C18" stroke-width="2"/>'
                f'<path d="M0 {y+3} H 800" stroke="#C98451" stroke-opacity=".35"/>')
    random.seed(5)
    bg.append('<rect y="400" width="800" height="80" fill="#5A2C18"/>')
    # baboons along the cliff top
    bg.append(baboon(232,62,.9)+baboon(280,62,.72,True)+baboon(400,62,.55))
    def empty(cx,cy,m):
        return f'<ellipse cx="{cx}" cy="{cy}" rx="9" ry="7" fill="#5A2C18" opacity="{.5 if m else .9}"/>'
    def hit(cx,cy,v,m):
        rx=9+6*v; ry=7+5*v
        if m: return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="#8A6A55"/>'
        return (f'<ellipse cx="{cx}" cy="{cy+2}" rx="{rx}" ry="{ry}" fill="#4A2412"/>'
                f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="url(#stone)"/>')
    def ph(x):
        cx=x+STEPW/2
        return (f'<rect x="{x}" y="{GY}" width="{STEPW}" height="{8*ROWH}" fill="#FFE4A6" opacity=".16"/>'
                + baboon(cx-4,GY-1,.8,c="#2A140B"))   # a baboon walks the cliff top with the beat
    names=["Kick","Snare","Hat","Djembe","Tom","Clap","Shaker","Perc"]
    return common(th,names,"\n".join(bg),empty,hit,ph,row_band=band)

for name,f in [("1_sunset_beach_bongo_dreams",beach),("2_rainforest_rhythm",rain),("3_safari_cliffs",safari)]:
    open(os.path.join(OUT,name+".svg"),"w").write(f())
print("ok")
