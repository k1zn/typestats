"""Post-process Ghidra output in re/decomp/*.c -> re/decomp_clean/*.c

- Collapses inlined std::vector::push_back:
      if (END == CAP) { <realloc...> } else { <construct at END>; END = END + n; }
  becomes
      /* push_back */ { <construct at END>; END = END + n; }
- Drops exception-frame bookkeeping (`local_xx = local_xx + 1;` counters, `local_c0 = 0x14;`)
  and AnsiString destructor calls.
"""
import re, glob, os, sys

def match_brace(s, i):
    """s[i] == '{' -> index of matching '}'"""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '{': depth += 1
        elif s[j] == '}':
            depth -= 1
            if depth == 0: return j
    return -1

IF_RE = re.compile(r'if \(([^{};\n()]+|\*\(\w+ \*+\)\(\w+ \+ 0x[0-9a-f]+\)) == ([^{};\n()]+|\*\(\w+ \*+\)\(\w+ \+ 0x[0-9a-f]+\))\) \{')

def collapse_push_back(s):
    out = []
    pos = 0
    while True:
        m = IF_RE.search(s, pos)
        if not m:
            out.append(s[pos:]); break
        end_var = m.group(1)
        ob = m.end() - 1
        cb = match_brace(s, ob)
        rest = s[cb + 1:]
        em = re.match(r'\s*else \{', rest)
        if cb < 0 or not em:
            out.append(s[pos:m.end()]); pos = m.end(); continue
        eob = cb + 1 + em.end() - 1
        ecb = match_brace(s, eob)
        then_body = s[ob + 1:cb]
        else_body = s[eob + 1:ecb]
        grows = re.search(rf'(?<![\w*]){re.escape(end_var)} = {re.escape(end_var)} \+ \d+;', else_body) or \
                re.search(r'(\*\(int \*\)\(\w+ \+ 0x[0-9a-f]+\)) = \1 \+ (?:\d+|0x[0-9a-f]+);', else_body)
        if grows and ('FUN_00564a40' in then_body or 'FUN_00553660' in then_body or 'vec_' in then_body or 'realloc' in then_body):
            out.append(s[pos:m.start()])
            out.append('/* push_back */ {' + else_body + '}')
            pos = ecb + 1
        else:
            out.append(s[pos:m.end()]); pos = m.end()
    return ''.join(out)

NOISE = [
    re.compile(r'^\s*(?:FUN_00563bdc|AnsiString_dtor|FUN_00564424|WideString_dtor)\(.*\);\s*$'),
    re.compile(r'^\s*/\* WARNING: Read-only address.*\*/\s*$'),
]

INC_RE = re.compile(r'^\s*(local_[0-9a-f]+) = \1 [+-] -?\d+;\s*$')

def clean_function(body):
    # The exception-frame object counter is the variable incremented/decremented most often.
    counts = {}
    for l in body.split('\n'):
        m = INC_RE.match(l)
        if m: counts[m.group(1)] = counts.get(m.group(1), 0) + 1
    exc = max(counts, key=counts.get) if counts and max(counts.values()) >= 4 else None
    out = []
    for l in body.split('\n'):
        m = INC_RE.match(l)
        if m and m.group(1) == exc: continue
        if any(p.match(l) for p in NOISE): continue
        out.append(l)
    return '\n'.join(out)

def clean(text):
    text = collapse_push_back(text)
    parts = re.split(r'(\n// ===== [0-9a-f]{8} \S+\n)', text)
    return ''.join(clean_function(p) if i % 2 == 0 else p for i, p in enumerate(parts))

os.makedirs('re/decomp_clean', exist_ok=True)
for f in sorted(glob.glob('re/decomp/*.c')):
    src = open(f, encoding='utf-8').read()
    dst = clean(src)
    open(os.path.join('re/decomp_clean', os.path.basename(f)), 'w', encoding='utf-8').write(dst)
    print(f, src.count('\n'), '->', dst.count('\n'))
