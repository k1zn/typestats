"""Disassemble TypeStats.exe: python re/scripts/disasm.py <start_va> <end_va|+len>
Annotates calls with names from re/vcl_symbols.json and re/rtl_names.json."""
import struct, sys, json, os
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
sys.stdout.reconfigure(encoding='utf-8')
d = open('TypeStats.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; base = struct.unpack_from('<I', d, pe + 52)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]
def off(va):
    for vs, rva, rs, ro in secs:
        if base + rva <= va < base + rva + max(vs, rs): return va - base - rva + ro
names = {int(k, 16): v for k, v in json.load(open('re/vcl_symbols.json', encoding='utf-8'))['functions'].items()}
if os.path.exists('re/rtl_names.json'):
    names.update({int(k, 16): v for k, v in json.load(open('re/rtl_names.json', encoding='utf-8')).items()})
start = int(sys.argv[1], 16)
end = start + int(sys.argv[2][1:], 16) if sys.argv[2].startswith('+') else int(sys.argv[2], 16)
md = Cs(CS_ARCH_X86, CS_MODE_32)
o = off(start)
for ins in md.disasm(d[o:o + (end - start)], start):
    ann = ''
    if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
        t = int(ins.op_str, 16); ann = '  ; ' + names.get(t, '')
    print(f'{ins.address:08x}  {ins.mnemonic:6} {ins.op_str}{ann}')
