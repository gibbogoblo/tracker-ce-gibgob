import math, os
OUT=__import__('os').path.dirname(__import__('os').path.abspath(__file__))
W,H=800,480
BG='#121218'; PANEL='#26262E'; PANEL2='#1C1C24'; LINE='#3A3946'; TEXT='#E6E9EC'; DIM='#8A949C'; DARK='#101418'
NOTE='#53B886'; INSTR='#B8AA53'; FX1='#7B6FD6'; FX2='#5AC8FA'; ACC='#E07B39'; MEL='#5F53B8'; SEL='#FFFFFF'
PAGES=['Main','Osc','Filter','Amp','LFO','Env','FX','Patch']
def esc(t): return t.replace('&','&amp;').replace('<','&lt;')
class S:
    def __init__(s): s.e=[]
    def r(s,x,y,w,h,f,rx=4,op=1,stroke=None,sw=0):
        st=f" stroke='{stroke}' stroke-width='{sw}'" if stroke else ''
        s.e.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}' fill='{f}' fill-opacity='{op}'{st}/>")
    def t(s,x,y,txt,c=TEXT,sz=13,b=False,a='start'):
        s.e.append(f"<text x='{x}' y='{y}' fill='{c}' font-size='{sz}' font-weight='{'bold' if b else 'normal'}' text-anchor='{a}'>{esc(txt)}</text>")
    def p(s,d,c,w=2,f='none',op=1):
        s.e.append(f"<path d='{d}' stroke='{c}' stroke-width='{w}' fill='{f}' fill-opacity='{op}' stroke-linejoin='round' stroke-linecap='round'/>")
    def svg(s): return f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' font-family='Liberation Sans, Arial, sans-serif'>"+''.join(s.e)+'</svg>'
def frame(page, title, boxes):
    s=S(); s.r(0,0,W,H,BG,0)
    s.r(0,0,W,29,PANEL,0); s.t(10,20,'MORPHANGUTAN',TEXT,15,True); s.t(150,20,'Track 5  ·  Patch: Hairy Lead',DIM,13)
    s.t(790,20,f'{PAGES.index(page)+1}/8 {page}',DIM,13,a='end')
    s.t(12,53,title,TEXT,14,True)
    cols=[NOTE,INSTR,FX1,FX2]
    for i,(lab,val) in enumerate(boxes):
        x=W-4*116-2+i*116
        s.r(x,33,112,30,PANEL,3)
        s.r(x,33,4,30,cols[i],0)
        s.t(x+10,52,lab,DIM,11); s.t(x+104,52,val,TEXT,13,True,'end')
    bw=(W-8-28)/8
    for i,pg in enumerate(PAGES):
        x=4+i*(bw+4); on=pg==page
        s.r(x,430,bw,44,ACC if on else PANEL,4,1 if on else 1)
        s.t(x+bw/2,457,pg,DARK if on else TEXT,14,True,'middle')
    return s
def knob(s,x,y,w,lab,val,frac,col=MEL,sel=False):
    s.r(x,y,w,52,PANEL2,4,1,SEL if sel else None,2 if sel else 0)
    s.t(x+8,y+18,lab,DIM,11); s.t(x+w-8,y+18,val,TEXT,13,True,'end')
    s.r(x+8,y+30,w-16,8,LINE,3); s.r(x+8,y+30,(w-16)*frac,8,col,3)
def wave(shape,x,y,w,h,col,s,cycles=1.5):
    pts=[]
    for i in range(0,121):
        u=i/120*cycles; ph=u-math.floor(u)
        def sh(k,p):
            return [math.sin(2*math.pi*p), 1-4*abs(p-0.5) if True else 0, 2*p-1 if p<1 else 0, 1 if p<0.5 else -1][k]
        k=int(shape); f=shape-k; k2=min(3,k+1)
        v=sh(k,ph)*(1-f)+sh(k2,ph)*f
        pts.append((x+i/120*w, y+h/2-v*h*0.42))
    s.p('M'+' L'.join(f'{a:.1f},{b:.1f}' for a,b in pts),col,2)
