"""Golden files for the time stamps of a recording (re/stamps.md): a recording "typed" in real time (no input is
sent anywhere: the records are made up here, on the clock) and stamped by the real authorities as the program does,
written independently of the program's code.

    python re/scripts/make_stamped.py tests/golden/stamps/stamped.tsf
    python re/scripts/make_stamped.py tests/golden/stamps/compressed.tsf --compress 0.85
    python re/scripts/make_stamped.py tests/golden/stamps/more.tsf --services Certum,SwissSign,Microsoft

    python re/scripts/make_stamped.py tests/golden/stamps/video.tsf --v3 --video
    python re/scripts/make_stamped.py tests/golden/stamps/video_late.tsf --v3 --video --video-shift 5

--compress: the records claim a faster typing than the real one (as a modified program would): the stamps then do
not confirm the time. --v3: the chain v3 (salted records in leaves of 8, re/stamps.md); --video: with a webcam clip
(re/webcam.md) of made-up packets, 10 a second, a key one every 4 s, timed in the document's time; --video-shift S:
the packets claim to be S seconds later than they are (a modified program dating video after the stamps). Needs
openssl in PATH (Git Bash has it) and the network; ~30 s.
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
    ("Certum", "http://time.certum.pl"),
    ("SwissSign", "http://tsa.swisssign.net"),
    ("Microsoft", "http://timestamp.acs.microsoft.com"),
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


def leaf(count, duration, group_hash):
    """Stamps::leaf: count, the sum of dt, the hash of the group."""
    return hashlib.sha256(bytes([count]) + struct.pack("<Q", duration) + group_hash).digest()


def salt(key, j):
    """Stamps::salt: the salt of record j of a chunk, from the stamp's key."""
    return hashlib.sha256(key + struct.pack("<I", j)).digest()[:16]


