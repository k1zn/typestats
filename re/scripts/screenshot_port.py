"""Screenshot of the port's main window: python re/scripts/screenshot_port.py <file.tsf> <out.png> [sel_start sel_len]"""
import os
import subprocess
import sys
import time
import warnings
from pathlib import Path

warnings.filterwarnings("ignore")
from pywinauto import Desktop  # noqa: E402
from PIL import ImageGrab  # noqa: E402
import ctypes  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
QT_BIN = Path(os.environ.get("QTDIR", r"C:\Users\kizn\Qt\6.8.3\mingw_64")) / "bin"
MINGW_BIN = Path(r"C:\Users\kizn\Qt\Tools\mingw1310_64\bin")

env = dict(os.environ)
env["PATH"] = os.pathsep.join([str(QT_BIN), str(MINGW_BIN), env.get("PATH", "")])
proc = subprocess.Popen([str(ROOT / "build" / "TypingStatistics.exe")] + sys.argv[1:2], env=env)
try:
    deadline = time.time() + 20
    win = None
    while time.time() < deadline and win is None:
        wins = [w for w in Desktop(backend="win32").windows(process=proc.pid) if w.is_visible() and w.window_text()]
        win = wins[0] if wins else None
        time.sleep(0.3)
    assert win, "no window"
    time.sleep(1.0)
    ctypes.windll.user32.SetForegroundWindow(win.handle)
    time.sleep(0.7)
    r = win.rectangle()
    ImageGrab.grab((r.left, r.top, r.right, r.bottom)).save(sys.argv[2])
    print(win.window_text(), r.width(), r.height())
finally:
    proc.kill()
