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
from wincapture import capture  # noqa: E402

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
# --no-capture: without the hook, or the typing of the user would be recorded into the window being captured.
proc = subprocess.Popen([str(ROOT / "build" / "TypingStatistics.exe"), "--no-capture"] + sys.argv[1:2] + sys.argv[3:], env=env)
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
    # On top without activation, from the screen (wincapture: PrintWindow shifts a Qt window by the invisible borders).
    w, h = capture(win.handle, sys.argv[2])
    print(w, h)
finally:
    proc.kill()
