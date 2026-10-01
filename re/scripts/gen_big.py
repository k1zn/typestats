"""Large recordings for the performance bench (re/perf.md).

The golden recordings (824.tsf - digits, обыка.tsf - text) are copied one after another until the
wanted number of records is reached. Every copy starts after a pause longer than the split pause
(a new fragment) with an auto-comment; inside the copies, now and then, a character is followed by
BackSpace and typed again, a key gets a "(…)" comment and a short pause becomes a split pause.

    python re/scripts/gen_big.py OUTDIR [--sizes 10000,100000,500000] [--seed 1] [--sources 824.tsf,обыка.tsf]
                                        [--prefix big]

Writes OUTDIR/<prefix>_<n>.tsf (unsigned) and OUTDIR/<prefix>_<n>.tsj (a journal of the same records).
`--sources 824.tsf --prefix digits` gives a recording without some finger tracks (the klavogram's worst case).
"""
import argparse
import os
import random
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GOLDEN = os.path.join(ROOT, "tests", "golden")

BS_DOWN = (0x0008, 0x0108000E)  # ch, flags: BackSpace press as the hook records it
BS_UP = (0x0000, 0x0008A00E)
KEY_UP = 0x2000


def read_tsf(path):
    """[(dt, flags, ch, comment)] of the data lines."""
    recs = []
    with open(path, "rb") as f:
        text = f.read().decode("cp1251")
    for line in text.splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2 or "=" in line.split("\t")[0]:
            continue
        try:
            dt = int(parts[0], 16)
            packed = int(parts[1].split()[0], 16)
        except ValueError:
            continue
        comment = ""
        if ";" in line:
            comment = line[line.index(";") + 1:]
        recs.append((dt, packed & 0xFFFFFFFF, (packed >> 32) & 0xFFFF, comment))
    return recs


def generate(n, sources, rnd):
    out = []
    copy = 0
    while len(out) < n:
        src = sources[copy % len(sources)]
        copy += 1
        for i, (dt, flags, ch, comment) in enumerate(src):
            if i == 0:
                # A new fragment, after a pause of 3..30 s, with an auto-comment.
                dt = rnd.randint(3_000_000, 30_000_000)
                comment = "01.10.2026 12:%02d:%02d Блокнот - копия %d" % (copy % 60, i % 60, copy)
            elif rnd.random() < 0.002:
                dt += rnd.randint(2_500_000, 8_000_000)  # a split pause inside the copy
            if comment == "" and rnd.random() < 0.001 and not flags & KEY_UP:
                comment = "(метка %d)" % len(out)
            out.append((dt, flags, ch, comment))
            # A typo: the character is erased and typed again.
            if not flags & KEY_UP and flags & 0x01000000 and ch >= 0x20 and rnd.random() < 0.03:
                out.append((rnd.randint(60_000, 200_000), BS_DOWN[1], BS_DOWN[0], ""))
                out.append((rnd.randint(50_000, 120_000), BS_UP[1], BS_UP[0], ""))
                if rnd.random() < 0.3:  # sometimes two
                    out.append((rnd.randint(60_000, 200_000), BS_DOWN[1], BS_DOWN[0], ""))
                    out.append((rnd.randint(50_000, 120_000), BS_UP[1], BS_UP[0], ""))
            if len(out) >= n:
                break
    return out[:n]


def write_tsf(path, recs):
    lines = []
    for dt, flags, ch, comment in recs:
        line = "%08X %012X" % (dt, (ch << 32) | flags)
        if comment:
            line += "\t;" + comment
        lines.append(line)
    lines += ["tsfVersion=1", "autor=perf", "date=01.10.2026 12:00:00"]
    with open(path, "wb") as f:
        f.write(("\r\n".join(lines) + "\r\n").encode("cp1251"))


def write_tsj(path, recs):
    with open(path, "wb") as f:
        for dt, flags, ch, comment in recs:
            c = comment.encode("cp1251")[:255]
            f.write(struct.pack("<IIIB", dt ^ 0x554973, flags, ch, len(c)) + c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--sizes", default="10000,100000,500000")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sources", default="824.tsf,обыка.tsf", help="golden recordings to copy")
    ap.add_argument("--prefix", default="big")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    sources = [read_tsf(os.path.join(GOLDEN, name)) for name in a.sources.split(",")]
    for size in (int(s) for s in a.sizes.split(",")):
        recs = generate(size, sources, random.Random(a.seed))
        name = "%s_%d" % (a.prefix, size)
        write_tsf(os.path.join(a.outdir, name + ".tsf"), recs)
        write_tsj(os.path.join(a.outdir, name + ".tsj"), recs)
        print("%s: %d records" % (name, len(recs)))


if __name__ == "__main__":
    main()
