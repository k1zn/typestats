"""Screenshot of the port's main window: python re/scripts/screenshot_port.py <file.tsf> <out.png> [--window TITLE] [application arguments]

Application arguments: e.g. `--lang en`, `--show settings` (opens that form). `--window TITLE` captures the window
whose title contains TITLE instead of the main one."""
import os
import subprocess
import sys
import time
import warnings
from pathlib import Path

warnings.filterwarnings("ignore")
from pywinauto import Desktop  # noqa: E402
from PIL import Image  # noqa: E402
import ctypes  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
QT_BIN = Path(os.environ.get("QTDIR", r"C:\Users\kizn\Qt\6.8.3\mingw_64")) / "bin"
MINGW_BIN = Path(r"C:\Users\kizn\Qt\Tools\mingw1310_64\bin")

title = None
if "--window" in sys.argv:
    i = sys.argv.index("--window")
    title = sys.argv[i + 1]
    del sys.argv[i:i + 2]

env = dict(os.environ)
env["PATH"] = os.pathsep.join([str(QT_BIN), str(MINGW_BIN), env.get("PATH", "")])
proc = subprocess.Popen([str(ROOT / "build" / "TypingStatistics.exe")] + sys.argv[1:2] + sys.argv[3:], env=env)
try:
    deadline = time.time() + 20
    win = None
    while time.time() < deadline and win is None:
        wins = [w for w in Desktop(backend="win32").windows(process=proc.pid) if w.is_visible() and w.window_text()
                and (title is None or title in w.window_text())]
        win = wins[0] if wins else None
        time.sleep(0.3)
    assert win, "no window"
    time.sleep(1.0)
    # PrintWindow: the window is captured wherever it is in the Z order and the focus stays with the user.
    r = win.rectangle()
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    user32.GetWindowDC.restype = gdi32.CreateCompatibleDC.restype = gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
    gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    wdc = user32.GetWindowDC(win.handle)
    mdc = gdi32.CreateCompatibleDC(ctypes.c_void_p(wdc))
    bmp = gdi32.CreateCompatibleBitmap(ctypes.c_void_p(wdc), r.width(), r.height())
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(win.handle, ctypes.c_void_p(mdc), 2)  # PW_RENDERFULLCONTENT
    buf = ctypes.create_string_buffer(r.width() * r.height() * 4)
    gdi32.GetBitmapBits(ctypes.c_void_p(bmp), len(buf), buf)
    Image.frombuffer("RGBA", (r.width(), r.height()), buf, "raw", "BGRA", 0, 1).convert("RGB").save(sys.argv[2])
    print(r.width(), r.height())
finally:
    proc.kill()
