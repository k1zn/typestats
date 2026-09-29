"""Differential test bench: the original TypeStats.exe vs build/tsstat.exe.

For every .tsf file the original is started with the file on the command line; the bench waits for
the main window, reads ListView2 (main statistics), ListView1 (Пауза/Длительность/Клавиша), the text
and character styles of the RichEdit (Memo4), then selects a few ranges in the RichEdit and reads
ListView2/ListView1 again. The process is killed afterwards, so the original writes nothing to the
registry. The same is computed with tsstat and compared line by line.

Options of the original (pause, "Только текст", "Разбивать по паузам") come from
HKCU\\Software\\TypingStatistics[\\<Profile>] exactly as the original reads them (TRegIniFile,
0x41ab08): Pause (default 2000), TextOnly (1), SplitOnEnter (0).

    python re/scripts/diffstand.py [files...] [--sel N] [--seed S] [--no-styles] [--record]

Without files all tests/golden/*.tsf are used. --record saves what the original shows to
tests/golden/orig/<name>.json (used by the C++ tests, so they do not need the original).
Exit code 1 if anything differs.
"""
import argparse
import json
import os
import random
import struct
import subprocess
import sys
import time
import warnings
import winreg
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import win32remote as wr  # noqa: E402

warnings.filterwarnings("ignore")  # pywinauto: "32-bit application should be automated using 32-bit Python"
from pywinauto import Desktop  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = ROOT / "TypeStats.exe"
TSSTAT = ROOT / "build" / "tsstat.exe"
QT_BIN = Path(os.environ.get("QTDIR", r"C:\Users\kizn\Qt\6.8.3\mingw_64")) / "bin"
MINGW_BIN = Path(r"C:\Users\kizn\Qt\Tools\mingw1310_64\bin")

# RichEdit colours (COLORREF, 0x00BBGGRR) the original uses for TextStyle bits, see re/text_reconstruction.md.
BLUE, RED, GREEN = 0xFF0000, 0x0000FF, 0x008000


# ---------------------------------------------------------------- options

def read_options():
    def get(key, name, default):
        try:
            v, _ = winreg.QueryValueEx(key, name)
            return v
        except OSError:
            return default

    opts = {"Pause": 2000, "TextOnly": True, "SplitOnEnter": False}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\TypingStatistics") as k:
            profile = get(k, "Profile", "")
    except OSError:
        return opts
    section = r"Software\TypingStatistics" + ("\\" + profile if profile else "")
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, section) as k:
            # TRegIniFile stores everything as REG_SZ; ReadBool is "value <> 0".
            opts["Pause"] = int(get(k, "Pause", opts["Pause"]))
            opts["TextOnly"] = int(get(k, "TextOnly", 1)) != 0
            opts["SplitOnEnter"] = int(get(k, "SplitOnEnter", 0)) != 0
    except OSError:
        pass
    return opts


def tsstat_args(opts):
    a = ["--split", str(opts["Pause"])]
    if opts["TextOnly"]:
        a.append("--only-text")
    if opts["SplitOnEnter"]:
        a.append("--by-pauses")
    return a


# ---------------------------------------------------------------- port side

def tsstat(args):
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(QT_BIN), str(MINGW_BIN), env.get("PATH", "")])
    r = subprocess.run([str(TSSTAT)] + args, capture_output=True, env=env)
    if r.returncode != 0:
        raise RuntimeError(f"tsstat {args}: {r.stderr.decode('utf-8', 'replace')}")
    return r.stdout.decode("utf-8").replace("\r\n", "\n")


def ansi_view(s):
    """What an ANSI (cp1251) RichEdit shows for s: WideCharToMultiByte with best fit, e.g. U+2588 -> '-'.

    The original keeps the text model in WideStrings but its TRichEdit is ANSI, so characters outside
    cp1251 are replaced on screen. The port keeps them; compare through this projection.
    """
    import ctypes
    k = ctypes.windll.kernel32
    buf = ctypes.create_string_buffer(len(s) * 2 + 1)
    n = k.WideCharToMultiByte(1251, 0, s, len(s), buf, len(buf), None, None)
    return buf.raw[:n].decode("cp1251")


