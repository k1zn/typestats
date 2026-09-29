"""Find VCL class VMTs in TypeStats.exe and dump published method tables
(event handler name -> address). BCB6/Delphi6 VMT layout."""
import struct, re, sys, json
sys.stdout.reconfigure(encoding='utf-8')
d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    s = pe + 24 + opt + i * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, s + 8)
    secs.append((base + va, ro, max(vs, rs)))
def off(va):
    for sva, ro, sz in secs:
        if sva <= va < sva + sz: return va - sva + ro
    return None
def u32(va): return struct.unpack_from('<I', d, off(va))[0]
def sstr(va):
    o = off(va); return d[o + 1:o + 1 + d[o]].decode('cp1251')
classes = sys.argv[1:] or ['TForm1','TForm2','TForm3','TForm4','TForm5','TForm6','TForm7','TForm8','TForm9','TForm10','Tkbd']
out = {}
for cls in classes:
    pat = bytes([len(cls)]) + cls.encode()
    for m in re.finditer(re.escape(pat), d):
        # look for pointer to this shortstring (vmtClassName at vmt-44)
        va_name = None
        for sva, ro, sz in secs:
            if ro <= m.start() < ro + sz: va_name = m.start() - ro + sva
        if va_name is None: continue
        for p in re.finditer(re.escape(struct.pack('<I', va_name)), d):
            # candidate: this pointer is at vmt-44
            for sva, ro, sz in secs:
                if ro <= p.start() < ro + sz: pva = p.start() - ro + sva
            vmt = pva + 44
            try:
                if u32(vmt - 76) != vmt: continue
            except Exception: continue
            mt = u32(vmt - 52); parent = u32(vmt - 36)
            meths = {}
            if mt:
                n = struct.unpack_from('<H', d, off(mt))[0]; q = mt + 2
                for _ in range(n):
                    size = struct.unpack_from('<H', d, off(q))[0]
                    code = u32(q + 2); name = sstr(q + 6)
                    meths[name] = hex(code); q += size
            out[cls] = {'vmt': hex(vmt), 'instance_size': u32(vmt - 40), 'methods': meths}
print(json.dumps(out, ensure_ascii=False, indent=1))
