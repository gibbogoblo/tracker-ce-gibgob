# Shift as an additive purple-blue layer over the whole screen (FT812: BLEND_FUNC(ONE, ONE) + one rect)
import sys, os
OUT = os.path.dirname(os.path.abspath(__file__))
base = open(sys.argv[1]).read()
F = "font-family='Liberation Sans, Arial' font-weight='bold'"
tb = ("<rect x='0' y='0' width='800' height='29' fill='#1A1A20'/>"
      f"<text x='12' y='19' {F} font-size='15' fill='#E6E9EC'>GibGob OS</text>"
      f"<text x='788' y='19' {F} font-size='14' fill='#E6E9EC' text-anchor='end'>00:42</text>")
for name, col in [("5_add_off", None), ("6_add_subtle", "#1C1648"), ("7_add_medium", "#2C2272"), ("8_add_strong", "#3E30A0")]:
    layer = "" if col is None else f"<rect x='0' y='29' width='800' height='451' fill='{col}' style='mix-blend-mode:plus-lighter'/>"
    open(os.path.join(OUT, name + ".svg"), "w").write(base.replace("</svg>", tb + layer + "</svg>"))
print("ok")