def port_rows(out):
    return [line.split("\t") for line in out.splitlines() if line]


def port_styles(text, runs_out):
    """Per character (colour, underline) of the port's runs; paragraph breaks are not compared."""
    st = [(None, False)] * len(text)
    for line in runs_out.splitlines():
        start, length, style = map(int, line.split("\t"))
        if style & (4 | 8):
            color = BLUE
        elif style & 0x10:
            color = GREEN
        elif style & 1:
            color = RED
        else:
            color = None
        for p in range(start, start + length):
            st[p] = (color, bool(style & 2))
    return st


# ---------------------------------------------------------------- original side

class Original:
    def __init__(self, tsf):
        # The original creates its journal <year>_<month>.tsj next to the exe at startup.
        self.journals = set(ORIGINAL.parent.glob("*.tsj"))
        self.proc = subprocess.Popen([str(ORIGINAL), str(tsf)])
        self.main = None
        deadline = time.time() + 20
        name = Path(tsf).name
        while time.time() < deadline:
            wins = Desktop(backend="win32").windows(class_name="TForm1", process=self.proc.pid)
            if wins and name in wins[0].window_text():
                self.main = wins[0]
                break
            time.sleep(0.2)
        if self.main is None:
            self.close()
            raise RuntimeError(f"original did not open {tsf}")
        kids = self.main.descendants()
        self.richedit = next(c.handle for c in kids if c.class_name() == "TRichEdit")
        self.lv2 = self.lv1 = None
        for c in kids:
            if c.class_name() == "TListView":
                # ListView2 is the upper one (main statistics), ListView1 is below it.
                if self.lv2 is None or c.rectangle().top < self.lv2.rectangle().top:
                    self.lv2, self.lv1 = c, self.lv2
                else:
                    self.lv1 = c
        self.lv2, self.lv1 = self.lv2.handle, self.lv1.handle
        # Wait until the statistics are filled in.
        while time.time() < deadline:
            rows = wr.listview_rows(self.lv2)
            if rows and all(len(r) > 1 and r[1] for r in rows):
                break
            time.sleep(0.2)
        time.sleep(0.3)
        self._crlf = None

    @property
    def crlf(self):
        """RichEdit positions per paragraph break: 2 (RichEdit 1.0, the TRichEdit of BCB6) or 1.

        Measurable only while the text has a break, and irrelevant until then.
        """
        if self._crlf is None:
            if "\r\n" not in wr.window_text(self.richedit):
                return 1
            self._crlf = self._crlf_width()
        return self._crlf

    def _crlf_width(self):
        # RichEdit 1.0 keeps CR LF as two characters: the native length equals WM_GETTEXT's.
        # (Not "select all": its end includes the final paragraph mark.)
        text = wr.window_text(self.richedit)
        with wr.RemoteBuffer(self.richedit, 0x100) as rb:
            rb.write(0, (8).to_bytes(4, "little") + (1200).to_bytes(4, "little"))  # GTL_NUMCHARS, UTF-16
            n = wr.send(self.richedit, 0x045F, rb.addr, 0)  # EM_GETTEXTLENGTHEX
        return 2 if n == len(text) else 1

    def text(self):
        return wr.window_text(self.richedit).replace("\r\n", "\n").replace("\r", "\n")

    def re_pos(self, text, pos):
        """Port text position (1 per paragraph break) -> RichEdit position."""
        return pos + text.count("\n", 0, pos) * (self.crlf - 1)

    def styles(self, text):
        n = len(text) + text.count("\n") * (self.crlf - 1)
        raw = wr.richedit_styles(self.richedit, n)
        out, p = [], 0
        for ch in text:
            c, u = raw[p]
            # black == auto; a paragraph mark has no visible format
            out.append((None, False) if ch == "\n" else (None if c == 0 else c, u))
            p += self.crlf if ch == "\n" else 1
        return out

    def select(self, text, start, length):
        """Selects in the RichEdit and waits for ListView2 and ListView1.

        The original refreshes the statistics in ApplicationEvents1Idle (flag DAT_0058e1d6 set by
        Memo4SelectionChange), and skips it if the range did not change. Sometimes the refresh does
        not come; then the caret is moved away and the selection set again.
        """
        a, b = self.re_pos(text, start), self.re_pos(text, start + length)
        for attempt in range(3):
            lists = lambda: (wr.listview_rows(self.lv2), wr.listview_rows(self.lv1))
            before = lists()
            wr.richedit_select(self.richedit, a, b)
            deadline = time.time() + 0.6
            cur = lists()
            while cur == before and time.time() < deadline:
                time.sleep(0.03)
                cur = lists()
            while True:  # stable for 0.1 s
                time.sleep(0.1)
                nxt = lists()
                if nxt == cur:
                    break
                cur = nxt
            if cur != before or attempt == 2:
                return
            # No refresh: maybe the range really is the same, maybe the event was lost. Detour and retry.
            detour = 0 if a > 0 else self.re_pos(text, len(text))
            wr.richedit_select(self.richedit, detour, detour)
            time.sleep(0.2)

    def _snapshot(self):
        return wr.window_text(self.richedit), wr.listview_rows(self.lv2)

    def _wait_stable(self, before):
        """Waits for Recalculate: the text or statistics change, then stay put."""
        deadline = time.time() + 5
        cur = self._snapshot()
        while cur == before and time.time() < deadline:
            time.sleep(0.15)
            cur = self._snapshot()
        while time.time() < deadline:
            time.sleep(0.3)
            nxt = self._snapshot()
            if nxt == cur:
                break
            cur = nxt

    def set_options(self, opts):
        """Sets "Только текст", "Разбивать по паузам" and the pause through the window.

        CheckBox2Click / Edit1KeyPress (Enter) save the preset (0x406a90) and call Recalculate.
        """
        BM_GETCHECK, BM_CLICK, WM_SETTEXT, WM_CHAR = 0x00F0, 0x00F5, 0x000C, 0x0102
        kids = self.main.descendants()
        boxes = {c.window_text(): c.handle for c in kids if c.class_name() == "TCheckBox"}
        for caption, want in (("Только текст", opts["TextOnly"]), ("Разбивать по паузам", opts["SplitOnEnter"])):
            h = boxes[caption]
            if bool(wr.send(h, BM_GETCHECK)) != want:
                before = self._snapshot()
                wr.send(h, BM_CLICK)
                self._wait_stable(before)
        edit = next(c.handle for c in kids if c.class_name() == "TEdit")
        if wr.window_text(edit) != str(opts["Pause"]):
            import ctypes
            before = self._snapshot()
            buf = ctypes.create_unicode_buffer(str(opts["Pause"]))
            wr.send(edit, WM_SETTEXT, 0, ctypes.addressof(buf))
            wr.send(edit, WM_CHAR, 13, 0)
            self._wait_stable(before)
        assert wr.window_text(edit) == str(opts["Pause"])

    def klav_view(self):
        """Klavogram bitmap width (px) and zoom (px/ms) from the original's memory.

        DAT_005b1348 -> klavogram; +0x6c zoom (float); +0x28 canvas object -> +0x28 TBitmap -> +0x1c width.
        """
        rd = lambda a, fmt: struct.unpack(fmt, wr.read_process(self.proc.pid, a, struct.calcsize(fmt)))[0]
        klav = rd(0x5B1348, "<I")
        bitmap = rd(rd(klav + 0x28, "<I") + 0x28, "<I")
        return {"klav_width": rd(bitmap + 0x1C, "<i"), "klav_zoom": rd(klav + 0x6C, "<f")}

    # Graph series objects (FUN_00403868): global pointer -> object, vector<float> at +0x24/+0x28.
    SERIES = {"pause": 0x5B1408, "curSpeed": 0x5B13EC, "medSpeed": 0x5B13F0, "classicSpeed": 0x5B13FC,
              "privSpeed": 0x5B1400, "curRhythm": 0x5B13F4, "medRhythm": 0x5B13F8, "arrhythmia": 0x5B140C,
              "finger": 0x5B1404}

    def series(self):
        """Raw float bits (hex) of every graph series, read from the original's memory."""
        rd = lambda a, n: wr.read_process(self.proc.pid, a, n)
        out = {}
        for name, glob in self.SERIES.items():
            obj, = struct.unpack("<I", rd(glob, 4))
            begin, end = struct.unpack("<II", rd(obj + 0x24, 8))
            raw = rd(begin, end - begin) if end > begin else b""
            out[name] = [f"{v:08x}" for v in struct.unpack(f"<{len(raw) // 4}I", raw)]
        return out

    def close(self):
        self.proc.kill()
        self.proc.wait()
        for j in set(ORIGINAL.parent.glob("*.tsj")) - self.journals:
            if j.stat().st_size == 0:  # an empty journal this run created
                j.unlink()


