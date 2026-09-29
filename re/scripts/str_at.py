"""Print C strings at given VAs: python re/scripts/str_at.py 0x590277 0x590282 ..."""
import struct, sys
sys.stdout.reconfigure(encoding='utf-8')
d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; base = struct.unpack_from('<I', d, pe + 52)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]
def off(va):
    for vs, rva, rs, ro in secs:
        if base + rva <= va < base + rva + max(vs, rs): return va - base - rva + ro
for a in sys.argv[1:]:
    va = int(a, 16); o = off(va); e = d.index(b'\0', o)
    print(a, repr(d[o:e].decode('cp1251')), d[o:o+16].hex(' '))
