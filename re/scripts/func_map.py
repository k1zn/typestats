"""Summarise decompiled functions: address, name, lines, referenced strings/globals, named calls."""
import re, glob, sys, json
sys.stdout.reconfigure(encoding='utf-8')
funcs = []
for f in sorted(glob.glob('re/decomp/range_*.c')):
    txt = open(f, encoding='utf-8').read()
    parts = re.split(r'\n// ===== ([0-9a-f]{8}) (\S+)\n', txt)
    for i in range(1, len(parts), 3):
        addr, name, body = parts[i], parts[i + 1], parts[i + 2]
        strs = sorted(set(re.findall(r'\b[su]_(\w+?)_00[0-9a-f]{6}', body)))
        calls = sorted(set(re.findall(r'\b(T\w+::\w+)\(', body)))
        apis = sorted(set(re.findall(r'\b([A-Z][a-z]+[A-Z]\w*[A-Za-z])\(', body)) - {c.split('::')[1] for c in calls})
        funcs.append((addr, name, body.count('\n'), strs, calls, apis))
for a, n, l, s, c, api in funcs:
    print(f'{a} {n:40} {l:5}  S:{",".join(s)[:150]}  C:{",".join(x.split("::")[1] for x in c)[:120]}  A:{",".join(api)[:100]}')
