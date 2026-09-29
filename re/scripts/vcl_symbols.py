"""Scan TypeStats.exe for every Delphi/BCB6 VMT and emit symbols for Ghidra:
- Class VMTs (vmt_<Class>), class name, parent
- Published methods (Class::Method)
- Published fields (Class.field offset/name)  -> struct members
- Property accessors from RTTI (Class::GetProp / SetProp, static ones)
- Virtual method table entries named by inheritance slot (Class::vN)
Output: re/vcl_symbols.json"""
import struct, sys, json
d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    s = pe + 24 + opt + i * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, s + 8)
    secs.append((base + va, ro, min(vs, rs) if rs else 0, d[s:s+8].rstrip(b'\0').decode()))
def off(va):
    for sva, ro, sz, _ in secs:
        if sva <= va < sva + sz: return va - sva + ro
    return None
def valid(va): return off(va) is not None
def u32(va): return struct.unpack_from('<I', d, off(va))[0]
def u16(va): return struct.unpack_from('<H', d, off(va))[0]
def sstr(va):
    o = off(va); return d[o + 1:o + 1 + d[o]].decode('cp1251')
text_lo, text_hi = base + 0x1000, base + 0x1000 + 1626112
vmts = {}
for sva, ro, sz, name in secs:
    if name not in ('.text', '.data'): continue
    for o in range(ro, ro + sz - 4, 4):
        v = struct.unpack_from('<I', d, o)[0]
        va = o - ro + sva
        if v == va + 76:
            try:
                nm = sstr(u32(v - 44))
                if nm and nm.isidentifier(): vmts[v] = nm
            except Exception: pass
out = {'classes': {}, 'functions': {}}
def addfn(addr, name):
    if text_lo <= addr < text_hi and addr not in out['functions']:
        out['functions'][addr] = name
for vmt, cls in vmts.items():
    info = {'vmt': vmt, 'parent': None, 'size': u32(vmt - 40), 'fields': [], 'props': []}
    par = u32(vmt - 36)
    if par and valid(par) and valid(u32(par)): info['parent'] = vmts.get(u32(par))
    # published methods
    mt = u32(vmt - 52)
    if mt and valid(mt):
        q = mt + 2
        for _ in range(u16(mt)):
            size = u16(q); addfn(u32(q + 2), f'{cls}::{sstr(q + 6)}'); q += size
    # published fields
    ft = u32(vmt - 56)
    if ft and valid(ft):
        n = u16(ft); q = ft + 6
        for _ in range(n):
            fo = u32(q); nm = sstr(q + 6); info['fields'].append((fo, nm)); q += 7 + len(nm.encode('cp1251'))
    # RTTI properties
    ti = u32(vmt - 60)
    if ti and valid(ti) and d[off(ti)] == 7:  # tkClass
        td = ti + 2 + d[off(ti) + 1]
        un = td + 10; pd = un + 1 + d[off(un)]
        q = pd + 2
        for _ in range(u16(pd)):
            gp, spp = u32(q + 4), u32(q + 8); nm = sstr(q + 26)
            info['props'].append(nm)
            if gp >> 24 not in (0xff, 0xfe) and gp: addfn(gp, f'{cls}::Get{nm}')
            if spp >> 24 not in (0xff, 0xfe) and spp: addfn(spp, f'{cls}::Set{nm}')
            q += 27 + len(nm.encode('cp1251'))
    out['classes'][cls] = info
# virtual methods: name by slot, attributing to the class that introduced the override
for vmt, cls in vmts.items():
    # count slots: until next known VMT-ish boundary; cap by first non-text pointer
    i = 0
    while True:
        a = vmt + 4 * i
        if not valid(a): break
        f = u32(a)
        if not (text_lo <= f < text_hi): break
        if i > 0 and (a + 76) in vmts: break
        par = out['classes'][cls]['parent']; pv = out['classes'][par]['vmt'] if par in out['classes'] else None
        same_as_parent = pv and valid(pv + 4*i) and u32(pv + 4*i) == f
        if not same_as_parent: addfn(f, f'{cls}::virt{i:02d}')
        i += 1
        if i > 400: break
# Delphi standard virtuals at negative offsets
neg = {-32: 'SafeCallException', -28: 'AfterConstruction', -24: 'BeforeDestruction', -20: 'Dispatch',
       -16: 'DefaultHandler', -12: 'NewInstance', -8: 'FreeInstance', -4: 'Destroy'}
for vmt, cls in vmts.items():
    for o, nm in neg.items():
        f = u32(vmt + o)
        if text_lo <= f < text_hi and f not in out['functions']: addfn(f, f'{cls}::{nm}')
out['functions'] = {hex(k): v for k, v in sorted(out['functions'].items())}
json.dump(out, open('re/vcl_symbols.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('vmts', len(vmts), 'functions', len(out['functions']))