def save(s,name):
    open(f'{OUT}/{name}.svg','w').write(s.svg())

# 1 Main
s=frame('Main','Overview',[('Cutoff','2.4k'),('Reso','25%'),('Attack','3ms'),('Release','250ms')])
s.r(10,72,300,148,PANEL2,6); s.t(20,90,'Output',DIM,11)
wave(2.4,20,100,280,110,'#B7AEF5',s,3)
labels=[('MAMA shape','Saw',0.66),('BABY shape','Saw +7c',0.66),('PAPA level','40%',0.4),('Drift','15%',0.15),
        ('Cutoff','2.4 kHz',0.55),('Resonance','25%',0.25),('Filter env','+35%',0.67),('Drive','0%',0),
        ('Attack','3 ms',0.05),('Release','250 ms',0.3),('Delay mix','20%',0.2),('Reverb mix','30%',0.3)]
for i,(a,b,f) in enumerate(labels):
    cx=322+(i%3)*158; cy=72+(i//3)*60
    if cx+150>790: pass
    knob(s,cx,cy,150,a,b,f,[MEL,MEL,MEL,MEL,ACC,ACC,ACC,ACC,NOTE,NOTE,FX2,FX2][i],sel=(i==4))
s.r(10,230,300,104,PANEL2,6); s.t(20,250,'Signal path',DIM,11)
for i,(lab,c) in enumerate([('OSC',MEL),('FILTER',ACC),('AMP',NOTE),('FX',FX2)]):
    x=22+i*72; s.r(x,264,60,34,c,6); s.t(x+30,286,lab,DARK,12,True,'middle')
    if i<3: s.p(f'M{x+60},281 L{x+72},281',DIM,2)
s.t(20,322,'MAMA + BABY + PAPA  ›  LP 24  ›  ADSR  ›  Delay · Reverb',DIM,11)
s.t(12,350,'Top buttons: pages · D-pad: pick · +/-: change · Middle: play a note · coloured: quick edit (toggle)',DIM,11)
save(s,'1_main')

# 2 Osc
s=frame('Osc','Oscillators',[('MAMA','Saw'),('BABY','Saw'),('Detune','+7c'),('PAPA','40%')])
for i,(name,sh,c,rows) in enumerate([('MAMA',2.0,MEL,[('Shape','Saw'),('Tune','0'),('Fine','0c'),('Level','80%')]),
                                     ('BABY',1.6,'#7B6FD6',[('Shape','Tri›Saw'),('Tune','+12'),('Fine','+7c'),('Level','60%')]),
                                     ('PAPA',0.0,'#4FA3E0',[('Shape','Sine'),('Octave','-1'),('Level','40%'),('','')])]):
    x=10+i*262; s.r(x,72,254,290,PANEL2,6,1,SEL if i==1 else None,2 if i==1 else 0)
    s.t(x+12,94,name,c,15,True)
    s.r(x+12,104,230,80,BG,4); wave(sh,x+16,108,222,72,c,s,2)
    s.t(x+12,200,'Sine — Tri — Saw — Square',DIM,10)
    s.r(x+12,206,230,6,LINE,3); s.r(x+12+sh/3*224,203,6,12,TEXT,2)
    for k,(a,b) in enumerate(rows):
        if not a: continue
        y=222+k*34; s.r(x+12,y,230,28,PANEL,4,1,SEL if (i==1 and k==2) else None,2 if (i==1 and k==2) else 0)
        s.t(x+22,y+19,a,DIM,12); s.t(x+232,y+19,b,TEXT,13,True,'end')
knob(s,10,370,254,'Noise','0%',0,DIM); knob(s,272,370,254,'Drift','15%',0.15,DIM)
s.r(534,370,256,52,PANEL2,4); s.t(546,400,'Morph: 0 sine · 1 tri · 2 saw · 3 square',DIM,11)
save(s,'2_osc')

# 3 Filter
s=frame('Filter','Filter',[('Cutoff','2.4k'),('Reso','25%'),('Drive','0%'),('Env amt','+35%')])
s.r(10,72,500,350,PANEL2,6); s.t(22,92,'Response',DIM,11)
gx,gy,gw,gh=40,110,450,260
for f in [100,1000,10000]:
    xx=gx+math.log10(f/20)/math.log10(1000)*gw; s.r(xx,gy,1,gh,LINE,0); s.t(xx,gy+gh+16,f'{f//1000}k' if f>=1000 else f'{f}',DIM,10,a='middle')
for db in [0,-24]:
    yy=gy+(-db)/48*(gh-30)+30; s.r(gx,yy,gw,1,LINE,0); s.t(gx-6,yy+4,f'{db}',DIM,10,a='end')
pts=[]; fc=2400; q=1.6
for i in range(0,201):
    f=20*10**(i/200*3); x=f/fc
    mag=1/math.sqrt((1-x*x)**2+(x/q)**2); mag=mag*mag
    db=20*math.log10(mag+1e-9); db=max(-48,min(12,db))
    pts.append((gx+i/200*gw, min(gy+gh, gy+30+(-db)/48*(gh-30))))
d='M'+' L'.join(f'{a:.1f},{b:.1f}' for a,b in pts)
s.p(d+f' L{gx+gw},{gy+gh} L{gx},{gy+gh} Z',ACC,0,ACC,0.18); s.p(d,ACC,2.5)
xx=gx+math.log10(fc/20)/3*gw; s.r(xx-1,gy,2,gh,TEXT,0,0.5); s.t(xx+6,gy+14,'2.4 kHz',TEXT,11,True)
rows=[('Type','LP 24'),('Cutoff','2.4 kHz'),('Resonance','25%'),('Drive','0%'),('Key track','50%'),('Env 1 → cutoff','+35%')]
for k,(a,b) in enumerate(rows):
    y=72+k*50; s.r(520,y,270,44,PANEL2,4,1,SEL if k==1 else None,2 if k==1 else 0)
    s.t(532,y+27,a,DIM,12); s.t(780,y+27,b,TEXT,14,True,'end')
s.t(522,388,'Types: LP 12 · LP 24 · Band · High · Comb + · Comb −',DIM,10)
save(s,'3_filter')

# 4 Amp
s=frame('Amp','Amp envelope',[('Attack','3 ms'),('Decay','400 ms'),('Sustain','75%'),('Release','250 ms')])
s.r(10,72,520,300,PANEL2,6)
ax,ay,aw,ah=34,96,472,240
A=(ax+40,ay); D=(ax+170,ay+ah*0.25); Sx=(ax+330,ay+ah*0.25); R=(ax+aw,ay+ah)
s.p(f'M{ax},{ay+ah} Q{ax+18},{ay+20} {A[0]},{A[1]} Q{A[0]+40},{D[1]} {D[0]},{D[1]} L{Sx[0]},{Sx[1]} Q{Sx[0]+50},{R[1]} {R[0]},{R[1]}',NOTE,3)
s.p(f'M{ax},{ay+ah} Q{ax+18},{ay+20} {A[0]},{A[1]} Q{A[0]+40},{D[1]} {D[0]},{D[1]} L{Sx[0]},{Sx[1]} Q{Sx[0]+50},{R[1]} {R[0]},{R[1]} Z',NOTE,0,NOTE,0.14)
for (px,py),c,l in [(A,NOTE,'A'),(D,INSTR,'D'),(Sx,FX1,'S'),(R,FX2,'R')]:
    s.e.append(f"<circle cx='{px}' cy='{py}' r='9' fill='{c}'/>"); s.t(px,py+4,l,DARK,11,True,'middle')
s.r(ax+250,ay+ah*0.25-2,2,4,TEXT,0); s.t(ax+250,ay+ah*0.25-12,'▼ now',TEXT,10,a='middle')
s.t(ax,ay+ah+24,'Coloured buttons pick A / D / S / R (toggle). The dot shows where a playing note is.',DIM,11)
rows=[('Volume','80%'),('Pan','C'),('Velocity','70%'),('Glide mode','Always'),('Glide time','0 ms')]
for k,(a,b) in enumerate(rows):
    y=72+k*60; s.r(540,y,250,52,PANEL2,4)
    s.t(552,y+31,a,DIM,12); s.t(780,y+31,b,TEXT,14,True,'end')
save(s,'4_amp')

# 5 LFO
s=frame('LFO','LFOs',[('LFO1 rate','1/8'),('LFO1 amt','+20%'),('LFO2 rate','0.3 Hz'),('LFO2 amt','-40%')])
for i,(name,shape,rows,c) in enumerate([('LFO 1','Triangle',[('Shape','Triangle'),('Rate','1/8 sync'),('Retrig','On'),('→ Cutoff','+20%'),('→ BABY morph','+10%')],ACC),
                                        ('LFO 2','Smooth random',[('Shape','Smooth rnd'),('Rate','0.3 Hz'),('Retrig','Free'),('→ Pan','-40%'),('→ Off','')],'#53B886')]):
    x=10+i*394; s.r(x,72,386,350,PANEL2,6,1,SEL if i==0 else None,2 if i==0 else 0)
    s.t(x+14,96,name,c,15,True); s.t(x+372,96,shape,DIM,11,a='end')
    s.r(x+14,106,358,80,BG,4)
    pts=[]
    import random; random.seed(3)
    vals=[random.uniform(-1,1) for _ in range(8)]
    for k in range(0,181):
        u=k/180*4
        if i==0: v=1-4*abs((u%1)-0.5)
        else:
            a=int(u*1.5); f=u*1.5-a; v=vals[a]*(1-f)+vals[a+1]*f
        pts.append((x+18+k/180*350, 146-v*32))
    s.p('M'+' L'.join(f'{a:.1f},{b:.1f}' for a,b in pts),c,2)
    for k,(a,b) in enumerate(rows):
        y=198+k*44; s.r(x+14,y,358,38,PANEL,4)
        s.t(x+24,y+24,a,DIM,12); s.t(x+362,y+24,b,TEXT,13,True,'end')
        if a.startswith('→') and b:
            val=float(b.strip('%+'))/100; cx0=x+200
            s.r(cx0,y+16,100,6,LINE,3)
            s.r(cx0+50+min(0,val)*50,y+16,abs(val)*50,6,c,3)
save(s,'5_lfo')

# 6 Env
s=frame('Env','Mod envelopes',[('Env1 dec','350ms'),('Env1 amt','+35%'),('Env2 att','500ms'),('Env2 amt','+15%')])
for i,(name,dests,c,shape) in enumerate([('ENV 1',[('→ Cutoff','+35%'),('→ Off','')],INSTR,(0.02,0.35,0.2,0.3)),
                                         ('ENV 2',[('→ BABY morph','+15%'),('→ Delay mix','+30%')],'#D6639E',(0.5,0.5,0.5,0.5))]):
    x=10+i*394; s.r(x,72,386,350,PANEL2,6)
    s.t(x+14,96,name,c,15,True)
    gx,gy,gw,gh=x+20,108,346,110
    a,dd,su,r=shape; tot=a+dd+0.4+r
    X=lambda t: gx+t/tot*gw
    pts=[(gx,gy+gh),(X(a),gy),(X(a+dd),gy+gh*(1-su)),(X(a+dd+0.4),gy+gh*(1-su)),(gx+gw,gy+gh)]
    d='M'+' L'.join(f'{p:.1f},{q:.1f}' for p,q in pts)
    s.p(d+' Z',c,0,c,0.15); s.p(d,c,2.5)
    for k,(lab,v) in enumerate([('A',f'{int(a*1000)} ms'),('D',f'{int(dd*1000)} ms'),('S',f'{int(su*100)}%'),('R',f'{int(r*1000)} ms')]):
        bx=x+14+k*90; s.r(bx,230,84,40,PANEL,4); s.t(bx+8,247,lab,DIM,11); s.t(bx+76,262,v,TEXT,13,True,'end')
    for k,(lab,v) in enumerate(dests):
        y=284+k*48; s.r(x+14,y,358,40,PANEL,4)
        s.t(x+24,y+25,lab,DIM,12); s.t(x+362,y+25,v,TEXT,13,True,'end')
save(s,'6_env')

# 7 FX
s=frame('FX','Effects',[('Dly time','1/8'),('Dly mix','20%'),('Verb dec','60%'),('Verb mix','30%')])
s.r(10,72,386,350,PANEL2,6); s.t(24,96,'DELAY',FX2,15,True); s.t(382,96,'Tape',DIM,11,a='end')
for k in range(6):
    hh=110*(0.65**k); x=40+k*55
    s.r(x,210-hh,18,hh,FX2,3,0.85 if k==0 else 0.55)
s.t(40,228,'echoes on 1/8 · feedback 45%',DIM,11)
for k,(a,b) in enumerate([('Type','Tape'),('Time','1/8'),('Feedback','45%'),('Mix','20%')]):
    y=242+k*44; s.r(24,y,358,38,PANEL,4); s.t(34,y+24,a,DIM,12); s.t(372,y+24,b,TEXT,13,True,'end')
s.r(404,72,386,350,PANEL2,6); s.t(418,96,'REVERB',FX1,15,True); s.t(776,96,'Plate',DIM,11,a='end')
pts=[]
import random; random.seed(5)
for k in range(0,300):
    t=k/300; env=math.exp(-t*4)
    pts.append((420+k*1.16, 160+random.uniform(-1,1)*48*env))
s.p('M'+' L'.join(f'{a:.1f},{b:.1f}' for a,b in pts),FX1,1.2)
s.t(420,228,'tail 60% · tone 55%',DIM,11)
for k,(a,b) in enumerate([('Type','Plate'),('Decay','60%'),('Tone','55%'),('Mix','30%')]):
    y=242+k*44; s.r(418,y,358,38,PANEL,4,1,SEL if k==3 else None,2 if k==3 else 0); s.t(428,y+24,a,DIM,12); s.t(766,y+24,b,TEXT,13,True,'end')
save(s,'7_fx')

# 8 Patch
s=frame('Patch','Patches',[('','—'),('','—'),('Go Ape','30%'),('','—')])
s.r(10,72,386,350,PANEL2,6); s.t(24,96,'Patches (MORPHANGUTAN)',DIM,11)
for k,n in enumerate(['Hairy Lead','Fat Saw Bass','Glass Pad','Morph Pluck','Jungle Reese','Garage Organ','Wobble Sub']):
    y=106+k*40; on=k==0
    s.r(24,y,358,34,ACC if on else PANEL,4); s.t(36,y+22,n,DARK if on else TEXT,13,True)
for k,(a,b) in enumerate([('Load','Middle'),('Save','Shift+Middle'),('Save as…','keyboard'),('Init patch','clean start'),('Go Ape','randomise 30%'),('Copy / Paste','Copy / Shift+Copy')]):
    y=72+k*52; s.r(404,y,386,46,PANEL2,4,1,SEL if k==4 else None,2 if k==4 else 0)
    s.t(418,y+28,a,TEXT,14,True); s.t(778,y+28,b,DIM,12,a='end')
s.t(406,404,'Macros could live here as a small list, or go.',DIM,11)
save(s,'8_patch')
print('ok')
