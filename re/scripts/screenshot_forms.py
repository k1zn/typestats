"""Reference screenshots of the original's secondary forms (Form3, Form4).

    python re/scripts/screenshot_forms.py <file.tsf> <out dir>

Saves form3.png, form3_avg.png, form4_keys.png, form4_key.png, form4_pair.png, form4_fingers.png, form4_finger.png,
form4_extra.png. The windows are captured with PrintWindow, wherever they are on the screen.
The registry preset is restored afterwards (as the bench does).
"""
import ctypes
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import diffstand as ds  # noqa: E402
import win32remote as wr  # noqa: E402
from PIL import Image  # noqa: E402


def print_window(hwnd, path):
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    user32.GetWindowDC.restype = gdi32.CreateCompatibleDC.restype = gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
    gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    rect = (ctypes.c_long * 4)()
    user32.GetWindowRect(hwnd, rect)
    w, h = rect[2] - rect[0], rect[3] - rect[1]
    wdc = user32.GetWindowDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(ctypes.c_void_p(wdc))
    bmp = gdi32.CreateCompatibleBitmap(ctypes.c_void_p(wdc), w, h)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, ctypes.c_void_p(mdc), 2)  # PW_RENDERFULLCONTENT
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetBitmapBits(ctypes.c_void_p(bmp), len(buf), buf)
    Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB").save(path)
    print(Path(path).name, w, h)


def main():
    tsf, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    snap = ds.reg_snapshot()
    orig = ds.Original(tsf)
    try:
        def shot(hwnd, name):
            time.sleep(0.5)
            print_window(hwnd, out / name)

        orig.open_extra()
        shot(orig.form3, "form3.png")
        orig.set_check(orig.x_avg, True)
        shot(orig.form3, "form3_avg.png")
        orig.set_check(orig.x_avg, False)

        orig.open_hist()
        shot(orig.form4, "form4_keys.png")
        orig.hist_drill(0)
        shot(orig.form4, "form4_key.png")
        orig.hist_drill(0)
        shot(orig.form4, "form4_pair.png")
        orig.hist_button("fingers")
        shot(orig.form4, "form4_fingers.png")
        orig.hist_drill(3)
        shot(orig.form4, "form4_finger.png")
        orig.hist_button("extra")
        shot(orig.form4, "form4_extra.png")
    finally:
        orig.close()
        ds.reg_restore(snap)


if __name__ == "__main__":
    main()
