# -*- coding: utf-8 -*-
"""
Capture a top-level visible window for a Windows process (by PID) as a BMP.

Compatible with Python 3.2+ on Windows (ctypes + subprocess only).
"""

from __future__ import print_function

import ctypes
import subprocess
import time
from ctypes import wintypes

__all__ = [
    "PW_CLIENTONLY",
    "PW_RENDERFULLCONTENT",
    "find_visible_hwnd_for_pid",
    "get_window_client_size",
    "get_window_screen_rect",
    "capture_hwnd_to_bmp_file",
    "capture_hwnd_to_bmp_bytes",
    "capture_pid_window_to_bmp_file",
    "PopenWindowCapture",
]

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

PW_CLIENTONLY = 0x00000001
PW_RENDERFULLCONTENT = 0x00000002


class BITMAPFILEHEADER(ctypes.Structure):
    _fields_ = [
        ("bfType", wintypes.WORD),
        ("bfSize", wintypes.DWORD),
        ("bfReserved1", wintypes.WORD),
        ("bfReserved2", wintypes.WORD),
        ("bfOffBits", wintypes.DWORD),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


def _structure_bytes(obj):
    return ctypes.string_at(ctypes.addressof(obj), ctypes.sizeof(obj))


def find_visible_hwnd_for_pid(pid):
    """
    Return HWND of a visible top-level window owned by pid, or None.

    If several match, the first one reported by EnumWindows wins (same as
    stopping enumeration once a match is found).
    """
    target = [None]

    GetWindowThreadProcessId = user32.GetWindowThreadProcessId
    GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    GetWindowThreadProcessId.restype = wintypes.DWORD

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def enum_callback(hwnd, lparam):
        pid_found = wintypes.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(pid_found))
        if pid_found.value == pid and user32.IsWindowVisible(hwnd):
            target[0] = hwnd
            return False
        return True

    user32.EnumWindows(enum_callback, 0)
    return target[0]


def get_window_screen_rect(hwnd):
    """Return (left, top, right, bottom) in screen coordinates."""
    rect = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    return (rect.left, rect.top, rect.right, rect.bottom)


def get_window_client_size(hwnd):
    """Return (width, height) of the client area in pixels."""
    r = wintypes.RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(r)):
        raise ctypes.WinError(ctypes.get_last_error())
    return (r.right - r.left, r.bottom - r.top)


def capture_hwnd_to_bmp_bytes(hwnd, print_flags=PW_RENDERFULLCONTENT):
    """
    Render the window into a DIB and return the bytes of a .bmp file.

    print_flags: PW_RENDERFULLCONTENT (default), PW_CLIENTONLY, or 0.
    """
    rect = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        raise ValueError("window has non-positive size")

    hwnd_dc = user32.GetWindowDC(hwnd)
    if not hwnd_dc:
        raise ctypes.WinError(ctypes.get_last_error())
    mem_dc = None
    bmp = None
    try:
        mem_dc = gdi32.CreateCompatibleDC(hwnd_dc)
        if not mem_dc:
            raise ctypes.WinError(ctypes.get_last_error())
        bmp = gdi32.CreateCompatibleBitmap(hwnd_dc, width, height)
        if not bmp:
            raise ctypes.WinError(ctypes.get_last_error())
        old = gdi32.SelectObject(mem_dc, bmp)
        try:
            if not user32.PrintWindow(hwnd, mem_dc, print_flags):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            gdi32.SelectObject(mem_dc, old)

        bmpinfo = BITMAPINFOHEADER()
        bmpinfo.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmpinfo.biWidth = width
        bmpinfo.biHeight = height
        bmpinfo.biPlanes = 1
        bmpinfo.biBitCount = 24
        bmpinfo.biCompression = 0

        row_bytes = ((width * 3 + 3) // 4) * 4
        buf_size = row_bytes * height
        buffer = ctypes.create_string_buffer(buf_size)

        lines = gdi32.GetDIBits(
            mem_dc,
            bmp,
            0,
            height,
            buffer,
            ctypes.byref(bmpinfo),
            0,
        )
        if lines != height:
            raise ctypes.WinError(ctypes.get_last_error())

        bmpfile = BITMAPFILEHEADER()
        bmpfile.bfType = 0x4D42
        bmpfile.bfOffBits = ctypes.sizeof(BITMAPFILEHEADER) + ctypes.sizeof(
            BITMAPINFOHEADER
        )
        bmpfile.bfSize = bmpfile.bfOffBits + buf_size

        return _structure_bytes(bmpfile) + _structure_bytes(bmpinfo) + buffer.raw[:buf_size]
    finally:
        if bmp:
            gdi32.DeleteObject(bmp)
        if mem_dc:
            gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(hwnd, hwnd_dc)


def capture_hwnd_to_bmp_file(hwnd, path, print_flags=PW_RENDERFULLCONTENT):
    """Write a BMP screenshot of hwnd to path (string path)."""
    data = capture_hwnd_to_bmp_bytes(hwnd, print_flags=print_flags)
    with open(path, "wb") as f:
        f.write(data)


def capture_pid_window_to_bmp_file(pid, path, print_flags=PW_RENDERFULLCONTENT):
    """Find a visible window for pid and write BMP to path."""
    hwnd = find_visible_hwnd_for_pid(pid)
    if not hwnd:
        raise RuntimeError("no visible top-level window for pid {0}".format(pid))
    capture_hwnd_to_bmp_file(hwnd, path, print_flags=print_flags)


class PopenWindowCapture(object):
    """
    Launch a subprocess and capture its window once it appears.

    Example::

        cap = PopenWindowCapture([\"notepad.exe\"])
        cap.wait_for_window()
        cap.save_bmp(\"screenshot.bmp\")
        cap.terminate()
    """

    def __init__(self, args, **popen_kwargs):
        self.proc = subprocess.Popen(args, **popen_kwargs)
        self._hwnd = None

    @property
    def pid(self):
        return self.proc.pid

    def wait_for_window(self, timeout_sec=30.0, poll_interval_sec=0.05):
        """Block until find_visible_hwnd_for_pid succeeds or timeout."""
        deadline = time.time() + timeout_sec
        while time.time() < deadline:
            hwnd = find_visible_hwnd_for_pid(self.pid)
            if hwnd:
                self._hwnd = hwnd
                return hwnd
            time.sleep(poll_interval_sec)
        raise RuntimeError(
            "timeout waiting for visible window (pid {0})".format(self.pid)
        )

    @property
    def hwnd(self):
        if not self._hwnd:
            raise RuntimeError("call wait_for_window() first")
        return self._hwnd

    def bmp_bytes(self, print_flags=PW_RENDERFULLCONTENT):
        return capture_hwnd_to_bmp_bytes(self.hwnd, print_flags=print_flags)

    def save_bmp(self, path, print_flags=PW_RENDERFULLCONTENT):
        capture_hwnd_to_bmp_file(self.hwnd, path, print_flags=print_flags)

    def terminate(self):
        """Try to end the child process (ignores errors)."""
        try:
            self.proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    cap = PopenWindowCapture(["notepad.exe"])
    try:
        cap.wait_for_window(timeout_sec=10.0)
        out = "screenshot.bmp"
        cap.save_bmp(out)
        print("Saved {0}".format(out))
    finally:
        cap.terminate()