# ---------------------------------------------------------------- comparison

class Report:
    def __init__(self):
        self.failures = 0

    def rows(self, title, orig, port):
        bad = []
        for i in range(max(len(orig), len(port))):
            o = orig[i] if i < len(orig) else None
            p = port[i] if i < len(port) else None
            if o != p:
                bad.append(f"    {i:3}: orig={o}  port={p}")
        self._print(title, bad)

    def text(self, title, orig, port):
        bad = []
        if orig != port:
            ol, pl = orig.split("\n"), port.split("\n")
            for i in range(max(len(ol), len(pl))):
                o = ol[i] if i < len(ol) else None
                p = pl[i] if i < len(pl) else None
                if o != p:
                    k = next((j for j in range(min(len(o or ""), len(p or ""))) if o[j] != p[j]),
                             min(len(o or ""), len(p or "")))
                    bad.append(f"    para {i}, col {k}: orig={o[max(0, k - 20):k + 40]!r}  port={p[max(0, k - 20):k + 40]!r}"
                               if o is not None and p is not None else f"    para {i}: orig={o!r} port={p!r}")
        self._print(title, bad)

    def styles(self, title, text, orig, port):
        bad = []
        for i, (o, p) in enumerate(zip(orig, port)):
            if o != p:
                bad.append(f"    pos {i} {text[i]!r}: orig={o} port={p}")
        if len(orig) != len(port):
            bad.append(f"    length orig={len(orig)} port={len(port)}")
        self._print(title, bad[:30] + ([f"    ... {len(bad) - 30} more"] if len(bad) > 30 else []))

    def _print(self, title, bad):
        if bad:
            self.failures += 1
            print(f"  DIFF {title}")
            print("\n".join(bad))
        else:
            print(f"  ok   {title}")