def records_hash(encoded, key):
    """Stamps::recordsHash: leaves of groups of 8 salted records from the first, hashed together."""
    leaves = b""
    for i in range(0, len(encoded), 12 * 8):
        group = encoded[i:i + 12 * 8]
        duration = sum(struct.unpack_from("<I", group, k)[0] for k in range(0, len(group), 12))
        commitments = b"".join(hashlib.sha256(salt(key, (i + k) // 12) + group[k:k + 12]).digest()
                               for k in range(0, len(group), 12))
        leaves += leaf(len(group) // 12, duration, hashlib.sha256(commitments).digest())
    return hashlib.sha256(leaves).digest()


def stamp_hash_v3(previous, length, delay_ms, encoded, key, packet_hashes):
    """Stamps::chunkHashV3."""
    h = hashlib.sha256()
    h.update(b"TypingStatistics stamps 3")
    h.update(bytes([len(previous)]))
    h.update(previous)
    h.update(struct.pack("<II", length, delay_ms))
    h.update(records_hash(encoded, key))
    h.update(struct.pack("<I", len(packet_hashes)))
    h.update(hashlib.sha256(b"".join(packet_hashes)).digest())
    return h.digest()


def packet_bytes(stream, key, pts, data):
    """MediaClip::packetBytes: stream, flags, pts, size, data."""
    return struct.pack("<BBqI", stream, 1 if key else 0, pts, len(data)) + data


def clip_bytes(packets, width=320, height=240):
    """MediaClip::serialize: a video stream (AV1), origin 0, everything visible, no shift."""
    out = b"TSMV" + bytes([1, 1]) + struct.pack("<BBHHB", 0, 0, width, height, 0)
    out += struct.pack("<qqi", 0, -(1 << 63), 0)
    return out + b"".join(packet_bytes(*p) for p in packets)


Z85 = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#"


def z85(data):
    data += b"\0" * (-len(data) % 4)
    out = []
    for i in range(0, len(data), 4):
        v = struct.unpack(">I", data[i:i + 4])[0]
        chars = []
        for _ in range(5):
            chars.append(Z85[v % 85])
            v //= 85
        out.append("".join(reversed(chars)))
    return "".join(out)


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


def read_records(path):
    """(dt, flags, ch, comment) of the data lines of a .tsf (version 1)."""
    out = []
    with open(path, encoding="cp1251") as f:
        for line in f.read().splitlines():
            data, _, comment = line.partition("\t;")
            parts = data.split()
            if len(parts) != 2 or "=" in data:
                continue
            try:
                dt, packed = int(parts[0], 16), int(parts[1], 16)
            except ValueError:
                continue
            out.append((dt, packed & 0xFFFFFFFF, packed >> 32, comment))
    return out


def normalized(records):
    """Recalc::normalized: no leading releases, dt of the first 60 s, auto-repeated modifiers dropped (their time to
    the next record), the in-memory marks cleared."""
    out, down, acc, started = [], set(), 0, False
    for dt, flags, ch, comment in records:
        up = bool(flags & KEY_UP)
        if not started:
            if up:
                continue
            started, acc = True, 60000000
        else:
            acc += dt
        vk = (flags >> 16) & 0xFF
        keep = True
        if up:
            down.discard(vk)
        else:
            keep = vk not in down or not (vk in (0x5B, 0x5C) or 0xA0 <= vk <= 0xA5)
            down.add(vk)
        if keep:
            out.append((acc & 0xFFFFFFFF, flags & ~(0x100 | 0x40000000), ch, comment))
            acc = 0
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--compress", type=float, default=1.0)
    ap.add_argument("--services", default="DigiCert,Sectigo,GlobalSign", help="the authorities, in turn")
    ap.add_argument("--replay", help="a .tsf to play again on the clock (its normalized records, its own timing)")
    ap.add_argument("--v3", action="store_true", help="the chain v3")
    ap.add_argument("--video", action="store_true", help="a webcam clip of made-up packets (needs --v3)")
    ap.add_argument("--video-shift", type=float, default=0.0, help="the packets claim to be this much later, s")
    args = ap.parse_args()
    services = [s for s in SERVICES if s[0] in args.services.split(",")]
    # Planned events: (time, flags, char, dt as written or None - the real one, comment).
    plan = []
    if args.replay:
        t = 0.3
        for i, (dt, flags, ch, comment) in enumerate(normalized(read_records(args.replay))):
            if i:
                t += dt / 1e6
            plan.append((t, flags, ch, dt, comment))
    else:
        rng = random.Random(1)
        text = "the quick brown fox jumps over the lazy dog "
        t = 0.3  # a pause of 3 s after the first sentence
        for i, c in enumerate(text * 2):
            if i == len(text):
                t += 3.0
            hold = rng.uniform(0.06, 0.12)
            plan.append((t, flags_of(c, False), ord(c), None, ""))
            plan.append((t + hold, flags_of(c, True), ord(c), None, ""))
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
    rng_video = random.Random(2)
    packets = []        # (stream, key, pts, data) of the clip, as the encoder would give them
    next_frame = 0.0    # the clock of the camera, s from the start
    stamped_packets = 0

    def frames_until(now):
        """The camera's frames up to now: 10 a second, a key one every 4 s, in the document's time (the first record
        is at 60 s of it: its dt)."""
        nonlocal next_frame
        while args.video and times and start + next_frame <= now:
            pts = 60000000 + int((start + next_frame - times[0]) * 1e6) + int(args.video_shift * 1e6)
            key = len(packets) % 40 == 0
            size = rng_video.randint(2000, 4000) if key else rng_video.randint(50, 600)
            packets.append((0, key, pts, bytes(rng_video.getrandbits(8) for _ in range(size))))
            next_frame += 0.1

    def stamp():
        nonlocal previous, stamped_end, last_stamp_record_time, service, stamped_packets
        end = len(records)
        now = time.perf_counter()
        frames_until(now)
        delay = int((now - times[-1]) * 1000)
        media_end = len(packets)
        key = os.urandom(16)
        if args.v3:
            hashes = [hashlib.sha256(packet_bytes(*p)).digest() for p in packets[stamped_packets:media_end]]
            digest = stamp_hash_v3(previous, end - stamped_end, delay, encoded[stamped_end * 12:end * 12], key,
                                   hashes)
        else:
            digest = stamp_hash(previous, end - stamped_end, delay, encoded[stamped_end * 12:end * 12])
        token = request_stamp(digest, workdir, services[service % len(services)])
        service += 1
        stamps.append((end, delay, token, media_end, key))
        stamped_packets = media_end
        previous, stamped_end, last_stamp_record_time = digest, end, times[-1]
        print(f"stamp {len(stamps)}: {end} records, delay {delay} ms, {services[(service - 1) % len(services)][0]}")

    comments = []
    for when, flags, ch, written, comment in plan:
        wait = start + when - time.perf_counter()
        if wait > 0:
            time.sleep(wait)
        now = time.perf_counter()
        frames_until(now)
        # The first record: dt 60 s, as the normalization makes it; then the real gaps, "compressed" if asked
        # (a replayed record keeps its own dt: it comes at that time).
        if not records:
            dt = 60000000
        elif written is not None:
            dt = written
        else:
            dt = int((now - times[-1]) * 1e6 * args.compress)
        records.append((dt, flags, ch))
        comments.append(comment)
        times.append(now)
        encoded += encode(dt, flags, ch)
        if last_stamp_record_time is None or now - last_stamp_record_time >= INTERVAL:
            stamp()
    time.sleep(IDLE)
    stamp()  # the idle one

    lines = ["%08X %012X" % (dt, (ch << 32) | flags) + ("\t;" + comment if comment else "")
             for (dt, flags, ch), comment in zip(records, comments)]
    lines += ["tsfVersion=1", "autor=make_stamped.py", "date=" + time.strftime("%d.%m.%Y %H:%M:%S")]
    b64 = __import__("base64").b64encode
    if args.v3:  # flags: the version - 1, shifted past the "voided" bit
        lines += ["Stamp%d=%d %d 4 %s %d %s" % (i + 1, end, delay, b64(token).decode(), media_end, z85(key))
                  for i, (end, delay, token, media_end, key) in enumerate(stamps)]
    else:
        lines += ["Stamp%d=%d %d 0 %s" % (i + 1, end, delay, b64(token).decode())
                  for i, (end, delay, token, _, _) in enumerate(stamps)]
    if packets:
        clip = clip_bytes(packets)
        text = z85(clip)
        lines.append("Webcam=1 %d %s" % (len(clip), hashlib.sha256(clip).hexdigest()))
        lines += ["WebcamData%d=%s" % (n + 1, text[i:i + 65535]) for n, i in enumerate(range(0, len(text), 65535))]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="cp1251", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{args.out}: {len(records)} records, {len(stamps)} stamps, {len(packets)} packets")


if __name__ == "__main__":
    main()
