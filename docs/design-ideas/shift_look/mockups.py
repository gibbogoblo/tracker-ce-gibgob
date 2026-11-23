# Shift look ideas, drawn over a real render of the main screen (Shift contents, no Shift look)
import os, sys, re
OUT = os.path.dirname(os.path.abspath(__file__))
base = open(sys.argv[1]).read()          # ...shiftbase.svg
lighten = open(sys.argv[2]).read()       # ...shift.svg (today's look)
F = "font-family='Liberation Sans, Arial' font-weight='bold'"


def titlebar(color="#1A1A20", text="GibGob OS", tcol="#E6E9EC", extra=""):
    return (f"<rect x='0' y='0' width='800' height='29' fill='{color}'/>"
            f"<text x='12' y='19' {F} font-size='15' fill='{tcol}'>{text}</text>"
            f"<text x='788' y='19' {F} font-size='14' fill='{tcol}' text-anchor='end'>00:42</text>{extra}")


def chip(x, y, text, bg="#FF7A2F", fg="#101418"):
    w = 9 * len(text) + 16
    return (f"<rect x='{x}' y='{y}' width='{w}' height='20' rx='10' fill='{bg}'/>"
            f"<text x='{x+w/2}' y='{y+14.5}' {F} font-size='12' fill='{fg}' text-anchor='middle' letter-spacing='1'>{text}</text>")


def add(svg, top, under=""):
    body = svg.replace("</svg>", "")
    head, rest = body.split("\n", 1)
    return head + "\n" + under + rest + top + "</svg>"


ideas = {}
# 0. today: everything lightened + a thin red frame
ideas["0_today_lighten"] = add(lighten, titlebar())
# 1. spotlight: the clips dim, what Shift changes (header values + top row) stays bright
ideas["1_spotlight"] = add(base, "<rect x='0' y='67' width='800' height='355' fill='#000' fill-opacity='0.58'/>"
                           + titlebar(extra=chip(400, 5, "SHIFT")))
# 2. hot header: the title bar turns orange and says what Shift does; orange rules frame the active parts
ideas["2_hot_header"] = add(base, titlebar("#FF7A2F", "SHIFT  ·  top row: CHIMP OUT  ·  colours: all len / BPM / swing", "#101418")
                            + "<rect x='0' y='65' width='800' height='3' fill='#FF7A2F'/><rect x='0' y='421' width='800' height='3' fill='#FF7A2F'/>")
# 3. hazard frame: a striped border (one small repeating picture on the Mini)
stripes = ("<defs><pattern id='hz' width='16' height='16' patternUnits='userSpaceOnUse' patternTransform='rotate(45)'>"
           "<rect width='8' height='16' fill='#FFB347'/><rect x='8' width='8' height='16' fill='#101418'/></pattern></defs>")
frame = ("<rect x='0' y='29' width='800' height='8' fill='url(#hz)'/><rect x='0' y='472' width='800' height='8' fill='url(#hz)'/>"
         "<rect x='0' y='29' width='8' height='451' fill='url(#hz)'/><rect x='792' y='29' width='8' height='451' fill='url(#hz)'/>")
ideas["3_hazard_frame"] = add(base, frame + titlebar(extra=chip(400, 5, "SHIFT", "#FFB347")), stripes)
# 4. tint wash: one see-through orange layer over the clips
ideas["4_tint_wash"] = add(base, "<rect x='0' y='67' width='800' height='355' fill='#FF6A1F' fill-opacity='0.24'/>"
                           + titlebar(extra=chip(400, 5, "SHIFT")))
for k, v in ideas.items():
    open(os.path.join(OUT, k + ".svg"), "w").write(v)
print("ok")
