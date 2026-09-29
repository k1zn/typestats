"""Extract TSpeedButton glyphs from DFM resources into resources/icons/<Form>_<Button>.png.
VCL uses the bottom-left pixel as the transparent colour; NumGlyphs=2 -> take the first (enabled) half.
Also extracts MAINICON and the tray ImageList."""
import struct, io, os, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
d = open('TypeStats.exe', 'rb').read()
RVA, OFF = 0x1e0000, 0x1d5800
def o(r): return r - RVA + OFF
res = {'TFORM1': (0x1e8780, 34329), 'TFORM3': (0x1f1490, 6659), 'TFORM4': (0x1f2e94, 4497)}
os.makedirs('resources/icons', exist_ok=True)
class R:
    def __init__(s, b): s.b = b; s.p = 0
    def u8(s): v = s.b[s.p]; s.p += 1; return v
    def ss(s): l = s.u8(); v = s.b[s.p:s.p + l]; s.p += l; return v.decode('cp1251')
    def n(s, k, f): v = struct.unpack_from(f, s.b, s.p)[0]; s.p += k; return v
def val(r):
    t = r.u8()
    if t == 1:
        out = []
        while r.b[r.p] != 0: out.append(val(r))
        r.p += 1; return out
    if t == 2: return r.n(1, '<b')
    if t == 3: return r.n(2, '<h')
    if t == 4: return r.n(4, '<i')
    if t == 5: r.p += 10; return None
    if t in (6, 7): return r.ss()
    if t in (8, 9): return t == 9
    if t in (10, 12, 20): l = r.n(4, '<I'); v = r.b[r.p:r.p + l]; r.p += l; return v
    if t == 11:
        while r.ss(): pass
        return None
    if t == 13: return None
    if t == 14:
        while r.b[r.p] != 0:
            if r.b[r.p] in (2, 3, 4): val(r)
            r.u8()
            while True:
                nm = r.ss()
                if not nm: break
                val(r)
        r.p += 1; return None
    if t == 15: r.p += 4; return None
    if t in (16, 17, 19, 21): r.p += 8; return None
    if t == 18: l = r.n(4, '<I'); r.p += 2 * l; return None
    raise Exception(t)
def save_glyph(blob, num, path):
    bmp = blob[4:]  # TBitmap stream: size prefix + BMP file
    im = Image.open(io.BytesIO(bmp)).convert('RGBA')
    w = im.width // max(1, num)
    im = im.crop((0, 0, w, im.height))
    key = im.getpixel((0, im.height - 1))
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][:3] == key[:3]: px[x, y] = (0, 0, 0, 0)
    im.save(path)
def comp(r, form, hints):
    fl = r.b[r.p]
    if fl & 0xf0 == 0xf0:
        r.p += 1
        if fl & 2: val(r)
    cls = r.ss(); nm = r.ss(); props = {}
    while True:
        pn = r.ss()
        if not pn: break
        props[pn] = val(r)
    if 'Glyph.Data' in props:
        path = f'resources/icons/{form}_{nm}.png'
        save_glyph(props['Glyph.Data'], props.get('NumGlyphs', 1), path)
        hints.append((path, props.get('Hint', '')))
    if cls == 'TImageList' and 'Bitmap' in props:
        open(f're/{form}_{nm}.bin', 'wb').write(props['Bitmap'])
    while r.b[r.p] != 0: comp(r, form, hints)
    r.p += 1
for k, (rva, sz) in res.items():
    b = d[o(rva):o(rva) + sz]; r = R(b); r.p = 4; hints = []
    comp(r, k.replace('TFORM', 'Form'), hints)
    for p, h in hints: print(p, h)
# MAINICON: group icon (type 14) -> icon id 1 (type 3), 4264 bytes at 0x1e3cdc
grp = d[o(0x1f71ac):o(0x1f71ac) + 20]
cnt = struct.unpack_from('<H', grp, 4)[0]
w, h, cc, _, planes, bpp, size, iid = struct.unpack_from('<BBBBHHIH', grp, 6)
img = d[o(0x1e3cdc):o(0x1e3cdc) + 4264]
ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w, h, cc, 0, planes, bpp, size, 22) + img
open('resources/icons/app.ico', 'wb').write(ico)
Image.open('resources/icons/app.ico').convert('RGBA').save('resources/icons/app.png')
print('app icon', w, h, bpp)
