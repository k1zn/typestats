"""Reading common controls of another (32-bit) process from 64-bit Python.

pywinauto's ListView/RichEdit wrappers lay out LVITEM etc. for the bitness of the running Python,
which breaks against the 32-bit TypeStats.exe. Here the structures are built by hand with 32-bit
layout in memory allocated inside the target process.
"""
import ctypes
import struct
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

user32.SendMessageW.restype = ctypes.c_ssize_t
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_ssize_t]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                        ctypes.POINTER(ctypes.c_size_t)]

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
LVM_GETITEMCOUNT = 0x1004
LVM_GETITEMTEXTW = 0x1073
HDM_GETITEMCOUNT = 0x1200
LVM_GETHEADER = 0x101F
EM_EXGETSEL = 0x0434
EM_EXSETSEL = 0x0437
EM_GETCHARFORMAT = 0x043A
EM_GETTEXTRANGE = 0x044B
EM_SETEVENTMASK = 0x0445
SCF_SELECTION = 1
CFM_UNDERLINE = 0x4
CFM_COLOR = 0x40000000
CFE_UNDERLINE = 0x4
CFE_AUTOCOLOR = 0x40000000


def send(hwnd, msg, wp=0, lp=0):
    return user32.SendMessageW(hwnd, msg, wp, lp)


class RemoteBuffer:
    """A block of memory inside the process that owns hwnd."""

    def __init__(self, hwnd, size=0x10000):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        self.proc = kernel32.OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, False, pid.value)
        if not self.proc:
            raise ctypes.WinError(ctypes.get_last_error())
        self.size = size
        self.addr = kernel32.VirtualAllocEx(self.proc, None, size, 0x3000, 0x04)
        if not self.addr:
            raise ctypes.WinError(ctypes.get_last_error())
        assert self.addr < 0x100000000, "target must be a 32-bit process"

    def write(self, offset, data: bytes):
        n = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(self.proc, self.addr + offset, data, len(data), ctypes.byref(n)):
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, offset, size) -> bytes:
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(self.proc, self.addr + offset, buf, size, ctypes.byref(n)):
            raise ctypes.WinError(ctypes.get_last_error())
        return buf.raw[:n.value]

    def close(self):
        if self.addr:
            kernel32.VirtualFreeEx(self.proc, self.addr, 0, 0x8000)
            self.addr = None
        kernel32.CloseHandle(self.proc)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def window_text(hwnd) -> str:
    """WM_GETTEXT is marshalled across processes by the system."""
    n = send(hwnd, WM_GETTEXTLENGTH)
    buf = ctypes.create_unicode_buffer(n + 1)
    send(hwnd, WM_GETTEXT, n + 1, ctypes.addressof(buf))
    return buf.value


def listview_rows(hwnd):
    """All rows of a report-mode ListView as lists of strings."""
    header = send(hwnd, LVM_GETHEADER)
    cols = max(1, send(header, HDM_GETITEMCOUNT)) if header else 1
    rows = []
    with RemoteBuffer(hwnd) as rb:
        text_off, text_len = 0x100, 0x1000
        for i in range(send(hwnd, LVM_GETITEMCOUNT)):
            row = []
            for c in range(cols):
                # LVITEMW, 32-bit: mask iItem iSubItem state stateMask pszText cchTextMax iImage lParam iIndent
                item = struct.pack("<IiiIIIiiiI", 1, i, c, 0, 0, rb.addr + text_off, text_len, 0, 0, 0)
                rb.write(0, item + b"\0" * 32)
                n = send(hwnd, LVM_GETITEMTEXTW, i, rb.addr)
                row.append(rb.read(text_off, n * 2).decode("utf-16-le") if n > 0 else "")
            rows.append(row)
    return rows


def richedit_select(hwnd, start, end):
    with RemoteBuffer(hwnd, 0x100) as rb:
        rb.write(0, struct.pack("<ii", start, end))
        send(hwnd, EM_EXSETSEL, 0, rb.addr)


