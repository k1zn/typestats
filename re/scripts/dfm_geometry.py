"""Dump the geometry of every form (DFM in RCDATA) to re/forms_geometry.txt.

forms_dfm.txt has the properties without positions; this is the companion: one line per component,
indented by nesting, with Left/Top/Width/Height and the properties that matter for the layout.
"""
import struct
import sys

d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]
res_rva = struct.unpack_from('<I', d, pe + 24 + 96 + 2 * 8)[0]


def off(rva):
    for vs, va, rs, ro in secs:
        if va <= rva < va + max(vs, rs):
            return rva - va + ro
    raise ValueError(hex(rva))


RES = off(res_rva)


def entries(dir_off):
    named, ids = struct.unpack_from('<HH', d, RES + dir_off + 12)
    for i in range(named + ids):
        name, target = struct.unpack_from('<II', d, RES + dir_off + 16 + i * 8)
        if name & 0x80000000:
            p = RES + (name & 0x7fffffff)
            n = struct.unpack_from('<H', d, p)[0]
            name = d[p + 2:p + 2 + 2 * n].decode('utf-16-le')
        yield name, target


def rcdata():
    for typ, t in entries(0):
        if typ != 10:
            continue
        for name, t2 in entries(t & 0x7fffffff):
            for _lang, t3 in entries(t2 & 0x7fffffff):
                rva, size = struct.unpack_from('<II', d, RES + t3)
                yield name, d[off(rva):off(rva) + size]


class R:
    def __init__(s, b):
        s.b = b
        s.p = 0

    def u8(s):
        v = s.b[s.p]
        s.p += 1
        return v

    def ss(s):
        n = s.u8()
        v = s.b[s.p:s.p + n]
        s.p += n
        return v.decode('cp1251')

    def n(s, k, f):
        v = struct.unpack_from(f, s.b, s.p)[0]
        s.p += k
        return v


def val(r):
    t = r.u8()
    if t == 1:
        out = []
        while r.b[r.p] != 0:
            out.append(val(r))
        r.p += 1
        return out
    if t == 2:
        return r.n(1, '<b')
    if t == 3:
        return r.n(2, '<h')
    if t == 4:
        return r.n(4, '<i')
    if t == 5:
        r.p += 10
        return None
    if t in (6, 7):
        return r.ss()
    if t in (8, 9):
        return t == 9
    if t in (10, 12, 20):
        n = r.n(4, '<I')
        v = r.b[r.p:r.p + n]
        r.p += n
        return v.decode('utf-8', 'replace') if t == 20 else ('cp1251' and (v.decode('cp1251') if t == 12 else f'<{n} bytes>'))
    if t == 11:
        out = []
        while True:
            s = r.ss()
            if not s:
                return out
            out.append(s)
    if t == 13:
        return None
    if t == 14:
        items = []
        while r.b[r.p] != 0:
            if r.b[r.p] in (2, 3, 4):
                val(r)
            r.u8()
            item = {}
            while True:
                nm = r.ss()
                if not nm:
                    break
                item[nm] = val(r)
            items.append(item)
        r.p += 1
        return items
    if t == 15:
        r.p += 4
        return None
    if t in (16, 17, 19, 21):
        r.p += 8
        return None
    if t == 18:
        n = r.n(4, '<I')
        v = r.b[r.p:r.p + 2 * n]
        r.p += 2 * n
        return v.decode('utf-16-le')
    raise Exception(t)


KEEP = ('Align', 'Caption', 'Hint', 'Text', 'Visible', 'Enabled', 'BevelOuter', 'BevelInner', 'BorderStyle',
        'ClientWidth', 'ClientHeight', 'Columns', 'Font.Height', 'Font.Name', 'Font.Style', 'Font.Color', 'Color',
        'Shape', 'Anchors', 'Min', 'Max', 'Position', 'Items.Strings', 'Lines.Strings', 'ItemIndex', 'Kind',
        'Alignment', 'AutoSize', 'Layout', 'WordWrap', 'Flat', 'GroupIndex', 'AllowAllUp', 'Down', 'Checked',
        'BorderIcons', 'FormStyle', 'Position', 'Constraints.MinWidth', 'Constraints.MinHeight', 'TabOrder',
        'ScrollBars', 'ReadOnly', 'Transparent', 'Increment', 'Style', 'ItemHeight', 'DropDownCount', 'Cursor',
        'MinSize', 'Beveled', 'Orientation', 'Frequency', 'SimplePanel', 'State', 'Ctl3D', 'Stretch', 'Center')


def comp(r, out, depth):
    fl = r.b[r.p]
    if fl & 0xf0 == 0xf0:
        r.p += 1
        if fl & 2:
            val(r)
    cls = r.ss()
    nm = r.ss()
    props = {}
    while True:
        pn = r.ss()
        if not pn:
            break
        props[pn] = val(r)
    geo = ' '.join(str(props.get(k, '-')) for k in ('Left', 'Top', 'Width', 'Height'))
    rest = ' '.join(f'{k}={props[k]!r}' for k in KEEP if k in props)
    out.append(f"{'  ' * depth}{nm}: {cls} [{geo}] {rest}")
    while r.b[r.p] != 0:
        comp(r, out, depth + 1)
    r.p += 1


out = []
for name, blob in rcdata():
    if blob[:4] != b'TPF0':
        continue
    out.append(f'===== {name}')
    r = R(blob)
    r.p = 4
    comp(r, out, 0)
open('re/forms_geometry.txt', 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print(len(out), 'lines')