def selections(text, args):
    rng = random.Random(args.seed + len(text))
    sels = [(0, len(text))]
    for _ in range(args.sel):
        s = rng.randrange(0, max(1, len(text) - 1))
        sels.append((s, rng.randrange(1, max(2, min(len(text) - s, 200)))))
    # Cursor without selection: the whole text, or with "Разбивать по паузам" the fragment around it.
    sels += [(rng.randrange(0, max(1, len(text))), 0) for _ in range(args.sel)] + [(0, 0), (len(text), 0)]
    return sels


def collect_variant(orig, opts, args):
    """Everything the original shows now (with options opts)."""
    text = orig.text()
    orig.select(text, 0, 0)
    rec = {"options": opts, "text": text, "stats": wr.listview_rows(orig.lv2),
           "lv1": wr.listview_rows(orig.lv1), "lv1_rows": wr.send(orig.lv1, 0x1028),  # LVM_GETCOUNTPERPAGE
           **orig.klav_view(), "series": orig.series(), "selections": []}
    if not args.no_styles:
        rec["styles"] = orig.styles(text)
    for s, n in selections(text, args):
        orig.select(text, s, n)
        rec["selections"].append({"start": s, "length": n, "stats": wr.listview_rows(orig.lv2),
                                  "lv1": wr.listview_rows(orig.lv1)})
    return rec