def richedit_styles(hwnd, length):
    """(color, underline) of every character position 0..length-1; color is None for auto."""
    out = []
    # No EN_SELCHANGE while scanning: otherwise every step runs the owner's OnSelectionChange.
    old_mask = send(hwnd, EM_SETEVENTMASK, 0, 0)
    try:
        with RemoteBuffer(hwnd, 0x1000) as rb:
            for p in range(length):
                rb.write(0, struct.pack("<ii", p, p + 1))
                send(hwnd, EM_EXSETSEL, 0, rb.addr)
                # CHARFORMATA (60 bytes): cbSize dwMask dwEffects yHeight yOffset crTextColor ...
                rb.write(0x100, struct.pack("<II", 60, CFM_COLOR | CFM_UNDERLINE) + b"\0" * 52)
                send(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, rb.addr + 0x100)
                _, mask, eff, _, _, color = struct.unpack("<IIIiiI", rb.read(0x100, 24))
                out.append((None if eff & CFE_AUTOCOLOR else color, bool(eff & CFE_UNDERLINE)))
            rb.write(0, struct.pack("<ii", 0, 0))
            send(hwnd, EM_EXSETSEL, 0, rb.addr)
    finally:
        send(hwnd, EM_SETEVENTMASK, 0, old_mask)
    return out


def read_process(pid, addr, size) -> bytes:
    """Reads memory of another process (e.g. globals of the original at fixed addresses)."""
    proc = kernel32.OpenProcess(0x0010 | 0x0400, False, pid)
    if not proc:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(proc, addr, buf, size, ctypes.byref(n)):
            raise ctypes.WinError(ctypes.get_last_error())
        return buf.raw[:n.value]
    finally:
        kernel32.CloseHandle(proc)


def header_texts(listview):
    """Column captions of a report-mode ListView."""
    header = send(listview, LVM_GETHEADER)
    out = []
    with RemoteBuffer(header, 0x1000) as rb:
        for i in range(send(header, HDM_GETITEMCOUNT)):
            # HDITEMW, 32-bit: mask cxy pszText hbm cchTextMax fmt lParam iImage iOrder type pvFilter state
            rb.write(0, struct.pack("<IiIIiiiiiIII", 2, 0, rb.addr + 0x100, 0, 0x200, 0, 0, 0, 0, 0, 0, 0))
            rb.write(0x100, bytes(0x400))
            send(header, 0x120B, i, rb.addr)  # HDM_GETITEMW
            out.append(rb.read(0x100, 0x400).decode("utf-16-le").split(chr(0))[0])
    return out


def listview_column_click(listview, column):
    """LVN_COLUMNCLICK for a VCL list view.

    WM_NOTIFY is not delivered across processes, so the message VCL reflects to the control
    (CN_NOTIFY = CN_BASE + WM_NOTIFY) is sent to the list itself.
    """
    with RemoteBuffer(listview, 0x100) as rb:
        # NMLISTVIEW, 32-bit: hwndFrom idFrom code iItem iSubItem uNewState uOldState uChanged pt lParam
        rb.write(0, struct.pack("<IIiiiIIIiii", listview, 0, -108, -1, column, 0, 0, 0, 0, 0, 0))
        send(listview, 0xBC4E, 0, rb.addr)


def listview_select(listview, index):
    """Selects and focuses one row (LVM_SETITEMSTATE); None clears the selection."""
    with RemoteBuffer(listview, 0x100) as rb:
        clear = struct.pack("<IiiIIIiiiI", 8, 0, 0, 0, 3, 0, 0, 0, 0, 0)
        rb.write(0, clear)
        send(listview, 0x102B, (1 << 64) - 1, rb.addr)  # all rows
        if index is not None:
            rb.write(0, struct.pack("<IiiIIIiiiI", 8, index, 0, 3, 3, 0, 0, 0, 0, 0))
            send(listview, 0x102B, index, rb.addr)


def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    send(hwnd, 0x000C, 0, ctypes.addressof(buf))  # WM_SETTEXT
