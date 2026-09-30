"""Screenshots of the original's main window for the UI stage.

    python re/scripts/screenshot_orig.py <file.tsf> <out dir> [SpeedButtonN ...]

Saves <out dir>/main.png, then clicks every named Form1 speed button in turn and saves main_<button>.png.
The registry preset is restored afterwards (as the bench does).
"""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import diffstand as ds  # noqa: E402
import win32remote as wr  # noqa: E402
from PIL import ImageGrab  # noqa: E402


def main():
    tsf, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    fields = {name: off for off, name in
              json.load(open(ds.ROOT / "re" / "vcl_symbols.json", encoding="utf-8"))["classes"]["TForm1"]["fields"]}
    snap = ds.reg_snapshot()
    orig = ds.Original(tsf)
    try:
        def shot(name):
            wr.user32.SetForegroundWindow(orig.main.handle)
            time.sleep(0.5)
            r = orig.main.rectangle()
            ImageGrab.grab((r.left, r.top, r.right, r.bottom)).save(out / name)
            print(name, r.width(), r.height())

        shot("main.png")
        for button in sys.argv[3:]:
            orig.click_speedbutton(0x5B09FC, fields[button])
            shot(f"main_{button}.png")
    finally:
        orig.close()
        ds.reg_restore(snap)


if __name__ == "__main__":
    main()