def compare_variant(tsf, rec, rep):
    """Compares a recorded variant against tsstat."""
    opts = rec["options"]
    base = tsstat_args(opts)
    tag = " ".join(base)
    text = rec["text"]
    port_text = ansi_view(tsstat(base + ["--text", str(tsf)]).rstrip("\n"))
    rep.rows(f"[{tag}] ListView2", rec["stats"], port_rows(tsstat(base + [str(tsf)])))
    rep.text(f"[{tag}] text", text, port_text)
    if rec.get("styles") and text == port_text:
        orig_st = [tuple(x) for x in rec["styles"]]
        rep.styles(f"[{tag}] styles", text, orig_st, port_styles(text, tsstat(base + ["--runs", str(tsf)])))
    for sel in rec["selections"]:
        s, n = sel["start"], sel["length"]
        port = port_rows(tsstat(base + ["--sel", str(s), str(n), str(tsf)]))
        rep.rows(f"[{tag}] ListView2 sel {s}+{n}", sel["stats"], port)


# (TextOnly, SplitOnEnter, Pause) tried by --variants in addition to the registry options.
VARIANTS = [(False, False, 2000), (True, False, 500), (True, True, 2000), (False, True, 500)]


def recorded_path(tsf):
    return ROOT / "tests" / "golden" / "orig" / (tsf.stem + ".json")


def run_file(tsf, opts, args, rep):
    """Runs the original (unless --offline), saves what it shows, compares it with the port."""
    print(f"== {tsf.name}")
    out = recorded_path(tsf)
    if args.offline:
        recs = json.loads(out.read_text(encoding="utf-8"))["variants"]
    else:
        recs = []
        snap = reg_snapshot()
        orig = Original(tsf)
        try:
            recs.append(collect_variant(orig, opts, args))
            if args.variants:
                for text_only, by_pauses, pause in VARIANTS:
                    v = {"Pause": pause, "TextOnly": text_only, "SplitOnEnter": by_pauses}
                    orig.set_options(v)
                    recs.append(collect_variant(orig, v, args))
        finally:
            orig.close()
            reg_restore(snap)  # before the next start: the original reads the preset on startup
        out.parent.mkdir(exist_ok=True)
        out.write_text(json.dumps({"variants": recs}, ensure_ascii=False, indent=1), encoding="utf-8")
    for rec in recs:
        compare_variant(tsf, rec, rep)


# ---------------------------------------------------------------- registry guard

REG_ROOT = r"Software\TypingStatistics"


def reg_snapshot(path=REG_ROOT):
    """{subkey path: {value name: (data, type)}} for the key and all its subkeys."""
    snap = {}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as k:
            n_keys, n_vals, _ = winreg.QueryInfoKey(k)
            snap[path] = {winreg.EnumValue(k, i)[0]: winreg.EnumValue(k, i)[1:] for i in range(n_vals)}
            subs = [winreg.EnumKey(k, i) for i in range(n_keys)]
    except FileNotFoundError:
        return snap
    for s in subs:
        snap.update(reg_snapshot(path + "\\" + s))
    return snap


def reg_restore(snap):
    now = reg_snapshot()
    for path in sorted(set(now) - set(snap), key=len, reverse=True):
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
    for path, vals in snap.items():
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as k:
            for name in set(now.get(path, {})) - set(vals):
                winreg.DeleteValue(k, name)
            for name, (data, typ) in vals.items():
                if now.get(path, {}).get(name) != (data, typ):
                    winreg.SetValueEx(k, name, 0, typ, data)
    assert reg_snapshot() == snap, "registry was not restored"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--sel", type=int, default=4, help="random selections per file")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-styles", action="store_true")
    ap.add_argument("--offline", action="store_true",
                    help="do not run the original, compare with tests/golden/orig/<name>.json recorded earlier")
    ap.add_argument("--variants", action="store_true",
                    help="also switch options in the original's window (it saves them to the registry at once; "
                         "the key is snapshotted and restored afterwards)")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    files = [Path(f).resolve() for f in args.files] or sorted((ROOT / "tests" / "golden").glob("*.tsf"))
    opts = read_options()
    print("options:", opts, "->", " ".join(tsstat_args(opts)))
    rep = Report()
    for f in files:
        run_file(f, opts, args, rep)
    print(f"\n{rep.failures} section(s) differ")
    sys.exit(1 if rep.failures else 0)


if __name__ == "__main__":
    main()
