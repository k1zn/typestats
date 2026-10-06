"""A window as it is on the screen, without taking the focus.

PrintWindow draws a Qt window shifted by the invisible resize borders of Windows 10/11 (a black strip on the left,
the right edge cut off). Instead the window is put on top of the others without activation (the keyboard focus
and the user's typing stay where they are), moved fully onto the screen, and its visible bounds (DWM extended
frame bounds, without the invisible borders) are taken from the screen. The title bar shows as inactive.
"""
import ctypes
import ctypes.wintypes as wt
import time

from PIL import ImageGrab

_u32 = ctypes.windll.user32
_dwm = ctypes.windll.dwmapi
HWND_TOPMOST, HWND_NOTOPMOST = wt.HWND(-1), wt.HWND(-2)
SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE, SWP_SHOWWINDOW = 0x1, 0x2, 0x10, 0x40
DWMWA_EXTENDED_FRAME_BOUNDS = 9


def visible_rect(hwnd):
    r = wt.RECT()
    _dwm.DwmGetWindowAttribute(wt.HWND(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS, ctypes.byref(r), ctypes.sizeof(r))
    return r.left, r.top, r.right, r.bottom


def capture(hwnd, path, position=(40, 40)):
    """Saves the window `hwnd` to `path`; returns its size."""
    h = wt.HWND(hwnd)
    _u32.SetWindowPos(h, HWND_TOPMOST, position[0], position[1], 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)
    time.sleep(0.8)  # the move and the repaint
    try:
        box = visible_rect(hwnd)
        ImageGrab.grab(box, all_screens=True).save(path)
        return box[2] - box[0], box[3] - box[1]
    finally:
        _u32.SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)
