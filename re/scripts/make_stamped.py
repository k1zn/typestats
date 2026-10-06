"""Golden files for the time stamps of a recording (re/stamps.md): a recording "typed" in real time (no input is
sent anywhere: the records are made up here, on the clock) and stamped by the real authorities as the program does,
written independently of the program's code.

    python re/scripts/make_stamped.py tests/golden/stamps/stamped.tsf
    python re/scripts/make_stamped.py tests/golden/stamps/compressed.tsf --compress 0.85

--compress: the records claim a faster typing than the real one (as a modified program would): the stamps then do
not confirm the time. Needs openssl in PATH (Git Bash has it) and the network; ~30 s.
"""
import argparse
import hashlib
import os
import random
import struct
import subprocess
import tempfile
import time
import urllib.request

SERVICES = [
    ("DigiCert", "http://timestamp.digicert.com"),
    ("Sectigo", "http://timestamp.sectigo.com"),
    ("GlobalSign", "http://timestamp.globalsign.com/tsa/r6advanced1"),
]
INTERVAL = 10.0  # Stamps::kIntervalMs
IDLE = 2.0       # Stamps::kIdleMs
HAS_CHAR, KEY_UP = 0x01000000, 0x2000
SCAN = {c: s for c, s in zip("qwertyuiopasdfghjklzxcvbnm", [
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32])}
SCAN[" "] = 0x39


def flags_of(c, up):
    vk = 0x20 if c == " " else ord(c.upper())
    return (vk << 16) | SCAN[c] | HAS_CHAR | (KEY_UP if up else 0)


def stamp_hash(previous, length, delay_ms, encoded):
    """Stamps::chunkHash: magic, len(previous), previous, count, delay, then dt/flags/ch of each record."""
    h = hashlib.sha256()
    h.update(b"TypingStatistics stamps 1")
    h.update(bytes([len(previous)]))
    h.update(previous)
    h.update(struct.pack("<II", length, delay_ms))
    h.update(encoded)
    return h.digest()


def encode(dt, flags, ch):
    marks = 0x100 | 0x200 | 0x40000000  # the marks that may be put later
    return struct.pack("<III", dt, flags & ~marks & 0xFFFFFFFF, ch)


def request_stamp(digest, workdir, service):
    query = os.path.join(workdir, "q.tsq")
    subprocess.run(["openssl", "ts", "-query", "-digest", digest.hex(), "-sha256", "-cert", "-out", query],
                   check=True, capture_output=True)
    with open(query, "rb") as f:
        body = f.read()
    req = urllib.request.Request(service[1], data=body, headers={"Content-Type": "application/timestamp-query"})
    with urllib.request.urlopen(req, timeout=15) as r:
        reply = r.read()
    path = os.path.join(workdir, "r.tsr")
    with open(path, "wb") as f:
        f.write(reply)
    token = os.path.join(workdir, "t.der")
    subprocess.run(["openssl", "ts", "-reply", "-in", path, "-token_out", "-out", token], check=True,
                   capture_output=True)
    with open(token, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--compress", type=float, default=1.0)
    args = ap.parse_args()
    rng = random.Random(1)
    text = "the quick brown fox jumps over the lazy dog "
    # Planned events: (time, char, up), a pause of 3 s after the first sentence.
    plan, t = [], 0.3
    for i, c in enumerate(text * 2):
        if i == len(text):
            t += 3.0
        hold = rng.uniform(0.06, 0.12)
        plan.append((t, c, False))
        plan.append((t + hold, c, True))
        t += rng.uniform(0.09, 0.26)
    plan.sort()

    workdir = tempfile.mkdtemp()
    start = time.perf_counter()
    records = []  # (dt µs as written, flags, ch)
    times = []    # real time of each record
    encoded = b""
    stamps = []   # (end, delay ms, token)
    previous = b""
    stamped_end = 0
    last_stamp_record_time = None
    service = 0

    def stamp():
        nonlocal previous, stamped_end, last_stamp_record_time, service
        end = len(records)
        delay = int((time.perf_counter() - times[-1]) * 1000)
        digest = stamp_hash(previous, end - stamped_end, delay, encoded[stamped_end * 12:end * 12])
        token = request_stamp(digest, workdir, SERVICES[service % len(SERVICES)])
        service += 1
        stamps.append((end, delay, token))
        previous, stamped_end, last_stamp_record_time = digest, end, times[-1]
        print(f"stamp {len(stamps)}: {end} records, delay {delay} ms, {SERVICES[(service - 1) % 3][0]}")

    for when, c, up in plan:
        wait = start + when - time.perf_counter()
        if wait > 0:
            time.sleep(wait)
        now = time.perf_counter()
        # The first record: dt 60 s, as the normalization makes it; then the real gaps, "compressed" if asked.
        dt = 60000000 if not records else int((now - times[-1]) * 1e6 * args.compress)
        flags = flags_of(c, up)
        records.append((dt, flags, ord(c)))
        times.append(now)
        encoded += encode(dt, flags, ord(c))
        if last_stamp_record_time is None or now - last_stamp_record_time >= INTERVAL:
            stamp()
    time.sleep(IDLE)
    stamp()  # the idle one

    lines = ["%08X %012X" % (dt, (ch << 32) | flags) for dt, flags, ch in records]
    lines += ["tsfVersion=1", "autor=make_stamped.py", "date=" + time.strftime("%d.%m.%Y %H:%M:%S")]
    lines += ["Stamp%d=%d %d 0 %s" % (i + 1, end, delay, __import__("base64").b64encode(token).decode())
              for i, (end, delay, token) in enumerate(stamps)]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="cp1251", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{args.out}: {len(records)} records, {len(stamps)} stamps")


if __name__ == "__main__":
    main()
