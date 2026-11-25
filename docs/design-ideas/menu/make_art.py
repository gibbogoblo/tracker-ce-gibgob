# Converts the menu background (the user's art) into two PALETTED565 bitmaps
# for the FT812: the top (sky, clouds, palms) at half size, drawn 2x; the
# bottom strip (speech bubble, monkey) at full size. Writes a C++ header.
import sys
from PIL import Image
src, out, png = sys.argv[1], sys.argv[2], sys.argv[3]
SPLIT = 290
img = Image.open(src).convert('RGB').resize((800, 480), Image.LANCZOS)
top = img.crop((0, 0, 800, SPLIT)).resize((400, SPLIT // 2), Image.LANCZOS)
bot = img.crop((0, SPLIT, 800, 480))
def pal565(im):
    q = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    p = q.getpalette()[:768]
    p += [0] * (768 - len(p))
    words = bytearray()
    for i in range(256):
        r, g, b = p[3*i], p[3*i+1], p[3*i+2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        words += bytes([v & 0xFF, v >> 8])
    return q, bytes(q.tobytes()), bytes(words)
tq, tpix, tpal = pal565(top)
bq, bpix, bpal = pal565(bot)
# a preview of what the Mini shows (each part back through its palette, the top 2x)
prev = Image.new('RGB', (800, 480))
prev.paste(tq.convert('RGB').resize((800, SPLIT), Image.NEAREST), (0, 0))
prev.paste(bq.convert('RGB'), (0, SPLIT))
prev.save(png)
def arr(name, data):
    s = 'alignas(4) static const uint8_t %s[%d] = {\n' % (name, len(data))
    for i in range(0, len(data), 24):
        s += '    ' + ', '.join('0x%02X' % b for b in data[i:i+24]) + ',\n'
    return s + '};\n'
with open(out, 'w') as f:
    f.write('// GibGob OS: the Menu background (the GibGob artwork: sky, palms, the speech\n')
    f.write('// bubble and the monkey), made by docs/design-ideas/menu/make_art.py from\n')
    f.write('// docs/design-ideas/menu/background.jpg. PALETTED565 (one byte a pixel and\n')
    f.write('// a 256-colour RGB565 palette): the top at half size (drawn 2x), the bottom\n')
    f.write('// strip (bubble and monkey) at full size.\n#pragma once\n#include <stdint.h>\n\n')
    f.write('namespace gibgob\n{\nnamespace menuart\n{\n')
    f.write('constexpr int kSplitY = %d;     // the bottom strip starts here on the screen\n' % SPLIT)
    f.write('constexpr int kTopW = 400, kTopH = %d; // drawn 2x: 800 x %d\n' % (SPLIT // 2, SPLIT))
    f.write('constexpr int kBotW = 800, kBotH = %d;\n' % (480 - SPLIT))
    f.write(arr('kTopPixels', tpix) + arr('kTopPalette', tpal) + arr('kBotPixels', bpix) + arr('kBotPalette', bpal))
    f.write('} // namespace menuart\n} // namespace gibgob\n')
print('top', len(tpix), 'bottom', len(bpix), 'palettes', len(tpal), len(bpal))
