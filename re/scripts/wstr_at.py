"""UTF-16 string literals of TypeStats.exe: python re/scripts/wstr_at.py <va>...   (str_at.py is for ANSI ones)
With `-f <start_va> <end_va>`: the literals loaded by `mov edx, imm` in that range, in code order."""
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8')
d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
base = struct.unpack_from('<I', d, pe + 52)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]


def off(va):
    for vs, rva, rs, ro in secs:
        if base + rva <= va < base + rva + max(vs, rs):
            return va - base - rva + ro


args = sys.argv[1:]
if args and args[0] == '-f':
    out = subprocess.run([sys.executable, 're/scripts/disasm.py', args[1], args[2]], capture_output=True, text=True,
                         encoding='utf-8').stdout
    args = [line.split()[-1] for line in out.splitlines() if 'mov    edx, 0x5' in line]
for a in args:
    o = e = off(int(a, 16))
    while d[e:e + 2] != b'\0\0':
        e += 2
    print(a, repr(d[o:e].decode('utf-16le', 'replace')), '| ansi:', repr(d[o:d.index(b'\0', o)].decode('cp1251', 'replace')))
